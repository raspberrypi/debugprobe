/*  Glue functions for a flash (page) based storage system, without a file system.
 *
 *  By CompuPhase, 2008-2012
 *  This "glue file" is in the public domain. It is distributed without
 *  warranties or conditions of any kind, either express or implied.
 *
 */

#include "minIniConfig.h"
#include "minIni.h"

#include "minGlue-Flash.h"
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "picoprobe_config.h"

#include "hardware/flash.h"


#define ERR_OK   0


/* NOTE: we only support one 'file' in FLASH, and only one 'file' in RAM. The one in RAM is for the read-write and temporary one  */
/* read-only FLASH 'file' is at MININI_CONFIG_FLASH_NVM_ADDR_START */
#if !MININI_CONFIG_READ_ONLY
    static unsigned char dataBuf[MININI_CONFIG_FLASH_NVM_MAX_DATA_SIZE]; /* ini file for read/write */
#endif



static int McuFlash_Erase(void *dst, uint32_t size)
{
    uint32_t flash_addr = (uint32_t)dst - MININI_CONFIG_FLASH_ADDR_START;

    printf("McuFlash_Erase(%lx,%lu)\n", flash_addr, size);
    flash_range_erase(flash_addr, size);
    return ERR_OK;
}   // McuFlash_Erase



static int McuFlash_Program(void *dst, const void *src, uint32_t size)
{
    uint32_t flash_addr = (uint32_t)dst - MININI_CONFIG_FLASH_ADDR_START;

    printf("McuFlash_Program(%lx,%p,%lu)\n", flash_addr, src, size);
    if (flash_addr % MININI_CONFIG_FLASH_NVM_BLOCK_SIZE == 0) {
        McuFlash_Erase(dst, MININI_CONFIG_FLASH_NVM_BLOCK_SIZE);
    }
    flash_range_program(flash_addr, src, size);
    return ERR_OK;
}   // McuFlash_Program



int ini_openread(const TCHAR *filename, INI_FILETYPE *file)
{
    printf("ini_openread(%s,%p)\n", filename, file);

    /* open file in read-only mode. This will use directly the data in FLASH */
    memset(file, 0, sizeof(INI_FILETYPE));
    file->header = (MinIniFlashFileHeader*)(MININI_CONFIG_FLASH_NVM_ADDR_START);
    file->data   = (unsigned char*)file->header + sizeof(MinIniFlashFileHeader);
    if (file->header->magicNumber != MININI_FLASH_MAGIC_DATA_NUMBER_ID) {
        return 0; /* failed, magic number does not match */
    }
    if (strcmp((char*)file->header->dataName, (char*)filename) != 0) {
        return 0; /* failed, not the file name of the storage */
    }
    file->curr       = file->data;
    file->isOpen     = true;
    file->isReadOnly = true;
    return 1; /* ok */
}   // ini_openread



static bool isTempFile(const TCHAR *filename)
{
    printf("isTempFile(%s)\n", filename);

    size_t len;

    len = strlen(filename);
    if (len == 0) {
        return false; /* illegal file name */
    }
    if (filename[len-1] == '~') { /* temporary file name */
        return true;
    }
    return false;
}   // isTempFile



#if !MININI_CONFIG_READ_ONLY
int ini_openwrite(const TCHAR *filename, INI_FILETYPE *file)
{
    printf("ini_openwrite(%s,%p)\n", filename, file);

    /* create always a new file */
    memset(file, 0, sizeof(INI_FILETYPE)); /* initialize all fields in header */
    memset(dataBuf, 0, sizeof(dataBuf)); /* initialize all data */
    file->header = (MinIniFlashFileHeader*)dataBuf;
    file->data   = (unsigned char*)file->header + sizeof(MinIniFlashFileHeader);
    file->header->magicNumber = MININI_FLASH_MAGIC_DATA_NUMBER_ID;
    strncpy((TCHAR *)file->header->dataName, filename, sizeof(file->header->dataName));
    file->header->dataSize = 0;
    file->curr             = file->data;
    file->isOpen           = true;
    file->isReadOnly       = false;
    return 1; /* ok */
}   // ini_openwrite
#endif



int ini_close(INI_FILETYPE *file)
{
    printf("ini_close(%p)\n", file);

    file->isOpen = false;
    if ( !file->isReadOnly  &&  !isTempFile((const char*)file->header->dataName)) { /* RAM data, and not temp file? */
        /* store data in FLASH */
        if (McuFlash_Program((void*)MININI_CONFIG_FLASH_NVM_ADDR_START, file->header, MININI_CONFIG_FLASH_NVM_MAX_DATA_SIZE) != ERR_OK) {
            return 0; /* failed */
        }
    }
    return 1; /* ok */
}   // ini_close



