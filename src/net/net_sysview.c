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
// - using NCM because it is driver free for Windows / Linux / iOS
// - we leave the IPv6 stuff outside
//

#include "FreeRTOS.h"
#include "stream_buffer.h"

#include "lwip/debug.h"
#include "lwip/stats.h"
#include "lwip/tcp.h"
#include "lwip/tcpip.h"
#include "lwip/err.h"

#include "picoprobe_config.h"
#include "net_sysview.h"
#include "rtt_io.h"


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
static struct tcp_pcb *m_pcb_listen;

enum echo_states
{
    SVS_NONE = 0,
    SVS_WAIT_HELLO,
    SVS_SEND_HELLO,
    SVS_READY,
};

// only one client instance is possible
static uint8_t m_state;
struct tcp_pcb *m_pcb_client;
static uint32_t m_xmt_cnt;

#define STREAM_SYSVIEW_SIZE      4096
#define STREAM_SYSVIEW_TRIGGER   32

static StreamBufferHandle_t      stream_sysview_to_host;

static bool block_call_back_message;
static bool block_call_to_tcp_output;



void sysview_error(void *arg, err_t err)
{
    picoprobe_error("sysview_error: %d\n", err);

    m_state = SVS_NONE;
}   // sysview_error



void sysview_close(struct tcp_pcb *tpcb)
{
    //printf("sysview_close(%p): %d\n", tpcb, m_state);

    picoprobe_info("=================================== SysView disconnect\n");

    tcp_arg(tpcb, NULL);
    tcp_sent(tpcb, NULL);
    tcp_recv(tpcb, NULL);
    tcp_err(tpcb, NULL);
    tcp_poll(tpcb, NULL, 0);

    tcp_close(tpcb);

    xStreamBufferReset(stream_sysview_to_host);
    
    block_call_to_tcp_output = false;
    block_call_back_message = false;

    m_state = SVS_NONE;
}   // sysview_close



static void sysview_try_send(void *ctx)
{
    block_call_back_message = false;

    if ( !xStreamBufferIsEmpty(stream_sysview_to_host)) {
        size_t cnt;
        size_t max_cnt;
        uint8_t tx_buf[512];
        err_t err;

        max_cnt = tcp_sndbuf(m_pcb_client);
        //printf("sysview_try_send: %d %d\n", xStreamBufferBytesAvailable(stream_sysview_to_host), max_cnt);
        if (max_cnt != 0) {
            max_cnt = MIN(sizeof(tx_buf), max_cnt);
            cnt = xStreamBufferReceive(stream_sysview_to_host, tx_buf, max_cnt, pdMS_TO_TICKS(10));
            if (cnt != 0) {
                // the write has either 512 bytes (so send it) or the stream is empty (so send it as well)
                err = tcp_write(m_pcb_client, tx_buf, cnt, TCP_WRITE_FLAG_COPY);
                //printf("sysview_try_send: %d %d\n", cnt, block_call_to_tcp_output);
                if (err != ERR_OK) {
                    picoprobe_error("sysview_try_send/a: %d\n", err);
                    sysview_close(m_pcb_client);
                }
                
                if ( !block_call_to_tcp_output  &&  tcp_sndbuf(m_pcb_client) < 2 * TCP_SND_BUF / 4) {
                    //printf("sysview_try_send: flush %d %d\n", tcp_sndbuf(m_pcb_client), 3 * TCP_SND_BUF / 4);
                    block_call_to_tcp_output = true;
                    tcp_output(m_pcb_client);
                }
                
                if ( !xStreamBufferIsEmpty(stream_sysview_to_host)) {
                    tcpip_callback_with_block(sysview_try_send, NULL, 0);
                }
            }
        }
        else {
//            printf("sysview_try_send: no tcp_sndbuf!!!!\n");
            if ( !block_call_to_tcp_output) {
                //printf("sysview_try_send: flush\n");
                block_call_to_tcp_output = true;
                tcp_output(m_pcb_client);
            }
        }
    }
}   // sysview_try_send



static err_t sysview_sent(void *arg, struct tcp_pcb *tpcb, uint16_t len)
{
    //printf("sysview_sent(%p,%p,%d) %d\n", arg, tpcb, len, m_state);

    block_call_to_tcp_output = false;

    if (m_state == SVS_SEND_HELLO)
    {
        m_state = SVS_READY;
        xStreamBufferReset(stream_sysview_to_host);
    }
    tcpip_callback_with_block(sysview_try_send, NULL, 0);

    m_xmt_cnt = 0;

    return ERR_OK;
}   // sysview_sent



