/*
 * The MIT License (MIT)
 *
 * Copyright (c) 2020 Jacob Berg Potter
 * Copyright (c) 2020 Peter Lawrence
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
 * This file is part of the TinyUSB stack.
 */

#include "tusb_option.h"

#if ECLIPSE_GUI || ( CFG_TUD_ENABLED && CFG_TUD_NCM )

#if ECLIPSE_GUI
    #define tu_static static
#endif

#include "device/usbd.h"
#include "device/usbd_pvt.h"
#include "net_device.h"
#include <stdio.h>

#include "task.h"

//--------------------------------------------------------------------+
// MACRO CONSTANT TYPEDEF
//--------------------------------------------------------------------+

#define NTH16_SIGNATURE      0x484D434E
#define NDP16_SIGNATURE_NCM0 0x304D434E
#define NDP16_SIGNATURE_NCM1 0x314D434E

typedef struct TU_ATTR_PACKED {
    uint16_t wLength;
    uint16_t bmNtbFormatsSupported;
    uint32_t dwNtbInMaxSize;
    uint16_t wNdbInDivisor;
    uint16_t wNdbInPayloadRemainder;
    uint16_t wNdbInAlignment;
    uint16_t wReserved;
    uint32_t dwNtbOutMaxSize;
    uint16_t wNdbOutDivisor;
    uint16_t wNdbOutPayloadRemainder;
    uint16_t wNdbOutAlignment;
    uint16_t wNtbOutMaxDatagrams;
} ntb_parameters_t;

typedef struct TU_ATTR_PACKED {
    uint32_t dwSignature;
    uint16_t wHeaderLength;
    uint16_t wSequence;
    uint16_t wBlockLength;
    uint16_t wNdpIndex;
} nth16_t;

typedef struct TU_ATTR_PACKED {
    uint16_t wDatagramIndex;
    uint16_t wDatagramLength;
} ndp16_datagram_t;

typedef struct TU_ATTR_PACKED {
    uint32_t dwSignature;
    uint16_t wLength;
    uint16_t wNextNdpIndex;
    ndp16_datagram_t datagram[];
} ndp16_t;

typedef union TU_ATTR_PACKED {
    struct {
        nth16_t nth;
        ndp16_t ndp;
    };
    uint8_t data[CFG_TUD_NCM_IN_NTB_MAX_SIZE];
} transmit_ntb_t;

struct ecm_notify_struct {
    tusb_control_request_t header;
    uint32_t downlink, uplink;
};

typedef struct {
    uint8_t itf_num;      // Index number of Management Interface, +1 for Data Interface
    uint8_t itf_data_alt; // Alternate setting of Data Interface. 0 : inactive, 1 : active

    uint8_t ep_notif;
    uint8_t ep_in;
    uint8_t ep_out;

    const ndp16_t *rcv_ndp;
    uint8_t rcv_datagram_num;
    uint8_t rcv_datagram_index;
    uint16_t rcv_datagram_size;

    enum {
        REPORT_SPEED, REPORT_CONNECTED, REPORT_DONE
    } report_state;
    bool report_pending;

    uint8_t current_ntb;           // Index in transmit_ntb[] that is currently being filled with datagrams
    uint8_t datagram_count;        // Number of datagrams in transmit_ntb[current_ntb]
    uint16_t next_datagram_offset;  // Offset in transmit_ntb[current_ntb].data to place the next datagram
    uint16_t ntb_in_size;        // Maximum size of transmitted (IN to host) NTBs; initially CFG_TUD_NCM_IN_NTB_MAX_SIZE
    uint8_t max_datagrams_per_ntb; // Maximum number of datagrams per NTB; initially CFG_TUD_NCM_MAX_DATAGRAMS_PER_NTB

    uint16_t nth_sequence;          // Sequence number counter for transmitted NTBs

    bool transferring;

} ncm_interface_t;

//--------------------------------------------------------------------+
// INTERNAL OBJECT & FUNCTION DECLARATION
//--------------------------------------------------------------------+

