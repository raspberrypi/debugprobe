/*
 * The MIT License (MIT)
 *
 * Copyright (c) 2019 Ha Thach (tinyusb.org)
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 *
 */

/*
 * Mounting under Linux
 * --------------------
 * I don't know why, but Linux does some prefetching on mounting the device: CURRENT.UF2 is
 * partially read while RAM.UF2 is not.  Unfortunately this also caches the data so that
 * the target must be already connected.
 */

#include "tusb.h"

#include "picoprobe_config.h"
#include "boot/uf2.h"                // this is the Pico variant of the UF2 header

#include "target_board.h"

#include "probe.h"
#include "msc_utils.h"


#define INCLUDE_RAM_UF2


#if CFG_TUD_MSC

#define ADWORD(X)       (X) & 0xff, ((X) & 0xff00) >> 8, ((X) & 0xff0000) >> 16, ((X) & 0xff000000) >> 24
#define AWORD(X)        (X) & 0xff, ((X) & 0xff00) >> 8
#define ADATE(Y,M,D)    AWORD((((Y)-1980) << 9) + ((M) << 5) + (D))
#define ATIME(H,M,S)    AWORD(((H) << 11) + ((M) << 5) + ((S) / 2))
#define SECTORS(BYTES)  (((BYTES) + BPB_BytsPerSec - 1) / BPB_BytsPerSec)
#define CLUSTERS(BYTES) (((BYTES) + BPB_BytsPerClus - 1) / BPB_BytsPerClus)
#define AFAT12(C1,C2)   (C1) & 0xff, (((C1) & 0xf00) >> 8) + (((C2) & 0x0f) << 4), (((C2) & 0xff0) >> 4)

#if OPTIMIZE_FOR_OPENOCD
    #define SPEC_VERSION "-openocd"
#else
    #define SPEC_VERSION
#endif
#if defined(GIT_HASH)
    #define _GIT_HASH "-" GIT_HASH
#else
    #define GIT_HASH
#endif

#define README_CONTENTS \
"This is Yet Another Picoprobe v" PICOPROBE_VERSION_STRING  _GIT_HASH SPEC_VERSION ".\r\n\r\n\
- CURRENT.UF2 mirrors the flash content of the target\r\n\
- INFO_UF2.TXT holds some information about probe and target\r\n\
- drop a UF2 file to flash the target device\r\n"
#define README_SIZE            (sizeof(README_CONTENTS) - 1)

#define INDEXHTM_CONTENTS \
"<html><head>\r\n\
<meta http-equiv=\"refresh\" content=\"0;URL='https://github.com/rgrr/yapicoprobe/tree/" GIT_HASH "'\"/>\r\n\
</head>\r\n\
<body>Redirecting to <a href=\"https://github.com/rgrr/yapicoprobe/tree/rg-\">yapicoprobe repository</a></body>\r\n\
</html>\r\n"
#define INDEXHTM_SIZE          (sizeof(INDEXHTM_CONTENTS) - 1)

#define INFOUF2_CONTENTS \
"UF2 Target Programmer v" PICOPROBE_VERSION_STRING _GIT_HASH SPEC_VERSION " for %s%s\r\n\
Model: Yet Another Picoprobe\r\n\
Board-ID: %s\r\n"
#define INFOUF2_SIZE           150                                              // generated text must fit into buffer


#define BPB_BytsPerSec              512UL
#define BPB_BytsPerClus             65536UL
#define BPB_TotSec                  (128*1024*2+20)                             // 16MB + some offset for FAT overhead

#if (BPB_TotSec < 65536)
    const uint16_t BPB_TotSec16     = BPB_TotSec;
    const uint32_t BPB_TotSec32     = 0;
#else
    const uint16_t BPB_TotSec16     = 0;
    const uint32_t BPB_TotSec32     = BPB_TotSec;
