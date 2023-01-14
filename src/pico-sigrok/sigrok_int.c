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


#include <stdint.h>
#include <stdbool.h>

#include "sigrok_int.h"



void sigrok_tx_init(sr_device_t *d)
{
    //A reset should have already been called to restart the device.
    //An additional one here would clear trigger and other state that had been updated
    //    reset(d);
    d->a_chan_cnt = 0;
    for (int i = 0;  i < SR_NUM_A_CHAN;  i++) {
        if (((d->a_mask) >> i) & 1) {
            d->a_chan_cnt++;
        }
    }
    //Nibbles per slice controls how PIO digital data is stored
    //Only support 0,1,2,4 or 8, which use 0,4,8,16 or 32 bits of PIO fifo data
    //per sample clock.
    d->d_nps = (d->d_mask &        0xF) ? 1 : 0;
    d->d_nps = (d->d_mask &       0xF0) ? (d->d_nps) + 1 : d->d_nps;
    d->d_nps = (d->d_mask &     0xFF00) ? (d->d_nps) + 2 : d->d_nps;
    d->d_nps = (d->d_mask & 0xFFFF0000) ? (d->d_nps) + 4 : d->d_nps;
    //Dealing with samples on a per nibble, rather than per byte basis in non D4 mode
    //creates a bunch of annoying special cases, so forcing non D4 mode to always store a minimum
    //of 8 bits.
    if (d->d_nps == 1  &&  d->a_chan_cnt > 0) {
        d->d_nps = 2;
    }

    //Digital channels must enable from D0 and go up, but that is checked by the host
    d->d_chan_cnt = 0;
    for (int i = 0;  i < SR_NUM_D_CHAN;  i++) {
        if (((d->d_mask) >> i) & 1) {
            //    Dprintf("i %d inv %d mask %X\n",i,invld,d->d_mask);
            d->d_chan_cnt++;
        }
    }
    d->d_tx_bps = (d->d_chan_cnt + 6) / 7;

    d->sample_and_send = true;
}   // sigrok_tx_init



// reset as part of init, or on a completed send
void sigrok_reset(sr_device_t *d)
{
    d->sample_and_send = false;
    d->continuous      = false;
    d->aborted         = false;
    d->all_started     = false;
    d->scnt            = 0;
}   // sigrok_reset



//initial post reset state
void sigrok_full_reset(sr_device_t *d)
{
    sigrok_reset(d);
    d->a_mask      = 0;
    d->d_mask      = 0;
    d->d_mask_D4   = 0;
    d->sample_rate = 5000;
    d->num_samples = 10;
    d->a_chan_cnt  = 0;
    d->d_chan_cnt  = 0;
    d->d_nps       = 0;
    d->cmdstr_ndx  = 0;
}   // sigrok_full_reset
