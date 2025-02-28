/**
 * \file
 * \brief Configuration header file for MinINI
 * Copyright (c) 2020-2021, Erich Styger
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * This header file is used to configure settings of the MinINI module.
 */
#ifndef __MININI_CONFIG_H
#define __MININI_CONFIG_H

#include <pico/stdlib.h>

#define MININI_CONFIG_FS_TYPE_GENERIC    (0) /* Generic File System */
#define MININI_CONFIG_FS_TYPE_FAT_FS     (1) /* FatFS File System */
#define MININI_CONFIG_FS_TYPE_TINY_FS    (2) /* TinyFS File System */
#define MININI_CONFIG_FS_TYPE_FLASH_FS   (3) /* Flash Page System */
#define MININI_CONFIG_FS_TYPE_LITTLE_FS  (4) /* LittleFS File System */

#ifndef MININI_CONFIG_FS
    #define MININI_CONFIG_FS      (MININI_CONFIG_FS_TYPE_FLASH_FS)
        /*!< File System integration used, one of MININI_CONFIG_FS_TYPE_GENERIC, MININI_CONFIG_FS_TYPE_FAT_FS, MININI_CONFIG_FS_TYPE_TINY_FS */
#endif

#define PORTABLE_STRNICMP

#ifndef MININI_CONFIG_USE_REAL
    #define MININI_CONFIG_USE_REAL                      (0)
        /*!< if ini file handling includes handling of floating point data types */
#endif

#if MININI_CONFIG_USE_REAL
    #define INI_REAL              double
#else
    //#define INI_REAL              double
#endif

#ifndef MININI_CONFIG_READ_ONLY
    #define MININI_CONFIG_READ_ONLY                     (0)
#endif
        /*!< if ini file handling is read-only */

#ifndef MININI_CONFIG_BUFFER_SIZE
    #define MININI_CONFIG_BUFFER_SIZE                   (64)
#endif
        /*!< maximum line length, maximum path length, buffer is allocated on the stack! */

#ifndef NDEBUG
//    #define NDEBUG
        /*!< comment above define to turn on assertions */
#endif

#if MININI_CONFIG_FS==MININI_CONFIG_FS_TYPE_FLASH_FS
    /* flash memory settings */
    #ifndef MININI_CONFIG_FLASH_NVM_NOF_BLOCKS
        #define MININI_CONFIG_FLASH_NVM_NOF_BLOCKS      (1)
            /*!< number of flash blocks */
    #endif

    #ifndef MININI_CONFIG_FLASH_NVM_BLOCK_SIZE
        #define MININI_CONFIG_FLASH_NVM_BLOCK_SIZE      (4096)
            /*!< must match FLASH_GetProperty(&s_flashDriver, kFLASH_PropertyPflash0SectorSize, &pflashSectorSize).
                 For LPC55Sxx it is 0x200, for LPC845 it is 0x400, for K22FN/K02FN it is 0x800 */
    #endif

    #ifndef MININI_CONFIG_FLASH_ADDR_START
        #define MININI_CONFIG_FLASH_ADDR_START          0x10000000
            /*!< start address of flash, this is the base address of the XIP flash of the RP2040 */
    #endif

    #ifndef MININI_CONFIG_FLASH_SIZE
        #define MININI_CONFIG_FLASH_SIZE                PICO_FLASH_SIZE_BYTES
            /*!< size of flash */
    #endif

    #ifndef MININI_CONFIG_FLASH_NVM_ADDR_START
        #define MININI_CONFIG_FLASH_NVM_ADDR_START      ((MININI_CONFIG_FLASH_ADDR_START + MININI_CONFIG_FLASH_SIZE) - (MININI_CONFIG_FLASH_NVM_NOF_BLOCKS * MININI_CONFIG_FLASH_NVM_BLOCK_SIZE))
            /*!< last block in FLASH, start address of data in flash */
    #endif

    #ifndef MININI_CONFIG_FLASH_NVM_MAX_DATA_SIZE
        #define MININI_CONFIG_FLASH_NVM_MAX_DATA_SIZE   (1 * 0x100)
            /*!< size for the data, less than MININI_CONFIG_FLASH_NVM_BLOCK_SIZE to save memory. For RP2040 it must be multiple of 0x100! */
    #endif

    #ifndef MININI_FILENAME
        /** "filename" of the the ini settings.  Must be non-zero. */
        #define MININI_FILENAME                         "config"
    #endif

    #ifndef MININI_SECTION
        /** section of the the ini settings */
        #define MININI_SECTION                          "config"
    #endif
#endif /* MININI_CONFIG_FS_TYPE_FLASH_FS */

#endif /* __MININI_CONFIG_H */