#endif
const uint8_t  BPB_SecPerClus       = SECTORS(BPB_BytsPerClus);                 // cluster size (65536 -> BPB_SecPerClus=128)
const uint16_t BPB_RootEntCnt       = BPB_BytsPerSec / 32;                      // only one sector for root directory
const uint16_t BPB_ResvdSecCnt      = 1;
const uint8_t  BPB_NumFATs          = 1;
const uint16_t BPB_FATSz16          = 8;                                        // -> ~2700 cluster fit into one sector for FAT12 (max ~170MB)
const uint32_t BS_VolID             = 0x1234;
const uint8_t  BPB_Media            = 0xf8;                                     // f0=floppy, f8=HDD, fa=RAM disk (format prompt)
const uint8_t  BS_DrvNum            = 0x80;                                     // 00=floppy, 80=fixed disk

// some calculations
const uint32_t c_TotalCluster       = BPB_TotSec / BPB_SecPerClus;              // -> 256 cluster for 16MB total size and 64KByte cluster size
const uint32_t c_BootStartSector    = 0;
const uint32_t c_BootSectors        = 1;                                        // must be 1
const uint32_t c_FatStartSector     = BPB_ResvdSecCnt;
const uint32_t c_FatSectors         = BPB_FATSz16 * BPB_NumFATs;
const uint32_t c_RootDirStartSector = c_FatStartSector + c_FatSectors;
const uint32_t c_RootDirSectors     = SECTORS(32 * BPB_RootEntCnt);             // must be 1
const uint32_t c_DataStartSector    = c_RootDirStartSector + c_RootDirSectors;

#define c_FirstSectorofCluster(N)   (c_DataStartSector + ((N) - 2) * BPB_SecPerClus)

// TODO remove
#define TARGET_FLASH_IMG_BASE       (g_board_info.target_cfg->flash_regions[0].start)
#define TARGET_FLASH_IMG_SIZE       (g_board_info.target_cfg->flash_regions[0].end - g_board_info.target_cfg->flash_regions[0].start)
#define TARGET_FLASH_UF2_SIZE       (2 * TARGET_FLASH_IMG_SIZE)

#ifdef INCLUDE_RAM_UF2
    #define TARGET_RAM_IMG_BASE     (g_board_info.target_cfg->ram_regions[0].start)
    #define TARGET_RAM_IMG_SIZE     (g_board_info.target_cfg->ram_regions[0].end - g_board_info.target_cfg->ram_regions[0].start)
    #define TARGET_RAM_UF2_SIZE     (2 * TARGET_RAM_IMG_SIZE)
#endif

//
// The cluster assignments must be manually reflected in \a fatsector
//
const uint32_t f_ReadmeStartCluster = 2;
const uint32_t f_ReadmeClusters = 1;
const uint32_t f_ReadmeStartSector = c_FirstSectorofCluster(f_ReadmeStartCluster);
const uint32_t f_ReadmeSectors = BPB_SecPerClus * f_ReadmeClusters;

const uint32_t f_InfoUF2TxtStartCluster = 4;
const uint32_t f_InfoUF2TxtClusters = 1;
const uint32_t f_InfoUF2TxtStartSector = c_FirstSectorofCluster(f_InfoUF2TxtStartCluster);
const uint32_t f_InfoUF2TxtSectors = BPB_SecPerClus * f_InfoUF2TxtClusters;
const uint32_t f_InfoUF2TxtSize = BPB_BytsPerSec;

const uint32_t f_IndexHtmStartCluster = 6;
const uint32_t f_IndexHtmClusters = 1;
const uint32_t f_IndexHtmStartSector = c_FirstSectorofCluster(f_IndexHtmStartCluster);
const uint32_t f_IndexHtmSectors = BPB_SecPerClus * f_IndexHtmClusters;

#define f_CurrentUF2StartCluster      16
#define f_CurrentUF2Clusters          (CLUSTERS(TARGET_FLASH_UF2_SIZE))
#define f_CurrentUF2StartSector       (c_FirstSectorofCluster(f_CurrentUF2StartCluster))
#define f_CurrentUF2Sectors           (BPB_SecPerClus * f_CurrentUF2Clusters)

