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
#include "get_serial.h"


//--------------------------------------------------------------------+
// Device Descriptors
//--------------------------------------------------------------------+
tusb_desc_device_t const desc_device =
{
    .bLength            = sizeof(tusb_desc_device_t),
    .bDescriptorType    = TUSB_DESC_DEVICE,
    .bcdUSB             = 0x0110, // // USB Specification version 1.1
    .bDeviceClass       = 0x00, // Each interface specifies its own
    .bDeviceSubClass    = 0x00, // Each interface specifies its own
    .bDeviceProtocol    = 0x00,
    .bMaxPacketSize0    = CFG_TUD_ENDPOINT0_SIZE,

    .idVendor           = 0x2E8A, // Pi
#if PICO_NO_FLASH
    .idProduct          = 0xF004, // Picoprobe - debug
#else
    .idProduct          = 0x0004, // Picoprobe
#endif
    .bcdDevice          = 0x0100, // Version 01.00
    .iManufacturer      = 0x01,
    .iProduct           = 0x02,
    .iSerialNumber      = 0x03,
    .bNumConfigurations = 0x01
};

// Invoked when received GET DEVICE DESCRIPTOR
// Application return pointer to descriptor
uint8_t const * tud_descriptor_device_cb(void)
{
  return (uint8_t const *) &desc_device;
}

//--------------------------------------------------------------------+
// Configuration Descriptor
//--------------------------------------------------------------------+

enum
{
  ITF_NUM_CDC_COM,
  ITF_NUM_CDC_DATA,
  ITF_NUM_PROBE,
  ITF_NUM_CDC_SUMP_COM,
  ITF_NUM_CDC_SUMP_DATA,
  ITF_NUM_TOTAL
};

#define CDC_NOTIFICATION_EP_NUM 0x81
#define CDC_DATA_OUT_EP_NUM 0x02
#define CDC_DATA_IN_EP_NUM 0x83
#define PROBE_OUT_EP_NUM 0x04
#define PROBE_IN_EP_NUM 0x85
#define CDC_SUMP_NOTIFICATION_EP_NUM 0x86
#define CDC_SUMP_DATA_OUT_EP_NUM 0x07
#define CDC_SUMP_DATA_IN_EP_NUM 0x88

#define CONFIG_TOTAL_LEN  (TUD_CONFIG_DESC_LEN + TUD_CDC_DESC_LEN + \
                           TUD_VENDOR_DESC_LEN + TUD_CDC_DESC_LEN)

uint8_t const desc_configuration[] =
{
  TUD_CONFIG_DESCRIPTOR(1, ITF_NUM_TOTAL, 0, CONFIG_TOTAL_LEN, TUSB_DESC_CONFIG_ATT_REMOTE_WAKEUP, 100),

  // Interface 0 + 1
  TUD_CDC_DESCRIPTOR(ITF_NUM_CDC_COM, 0, CDC_NOTIFICATION_EP_NUM, 64, CDC_DATA_OUT_EP_NUM, CDC_DATA_IN_EP_NUM, 64),

  // Interface 2
  TUD_VENDOR_DESCRIPTOR(ITF_NUM_PROBE, 0, PROBE_OUT_EP_NUM, PROBE_IN_EP_NUM, 64),

  // Interface 3 + 4
  TUD_CDC_DESCRIPTOR(ITF_NUM_CDC_SUMP_COM, 0, CDC_SUMP_NOTIFICATION_EP_NUM, 64,
                     CDC_SUMP_DATA_OUT_EP_NUM, CDC_SUMP_DATA_IN_EP_NUM, 64)
};

// Invoked when received GET CONFIGURATION DESCRIPTOR
// Application return pointer to descriptor
// Descriptor contents must exist long enough for transfer to complete
uint8_t const * tud_descriptor_configuration_cb(uint8_t index)
{
  (void) index; // for multiple configurations
  return desc_configuration;
}

//--------------------------------------------------------------------+
// String Descriptors
//--------------------------------------------------------------------+

// array of pointer to string descriptors
char const* string_desc_arr [] =
{
  (const char[]) { 0x09, 0x04 }, // 0: is supported language is English (0x0409)
  "Raspberry Pi", // 1: Manufacturer
  "Picoprobe",    // 2: Product
  usb_serial,     // 3: Serial, uses flash unique ID
};

static uint16_t _desc_str[32];

// Invoked when received GET STRING DESCRIPTOR request
// Application return pointer to descriptor, whose contents must exist long enough for transfer to complete
uint16_t const* tud_descriptor_string_cb(uint8_t index, uint16_t langid)
{
  (void) langid;

  uint8_t chr_count;

  if ( index == 0)
  {
    memcpy(&_desc_str[1], string_desc_arr[0], 2);
    chr_count = 1;
  }else
  {
    // Convert ASCII string into UTF-16

    if ( !(index < sizeof(string_desc_arr)/sizeof(string_desc_arr[0])) ) return NULL;

    const char* str = string_desc_arr[index];

    // Cap at max char
    chr_count = strlen(str);
    if ( chr_count > 31 ) chr_count = 31;

    for(uint8_t i=0; i<chr_count; i++)
    {
      _desc_str[1+i] = str[i];
    }
  }

  // first byte is length (including header), second byte is string type
  _desc_str[0] = (TUSB_DESC_STRING << 8 ) | (2*chr_count + 2);

  return _desc_str;
}
