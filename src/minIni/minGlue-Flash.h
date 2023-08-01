/*  Glue functions for the minIni library, for a Kinetis flash page
 *
 *  By CompuPhase, 2008-2012
 *  This "glue file" is in the public domain. It is distributed without
 *  warranties or conditions of any kind, either express or implied.
 *
 */

#ifndef _MINGLUE_FLASH_H__
#define _MINGLUE_FLASH_H__

#include <stddef.h>
#include <stdbool.h>
#include "minIniConfig.h" /* MinIni config file */

#if MININI_CONFIG_FS == MININI_CONFIG_FS_TYPE_FLASH_FS

#define INI_BUFFERSIZE   (MININI_CONFIG_BUFFER_SIZE)       /* maximum line length, maximum path length */

#define MININI_FLASH_MAGIC_DATA_NUMBER_ID   (0xFEEDBABE) /* magic ID to mark valid memory */
typedef struct MinIniFlashFileHeader {
    unsigned int magicNumber; /* magic ID: MAGIC_DATA_NUMBER_ID */
    unsigned char dataName[16]; /* file/data name, limited to 16 bytes, zero terminated */
    size_t dataSize; /* size of data, without this header */
} MinIniFlashFileHeader;

typedef struct {
    MinIniFlashFileHeader *header; /* pointer to header, is at the start of the data */
    unsigned char *data; /* start of data */
    unsigned char *curr; /* current data/file pointer */
    bool isReadOnly; /* if file is for read and write or read-only */
    bool isOpen; /* if file is open or not */
} MinIniaFlashDataFile_t;


#define TCHAR           char
#define INI_FILETYPE    MinIniaFlashDataFile_t
#define INI_FILEPOS     size_t

#define ini_assert(condition)         /* empty */

int ini_openread(const TCHAR *filename, INI_FILETYPE *file);
int ini_openwrite(const TCHAR *filename, INI_FILETYPE *file);
int ini_close(INI_FILETYPE *file);
int ini_read(TCHAR *buffer, size_t size, INI_FILETYPE *file);
int ini_write(TCHAR *buffer, INI_FILETYPE *file);
int ini_remove(const TCHAR *filename);
int ini_tell(INI_FILETYPE *file, INI_FILEPOS *pos);
int ini_seek(INI_FILETYPE *file, INI_FILEPOS *pos);
int ini_rename(const TCHAR *source, const TCHAR *dest);

#if defined(INI_REAL)
    #include <stdio.h> /* for sprintf() */

    #define ini_ftoa(string,value)        sprintf((string),"%f",(value))
    #define ini_atof(string)              (INI_REAL)strtod((string),NULL)
#endif /* defined INI_REAL */

/*!
 * \brief Module de-initialization
 * \return error code, 0 for no error
 */
int ini_deinit(void);

/*!
 * \brief Module initialization
 * \return error code, 0 for no error
 */
int ini_init(void);

/*!
 * Print content of minIni
 */
void ini_print_all(void);

#endif /* MININI_CONFIG_FS==MININI_CONFIG_FS_TYPE_FLASH_FS */

#endif /* _MINGLUE_FLASH_H__ */
