//Raspberry Pi PICO/RP2040 code to implement a logic analyzer and oscilloscope

/**
 * Some Original code from the pico examples project:
 * Copyright (c) 2020 Raspberry Pi (Trading) Ltd.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <stdio.h>
#include "pico/stdlib.h"
#include <stdlib.h>
#include "stdarg.h"
#include <string.h>
#include "hardware/pio.h"
#include "hardware/adc.h"
#include "hardware/dma.h"
#include "hardware/structs/bus_ctrl.h"
#include "hardware/clocks.h"

#include "FreeRTOS.h"
#include "task.h"

#include "picoprobe_config.h"
#include "sigrok_int.h"
#include "cdc_sigrok.h"
#include "sigrok.h"


// TODO nur GP12-GP19 verwenden?
// TODO wie geht das heier mit dem Debug-Output?
// TODO TinyUSB-Setup und Handling
// TODO Taskpriorisierung
// TODO Puffergrößen
// TODO Initialisierung der Tasks
// TODO Initialisierung des Systems

//NODMA is a debug mode that disables the DMA engine and prints raw PIO FIFO outputs
//it is limited to testing a small number of samples equal to the PIO FIFO depths
//#define NODMA 1
//These two enable debug print outs of D4 generation, D4_DBG2 are consider higher verbosity
//#define D4_DBG 1
//#define D4_DBG2 2


//The size of the buffer sent to the CDC serial
//The TUD CDC buffer is only 256B so it doesn't help to have more than this.
#define TX_BUF_SIZE 260
//This sets the point which we will send data from the txbuf to the usb cdc.
//For the 5-21 channel RLE it must leave a spare ~83 entries to cover the case where
//a new long steady input comes after deciding to not send a sample.
//(Assuming 128KB samples per half, a max rle value of 1568 we can get
//  256*1024/2/1568=83 max length rles on a steady input).
//Other than that the value is not very specific because the usb tub code
//implement a 256 entry fifo that queues things up and sends max length 64B transactions
//20 is arbitrarly picked to ensure that if we have even a little we send it so that
//at least something goes across the link.
#define TX_BUF_THRESH 20

//
// shared between modules, definition is here
//
sr_device_t       sr_dev;

//
// module private
//
__attribute__ ((aligned(4))) uint8_t capture_buf[SR_DMA_BUF_SIZE];    // 4byte aligned

volatile uint32_t c0cnt=0;
volatile uint32_t tstart;
uint8_t txbuf[TX_BUF_SIZE];
uint16_t txbufidx;                // TODO correct?
uint32_t rxbufdidx;
uint32_t rlecnt;
uint32_t sent_cnt;                            //!< count of characters sent serially
//Number of bytes stored as DMA per slice, must be 1,2 or 4 to support aligned access
//This will be be zero for 1-4 digital channels.
uint8_t d_dma_bps;
uint32_t samp_remain;
uint32_t lval,cval; //last and current digital sample values
uint32_t num_halves; //track the number of halves we have processed

volatile uint32_t *tstsa0, *tstsa1, *tstsd0, *tstsd1;
volatile uint32_t *taddra0, *taddra1, *taddrd0, *taddrd1;
volatile int lowerhalf; //are we processing the upper or lower half of the data buffers
volatile bool mask_xfer_err;

static TaskHandle_t task_sigrok;



//This is an optimized transmit of trace data for configurations with 4 or fewer digital channels
//and no analog.  Run length encoding (RLE) is used to send counts of repeated values to effeciently utilize
//USB CDC link bandwidth.  This is the only mode where a given serial byte can have both sample information
//and RLE counts.
//Samples from PIO are dma'd in 32 bit words, each containing 8 samples of 4 bits (1 nibble).
//RLE Encoding:
//Values 0x80-0xFF encode an rle cnt of a previous value with a new value:
//  Bit 7 is 1 to distinguish from the rle only values.
//  Bits 6:4 indicate a run length up to 7 cycles of the previous value
//  Bits 3:0 are the new value.
//For longer runs, an RLE only encoding uses decimal values 48 to 127 (0x30 to 0x7F)
//as x8 run length values of 8..640.
//All other ascii values (except from the abort and the end of run byte_cnt) are reserved.
static void send_slices_D4(sr_device_t *d,uint8_t *dbuf)
{
    uint8_t nibcurr,niblast;
    uint32_t cword,lword; //current and last word
    uint32_t *cptr;

    txbufidx=0;
    //Don't optimize the first word (eight samples) perfectly, just send them to make the for loop easier,
    //and setup the initial conditions for rle tracking
    cptr=(uint32_t *) &(dbuf[0]);
    cword=*cptr;
#ifdef D4_DBG
    Dprintf("Dbuf %p cptr %p data 0x%X\n",(void *)&(dbuf[0]),(void *) cptr,cword);
#endif
    lword = cword;
    for (int j = 0;  j < 8;  j++) {
        nibcurr = cword&0xF;
        txbuf[j] = nibcurr | 0x80;
        cword >>= 4;
    }
    niblast=nibcurr;
    cptr=(uint32_t *) &(txbuf[0]);
    txbufidx = 8;
    rxbufdidx = 4;
    rlecnt = 0;

    if (d->samples_per_half <= 8) {
        cdc_sigrok_write((char *)txbuf, txbufidx);
        d->scnt += d->samples_per_half;
        return;
    }
    //chngcnt=8;
    //The total number of 4 bit samples remaining to process from this half.
    //Subtract 8 because we procesed the word above.
    samp_remain = d->samples_per_half-8;

    //If in fixed sample (non-continous mode) only send the amount of samples requested
    if ( !d->continuous  &&  d->scnt + samp_remain > d->num_samples) {
        samp_remain = d->num_samples-d->scnt;
        d->scnt += samp_remain;
    }
    else {
        d->scnt += d->samples_per_half;
    }
    //Process one  word (8 samples) at a time.
    for (int i = 0;  i < (samp_remain >> 3);  i++) {
        cptr = (uint32_t *) &(dbuf[rxbufdidx]);
        cword = *cptr;
        rxbufdidx += 4;
#ifdef D4_DBG2
        Dprintf("dbuf0 %p dbufr %p cptr %p\n",dbuf,&(dbuf[rxbufdidx]),cptr);
#endif
        //Send maximal RLE counts in this outer section to the txbuf, and if we accumulate a few of them
        //push to the device so that we don't accumulate large numbers
        //of unsent RLEs.  That allows the host to process them gradually rather than in a flood
        //when we get a value change.
        while (rlecnt >= 640) {
            txbuf[txbufidx++] = 127;
            rlecnt -= 640;
            if (txbufidx > 3){
                cdc_sigrok_write((char *)txbuf, txbufidx);
                sent_cnt += txbufidx;
                txbufidx = 0;
            }
        }
        //Coarse rle looks across the full word and allows a faster compare in cases with low activity factors
        //We must make sure cword==lword and that all nibbles of cword are the same
        if (cword == lword  &&  (cword >> 4) == (cword & 0x0FFFFFFF)) {
            rlecnt += 8;
#ifdef D4_DBG2
            Dprintf("coarse word 0x%X\n",cword);
#endif
        }
        else {//if coarse rle didn't match
#ifdef D4_DBG2
            Dprintf("cword 0x%X nibcurr 0x%X i %d rx idx %u  rlecnt %u\n",cword,nibcurr,i,rxbufdidx,rlecnt);
#endif
            lword = cword;
            for (int j = 0;  j < 8;  j++) { //process all 8 nibbles
                nibcurr = cword & 0xF;
                if (nibcurr == niblast) {
                    rlecnt++;
                }
                else{
                    //If the value changes we must push all remaing rles to the txbuf
                    //chngcnt++;
                    //Send intermediate 8..632 RLEs
                    if (rlecnt > 7) {
                        int rlemid = rlecnt & 0x3F8;
                        txbuf[txbufidx++] = (rlemid >> 3) + 47;
                    }
                    //And finally the 0..7 rle along with the new value
                    rlecnt &= 0x7;
#ifdef D4_DBG2 //print when sample value changes
                    Dprintf("VChang val 0x%X rlecnt %d i%d j%d\n",nibcurr,rlecnt,i,j);
#endif
                    txbuf[txbufidx++] = 0x80 | nibcurr | (rlecnt << 4);
                    rlecnt = 0;
                }//nibcurr!=last
                cword >>= 4;
                niblast = nibcurr;
            }
        }
#ifdef D4_DBG2
        Dprintf("i %d rx idx %u  rlecnt %u\n",i,rxbufdidx,rlecnt);
        Dprintf("i %u tx idx %d bufs 0x%X 0x%X 0x%X\n",i,txbufidx,txbuf[txbufidx-3],txbuf[txbufidx-2],txbuf[txbufidx-1]);
#endif
        //Emperically found that transmitting groups of around 32B gives optimum bandwidth
        if (txbufidx >= 64) {
            cdc_sigrok_write((char *)txbuf, txbufidx);
            sent_cnt += txbufidx;
            txbufidx = 0;
        }
    }//for i in samp_send>>3
    //At the end of processing the half send any residual samples as we don't maintain state between the halves
    //Maximal 640 values first
    while (rlecnt >= 640) {
        txbuf[txbufidx++] = 127;
        rlecnt -= 640;
    }
    //Middle rles 8..632
    if (rlecnt > 7) {
        int rleend = rlecnt & 0x3F8;
        txbuf[txbufidx++] = (rleend >> 3) + 47;
    }
    //1..7 RLE
    //The rle and value encoding counts as both a sample count of rle and a new sample
    //thus we must decrement rlecnt by 1 and resend the current value which will match the previous values
    //(if the current value didn't match, the rlecnt would be 0).
    if (rlecnt) {
        rlecnt &= 0x7;
        rlecnt--;
        txbuf[txbufidx++] = 0x80 | nibcurr | (rlecnt << 4);
        rlecnt = 0;
    }
    if (txbufidx) {
        cdc_sigrok_write((char *)txbuf, txbufidx);
        sent_cnt += txbufidx;
        txbufidx = 0;
    }
}   // send_slices_D4



//Send a digital sample of multiple bytes with the 7 bit encoding
static void tx_d_samp(sr_device_t *d,uint32_t cval)
{
    for (uint32_t b = 0;  b < d->d_tx_bps;  b++) {
        txbuf[txbufidx++] = cval | 0x80;
        cval >>= 7;
    }
}   // tx_d_samp



//Allow for 1,2 or 4B reads of sample data to reduce memory read overhead when
//parsing digital sample data.  This function is correct for all uses, but if included
//the compiled code is substantially slower to the point that digital only transfers
//can't keep up with USB rate.  Thus it is only used by the send_slices_analog which is already
//limited to 500khz, and in the starting send_slice_init.
static uint32_t  get_cval(uint8_t *dbuf)
{
    uint32_t cval;

    if (d_dma_bps == 1) {
        cval = dbuf[rxbufdidx];
    }
    else if (d_dma_bps == 2) {
        cval = (*((uint16_t *) (dbuf+rxbufdidx)));
    }
    else {
        cval =(*((uint32_t *) (dbuf+rxbufdidx)));
        //To make a 32bit value written from PIO we pull in IOs that aren't actual
        //digital channels so mask them off
        cval &= SR_PIO_D_MASK;
    }
    rxbufdidx += d_dma_bps;
    return cval;
}   // get_cval



/*RLE encoding for 5-21 channels has two ranges.
Decimal 48 to  79 are RLEs of 1 to 32 respectively.
Decimal 80 to 127 are (N-78)*32 thus 64,96..80,120..1568
Note that it is the responsibility of the caller to
forward txbuf bytes to USB to prevent txbufidx from overflowing the size
of txbuf. We do not always push to USB to reduce its impact
on performance.
 */
