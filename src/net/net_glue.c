/*
 * The MIT License (MIT)
 *
 * Copyright (c) 2021 Raspberry Pi (Trading) Ltd.
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


//----------------------------------------------------------------------------------------------------------------------
//
// TCP server for SystemView
// - using RNDIS / ECM because it is driver free for Windows / Linux / iOS
// - we leave the IPv6 stuff outside
//


#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "picoprobe_config.h"

#include "lwip/tcpip.h"
#include "dhserver.h"

#include "tusb.h"
#include "device/usbd_pvt.h" // for usbd_defer_func


#define EV_RCVFRAME_READY     1


/* lwip context */
static struct netif netif_data;

/* shared between tud_network_recv_cb() and service_traffic() */
static struct pbuf *received_frame = NULL;

static uint8_t rcv_buff[4000];
static uint16_t rcv_buff_len = 0;

static uint8_t xmt_buff[4000];
static uint16_t xmt_buff_len = 0;

#ifndef OPT_NET_192_168
    #define OPT_NET_192_168   10
#endif

/* network parameters of this MCU */
static const ip4_addr_t ipaddr  = IPADDR4_INIT_BYTES(192, 168, OPT_NET_192_168, 1);
static const ip4_addr_t netmask = IPADDR4_INIT_BYTES(255, 255, 255, 0);
static const ip4_addr_t gateway = IPADDR4_INIT_BYTES(0, 0, 0, 0);

/* database IP addresses that can be offered to the host; this must be in RAM to store assigned MAC addresses */
static dhcp_entry_t entries[] =
{
    /* mac ip address                          lease time */
    { {0}, IPADDR4_INIT_BYTES(192, 168, OPT_NET_192_168, 2), 24 * 60 * 60 },
};

static const dhcp_config_t dhcp_config =
{
    .router = IPADDR4_INIT_BYTES(0, 0, 0, 0),  // router address (if any)
    .port = 67,                                // listen port
    .dns = IPADDR4_INIT_BYTES(0, 0, 0, 0),     // dns server
    NULL,                                      // dns suffix: specify NULL, otherwise /etc/resolv.conf will be changed
    TU_ARRAY_SIZE(entries),                    // num entry
    entries                                    // entries
};



void tud_network_init_cb(void)
/**
 * initialize any network state back to the beginning
 */
{
    /* if the network is re-initializing and we have a leftover packet, we must do a cleanup */
    if (received_frame != NULL)
    {
        pbuf_free(received_frame);
        received_frame = NULL;
    }
}   // tud_network_init_cb



static void context_tinyusb_tud_network_recv_renew(void *param)
{
    tud_network_recv_renew();
}   // context_tinyusb_tud_network_recv_renew



static void net_glue_usb_to_lwip(void *ptr)
/**
 * handle any packet received by tud_network_recv_cb() in context of lwIP
 */
{
    //printf("net_glue_usb_to_lwip\n");

#if 0
    if (received_frame != NULL) {
        ethernet_input(received_frame, &netif_data);
        pbuf_free(received_frame);
        received_frame = NULL;
        tud_network_recv_renew();
    }
#else
    if (rcv_buff_len != 0) {
        struct pbuf *p = pbuf_alloc(PBUF_RAW, rcv_buff_len, PBUF_POOL);

        if (p) {
            memcpy(p->payload, rcv_buff, rcv_buff_len);
            ethernet_input(p, &netif_data);
            pbuf_free(p);
            rcv_buff_len = 0;
#if 0
            tud_network_recv_renew();
#else
            // did not change anything
            usbd_defer_func(context_tinyusb_tud_network_recv_renew, NULL, false);
#endif
        }
    }
#endif
}   // net_glue_usb_to_lwip



bool tud_network_recv_cb(const uint8_t *src, uint16_t size)
/**
 * Copy buffer (host ->) TinyUSB -> lwIP (-> application)
 * \return false if the packet buffer was not accepted
 */
{
    //printf("tud_network_recv_cb(%p,%u)\n", src, size);

#if 0
    /* this shouldn't happen, but if we get another packet before
    parsing the previous, we must signal our inability to accept it */
    if (received_frame)
        return false;

    if (size) {
        struct pbuf *p = pbuf_alloc(PBUF_RAW, size, PBUF_POOL);

        if (p) {
            /* pbuf_alloc() has already initialized struct; all we need to do is copy the data */
            memcpy(p->payload, src, size);

            /* store away the pointer for service_traffic() to later handle */
            received_frame = p;
            tcpip_callback_with_block(net_glue_usb_to_lwip, NULL, 0);
        }
    }
#else
    if (rcv_buff_len != 0)
        return false;

    if (size != 0) {
        assert(size < sizeof(rcv_buff));
        memcpy(rcv_buff, src, size);
        rcv_buff_len = size;
        tcpip_callback_with_block(net_glue_usb_to_lwip, NULL, 0);
    }
#endif
    return true;
}   // tud_network_recv_cb