int ini_read(TCHAR *buffer, size_t size, INI_FILETYPE *file)
{
    printf("ini_read(%p,%d,%p)\n", buffer, size, file);

    /* read a string until EOF or '\n' */
    TCHAR ch[2];

    buffer[0] = '\0'; /* zero terminate */
    for(;;) {
        if (file->curr >= file->data+file->header->dataSize) { /* beyond boundaries? */
            file->curr = file->data+file->header->dataSize; /* point to max position possible, one byte beyond */
            return 0; /* EOF */
        }
        ch[0] = *file->curr; /* read character */
        ch[1] = '\0';
        file->curr++; /* move pointer */
        strncat(buffer, ch, size); /* add character to buffer */
        if (ch[0] == '\n') { /* reached end of line */
            return 1; /* ok */
        }
    }
    return 1; /* ok */
}   // ini_read



#if !MININI_CONFIG_READ_ONLY
int ini_write(TCHAR *buffer, INI_FILETYPE *file)
{
    printf("ini_write(%p,%p)\n", buffer, file);

    size_t len, remaining;

    /* write zero terminated string to file */
    if (file->isReadOnly) {
        return 1; /* error, file is read-only */
    }
    /* data is in RAM buffer */
    len = strlen(buffer);
    remaining = dataBuf + sizeof(dataBuf) - file->curr;
    strncpy((TCHAR *)file->curr, buffer, remaining);
    file->curr += len;
    if (file->curr >= (unsigned char*)file->header + MININI_CONFIG_FLASH_NVM_MAX_DATA_SIZE) { /* outside valid memory? */
        file->curr = (unsigned char*)file->header + MININI_CONFIG_FLASH_NVM_MAX_DATA_SIZE; /* set to highest possible location */
        return 0; /* error */
    }
    if (file->curr >= file->data + file->header->dataSize) { /* moved beyond current size? file is growing */
        file->header->dataSize = file->curr - file->data;    /* update size */
        return 1; /* ok */
    }
    return 1; /* ok */
}   // ini_write
#endif



#if !MININI_CONFIG_READ_ONLY
int ini_remove(const TCHAR *filename)
{
    printf("ini_remove(%s)\n", filename);

    MinIniFlashFileHeader *hp;

    /* check first if we are removing the data in FLASH */
    hp = (MinIniFlashFileHeader*)MININI_CONFIG_FLASH_NVM_ADDR_START;
    if (    hp->magicNumber == MININI_FLASH_MAGIC_DATA_NUMBER_ID /* valid file */
        &&  strcmp((char*)hp->dataName, filename) == 0 /* file name matches */
    )
    {
        /* flash data file */
        if (McuFlash_Erase((void*)MININI_CONFIG_FLASH_NVM_ADDR_START, MININI_CONFIG_FLASH_NVM_NOF_BLOCKS * MININI_CONFIG_FLASH_NVM_BLOCK_SIZE) == ERR_OK) {
            return 1; /* ok */
        } else {
            return 0; /* error */
        }
    }
    /* check if we are removing the temp file */
    hp = (MinIniFlashFileHeader*)dataBuf;
    if (    hp->magicNumber == MININI_FLASH_MAGIC_DATA_NUMBER_ID /* valid file */
        &&  strcmp((char*)hp->dataName, filename) == 0 /* it the data in RAM */
    )
    {
        /* RAM data file */
        memset(dataBuf, 0, sizeof(dataBuf));
        return 1; /* ok */
    }
    return 0; /* error */
}   // ini_remove
#endif



int ini_tell(INI_FILETYPE *file, INI_FILEPOS *pos)
{
    printf("ini_tell(%p,*%d)\n", file, file->curr - file->data);

    /* return the current file pointer (offset into file) */
    *pos = file->curr - file->data;
    return 1; /* ok */
}   // ini_tell



int ini_seek(INI_FILETYPE *file, INI_FILEPOS *pos)
{
    printf("ini_seek(%p,%d)\n", file, *pos);

    /* move the file pointer to the given position */
    file->curr = file->data + *pos; /* move current data pointer */
    if (file->curr >= (unsigned char*)file->header + MININI_CONFIG_FLASH_NVM_MAX_DATA_SIZE) { /* outside limits? */
        file->curr = (unsigned char*)file->header + MININI_CONFIG_FLASH_NVM_MAX_DATA_SIZE;    /* back to valid range, which can be one byte beyond buffer */
        return 0; /* error, EOF */
    }
    return 1; /* ok */
}   // ini_seek