#ifdef INCLUDE_RAM_UF2
    #define f_RAMUF2StartCluster      (f_CurrentUF2StartCluster + f_CurrentUF2Clusters)
    #define f_RAMUF2Clusters          (CLUSTERS(TARGET_RAM_UF2_SIZE))
    #define f_RAMUF2StartSector       (c_FirstSectorofCluster(f_RAMUF2StartCluster))
    #define f_RAMUF2Sectors           (BPB_SecPerClus * f_RAMUF2Clusters)
#endif

static uint64_t last_write_ms = 0;

static const uint8_t bootsector[BPB_BytsPerSec] =
    //------------- Block0: Boot Sector -------------//
    // see http://elm-chan.org/docs/fat_e.html
    {
        0xEB, 0x3C, 0x90,                                 // BS_JmpBoot
        'M',  'S',  'D',  'O',  'S',  '5',  '.',  '0',    // BS_OEMName
        AWORD(BPB_BytsPerSec),
        BPB_SecPerClus,
        AWORD(BPB_ResvdSecCnt),
        BPB_NumFATs,
        AWORD(BPB_RootEntCnt),
        AWORD(BPB_TotSec16),                              // BPB_TotSec16 / BPB_SecPerClus determines FAT type, there are legal 1365 cluster -> FAT12
        BPB_Media,
        AWORD(BPB_FATSz16),
        AWORD(1),                                         // BPB_SecPerTrk
        AWORD(1),                                         // BPB_NumHeads
        ADWORD(0),                                        // BPB_HiddSec
        ADWORD(BPB_TotSec32),                             // BPB_TotSec32

        // byte 36 and more:
                           BS_DrvNum, 0x00, 0x29,       ADWORD(BS_VolID),  'Y',  'A',  'P',  'i',  'c',
         'o',  'p',  'r',  'o',  'b',  'e',
                                             'F',  'A',  'T',  '1',  '2',  ' ',  ' ',  ' ', 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,

        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,

        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,

        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, AWORD(0xaa55)
    };

static const uint8_t fatsector[] =
    //------------- Block1: FAT12 Table -------------//
    {
        // cluster 0 & 1
        AFAT12(0xf00 + BPB_Media, 0xfff), 
        
        // cluster 2 (0_README.TXT) & 3 (bad) - must be f_ReadmeStartCluster
        AFAT12(0xfff, 0xff7),

        // cluster 4 (0_README.TXT) & 5 (bad) - must be f_InfoUF2TxtStartCluster
        AFAT12(0xfff, 0xff7),

        // cluster 6 (0_README.TXT) & 7 (bad) - must be f_IndexHtmStartCluster
        AFAT12(0xfff, 0xff7),

		// cluster 8..15 (8) are spares
		AFAT12(0xff7, 0xff7), AFAT12(0xff7, 0xff7), AFAT12(0xff7, 0xff7), AFAT12(0xff7, 0xff7),

        // cluster 16.. f_CurrentUF2StartCluster
    };

