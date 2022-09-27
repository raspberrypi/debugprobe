/*
 * The MIT License (MIT)
 *
 * Copyright (c) 2022 Vlad Tomoiaga (tvlad1234)
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

#include "bsp/board.h"
#include "tusb.h"

#include "pico/stdlib.h"
#include "hardware/flash.h"

#if CFG_TUD_MSC

#define WP_PIN 14

#define FLASH_OFFSET_KB 48
#define FLASH_TARGET_OFFSET (FLASH_OFFSET_KB * 1024)
uint8_t *flash_target_contents = (uint8_t *)(XIP_BASE + FLASH_TARGET_OFFSET);

enum
{
    // FLASH_DISK_BLOCK_NUM = 490,
    FLASH_DISK_BLOCK_NUM = ((2048 - FLASH_OFFSET_KB) / 4),
    FLASH_DISK_BLOCK_SIZE = 4096
};

#if SERIAL_LOG
char printBuf[100]; // buffer for printing messages to serial
#endif

static uint8_t lba_buffer[FLASH_DISK_BLOCK_SIZE]; // buffer to write to
static uint32_t prevWriteLba = -1;                // last LBA that's been written to

bool read_wp_pin(void)
{
    static bool pin_init = false;
    if (!pin_init)
    {
        gpio_init(WP_PIN);
        gpio_set_dir(WP_PIN, GPIO_IN);
        gpio_pull_up(WP_PIN);
        sleep_ms(10);
        pin_init = true;
    }
    return !gpio_get(WP_PIN);
}

// Update flash
void update_flash_block(uint32_t block, uint8_t *data)
{

#if SERIAL_LOG
    if (tud_cdc_connected())
    {
        sprintf(printBuf, "FLASH: updating block %d\n\r", prevWriteLba);
        tud_cdc_write_str(printBuf);
    }
#endif

    // write the previous block to flash
    uint32_t ints = save_and_disable_interrupts();                                  // disable interrupts (needed if running from flash)
    flash_range_erase(FLASH_TARGET_OFFSET + (FLASH_DISK_BLOCK_SIZE * block), 4096); // need to erase first
    flash_range_program(FLASH_TARGET_OFFSET + (FLASH_DISK_BLOCK_SIZE * block), data, FLASH_DISK_BLOCK_SIZE);
    restore_interrupts(ints); // restore interrupts
}

// Invoked to determine max LUN
uint8_t tud_msc_get_maxlun_cb(void)
{
    return 1; // we only have 1 LUN
}

// Invoked when received SCSI_CMD_INQUIRY
// Application fill vendor id, product id and revision with string up to 8, 16, 4 characters respectively
void tud_msc_inquiry_cb(uint8_t lun, uint8_t vendor_id[8], uint8_t product_id[16], uint8_t product_rev[4])
{
    char vid[] = "TinyUSB";
    char pid[] = "Mass Storage";
    char rev[] = "1.0";

    memcpy(vendor_id, vid, strlen(vid));
    memcpy(product_id, pid, strlen(pid));
    memcpy(product_rev, rev, strlen(rev));
}

// Invoked when received Test Unit Ready command.
// return true allowing host to read/write this LUN e.g SD card inserted
bool tud_msc_test_unit_ready_cb(uint8_t lun)
{
    (void)lun;

    return true; // always ready
}

// Invoked when received SCSI_CMD_READ_CAPACITY_10 and SCSI_CMD_READ_FORMAT_CAPACITY to determine the disk size
// Application update block count and block size
void tud_msc_capacity_cb(uint8_t lun, uint32_t *block_count, uint16_t *block_size)
{
    switch (lun)
    {
    case 0: // Flash
        *block_count = FLASH_DISK_BLOCK_NUM;
        *block_size = FLASH_DISK_BLOCK_SIZE;
        break;

    default:
        *block_count = 0;
        *block_count = 0;
        break;
    }
}

// Invoked when received Start Stop Unit command
// - Start = 0 : stopped power mode, if load_eject = 1 : unload disk storage
// - Start = 1 : active mode, if load_eject = 1 : load disk storage
bool tud_msc_start_stop_cb(uint8_t lun, uint8_t power_condition, bool start, bool load_eject)
{
    (void)power_condition;

    if (load_eject)
    {
        if (start)
        {
            // load disk storage
        }
        else
        {
            // unload disk storage
        }
    }

    return true;
}

// Callback invoked when received READ10 command.
// Copy disk's data to buffer (up to bufsize) and return number of copied bytes.
int32_t tud_msc_read10_cb(uint8_t lun, uint32_t lba, uint32_t offset, void *buffer, uint32_t bufsize)
{

#if SERIAL_LOG
    if (tud_cdc_connected())
    {
        sprintf(printBuf, "LUN%d: read %d bytes from block %d, offset %d, ", lun, bufsize, lba, offset);
        tud_cdc_write_str(printBuf);
        if (lba == prevWriteLba)
            sprintf(printBuf, "from buffer\n\r");
        else
            sprintf(printBuf, "from flash\n\r");
        tud_cdc_write_str(printBuf);
    }
#endif

    // out of disk
    switch (lun)
    {
    case 0:
        if (lba >= FLASH_DISK_BLOCK_NUM)
            return -1;
        break;

    default:
        return -1;
        break;
    }

    uint8_t const *addr;
    switch (lun)
    {
    case 0:
        if (lba == prevWriteLba)
            addr = lba_buffer + offset;
        else
            addr = flash_target_contents + (FLASH_DISK_BLOCK_SIZE * lba) + offset;
        break;

    default:
        return -1;
        break;
    }

    memcpy(buffer, addr, bufsize);
    return bufsize;
}

bool tud_msc_is_writable_cb(uint8_t lun)
{
    (void)lun;
    return read_wp_pin(); // we can write to all LUNs
}

// Callback invoked when received WRITE10 command.
// Process data in buffer to disk's storage and return number of written bytes
int32_t tud_msc_write10_cb(uint8_t lun, uint32_t lba, uint32_t offset, uint8_t *buffer, uint32_t bufsize)
{

#if SERIAL_LOG
    if (tud_cdc_connected())
    {
        sprintf(printBuf, "LUN%d: write %d bytes to block %d, offset %d\n\r", lun, bufsize, lba, offset);
        tud_cdc_write_str(printBuf);
    }
#endif

    // out of disk
    switch (lun)
    {
    case 0:
        if (lba >= FLASH_DISK_BLOCK_NUM)
            return -1;
        break;

    default:
        return -1;
        break;
    }

    uint8_t *addr;
    switch (lun)
    {
    case 0: // Flash
        addr = flash_target_contents + (FLASH_DISK_BLOCK_SIZE * lba);

        if (lba != prevWriteLba) // flush the buffer to the flash if we're moving to another LBA
            update_flash_block(prevWriteLba, lba_buffer);

        // write to the buffer
        memcpy(lba_buffer + offset, buffer, bufsize);
        prevWriteLba = lba;
        break;

    default:
        return -1;
        break;
    }

    return bufsize;
}

// Callback invoked when received an SCSI command not in built-in list below
// - READ_CAPACITY10, READ_FORMAT_CAPACITY, INQUIRY, MODE_SENSE6, REQUEST_SENSE
// - READ10 and WRITE10 has their own callbacks
int32_t tud_msc_scsi_cb(uint8_t lun, uint8_t const scsi_cmd[16], void *buffer, uint16_t bufsize)
{
    // read10 & write10 has their own callback and MUST not be handled here

    void const *response = NULL;
    int32_t resplen = 0;

    // most scsi handled is input
    bool in_xfer = true;

    switch (scsi_cmd[0])
    {
    case SCSI_CMD_PREVENT_ALLOW_MEDIUM_REMOVAL:
        // Host is about to read/write etc ... better not to disconnect disk

#if SERIAL_LOG
        if (tud_cdc_connected())
        {
            sprintf(printBuf, "Ejected LUN %d\n\r", lun);
            tud_cdc_write_str(printBuf);
        }
#endif

        if (lun == 0 && prevWriteLba > -1) // Flush buffer to flash on eject
            update_flash_block(prevWriteLba, lba_buffer);

        resplen = 0;
        break;

    case SCSI_CMD_START_STOP_UNIT:
        // Host try to eject/safe remove/poweroff us. We could safely disconnect with disk storage, or go into lower power
        // scsi_start_stop_unit_t const * start_stop = (scsi_start_stop_unit_t const *) scsi_cmd;
        // Start bit = 0 : low power mode, if load_eject = 1 : unmount disk storage as well
        // Start bit = 1 : Ready mode, if load_eject = 1 : mount disk storage
        //  start_stop->start;
        //  start_stop->load_eject;

        resplen = 0;
        break;

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

    if (response && (resplen > 0))
    {
        if (in_xfer)
        {
            memcpy(buffer, response, resplen);
        }
        else
        {
            // SCSI output
        }
    }

    return resplen;
}

#endif
