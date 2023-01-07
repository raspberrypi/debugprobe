//Pin usage
//GP0 and 1 are reserved for debug uart
//GP2-GP22 are digital inputs
//GP23 controls power supply modes and is not a board input
//GP24-25 are not on the board and not used
//GP26-28 are ADC.
//number of analog channels
#define NUM_A_CHAN 3
//number of digital channels
#define NUM_D_CHAN 21
//Mask of bits 22:2 to use as inputs -
#define GPIO_D_MASK 0x7FFFFC
//Storage size of the DMA buffer.  The buffer is split into two halves so that when the first
//buffer fills we can send the trace data serially while the other buffer is DMA'dinto
#define DMA_BUF_SIZE 220000
//The size of the buffer sent to the CDC serial
//The TUD CDC buffer is only 256B so it doesn't help to have more than this.
#define TX_BUF_SIZE 260
#define UART_BAUD 921600
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
//Base value of sys_clk in khz.  Must be <=125Mhz per RP2040 spec and a multiple of 24Mhz
//to support integer divisors of the PIO clock and ADC clock
#define SYS_CLK_BASE 120000
//Boosted sys_clk in khz.  Runs the part above its specifed frequency limit to support faster
//processing of digital run length encoding which in some cases may allow for faster
//streaming of digital only data.
//****************************
//Use this at your own risk
//***************************
//Frequency must be a 24Mhz multiple and less than 300Mhz to avoid known issues
//The authors PICO failed at 288Mhz, but testing with 240Mhz seemed reliable
//#define SYS_CLK_BOOST_EN 1
//#define SYS_CLK_BOOST_FREQ 240000
int Dprintf(const char *fmt, ...)
{

    va_list argptr;
    int len = 1;
    char    _dstr[256];


     memset(&_dstr, 0x00, sizeof(_dstr) );
    va_start(argptr, fmt);
    len = vsprintf(_dstr, fmt, argptr);
    va_end(argptr);

        if ( (len > 0) && (len < 240 ) )
        {

    uart_puts(uart0,_dstr);
    uart_tx_wait_blocking(uart0);
        }
         else{
         uart_puts(uart0,"UART OVRFLW");
        uart_tx_wait_blocking(uart0);

             }
    return len;

}
typedef struct sr_device {
  uint32_t sample_rate;
  uint32_t num_samples;
  uint32_t a_mask,d_mask;
  uint32_t samples_per_half; //number of samples for one of the 4 dma target arrays
  uint8_t a_chan_cnt; //count of enabled analog channels
  uint8_t d_chan_cnt; //count of enabled digital channels
  uint8_t d_tx_bps;   //Digital Transmit bytes per slice
  //Pins sampled by the PIO - 4,8,16 or 32
  uint8_t pin_count;
  uint8_t d_nps; //digital nibbles per slice from a PIO/DMA perspective.
  uint32_t scnt; //number of samples sent
  char cmdstrptr;
  char cmdstr[20];//used for parsing input
  uint32_t d_size,a_size; //size of each of the two data buffers for each of a& d
  uint32_t dbuf0_start,dbuf1_start,abuf0_start,abuf1_start; //starting memory pointers of adc buffers
  char rspstr[20];
//mark key control variables voltatile since multiple cores might access them
  volatile bool started;
  volatile bool sending;
  volatile bool cont;
  volatile bool aborted;
  /*Depracated trigger logic
//If HW trigger enabled, uncomment all usages
  //volatile bool notfirst;  //Have we processed at least a first sample (so that lval is correct
  //  volatile bool triggered;
  //  uint32_t tlval; //last digital sample value - must keep it across multiple calls to send_slices for trigger
  //  uint32_t lvl0mask,lvl1mask,risemask,fallmask,chgmask;
  End depracated trigger logic*/

  } sr_device_t;