static const uint8_t rootdirsector[] =
    //------------- Block2: Root Directory -------------//
    {
        // first entry is volume label
        'Y', 'A', 'P', 'i', 'c', 'o', 'p', 'r', 'o', 'b', 'e',            // DIR_Name
        0x08,                                                             // DIR_Attr: ATTR_VOLUME_ID
        0,                                                                // DIR_NTRes
        0,                                                                // DIR_CrtTimeTenth
        AWORD(0),                                                         // DIR_CrtTime
        AWORD(0),                                                         // DIR_CrtDate
        AWORD(0),                                                         // DIR_LstAccDate
        AWORD(0),                                                         // DIR_FstClusHi
        ATIME(12, 0, 0),                                                  // DIR_WrtTime
        ADATE(2022, 12, 6),                                               // DIR_WrtDate
        AWORD(0),                                                         // DIR_FstClusLO
        ADWORD(0),                                                        // DIR_FileSize

        // second entry is "0_README.TXT"
        '0', '_', 'R', 'E', 'A', 'D', 'M', 'E', 'T', 'X', 'T',
        0x01,                                                             // DIR_Attr: ATTR_READ_ONLY
        0,                                                                // DIR_NTRes
        0,                                                                // DIR_CrtTimeTenth
        ATIME(12, 0, 0),                                                  // DIR_CrtTime
        ADATE(2022, 12, 6),                                               // DIR_CrtDate
        ADATE(2022, 12, 6),                                               // DIR_LstAccDate
        AWORD(0),                                                         // DIR_FstClusHi
        ATIME(12, 0, 0),                                                  // DIR_WrtTime
        ADATE(2022, 12, 6),                                               // DIR_WrtDate
        AWORD(f_ReadmeStartCluster),                                      // DIR_FstClusLO
        ADWORD(README_SIZE),                                              // DIR_FileSize

        // third entry is "INFO_UF2.TXT"
        'I', 'N', 'F', 'O', '_', 'U', 'F', '2', 'T', 'X', 'T',
        0x01,                                                             // DIR_Attr: ATTR_READ_ONLY
        0,                                                                // DIR_NTRes
        0,                                                                // DIR_CrtTimeTenth
        ATIME(12, 0, 0),                                                  // DIR_CrtTime
        ADATE(2022, 12, 6),                                               // DIR_CrtDate
        ADATE(2022, 12, 6),                                               // DIR_LstAccDate
        AWORD(0),                                                         // DIR_FstClusHi
        ATIME(12, 0, 0),                                                  // DIR_WrtTime
        ADATE(2022, 12, 6),                                               // DIR_WrtDate
        AWORD(f_InfoUF2TxtStartCluster),                                  // DIR_FstClusLO
        ADWORD(INFOUF2_SIZE),                                             // DIR_FileSize

        // fourth entry is "INDEX.HTM"
        'I', 'N', 'D', 'E', 'X', ' ', ' ', ' ', 'H', 'T', 'M',
        0x01,                                                             // DIR_Attr: ATTR_READ_ONLY
        0,                                                                // DIR_NTRes
        0,                                                                // DIR_CrtTimeTenth
        ATIME(12, 0, 0),                                                  // DIR_CrtTime
        ADATE(2022, 12, 6),                                               // DIR_CrtDate
        ADATE(2022, 12, 6),                                               // DIR_LstAccDate
        AWORD(0),                                                         // DIR_FstClusHi
        ATIME(12, 0, 0),                                                  // DIR_WrtTime
        ADATE(2022, 12, 6),                                               // DIR_WrtDate
        AWORD(f_IndexHtmStartCluster),                                    // DIR_FstClusLO
        ADWORD(INDEXHTM_SIZE),                                            // DIR_FileSize

        // more entries are appended via append_dir_entry()
    };

static const uint8_t blank_dir_entry[] =
    {
        // second entry is "0_README.TXT"
        'a', 'b', 'c', 'd', 'e', 'f', 'g', 'h', 'i', 'j', 'k',
        0x01,                                                             // DIR_Attr: ATTR_READ_ONLY
        0,                                                                // DIR_NTRes
        0,                                                                // DIR_CrtTimeTenth
        ATIME(12, 0, 0),                                                  // DIR_CrtTime
        ADATE(2022, 12, 6),                                               // DIR_CrtDate
        ADATE(2022, 12, 6),                                               // DIR_LstAccDate
        AWORD(0),                                                         // DIR_FstClusHi
        ATIME(12, 0, 0),                                                  // DIR_WrtTime
        ADATE(2022, 12, 6),                                               // DIR_WrtDate
        AWORD(0xffff),                                                    // DIR_FstClusLO
        ADWORD(0xffffffff),                                               // DIR_FileSize
    };



/// Read a single sector from an input buffer.
/// Note that the input is checked against overflow, but the output buffer must have at least size of a sector.
static int32_t read_sector_from_buffer(void *sector_buffer, const uint8_t *src, uint32_t src_len, uint32_t sector_offs)
{
    uint32_t src_offs = BPB_BytsPerSec * sector_offs;

    if (src_offs > src_len) {
        memset(sector_buffer, 0xff, BPB_BytsPerSec);
    }
    else {
        uint32_t n = MIN(BPB_BytsPerSec, src_len - src_offs);
        memcpy(sector_buffer, src + src_offs, n);
        memset(sector_buffer + n, 0, BPB_BytsPerSec - n);
    }
    return BPB_BytsPerSec;
}   // read_sector_from_buffer



