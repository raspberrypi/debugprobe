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

#include <stdio.h>
#include "device/usbd.h"
#include "device/usbd_pvt.h"
#include "net_device.h"
#include "ncm.h"

#undef CFG_TUD_NCM_MAX_DATAGRAMS_PER_NTB
#define CFG_TUD_NCM_MAX_DATAGRAMS_PER_NTB 1


//--------------------------------------------------------------------+
// MACRO CONSTANT TYPEDEF
//--------------------------------------------------------------------+

typedef struct {
    uint8_t itf_num;      // Index number of Management Interface, +1 for Data Interface
    uint8_t itf_data_alt; // Alternate setting of Data Interface. 0 : inactive, 1 : active

    uint8_t ep_notif;
    uint8_t ep_in;
    uint8_t ep_out;

    const ndp16_t *rcv_ndp;
    uint8_t rcv_datagram_num;
    uint8_t rcv_datagram_index;
    CFG_TUSB_MEM_ALIGN uint8_t rcv_datagram[CFG_TUD_NCM_OUT_NTB_MAX_SIZE+400];
    uint16_t rcv_usb_datagram_size;

    enum {
        REPORT_SPEED, REPORT_CONNECTED, REPORT_DONE
    } report_state;
    bool report_pending;

    uint16_t next_datagram_offset;  // Offset in transmit_ntb[current_ntb].data to place the next datagram
    uint16_t ntb_in_size;        // Maximum size of transmitted (IN to host) NTBs; initially CFG_TUD_NCM_IN_NTB_MAX_SIZE
    uint8_t max_datagrams_per_ntb; // Maximum number of datagrams per NTB; initially CFG_TUD_NCM_MAX_DATAGRAMS_PER_NTB

    uint16_t nth_sequence;          // Sequence number counter for transmitted NTBs

    bool can_xmit;
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
        .wNtbOutMaxDatagrams = 1                                     // 0=no limit TODO set to 0
};

CFG_TUSB_MEM_SECTION CFG_TUSB_MEM_ALIGN tu_static transmit_ntb_t transmit_ntb;

tu_static ncm_interface_t ncm_interface;



static void ncm_prepare_for_tx(void)
/**
 * Set up the NTB state in ncm_interface to be ready to add datagrams.
 */
{
    //printf("ncm_prepare_for_tx()\n");
    // datagrams start after all the headers
    ncm_interface.next_datagram_offset =   sizeof(nth16_t) + sizeof(ndp16_t)
                                         + ((CFG_TUD_NCM_MAX_DATAGRAMS_PER_NTB + 1) * sizeof(ndp16_datagram_t));
    memset(&transmit_ntb, 0, sizeof(transmit_ntb_t));
    ncm_interface.can_xmit = true;
}



