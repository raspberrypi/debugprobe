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

#include "tusb.h"
#include "picoprobe_config.h"



#if CFG_TUD_MSC

#define ADWORD(X)       (X) & 0xff, ((X) & 0xff00) >> 8, ((X) & 0xff0000) >> 16, ((X) & 0xff000000) >> 24
#define AWORD(X)        (X) & 0xff, ((X) & 0xff00) >> 8
#define ADATE(Y,M,D)    AWORD((((Y)-1980) << 9) + ((M) << 5) + (D))
#define ATIME(H,M,S)    AWORD(((H) << 11) + ((M) << 5) + ((S) / 2))
#define SECTORS(BYTES)  (((BYTES) + BPB_BytsPerSec - 1) / BPB_BytsPerSec)
#define CLUSTERS(BYTES) (((BYTES) + BPB_BytsPerClus - 1) / BPB_BytsPerClus)
#define AFAT12(C1,C2)   (C1) & 0xff, (((C1) & 0xf00) >> 8) + (((C2) & 0x0f) << 4), (((C2) & 0xff0) >> 4)

#define README_CONTENTS \
"This is the Raspberry Pi Pico Target Flash Drive.\r\n\r\n\
- fetch RP2040.BIN to fetch the whole target flash memory\r\n\
- drop a UF2 file to flash the target device\r\n"

#define BPB_BytsPerSec         512UL
#define BPB_BytsPerClus        65536UL
const uint16_t BPB_TotSec16    = 32768;                                         // 16MB
const uint8_t  BPB_SecPerClus  = SECTORS(BPB_BytsPerClus);                      // cluster size (65536 -> BPB_SecPerClus=128)
const uint16_t BPB_RootEntCnt  = BPB_BytsPerSec / 32;                           // only one sector for root directory
const uint16_t BPB_ResvdSecCnt = 1;
const uint8_t  BPB_NumFATs     = 1;
const uint16_t BPB_FATSz16     = 1;                                             // -> ~340 cluster fit into one sector for FAT12
const uint32_t BS_VolID        = 0x1234;
const uint8_t  BPB_Media       = 0xf8;                                          // f0=floppy, f8=HDD, fa=RAM disk (format prompt)
const uint8_t  BS_DrvNum       = 0x80;                                          // 00=floppy, 80=fixed disk

// some calulations
const uint32_t c_TotalCluster = BPB_TotSec16 / BPB_SecPerClus;                  // -> 256 cluster for 16MB total size and 64KByte cluster size
const uint32_t c_BootStartSector = 0;
const uint32_t c_BootSectors = 1;                                               // must be 1
const uint32_t c_FatStartSector = BPB_ResvdSecCnt;
const uint32_t c_FatSectors = BPB_FATSz16 * BPB_NumFATs;                        // must be 1
const uint32_t c_RootDirStartSector = c_FatStartSector + c_FatSectors;
const uint32_t c_RootDirSectors = SECTORS(32 * BPB_RootEntCnt);                 // must be 1
const uint32_t c_DataStartSector = c_RootDirStartSector + c_RootDirSectors;

#define c_FirstSectorofCluster(N) (c_DataStartSector + ((N) - 2) * BPB_SecPerClus)

#define RP2040_IMG_SIZE        0x200000

const uint32_t c_ReadmeStartCluster = 2;
const uint32_t c_ReadmeClusters = 1;
const uint32_t c_ReadmeStartSector = c_FirstSectorofCluster(c_ReadmeStartCluster);
const uint32_t c_ReadmeSectors = BPB_SecPerClus * c_ReadmeClusters;

const uint32_t c_RP2040StartCluster = 4;
const uint32_t c_RP2040Clusters = CLUSTERS(RP2040_IMG_SIZE);
const uint32_t c_RP2040StartSector = c_FirstSectorofCluster(c_RP2040StartCluster);
const uint32_t c_RP2040Sectors = BPB_SecPerClus * c_RP2040Clusters;