static void append_dir_entry(uint8_t *buf, const char *name, uint32_t start_cluster, uint32_t size)
{
    uint32_t n;

    for (n = 0;  n < BPB_BytsPerSec;  n += sizeof(blank_dir_entry)) {
        if (buf[n] == 0) {
            break;
        }
    }
    if (n <= BPB_BytsPerSec - sizeof(blank_dir_entry)) {
        memcpy(buf + n, blank_dir_entry, sizeof(blank_dir_entry));
        memcpy(buf + n, name, 11);
        buf[n + 26 + 0] = start_cluster & 0xff;
        buf[n + 26 + 1] = start_cluster >> 8;
        buf[n + 28 + 0] = size & 0xff;
        buf[n + 28 + 1] = size >>  8;
        buf[n + 28 + 2] = size >> 16;
        buf[n + 28 + 3] = size >> 24;
    }
    else {
        // directory is full, how to proceed?  (but this is a static / compile time problem...)
    }
}   // append_dir_entry



static void insert_fat_entry(uint8_t *buf, uint16_t start, uint16_t entry_no, uint16_t cluster_ref)
{
    uint16_t end = start + BPB_BytsPerSec - 1;
    uint16_t n;

//    cdc_debug_printf("%u %u\n", entry_no, cluster_ref);

    if ((entry_no & 0x01) == 0) {
        n = 3 * (entry_no / 2);
        if (n >= start  &&  n <= end) { buf[n - start] = cluster_ref & 0xff; }
        ++n;
        if (n >= start  &&  n <= end) { buf[n - start] = (buf[n - start] & 0xf0) | (cluster_ref >> 8); }
    }
    else {
        n = 3 * (entry_no / 2) + 1;
        if (n >= start  &&  n <= end) { buf[n - start] = (buf[n - start] & 0x0f) | ((cluster_ref & 0x0f) << 4); }
        ++n;
        if (n >= start  &&  n <= end) { buf[n - start] = (cluster_ref & 0xff0) >> 4; }
    }
}   // insert_fat_entry



// Invoked when received SCSI_CMD_INQUIRY
// Application fill vendor id, product id and revision with string up to 8, 16, 4 characters respectively
void tud_msc_inquiry_cb(uint8_t lun, uint8_t vendor_id[8], uint8_t product_id[16], uint8_t product_rev[4])
{
    const char vid[ 8] = "DAPLink\0";
    const char pid[16] = "YAPicoprobe\0\0\0\0\0";
    const char rev[]   = PICOPROBE_VERSION_STRING "\0\0\0\0";

    memcpy(vendor_id, vid, 8);
    memcpy(product_id, pid, 16);
    memcpy(product_rev, rev, 4);
//    picoprobe_info("tud_msc_inquiry_cb(%d, %s, %s, %s)\n", lun, vendor_id, product_id, product_rev);
}   // tud_msc_inquiry_cb



// Invoked when received Test Unit Ready command.
// return true allowing host to read/write this LUN e.g SD card inserted
bool tud_msc_test_unit_ready_cb(uint8_t lun)
{
    (void)lun;

//    picoprobe_info("tud_msc_test_unit_ready_cb(%d)\n", lun);
    
#if 0
    if (last_write_ms != 0) {
        uint64_t now_ms = time_us_64() / 1000;
        if (now_ms - last_write_ms > 500) {
            // TODO actually it would be enough to force the host to reread directory etc.
            // For codes see: https://github.com/tpn/winsdk-10/blob/master/Include/10.0.16299.0/shared/scsi.h
            picoprobe_info("tud_msc_test_unit_ready_cb(%d) - we had a write.  Remount device.\n", lun);
            tud_msc_set_sense(lun, SCSI_SENSE_NOT_READY, 0x3a, 0x00);             // tried a lot of other code -> no change (0x3a=SCSI_ADSENSE_NO_MEDIA_IN_DEVICE))
            last_write_ms = 0;
            return false;
        }
    }
#endif

    return true; // always ready
}   // tud_msc_test_unit_ready_cb



