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

#include <stdio.h>
#include "pico/stdlib.h"
#include <stdlib.h>
#include "stdarg.h"
#include <string.h>
#include "hardware/sync.h"

#include "tusb.h"

#include "picoprobe_config.h"
#include "sigrok_int.h"
#include "cdc_sigrok.h"


// PICO_CONFIG: PICO_STDIO_USB_STDOUT_TIMEOUT_US, Number of microseconds to be blocked trying to write USB
// output before assuming the host has disappeared and discarding data, default=500000, group=pico_stdio_usb
#ifndef PICO_STDIO_USB_STDOUT_TIMEOUT_US
    #define PICO_STDIO_USB_STDOUT_TIMEOUT_US 500000
#endif



//The function stdio_usb_out_chars is part of the PICO sdk usb library.
//However the function is not externally visible from the library and rather than
//figure out the build dependencies to do that it is just copied here.
//This is much faster than a printf of the string, and even faster than the puts_raw
//supported by the PICO SDK stdio library, which doesn't allow the known length of the buffer
//to be specified. (The C standard write function doesn't seem to work at all).
//This function also avoids the inserting of CR/LF in certain modes.
//The tud_cdc_write_available function returns 256, and thus we have a 256B buffer to feed into
//but the CDC serial issues in groups of 64B.
//Since there is another memory fifo inside the TUD code this might possibly be optimized
//to directly write to it, rather than writing txbuf.  That might allow faster rle processing
//but is a bit too complicated.
void cdc_sigrok_write(const char *buf, int length)
{
    static uint64_t last_avail_time;

    if (tud_cdc_connected()) {
        for (int i = 0; i < length;) {
            int n = length - i;
            int avail = (int) tud_cdc_write_available();
            if (n > avail)
                n = avail;
            if (n) {
                int n2 = (int) tud_cdc_write(buf + i, (uint32_t)n);
                tud_task();
                tud_cdc_write_flush();
                i += n2;
                last_avail_time = time_us_64();
            }
            else {
                tud_task();
                tud_cdc_write_flush();
                if (!tud_cdc_connected() ||
                        (!tud_cdc_write_available() && time_us_64() > last_avail_time + PICO_STDIO_USB_STDOUT_TIMEOUT_US)) {
                    break;
                }
            }
        }
    }
    else {
        // reset our timeout
        last_avail_time = 0;
    }
}   // cdc_sigrok_write



