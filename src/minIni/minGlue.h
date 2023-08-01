/*  Glue functions for the minIni library, based on the C/C++ stdio library
 *
 *  Or better said: this file contains macros that maps the function interface
 *  used by minIni to the standard C/C++ file I/O functions.
 *
 *  By CompuPhase, 2008-2014
 *  This "glue file" is in the public domain. It is distributed without
 *  warranties or conditions of any kind, either express or implied.
 */

#include "minIniConfig.h" /* MinIni config file */

#if MININI_CONFIG_FS==MININI_CONFIG_FS_TYPE_GENERIC
  /* map required file I/O types and functions to the standard C library */
  #include <stdio.h>

  #define INI_FILETYPE                  FILE*
  #define ini_openread(filename,file)   ((*(file) = fopen((filename),"rb")) != NULL)
  #define ini_openwrite(filename,file)  ((*(file) = fopen((filename),"wb")) != NULL)
  #define ini_close(file)               (fclose(*(file)) == 0)
  #define ini_read(buffer,size,file)    (fgets((buffer),(size),*(file)) != NULL)
  #define ini_write(buffer,file)        (fputs((buffer),*(file)) >= 0)
  #define ini_rename(source,dest)       (rename((source), (dest)) == 0)
  #define ini_remove(filename)          (remove(filename) == 0)

  #define INI_FILEPOS                   fpos_t
  #define ini_tell(file,pos)            (fgetpos(*(file), (pos)) == 0)
  #define ini_seek(file,pos)            (fsetpos(*(file), (pos)) == 0)

  /* for floating-point support, define additional types and functions */
  #define ini_ftoa(string,value)        sprintf((string),"%f",(value))
  #define ini_atof(string)              (INI_REAL)strtod((string),NULL)

  typedef char TCHAR;

  int ini_init(void);
  int ini_deinit(void);

#elif MININI_CONFIG_FS==MININI_CONFIG_FS_TYPE_FAT_FS
  #include "minGlue-FatFs.h"
#elif MININI_CONFIG_FS==MININI_CONFIG_FS_TYPE_FLASH_FS
  #include "minGlue-Flash.h"
#elif MININI_CONFIG_FS==MININI_CONFIG_FS_TYPE_LITTLE_FS
  #include "minGlue-LittleFS.h"
#else
  #error "define the type of system"
#endif