// Invoked when received SCSI_CMD_READ_CAPACITY_10 and SCSI_CMD_READ_FORMAT_CAPACITY to determine the disk size
// Application update block count and block size
void tud_msc_capacity_cb(uint8_t lun, uint32_t* block_count, uint16_t* block_size)
{
    (void)lun;

    *block_count = BPB_TotSec;
    *block_size = BPB_BytsPerSec;

//    picoprobe_info("tud_msc_capacity_cb(%d, %lu, %u)\n", lun, *block_count, *block_size);
}   // tud_msc_capacity_cb



// Invoked when received Start Stop Unit command
// - Start = 0 : stopped power mode, if load_eject = 1 : unload disk storage
// - Start = 1 : active mode, if load_eject = 1 : load disk storage
bool tud_msc_start_stop_cb(uint8_t lun, uint8_t power_condition, bool start, bool load_eject)
{
    (void)lun;
    (void)power_condition;

//    picoprobe_info("tud_msc_start_stop_cb(%d, %d, %d, %d)\n", lun, power_condition, start, load_eject);
    
    if (load_eject) {
        if (start) {
            // load disk storage
        } else {
            // unload disk storage
        }
    }

    return true;
}   // tud_msc_start_stop_cb



/// Callback invoked when received READ10 command.
/// Copy disk's data to buffer (up to bufsize) and return number of copied bytes.
/// Note that tinyusb tries to read ahead until the internal buffer is full (until CFG_TUD_MSC_EP_BUFSIZE).
///
/// \note
///    one can assume that \a bufsize >= 512
///
int32_t tud_msc_read10_cb(uint8_t lun, uint32_t lba, uint32_t offset, void* buffer, uint32_t bufsize)
{
    int32_t r = bufsize;

    assert(bufsize >= 512);

//    picoprobe_info("tud_msc_read10_cb(%d, %lu, %lu, 0x%p, %lu)\n", lun, lba, offset, buffer, bufsize);

    if (lba >= BPB_TotSec)
        return -1;

    if (lba >= c_BootStartSector  &&  lba < c_BootStartSector + c_BootSectors) {
//        picoprobe_info("  BOOT\n");
        r = MIN(bufsize, BPB_BytsPerSec);
        memcpy(buffer, bootsector, r);
    }
    else if (lba >= c_FatStartSector  &&  lba < c_FatStartSector + c_FatSectors) {
        //
        // FAT has to be generated dynamically because of CURRENT.UF2 and RAM.UF2
        //
        uint32_t block_no = lba - c_FatStartSector;

//        picoprobe_info("  FAT %lu\n", block_no);
        r = BPB_BytsPerSec;
        memset(buffer, 0, r);
        if (block_no == 0) {
            memcpy(buffer, fatsector, sizeof(fatsector));
        }
        for (uint16_t cluster = f_CurrentUF2StartCluster;  cluster < f_CurrentUF2StartCluster + f_CurrentUF2Clusters;  ++cluster) {
            insert_fat_entry(buffer, block_no * BPB_BytsPerSec,
                             cluster, cluster < (f_CurrentUF2StartCluster + f_CurrentUF2Clusters - 1) ? cluster + 1 : 0xfff);
        }
#ifdef INCLUDE_RAM_UF2
        for (uint16_t cluster = f_RAMUF2StartCluster;  cluster < f_RAMUF2StartCluster + f_RAMUF2Clusters;  ++cluster) {
            insert_fat_entry(buffer, block_no * BPB_BytsPerSec,
                             cluster, cluster < (f_RAMUF2StartCluster + f_RAMUF2Clusters - 1) ? cluster + 1 : 0xfff);
        }
#endif
    }
    else if (lba >= c_RootDirStartSector  &&  lba < c_RootDirStartSector + c_RootDirSectors) {
        //
        // CURRENT.UF2 and RAM.UF2 are dynamically created because they may vary in size depending on target
        //
//        picoprobe_info("  ROOTDIR\n");
        r = read_sector_from_buffer(buffer, rootdirsector, sizeof(rootdirsector), lba - c_RootDirStartSector);
        append_dir_entry(buffer, "CURRENT UF2", f_CurrentUF2StartCluster, TARGET_FLASH_UF2_SIZE);
#ifdef INCLUDE_RAM_UF2
        append_dir_entry(buffer, "RAM     UF2", f_RAMUF2StartCluster, TARGET_RAM_UF2_SIZE);
#endif
    }
    else if (lba >= f_ReadmeStartSector  &&  lba < f_ReadmeStartSector + f_ReadmeSectors) {
//        picoprobe_info("  README\n");
        r = read_sector_from_buffer(buffer, (const uint8_t *)README_CONTENTS, README_SIZE, lba - f_ReadmeStartSector);
    }
    else if (lba >= f_InfoUF2TxtStartSector  &&  lba < f_InfoUF2TxtStartSector + f_InfoUF2TxtSectors) {
        char buf[INFOUF2_SIZE];
        int n;

//        picoprobe_info("  INFO_UF2.TXT\n");
        n = snprintf(buf, sizeof(buf), INFOUF2_CONTENTS,
                     g_board_info.target_cfg->target_part_number,
                     msc_target_is_writable() ? "" : " (READONLY)",
                     g_board_info.board_name);
        for (int i = n;  i < sizeof(buf);  ++i) {
            buf[i] = ' ';
        }
        r = read_sector_from_buffer(buffer, (const uint8_t *)buf, sizeof(buf), lba - f_InfoUF2TxtStartSector);
    }
    else if (lba >= f_IndexHtmStartSector  &&  lba < f_IndexHtmStartSector + f_IndexHtmSectors) {
//        picoprobe_info("  INDEX.HTM\n");
        r = read_sector_from_buffer(buffer, (const uint8_t *)INDEXHTM_CONTENTS, INDEXHTM_SIZE, lba - f_IndexHtmStartSector);
    }
    else if (lba >= f_CurrentUF2StartSector  &&  lba < f_CurrentUF2StartSector + f_CurrentUF2Sectors) {
        const uint32_t payload_size = 256;
        const uint32_t num_blocks   = f_CurrentUF2Sectors;
        uint32_t block_no     = lba - f_CurrentUF2StartSector;
        uint32_t target_addr  = payload_size * block_no + TARGET_FLASH_IMG_BASE;
        struct uf2_block *uf2 = (struct uf2_block *)buffer;

        static_assert(BPB_BytsPerSec == 512);
        static_assert(payload_size <= sizeof(uf2->data));
        assert(bufsize >= sizeof(*uf2));

//        picoprobe_debug("  CURRENT.UF2 %lu 0x%lx\n", lba, target_addr);
        r = -1;
        if (msc_target_connect(false)) {
            if (msc_target_read_memory(uf2, target_addr, block_no, num_blocks) != 0) {
                r = BPB_BytsPerSec;
            }
        }
    }
#ifdef INCLUDE_RAM_UF2
    else if (lba >= f_RAMUF2StartSector  &&  lba < f_RAMUF2StartSector + f_RAMUF2Sectors) {
        const uint32_t payload_size = 256;
        const uint32_t num_blocks   = f_RAMUF2Sectors;
        uint32_t block_no     = lba - f_RAMUF2StartSector;
        uint32_t target_addr  = payload_size * block_no + TARGET_RAM_IMG_BASE;
        struct uf2_block *uf2 = (struct uf2_block *)buffer;

        static_assert(BPB_BytsPerSec == 512);
        static_assert(payload_size <= sizeof(uf2->data));
        assert(bufsize >= sizeof(*uf2));

//        picoprobe_info("  RAM.UF2 0x%lx\n", target_addr);
        r = -1;
        if (msc_target_connect(false)) {
            if (msc_target_read_memory(uf2, target_addr, block_no, num_blocks) != 0) {
                r = BPB_BytsPerSec;
            }
        }
//        picoprobe_info("    %lu\n", r);
    }
#endif
    else {
//        picoprobe_info("  OTHER\n");
        memset(buffer, 0, bufsize);
    }

    return r;
}   // tud_msc_read10_cb



