/*
 * The MIT License (MIT)
 *
 * Copyright (c) 2019 Ha Thach (tinyusb.org)
 * Copyright (c) 2021 Peter Lawrence
 * Copyright (c) 2022 Raspberry Pi Ltd
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
#include "picoprobe_config.h"


//--------------------------------------------------------------------+
// String enums
//--------------------------------------------------------------------+

// OPT_NET must be enabled for any effect
// 0 -> ECM, ECM has good throughput, but no driver for Win10
// 1 -> RNDIS, RNDIS must be the only USB class under windows to get RNDIS work successfully
#define USE_RNDIS    1

enum
{
    STRID_LANGID = 0,
    STRID_MANUFACTURER,
    STRID_PRODUCT,
    STRID_SERIAL,
    STRID_INTERFACE_DAP2,
    STRID_INTERFACE_DAP1,
    STRID_INTERFACE_MSC,
    STRID_INTERFACE_CDC_UART,
#if OPT_SIGROK
    STRID_INTERFACE_CDC_SIGROK,
#endif
    STRID_INTERFACE_CDC_DEBUG,
#if OPT_NET
    STRID_INTERFACE_NET,
    STRID_MAC,
#endif
};


//--------------------------------------------------------------------+
// Device Descriptors
//--------------------------------------------------------------------+

tusb_desc_device_t const desc_device =
{
    .bLength            = sizeof(tusb_desc_device_t),
    .bDescriptorType    = TUSB_DESC_DEVICE,
#if OPT_CMSIS_DAPV2                                     // USB Specification version 2.1 for BOS (DAPv2) under Windows
    .bcdUSB             = 0x0210,
#else
    .bcdUSB             = 0x0200,
#endif

    // Use Interface Association Descriptor (IAD) device class
    .bDeviceClass       = TUSB_CLASS_MISC,
    .bDeviceSubClass    = MISC_SUBCLASS_COMMON,
    .bDeviceProtocol    = MISC_PROTOCOL_IAD,

    .bMaxPacketSize0    = CFG_TUD_ENDPOINT0_SIZE,

    .idVendor           = 0x2E8A, // Pi
    .idProduct          = 0x000c, // CMSIS-DAP adapter
    .bcdDevice          = (PICOPROBE_VERSION_MAJOR << 8) + (16*(PICOPROBE_VERSION_MINOR / 10)) + PICOPROBE_VERSION_MINOR % 10,
    .iManufacturer      = STRID_MANUFACTURER,
    .iProduct           = STRID_PRODUCT,
    .iSerialNumber      = STRID_SERIAL,
    .bNumConfigurations = 1,
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
#if OPT_CMSIS_DAPV2
    ITF_NUM_PROBE_VENDOR,               // Old versions of Keil MDK only look at interface 0
#endif
#if OPT_CMSIS_DAPV1
    ITF_NUM_PROBE_HID,
#endif
#if OPT_TARGET_UART
    ITF_NUM_CDC_UART_COM,
    ITF_NUM_CDC_UART_DATA,
#endif
#if OPT_SIGROK
    ITF_NUM_CDC_SIGROK_COM,
    ITF_NUM_CDC_SIGROK_DATA,
#endif
#if OPT_MSC
    ITF_NUM_MSC,
#endif
#if OPT_NET
    #if CFG_TUD_ECM_RNDIS
        ITF_NUM_CDC_RNDIS_COM,
        ITF_NUM_CDC_RNDIS_DATA,
    #else
        ITF_NUM_CDC_NCM_COM,
        ITF_NUM_CDC_NCM_DATA,
    #endif
#endif
#if OPT_PROBE_DEBUG_OUT
    ITF_NUM_CDC_DEBUG_COM,
    ITF_NUM_CDC_DEBUG_DATA,
#endif
    ITF_NUM_TOTAL
};


// don't know if consecutive numbering is required.  Let's do it anyway
enum
{
    DUMMY_CNT = 0,
#if OPT_CMSIS_DAPV2
    PROBE_VENDOR_OUT_EP_CNT,
    PROBE_VENDOR_IN_EP_CNT,
#endif
#if OPT_CMSIS_DAPV1
    PROBE_HID_OUT_EP_CNT,
    PROBE_HID_IN_EP_CNT,
#endif
#if OPT_TARGET_UART
    CDC_UART_NOTIFICATION_EP_CNT,
    CDC_UART_DATA_OUT_EP_CNT,
    CDC_UART_DATA_IN_EP_CNT,
#endif
#if OPT_SIGROK
    CDC_SIGROK_NOTIFICATION_EP_CNT,
    CDC_SIGROK_DATA_OUT_EP_CNT,
    CDC_SIGROK_DATA_IN_EP_CNT,
#endif
#if OPT_MSC
    MSC_OUT_EP_CNT,
    MSC_IN_EP_CNT,
#endif
#if OPT_NET
    #if CFG_TUD_ECM_RNDIS
        CDC_RNDIS_NOTIFICATION_EP_CNT,
        CDC_RNDIS_DATA_EP_CNT,
    #else
        CDC_NCM_NOTIFICATION_EP_CNT,
        CDC_NCM_DATA_EP_CNT,
    #endif
#endif
#if OPT_PROBE_DEBUG_OUT
    CDC_DEBUG_NOTIFICATION_EP_CNT,
    CDC_DEBUG_DATA_OUT_EP_CNT,
    CDC_DEBUG_DATA_IN_EP_CNT,
#endif
};


#if OPT_CMSIS_DAPV2
    #define PROBE_VENDOR_OUT_EP_NUM         (PROBE_VENDOR_OUT_EP_CNT + 0x00)
    #define PROBE_VENDOR_IN_EP_NUM          (PROBE_VENDOR_IN_EP_CNT + 0x80)
#endif
#if OPT_CMSIS_DAPV1
    #define PROBE_HID_OUT_EP_NUM            (PROBE_HID_OUT_EP_CNT + 0x00)
    #define PROBE_HID_IN_EP_NUM             (PROBE_HID_IN_EP_CNT + 0x80)
#endif
#if OPT_TARGET_UART
    #define CDC_UART_NOTIFICATION_EP_NUM    (CDC_UART_NOTIFICATION_EP_CNT + 0x80)
    #define CDC_UART_DATA_OUT_EP_NUM        (CDC_UART_DATA_OUT_EP_CNT + 0x00)
    #define CDC_UART_DATA_IN_EP_NUM         (CDC_UART_DATA_IN_EP_CNT + 0x80)
#endif
#if OPT_SIGROK
    #define CDC_SIGROK_NOTIFICATION_EP_NUM  (CDC_SIGROK_NOTIFICATION_EP_CNT + 0x80)
    #define CDC_SIGROK_DATA_OUT_EP_NUM      (CDC_SIGROK_DATA_OUT_EP_CNT + 0x00)
    #define CDC_SIGROK_DATA_IN_EP_NUM       (CDC_SIGROK_DATA_IN_EP_CNT + 0x80)
#endif
#if OPT_MSC
    #define MSC_OUT_EP_NUM                  (MSC_OUT_EP_CNT + 0x00)
    #define MSC_IN_EP_NUM                   (MSC_IN_EP_CNT + 0x80)
#endif
#if OPT_NET
    #if CFG_TUD_ECM_RNDIS
        #define CDC_RNDIS_NOTIFICATION_EP_NUM   (CDC_RNDIS_NOTIFICATION_EP_CNT + 0x80)
        #define CDC_RNDIS_DATA_OUT_EP_NUM       (CDC_RNDIS_DATA_EP_CNT + 0x00)
        #define CDC_RNDIS_DATA_IN_EP_NUM        (CDC_RNDIS_DATA_EP_CNT + 0x80)
    #else
        #define CDC_NCM_NOTIFICATION_EP_NUM     (CDC_NCM_NOTIFICATION_EP_CNT + 0x80)
        #define CDC_NCM_DATA_OUT_EP_NUM         (CDC_NCM_DATA_EP_CNT + 0x00)
        #define CDC_NCM_DATA_IN_EP_NUM          (CDC_NCM_DATA_EP_CNT + 0x80)
    #endif
#endif
#if OPT_PROBE_DEBUG_OUT
    #define CDC_DEBUG_NOTIFICATION_EP_NUM   (CDC_DEBUG_NOTIFICATION_EP_CNT + 0x80)
    #define CDC_DEBUG_DATA_OUT_EP_NUM       (CDC_DEBUG_DATA_OUT_EP_CNT + 0x00)
    #define CDC_DEBUG_DATA_IN_EP_NUM        (CDC_DEBUG_DATA_IN_EP_CNT + 0x80)
#endif


#if USE_RNDIS
#define CONFIG_TOTAL_LEN   (TUD_CONFIG_DESC_LEN + CFG_TUD_CDC*TUD_CDC_DESC_LEN + CFG_TUD_VENDOR*TUD_VENDOR_DESC_LEN \
                            + CFG_TUD_HID*TUD_HID_INOUT_DESC_LEN + CFG_TUD_MSC*TUD_MSC_DESC_LEN                     \
                            + CFG_TUD_ECM_RNDIS*TUD_RNDIS_DESC_LEN                                                  \
                            + CFG_TUD_NCM*TUD_CDC_NCM_DESC_LEN)
#else
#define CONFIG_TOTAL_LEN   (TUD_CONFIG_DESC_LEN + CFG_TUD_CDC*TUD_CDC_DESC_LEN + CFG_TUD_VENDOR*TUD_VENDOR_DESC_LEN \
                            + CFG_TUD_HID*TUD_HID_INOUT_DESC_LEN + CFG_TUD_MSC*TUD_MSC_DESC_LEN                     \
                            + CFG_TUD_ECM_RNDIS*TUD_CDC_ECM_DESC_LEN                                                \
                            + CFG_TUD_NCM*TUD_CDC_NCM_DESC_LEN)
#endif

#if OPT_CMSIS_DAPV1
    static uint8_t const desc_hid_report[] =
    {
        TUD_HID_REPORT_DESC_GENERIC_INOUT(CFG_TUD_HID_EP_BUFSIZE)
    };

    uint8_t const * tud_hid_descriptor_report_cb(uint8_t itf)
    {
        (void)itf;
        return desc_hid_report;
    }
#endif



//
// note that there is a 64byte packet limit for full speed!
//
#define CURR_MA 200

static uint8_t const desc_configuration[] =
{
    // Config number, interface count, string index, total length, attribute, power in mA
    //TUD_CONFIG_DESCRIPTOR(1, ITF_NUM_TOTAL, 0, CONFIG_TOTAL_LEN, TUSB_DESC_CONFIG_ATT_REMOTE_WAKEUP, 500),
    TUD_CONFIG_DESCRIPTOR(1, ITF_NUM_TOTAL, 0, CONFIG_TOTAL_LEN, 0, CURR_MA),
#if OPT_CMSIS_DAPV2
    TUD_VENDOR_DESCRIPTOR(ITF_NUM_PROBE_VENDOR, STRID_INTERFACE_DAP2, PROBE_VENDOR_OUT_EP_NUM, PROBE_VENDOR_IN_EP_NUM, 64),
#endif
#if OPT_CMSIS_DAPV1
    TUD_HID_INOUT_DESCRIPTOR(ITF_NUM_PROBE_HID, STRID_INTERFACE_DAP1, HID_ITF_PROTOCOL_NONE, sizeof(desc_hid_report), PROBE_HID_OUT_EP_NUM, PROBE_HID_IN_EP_NUM, CFG_TUD_HID_EP_BUFSIZE, 1),
#endif
#if OPT_TARGET_UART
    TUD_CDC_DESCRIPTOR(ITF_NUM_CDC_UART_COM, STRID_INTERFACE_CDC_UART, CDC_UART_NOTIFICATION_EP_NUM, 64, CDC_UART_DATA_OUT_EP_NUM, CDC_UART_DATA_IN_EP_NUM, 64),
#endif
#if OPT_SIGROK
    TUD_CDC_DESCRIPTOR(ITF_NUM_CDC_SIGROK_COM, STRID_INTERFACE_CDC_SIGROK, CDC_SIGROK_NOTIFICATION_EP_NUM, 64, CDC_SIGROK_DATA_OUT_EP_NUM, CDC_SIGROK_DATA_IN_EP_NUM, 64),
#endif
#if OPT_MSC
    TUD_MSC_DESCRIPTOR(ITF_NUM_MSC, STRID_INTERFACE_MSC, MSC_OUT_EP_NUM, MSC_IN_EP_NUM, 64),
#endif
#if OPT_NET
    #if CFG_TUD_ECM_RNDIS
        #if USE_RNDIS
            TUD_RNDIS_DESCRIPTOR(ITF_NUM_CDC_RNDIS_COM, STRID_INTERFACE_NET, CDC_RNDIS_NOTIFICATION_EP_NUM, 8, CDC_RNDIS_DATA_OUT_EP_NUM, CDC_RNDIS_DATA_IN_EP_NUM, 64),
        #else
            // TODO ECM must be an alternative to RNDIS, RNDIS works on Windows, ECM on iOS, Linux can handle both!?
            TUD_CDC_ECM_DESCRIPTOR(ITF_NUM_CDC_RNDIS_COM, STRID_INTERFACE_NET, STRID_MAC, CDC_RNDIS_NOTIFICATION_EP_NUM, 64, CDC_RNDIS_DATA_OUT_EP_NUM, CDC_RNDIS_DATA_IN_EP_NUM, 64, CFG_TUD_NET_MTU),
        #endif
    #else
        TUD_CDC_NCM_DESCRIPTOR(ITF_NUM_CDC_NCM_COM, STRID_INTERFACE_NET, STRID_MAC, CDC_NCM_NOTIFICATION_EP_NUM, 64, CDC_NCM_DATA_OUT_EP_NUM, CDC_NCM_DATA_IN_EP_NUM, 64, CFG_TUD_NET_MTU),
    #endif
#endif
#if OPT_PROBE_DEBUG_OUT
    TUD_CDC_DESCRIPTOR(ITF_NUM_CDC_DEBUG_COM, STRID_INTERFACE_CDC_DEBUG, CDC_DEBUG_NOTIFICATION_EP_NUM, 64, CDC_DEBUG_DATA_OUT_EP_NUM, CDC_DEBUG_DATA_IN_EP_NUM, 64),
#endif
};


// Invoked when received GET CONFIGURATION DESCRIPTOR
// Application return pointer to descriptor
// Descriptor contents must exist long enough for transfer to complete
uint8_t const * tud_descriptor_configuration_cb(uint8_t index)
{
    (void)index; // for multiple configurations
    return desc_configuration;
}   // tud_descriptor_configuration_cb


//--------------------------------------------------------------------+
// String Descriptors
//--------------------------------------------------------------------+

// array of pointer to string descriptors
static char const* string_desc_arr[] =
{
    [STRID_LANGID]               = (const char[]) { 0x09, 0x04 },       // is supported language is English (0x0409)
    [STRID_MANUFACTURER]         = "RaspberryPi",                       // Manufacturer
    [STRID_PRODUCT]              = "YAPicoprobe CMSIS-DAP",             // Product,                                 **MUST** contain "CMSIS-DAP" to enable "CMSIS-DAP v1"
    [STRID_SERIAL]               = usb_serial,                          // Serial, uses flash unique ID
    [STRID_INTERFACE_DAP2]       = "YAPicoprobe CMSIS-DAP v2",          // Interface descriptor for Bulk transport, **MUST** contain "CMSIS-DAP" to enable "CMSIS-DAP v2"
    [STRID_INTERFACE_DAP1]       = "YAPicoprobe CMSIS-DAP v1",          // Interface descriptor for HID transport
    [STRID_INTERFACE_MSC]        = "YAPicoprobe Flash Drive",           // Interface descriptor for MSC interface
    [STRID_INTERFACE_CDC_UART]   = "YAPicoprobe CDC-UART",              // Interface descriptor for CDC UART (from target)
#if OPT_SIGROK
    [STRID_INTERFACE_CDC_SIGROK] = "YAPicoprobe CDC-SIGROK",            // Interface descriptor for CDC SIGROK
#endif
    [STRID_INTERFACE_CDC_DEBUG]  = "YAPicoprobe CDC-DEBUG",             // Interface descriptor for CDC DEBUG
#if OPT_NET
    #if CFG_TUD_ECM_RNDIS
        #if USE_RNDIS
            [STRID_INTERFACE_NET]  = "YaPicoprobe RNDIS",               // Interface descriptor for SysView RNDIS
        #else
            [STRID_INTERFACE_NET]  = "YaPicoprobe ECM",                 // Interface descriptor for SysView RNDIS
        #endif
    #else
        [STRID_INTERFACE_NET]  = "YaPicoprobe NCM",                     // Interface descriptor for SysView NCM
    #endif
    [STRID_MAC]                  = "",
#endif
};

static uint16_t _desc_str[32];

// Invoked when received GET STRING DESCRIPTOR request
// Application return pointer to descriptor, whose contents must exist long enough for transfer to complete
uint16_t const* tud_descriptor_string_cb(uint8_t index, uint16_t langid)
{
    (void)langid;

    uint8_t chr_count = 0;

    if (index == STRID_LANGID) {
        memcpy( &_desc_str[1], string_desc_arr[0], 2);
        chr_count = 1;
    }
#if OPT_NET
    else if (index == STRID_MAC) {
        // Convert MAC address into UTF-16
        for (unsigned i = 0;  i < sizeof(tud_network_mac_address);  ++i) {
            _desc_str[1+chr_count++] = "0123456789ABCDEF"[(tud_network_mac_address[i] >> 4) & 0xf];
            _desc_str[1+chr_count++] = "0123456789ABCDEF"[(tud_network_mac_address[i] >> 0) & 0xf];
        }
    }
#endif
    else {
        // Convert ASCII string into UTF-16

        if (!(index < sizeof(string_desc_arr) / sizeof(string_desc_arr[0])))
            return NULL;

        const char* str = string_desc_arr[index];

        // Cap at max char
        chr_count = strlen(str);
        if (chr_count > 31)
            chr_count = 31;

        for (uint8_t i = 0; i < chr_count; i++) {
            _desc_str[1 + i] = str[i];
        }
    }

    // first byte is length (including header), second byte is string type
    _desc_str[0] = (TUSB_DESC_STRING << 8 ) | (2*chr_count + 2);

    return _desc_str;
}


#if OPT_CMSIS_DAPV2
/* [incoherent gibbering to make Windows happy] */

