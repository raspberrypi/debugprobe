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
#include "lwip/err.h"

#include "net_sysview.h"


/**
 * Some definitions for server communication
 * - server port
 * - "Hello" message expected by SystemView App: SEGGER SystemView VM.mm.rr
 */
#define SEGGER_SYSVIEW_MAJOR            3
#define SEGGER_SYSVIEW_MINOR            32
#define SEGGER_SYSVIEW_REV              0
#ifndef SYSVIEW_COMM_SERVER_PORT
    #define SYSVIEW_COMM_SERVER_PORT    19111
#endif
#define SYSVIEW_COMM_APP_HELLO_SIZE     32
#define SYSVIEW_COMM_TARGET_HELLO_SIZE  32
static const uint8_t sysview_hello[SYSVIEW_COMM_TARGET_HELLO_SIZE] = { 'S', 'E', 'G', 'G', 'E', 'R', ' ', 'S', 'y', 's', 't', 'e', 'm', 'V', 'i', 'e', 'w', ' ', 'V',
        '0' + SEGGER_SYSVIEW_MAJOR, '.',
        '0' + (SEGGER_SYSVIEW_MINOR / 10), '0' + (SEGGER_SYSVIEW_MINOR % 10), '.',
        '0' + (SEGGER_SYSVIEW_REV / 10), '0' + (SEGGER_SYSVIEW_REV % 10), '\0', 0, 0, 0, 0, 0 };


// pcb for socket listening
static struct tcp_pcb *sysview_pcb;


enum echo_states
{
    SVS_NONE = 0,
    SVS_WAIT_HELLO,
    SVS_SEND_HELLO,
    SVS_READY,
};


// state of the server socket
struct sysview_state
{
    uint8_t state;
    struct tcp_pcb *pcb;
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
    printf("sysview_close(%p,%p): %d\n", tpcb, svs, svs->state);

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



static err_t sysview_sent(void *arg, struct tcp_pcb *tpcb, uint16_t len)
{
    struct sysview_state *svs = (struct sysview_state *)arg;

    printf("sysview_sent(%p,%p,%d) %d\n", arg, tpcb, len, svs->state);

    if (svs->state == SVS_SEND_HELLO)
    {
        svs->state = SVS_READY;
    }

    return ERR_OK;
}   // sysview_sent



static err_t sysview_recv(void *arg, struct tcp_pcb *tpcb, struct pbuf *p, err_t err)
{
    struct sysview_state *svs = (struct sysview_state *)arg;
    err_t ret_err;

    printf("sysview_recv(%p,%p,%p,%d) %d\n", arg, tpcb, p, err, svs->state);

    LWIP_ASSERT("arg != NULL", arg != NULL);

    if (p == NULL)
    {
        //
        // remote host closed connection
        //
        sysview_close(tpcb, svs);
        ret_err = ERR_OK;
    }
    else if (err != ERR_OK)
    {
        //
        // cleanup, for unknown reason
        //
        // TODO actually return pbuf only, if !ERR_ABRT ??
        if (p != NULL)
        {
            pbuf_free(p);
        }
        ret_err = err;
    }
    else if (svs->state == SVS_WAIT_HELLO)
    {
        //
        // expecting hello message
        //
        printf("sysview_recv, %d:'%s'\n", p->len, (const char *)p->payload);
        if (p->len != SYSVIEW_COMM_APP_HELLO_SIZE)
        {
            // invalid hello
            pbuf_free(p);
            tcp_abort(tpcb);
            // TODO close here?
            ret_err = ERR_ABRT;
        }
        else
        {
            // valid hello, response with hello back
            tcp_recved(tpcb, SYSVIEW_COMM_APP_HELLO_SIZE);
            pbuf_free(p);
            tcp_write(tpcb, sysview_hello, SYSVIEW_COMM_TARGET_HELLO_SIZE, 0);
            svs->state = SVS_SEND_HELLO;
            ret_err = ERR_OK;
        }
    }
    else if (svs->state == SVS_READY)
    {
        //
        // send received data to RTT SysView
        //
        tcp_recved(tpcb, p->len);
        // TODO data to RTT
        pbuf_free(p);
        ret_err = ERR_OK;
    }
    else
    {
        /* unknown svs->state, trash data  */
        tcp_recved(tpcb, p->tot_len);
        pbuf_free(p);
        ret_err = ERR_OK;
    }
    return ret_err;
}   // sysview_recv



err_t sysview_poll(void *arg, struct tcp_pcb *tpcb)
{
    err_t ret_err;
    struct sysview_state *svs = (struct sysview_state *)arg;

    printf("sysview_poll(%p,%p) %d\n", arg, tpcb, svs->state);

    if (svs != NULL)
    {
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
        svs->state = SVS_WAIT_HELLO;
        svs->pcb = newpcb;
        /* pass newly allocated svs to our callbacks */
        tcp_arg(newpcb,  svs);
        tcp_err(newpcb,  sysview_error);
        tcp_recv(newpcb, sysview_recv);
        tcp_poll(newpcb, sysview_poll, 0);        // TODO interval?
        tcp_sent(newpcb, sysview_sent);
        ret_err = ERR_OK;
    }
    else
    {
        printf("sysview_accept(): cannot alloc\n");
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

    err = tcp_bind(pcb, IP_ADDR_ANY, SYSVIEW_COMM_SERVER_PORT);
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