bool tud_msc_is_writable_cb(uint8_t lun)
{
    (void)lun;

//    picoprobe_info("tud_msc_is_writable_cb(%d)\n", lun);

    return msc_target_is_writable();
}   // tud_msc_is_writable_cb



// Callback invoked when received WRITE10 command.
// Process data in buffer to disk's storage and return number of written bytes
int32_t tud_msc_write10_cb(uint8_t lun, uint32_t lba, uint32_t offset, uint8_t* buffer, uint32_t bufsize)
{
    int32_t r = bufsize;

    (void)lun;

//    picoprobe_info("tud_msc_write10_cb(%d, %lu, %lu, 0x%p, %lu)\n", lun, lba, offset, buffer, bufsize);
    
    if (lba >= BPB_TotSec)
        return -1;

    if (lba >= c_BootStartSector  &&  lba < c_BootStartSector + c_BootSectors) {
//        picoprobe_info("  BOOT\n");
        r = MIN(bufsize, BPB_BytsPerSec);
#if 0
        for (int i = 0;  i < 512;  ++i) {
            if (i % 32 == 0)
                cdc_debug_printf("\n");
            else if (i % 16 == 0)
                cdc_debug_printf(" ");
            cdc_debug_printf(" %02x", buffer[i]);
        }
        cdc_debug_printf("\n");
#endif
    }
    else if (lba >= c_FatStartSector  &&  lba < c_FatStartSector + c_FatSectors) {
//        picoprobe_info("  FAT\n");
        r = MIN(bufsize, BPB_BytsPerSec);
    }
    else if (lba >= c_RootDirStartSector  &&  lba < c_RootDirStartSector + c_RootDirSectors) {
//        picoprobe_info("  ROOTDIR\n");
        r = MIN(bufsize, BPB_BytsPerSec);
    }
    else {
        r = -1;
//        picoprobe_info("  tud_msc_write10_cb: check if UF2 data\n");
        if (msc_is_uf2_record(buffer,  bufsize)) {
            if (msc_target_connect(true)) {
                if (msc_target_write_memory((struct uf2_block *)buffer) != 0) {
                    static_assert(sizeof(struct uf2_block) == 512);
                    r = sizeof(struct uf2_block);
                }
            }
        }
    }

    last_write_ms = (uint32_t)(time_us_64() / 1000);
    return r;
}   // tud_msc_write10_cb