CFG_TUSB_MEM_SECTION CFG_TUSB_MEM_ALIGN tu_static const ntb_parameters_t ntb_parameters = {
        .wLength = sizeof(ntb_parameters_t),
        .bmNtbFormatsSupported = 0x01,                                 // 16-bit NTB supported
        .dwNtbInMaxSize = CFG_TUD_NCM_IN_NTB_MAX_SIZE+400,
        .wNdbInDivisor = 4,
        .wNdbInPayloadRemainder = 0,
        .wNdbInAlignment = CFG_TUD_NCM_ALIGNMENT,
        .wReserved = 0,
        .dwNtbOutMaxSize = CFG_TUD_NCM_OUT_NTB_MAX_SIZE,
        .wNdbOutDivisor = 4,
        .wNdbOutPayloadRemainder = 0,
        .wNdbOutAlignment = CFG_TUD_NCM_ALIGNMENT,
        .wNtbOutMaxDatagrams = 1                                     // 0=no limit
};

CFG_TUSB_MEM_SECTION CFG_TUSB_MEM_ALIGN tu_static transmit_ntb_t transmit_ntb[2];

CFG_TUSB_MEM_SECTION CFG_TUSB_MEM_ALIGN tu_static uint8_t receive_ntb[CFG_TUD_NCM_OUT_NTB_MAX_SIZE+400];

tu_static ncm_interface_t ncm_interface;

static void ncm_prepare_for_tx(void)
/**
 * Set up the NTB state in ncm_interface to be ready to add datagrams.
 *
 * \pre
 *    \a ncm_interface.current_ntb must be set correctly
 */
{
    //printf("ncm_prepare_for_tx()\n");
    ncm_interface.datagram_count = 0;
    // datagrams start after all the headers
    ncm_interface.next_datagram_offset =   sizeof(nth16_t) + sizeof(ndp16_t)
                                         + ((CFG_TUD_NCM_MAX_DATAGRAMS_PER_NTB + 1) * sizeof(ndp16_datagram_t));
    memset(transmit_ntb + ncm_interface.current_ntb, 0, sizeof(transmit_ntb_t));
}

/*
 * If not already transmitting, start sending the current NTB to the host and swap buffers
 * to start filling the other one with datagrams.
 */
static void ncm_start_tx(void)
{
    //printf("ncm_start_tx() - %d %d\n", ncm_interface.transferring, ncm_interface.datagram_count);

    if (ncm_interface.transferring) {
        return;
    }

    transmit_ntb_t *ntb = &transmit_ntb[ncm_interface.current_ntb];
    size_t ntb_length = ncm_interface.next_datagram_offset;

    // Fill in NTB header
    ntb->nth.dwSignature = NTH16_SIGNATURE;
    ntb->nth.wHeaderLength = sizeof(nth16_t);
    ntb->nth.wSequence = ncm_interface.nth_sequence++;
    ntb->nth.wBlockLength = ntb_length;
    ntb->nth.wNdpIndex = sizeof(nth16_t);

    // Fill in NDP16 header and terminator
    ntb->ndp.dwSignature = NDP16_SIGNATURE_NCM0;
    ntb->ndp.wLength = sizeof(ndp16_t) + (ncm_interface.datagram_count + 1) * sizeof(ndp16_datagram_t);
    ntb->ndp.wNextNdpIndex = 0;
    ntb->ndp.datagram[ncm_interface.datagram_count].wDatagramIndex = 0;
    ntb->ndp.datagram[ncm_interface.datagram_count].wDatagramLength = 0;

    // Kick off an endpoint transfer
    usbd_edpt_xfer(0, ncm_interface.ep_in, ntb->data, ntb_length);
    ncm_interface.transferring = true;

    // Swap to the other NTB and clear it out
    ncm_interface.current_ntb = 1 - ncm_interface.current_ntb;
    ncm_prepare_for_tx();
}



tu_static struct ecm_notify_struct ncm_notify_connected = {
        .header = {
                .bmRequestType_bit = {
                        .recipient = TUSB_REQ_RCPT_INTERFACE,
                        .type = TUSB_REQ_TYPE_CLASS,
                        .direction = TUSB_DIR_IN
                },
                .bRequest = CDC_NOTIF_NETWORK_CONNECTION,
                .wValue = 1 /* Connected */,
                .wLength = 0,
        },
};

