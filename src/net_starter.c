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
#include "lwip/ip.h"
#include "lwip/etharp.h"
#include "lwip/tcpip.h"
#include "lwip/timeouts.h"
#include "dhserver.h"
#include "dnserver.h"

#include "FreeRTOS.h"
#include "tusb.h"



/* lwip context */
static struct netif netif_data;

/* shared between tud_network_recv_cb() and service_traffic() */
static struct pbuf *received_frame;

#define INIT_IP4(a,b,c,d) { PP_HTONL(LWIP_MAKEU32(a,b,c,d)) }

/* network parameters of this MCU */
static const ip4_addr_t ipaddr  = INIT_IP4(192, 168, 7, 1);
static const ip4_addr_t netmask = INIT_IP4(255, 255, 255, 0);
static const ip4_addr_t gateway = INIT_IP4(0, 0, 0, 0);

/* database IP addresses that can be offered to the host; this must be in RAM to store assigned MAC addresses */
static dhcp_entry_t entries[] =
{
    /* mac ip address                          lease time */
    { {0}, INIT_IP4(192, 168, 7, 2), 24 * 60 * 60 },
    { {0}, INIT_IP4(192, 168, 7, 3), 24 * 60 * 60 },
    { {0}, INIT_IP4(192, 168, 7, 4), 24 * 60 * 60 },
};

static const dhcp_config_t dhcp_config =
{
    .router = INIT_IP4(0, 0, 0, 0),            /* router address (if any) */
    .port = 67,                                /* listen port */
    .dns = INIT_IP4(0, 0, 0, 0),               /* dns server (if any) */
    "usb",                                     /* dns suffix */
    TU_ARRAY_SIZE(entries),                    /* num entry */
    entries                                    /* entries */
};

static TaskHandle_t   task_net_starter = NULL;


//----------------------------------------------------------------------------------------------------------------------
#if 1

#include "lwip/debug.h"
#include "lwip/stats.h"
#include "lwip/tcp.h"


static struct tcp_pcb *echo_pcb;

enum echo_states
{
  ES_NONE = 0,
  ES_ACCEPTED,
  ES_RECEIVED,
  ES_CLOSING
};

struct echo_state
{
  u8_t state;
  u8_t retries;
  struct tcp_pcb *pcb;
  /* pbuf (chain) to recycle */
  struct pbuf *p;
};

err_t echo_accept(void *arg, struct tcp_pcb *newpcb, err_t err);
err_t echo_recv(void *arg, struct tcp_pcb *tpcb, struct pbuf *p, err_t err);
void echo_error(void *arg, err_t err);
err_t echo_poll(void *arg, struct tcp_pcb *tpcb);
err_t echo_sent(void *arg, struct tcp_pcb *tpcb, u16_t len);
void echo_send(struct tcp_pcb *tpcb, struct echo_state *es);
void echo_close(struct tcp_pcb *tpcb, struct echo_state *es);

void
echo_init(void)
{
    printf("!!!!!!!!!!!!!!!!!!!! echo_init\n");
  echo_pcb = tcp_new();
  if (echo_pcb != NULL)
  {
    err_t err;

    err = tcp_bind(echo_pcb, IP_ADDR_ANY, 7);
    if (err == ERR_OK)
    {
      echo_pcb = tcp_listen(echo_pcb);
      tcp_accept(echo_pcb, echo_accept);
    }
    else
    {
      /* abort? output diagnostic? */
        printf("!!!!!!!!!!!!!!!!!!!! cannot bind\n");
    }
  }
  else
  {
    /* abort? output diagnostic? */
      printf("!!!!!!!!!!!!!!!!!!!! tcp_new\n");
  }
}


err_t
echo_accept(void *arg, struct tcp_pcb *newpcb, err_t err)
{
  err_t ret_err;
  struct echo_state *es;

  LWIP_UNUSED_ARG(arg);
  LWIP_UNUSED_ARG(err);

  /* commonly observed practive to call tcp_setprio(), why? */
  tcp_setprio(newpcb, TCP_PRIO_MIN);

  es = (struct echo_state *)mem_malloc(sizeof(struct echo_state));
  if (es != NULL)
  {
    es->state = ES_ACCEPTED;
    es->pcb = newpcb;
    es->retries = 0;
    es->p = NULL;
    /* pass newly allocated es to our callbacks */
    tcp_arg(newpcb, es);
    tcp_recv(newpcb, echo_recv);
    tcp_err(newpcb, echo_error);
    tcp_poll(newpcb, echo_poll, 0);
    ret_err = ERR_OK;
  }
  else
  {
    ret_err = ERR_MEM;
  }
  return ret_err;
}

