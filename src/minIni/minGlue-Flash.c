/*  Glue functions for a flash (page) based storage system, without a file system.
 *
 *  By CompuPhase, 2008-2012
 *  This "glue file" is in the public domain. It is distributed without
 *  warranties or conditions of any kind, either express or implied.
 *
 */

#include "McuMinINIconfig.h" /* MinIni config file */

#if McuMinINI_CONFIG_FS==McuMinINI_CONFIG_FS_TYPE_FLASH_FS
#include "minGlue-Flash.h"
#include <stddef.h>
#include <string.h>
#include "McuLib.h"
#include "McuLog.h"
#include "McuUtility.h"
#include "McuFlash.h"

/* NOTE: we only support one 'file' in FLASH, and only one 'file' in RAM. The one in RAM is for the read-write and temporary one  */
/* read-only FLASH 'file' is at McuMinINI_CONFIG_FLASH_NVM_ADDR_START */
#if !McuMinINI_CONFIG_READ_ONLY
  static unsigned char dataBuf[McuMinINI_CONFIG_FLASH_NVM_MAX_DATA_SIZE]; /* ini file for read/write */
#endif

int ini_openread(const TCHAR *filename, INI_FILETYPE *file) {
  /* open file in read-only mode. This will use directly the data in FLASH */
  memset(file, 0, sizeof(INI_FILETYPE));
  file->header = (MinIniFlashFileHeader*)(McuMinINI_CONFIG_FLASH_NVM_ADDR_START);
  file->data = (unsigned char*)file->header + sizeof(MinIniFlashFileHeader);
  if (file->header->magicNumber != MININI_FLASH_MAGIC_DATA_NUMBER_ID) {
    return 0; /* failed, magic number does not match */
  }
  if (McuUtility_strcmp((char*)file->header->dataName, (char*)filename)!=0) {
    return 0; /* failed, not the file name of the storage */
  }
  file->curr = file->data;
  file->isOpen = true;
  file->isReadOnly = true;
  return 1; /* ok */
}

static bool isTempFile(const TCHAR *filename) {
  size_t len;

  len = McuUtility_strlen(filename);
  if (len==0) {
    return false; /* illegal file name */
  }
  if (filename[len-1]=='~') { /* temporary file name */
    return true;
  }
  return false;
}

#if !McuMinINI_CONFIG_READ_ONLY
int ini_openwrite(const TCHAR *filename, INI_FILETYPE *file) {
  /* create always a new file */
  memset(file, 0, sizeof(INI_FILETYPE)); /* initialize all fields in header */
  memset(dataBuf, 0, sizeof(dataBuf)); /* initialize all data */
  file->header = (MinIniFlashFileHeader*)dataBuf;
  file->data = (unsigned char*)file->header + sizeof(MinIniFlashFileHeader);
  file->header->magicNumber = MININI_FLASH_MAGIC_DATA_NUMBER_ID;
  McuUtility_strcpy(file->header->dataName, sizeof(file->header->dataName), (unsigned char*)filename);
  file->header->dataSize = 0;
  file->curr = file->data;
  file->isOpen = true;
  file->isReadOnly = false;
  return 1; /* ok */
}
#endif

int ini_close(INI_FILETYPE *file) {
  file->isOpen = false;
  if (!file->isReadOnly  && !isTempFile((const char*)file->header->dataName)) { /* RAM data, and not temp file? */
    /* store data in FLASH */
    if (McuFlash_Program((void*)McuMinINI_CONFIG_FLASH_NVM_ADDR_START, file->header, McuMinINI_CONFIG_FLASH_NVM_MAX_DATA_SIZE)!=ERR_OK) {
      return 0; /* failed */
    }
  }
  return 1; /* ok */
}

int ini_read(TCHAR *buffer, size_t size, INI_FILETYPE *file) {
  /* read a string until EOF or '\n' */
  unsigned char ch;

  buffer[0] = '\0'; /* zero terminate */
  for(;;) {
    if (file->curr >= file->data+file->header->dataSize) { /* beyond boundaries? */
      file->curr = file->data+file->header->dataSize; /* point to max position possible, one byte beyond */
      return 0; /* EOF */
    }
    ch = *file->curr; /* read character */
    file->curr++; /* move pointer */
    McuUtility_chcat((unsigned char*)buffer, size, ch); /* add character to buffer */
    if (ch=='\n') { /* reached end of line */
      return 1; /* ok */
    }
  }
  return 1; /* ok */
}