//Process incoming character stream
//Return 1 if the device rspstr has a response to send to host
//Be sure that rspstr does not have \n  or \r.
static int process_char(sr_device_t *d, char charin)
{
    int tmpint,tmpint2,ret;

    //set default rspstr for all commands that have a dataless ack
    d->rspstr[0] = '*';
    d->rspstr[1] = 0;
    //the reset character works by itself
    if (charin == '*') {
        sigrok_reset(d);
        Dprintf("RST* %d\n", d->sending);
        return 0;
    }
    else if ((charin == '\r')  ||  (charin == '\n')) {
        d->cmdstr[d->cmdstrptr] = 0;
        switch(d->cmdstr[0]) {
            case 'i':
                //SREGEN,AxxyDzz,00 - num analog, analog size, num digital,version
                sprintf(d->rspstr, "SRPICO,A%02d1D%02d,02", NUM_A_CHAN, NUM_D_CHAN);
                Dprintf("ID rsp %s\n", d->rspstr);
                ret=1;
                break;

            case 'R':
                tmpint=atol(&(d->cmdstr[1]));
                if ((tmpint>=5000)&&(tmpint<=120000016)) { //Add 16 to support cfg_bits
                    d->sample_rate = tmpint;
                    //Dprintf("SMPRATE= %u\n",d->sample_rate);
                    ret = 1;
                }
                else {
                    Dprintf("unsupported smp rate %s\n",d->cmdstr);
                    ret = 0;
                }
                break;

            //sample limit
            case 'L':
                tmpint = atol(&(d->cmdstr[1]));
                if (tmpint > 0) {
                    d->num_samples = tmpint;
                    //Dprintf("NUMSMP=%u\n",d->num_samples);
                    ret = 1;
                }
                else {
                    Dprintf("bad num samples %s\n",d->cmdstr);
                    ret = 0;
                }
                break;

            case 'a':
                tmpint = atoi(&(d->cmdstr[1])); //extract channel number
                if (tmpint >= 0) {
                    //scale and offset are both in integer uVolts
                    //separated by x
                    sprintf(d->rspstr,"25700x0");  //3.3/(2^7) and 0V offset
                    //Dprintf("ASCL%d\n",tmpint);
                    ret = 1;
                }
                else {
                    Dprintf("bad ascale %s\n",d->cmdstr);
                    ret = 1; //this will return a '*' causing the host to fail
                }
                break;

            case 'F': //fixed set of samples
                Dprintf("STRT_FIX\n");
                sigrok_tx_init(d);
                d->cont = 0;
                ret = 0;
                break;

            case 'C':  //continous mode
                sigrok_tx_init(d);
                d->cont = 1;
                Dprintf("STRT_CONT\n");
                ret = 0;
                break;

            case 't': //trigger -format tvxx where v is value and xx is two digit channel
                /*HW trigger depracated
                tmpint=d->cmdstr[1]-'0';
                    tmpint2=atoi(&(d->cmdstr[2])); //extract channel number which starts at D2
                //Dprintf("Trigger input %d val %d\n",tmpint2,tmpint);
                    if((tmpint2>=2)&&(tmpint>=0)&&(tmpint<=4)){
                      d->triggered=false;
                      switch(tmpint){
                    case 0: d->lvl0mask|=1<<(tmpint2-2);break;
                    case 1: d->lvl1mask|=1<<(tmpint2-2);break;
                    case 2: d->risemask|=1<<(tmpint2-2);break;
                    case 3: d->fallmask|=1<<(tmpint2-2);break;
                    default: d->chgmask|=1<<(tmpint2-2);break;
                  }
                      //Dprintf("Trigger channel %d val %d 0x%X\n",tmpint2,tmpint,d->lvl0mask);
                  //Dprintf("LVL0mask 0x%X\n",d->lvl0mask);
                      //Dprintf("LVL1mask 0x%X\n",d->lvl1mask);
                      //Dprintf("risemask 0x%X\n",d->risemask);
                      //Dprintf("fallmask 0x%X\n",d->fallmask);
                      //Dprintf("edgemask 0x%X\n",d->chgmask);
                    }else{
                  Dprintf("bad trigger channel %d val %d\n",tmpint2,tmpint);
                      d->triggered=true;
                    }
                 */
                ret = 1;
                break;

            case 'p': //pretrigger count
                tmpint = atoi(&(d->cmdstr[1]));
                Dprintf("Pre-trigger samples %d cmd %s\n",tmpint,d->cmdstr);
                ret = 1;
                break;

            //format is Axyy where x is 0 for disabled, 1 for enabled and yy is channel #
            case 'A':  ///enable analog channel always a set
                tmpint = d->cmdstr[1] - '0'; //extract enable value
                tmpint2 = atoi(&(d->cmdstr[2])); //extract channel number
                if ((tmpint >= 0)  &&  (tmpint <= 1)  &&  (tmpint2 >= 0)  &&  (tmpint2 <= 31)) {
                    d->a_mask=d->a_mask & ~(1<<tmpint2);
                    d->a_mask=d->a_mask | (tmpint<<tmpint2);
                    //Dprintf("A%d EN %d Msk 0x%X\n",tmpint2,tmpint,d->a_mask);
                    ret = 1;
                }
                else {
                    ret = 0;
                }
                break;

            //format is Dxyy where x is 0 for disabled, 1 for enabled and yy is channel #
            case 'D':  ///enable digital channel always a set
                tmpint = d->cmdstr[1] - '0'; //extract enable value
                tmpint2 = atoi(&(d->cmdstr[2])); //extract channel number
                if ((tmpint >= 0)  &&  (tmpint <= 1)  &&  (tmpint2 >= 0)  &&  (tmpint2 <= 31)) {
                    d->d_mask = d->d_mask & ~(1 << tmpint2);
                    d->d_mask = d->d_mask | (tmpint << tmpint2);
                    //Dprintf("D%d EN %d Msk 0x%X\n",tmpint2,tmpint,d->d_mask);
                    ret = 1;
                }
                else {
                    ret = 0;
                }
                break;

            default:
                Dprintf("bad command %s\n",d->cmdstr);
                ret = 0;
                break;
        }

        //        Dprintf("CmdDone %s\n",d->cmdstr);
        d->cmdstrptr = 0;
    }
    else {
        //no CR/LF
        if (d->cmdstrptr >= sizeof(d->cmdstr) - 1) {
            d->cmdstr[sizeof(d->cmdstr) - 2] = 0;
            Dprintf("Command overflow %s\n",d->cmdstr);
            d->cmdstrptr = 0;
        }
        d->cmdstr[d->cmdstrptr++] = charin;
        ret = 0;
    }//else
    //default return 0 means to not send any kind of response
    return ret;
}   // process_char



//This is a simple maintenance loop to monitor serial activity so that core0 can be dedicated
//to monitoring DMA activity and sending trace data.
//Most of the time this loop is stalled with wfes (wait for events).
// TODO this should become a task
void core1_code()
{
    int intin;

    for (;;) {
        //The wait for event (wfe) puts core1 in an idle state
        //Each core instruction takes a memory cycle, as does each core memory or IO register read.
        //The memory fabric supports up to 4 reads per cycle if there are no bank conflicts
        //The DMA engine needs a read of the PIO and ADC and a write of memory
        //and thus chances of conflicts are high.  Thus once we are in sampling mode slow down C1 to allow
        //C0 to loop faster and process faster.
        //Without wfes C1 typically completes 10x the number of outer loops as C0, but with the wfes
        //C0 completes more loops and C1 activity drops to 1% .
        //Much of the C0 code has built in sev (Send Event) but we also add an explicit one in the main while loop

        if (dev.started) {
            c1cnt++;
            __wfe();
            __wfe();
            __wfe();
            __wfe();
            __wfe();
            __wfe();
            __wfe();
            __wfe();
            __wfe();
            __wfe();
        }

        if ( !dev.started) {
            //always drain all defined uarts as if that is not done it can
            //effect the usb serial CDC stability
            //these are generally rare events caused by noise/reset events
            //and thus not checked when dev.started
            while (uart_is_readable_within_us(uart0, 0)) {
                (void)uart_getc(uart0);
            }
        }
        //look for commands on usb cdc
        intin = getchar_timeout_us(0);
        //The '+' is the only character we track during normal sampling because it can end
        //a continuous trace.  A reset '*' should only be seen after we have completed normally
        //or hit an error condition.
        if (intin == '+') {
            dev.sending = false;
            dev.aborted = false; //clear the abort so we stop sending !!
        }
        //send_resp is used to eliminate all prints from core1 to prevent collisions with
        //prints in core0
        else if (intin >= 0) {
            if (process_char(&dev, (char)intin)) {
                send_resp = true;
            }
        }
    }
}   // core1_code