static void check_rle(void)
{
    while (rlecnt >= 1568) {
        txbuf[txbufidx++] = 127;
        rlecnt -= 1568;
    }
    if (rlecnt > 32) {
        uint16_t rlediv = rlecnt >> 5;
        txbuf[txbufidx++] = rlediv + 78;//was 86;
        rlecnt -= rlediv << 5;
    }
    if (rlecnt) {
        txbuf[txbufidx++] = 47 + rlecnt;
        rlecnt = 0;
    }
}   // check_rle



//Send txbuf to usb based on an input threshold
static void check_tx_buf(uint16_t cnt)
{
    if (txbufidx >= cnt) {
        cdc_sigrok_write((char *)txbuf, txbufidx);
        sent_cnt += txbufidx;
        txbufidx = 0;
    }
}   // check_tx_buf



//Common init for send_slices_1B/2B/4B, but not D4 or analog
static void send_slice_init(sr_device_t *d,uint8_t *dbuf)
{
    rxbufdidx = 0;
    //Adjust the number of samples to send if there are more in the dma buffer
    //then we need.
    samp_remain = d->samples_per_half;
    if ( !d->continuous  &&  d->scnt + samp_remain > d->num_samples) {
        samp_remain=d->num_samples-d->scnt;
        d->scnt += samp_remain;
    }
    else {
        d->scnt += d->samples_per_half;
    }
    txbufidx = 0;
    //Always send the first sample to establish a previous value for RLE
    //the use of get_cval is inefficient, but only done once per half buffer
    lval = get_cval(dbuf);
    //If we are in 4B mode shift off invalid bits
    lval &= SR_PIO_D_MASK;
    tx_d_samp(d, lval);
    samp_remain--;
    rlecnt = 0;
}   // send_slice_init



//There are three very similar functions send_slices_1B/2B/4B.
//Each of which  is very similar but exist because if a common function
//is used with the get_cval in the inner loop, the performance drops
//substantially.  Thus each function has a 1,2, or 4B aligned read respectively.
//We can just always read a 4B value because the core doesn't support non-aligned accesses.
//These must be marked noinline to ensure they remain separate functions for good performance
//1B is 5-8 channels
static void __attribute__ ((noinline)) send_slices_1B(sr_device_t *d,uint8_t *dbuf)
{
    send_slice_init(d, dbuf);
    for (int s = 0;  s < samp_remain;  s++) {
        cval = dbuf[rxbufdidx++];
        if (cval == lval) {
            rlecnt++;
        }
        else {
            check_rle();
            tx_d_samp(d,cval);
            check_tx_buf(TX_BUF_THRESH);
        }
        lval = cval;
    }
    check_rle();
    check_tx_buf(1);
}   // send_slices_1B