#if !McuMinINI_CONFIG_READ_ONLY
int ini_write(TCHAR *buffer, INI_FILETYPE *file) {
  size_t len, remaining;

  /* write zero terminated string to file */
  if (file->isReadOnly) {
    return 1; /* error, file is read-only */
  }
  /* data is in RAM buffer */
  len = McuUtility_strlen(buffer);
  remaining = dataBuf+sizeof(dataBuf)-file->curr;
  McuUtility_strcpy(file->curr, remaining, (const unsigned char*)buffer);
  file->curr += len;
  if (file->curr >= (unsigned char*)file->header+McuMinINI_CONFIG_FLASH_NVM_MAX_DATA_SIZE) { /* outside valid memory? */
    file->curr = (unsigned char*)file->header+McuMinINI_CONFIG_FLASH_NVM_MAX_DATA_SIZE; /* set to highest possible location */
    return 0; /* error */
  }
  if (file->curr >= file->data+file->header->dataSize) { /* moved beyond current size? file is growing */
    file->header->dataSize = file->curr - file->data; /* update size */
    return 1; /* ok */
  }
  return 1; /* ok */
}
#endif

#if !McuMinINI_CONFIG_READ_ONLY
int ini_remove(const TCHAR *filename) {
  MinIniFlashFileHeader *hp;

  /* check first if we are removing the data in FLASH */
  hp = (MinIniFlashFileHeader*)McuMinINI_CONFIG_FLASH_NVM_ADDR_START;
  if (   hp->magicNumber==MININI_FLASH_MAGIC_DATA_NUMBER_ID /* valid file */
      && McuUtility_strcmp((char*)hp->dataName, filename)==0 /* file name matches */
     )
  {
    /* flash data file */
    if (McuFlash_Erase((void*)McuMinINI_CONFIG_FLASH_NVM_ADDR_START, McuMinINI_CONFIG_FLASH_NVM_NOF_BLOCKS*McuMinINI_CONFIG_FLASH_NVM_BLOCK_SIZE)==ERR_OK) {
      return 1; /* ok */
    } else {
      return 0; /* error */
    }
  }
  /* check if we are removing the temp file */
  hp = (MinIniFlashFileHeader*)dataBuf;
  if (   hp->magicNumber==MININI_FLASH_MAGIC_DATA_NUMBER_ID /* valid file */
      && McuUtility_strcmp((char*)hp->dataName, filename)==0 /* it the data in RAM */
      )
  {
    /* RAM data file */
    memset(dataBuf, 0, sizeof(dataBuf));
    return 1; /* ok */
  }
  return 0; /* error */
}
#endif

int ini_tell(INI_FILETYPE *file, INI_FILEPOS *pos) {
  /* return the current file pointer (offset into file) */
  *pos = file->curr - file->data;
  return 1; /* ok */
}

int ini_seek(INI_FILETYPE *file, INI_FILEPOS *pos) {
  /* move the file pointer to the given position */
  file->curr = file->data + *pos; /* move current data pointer */
  if (file->curr >= (unsigned char*)file->header+McuMinINI_CONFIG_FLASH_NVM_MAX_DATA_SIZE) { /* outside limits? */
    file->curr = (unsigned char*)file->header + McuMinINI_CONFIG_FLASH_NVM_MAX_DATA_SIZE; /* back to valid range, which can be one byte beyond buffer */
    return 0; /* error, EOF */
  }
  return 1; /* ok */
}

#if !McuMinINI_CONFIG_READ_ONLY
int ini_rename(const TCHAR *source, const TCHAR *dest) {
  /* e.g. test.in~ -> test.ini: this always will do a store from RAM to FLASH! */
  MinIniFlashFileHeader *hp;

  if (isTempFile(source)) { /* store temporary file into flash */
    hp = (MinIniFlashFileHeader*)dataBuf;
    if (McuUtility_strcmp((char*)hp->dataName, source)!=0) { /* file name in RAM does not match? */
      return 0; /* error */
    }
    McuUtility_strcpy(hp->dataName, sizeof(hp->dataName), (unsigned char*)dest); /* rename file */
    /* store in flash */
    if (McuFlash_Program((void*)McuMinINI_CONFIG_FLASH_NVM_ADDR_START, (unsigned char*)dataBuf, sizeof(dataBuf))!=ERR_OK) {
      return 0; /* failed */
    }
    memset(dataBuf, 0, sizeof(dataBuf)); /* erase RAM file content */
  }
  return 1; /* ok */
}
#endif