static err_t sysview_recv(void *arg, struct tcp_pcb *tpcb, struct pbuf *p, err_t err)
{
    err_t ret_err;

    // printf("sysview_recv(%p,%p,%p,%d) %d\n", arg, tpcb, p, err, m_state);

    if (p == NULL)
    {
        //
        // remote host closed connection
        //
        sysview_close(tpcb);
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
    else if (m_state == SVS_WAIT_HELLO)
    {
        //
        // expecting hello message
        //
        // printf("sysview_recv, %d:'%s'\n", p->len, (const char *)p->payload);
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
            m_state = SVS_SEND_HELLO;
            ret_err = ERR_OK;
        }
    }
    else if (m_state == SVS_READY)
    {
        //
        // send received data to RTT SysView
        //
        for (uint16_t ndx = 0;  ndx < p->len;  ++ndx)
        {
            rtt_sysview_send_byte(((uint8_t *)p->payload)[ndx]);
        }
        tcp_recved(tpcb, p->len);
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
    //printf("sysview_poll(%p,%p) %d\n", arg, tpcb, m_state);

    //sysview_try_send(NULL);
    tcpip_callback_with_block(sysview_try_send, NULL, 0);
    return ERR_OK;
}   // sysview_poll



static err_t sysview_accept(void *arg, struct tcp_pcb *newpcb, err_t err)
{
    picoprobe_info("=================================== SysView connect\n");

    /* commonly observed practice to call tcp_setprio(), why? */
    tcp_setprio(newpcb, TCP_PRIO_MAX);

    m_state = SVS_WAIT_HELLO;
    m_pcb_client = newpcb;
    /* pass newly allocated svs to our callbacks */
    tcp_arg(newpcb,  NULL);
    tcp_err(newpcb,  sysview_error);
    tcp_recv(newpcb, sysview_recv);
    tcp_poll(newpcb, sysview_poll, 0);
    tcp_sent(newpcb, sysview_sent);

    return ERR_OK;
}   // sysview_accept

// TODO recheck here


bool net_sysview_is_connected(void)
{
    return m_state == SVS_READY;
}   // net_sysview_is_connected



uint32_t net_sysview_send(const uint8_t *buf, uint32_t cnt)
/**
 * Send characters from SysView RTT channel into stream.
 *
 * \param buf  pointer to the buffer to be sent, if NULL then remaining space in stream is returned
 * \param cnt  number of bytes to be sent
 * \return if \buf is NULL the remaining space in stream is returned, otherwise the number of bytes sent
 */
{
    uint32_t r = 0;

#if 0
    if (m_state != SVS_NONE  &&  buf != NULL)
        printf("net_sysview_send(%p,%lu) %d\n", buf, cnt, m_state);
#endif

    if (buf == NULL) {
        r = xStreamBufferSpacesAvailable(stream_sysview_to_host);
    }
    else {
        if ( !net_sysview_is_connected())
        {
            xStreamBufferReset(stream_sysview_to_host);
        }
        else
        {
            r = xStreamBufferSend(stream_sysview_to_host, buf, cnt, pdMS_TO_TICKS(1000));

            if ( !block_call_back_message)
            {
                err_t err;

                block_call_back_message = true;
                err = tcpip_callback_with_block(sysview_try_send, NULL, 0);
                if (err != ERR_OK) {
                    picoprobe_error("net_sysview_send: error %d\n", err);
                    sysview_close(m_pcb_client);
                }
            }
        }
    }

    return r;
}   // net_sysview_send



void net_sysview_init(void)
{
    err_t err;
    struct tcp_pcb *pcb;

    //
    // initialize some infrastructure
    //
    stream_sysview_to_host = xStreamBufferCreate(STREAM_SYSVIEW_SIZE, STREAM_SYSVIEW_TRIGGER);
    if (stream_sysview_to_host == NULL) {
        picoprobe_error("net_sysview_init: cannot create stream_sysview_to_host\n");
    }

    //
    // initialize socket listener
    //
    pcb = tcp_new_ip_type(IPADDR_TYPE_ANY);
    if (pcb == NULL)
    {
        picoprobe_error("net_sysview_init: cannot get pcb\n");
        return;
    }

    err = tcp_bind(pcb, IP_ADDR_ANY, SYSVIEW_COMM_SERVER_PORT);
    if (err != ERR_OK)
    {
        picoprobe_error("net_sysview_init: cannot bind, err:%d\n", err);
        return;
    }

    m_pcb_listen = tcp_listen_with_backlog(pcb, 1);
    if (m_pcb_listen == NULL)
    {
        if (pcb != NULL)
        {
            tcp_close(pcb);
        }
        picoprobe_error("net_sysview_init: cannot listen\n");
        return;
    }

    tcp_accept(m_pcb_listen, sysview_accept);
}   // net_sysview_init