//2B is 9-16 channels
static void __attribute__ ((noinline)) send_slices_2B(sr_device_t *d,uint8_t *dbuf)
{
    send_slice_init(d,dbuf);
    for (int s = 0;  s < samp_remain;  s++) {
        cval=(*((uint16_t *) (dbuf+rxbufdidx)));
        rxbufdidx += 2;
        if (cval == lval) {
            rlecnt++;
        }
        else {
            check_rle();
            tx_d_samp(d, cval);
            check_tx_buf(TX_BUF_THRESH);
        }
        lval = cval;
    }
    check_rle();
    check_tx_buf(1);
}   // send_slices_2B



//4B is 17-21 channels and is the only one that must mask invalid bits which are captured by DMA
static void __attribute__ ((noinline)) send_slices_4B(sr_device_t *d,uint8_t *dbuf)
{
    send_slice_init(d, dbuf);
    for (int s = 0;  s < samp_remain;  s++) {
        cval = (*((uint32_t *) (dbuf+rxbufdidx)));
        rxbufdidx += 4;
        //Mask invalid bits
        cval &= SR_PIO_D_MASK;
        if (cval == lval) {
            rlecnt++;
        }
        else {
            check_rle();
            tx_d_samp(d, cval);
            check_tx_buf(TX_BUF_THRESH);
        }
        lval = cval;
    }
    check_rle();
    check_tx_buf(1);
}   // send_slices_4B



//Slice transmit code, used for all cases with any analog channels
//All digital channels for one slice are sent first in 7 bit bytes using values 0x80 to 0xFF
//Analog channels are sent next, with each channel taking one 7 bit byte using values 0x80 to 0xFF.
//This does not support run length encoding because it's not clear how to define RLE on analog signals
static void send_slices_analog(sr_device_t *d,uint8_t *dbuf,uint8_t *abuf)
{
    uint32_t rxbufaidx=0;

    rxbufdidx = 0;
    samp_remain = d->samples_per_half;
    if ( !d->continuous  &&  d->scnt + samp_remain > d->num_samples) {
        samp_remain = d->num_samples - d->scnt;
        d->scnt += samp_remain;
    }
    else {
        d->scnt += d->samples_per_half;
    }
    txbufidx = 0;
    for (int s = 0;  s < samp_remain;  s++) {
        if (d->d_mask) {
            cval = get_cval(dbuf);
            tx_d_samp(d, cval);
            Dprintf("s %d cv %lX bps %d idx t %d r %ld\n", s, cval, d_dma_bps, txbufidx, rxbufdidx);
        }
        for (uint32_t i = 0;  i < d->a_chan_cnt;  i++) {
            txbuf[txbufidx] = (abuf[rxbufaidx] >> 1) | 0x80;
            txbufidx++;
            rxbufaidx++;
            Dprintf("av %X cnt %d idx t %d r %ld\n", abuf[rxbufaidx-1], d->a_chan_cnt, txbufidx, rxbufaidx);
        }
        //Since this doesn't support RLEs we don't need to buffer
        //extra bytes to prevent txbuf overflow, but this value
        //works well anyway
        check_tx_buf(TX_BUF_THRESH);
    }
    check_tx_buf(1);
}   // send_slices_analog



//See if a given half's dma engines are idle and if so process the data, update the write pointer and
//ensure that when done the other dma is still busy indicating we didn't lose data .
//int __not_in_flash_func(check_half)(sr_device_t *d,volatile uint32_t *tstsa0,volatile uint32_t *tstsa1,volatile uint32_t *tstsd0,
static int check_half(sr_device_t *d, volatile uint32_t *tstsa0, volatile uint32_t *tstsa1, volatile uint32_t *tstsd0,
                      volatile uint32_t *tstsd1, volatile uint32_t *t_addra0, volatile uint32_t *t_addrd0,
                      uint8_t *d_start_addr, uint8_t *a_start_addr, bool mask_xfer_err)
{
    bool a0busy, d0busy;

    a0busy = (*tstsa0 & DMA_CH0_CTRL_TRIG_BUSY_BITS) != 0;
    d0busy = (*tstsd0 & DMA_CH0_CTRL_TRIG_BUSY_BITS) != 0;

    if(     ( !a0busy  ||  d->a_mask == 0)
        &&  ( !d0busy  ||  d->d_mask == 0)) {
        //Use two dma controllers where each writes half of the trace space.
        //When one half finishes we send it's data while the other dma controller writes to the other half.
        //We rely on DMA chaining where one completing dma engine triggers the next.
        //When chaining happens the original target address is not restored so we must rewrite the starting address.
        //Also when chaining is enabled we can get into an infinite loop where we ping pong between each other.
        //and it is possible that if the other DMA controller finishes before we have send our half of the data that
        //our DMA controller will startup and overwrite our data.  That is the DMA overflow condition.
        //We don't actually overflow the DMA, but instead we fail to establish proper chaining to keep the
        //ping pong going.
        //The only way to avoid the overflow condition is to reduce the sampling rate so that the transmit of samples
        //can keep up, or do a fixed sample that fits into the sample buffer.
        //When we first enter the loop it is assumed that the other controller is not chained to us so that if we don't
        //process in time then our DMA controller won't be started by the other.  That assumption is maintained by
        //always pointing our DMA engine to itself so tht in the next function call it is the "other" controller.
        //We then process our half's samples and send to USB.
        //When done we confirm that the other channel is still active, and if so we know we haven't overflowed and thus
        //can establish the chain from the other channel to us.  If the other channel is not still active, or if the
        //PIO/ADC FIFOs indicates an RXstall condition which indicates PIO lost samples, then we abort.
        //Note that in all cases we should never actually send any corrupted data we just send less than what was requested.
        //Note that we must use the "alias" versions of the DMA CSRs to prevent writes from triggering them.
        //Since we swap the csr pointers we determine the other half from the address offsets.
        uint8_t myachan = ((tstsa0[0] & DMA_CH0_CTRL_TRIG_RING_SIZE_BITS) >> DMA_CH0_CTRL_TRIG_RING_SIZE_LSB);
        uint8_t mydchan = ((tstsd0[0] & DMA_CH0_CTRL_TRIG_RING_SIZE_BITS) >> DMA_CH0_CTRL_TRIG_RING_SIZE_LSB);
        Dprintf("my stts pre a 0x%lX d 0x%lX\n", *tstsa0, *tstsd0);
        //Set my chain to myself so that I can't chain to the other.
        uint32_t ttmp;
        ttmp = ((tstsd0[1]) & ~DMA_CH0_CTRL_TRIG_CHAIN_TO_BITS) | (mydchan << DMA_CH0_CTRL_TRIG_CHAIN_TO_LSB);
        tstsd0[1] = ttmp;
        ttmp = ((tstsa0[1]) & ~DMA_CH0_CTRL_TRIG_CHAIN_TO_BITS) | (myachan << DMA_CH0_CTRL_TRIG_CHAIN_TO_LSB);
        tstsa0[1] = ttmp;
        *t_addra0 = (uint32_t)a_start_addr;
        *t_addrd0 = (uint32_t)d_start_addr;

        bool piorxstall1 = (d->d_mask != 0  &&  (SIGROK_PIO->fdebug & 0x1) != 0);    // TODO fixed to SM0

        if (d->a_mask != 0) {
            send_slices_analog(d, d_start_addr, a_start_addr);
        }
        else if (d_dma_bps == 0) {
            send_slices_D4(d, d_start_addr);
        }
        else if (d_dma_bps == 1) {
            send_slices_1B(d, d_start_addr);
        }
        else if (d_dma_bps == 2) {
            send_slices_2B(d, d_start_addr);
        }
        else {
            send_slices_4B(d, d_start_addr);
        }

        if ( !d->continuous  &&  d->scnt >= d->num_samples) {
            d->sample_and_send = false;
        }

        //Set my other chain to me
        //use aliases here as well to prevent triggers
        ttmp = (tstsd1[1] & ~DMA_CH0_CTRL_TRIG_CHAIN_TO_BITS) | (mydchan << DMA_CH0_CTRL_TRIG_CHAIN_TO_LSB);
        tstsd1[1] = ttmp;
        ttmp = (tstsa1[1] & ~DMA_CH0_CTRL_TRIG_CHAIN_TO_BITS) | (myachan << DMA_CH0_CTRL_TRIG_CHAIN_TO_LSB);
        tstsa1[1] = ttmp;
        num_halves++;

        bool piorxstall2 = (d->d_mask != 0  &&  (SIGROK_PIO->fdebug & 0x1) != 0);    // TODO fixed to SM0

        volatile uint32_t *adcfcs;
        adcfcs = (volatile uint32_t *)(ADC_BASE + ADC_FCS_OFFSET);
        bool adcfail = (d->a_mask != 0  &&  (*adcfcs & (ADC_FCS_OVER_BITS | ADC_FCS_UNDER_BITS)) != 0);
        if (adcfail) {
            Dprintf("adcfcs 0x%lX %p\n", *adcfcs, (void *)adcfcs);
        }

        //Ensure other dma is still busy, if not that means we have samples from PIO/ADC that could be lost.
        //It's only an error if we haven't already gotten the samples we need, or if we are processing the first
        //half and all the remaining samples we need are in the 2nd half.
        //Note that in continuous mode num_samples isn't defined.
        bool proc_fail =    (d->a_mask != 0  &&  (*tstsa1 & DMA_CH0_CTRL_TRIG_BUSY_BITS) == 0)
                         || (d->d_mask != 0  &&  (*tstsd1 & DMA_CH0_CTRL_TRIG_BUSY_BITS) == 0);
        Dprintf("pf 0x%lX 0x%lX %d\n", *tstsa1, *tstsd1, proc_fail);
        //       if(mask_xfer_err
        //     || ((piorxstall1==0)
        //      &&((((*tstsa1)>>24)&1)||(d->a_mask==0))
        //         &&((((*tstsd1)>>24)&1)||(d->d_mask==0)))){
        if (mask_xfer_err  ||  (!piorxstall1  &&  !adcfail  &&  !piorxstall2  &&  !proc_fail)) {
            Dprintf("h\n");
            return 1;
        }
        else {
            if (piorxstall1  ||  piorxstall2) {
                Dprintf("***Abort PIO RXSTALL*** %d %d half %lu\n", piorxstall1, piorxstall2, num_halves);
            }
            if (proc_fail) {
                Dprintf("***Abort DMA ovrflow*** half %lu\n", num_halves);
            }
            if (adcfail) {
                Dprintf("***Abort ADC ovrflow*** half %lu\n", num_halves);
            }
            d->aborted = true;
            //Issue end of trace markers to host
            //The main loop also sends these periodically until the host is done..
            cdc_sigrok_write("!!!", 3);
            Dprintf("scnt %lu\n", d->scnt);
            Dprintf("a st %lu msk %lu\n", *tstsa1, d->a_mask);
            Dprintf("d st %lu msk %lu\n", *tstsd1, d->d_mask);
            return -1;
        }
    }
    return 0;
}   // check_half