static uint64_t last_write_ms = 0;

uint8_t bootsector[BPB_BytsPerSec] =
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
        AWORD(BPB_TotSec16),                              // BPB_TotSec16 / BPB_SecPerClus determines FAT type, there are legal 340 cluster -> FAT12
        BPB_Media,
        AWORD(BPB_FATSz16),
        AWORD(1),                                         // BPB_SecPerTrk
        AWORD(1),                                         // BPB_NumHeads
        ADWORD(0),                                        // BPB_HiddSec
        ADWORD(0),                                        // BPB_TotSec32

        // byte 36 and more:
                           BS_DrvNum, 0x00, 0x29,       ADWORD(BS_VolID),  'P',  'i',  'P',  'r',  'o',
         'b',  'e',  ' ',  'M',  'S',  'C', 
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

uint8_t fatsector[BPB_BytsPerSec] =
    //------------- Block1: FAT16 Table -------------//
    {
        // cluster 0 & 1
        AFAT12(0xf00 + BPB_Media, 0xfff), 
        
        // cluster 2 (0_README) & 3 (dummy) - must be c_ReadmeStartCluster
        AFAT12(0xfff, 0),

        // cluster 4..36 - must be c_RP2040StartCluster
        AFAT12( 5,  6), AFAT12( 7,  8), AFAT12( 9, 10), AFAT12(11, 12),
        AFAT12(13, 14), AFAT12(15, 16), AFAT12(17, 18), AFAT12(19, 20),
        AFAT12(21, 22), AFAT12(23, 24), AFAT12(25, 26), AFAT12(27, 28),
        AFAT12(29, 30), AFAT12(31, 32), AFAT12(33, 34), AFAT12(35, 0xfff),
    };

uint8_t rootdirsector[BPB_BytsPerSec] =
    //------------- Block2: Root Directory -------------//
    {
        // first entry is volume label
        'P', 'i', 'P', 'r', 'o', 'b', 'e', ' ', 'M', 'S', 'C',            // DIR_Name
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

        // second entry is readme file
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
        AWORD(c_ReadmeStartCluster),                                      // DIR_FstClusLO
        ADWORD(sizeof(README_CONTENTS) - 1),                              // DIR_FileSize

        // third entry is the current flash content of the target
        'R', 'P', '2', '0', '4', '0', ' ', ' ', 'B', 'I', 'N',
        0x01,                                                             // DIR_Attr: ATTR_READ_ONLY
        0,                                                                // DIR_NTRes
        0,                                                                // DIR_CrtTimeTenth
        ATIME(12, 0, 0),                                                  // DIR_CrtTime
        ADATE(2022, 12, 6),                                               // DIR_CrtDate
        ADATE(2022, 12, 6),                                               // DIR_LstAccDate
        AWORD(0),                                                         // DIR_FstClusHi
        ATIME(12, 0, 0),                                                  // DIR_WrtTime
        ADATE(2022, 12, 6),                                               // DIR_WrtDate
        AWORD(c_RP2040StartCluster),                                      // DIR_FstClusLO
        ADWORD(RP2040_IMG_SIZE),                                          // DIR_FileSize
    };



/// Read a single sector from an input buffer.
/// Note that the input is checked against overflow, but the output buffer must have at least size of a sector.
int32_t read_sector_from_buffer(void *sector_buffer, const uint8_t *src, uint32_t src_len, uint32_t sector_offs)
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
}



// Invoked when received SCSI_CMD_INQUIRY
// Application fill vendor id, product id and revision with string up to 8, 16, 4 characters respectively
void tud_msc_inquiry_cb(uint8_t lun, uint8_t vendor_id[8], uint8_t product_id[16], uint8_t product_rev[4])
{
    const char vid[] = "PiProbe";
    const char pid[] = "Target Flash";
    const char rev[] = "1.0";

    strncpy((char *)vendor_id, vid, 8);
    strncpy((char *)product_id, pid, 16);
    strncpy((char *)product_rev, rev, 4);

    picoprobe_info("tud_msc_inquiry_cb(%d, %s, %s, %s)\n", lun, vendor_id, product_id, product_rev);    
}