err_t
echo_recv(void *arg, struct tcp_pcb *tpcb, struct pbuf *p, err_t err)
{
  struct echo_state *es;
  err_t ret_err;

  LWIP_ASSERT("arg != NULL",arg != NULL);
  es = (struct echo_state *)arg;
  if (p == NULL)
  {
    /* remote host closed connection */
    es->state = ES_CLOSING;
    if(es->p == NULL)
    {
       /* we're done sending, close it */
       echo_close(tpcb, es);
    }
    else
    {
      /* we're not done yet */
      tcp_sent(tpcb, echo_sent);
      echo_send(tpcb, es);
    }
    ret_err = ERR_OK;
  }
  else if(err != ERR_OK)
  {
    /* cleanup, for unkown reason */
    if (p != NULL)
    {
      es->p = NULL;
      pbuf_free(p);
    }
    ret_err = err;
  }
  else if(es->state == ES_ACCEPTED)
  {
    /* first data chunk in p->payload */
    es->state = ES_RECEIVED;
    /* store reference to incoming pbuf (chain) */
    es->p = p;
    /* install send completion notifier */
    tcp_sent(tpcb, echo_sent);
    echo_send(tpcb, es);
    ret_err = ERR_OK;
  }
  else if (es->state == ES_RECEIVED)
  {
    /* read some more data */
    if(es->p == NULL)
    {
      es->p = p;
      tcp_sent(tpcb, echo_sent);
      echo_send(tpcb, es);
    }
    else
    {
      struct pbuf *ptr;

      /* chain pbufs to the end of what we recv'ed previously  */
      ptr = es->p;
      pbuf_chain(ptr,p);
    }
    ret_err = ERR_OK;
  }
  else if(es->state == ES_CLOSING)
  {
    /* odd case, remote side closing twice, trash data */
    tcp_recved(tpcb, p->tot_len);
    es->p = NULL;
    pbuf_free(p);
    ret_err = ERR_OK;
  }
  else
  {
    /* unkown es->state, trash data  */
    tcp_recved(tpcb, p->tot_len);
    es->p = NULL;
    pbuf_free(p);
    ret_err = ERR_OK;
  }
  return ret_err;
}

void
echo_error(void *arg, err_t err)
{
  struct echo_state *es;

  LWIP_UNUSED_ARG(err);

  es = (struct echo_state *)arg;
  if (es != NULL)
  {
    mem_free(es);
  }
}

err_t
echo_poll(void *arg, struct tcp_pcb *tpcb)
{
  err_t ret_err;
  struct echo_state *es;

  es = (struct echo_state *)arg;
  if (es != NULL)
  {
    if (es->p != NULL)
    {
      /* there is a remaining pbuf (chain)  */
      tcp_sent(tpcb, echo_sent);
      echo_send(tpcb, es);
    }
    else
    {
      /* no remaining pbuf (chain)  */
      if(es->state == ES_CLOSING)
      {
        echo_close(tpcb, es);
      }
    }
    ret_err = ERR_OK;
  }
  else
  {
    /* nothing to be done */
    tcp_abort(tpcb);
    ret_err = ERR_ABRT;
  }
  return ret_err;
}

err_t
echo_sent(void *arg, struct tcp_pcb *tpcb, u16_t len)
{
  struct echo_state *es;

  LWIP_UNUSED_ARG(len);

  es = (struct echo_state *)arg;
  es->retries = 0;

  if(es->p != NULL)
  {
    /* still got pbufs to send */
    tcp_sent(tpcb, echo_sent);
    echo_send(tpcb, es);
  }
  else
  {
    /* no more pbufs to send */
    if(es->state == ES_CLOSING)
    {
      echo_close(tpcb, es);
    }
  }
  return ERR_OK;
}