tu_static struct ecm_notify_struct ncm_notify_speed_change = {
        .header = {
                .bmRequestType_bit = {
                        .recipient = TUSB_REQ_RCPT_INTERFACE,
                        .type = TUSB_REQ_TYPE_CLASS,
                        .direction = TUSB_DIR_IN
                },
                .bRequest = CDC_NOTIF_CONNECTION_SPEED_CHANGE,
                .wLength = 8,
        },
        .downlink = 1000000,
        .uplink = 1000000,
};



static uint8_t usb_buffi[CFG_TUD_NCM_OUT_NTB_MAX_SIZE+400];

void tud_network_recv_renew(void)
/**
 * context: lwIP & TinyUSB
 */
{
    printf("tud_network_recv_renew() - %d [%p]\n", ncm_interface.rcv_datagram_num, xTaskGetCurrentTaskHandle());
    if (ncm_interface.rcv_datagram_index >= ncm_interface.rcv_datagram_num) {   //   &&  ncm_interface.rcv_datagram_size != 0) {
        bool r;

        printf("--0\n");
#if 0
        // funny effect with "iperf -c 192.168.14.1 -e -i 1 -l 22 -M 200"
        // receive_ntb is overwritten!
        //        [10:37:42.330968 0.000287] 38.039 (  0) -   0 90 54
        //        [10:37:42.331361 0.000394] 38.039 (  0) -   1 146 114
        //        [10:37:42.331770 0.000409] 38.039 (  0) -   2 262 254
        //        [10:37:42.332156 0.000386] 38.039 (  0) -   3 518 254
        //        [10:37:42.332566 0.000410] 38.039 (  0) -   4 774 254
        //        [10:37:42.332979 0.000412] 38.039 (  0) -   5 1030 254
        //        [10:37:42.333436 0.000457] 38.040 (  1) -   6 1286 254
        //        [10:37:42.333871 0.000435] 38.040 (  0) -   7 1542 254
        //        [10:37:42.334303 0.000432] 38.040 (  0) -   8 1798 254
        //        [10:37:42.334732 0.000429] 38.040 (  0) -   9 2054 254
        //        [10:37:42.335162 0.000431] 38.040 (  0) -   10 2310 254
        //        [10:37:42.335614 0.000452] 38.040 (  0) -   11 0 0
        //        [10:37:42.335992 0.000378] 38.040 (  0) - tud_network_recv_renew: 11 0x304d434e 56 0
        //        [10:37:42.336912 0.000919] 38.040 (  0) - tud_network_recv_renew->: 0 200103D8 90 54          OK
        //        [10:37:42.337755 0.000844] 38.041 (  1) - tud_network_recv_renew() - 11 [20020120]
        //        [10:37:42.338528 0.000773] 38.041 (  0) - tud_network_recv_renew->: 1 200103D8 146 114        OK
        //        [10:37:42.339365 0.000837] 38.041 (  0) - tud_network_recv_renew() - 11 [20020120]
        //        [10:37:42.340137 0.000771] 38.041 (  0) - tud_network_recv_renew->: 2 200103D8 262 254        OK
        //        [10:37:42.340965 0.000828] 38.042 (  1) - tud_network_recv_renew() - 11 [20020120]
        //        [10:37:42.341748 0.000783] 38.042 (  0) - tud_network_recv_renew->: 3 200103D8 518 254        OK
        //        [10:37:42.342572 0.000824] 38.042 (  0) - tud_network_recv_renew() - 11 [20020120]
        //        [10:37:42.343345 0.000773] 38.042 (  0) - tud_network_recv_renew->: 4 200103D8 774 254        OK
        //        [10:37:42.344172 0.000827] 38.043 (  1) - tud_network_recv_renew() - 11 [20020120]
        //        [10:37:42.344932 0.000760] 38.043 (  0) - tud_network_recv_renew->: 5 200103D8 1030 254       OK
        //        [10:37:42.345778 0.000847] 38.043 (  0) - tud_network_recv_renew() - 11 [20020120]
        //        [10:37:42.346567 0.000788] 38.043 (  0) - tud_network_recv_renew->: 6 200103D8 1626 254       FAIL
        //        [10:37:42.347423 0.000856] 38.044 (  1) - tud_network_recv_renew() - 11 [20020120]
        //        [10:37:42.348195 0.000772] 38.044 (  0) - tud_network_recv_renew->: 7 200103D8 1882 254       FAIL
        //        [10:37:42.349037 0.000842] 38.044 (  0) - tud_network_recv_renew() - 11 [20020120]
        //        [10:37:42.349828 0.000792] 38.044 (  0) - tud_network_recv_renew->: 8 200103D8 0 0            FAIL
        //        [10:37:42.350546 0.000717] 38.046 (  2) - handle_incoming_datagram(2136)
        //        [10:37:42.351156 0.000611] 38.046 (  0) - tud_network_recv_renew() - 11 [20020120]
        //        [10:37:42.351867 0.000711] 38.046 (  0) - tud_network_recv_renew->: 9 200103D8 0 0            FAIL & DEAD
        r = usbd_edpt_xfer(0, ncm_interface.ep_out, receive_ntb, sizeof(receive_ntb));
        if ( !r) {
            printf("--0.0\n");
            return;
        }
#elif 1
        //memset(usb_buffi, 0, sizeof(usb_buffi));  // this will not work!
        r = usbd_edpt_xfer(0, ncm_interface.ep_out, usb_buffi, sizeof(usb_buffi));
        if ( !r) {
            printf("--0.0\n");
            return;
        }
        memcpy(receive_ntb, usb_buffi, sizeof(receive_ntb));
#else
#if 1
        {
            static bool inited;

            if ( !inited) {
                inited = true;
                r = usbd_edpt_xfer(0, ncm_interface.ep_out, usb_buffi, sizeof(usb_buffi));
                if ( !r) {
                    printf("--0.0\n");
                    return;
                }
            }
        }
#endif
        if (ncm_interface.rcv_datagram_size == 0) {
            printf("--0.00\n");
            return;
        }
        memcpy(receive_ntb, usb_buffi, ncm_interface.rcv_datagram_size);
        ncm_interface.rcv_datagram_size = 0;
#endif

        printf("--1\n");

        const nth16_t *hdr = (const nth16_t*) receive_ntb;
        if (hdr->dwSignature != NTH16_SIGNATURE) {
            printf("--1.1 0x%lx\n", hdr->dwSignature);
            return;
        }

        const ndp16_t *ndp = (const ndp16_t*) (receive_ntb + hdr->wNdpIndex);
        if (ndp->dwSignature != NDP16_SIGNATURE_NCM0  &&  ndp->dwSignature != NDP16_SIGNATURE_NCM1) {
            printf("--1.2 0x%lx\n", ndp->dwSignature);
            return;
        }

        printf("--2\n");

        int max_rcv_datagrams = (ndp->wLength - 12) / 4;
        ncm_interface.rcv_datagram_index = 0;
        ncm_interface.rcv_datagram_num = 0;
        ncm_interface.rcv_ndp = ndp;
#if 0
        for (int i = 0;  i < max_rcv_datagrams  &&  ndp->datagram[i].wDatagramIndex != 0  &&  ndp->datagram[i].wDatagramLength != 0; i++)
        {
            ncm_interface.rcv_datagram_num++;
        }
#else
        int i = 0;
        while (i <= max_rcv_datagrams)
        {
            printf("  %d %d %d\n", ncm_interface.rcv_datagram_num, ndp->datagram[i].wDatagramIndex, ndp->datagram[i].wDatagramLength);
            if (ndp->datagram[i].wDatagramIndex == 0  &&  ndp->datagram[i].wDatagramLength == 0) {
                break;
            }
            ++i;
            ncm_interface.rcv_datagram_num++;
        }
#endif

#if 1
        printf("tud_network_recv_renew: %d 0x%08lx %d %d\n", ncm_interface.rcv_datagram_num, ndp->dwSignature, ndp->wLength,
                ndp->wNextNdpIndex);
#endif

    }

    if (ncm_interface.rcv_datagram_num == 0) {
        return;
    }

    const ndp16_t *ndp = ncm_interface.rcv_ndp;
    const int i = ncm_interface.rcv_datagram_index;
    ncm_interface.rcv_datagram_index++;

    printf("tud_network_recv_renew->: %d %p %d %d\n", i, ndp, ndp->datagram[i].wDatagramIndex, ndp->datagram[i].wDatagramLength);

    if ( !tud_network_recv_cb(receive_ntb + ndp->datagram[i].wDatagramIndex, ndp->datagram[i].wDatagramLength)) {
        printf("!!!!!!!!!!!!!!!!!!!!\n");
        ncm_interface.rcv_datagram_index--;
    }
}



