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

/**
 * TODO
 * - what does \a rhport mean?
 */


#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include <tusb.h>
#include "device/usbd.h"
#include "device/usbd_pvt.h"


typedef struct {
    uint8_t      ep_in;       //!< endpoint for TODO
    uint8_t      ep_out;      //!< endpoint for TODO
    uint8_t      ep_notif;    //!< notification endpoint
    uint8_t      itf_num;     //!< interface number
} ncm_interface_t;


static ncm_interface_t ncm_interface;



void netd_init(void)
/**
 * Initialize the driver data structures.
 * Might be called several times.
 */
{
    printf("netd_init()\n");

    memset( &ncm_interface, 0, sizeof(ncm_interface));
}   // netd_init



void netd_reset(uint8_t rhport)
/**
 * Resets the port.
 * In this driver this is the same as netd_init()
 */
{
    printf("netd_reset(%d)\n", rhport);

    netd_init();
}   // netd_reset



uint16_t netd_open(uint8_t rhport, tusb_desc_interface_t const *itf_desc, uint16_t max_len)
/**
 * Open the USB interface.
 * - parse the USB descriptor \a TUD_CDC_NCM_DESCRIPTOR for itfnum and endpoints
 * - a specific order of elements in the descriptor is tested.
 *
 * \note
 *   Actually all of the information could be read directly from \a itf_desc, because the
 *   structure and the values are well known.  But we do it this way.
 *
 * \post
 * - \a itf_num set
 * - \a ep_notif, \a ep_in and \a ep_out are set
 * - USB interface is open
 */
{
    printf("netd_open(%d,%p,%d)\n", rhport, itf_desc, max_len);

    TU_ASSERT(ncm_interface.ep_notif == 0, 0);           // interface not allocated!

    ncm_interface.itf_num = itf_desc->bInterfaceNumber;  // management interface

    //
    // skip the two first entries and the following TUSB_DESC_CS_INTERFACE entries
    //
    uint16_t drv_len = sizeof(tusb_desc_interface_t);
    uint8_t const *p_desc = tu_desc_next(itf_desc);
    while (tu_desc_type(p_desc) == TUSB_DESC_CS_INTERFACE  &&  drv_len <= max_len) {
        drv_len += tu_desc_len(p_desc);
        p_desc = tu_desc_next(p_desc);
    }

    //
    // get notification endpoint
    //
    TU_ASSERT(tu_desc_type(p_desc) == TUSB_DESC_ENDPOINT, 0);
    TU_ASSERT(usbd_edpt_open(rhport, (tusb_desc_endpoint_t const* ) p_desc), 0);
    ncm_interface.ep_notif = ((tusb_desc_endpoint_t const*) p_desc)->bEndpointAddress;
    drv_len += tu_desc_len(p_desc);
    p_desc = tu_desc_next(p_desc);

    //
    // skip the following TUSB_DESC_INTERFACE entries (which must be TUSB_CLASS_CDC_DATA)
    //
    while (tu_desc_type(p_desc) == TUSB_DESC_INTERFACE  &&  drv_len <= max_len) {
        tusb_desc_interface_t const *data_itf_desc = (tusb_desc_interface_t const*)p_desc;
        TU_ASSERT(data_itf_desc->bInterfaceClass == TUSB_CLASS_CDC_DATA, 0);

        drv_len += tu_desc_len(p_desc);
        p_desc = tu_desc_next(p_desc);
    }

    //
    // a TUSB_DESC_ENDPOINT (actually two) must follow, open these endpoints
    //
    TU_ASSERT(tu_desc_type(p_desc) == TUSB_DESC_ENDPOINT, 0);
    TU_ASSERT(usbd_open_edpt_pair(rhport, p_desc, 2, TUSB_XFER_BULK, &ncm_interface.ep_out, &ncm_interface.ep_in));
    drv_len += 2 * sizeof(tusb_desc_endpoint_t);

    return drv_len;
}   // netd_open



bool netd_xfer_cb(uint8_t rhport, uint8_t ep_addr, xfer_result_t result, uint32_t xferred_bytes)
{
    printf("netd_xfer_cb(%d,%d,%d,%u)\n", rhport, ep_addr, result, (unsigned)xferred_bytes);
    return true;
}   // netd_xfer_cb



bool netd_control_xfer_cb(uint8_t rhport, uint8_t stage, tusb_control_request_t const *request)
{
    printf("netd_control_xfer_cb(%d, %d, %p)\n", rhport, stage, request);
    return true;
}   // netd_control_xfer_cb



bool tud_network_can_xmit(uint16_t size)
{
    printf("tud_network_can_xmit(%d)\n", size);
    return false;
}   // tud_network_can_xmit



void tud_network_xmit(void *ref, uint16_t arg)
{
    printf("tud_network_xmit(%p, %d)\n", ref, arg);
}   // tud_network_xmit