void
echo_send(struct tcp_pcb *tpcb, struct echo_state *es)
{
  struct pbuf *ptr;
  err_t wr_err = ERR_OK;

  while ((wr_err == ERR_OK) &&
         (es->p != NULL) &&
         (es->p->len <= tcp_sndbuf(tpcb)))
  {
  ptr = es->p;

  /* enqueue data for transmission */
  wr_err = tcp_write(tpcb, ptr->payload, ptr->len, 1);
  if (wr_err == ERR_OK)
  {
     u16_t plen;
      u8_t freed;

     plen = ptr->len;
     /* continue with next pbuf in chain (if any) */
     es->p = ptr->next;
     if(es->p != NULL)
     {
       /* new reference! */
       pbuf_ref(es->p);
     }
     /* chop first pbuf from chain */
      do
      {
        /* try hard to free pbuf */
        freed = pbuf_free(ptr);
      }
      while(freed == 0);
     /* we can read more data now */
     tcp_recved(tpcb, plen);
   }
   else if(wr_err == ERR_MEM)
   {
      /* we are low on memory, try later / harder, defer to poll */
     es->p = ptr;
   }
   else
   {
     /* other problem ?? */
   }
  }
}

void
echo_close(struct tcp_pcb *tpcb, struct echo_state *es)
{
  tcp_arg(tpcb, NULL);
  tcp_sent(tpcb, NULL);
  tcp_recv(tpcb, NULL);
  tcp_err(tpcb, NULL);
  tcp_poll(tpcb, NULL, 0);

  if (es != NULL)
  {
    mem_free(es);
  }
  tcp_close(tpcb);
}
#endif


//----------------------------------------------------------------------------------------------------------------------



#if 0
static void service_traffic(void)
{
    /* handle any packet received by tud_network_recv_cb() */
    if (received_frame) {
        printf("service_traffic(): %p\n", received_frame);

        ethernet_input(received_frame, &netif_data);
        pbuf_free(received_frame);
        received_frame = NULL;
        tud_network_recv_renew();
    }
    sys_check_timeouts();
}   // service_traffic
#endif



/* handle any DNS requests from dns-server */
bool dns_query_proc(const char *name, ip4_addr_t *addr)
{
    printf("dns_query_proc(%s,.)\n", name);

    if (0 == strcmp(name, "tiny.usb")) {
        *addr = ipaddr;
        return true;
    }
    return false;
}   // dns_query_proc



void tud_network_init_cb(void)
{
    printf("tud_network_init_cb() - %p\n", received_frame);

    /* if the network is re-initializing and we have a leftover packet, we must do a cleanup */
    if (received_frame)
    {
        pbuf_free(received_frame);
        received_frame = NULL;
    }

#if 0
    printf("tud_network_init_cb() a\n");
    while ( !netif_is_up(&netif_data))
        ;
    printf("tud_network_init_cb() b\n");
    while (dhserv_init(&dhcp_config) != ERR_OK)
        ;
    printf("tud_network_init_cb() c\n");
    while (dnserv_init(IP_ADDR_ANY, 53, dns_query_proc) != ERR_OK)
        ;
#endif
    printf("tud_network_init_cb() d\n");
}   // tud_network_init_cb



bool tud_network_recv_cb(const uint8_t *src, uint16_t size)
{
    printf("tud_network_recv_cb(%p,%u)\n", src, size);

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
        }
    }
    return true;
}   // tud_network_recv_cb



uint16_t tud_network_xmit_cb(uint8_t *dst, void *ref, uint16_t arg)
{
    printf("tud_network_xmit_cb(%p,%p,%u)\n", dst, ref, arg);

    struct pbuf *p = (struct pbuf *)ref;

    (void)arg; /* unused for this example */

    return pbuf_copy_partial(p, dst, p->tot_len, 0);
}   // tud_network_xmit_cb



void tud_network_link_state_cb(bool state)
{
    printf("tud_network_link_state_cb(%d)\n", state);
}   // tud_network_link_state_cb



static err_t my_linkoutput_fn(struct netif *netif, struct pbuf *p)
{
    (void)netif;

    printf("my_linkoutput_fn()\n");

    for (;;) {
        /* if TinyUSB isn't ready, we must signal back to lwip that there is nothing we can do */
        if (!tud_ready())
            return ERR_USE;

        /* if the network driver can accept another packet, we make it happen */
        if (tud_network_can_xmit(p->tot_len)) {
            tud_network_xmit(p, 0 /* unused for this example */);
            return ERR_OK;
        }

        /* transfer execution to TinyUSB in the hopes that it will finish transmitting the prior packet */
        tud_task();
    }
}   // my_linkoutput_fn