uint16_t tud_network_xmit_cb(uint8_t *dst, void *ref, uint16_t arg)
/**
 * This does the actual copy operation into a TinyUSB buffer.
 * Called by tud_network_xmit().
 *
 * (application ->) lwIP -> TinyUSB (-> host)
 *
 * \return number of bytes copied
 */
{
    //printf("!!!!!!!!!!!!!!tud_network_xmit_cb(%p,%p,%u)\n", dst, ref, arg);

#if 0
    struct pbuf *p = (struct pbuf *)ref;
    struct pbuf *q;
    uint16_t len = 0;

    (void)arg; /* unused for this example */

    /* traverse the "pbuf chain"; see ./lwip/src/core/pbuf.c for more info */
    for (q = p;  q != NULL;  q = q->next)
    {
        memcpy(dst, (char *)q->payload, q->len);
        dst += q->len;
        len += q->len;
        if (q->len == q->tot_len)
            break;
    }

    return len;
#elif 0
    struct pbuf *p = (struct pbuf *)ref;

    (void)arg; /* unused for this example */

    return pbuf_copy_partial(p, dst, p->tot_len, 0);
#else
    uint16_t r = xmt_buff_len;
    memcpy(dst, xmt_buff, xmt_buff_len);
    xmt_buff_len = 0;
    return r;
#endif
}   // tud_network_xmit_cb



static void context_tinyusb_linkoutput(void *param)
{
    if ( !tud_network_can_xmit(xmt_buff_len)) {
        printf("context_tinyusb_linkoutput: sleep\n");
        vTaskDelay(pdMS_TO_TICKS(10));
        usbd_defer_func(context_tinyusb_linkoutput, NULL, false);
    }
    else {
        tud_network_xmit(xmt_buff, 0);
    }
}   // context_tinyusb_linkoutput



static err_t linkoutput_fn(struct netif *netif, struct pbuf *p)
/**
 * called by lwIP to transmit data to TinyUSB
 */
{
    (void)netif;

#if 0
    // TODO: interesting: if !can_xmit, then TinyUSB does the transfer in the "background"
    //       but this is priority dependent.  Better sleep here for a short period and then repeat.
    for (;;)
    {
        //printf("linkoutput_fn()\n");
        /* if TinyUSB isn't ready, we must signal back to lwip that there is nothing we can do */
        if ( !tud_ready()) {
            return ERR_USE;
        }

        /* if the network driver can accept another packet, we make it happen */
        if (tud_network_can_xmit(p->tot_len)) {
            tud_network_xmit(p, 0 /* unused for this example */);
            return ERR_OK;
        }
//        else
//            return ERR_USE;
//
//        tud_task();
        //printf("linkoutput_fn - sleep\n");
        vTaskDelay(pdMS_TO_TICKS(10));
    }

    return ERR_USE;
#else
    if ( !tud_ready()) {
        return ERR_USE;
    }

    if (xmt_buff_len != 0) {
        return ERR_USE;
    }

    xmt_buff_len = pbuf_copy_partial(p, xmt_buff, p->tot_len, 0);
    usbd_defer_func(context_tinyusb_linkoutput, NULL, false);

    return ERR_OK;
#endif
}   // linkoutput_fn



static err_t ip4_output_fn(struct netif *netif, struct pbuf *p, const ip4_addr_t *addr)
{
    return etharp_output(netif, p, addr);
}   // ip4_output_fn



#if LWIP_IPV6
static err_t ip6_output_fn(struct netif *netif, struct pbuf *p, const ip6_addr_t *addr)
{
    return ethip6_output(netif, p, addr);
}   // ip6_output_fn
#endif



static err_t netif_init_cb(struct netif *netif)
{
    LWIP_ASSERT("netif != NULL", (netif != NULL));
    netif->mtu = CFG_TUD_NET_MTU;
    netif->flags = NETIF_FLAG_BROADCAST | NETIF_FLAG_ETHARP | NETIF_FLAG_LINK_UP | NETIF_FLAG_UP;
    netif->state = NULL;
    netif->name[0] = 'E';
    netif->name[1] = 'X';
    netif->linkoutput = linkoutput_fn;
    netif->output = ip4_output_fn;
#if LWIP_IPV6
    netif->output_ip6 = ip6_output_fn;
#endif
    return ERR_OK;
}   // netif_init_cb



void net_glue_init(void)
{
    struct netif *netif = &netif_data;

    tcpip_init(NULL, NULL);

    /* the lwip virtual MAC address must be different from the host's; to ensure this, we toggle the LSbit */
    netif->hwaddr_len = sizeof(tud_network_mac_address);
    memcpy(netif->hwaddr, tud_network_mac_address, sizeof(tud_network_mac_address));
    netif->hwaddr[5] ^= 0x01;     // don't know what for, but everybody is doing it...

    netif = netif_add(netif, &ipaddr, &netmask, &gateway, NULL, netif_init_cb, ip_input);
#if LWIP_IPV6
    netif_create_ip6_linklocal_address(netif, 1);
#endif
    netif_set_default(netif);

    while ( !netif_is_up(netif))
        ;

    while (dhserv_init(&dhcp_config) != ERR_OK)
        ;
}   // net_glue_init