// Invoked when received Test Unit Ready command.
// return true allowing host to read/write this LUN e.g SD card inserted
bool tud_msc_test_unit_ready_cb(uint8_t lun)
{
    (void)lun;

    // picoprobe_info("tud_msc_test_unit_ready_cb(%d)\n", lun);
    
    if (last_write_ms != 0) {
        uint64_t now_ms = time_us_64() / 1000;
        if (now_ms - last_write_ms > 500) {
            tud_msc_set_sense(lun, SCSI_SENSE_NOT_READY, 0x3a, 0x00);   // or SCSI_SENSE_RECOVERED_ERROR?
            last_write_ms = 0;
            return false;
        }
    }

    return true; // always ready
}



// Invoked when received SCSI_CMD_READ_CAPACITY_10 and SCSI_CMD_READ_FORMAT_CAPACITY to determine the disk size
// Application update block count and block size
void tud_msc_capacity_cb(uint8_t lun, uint32_t* block_count, uint16_t* block_size)
{
    (void)lun;

    *block_count = BPB_TotSec16;
    *block_size = BPB_BytsPerSec;

    picoprobe_info("tud_msc_capacity_cb(%d, %lu, %u)\n", lun, *block_count, *block_size);    
}



// Invoked when received Start Stop Unit command
// - Start = 0 : stopped power mode, if load_eject = 1 : unload disk storage
// - Start = 1 : active mode, if load_eject = 1 : load disk storage
bool tud_msc_start_stop_cb(uint8_t lun, uint8_t power_condition, bool start, bool load_eject)
{
    (void)lun;
    (void)power_condition;

    picoprobe_info("tud_msc_start_stop_cb(%d, %d, %d, %d)\n", lun, power_condition, start, load_eject);
    
    if (load_eject) {
        if (start) {
            // load disk storage
        } else {
            // unload disk storage
        }
    }

    return true;
}



/// Callback invoked when received READ10 command.
/// Copy disk's data to buffer (up to bufsize) and return number of copied bytes.
/// Note that tinyusb tries to read ahead until the internal buffer is full (until CFG_TUD_MSC_EP_BUFSIZE).
int32_t tud_msc_read10_cb(uint8_t lun, uint32_t lba, uint32_t offset, void* buffer, uint32_t bufsize)
{
    int32_t r = bufsize;

    picoprobe_info("tud_msc_read10_cb(%d, %lu, %lu, 0x%p, %lu)\n", lun, lba, offset, buffer, bufsize);

    // out of ramdisk
    if (lba >= BPB_TotSec16)
        return -1;

    if (lba >= c_BootStartSector  &&  lba < c_BootStartSector + c_BootSectors) {
        picoprobe_info("  BOOT\n");
        r = MIN(bufsize, BPB_BytsPerSec);
        memcpy(buffer, bootsector, r);
    }
    else if (lba >= c_FatStartSector  &&  lba < c_FatStartSector + c_FatSectors) {
        picoprobe_info("  FAT\n");
        r = MIN(bufsize, BPB_BytsPerSec);
        memcpy(buffer, fatsector, r);
    }
    else if (lba >= c_RootDirStartSector  &&  lba < c_RootDirStartSector + c_RootDirSectors) {
        picoprobe_info("  ROOTDIR\n");
        r = MIN(bufsize, BPB_BytsPerSec);
        memcpy(buffer, rootdirsector, r);
    }
    else if (lba >= c_ReadmeStartSector  &&  lba < c_ReadmeStartSector + c_ReadmeSectors) {
        picoprobe_info("  README\n");
        r = read_sector_from_buffer(buffer, (const uint8_t *)README_CONTENTS, sizeof(README_CONTENTS)-1, lba - c_ReadmeStartSector);
    }
    else if (lba >= c_RP2040StartSector  &&  lba < c_RP2040StartSector + c_RP2040Sectors) {
        picoprobe_info("  RP2040\n");
        memset(buffer, lba & 0xff, bufsize);
    }
    else {
        picoprobe_info("  OTHER\n");
        memset(buffer, 0, bufsize);
    }

    return r;
}