//Check if dma activity is complete.  This was split out to allow core1 to do the monitoring
//and slice processing and leave usb interrupt handling to core 0, but doing so did not improve
//streaming performance, so it's left in core0.
static void dma_check(sr_device_t *d)
{
//    Dprintf("dma_check: %d %d %lu %lu %d\n", d->sample_and_send, d->all_started, d->scnt, d->num_samples, d->continuous);
    if (d->sample_and_send  &&  d->all_started  &&  (d->scnt < d->num_samples  ||  d->continuous)) {
        int ret;

        c0cnt++;
        if (lowerhalf) {
            ret = check_half(&sr_dev, tstsa0, tstsa1, tstsd0, tstsd1, taddra0, taddrd0,
                             &(capture_buf[d->dbuf0_start]), &(capture_buf[d->abuf0_start]), mask_xfer_err);
            if (ret == 1) {
                lowerhalf = 0;
            }
            else if (ret < 0) {
                d->sample_and_send = false;
            }
        }
        if (lowerhalf == 0) {
            ret = check_half(&sr_dev, tstsa1, tstsa0, tstsd1, tstsd0, taddra1, taddrd1,
                             &(capture_buf[d->dbuf1_start]), &(capture_buf[d->abuf1_start]), mask_xfer_err);
            if (ret == 1) {
                lowerhalf = 1;
            }
            else if (ret < 0) {
                d->sample_and_send = false;
            }
        }
    }
}   // dma_check



/**
 * Main task of sigrok.
 */