int ini_deinit(void) {
  /* nothing needed */
  return 0; /* ok */
}

int ini_init(void) {
#if McuLib_CONFIG_CPU_IS_LPC55xx
  if((McuMinINI_CONFIG_FLASH_NVM_MAX_DATA_SIZE%McuFlash_CONFIG_FLASH_BLOCK_SIZE)!=0) {
#elif McuLib_CONFIG_CPU_VARIANT==McuLib_CONFIG_CPU_VARIANT_RP2040
  if((McuMinINI_CONFIG_FLASH_NVM_MAX_DATA_SIZE%McuFlash_CONFIG_FLASH_BLOCK_SIZE)!=0) {
#else
  if( ! (McuMinINI_CONFIG_FLASH_NVM_MAX_DATA_SIZE==64
     || McuMinINI_CONFIG_FLASH_NVM_MAX_DATA_SIZE==128
     || McuMinINI_CONFIG_FLASH_NVM_MAX_DATA_SIZE==256
     || McuMinINI_CONFIG_FLASH_NVM_MAX_DATA_SIZE==512
     || McuMinINI_CONFIG_FLASH_NVM_MAX_DATA_SIZE==1024
     || McuMinINI_CONFIG_FLASH_NVM_MAX_DATA_SIZE==2048
     || McuMinINI_CONFIG_FLASH_NVM_MAX_DATA_SIZE==4096
     || McuMinINI_CONFIG_FLASH_NVM_MAX_DATA_SIZE==8192)
    )
  {
#endif
    /* data size (number of bytes) needs to be 64, 128, 256, 512, 1024 bytes for the flash programming! */
    McuLog_fatal("wrong size of data!");
    for(;;) {}
  }
  return 0; /* ok */
}

static void PrintDataStatus(const McuShell_StdIOType *io, MinIniFlashFileHeader *hp, const unsigned char *dataName) {
  uint8_t buf[48];

  if (!McuFlash_IsAccessible(hp, sizeof(MinIniFlashFileHeader))) { /* accessing erased FLASH on LPC55Sxx will cause hard fault! */
    McuUtility_strcpy(buf, sizeof(buf), (unsigned char*)"ERASED, not accessible\r\n");
    McuShell_SendStatusStr(dataName, buf, io->stdOut);
    return;
  }
  McuUtility_strcpy(buf, sizeof(buf), (unsigned char*)"magic 0x");
  McuUtility_strcatNum32Hex(buf, sizeof(buf), hp->magicNumber);
  McuUtility_strcat(buf, sizeof(buf), (unsigned char*)", ");
  if (hp->magicNumber==MININI_FLASH_MAGIC_DATA_NUMBER_ID) {
    McuUtility_strcat(buf, sizeof(buf), hp->dataName);
    McuUtility_strcat(buf, sizeof(buf), (unsigned char*)", size ");
    McuUtility_strcatNum32u(buf, sizeof(buf), hp->dataSize);
  } else {
    McuUtility_strcat(buf, sizeof(buf), (unsigned char*)"<not valid>");
  }
  McuUtility_strcat(buf, sizeof(buf), (unsigned char*)"\r\n");
  McuShell_SendStatusStr(dataName, buf, io->stdOut);
}

static uint8_t PrintStatus(const McuShell_StdIOType *io) {
  uint8_t buf[48];

  McuShell_SendStatusStr((unsigned char*)"ini", (unsigned char*)"ini flash status\r\n", io->stdOut);
  McuUtility_strcpy(buf, sizeof(buf), (unsigned char*)"start 0x");
  McuUtility_strcatNum32Hex(buf, sizeof(buf), McuMinINI_CONFIG_FLASH_NVM_ADDR_START);
  McuUtility_strcat(buf, sizeof(buf), (unsigned char*)", block 0x");
  McuUtility_strcatNum16Hex(buf, sizeof(buf), McuMinINI_CONFIG_FLASH_NVM_BLOCK_SIZE);
  McuUtility_strcat(buf, sizeof(buf), (unsigned char*)", nof ");
  McuUtility_strcatNum16u(buf, sizeof(buf), McuMinINI_CONFIG_FLASH_NVM_NOF_BLOCKS);
  McuUtility_strcat(buf, sizeof(buf), (unsigned char*)"\r\n");
  McuShell_SendStatusStr((unsigned char*)"  flash", buf, io->stdOut);

  McuUtility_Num32uToStr(buf, sizeof(buf), McuMinINI_CONFIG_FLASH_NVM_MAX_DATA_SIZE);
  McuUtility_strcat(buf, sizeof(buf), (unsigned char*)" bytes\r\n");
  McuShell_SendStatusStr((unsigned char*)"  max data", buf, io->stdOut);

  PrintDataStatus(io, (MinIniFlashFileHeader*)McuMinINI_CONFIG_FLASH_NVM_ADDR_START, (const unsigned char*)"  data");
#if !McuMinINI_CONFIG_READ_ONLY
  PrintDataStatus(io, (MinIniFlashFileHeader*)dataBuf, (const unsigned char*)"  ram");
#endif
  return ERR_OK;
}

static uint8_t DumpData(const McuShell_StdIOType *io) {
  MinIniFlashFileHeader *hp;
  const unsigned char *p;

  hp = (MinIniFlashFileHeader*)McuMinINI_CONFIG_FLASH_NVM_ADDR_START;
  PrintDataStatus(io, hp, (const unsigned char*)"data");
  if (hp->magicNumber==MININI_FLASH_MAGIC_DATA_NUMBER_ID) {
    p = (const unsigned char*)hp + sizeof(MinIniFlashFileHeader);
    for(unsigned int i=0; i<hp->dataSize; i++) {
      io->stdOut(*p);
      p++;
    }
  }
  hp = (MinIniFlashFileHeader*)dataBuf;
  PrintDataStatus(io, hp, (const unsigned char*)"ram");
  if (hp->magicNumber==MININI_FLASH_MAGIC_DATA_NUMBER_ID) {
    p = (const unsigned char*)hp + sizeof(MinIniFlashFileHeader);
    for(unsigned int i=0; i<hp->dataSize; i++) {
      io->stdOut(*p);
      p++;
    }
  }
  return ERR_OK;
}

uint8_t ini_ParseCommand(const unsigned char *cmd, bool *handled, const McuShell_StdIOType *io) {
  if (McuUtility_strcmp((char*)cmd, McuShell_CMD_HELP)==0 || McuUtility_strcmp((char*)cmd, "ini help")==0) {
    McuShell_SendHelpStr((unsigned char*)"ini", (const unsigned char*)"Group of flash ini commands\r\n", io->stdOut);
    McuShell_SendHelpStr((unsigned char*)"  help|status", (unsigned char*)"Print help or status information\r\n", io->stdOut);
    McuShell_SendHelpStr((unsigned char*)"  dump", (unsigned char*)"Dump data information\r\n", io->stdOut);
    McuShell_SendHelpStr((unsigned char*)"  erase", (unsigned char*)"Erase data information\r\n", io->stdOut);
    *handled = TRUE;
    return ERR_OK;
  } else if ((McuUtility_strcmp((char*)cmd, McuShell_CMD_STATUS)==0) || (McuUtility_strcmp((char*)cmd, "ini status")==0)) {
    *handled = TRUE;
    return PrintStatus(io);
  } else if (McuUtility_strcmp((char*)cmd, "ini dump")==0) {
    *handled = TRUE;
    return DumpData(io);
  } else if (McuUtility_strcmp((char*)cmd, "ini erase")==0) {
    *handled = TRUE;
    return McuFlash_Erase((void*)McuMinINI_CONFIG_FLASH_NVM_ADDR_START, McuMinINI_CONFIG_FLASH_NVM_NOF_BLOCKS*McuMinINI_CONFIG_FLASH_NVM_BLOCK_SIZE);
  }
  return ERR_OK;
}

#endif /* McuMinINI_CONFIG_FS==McuMinINI_CONFIG_FS_TYPE_KINETIS_FLASH_FS */