#if !MININI_CONFIG_READ_ONLY
int ini_rename(const TCHAR *source, const TCHAR *dest)
{
    printf("ini_rename(%s,%s)\n", source, dest);

    /* e.g. test.in~ -> test.ini: this always will do a store from RAM to FLASH! */
    MinIniFlashFileHeader *hp;

    if (isTempFile(source)) { /* store temporary file into flash */
        hp = (MinIniFlashFileHeader*)dataBuf;
        if (strcmp((char*)hp->dataName, source) != 0) { /* file name in RAM does not match? */
            return 0; /* error */
        }
        strncpy((TCHAR *)hp->dataName, dest, sizeof(hp->dataName)); /* rename file */
        /* store in flash */
        if (McuFlash_Program((void*)MININI_CONFIG_FLASH_NVM_ADDR_START, (unsigned char*)dataBuf, sizeof(dataBuf)) != ERR_OK) {
            return 0; /* failed */
        }
        memset(dataBuf, 0, sizeof(dataBuf)); /* erase RAM file content */
    }
    return 1; /* ok */
}   // ini_rename
#endif



int ini_deinit(void)
{
    /* nothing needed */
    return 0; /* ok */
}   // ini_deinit



int ini_init(void)
{
    printf("ini_init()--------\n");
    if ( !(    MININI_CONFIG_FLASH_NVM_MAX_DATA_SIZE == 64
           ||  MININI_CONFIG_FLASH_NVM_MAX_DATA_SIZE == 128
           ||  MININI_CONFIG_FLASH_NVM_MAX_DATA_SIZE == 256
           ||  MININI_CONFIG_FLASH_NVM_MAX_DATA_SIZE == 512
           ||  MININI_CONFIG_FLASH_NVM_MAX_DATA_SIZE == 1024
           ||  MININI_CONFIG_FLASH_NVM_MAX_DATA_SIZE == 2048
           ||  MININI_CONFIG_FLASH_NVM_MAX_DATA_SIZE == 4096
           ||  MININI_CONFIG_FLASH_NVM_MAX_DATA_SIZE == 8192)
    )
    {
        /* data size (number of bytes) needs to be 64, 128, 256, 512, 1024 bytes for the flash programming! */
        // TODO McuLog_fatal("wrong size of data!");
        for (;;) {}
    }

    {
        MinIniFlashFileHeader *hp = (MinIniFlashFileHeader*)MININI_CONFIG_FLASH_NVM_ADDR_START;

        if (hp->magicNumber != MININI_FLASH_MAGIC_DATA_NUMBER_ID)
        {
            McuFlash_Erase((void*)MININI_CONFIG_FLASH_NVM_ADDR_START, MININI_CONFIG_FLASH_NVM_NOF_BLOCKS * MININI_CONFIG_FLASH_NVM_BLOCK_SIZE);
        }

#if 1
        int r;

        //ini_puts("probe", "init", "1", "data");
        printf("-------------------------1\n");
        long cnt = ini_getl("probe", "bootcnt", 99, "data");
        printf("-------------------------2 %ld\n", cnt);
        r = ini_putl("probe", "bootcnt", cnt + 1, "data");
        printf("-------------------------3 %d\n", r);

        if (cnt == 8) {
            ini_puts("probe", "net", "14", "data");
        }

        {
            char name[20];

            snprintf(name, sizeof(name), "cnt%ld", cnt);
            r = ini_putl("cnt", name, cnt*2, "data");
            printf("-------------------------4 %d\n", r);
        }

        {
            long cnt;

            cnt = ini_getl("fibo", "cnt", 0, "data");
            if (cnt == 0)
                ini_putl("fibo", "0", 1, "data");
            else if (cnt == 1)
                ini_putl("fibo", "1", 1, "data");
            else {
                char name_pp[20];
                char name_p[20];
                char name[20];
                sprintf(name_pp, "%ld", cnt - 2);
                sprintf(name_p,  "%ld", cnt - 1);
                sprintf(name,    "%ld", cnt);

                long cnt_pp = ini_getl("fibo", name_pp, 0, "data");
                long cnt_p  = ini_getl("fibo", name_p, 0, "data");
                ini_putl("fibo", name, cnt_pp + cnt_p, "data");
            }
            ini_putl("fibo", "cnt", cnt + 1, "data");
        }

        cnt = ini_getl("probe", "bootcnt2", 99, "data");
        printf("-------------------------5 %ld\n", cnt);
        ini_putl("probe", "bootcnt2", cnt + 5, "data");
        printf("-------------------------6\n");
#endif
    }

    return 0; /* ok */
}   // ini_init