static void handle_incoming_datagram(uint32_t len)
{
    printf("!!!!!!!!!!!!!handle_incoming_datagram(%lu) %d\n", len, ncm_interface.rcv_datagram_size);

#if 0
    if (len != 0) {
        usbd_edpt_xfer(0, ncm_interface.ep_out, usb_buffi, sizeof(usb_buffi));
        ncm_interface.rcv_datagram_size = len;
    }
#else
    ncm_interface.rcv_datagram_size = len;
#endif

    tud_network_recv_renew();
}



//--------------------------------------------------------------------+
// USBD Driver API
//--------------------------------------------------------------------+

void netd_init(void)
/**
 * called on start
 *
 * context: TinyUSB
 */
{
    printf("netd_init() [%p]\n", xTaskGetCurrentTaskHandle());

    tu_memclr(&ncm_interface, sizeof(ncm_interface));
    ncm_interface.ntb_in_size = CFG_TUD_NCM_IN_NTB_MAX_SIZE;
    ncm_interface.max_datagrams_per_ntb = CFG_TUD_NCM_MAX_DATAGRAMS_PER_NTB;
    ncm_prepare_for_tx();
}



void netd_reset(uint8_t rhport)
/**
 * called with rhport=0
 *
 * context: TinyUSB
 */
{
    (void) rhport;

    printf("netd_reset(%d) [%p]\n", rhport, xTaskGetCurrentTaskHandle());

    netd_init();
}