bool tud_msc_is_writable_cb(uint8_t lun)
{
    (void)lun;

    picoprobe_info("tud_msc_is_writable_cb(%d)\n", lun);

    return true;
}



// Callback invoked when received WRITE10 command.
// Process data in buffer to disk's storage and return number of written bytes
int32_t tud_msc_write10_cb(uint8_t lun, uint32_t lba, uint32_t offset, uint8_t* buffer, uint32_t bufsize)
{
    int32_t r = bufsize;

    (void)lun;

    picoprobe_info("tud_msc_write10_cb(%d, %lu, %lu, 0x%p, %lu)\n", lun, lba, offset, buffer, bufsize);
    
    // out of ramdisk
    if (lba >= BPB_TotSec16)
        return -1;

    if (lba >= c_BootStartSector  &&  lba < c_BootStartSector + c_BootSectors) {
        picoprobe_info("  BOOT\n");
        r = MIN(bufsize, BPB_BytsPerSec);
    }
    else if (lba >= c_FatStartSector  &&  lba < c_FatStartSector + c_FatSectors) {
        picoprobe_info("  FAT\n");
        r = MIN(bufsize, BPB_BytsPerSec);
    }
    else if (lba >= c_RootDirStartSector  &&  lba < c_RootDirStartSector + c_RootDirSectors) {
        picoprobe_info("  ROOTDIR\n");
        r = MIN(bufsize, BPB_BytsPerSec);
    }
    else if (lba >= c_ReadmeStartSector  &&  lba < c_ReadmeStartSector + c_ReadmeSectors) {
        picoprobe_info("  README\n");
        r = BPB_BytsPerSec;
    }
    else if (lba >= c_RP2040StartSector  &&  lba < c_RP2040StartSector + c_RP2040Sectors) {
        picoprobe_info("  RP2040\n");
        r = BPB_BytsPerSec;
    }
    else {
        picoprobe_info("  OTHER\n");
    }

    last_write_ms = (uint32_t)(time_us_64() / 1000);

    return r;
}



// Callback invoked when received an SCSI command not in built-in list below
// - READ_CAPACITY10, READ_FORMAT_CAPACITY, INQUIRY, MODE_SENSE6, REQUEST_SENSE
// - READ10 and WRITE10 has their own callbacks
int32_t tud_msc_scsi_cb(uint8_t lun, uint8_t const scsi_cmd[16], void* buffer, uint16_t bufsize)
{
    void const* response = NULL;
    int32_t resplen = 0;

    picoprobe_info("tud_msc_scsi_cb(%d, %02x %02x %02x %02x, 0x%p, %u)\n", lun, scsi_cmd[0], scsi_cmd[1], scsi_cmd[2], scsi_cmd[3], buffer, bufsize);
    
    // most scsi handled is input
    bool in_xfer = true;

    switch (scsi_cmd[0]) {
        default:
            // Set Sense = Invalid Command Operation
            tud_msc_set_sense(lun, SCSI_SENSE_ILLEGAL_REQUEST, 0x20, 0x00);

            // negative means error -> tinyusb could stall and/or response with failed status
            resplen = -1;
            break;
    }

    // return resplen must not larger than bufsize
    if (resplen > bufsize)
        resplen = bufsize;

    if (response && (resplen > 0)) {
        if (in_xfer) {
            memcpy(buffer, response, resplen);
        } else {
            // SCSI output
        }
    }

    return resplen;
}

#endif
