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


#include "lwip/debug.h"
#include "lwip/stats.h"
#include "lwip/tcp.h"

#include "net_sysview.h"



#define PORT_SYSVIEW           19111



static struct tcp_pcb *sysview_pcb;

enum echo_states
{
    ES_NONE = 0,
    ES_ACCEPTED,
    ES_RECEIVED,
    ES_CLOSING
};


struct sysview_state
{
    u8_t state;
    u8_t retries;
    struct tcp_pcb *pcb;
    /* pbuf (chain) to recycle */
    struct pbuf *p;
};



void sysview_error(void *arg, err_t err)
{
    struct sysview_state *svs;

    printf("sysview_error(): %d\n", err);

    svs = (struct sysview_state *)arg;
    if (svs != NULL)
    {
        mem_free(svs);
    }
}   // sysview_error



void sysview_close(struct tcp_pcb *tpcb, struct sysview_state *svs)
{
    tcp_arg(tpcb, NULL);
    tcp_sent(tpcb, NULL);
    tcp_recv(tpcb, NULL);
    tcp_err(tpcb, NULL);
    tcp_poll(tpcb, NULL, 0);

    if (svs != NULL)
    {
        mem_free(svs);
    }
    tcp_close(tpcb);
}   // sysview_close



static void sysview_send(struct tcp_pcb *tpcb, struct sysview_state *svs)
{
    struct pbuf *ptr;
    err_t wr_err = ERR_OK;

    while ((wr_err == ERR_OK) &&
            (svs->p != NULL) &&
            (svs->p->len <= tcp_sndbuf(tpcb)))
    {
        ptr = svs->p;

        /* enqueue data for transmission */
        wr_err = tcp_write(tpcb, ptr->payload, ptr->len, 1);
        if (wr_err == ERR_OK)
        {
            u16_t plen;
            u8_t freed;

            plen = ptr->len;
            /* continue with next pbuf in chain (if any) */
            svs->p = ptr->next;
            if(svs->p != NULL)
            {
                /* new reference! */
                pbuf_ref(svs->p);
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
            svs->p = ptr;
        }
        else
        {
            /* other problem ?? */
        }
    }
}   // sysview_send



static err_t sysview_sent(void *arg, struct tcp_pcb *tpcb, u16_t len)
{
    struct sysview_state *svs;

    svs = (struct sysview_state *)arg;
    svs->retries = 0;

    if(svs->p != NULL)
    {
        /* still got pbufs to send */
        tcp_sent(tpcb, sysview_sent);
        sysview_send(tpcb, svs);
    }
    else
    {
        /* no more pbufs to send */
        if(svs->state == ES_CLOSING)
        {
            sysview_close(tpcb, svs);
        }
    }
    return ERR_OK;
}   // sysview_sent



static err_t sysview_recv(void *arg, struct tcp_pcb *tpcb, struct pbuf *p, err_t err)
{
    struct sysview_state *svs;
    err_t ret_err;

    LWIP_ASSERT("arg != NULL", arg != NULL);

    svs = (struct sysview_state *)arg;
    if (p == NULL)
    {
        /* remote host closed connection */
        svs->state = ES_CLOSING;
        if(svs->p == NULL)
        {
            /* we're done sending, close it */
            sysview_close(tpcb, svs);
        }
        else
        {
            /* we're not done yet */
            tcp_sent(tpcb, sysview_sent);
            sysview_send(tpcb, svs);
        }
        ret_err = ERR_OK;
    }
    else if(err != ERR_OK)
    {
        /* cleanup, for unkown reason */
        if (p != NULL)
        {
            svs->p = NULL;
            pbuf_free(p);
        }
        ret_err = err;
    }
    else if(svs->state == ES_ACCEPTED)
    {
        /* first data chunk in p->payload */
        svs->state = ES_RECEIVED;
        /* store reference to incoming pbuf (chain) */
        svs->p = p;
        /* install send completion notifier */
        tcp_sent(tpcb, sysview_sent);
        sysview_send(tpcb, svs);
        ret_err = ERR_OK;
    }
    else if (svs->state == ES_RECEIVED)
    {
        /* read some more data */
        if(svs->p == NULL)
        {
            svs->p = p;
            tcp_sent(tpcb, sysview_sent);
            sysview_send(tpcb, svs);
        }
        else
        {
            struct pbuf *ptr;

            /* chain pbufs to the end of what we recv'ed previously  */
            ptr = svs->p;
            pbuf_chain(ptr,p);
        }
        ret_err = ERR_OK;
    }
    else if(svs->state == ES_CLOSING)
    {
        /* odd case, remote side closing twice, trash data */
        tcp_recved(tpcb, p->tot_len);
        svs->p = NULL;
        pbuf_free(p);
        ret_err = ERR_OK;
    }
    else
    {
        /* unkown es->state, trash data  */
        tcp_recved(tpcb, p->tot_len);
        svs->p = NULL;
        pbuf_free(p);
        ret_err = ERR_OK;
    }
    return ret_err;
}   // sysview_recv



err_t sysview_poll(void *arg, struct tcp_pcb *tpcb)
{
    err_t ret_err;
    struct sysview_state *svs;

    svs = (struct sysview_state *)arg;
    if (svs != NULL)
    {
        if (svs->p != NULL)
        {
            /* there is a remaining pbuf (chain)  */
            tcp_sent(tpcb, sysview_sent);
            sysview_send(tpcb, svs);
        }
        else
        {
            /* no remaining pbuf (chain)  */
            if(svs->state == ES_CLOSING)
            {
                sysview_close(tpcb, svs);
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
}   // sysview_poll



static err_t sysview_accept(void *arg, struct tcp_pcb *newpcb, err_t err)
{
    err_t ret_err;
    struct sysview_state *svs;

    /* commonly observed practive to call tcp_setprio(), why? */
    tcp_setprio(newpcb, TCP_PRIO_MAX);

    svs = (struct sysview_state *)mem_malloc(sizeof(struct sysview_state));
    if (svs != NULL)
    {
        svs->state = ES_ACCEPTED;
        svs->pcb = newpcb;
        svs->retries = 0;
        svs->p = NULL;
        /* pass newly allocated es to our callbacks */
        tcp_arg(newpcb, svs);
        tcp_recv(newpcb, sysview_recv);
        tcp_err(newpcb, sysview_error);
        tcp_poll(newpcb, sysview_poll, 0);        // TODO interval?
        ret_err = ERR_OK;
    }
    else
    {
        printf("sysview_accept(): cannot allow\n");
        ret_err = ERR_MEM;
    }
    return ret_err;
}   // sysview_accept



void net_sysview_init(void)
{
    err_t err;
    struct tcp_pcb *pcb;

    pcb = tcp_new_ip_type(IPADDR_TYPE_ANY);
    if (pcb == NULL)
    {
        printf("net_sysview_init(): cannot get pcb\n");
        return;
    }

    err = tcp_bind(pcb, IP_ADDR_ANY, PORT_SYSVIEW);
    if (err != ERR_OK)
    {
        printf("net_sysview_init(): cannot bind, err:%d\n", err);
        return;
    }

    sysview_pcb = tcp_listen_with_backlog(pcb, 1);
    if (sysview_pcb == NULL)
    {
        if (pcb != NULL)
        {
            tcp_close(pcb);
        }
        printf("net_sysview_init(): cannot listen\n");
        return;
    }

    tcp_accept(sysview_pcb, sysview_accept);
}   // net_sysview_init