uint16_t netd_open(uint8_t rhport, tusb_desc_interface_t const *itf_desc, uint16_t max_len)
/**
 * called with max_len=143
 *
 * context: TinyUSB
 */
{
    // confirm interface hasn't already been allocated
    TU_ASSERT(0 == ncm_interface.ep_notif, 0);

    printf("netd_open(%d,%p,%d) [%p]\n", rhport, itf_desc, max_len, xTaskGetCurrentTaskHandle());

    //------------- Management Interface -------------//
    ncm_interface.itf_num = itf_desc->bInterfaceNumber;

    uint16_t drv_len = sizeof(tusb_desc_interface_t);
    uint8_t const *p_desc = tu_desc_next(itf_desc);

    // Communication Functional Descriptors
    while (TUSB_DESC_CS_INTERFACE == tu_desc_type(p_desc) && drv_len <= max_len) {
        drv_len += tu_desc_len(p_desc);
        p_desc = tu_desc_next(p_desc);
    }

    // notification endpoint (if any)
    if (TUSB_DESC_ENDPOINT == tu_desc_type(p_desc)) {
        TU_ASSERT(usbd_edpt_open(rhport, (tusb_desc_endpoint_t const* ) p_desc), 0);

        ncm_interface.ep_notif = ((tusb_desc_endpoint_t const*) p_desc)->bEndpointAddress;

        drv_len += tu_desc_len(p_desc);
        p_desc = tu_desc_next(p_desc);
    }

    //------------- Data Interface -------------//
    // - CDC-NCM data interface has 2 alternate settings
    //   - 0 : zero endpoints for inactive (default)
    //   - 1 : IN & OUT endpoints for transfer of NTBs
    TU_ASSERT(TUSB_DESC_INTERFACE == tu_desc_type(p_desc), 0);

    do {
        tusb_desc_interface_t const *data_itf_desc = (tusb_desc_interface_t const*) p_desc;
        TU_ASSERT(TUSB_CLASS_CDC_DATA == data_itf_desc->bInterfaceClass, 0);

        drv_len += tu_desc_len(p_desc);
        p_desc = tu_desc_next(p_desc);
    } while ((TUSB_DESC_INTERFACE == tu_desc_type(p_desc)) && (drv_len <= max_len));

    // Pair of endpoints
    TU_ASSERT(TUSB_DESC_ENDPOINT == tu_desc_type(p_desc), 0);

    TU_ASSERT(usbd_open_edpt_pair(rhport, p_desc, 2, TUSB_XFER_BULK, &ncm_interface.ep_out, &ncm_interface.ep_in));

    //usbd_edpt_xfer(0, ncm_interface.ep_out, usb_buffi, sizeof(usb_buffi));

    drv_len += 2 * sizeof(tusb_desc_endpoint_t);

    return drv_len;
}