//reset as part of init, or on a completed send
void reset(sr_device_t *d){
    d->sending=0;
    d->cont=0;
    d->aborted=false;
    d->started=false;
    d->scnt=0;
    //d->notfirst=false;
    //Set triggered by default so that we don't check for HW triggers
    //unless the driver sends a trigger command
    //d->triggered=true;
    //d->tlval=0;
    //d->lvl0mask=0;
    //d->lvl1mask=0;
    //d->risemask=0;
    //d->fallmask=0;
    //d->chgmask=0;
};
//initial post reset state
void init(sr_device_t *d){
    reset(d);
    d->a_mask=0;
    d->d_mask=0;
    d->sample_rate=5000;
    d->num_samples=10;
    d->a_chan_cnt=0;
    d->d_nps=0;
    d->cmdstrptr=0;
}
void tx_init(sr_device_t *d){
  //A reset should have already been called to restart the device.
  //An additional one here would clear trigger and other state that had been updated
  //    reset(d);
    d->a_chan_cnt=0;
    for(int i=0;i<NUM_A_CHAN;i++){
       if(((d->a_mask)>>i)&1){d->a_chan_cnt++;}
    }
    //Nibbles per slice controls how PIO digital data is stored
    //Only support 0,1,2,4 or 8, which use 0,4,8,16 or 32 bits of PIO fifo data
    //per sample clock.
    d->d_nps=(d->d_mask&       0xF) ? 1 : 0;
    d->d_nps=(d->d_mask&      0xF0) ? (d->d_nps)+1 : d->d_nps;
    d->d_nps=(d->d_mask&    0xFF00) ? (d->d_nps)+2 : d->d_nps;
    d->d_nps=(d->d_mask&0xFFFF0000) ? (d->d_nps)+4 : d->d_nps;
    //Dealing with samples on a per nibble, rather than per byte basis in non D4 mode
    //creates a bunch of annoying special cases, so forcing non D4 mode to always store a minimum
    //of 8 bits.
    if((d->d_nps==1)&&(d->a_chan_cnt>0)){d->d_nps=2;}

    //Digital channels must enable from D0 and go up, but that is checked by the host
    d->d_chan_cnt=0;
    for(int i=0;i<NUM_D_CHAN;i++){
       if(((d->d_mask)>>i)&1){
     //    Dprintf("i %d inv %d mask %X\n\r",i,invld,d->d_mask);
          d->d_chan_cnt++;
       }
    }
    d->d_tx_bps=(d->d_chan_cnt+6)/7;
    d->sending=true;

}
//Process incoming character stream
//Return 1 if the device rspstr has a response to send to host
//Be sure that rspstr does not have \n  or \r.
int process_char(sr_device_t *d,char charin){
   int tmpint,tmpint2,ret;
   //set default rspstr for all commands that have a dataless ack
   d->rspstr[0]='*';
   d->rspstr[1]=0;
   //the reset character works by itself
  if(charin=='*'){
     reset(d);
     Dprintf("RST* %d\n\r",d->sending);
     return 0;
  }else if((charin=='\r')||(charin=='\n')){
    d->cmdstr[d->cmdstrptr]=0;
    switch(d->cmdstr[0]){
    case 'i':
       //SREGEN,AxxyDzz,00 - num analog, analog size, num digital,version
       sprintf(d->rspstr,"SRPICO,A%02d1D%02d,02",NUM_A_CHAN,NUM_D_CHAN);
       Dprintf("ID rsp %s\n\r",d->rspstr);
       ret=1;
       break;
     case 'R':
       tmpint=atol(&(d->cmdstr[1]));
       if((tmpint>=5000)&&(tmpint<=120000016)){ //Add 16 to support cfg_bits
          d->sample_rate=tmpint;
          //Dprintf("SMPRATE= %u\n\r",d->sample_rate);
          ret=1;
       }
       else {
          Dprintf("unsupported smp rate %s\n\r",d->cmdstr);
          ret=0;
       }
       break;
     //sample limit
     case 'L':
       tmpint=atol(&(d->cmdstr[1]));
       if(tmpint>0){
          d->num_samples=tmpint;
          //Dprintf("NUMSMP=%u\n\r",d->num_samples);
          ret=1;
       }else{
          Dprintf("bad num samples %s\n\r",d->cmdstr);
          ret=0;
       }
       break;
     case 'a':
         tmpint=atoi(&(d->cmdstr[1])); //extract channel number
         if(tmpint>=0){
       //scale and offset are both in integer uVolts
           //separated by x
            sprintf(d->rspstr,"25700x0");  //3.3/(2^7) and 0V offset
            //Dprintf("ASCL%d\n\r",tmpint);
            ret=1;
         }else{
            Dprintf("bad ascale %s\n\r",d->cmdstr);
            ret=1; //this will return a '*' causing the host to fail
          }
           break;
      case 'F': //fixed set of samples
       Dprintf("STRT_FIX\n\r");
           tx_init(d);
           d->cont=0;
           ret=0;
           break;
      case 'C':  //continous mode
            tx_init(d);
            d->cont=1;
            Dprintf("STRT_CONT\n\r");
            ret=0;
            break;
      case 't': //trigger -format tvxx where v is value and xx is two digit channel
    /*HW trigger depracated
        tmpint=d->cmdstr[1]-'0';
            tmpint2=atoi(&(d->cmdstr[2])); //extract channel number which starts at D2
        //Dprintf("Trigger input %d val %d\n\r",tmpint2,tmpint);
            if((tmpint2>=2)&&(tmpint>=0)&&(tmpint<=4)){
              d->triggered=false;
              switch(tmpint){
            case 0: d->lvl0mask|=1<<(tmpint2-2);break;
            case 1: d->lvl1mask|=1<<(tmpint2-2);break;
            case 2: d->risemask|=1<<(tmpint2-2);break;
            case 3: d->fallmask|=1<<(tmpint2-2);break;
            default: d->chgmask|=1<<(tmpint2-2);break;
          }
              //Dprintf("Trigger channel %d val %d 0x%X\n\r",tmpint2,tmpint,d->lvl0mask);
          //Dprintf("LVL0mask 0x%X\n\r",d->lvl0mask);
              //Dprintf("LVL1mask 0x%X\n\r",d->lvl1mask);
              //Dprintf("risemask 0x%X\n\r",d->risemask);
              //Dprintf("fallmask 0x%X\n\r",d->fallmask);
              //Dprintf("edgemask 0x%X\n\r",d->chgmask);
            }else{
          Dprintf("bad trigger channel %d val %d\n\r",tmpint2,tmpint);
              d->triggered=true;
            }
    */
            ret=1;
            break;
      case 'p': //pretrigger count
            tmpint=atoi(&(d->cmdstr[1]));
            Dprintf("Pre-trigger samples %d cmd %s\n\r",tmpint,d->cmdstr);
            ret=1;
            break;
      //format is Axyy where x is 0 for disabled, 1 for enabled and yy is channel #
      case 'A':  ///enable analog channel always a set
            tmpint=d->cmdstr[1]-'0'; //extract enable value
            tmpint2=atoi(&(d->cmdstr[2])); //extract channel number
            if((tmpint>=0)&&(tmpint<=1)&&(tmpint2>=0)&&(tmpint2<=31)){
               d->a_mask=d->a_mask & ~(1<<tmpint2);
               d->a_mask=d->a_mask | (tmpint<<tmpint2);
               //Dprintf("A%d EN %d Msk 0x%X\n\r",tmpint2,tmpint,d->a_mask);
               ret=1;
            }else{
               ret=0;
            }
            break;
      //format is Dxyy where x is 0 for disabled, 1 for enabled and yy is channel #
       case 'D':  ///enable digital channel always a set
            tmpint=d->cmdstr[1]-'0'; //extract enable value
            tmpint2=atoi(&(d->cmdstr[2])); //extract channel number
            if((tmpint>=0)&&(tmpint<=1)&&(tmpint2>=0)&&(tmpint2<=31)){
               d->d_mask=d->d_mask & ~(1<<tmpint2);
               d->d_mask=d->d_mask | (tmpint<<tmpint2);
               //Dprintf("D%d EN %d Msk 0x%X\n\r",tmpint2,tmpint,d->d_mask);
               ret=1;
             }else{
                ret=0;
             }
             break;
        default:
             Dprintf("bad command %s\n\r",d->cmdstr);
             ret=0;
        }//case
//        Dprintf("CmdDone %s\n\r",d->cmdstr);
        d->cmdstrptr=0;
      }else{ //no CR/LF
         if(d->cmdstrptr>=19){
            d->cmdstr[18]=0;
            Dprintf("Command overflow %s\n\r",d->cmdstr);
            d->cmdstrptr=0;
          }
          d->cmdstr[d->cmdstrptr++]=charin;
          ret=0;
       }//else
//default return 0 means to not send any kind of response
   return ret;
}//process_char