static void sigrok_thread(void *ptr)
{
    dma_channel_config acfg0, acfg1, pcfg0, pcfg1;
    uint admachan0, admachan1, pdmachan0, pdmachan1;

    vTaskDelay(pdMS_TO_TICKS(100));
    Dprintf("-------------------- Hello from PICO sigrok\n");

    uint32_t f_pll_sys = frequency_count_khz(CLOCKS_FC0_SRC_VALUE_PLL_SYS_CLKSRC_PRIMARY);
    Dprintf("pll_sys = %lukHz\n", f_pll_sys);
    uint32_t f_clk_sys = frequency_count_khz(CLOCKS_FC0_SRC_VALUE_CLK_SYS);
    Dprintf("clk_sys = %lukHz\n", f_clk_sys);
    uint32_t f_clk_adc = frequency_count_khz(CLOCKS_FC0_SRC_VALUE_CLK_ADC);
    Dprintf("clk_adc = %lukHz\n", f_clk_adc);

    // TODO what is this?
#if 0
    //Set GPIO23 (TP4) to control switched mode power supply noise
    gpio_init_mask(1 << 23);
    gpio_set_dir_masked(1 << 23, 1 << 23);
    gpio_put_masked(1 << 23, 1 << 23);
    //Early CDC IO code had lots of sleep statements, but the TUD code seems to have sufficient
    //checks that this isn't needed, but it doesn't hurt...
    vTaskDelay(pdMS_TO_TICKS(100));
#endif

    //GPIOs 26 through 28 (the ADC ports) are on the PICO, GPIO29 is not a pin on the PICO
    adc_gpio_init(26);
    adc_gpio_init(27);
    adc_gpio_init(28);
    adc_init();

    admachan0 = dma_claim_unused_channel(true);
    admachan1 = dma_claim_unused_channel(true);
    pdmachan0 = dma_claim_unused_channel(true);
    pdmachan1 = dma_claim_unused_channel(true);
    acfg0 = dma_channel_get_default_config(admachan0);
    acfg1 = dma_channel_get_default_config(admachan1);
    pcfg0 = dma_channel_get_default_config(pdmachan0);
    pcfg1 = dma_channel_get_default_config(pdmachan1);

    //ADC transfer 8 bytes, PIO transfer the 4B default
    channel_config_set_transfer_data_size(&acfg0, DMA_SIZE_8);
    channel_config_set_transfer_data_size(&acfg1, DMA_SIZE_8);
    channel_config_set_transfer_data_size(&pcfg0, DMA_SIZE_32);
    channel_config_set_transfer_data_size(&pcfg1, DMA_SIZE_32);
    channel_config_set_read_increment(&acfg0, false);
    channel_config_set_read_increment(&acfg1, false);
    channel_config_set_read_increment(&pcfg0, false);
    channel_config_set_read_increment(&pcfg1, false);
    channel_config_set_write_increment(&acfg0, true);
    channel_config_set_write_increment(&acfg1, true);
    channel_config_set_write_increment(&pcfg0, true);
    channel_config_set_write_increment(&pcfg1, true);

    // Pace transfers based on availability of ADC samples
    channel_config_set_dreq(&acfg0, DREQ_ADC);
    channel_config_set_dreq(&acfg1, DREQ_ADC);
    volatile uint32_t *tcounta0, *tcounta1, *tcountd0, *tcountd1;
    tcounta0 = (volatile uint32_t *)(DMA_BASE + 0x40 * admachan0 + DMA_CH0_TRANS_COUNT_OFFSET);
    tcounta1 = (volatile uint32_t *)(DMA_BASE + 0x40 * admachan1 + DMA_CH0_TRANS_COUNT_OFFSET);
    tcountd0 = (volatile uint32_t *)(DMA_BASE + 0x40 * pdmachan0 + DMA_CH0_TRANS_COUNT_OFFSET);
    tcountd1 = (volatile uint32_t *)(DMA_BASE + 0x40 * pdmachan1 + DMA_CH0_TRANS_COUNT_OFFSET);

    //Use the debug version of the count registers because the count of the DMA_TRANS_COUNT is not
    //visible if it is not active
    volatile uint32_t *tcountdbga0, *tcountdbgd0;
    tcountdbga0 = (volatile uint32_t *)(DMA_BASE + 0x40 * admachan0 + DMA_CH0_DBG_TCR_OFFSET); //DMA_TRANS_COUNT DBGoffset
    tcountdbgd0 = (volatile uint32_t *)(DMA_BASE + 0x40 * pdmachan0 + DMA_CH0_DBG_TCR_OFFSET); //DMA_TRANS_COUNT DBGoffset

    taddra0 = (volatile uint32_t *)(DMA_BASE + 0x40 * admachan0 + DMA_CH0_WRITE_ADDR_OFFSET);
    taddra1 = (volatile uint32_t *)(DMA_BASE + 0x40 * admachan1 + DMA_CH0_WRITE_ADDR_OFFSET);
    taddrd0 = (volatile uint32_t *)(DMA_BASE + 0x40 * pdmachan0 + DMA_CH0_WRITE_ADDR_OFFSET);
    taddrd1 = (volatile uint32_t *)(DMA_BASE + 0x40 * pdmachan1 + DMA_CH0_WRITE_ADDR_OFFSET);

    tstsa0 = (volatile uint32_t *)(DMA_BASE + 0x40 * admachan0 + DMA_CH0_CTRL_TRIG_OFFSET);
    tstsa1 = (volatile uint32_t *)(DMA_BASE + 0x40 * admachan1 + DMA_CH0_CTRL_TRIG_OFFSET);
    tstsd0 = (volatile uint32_t *)(DMA_BASE + 0x40 * pdmachan0 + DMA_CH0_CTRL_TRIG_OFFSET);
    tstsd1 = (volatile uint32_t *)(DMA_BASE + 0x40 * pdmachan1 + DMA_CH0_CTRL_TRIG_OFFSET);

    //Give High priority to DMA to ensure we don't overflow the PIO or DMA fifos
    //The DMA controller must read across the common bus to read the PIO fifo so enabled both reads and write
    bus_ctrl_hw->priority = BUSCTRL_BUS_PRIORITY_DMA_W_BITS | BUSCTRL_BUS_PRIORITY_DMA_R_BITS;

    sigrok_full_reset(&sr_dev);

    Dprintf("DMA start %p\n",(void *)capture_buf);

    //This testmode forces the device into a capture state without needing
    //sigrok cli/pulseview to initiate it
    //All of the needed fields may not be up to date...
    /*
   dev.started=false;
   dev.cont=false;
   dev.sending=true;
   dev.a_chan_cnt=0; //count of enabled analog channels
   dev.a_mask=0x0;
   dev.d_mask=0xF;
   dev.d_nps=1; //digital nibbles per slice
   dev.sample_rate=1000;
   dev.num_samples=5000;
   dev.scnt=0; //number of samples sent
   sent_cnt=0;

     */

    gpio_init_mask(SR_GPIO_D_MASK);            // set as GPIO_FUNC_SIO and clear output enable
    gpio_set_dir_masked(SR_GPIO_D_MASK, 0);    // set all to input

    for (;;) {
//        Dprintf("ss %d %d\n", sr_dev.sample_and_send, sr_dev.all_started);
        if (sr_dev.sample_and_send  ||  sr_dev.all_started) {
            // no delay
//            vTaskDelay(1);
        }
        else {
            vTaskDelay(pdMS_TO_TICKS(10));      // TODO otherwise we will consume the whole CPU
        }

        if (sr_dev.send_resp) {
            //
            // send command response to host
            //
            cdc_sigrok_write(sr_dev.rspstr, strnlen(sr_dev.rspstr, sizeof(sr_dev.rspstr)));
            sr_dev.send_resp = false;
        }

        if (sr_dev.sample_and_send  &&  !sr_dev.all_started) {
            //
            // initialize sampling and transmission
            // - reset several counters
            // - initialize hardware
            //

            //Only boost frequency during a sample so that average device power is less.
            //It's not clear that this is needed because rp2040 is pretty low power, but it can't hurt...
            lowerhalf = 1;

            //Sample rate must always be even.  Pulseview code enforces this
            //because a frequency step of 2 is required to get a pulldown to specify
            //the sample rate, but sigrok cli can still pass it.
            sr_dev.sample_rate &= 0xfffffffe;

            //Adjust up and align to 4 to avoid rounding errors etc
            if (sr_dev.num_samples < 16) {
                sr_dev.num_samples = 16;
            }
            sr_dev.num_samples = (sr_dev.num_samples + 3) & 0xFFFFFFFC;

            //Divide capture buf evenly based on channel enables
            //d_size is aligned to 4 bytes because pio operates on words
            //These are the sizes for each half buffer in bytes
            //Calculate relative size in terms of nibbles which is the smallest unit, thus a_chan_cnt is multiplied by 2
            //Nibble size storage is only allow for D4 mode with no analog channels enabled
            //For instance a D0..D5 with A0 would give 1/2 the storage to digital and 1/2 to analog
            uint32_t d_nibbles, a_nibbles, t_nibbles; //digital, analog and total nibbles
            d_nibbles = sr_dev.d_nps;  //digital is in grous of 4 bits
            a_nibbles = sr_dev.a_chan_cnt * 2; //1 byte per sample
            t_nibbles = d_nibbles + a_nibbles;

            //total buf size must be a multiple of a_nibbles*2, d_nibbles*8, and t_nibbles so that division is always
            //in whole samples
            //Also set a multiple of 32  because the dma buffer is split in half, and
            //the PIO does writes on 4B boundaries, and then a 4x factor for any other size/alignment issues
            uint32_t chunk_size = t_nibbles * 32;
            if (a_nibbles != 0)
                chunk_size *= a_nibbles;
            if (d_nibbles != 0)
                chunk_size *= d_nibbles;
            uint32_t dig_bytes_per_chunk = chunk_size*d_nibbles / t_nibbles;
            uint32_t dig_samples_per_chunk=(d_nibbles) ? dig_bytes_per_chunk*2/d_nibbles : 0;
            uint32_t chunk_samples=d_nibbles ? dig_samples_per_chunk  : (chunk_size*2)/(a_nibbles);
            //total chunks in entire buffer-round to 2 since we split it in half
            uint32_t buff_chunks = (SR_DMA_BUF_SIZE / chunk_size) & 0xFFFFFFFE;
            //round up and force power of two since we cut it in half
            uint32_t chunks_needed = ((sr_dev.num_samples / chunk_samples) + 2) & 0xFFFFFFFE;
            Dprintf("Initial buf calcs nibbles d %lu a %lu t %lu\n", d_nibbles, a_nibbles, t_nibbles);
            Dprintf("chunk size %lu samples %lu buff %lu needed %lu\n", chunk_size, chunk_samples, buff_chunks, chunks_needed);
            Dprintf("dbytes per chunk %lu dig samples per chunk %lu\n", dig_bytes_per_chunk, dig_samples_per_chunk);
            //If all of the samples we need fit in two half buffers or less then we can mask the error
            //logic that is looking for cases where we didn't send one half buffer to the host before
            //the 2nd buffer ended because we only use each half buffer once.
            mask_xfer_err = false;
            //If requested samples are smaller than the buffer, reduce the size so that the
            //transfer completes sooner.
            //Also, mask the sending of aborts if the requested number of samples fit into RAM
            //Don't do this in continuous mode as the final size is unknown
            if ( !sr_dev.continuous) {
                if (buff_chunks > chunks_needed) {
                    mask_xfer_err = true;
                    buff_chunks = chunks_needed;
                    Dprintf("Reduce buf chunks to %ld\n", buff_chunks);
                }
            }
            //Give dig and analog equal fractions
            //This is the size of each half buffer in bytes
            sr_dev.d_size = (buff_chunks * chunk_size * d_nibbles) / (t_nibbles * 2);
            sr_dev.a_size = (buff_chunks * chunk_size * a_nibbles) / (t_nibbles * 2);
            sr_dev.samples_per_half = chunk_samples * buff_chunks / 2;
            Dprintf("Final sizes d %ld a %ld mask err %d samples per half %ld\n",
                    sr_dev.d_size, sr_dev.a_size, mask_xfer_err, sr_dev.samples_per_half);

            //Clear any previous ADC over/underflow
            volatile uint32_t *adcfcs;
            adcfcs = (volatile uint32_t *)(ADC_BASE + ADC_FCS_OFFSET);
            *adcfcs |= (ADC_FCS_OVER_BITS | ADC_FCS_UNDER_BITS);

            //Ensure any previous dma is done
            //The cleanup loop also does this but it doesn't hurt to do it twice
            dma_channel_abort(admachan0);
            dma_channel_abort(admachan1);
            dma_channel_abort(pdmachan0);
            dma_channel_abort(pdmachan1);
            //Enable the initial chaing from the first half to 2nd, further chains are enabled based
            //on whether we can parse each half in time.
            channel_config_set_chain_to(&acfg0, admachan1);
            channel_config_set_chain_to(&pcfg0, pdmachan1);

            num_halves = 0;
            sr_dev.dbuf0_start = 0;
            sent_cnt = 0;
            sr_dev.dbuf1_start = sr_dev.d_size;
            sr_dev.abuf0_start = sr_dev.dbuf1_start + sr_dev.d_size;
            sr_dev.abuf1_start = sr_dev.abuf0_start + sr_dev.a_size;

            if (sr_dev.a_chan_cnt != 0) {
                adc_run(false);
                //             en, dreq_en,dreq_thresh,err_in_fifo,byte_shift to 8 bit
                adc_fifo_setup(false, true,   1,           false,       true);
                adc_fifo_drain();

                //This sdk function doesn't support support the fractional divisor
                // adc_set_clkdiv((float)(adcdivint-1));
                //The ADC divisor has some not well documented limitations.
                //-A value of 0 actually creates a 500khz sample clock.
                //-Values below 96 don't work well (the SDK has comments about it
                //in the adc_set_clkdiv document)
                //It is also import to subtract one from the desired divisor
                //because the period of ADC clock is 1+INT+FRAC/256
                //For the case of a requested 500khz clock, we would normally write
                //a divisor of 95, but doesn't give the desired result, so we use
                //the 0 value instead.
                //Fractional divisors should generally be avoided because it creates
                //skew with digital samples.
                volatile uint32_t *adcdiv = (volatile uint32_t *)(ADC_BASE + ADC_DIV_OFFSET);
                const uint32_t sample_rate_khz = (sr_dev.a_chan_cnt * sr_dev.sample_rate) / 1000;     // sample_rate is a multiple of 1000

                Dprintf("starting d_nps %u a_chan_cnt %u d_size %lu a_size %lu a_mask %lX\n",
                        sr_dev.d_nps, sr_dev.a_chan_cnt, sr_dev.d_size, sr_dev.a_size, sr_dev.a_mask);
                Dprintf("start offsets d0 0x%lX d1 0x%lX a0 0x%lX a1 0x%lX samperhalf %lu\n",
                        sr_dev.dbuf0_start, sr_dev.dbuf1_start, sr_dev.abuf0_start, sr_dev.abuf1_start, sr_dev.samples_per_half);
                Dprintf("starting data buf values 0x%X 0x%X\n", capture_buf[sr_dev.dbuf0_start], capture_buf[sr_dev.dbuf1_start]);

                uint32_t div_256 = (256 * f_clk_adc + sample_rate_khz / 2) / sample_rate_khz;
                uint32_t div_int  = div_256 >> 8;
                uint32_t div_frac = div_256 & 0xff;
                if (div_int == 0) {
                    div_int  = 1;
                    div_frac = 0;
                }
                else if (div_int > 0xffff) {
                    div_int = 0xffff;
                    div_frac = 0xff;
                }
                *adcdiv = ((div_int - 1) << 8) | div_frac;
                Dprintf("adcdiv %lu = %lu.%lu\n", *adcdiv, div_int, div_frac);

                //This is needed to clear the AINSEL so that when the round robin arbiter starts we start sampling on channel 0
                adc_select_input(0);
                adc_set_round_robin(sr_dev.a_mask & 0x7);
                //             en, dreq_en,dreq_thresh,err_in_fifo,byte_shift to 8 bit
                adc_fifo_setup(true, true,   1,           false,       true);

                //set chan0 to immediate trigger (but without adc_run it shouldn't start), chan1 is chained to it.
                // channel, config, write_addr,read_addr,transfer_count,trigger)
                dma_channel_configure(admachan0, &acfg0, &(capture_buf[sr_dev.abuf0_start]), &adc_hw->fifo, sr_dev.a_size, true);
                dma_channel_configure(admachan1, &acfg1, &(capture_buf[sr_dev.abuf1_start]), &adc_hw->fifo, sr_dev.a_size, false);
                adc_fifo_drain();
            }

            if (sr_dev.d_mask != 0) {
                //analyzer_init from pico-examples
                //Due to how PIO shifts in bits, if any digital channel within a group of 8 is set,
                //then all groups below it must also be set. We further restrict it in the tx_init function
                //by saying digital channel usage must be continous.
                /* pin count is restricted to 4,8,16 or 32, and pin count of 4 is only used
                Pin count is kept to a powers of 2 so that we always read a sample with a single byte/word/dword read
                for faster parsing.
                   if analog is disabled and we are in D4 mode
                    bits d_dma_bps   d_tx_bps
                    0-4    0          1        No analog channels
                    0-4    1          1        1 or more analog channels
                    5-7    1          1
                    8      1          2
                    9-12   2          2
                    13-14  2          2
                    15-16  2          3
                    17-21  4          3
                 */
                sr_dev.pin_count = 0;
                if (sr_dev.d_mask & 0x0000000F)
                    sr_dev.pin_count += 4;
                if (sr_dev.d_mask & 0x000000F0)
                    sr_dev.pin_count += 4;
                if (sr_dev.d_mask & 0x0000FF00)
                    sr_dev.pin_count += 8;
                if (sr_dev.d_mask & 0x0FFF0000)
                    sr_dev.pin_count += 16;
                //If 4 or less channels are enabled but ADC is also enabled, set a minimum size of 1B of PIO storage
                if (sr_dev.pin_count == 4  &&  sr_dev.a_chan_cnt) {
                    sr_dev.pin_count = 8;
                }
                d_dma_bps = sr_dev.pin_count >> 3;
                Dprintf("pin_count %d\n", sr_dev.pin_count);
                uint16_t capture_prog_instr;
                capture_prog_instr = pio_encode_in(pio_pins, sr_dev.pin_count);
                Dprintf("capture_prog_instr 0x%X\n",capture_prog_instr);
                struct pio_program capture_prog = {
                        .instructions = &capture_prog_instr,
                        .length = 1,
                        .origin = -1
                };
                uint offset = pio_add_program(SIGROK_PIO, &capture_prog);
                // Configure state machine to loop over this `in` instruction forever,
                // with autopush enabled.
                pio_sm_config c = pio_get_default_sm_config();
                //start at SR_BASE_D_CHAN
                sm_config_set_in_pins(&c, SR_BASE_D_CHAN);
                sm_config_set_wrap(&c, offset, offset);

                const uint32_t sample_rate_khz = sr_dev.sample_rate / 1000;                         // sample_rate is a multiple of 1000
                uint32_t div_256 = (256 * f_clk_sys + sample_rate_khz / 2) / sample_rate_khz;
                uint32_t div_int  = div_256 >> 8;
                uint32_t div_frac = div_256 & 0xff;
                if (div_int == 0) {
                    div_int  = 1;
                    div_frac = 0;
                }
                else if (div_int > 0xffff) {
                    div_int = 0xffff;
                    div_frac = 0xff;
                }
                Dprintf("PIO sample clk %lukHz / %lu.%lu = %lukHz\n", f_clk_sys, div_int, div_frac, sample_rate_khz);
                sm_config_set_clkdiv_int_frac(&c, div_int, div_frac);

                //Since we enable digital channels in groups of 4, we always get 32 bit words
                sm_config_set_in_shift(&c, true, true, 32);
                sm_config_set_fifo_join(&c, PIO_FIFO_JOIN_RX);
                pio_sm_init(SIGROK_PIO, SIGROK_SM, offset, &c);
                //Analyzer arm from pico examples
                pio_sm_set_enabled(SIGROK_PIO, SIGROK_SM, false); //clear the enabled bit
                //XOR the shiftctrl field with PIO_SM0_SHIFTCTRL_FJOIN_RX_BITS
                //Do it twice to restore the value
                pio_sm_clear_fifos(SIGROK_PIO, SIGROK_SM);
                //write the restart bit of PIO_CTRL
                pio_sm_restart(SIGROK_PIO, SIGROK_SM);

#ifndef NODMA
                channel_config_set_dreq(&pcfg0, pio_get_dreq(SIGROK_PIO,SIGROK_SM,false));
                channel_config_set_dreq(&pcfg1, pio_get_dreq(SIGROK_PIO,SIGROK_SM,false));

                //                       number    config   buffer target                  piosm          xfer size  trigger
                dma_channel_configure(pdmachan0, &pcfg0, &(capture_buf[sr_dev.dbuf0_start]), &SIGROK_PIO->rxf[SIGROK_SM], sr_dev.d_size >> 2, true);
                dma_channel_configure(pdmachan1, &pcfg1, &(capture_buf[sr_dev.dbuf1_start]), &SIGROK_PIO->rxf[SIGROK_SM], sr_dev.d_size >> 2, false);
#endif

                //This is done later so that we start everything as close in time as possible
                //             pio_sm_set_enabled(SIGROK_PIO, SIGROK_SM, true);
            }

            //These must be at their initial value,(or zero for the 2ndhalf) otherwise it indicates they have started to countdown
            Dprintf("Tcount start d %lu %lu a %lu %lu\n", *tcountd0, *tcountd1, *tcounta0, *tcounta1);
            //These must be the initial value for both
//            Dprintf("Tcount dbg start d 0x%X 0x%X a 0x%X 0x%X\n", *tcountdbgd0, *tcountdbgd1, *tcountdbga0, *tcountdbga1);
            //These catch cases in DMA coding where DMA engines have started too soon..

            if (*tcountd0 != *tcountdbgd0  &&  sr_dev.d_mask != 0) {
                Dprintf("\n\nERROR: DMAD0 changing\n\n");
            }
            if (*tcounta0 != *tcountdbga0  &&  sr_dev.a_mask != 0) {
                Dprintf("\n\nERROR: DMAA0 changing\n\n");
            }
            if (*tcountd1 != 0) {
                Dprintf("\n\nERROR: DMAD1 should start with 0 tcount\n\n");
            }
            if (*tcounta1 != 0) {
                Dprintf("\n\nERROR: DMAA1 should start with 0 tcount\n\n");
            }

//            Dprintf("LVL0mask 0x%X\n", sr_dev.lvl0mask);
//            Dprintf("LVL1mask 0x%X\n", sr_dev.lvl1mask);
//            Dprintf("risemask 0x%X\n", sr_dev.risemask);
//            Dprintf("fallmask 0x%X\n", sr_dev.fallmask);
//            Dprintf("edgemask 0x%X\n", sr_dev.chgmask);

            Dprintf("dma addr start d 0x%lX 0x%lX a 0x%lX 0x%lX\n", *taddrd0, *taddrd1, *taddra0, *taddra1);
            Dprintf("capture_buf base %p\n",capture_buf);
            Dprintf("capture_buf dig %p %p\n",&(capture_buf[sr_dev.dbuf0_start]),&(capture_buf[sr_dev.dbuf1_start]));
            Dprintf("capture_buf analog %p %p\n",&(capture_buf[sr_dev.abuf0_start]),&(capture_buf[sr_dev.abuf1_start]));

            Dprintf("DMA channel assignments a %d %d d %d %d\n",admachan0,admachan1,pdmachan0,pdmachan1);
            Dprintf("DMA ctr reg addrs a %p %p d %p %p\n",(void *) tstsa0,(void *)tstsa1,(void *)tstsd0,(void *)tstsd1);
            Dprintf("DMA ctrl reg a 0x%lX 0x%lX d 0x%lX 0x%lX\n", *tstsa0, *tstsa1, *tstsd0, *tstsd1);
            //Enable logic and analog close together for best possible alignment
            //warning - do not put printfs or similar things here...
            tstart = time_us_32();
            adc_run(true); //enable free run sample mode
            pio_sm_set_enabled(SIGROK_PIO, SIGROK_SM, true);
            sr_dev.all_started = true;
        }

        dma_check(&sr_dev);

        //In high verbosity modes the host can miss the "!" so send these until it sends a "+"
        if (sr_dev.aborted) {
            Dprintf("sending abort !\n");
            cdc_sigrok_write("!!!", 3);
            vTaskDelay(pdMS_TO_TICKS(200));
        }

        //if we abort or normally finish a run sending gets dropped
        if ( !sr_dev.sample_and_send  &&  sr_dev.all_started) {
            //The end of sequence byte_cnt uses a "$<byte_cnt>+" format.
            //Send the byte_cnt to ensure no bytes were lost
            if ( !sr_dev.aborted) {
                char brsp[16];
                int n;

                //Give the host time to finish processing samples so that the bytecnt
                //isn't dropped on the wire
                vTaskDelay(pdMS_TO_TICKS(100));
                Dprintf("Cleanup bytecnt %lu\n", sent_cnt);
                n = snprintf(brsp, sizeof(brsp), "$%lu+", sent_cnt);
                cdc_sigrok_write(brsp, n);
            }

#ifdef NODMA
            // if dma is disabled and sample sizes are small this can be used
            // to pull the raw sample data from the pio fifos.
            uint lvl;
            lvl=pio_sm_get_rx_fifo_level(pio,SIGROK_SM);
            Dprintf("FIFOlvl 0x%X\n",lvl);
            uint32_t fval;
            for (int x = 0;  x < lvl;  x++) {
                Dprintf("RX FIFO x %d:0x%X\n",x,pio_sm_get_blocking(pio,SIGROK_SM));
            }

#endif
            adc_run(false);
            adc_fifo_drain();
            pio_sm_restart(SIGROK_PIO, SIGROK_SM);
            pio_sm_set_enabled(SIGROK_PIO, SIGROK_SM, false);
            pio_sm_clear_fifos(SIGROK_PIO, SIGROK_SM);
            pio_clear_instruction_memory(SIGROK_PIO);

            dma_channel_abort(admachan0);
            dma_channel_abort(admachan1);
            dma_channel_abort(pdmachan0);
            dma_channel_abort(pdmachan1);
            sr_dev.all_started = false;

            //Print USB Endpoint controls in the DPSRAM, which is at the base of USBCTRL
            //0x0 is setup packet,
            //0x8-0xfc - EP in/out buffer control
            //0x100 - EP0 buffer 0 (in and out)
            //0x140 - EPO buffer 1
            //0x180 - data buffers
            /*
             volatile uint32_t *usbctrl;
             usbctrl=(volatile uint32_t *)(USBCTRL_BASE);
             for(i=0;i<256;i+=4){ //It's a 4k space, but everything above this is zero
               Dprintf("0x%03X %8X %8X %8X %8X\n",i*4,usbctrl[i],usbctrl[i+1],usbctrl[i+2],usbctrl[i+3]);
         }
             */

            //Print out debug information after completing, rather than before so that it doesn't
            //delay the start of a capture
            Dprintf("Complete: SRate %lu NSmp %lu\n", sr_dev.sample_rate, sr_dev.num_samples);
            Dprintf("Cont %d bcnt %lu\n", sr_dev.continuous, sent_cnt);
            Dprintf("DMsk 0x%lX AMsk 0x%lX\n", sr_dev.d_mask, sr_dev.a_mask);
            Dprintf("Half buffers %lu sampperhalf %lu\n", num_halves, sr_dev.samples_per_half);

            //Report the number of main loops for each core to ensure
            //C0 runs more loops
            Dprintf("loop count C0 %lu\n", c0cnt);
            c0cnt = 0;
        }
    }
}   // sigrok_thread