static void ncm_report(void)
/**
 * called on init
 */
{
    //printf("ncm_report - %d\n", ncm_interface.report_state);
    uint8_t const rhport = 0;
    if (ncm_interface.report_state == REPORT_SPEED) {
        ncm_notify_speed_change.header.wIndex = ncm_interface.itf_num;
        usbd_edpt_xfer(rhport, ncm_interface.ep_notif, (uint8_t*) &ncm_notify_speed_change,
                sizeof(ncm_notify_speed_change));
        ncm_interface.report_state = REPORT_CONNECTED;
        ncm_interface.report_pending = true;
    }
    else if (ncm_interface.report_state == REPORT_CONNECTED) {
        ncm_notify_connected.header.wIndex = ncm_interface.itf_num;
        usbd_edpt_xfer(rhport, ncm_interface.ep_notif, (uint8_t*) &ncm_notify_connected, sizeof(ncm_notify_connected));
        ncm_interface.report_state = REPORT_DONE;
        ncm_interface.report_pending = true;
    }
}



TU_ATTR_WEAK void tud_network_link_state_cb(bool state)
/**
 * called on init three times with 1/0/1
 *
 * context: TinyUSB
 */
{
    (void) state;
    printf("tud_network_link_state_cb(%d) [%p]\n", state, xTaskGetCurrentTaskHandle());
}



// Handle class control request
// return false to stall control endpoint (e.g unsupported request)
bool netd_control_xfer_cb(uint8_t rhport, uint8_t stage, tusb_control_request_t const *request)
/**
 * Called on init of connection
 *
 * context: TinyUSB
 */
{
    printf("netd_control_xfer_cb(%d, %d, %p) [%p]\n", rhport, stage, request, xTaskGetCurrentTaskHandle());

    if (stage != CONTROL_STAGE_SETUP)
        return true ;

    switch (request->bmRequestType_bit.type) {
    case TUSB_REQ_TYPE_STANDARD:
        switch (request->bRequest) {
        case TUSB_REQ_GET_INTERFACE: {
            uint8_t const req_itfnum = (uint8_t) request->wIndex;
            TU_VERIFY(ncm_interface.itf_num + 1 == req_itfnum);

            tud_control_xfer(rhport, request, &ncm_interface.itf_data_alt, 1);
        }
            break;

        case TUSB_REQ_SET_INTERFACE: {
            uint8_t const req_itfnum = (uint8_t) request->wIndex;
            uint8_t const req_alt = (uint8_t) request->wValue;

            // Only valid for Data Interface with Alternate is either 0 or 1
            TU_VERIFY(ncm_interface.itf_num + 1 == req_itfnum && req_alt < 2);

            if (req_alt != ncm_interface.itf_data_alt) {
                ncm_interface.itf_data_alt = req_alt;

                if (ncm_interface.itf_data_alt) {
                    if (!usbd_edpt_busy(rhport, ncm_interface.ep_out)) {
                        tud_network_recv_renew(); // prepare for incoming datagrams
                    }
                    if (!ncm_interface.report_pending) {
                        ncm_report();
                    }
                }

                tud_network_link_state_cb(ncm_interface.itf_data_alt);
            }

            tud_control_status(rhport, request);
        }
            break;

            // unsupported request
        default:
            return false ;
        }
        break;

    case TUSB_REQ_TYPE_CLASS:
        TU_VERIFY(ncm_interface.itf_num == request->wIndex);

        //printf("netd_control_xfer_cb/TUSB_REQ_TYPE_CLASS: %d\n", request->bRequest);

        if (NCM_GET_NTB_PARAMETERS == request->bRequest) {
            tud_control_xfer(rhport, request, (void*) (uintptr_t) &ntb_parameters, sizeof(ntb_parameters));
        }

        break;

        // unsupported request
    default:
        return false ;
    }

    return true ;
}