// Callback invoked when received an SCSI command not in built-in list below
// - READ_CAPACITY10, READ_FORMAT_CAPACITY, INQUIRY, MODE_SENSE6, REQUEST_SENSE
// - READ10 and WRITE10 has their own callbacks
int32_t tud_msc_scsi_cb(uint8_t lun, uint8_t const scsi_cmd[16], void* buffer, uint16_t bufsize)
{
    int32_t resplen = 0;

    switch (scsi_cmd[0]) {
    	case SCSI_CMD_PREVENT_ALLOW_MEDIUM_REMOVAL:
            /* SCSI_CMD_PREVENT_ALLOW_MEDIUM_REMOVAL is the Prevent/Allow Medium Removal
            command (1Eh) that requests the library to enable or disable user access to
            the storage media/partition. */
//    		picoprobe_debug("tud_msc_scsi_cb() invoked: SCSI_CMD_PREVENT_ALLOW_MEDIUM_REMOVAL\n");
            resplen = 0;
            break;
	   default:
//		    picoprobe_info("tud_msc_scsi_cb(%d, %02x %02x %02x %02x, 0x%p, %u)\n", lun, scsi_cmd[0], scsi_cmd[1], scsi_cmd[2], scsi_cmd[3], buffer, bufsize);

            // Set Sense = Invalid Command Operation
            tud_msc_set_sense(lun, SCSI_SENSE_ILLEGAL_REQUEST, 0x20, 0x00);

            // negative means error -> tinyusb could stall and/or response with failed status
            resplen = -1;
            break;
    }

    return resplen;
}   // tud_msc_scsi_cb

#endif