static err_t my_ip4_output_fn(struct netif *netif, struct pbuf *p, const ip4_addr_t *addr)
{
    printf("my_ip4_output_fn()\n");

    return etharp_output(netif, p, addr);
}   // my_ip4_output_fn



err_t my_ip_input(struct pbuf *p, struct netif *inp)
{
    printf("my_ip_input(%p,%p)\n", p, inp);
    if (p != NULL) {
        if (IP_HDR_GET_VERSION(p->payload) == 6) {
            printf("my_ip_input 6\n");
            //return ip6_input(p, inp);
            return ERR_ABRT;
        }
        printf("my_ip_input 4\n");
        return ip4_input(p, inp);
    }
    return ERR_VAL;
}   // my_ip_input



static err_t my_netif_init_cb(struct netif *netif)
{
    printf("my_netif_init_cb(%p)\n", netif);

    LWIP_ASSERT("netif != NULL", (netif != NULL));
    netif->mtu = CFG_TUD_NET_MTU;
    netif->flags = NETIF_FLAG_BROADCAST | NETIF_FLAG_ETHARP | NETIF_FLAG_LINK_UP | NETIF_FLAG_UP;
    //netif->hwaddr_len = ETHARP_HWADDR_LEN;
    netif->state = NULL;
    netif->name[0] = 'E';
    netif->name[1] = 'X';
    netif->linkoutput = my_linkoutput_fn;
    netif->output = my_ip4_output_fn;
    return ERR_OK;
}   // my_netif_init_cb



static void init_lwip(void)
{
    struct netif *netif = &netif_data;

    printf("init_lwip()\n");
#if NO_SYS
    lwip_init();
#else
    tcpip_init(NULL, NULL);
#endif

    /* the lwip virtual MAC address must be different from the host's; to ensure this, we toggle the LSbit */
#if 0
    netif->hwaddr_len = sizeof(tud_network_mac_address);
    memcpy(netif->hwaddr, tud_network_mac_address, sizeof(tud_network_mac_address));
#else
    netif->hwaddr_len = NETIF_MAX_HWADDR_LEN;
    memcpy(netif->hwaddr, tud_network_mac_address, NETIF_MAX_HWADDR_LEN);
#endif
    netif->hwaddr[5] ^= 0x01;

    netif = netif_add(netif, &ipaddr, &netmask, &gateway, NULL, my_netif_init_cb, my_ip_input);
    printf("init_lwip() - %p\n", netif);
    netif_set_default(netif);

#if 1
    printf("init_lwip() a\n");
    while ( !netif_is_up(&netif_data))
        printf("init_lwip() ax\n");
#if 1
    printf("init_lwip() b\n");
    while (dhserv_init(&dhcp_config) != ERR_OK)
        printf("init_lwip() bx\n");
    printf("init_lwip() c\n");
    while (dnserv_init(IP_ADDR_ANY, 53, dns_query_proc) != ERR_OK)
        printf("init_lwip() cx\n");
#endif
#endif
    printf("init_lwip() d\n");
}   // init_lwip



void net_starter_thread(void *ptr)
{
    vTaskDelay(pdMS_TO_TICKS(2000));

    echo_init();

    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(200));

        /* handle any packet received by tud_network_recv_cb() */
        if (received_frame) {
            printf("service_traffic(): %p\n", received_frame);

            ethernet_input(received_frame, &netif_data);
            pbuf_free(received_frame);
            received_frame = NULL;
            tud_network_recv_renew();
        }
        sys_check_timeouts();
    }
}   // net_starter_thread



void net_starter_init(uint32_t task_prio)
{
    printf("net_starter_init()\n");

    //tud_init(0);
    init_lwip();

#if 0
    events = xEventGroupCreate();

    stream_rtt = xStreamBufferCreate(STREAM_RTT_SIZE, STREAM_RTT_TRIGGER);
    if (stream_rtt == NULL) {
        picoprobe_error("net_starter_init: cannot create stream_rtt\n");
    }
#endif

    xTaskCreateAffinitySet(net_starter_thread, "NET_STARTER", configMINIMAL_STACK_SIZE, NULL, task_prio, 1, &task_net_starter);
    if (task_net_starter == NULL)
    {
        picoprobe_error("net_starter_init: cannot create task_net_starter\n");
    }
}   // net_starter_init