//--------------------------------------------------------------------+
// BOS Descriptor
//--------------------------------------------------------------------+

/* Microsoft OS 2.0 registry property descriptor
Per MS requirements https://msdn.microsoft.com/en-us/library/windows/hardware/hh450799(v=vs.85).aspx
device should create DeviceInterfaceGUIDs. It can be done by driver and
in case of real PnP solution device should expose MS "Microsoft OS 2.0
registry property descriptor". Such descriptor can insert any record
into Windows registry per device/configuration/interface. In our case it
will insert "DeviceInterfaceGUIDs" multistring property.


https://developers.google.com/web/fundamentals/native-hardware/build-for-webusb/
(Section Microsoft OS compatibility descriptors)
*/
#define MS_OS_20_DESC_LEN  0xB2

#define BOS_TOTAL_LEN      (TUD_BOS_DESC_LEN + TUD_BOS_MICROSOFT_OS_DESC_LEN)

uint8_t const desc_bos[] = {
    // total length, number of device caps
    TUD_BOS_DESCRIPTOR(BOS_TOTAL_LEN, 1),

    // Microsoft OS 2.0 descriptor
    TUD_BOS_MS_OS_20_DESCRIPTOR(MS_OS_20_DESC_LEN, 1)
};

uint8_t const desc_ms_os_20[] = {
    // Set header: length, type, windows version, total length
    U16_TO_U8S_LE(0x000A), U16_TO_U8S_LE(MS_OS_20_SET_HEADER_DESCRIPTOR), U32_TO_U8S_LE(0x06030000), U16_TO_U8S_LE(MS_OS_20_DESC_LEN),

    // Configuration subset header: length, type, configuration index, reserved, configuration total length
    U16_TO_U8S_LE(0x0008), U16_TO_U8S_LE(MS_OS_20_SUBSET_HEADER_CONFIGURATION), 0, 0, U16_TO_U8S_LE(MS_OS_20_DESC_LEN - 0x0A),

    // Function Subset header: length, type, first interface, reserved, subset length
    U16_TO_U8S_LE(0x0008), U16_TO_U8S_LE(MS_OS_20_SUBSET_HEADER_FUNCTION), ITF_NUM_PROBE_VENDOR, 0, U16_TO_U8S_LE(MS_OS_20_DESC_LEN - 0x0A - 0x08),

    // MS OS 2.0 Compatible ID descriptor: length, type, compatible ID, sub compatible ID
    U16_TO_U8S_LE(0x0014), U16_TO_U8S_LE(MS_OS_20_FEATURE_COMPATBLE_ID), 'W', 'I', 'N', 'U', 'S', 'B', 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // sub-compatible

    // MS OS 2.0 Registry property descriptor: length, type
    U16_TO_U8S_LE(MS_OS_20_DESC_LEN - 0x0A - 0x08 - 0x08 - 0x14), U16_TO_U8S_LE(MS_OS_20_FEATURE_REG_PROPERTY),
    U16_TO_U8S_LE(0x0007), U16_TO_U8S_LE(0x002A), // wPropertyDataType, wPropertyNameLength and PropertyName "DeviceInterfaceGUIDs\0" in UTF-16
    'D', 0x00, 'e', 0x00, 'v', 0x00, 'i', 0x00, 'c', 0x00, 'e', 0x00, 'I', 0x00, 'n', 0x00, 't', 0x00, 'e', 0x00,
    'r', 0x00, 'f', 0x00, 'a', 0x00, 'c', 0x00, 'e', 0x00, 'G', 0x00, 'U', 0x00, 'I', 0x00, 'D', 0x00, 's', 0x00, 0x00, 0x00,
    U16_TO_U8S_LE(0x0050), // wPropertyDataLength
                           // bPropertyData "{CDB3B5AD-293B-4663-AA36-1AAE46463776}" as a UTF-16 string (b doesn't mean bytes)
    '{', 0x00, 'C', 0x00, 'D', 0x00, 'B', 0x00, '3', 0x00, 'B', 0x00, '5', 0x00, 'A', 0x00, 'D', 0x00, '-', 0x00,
    '2', 0x00, '9', 0x00, '3', 0x00, 'B', 0x00, '-', 0x00, '4', 0x00, '6', 0x00, '6', 0x00, '3', 0x00, '-', 0x00,
    'A', 0x00, 'A', 0x00, '3', 0x00, '6', 0x00, '-', 0x00, '1', 0x00, 'A', 0x00, 'A', 0x00, 'E', 0x00, '4', 0x00,
    '6', 0x00, '4', 0x00, '6', 0x00, '3', 0x00, '7', 0x00, '7', 0x00, '6', 0x00, '}', 0x00, 0x00, 0x00, 0x00, 0x00
};

TU_VERIFY_STATIC(sizeof(desc_ms_os_20) == MS_OS_20_DESC_LEN, "Incorrect size");

uint8_t const * tud_descriptor_bos_cb(void)
{
    return desc_bos;
}
#endif