//Depracated trigger logic
//This HW based trigger which should be part of send slices was tested enough to confirm the
//trigger value worked, however it
//was not fully implemented because the RP2040 wasn't able to perform the trigger detection and
//memory buffer management to support sample rates that were substantially higher than the
//stream rates across USB.  Thus there wasn't a compelling reason to have it.
//It's left as an example as to how the masks could be used.
//  To fully support a HW based triggering, a precapture ring buffer of both digital and analog samples
//would need to be created an managed to store and send pretrigger values.
//The ring buffer would need to support RLEs and would need to ensure it was sent before sending
//other samples capture by the DMA after the trigger event.
/*
//   uint32_t tlval; Trigger last val
//   tlval=d->tlval;
//   uint32_t all_mask=d->lvl0mask | d->lvl1mask| d->risemask | d->fallmask | d->chgmask;


       if(d->triggered==false) {
         uint32_t matches=0;
         matches|=(~cval & d->lvl0mask);
         matches|=(cval & d->lvl1mask);
         if(d->notfirst){
           matches|=(cval & ~tlval & d->risemask);
           matches|=(~cval & tlval & d->fallmask);
           matches|=(~cval & tlval & d->chgmask);
         }
         if(matches==all_mask){
       //Dprintf("Triggered c 0x%X l 0x%X\n",cval,tlval);
           d->triggered=true;
           //This sends the last val on a trigger because SW based trigger on the host needs to see its
           //value so that rising/falling/edge triggeers will fire there too.
           lbyte=0;
           for(char b=0;b < d->d_tx_bps;b++){
              cbyte=tlval&0xFF;
              txbuf[txbufidx]=(cbyte<<b)|lbyte|0x80;
              lbyte=cbyte>>(7-b);
              tlval>>=8;
          txbufidx++;
           } //for b
         }//matches==all_mask
         d->notfirst=true;
       }
       if(d->triggered){
             //Transmit samples if we have already triggered.
        }

//save trigger last value to support rising/falling/change values
//      tlval=lval;
End of depracated trigger logic
*/



void sigrok_init(uint32_t task_prio)
{
    xTaskCreate(sigrok_thread, "CDC_SIGROK", configMINIMAL_STACK_SIZE, NULL, task_prio, &task_sigrok);

    cdc_sigrok_init(task_prio);
}   // sigrok_init