bool netd_xfer_cb(uint8_t rhport, uint8_t ep_addr, xfer_result_t result, uint32_t xferred_bytes)
/**
 * context: TinyUSB
 */
{
    (void) rhport;
    (void) result;

    printf("netd_xfer_cb(%d,%d,%d,%lu) [%p]\n", rhport, ep_addr, result, xferred_bytes, xTaskGetCurrentTaskHandle());

    /* new datagram receive_ntb */
    if (ep_addr == ncm_interface.ep_out) {
        printf("  EP_OUT\n");
        handle_incoming_datagram(xferred_bytes);
    }

    /* data transmission finished */
    if (ep_addr == ncm_interface.ep_in) {
        //printf("  EP_IN %d %d %d\n", ncm_interface.transferring, ncm_interface.datagram_count,
                //ncm_interface.itf_data_alt);
        if (ncm_interface.transferring) {
            ncm_interface.transferring = false;
            //tud_network_recv_renew();
        }

        // If there are datagrams queued up that we tried to send while this NTB was being emitted, send them now
        if (ncm_interface.datagram_count && ncm_interface.itf_data_alt == 1) {
            ncm_start_tx();
        }
    }

    if (ep_addr == ncm_interface.ep_notif) {
        //printf("  EP_NOTIF\n");
        ncm_interface.report_pending = false;
        ncm_report();
    }

    return true ;
}



bool tud_network_can_xmit(uint16_t size)
/**
 * poll network driver for its ability to accept another packet to transmit
 *
 * context: lwIP
 *
 */
{
    TU_VERIFY(ncm_interface.itf_data_alt == 1);

    size_t next_datagram_offset = ncm_interface.next_datagram_offset;

#if 0
    printf("tud_network_can_xmit(%d) %d %d - %d %d [%p]\n", size, ncm_interface.datagram_count, ncm_interface.max_datagrams_per_ntb,
            next_datagram_offset, ncm_interface.ntb_in_size, xTaskGetCurrentTaskHandle());
#endif

    if (ncm_interface.datagram_count >= ncm_interface.max_datagrams_per_ntb) {
        // this happens if max... is set to 1
        printf("NTB full [by count]\r\n");
        return false ;
    }

    if (next_datagram_offset + size > ncm_interface.ntb_in_size) {
        // this happens
        printf("ntb full [by size]\r\n");
        return false ;
    }

    return true ;
}



void tud_network_xmit(void *ref, uint16_t arg)
/**
 * context: lwIP.
 */
{
    transmit_ntb_t *ntb = &transmit_ntb[ncm_interface.current_ntb];
    size_t next_datagram_offset = ncm_interface.next_datagram_offset;

    //printf("tud_network_xmit(%p,%d) [%p]\n", ref, arg, xTaskGetCurrentTaskHandle());

    uint16_t size = tud_network_xmit_cb(ntb->data + next_datagram_offset, ref, arg);

    ntb->ndp.datagram[ncm_interface.datagram_count].wDatagramIndex = ncm_interface.next_datagram_offset;
    ntb->ndp.datagram[ncm_interface.datagram_count].wDatagramLength = size;

    ncm_interface.datagram_count++;
    next_datagram_offset += size;

    // round up so the next datagram is aligned correctly
    next_datagram_offset += (CFG_TUD_NCM_ALIGNMENT - 1);
    next_datagram_offset -= (next_datagram_offset % CFG_TUD_NCM_ALIGNMENT);

    ncm_interface.next_datagram_offset = next_datagram_offset;

    ncm_start_tx();
}

#endif
