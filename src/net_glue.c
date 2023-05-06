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

#include "lwip/init.h"
#include "lwip/timeouts.h"
#include "lwip/ethip6.h"

#include "lwip/tcpip.h"
#include "dhserver.h"
#if OPT_NET_DNS
    #include "dnserver.h"
#endif

#include "FreeRTOS.h"
#include "event_groups.h"

#include "tusb.h"


#define EV_RCVFRAME_READY     1


/* lwip context */
static struct netif netif_data;

/* shared between tud_network_recv_cb() and service_traffic() */
static struct pbuf *received_frame;

/* network parameters of this MCU */
static const ip4_addr_t ipaddr  = IPADDR4_INIT_BYTES(192, 168, 10, 1);
static const ip4_addr_t netmask = IPADDR4_INIT_BYTES(255, 255, 255, 0);
static const ip4_addr_t gateway = IPADDR4_INIT_BYTES(0, 0, 0, 0);

/* database IP addresses that can be offered to the host; this must be in RAM to store assigned MAC addresses */
static dhcp_entry_t entries[] =
{
    /* mac ip address                          lease time */
    { {0}, IPADDR4_INIT_BYTES(192, 168, 10, 2), 24 * 60 * 60 },
    { {0}, IPADDR4_INIT_BYTES(192, 168, 10, 3), 24 * 60 * 60 },
    { {0}, IPADDR4_INIT_BYTES(192, 168, 10, 4), 24 * 60 * 60 },
};

static const dhcp_config_t dhcp_config =
{
    .router = IPADDR4_INIT_BYTES(0, 0, 0, 0),  /* router address (if any) */
    .port = 67,                                /* listen port */
#if OPT_NET_DNS
    .dns = IPADDR4_INIT_BYTES(192, 168, 10, 1),/* dns server (if any) */
#else
    .dns = IPADDR4_INIT_BYTES(0, 0, 0, 0),
#endif
    "usb",                                     /* dns suffix */
    TU_ARRAY_SIZE(entries),                    /* num entry */
    entries                                    /* entries */
};

static TaskHandle_t   task_net_glue = NULL;

static EventGroupHandle_t  events;



#if OPT_NET_DNS
static bool dns_query_proc(const char *name, ip4_addr_t *addr)
/**
 * Handle any DNS requests from dns-server
 * If enabled all DNS requests seem to go over the Pico!?
 */
{
    printf("dns_query_proc(%s,.)\n", name);

    if (0 == strcmp(name, "tiny.usb")) {
        *addr = ipaddr;
        return true;
    }
    return false;
}   // dns_query_proc
#endif



void tud_network_init_cb(void)
{
    /* if the network is re-initializing and we have a leftover packet, we must do a cleanup */
    if (received_frame)
    {
        pbuf_free(received_frame);
        received_frame = NULL;
    }
}   // tud_network_init_cb



bool tud_network_recv_cb(const uint8_t *src, uint16_t size)
{
    //printf("!!!!!!!!!!!!!!tud_network_recv_cb(%p,%u)\n", src, size);

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
            xEventGroupSetBits(events, EV_RCVFRAME_READY);
        }
    }
    return true;
}   // tud_network_recv_cb



uint16_t tud_network_xmit_cb(uint8_t *dst, void *ref, uint16_t arg)
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
#else
    struct pbuf *p = (struct pbuf *)ref;

    (void)arg; /* unused for this example */

    return pbuf_copy_partial(p, dst, p->tot_len, 0);
#endif
}   // tud_network_xmit_cb



static err_t linkoutput_fn(struct netif *netif, struct pbuf *p)
{
    (void)netif;

    for (;;) {
        /* if TinyUSB isn't ready, we must signal back to lwip that there is nothing we can do */
        if (!tud_ready())
            return ERR_USE;

        /* if the network driver can accept another packet, we make it happen */
        if (tud_network_can_xmit(p->tot_len)) {
            tud_network_xmit(p, 0 /* unused for this example */);
            return ERR_OK;
        }
    }
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



static void init_lwip(void)
{
    struct netif *netif = &netif_data;

    tcpip_init(NULL, NULL);

    /* the lwip virtual MAC address must be different from the host's; to ensure this, we toggle the LSbit */
    netif->hwaddr_len = sizeof(tud_network_mac_address);
    memcpy(netif->hwaddr, tud_network_mac_address, sizeof(tud_network_mac_address));
    netif->hwaddr[5] ^= 0x01;

    netif = netif_add(netif, &ipaddr, &netmask, &gateway, NULL, netif_init_cb, ip_input);
#if LWIP_IPV6
    netif_create_ip6_linklocal_address(netif, 1);
#endif
    netif_set_default(netif);

    while ( !netif_is_up(netif))
        ;

    while (dhserv_init(&dhcp_config) != ERR_OK)
        ;
#if OPT_NET_DNS
    while (dnserv_init(IP_ADDR_ANY, 53, dns_query_proc) != ERR_OK)
        ;
#endif
}   // init_lwip



void net_glue_thread(void *ptr)
{
    for (;;) {
        xEventGroupWaitBits(events, EV_RCVFRAME_READY, pdTRUE, pdFALSE, pdMS_TO_TICKS(100));

        /* handle any packet received by tud_network_recv_cb() */
        if (received_frame) {
            ethernet_input(received_frame, &netif_data);
            pbuf_free(received_frame);
            received_frame = NULL;
            tud_network_recv_renew();
        }
    }
}   // net_glue_thread



void net_glue_init(uint32_t task_prio)
{
    events = xEventGroupCreate();

    init_lwip();

    xTaskCreateAffinitySet(net_glue_thread, "NET_GLUE", configMINIMAL_STACK_SIZE, NULL, task_prio, 1, &task_net_glue);
    if (task_net_glue == NULL)
    {
        picoprobe_error("net_glue_init: cannot create task_net_glue\n");
    }
}   // net_glue_init