tu_static struct ncm_notify_struct ncm_notify_connected = {
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

tu_static struct ncm_notify_struct ncm_notify_speed_change = {
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



void tud_network_recv_renew(void)
/**
 * context: lwIP & TinyUSB
 */
{
    //printf("tud_network_recv_renew() - %d [%p]\n", ncm_interface.rcv_datagram_num, xTaskGetCurrentTaskHandle());
    if (ncm_interface.rcv_datagram_index >= ncm_interface.rcv_datagram_num) {

        //printf("--0\n");

        if (ncm_interface.rcv_usb_datagram_size == 0) {
            if (usbd_edpt_busy(0, ncm_interface.ep_out)) {
                printf("--0.1\n");
                return;
            }
            else {
                bool r = usbd_edpt_xfer(0, ncm_interface.ep_out, ncm_interface.rcv_datagram, sizeof(ncm_interface.rcv_datagram));
                if ( !r) {
                    printf("--0.2\n");
                    return;
                }
            }
        }

        //printf("--1\n");

        const nth16_t *hdr = (const nth16_t*)ncm_interface.rcv_datagram;
        if (ncm_interface.rcv_usb_datagram_size < sizeof(nth16_t) + sizeof(ndp16_t) + 2*sizeof(ndp16_datagram_t)) {
            //printf("--1.1.1 %d\n", ncm_interface.rcv_usb_datagram_size);
            return;
        }
        if (hdr->dwSignature != NTH16_SIGNATURE) {
            printf("--1.1.2 0x%lx %d\n", hdr->dwSignature, ncm_interface.rcv_usb_datagram_size);
            return;
        }
        if (hdr->wNdpIndex < sizeof(nth16_t)) {
            printf("--1.1.3 %d\n", hdr->wNdpIndex);
            return;
        }
        if (hdr->wNdpIndex + sizeof(ndp16_t) > ncm_interface.rcv_usb_datagram_size) {
            printf("--1.1.4 %d %d\n", hdr->wNdpIndex + sizeof(ndp16_t), ncm_interface.rcv_usb_datagram_size);
            return;
        }

        const ndp16_t *ndp = (const ndp16_t*) (ncm_interface.rcv_datagram + hdr->wNdpIndex);
        if (hdr->wNdpIndex + ndp->wLength > ncm_interface.rcv_usb_datagram_size) {
            printf("--1.2.1 %d %d\n", hdr->wNdpIndex + ndp->wLength, ncm_interface.rcv_usb_datagram_size);
            return;
        }
        if (ndp->dwSignature != NDP16_SIGNATURE_NCM0  &&  ndp->dwSignature != NDP16_SIGNATURE_NCM1) {
            printf("--1.2.2 0x%lx %d\n", ndp->dwSignature, ncm_interface.rcv_usb_datagram_size);
            return;
        }

        //printf("--2\n");

        int max_rcv_datagrams = (ndp->wLength - 8) / 4;
        ncm_interface.rcv_datagram_index = 0;
        ncm_interface.rcv_datagram_num = 0;
        ncm_interface.rcv_ndp = ndp;
        while (ncm_interface.rcv_datagram_num < max_rcv_datagrams)
        {
#if 0
            printf("  %d %d %d\n", ncm_interface.rcv_datagram_num,
                    ndp->datagram[ncm_interface.rcv_datagram_num].wDatagramIndex,
                    ndp->datagram[ncm_interface.rcv_datagram_num].wDatagramLength);
#endif
            if (    ndp->datagram[ncm_interface.rcv_datagram_num].wDatagramIndex == 0
                &&  ndp->datagram[ncm_interface.rcv_datagram_num].wDatagramLength == 0) {
                break;
            }
            ++ncm_interface.rcv_datagram_num;
        }

#if 0
        printf("tud_network_recv_renew: %d 0x%08lx %d %d\n", ncm_interface.rcv_datagram_num, ndp->dwSignature, ndp->wLength,
                ndp->wNextNdpIndex);
#endif

        ncm_interface.rcv_usb_datagram_size = 0;
    }

    if (ncm_interface.rcv_datagram_num == 0) {
        return;
    }

    const ndp16_t *ndp = ncm_interface.rcv_ndp;

#if 0
    printf("tud_network_recv_renew->: %d %p %d %d\n", ncm_interface.rcv_datagram_index,
            ndp, ndp->datagram[ncm_interface.rcv_datagram_index].wDatagramIndex,
            ndp->datagram[ncm_interface.rcv_datagram_index].wDatagramLength);
#endif

    if (tud_network_recv_cb(ncm_interface.rcv_datagram + ndp->datagram[ncm_interface.rcv_datagram_index].wDatagramIndex,
                            ndp->datagram[ncm_interface.rcv_datagram_index].wDatagramLength)) {
        //printf("!!!!!!!!!!!!!!!!!!!!\n");
        ++ncm_interface.rcv_datagram_index;
    }
}



static void do_in_xfer(uint8_t *buf, uint16_t len)
{
    ncm_interface.can_xmit = false;
    usbd_edpt_xfer(0, ncm_interface.ep_in, buf, len);
}   // do_in_xfer



static void handle_incoming_datagram(uint32_t len)
{
    //printf("!!!!!!!!!!!!!handle_incoming_datagram(%lu) %d\n", len, ncm_interface.rcv_usb_datagram_size);

    ncm_interface.rcv_usb_datagram_size = len;

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

    drv_len += 2 * sizeof(tusb_desc_endpoint_t);

    return drv_len;
}



static void ncm_report(void)
/**
 * called on init
 */
{
    printf("ncm_report - %d\n", ncm_interface.report_state);
    uint8_t const rhport = 0;
    if (ncm_interface.report_state == REPORT_SPEED) {
        ncm_notify_speed_change.header.wIndex = ncm_interface.itf_num;
        usbd_edpt_xfer(rhport, ncm_interface.ep_notif, (uint8_t*) &ncm_notify_speed_change, sizeof(ncm_notify_speed_change));
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
                            if ( !ncm_interface.report_pending) {
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

    //printf("netd_xfer_cb(%d,%d,%d,%lu) [%p]\n", rhport, ep_addr, result, xferred_bytes, xTaskGetCurrentTaskHandle());

    /* new datagram rcv_datagram */
    if (ep_addr == ncm_interface.ep_out) {
        //printf("  EP_OUT\n");
        handle_incoming_datagram(xferred_bytes);
    }

    /* data transmission finished */
    if (ep_addr == ncm_interface.ep_in) {
        //printf("  EP_IN %d %d\n", ncm_interface.can_xmit, ncm_interface.itf_data_alt);
        if (xferred_bytes != 0   &&  xferred_bytes % CFG_TUD_NET_ENDPOINT_SIZE == 0)
        {
            do_in_xfer(NULL, 0); /* a ZLP is needed */
        }
        else
        {
            /* we're finally finished */
            ncm_prepare_for_tx();
        }
    }

    if (ep_addr == ncm_interface.ep_notif) {
        printf("  EP_NOTIF\n");
        ncm_interface.report_pending = false;
        ncm_report();
    }

    return true ;
}   // netd_xfer_cb



bool tud_network_can_xmit(uint16_t size)
/**
 * poll network driver for its ability to accept another packet to transmit
 *
 * context: lwIP
 *
 */
{
    //printf("tud_network_can_xmit() %d [%p]\n", ncm_interface.can_xmit, xTaskGetCurrentTaskHandle());
    return ncm_interface.can_xmit;
}   // tud_network_can_xmit



void tud_network_xmit(void *ref, uint16_t arg)
/**
 * context: lwIP.
 */
{
    transmit_ntb_t *ntb = &transmit_ntb;

    //printf("tud_network_xmit(%p,%d) %d [%p]\n", ref, arg, ncm_interface.can_xmit, xTaskGetCurrentTaskHandle());

    if ( !ncm_interface.can_xmit) {
        return;
    }

    uint16_t size = tud_network_xmit_cb(ntb->data + ncm_interface.next_datagram_offset, ref, arg);

    ntb->ndp.datagram[0].wDatagramIndex = ncm_interface.next_datagram_offset;
    ntb->ndp.datagram[0].wDatagramLength = size;

    ncm_interface.next_datagram_offset += size;

    // round up so the next datagram is aligned correctly
    ncm_interface.next_datagram_offset += (CFG_TUD_NCM_ALIGNMENT - 1);
    ncm_interface.next_datagram_offset -= (ncm_interface.next_datagram_offset % CFG_TUD_NCM_ALIGNMENT);

    // Fill in NTB header
    ntb->nth.dwSignature = NTH16_SIGNATURE;
    ntb->nth.wHeaderLength = sizeof(nth16_t);
    ntb->nth.wSequence = ncm_interface.nth_sequence++;
    ntb->nth.wBlockLength = ncm_interface.next_datagram_offset;
    ntb->nth.wNdpIndex = sizeof(nth16_t);

    // Fill in NDP16 header and terminator
    ntb->ndp.dwSignature = NDP16_SIGNATURE_NCM0;
    ntb->ndp.wLength = sizeof(ndp16_t) + (2) * sizeof(ndp16_datagram_t);
    ntb->ndp.wNextNdpIndex = 0;
    ntb->ndp.datagram[1].wDatagramIndex = 0;
    ntb->ndp.datagram[1].wDatagramLength = 0;

    // Kick off an endpoint transfer
    do_in_xfer(ntb->data, ncm_interface.next_datagram_offset);
}   // tud_network_xmit

#endif