static void PrintDataStatus(MinIniFlashFileHeader *hp, const unsigned char *dataName)
{
    picoprobe_info("magic 0x%08x\n", hp->magicNumber);
    if (hp->magicNumber == MININI_FLASH_MAGIC_DATA_NUMBER_ID) {
        picoprobe_info("    name: %s\n", hp->dataName);
        picoprobe_info("    size: %d\n", hp->dataSize);
    }
    else {
        picoprobe_info("    <not valid>\n");
    }
}   // PrintDataStatus



#if 0
static uint8_t PrintStatus(const McuShell_StdIOType *io)
{
  uint8_t buf[48];

  McuShell_SendStatusStr((unsigned char*)"ini", (unsigned char*)"ini flash status\r\n", io->stdOut);
  McuUtility_strcpy(buf, sizeof(buf), (unsigned char*)"start 0x");
  McuUtility_strcatNum32Hex(buf, sizeof(buf), MININI_CONFIG_FLASH_NVM_ADDR_START);
  McuUtility_strcat(buf, sizeof(buf), (unsigned char*)", block 0x");
  McuUtility_strcatNum16Hex(buf, sizeof(buf), MININI_CONFIG_FLASH_NVM_BLOCK_SIZE);
  McuUtility_strcat(buf, sizeof(buf), (unsigned char*)", nof ");
  McuUtility_strcatNum16u(buf, sizeof(buf), MININI_CONFIG_FLASH_NVM_NOF_BLOCKS);
  McuUtility_strcat(buf, sizeof(buf), (unsigned char*)"\r\n");
  McuShell_SendStatusStr((unsigned char*)"  flash", buf, io->stdOut);

  McuUtility_Num32uToStr(buf, sizeof(buf), MININI_CONFIG_FLASH_NVM_MAX_DATA_SIZE);
  McuUtility_strcat(buf, sizeof(buf), (unsigned char*)" bytes\r\n");
  McuShell_SendStatusStr((unsigned char*)"  max data", buf, io->stdOut);

  PrintDataStatus(io, (MinIniFlashFileHeader*)MININI_CONFIG_FLASH_NVM_ADDR_START, (const unsigned char*)"  data");
#if !MININI_CONFIG_READ_ONLY
  PrintDataStatus(io, (MinIniFlashFileHeader*)dataBuf, (const unsigned char*)"  ram");
#endif
  return ERR_OK;
}   // PrintStatus
#endif



void ini_print_all(void)
{
    MinIniFlashFileHeader *hp;
    const unsigned char *p;

    hp = (MinIniFlashFileHeader*)MININI_CONFIG_FLASH_NVM_ADDR_START;
    PrintDataStatus(hp, (const unsigned char*)"data");
    if (hp->magicNumber == MININI_FLASH_MAGIC_DATA_NUMBER_ID) {
        p = (const unsigned char*)hp + sizeof(MinIniFlashFileHeader);
        for (size_t i = 0;  i < hp->dataSize;  i++) {
            printf("%c", *p);
            p++;
        }
    }
    hp = (MinIniFlashFileHeader*)dataBuf;
    PrintDataStatus(hp, (const unsigned char*)"ram");
    if (hp->magicNumber == MININI_FLASH_MAGIC_DATA_NUMBER_ID) {
        p = (const unsigned char*)hp + sizeof(MinIniFlashFileHeader);
        for (size_t i = 0;  i < hp->dataSize;  i++) {
            printf("%c", *p);
            p++;
        }
    }
}   // ini_print_all



#if 0
uint8_t ini_ParseCommand(const unsigned char *cmd, bool *handled, const McuShell_StdIOType *io)
{
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
    return McuFlash_Erase((void*)MININI_CONFIG_FLASH_NVM_ADDR_START, MININI_CONFIG_FLASH_NVM_NOF_BLOCKS*MININI_CONFIG_FLASH_NVM_BLOCK_SIZE);
  }
  return ERR_OK;
}   // ini_ParseCommand

#endif
