#include <mbed.h>
#include <stdio.h>
#include <wave_player.h>
#include "MODDMA.h"
#include "SDFileSystem.h"

#define BUFFER_SIZE 2879

DigitalOut led1(LED1);
DigitalOut led3(LED3);
DigitalOut led4(LED4);

AnalogOut signal(p18);

SDFileSystem    sd(p5, p6, p7, p8, "sd"); //PinName mosi, PinName miso, PinName sclk, PinName cs, const char* name

MODDMA dma;
//MODDMA_Config *conf0, *conf1;
MODDMA_Config *conf0;

void TC0_callback(void);
void ERR0_callback(void);

//void TC1_callback(void);
//void ERR1_callback(void);

unsigned chunk_id,chunk_size,channel;
unsigned data,samp_int,i;
short unsigned dac_data;
long long slice_value;
char *slice_buf;
short *data_sptr;
unsigned char *data_bptr;
int *data_wptr;
FMT_STRUCT wav_format;
long slice,num_slices;
unsigned short DAC_fifo[BUFFER_SIZE];
short DAC_wptr;
int read_slices = 0;
int DMA_complete = 0;
int file_end = 0;

FILE *wav_file4 = fopen("/sd/wf/bd_04.wav", "r"); //16 bit 22.1 khz

/*
Reads wave file header info, and sets a flag when it gets to the slice data
*/
void read_wav_file(FILE *& wavefile) 
{
  fread(&chunk_id,4,1,wavefile); //get chunk id
  fread(&chunk_size,4,1,wavefile); //get chunk size
  //printf("chunk type 0x%x, size %d\n",chunk_id,chunk_size);
  if (!feof(wavefile)) //sequentially reading the wave file seperating it into different chunk_ids
  { 
    switch (chunk_id) 
    {
      case 0x46464952: //RIFF CHUNK
        fread(&data,4,1,wavefile); 
        break;
      case 0x20746d66: //WAV FORMAT DATA
        fread(&wav_format,sizeof(wav_format),1,wavefile); //get wave format info
        //printf("wav_format - %d\n", wav_format.block_align);
        if (chunk_size > sizeof(wav_format))
          fseek(wavefile,chunk_size-sizeof(wav_format),SEEK_CUR);
        break;
      case 0x61746164: //AUDIO DATA
        read_slices = 1; //set read_slices flag, start reading slices 
        num_slices=chunk_size/wav_format.block_align; //atleast 16 bits / 16 bits
        //printf("num_slices = %d / %d = %d \n", chunk_size, wav_format.block_align, chunk_size / wav_format.block_align); 
        break;
      case 0x5453494c: //INFO CHUNK
        fseek(wavefile,chunk_size,SEEK_CUR);
        break;
      default: //UNKNOWN CHUNK
        //printf("unknown chunk type 0x%x, size %d\n",chunk_id,chunk_size);
        data=fseek(wavefile,chunk_size,SEEK_CUR);
        break;
    }
    file_end = 0;
  } else if (feof(wavefile)) 
  {
    file_end = 1;
    
  }
}

/*
Reads slice by slice, averaging channels to mono. Saves to the DAC_fifo[slicesRead]
*/
void read_and_avg_slices(FILE *& wavefile, short DAC_wptr)
{
  if (!feof(wavefile)) //sequentially reading the wave file seperating it into different chunk_ids
  { 
    //allocate slice buffer big enough to hold a slice
    slice_buf=(char *)malloc(wav_format.block_align); //set a 16 bit slice buffer (block_aligis type short)  
    if (!slice_buf) 
    {
      printf("Unable to malloc slice buffer");
      exit(1);
    }     
    //samp_int=1000000/(wav_format.sample_rate
    fread(slice_buf,wav_format.block_align,1,wavefile);
    if (feof(wavefile)) 
    {
      printf("Oops -- not enough slices in the wave file\n");
      exit(1);
    }
    data_sptr=(short *)slice_buf;     // 16 bit samples
    slice_value=0;
    for (channel=0;channel<wav_format.num_channels;channel++) 
    {
      switch (wav_format.sig_bps) 
      {
        case 16:
            //printf("16 bit channel %d data=%d ",channel,data_sptr[channel]);
            slice_value+=data_sptr[channel];
            break;
      }
    }
    slice_value/=wav_format.num_channels; // summed and averaged
    //slice_value is now averaged.  Next it needs to be scaled to an unsigned 16 bit value with DC offso  ibe written to the  DAC.
    switch (wav_format.sig_bps) 
    {
      case 16:   
        slice_value+=32768; //scaling to unsigned 16 bit
        break;
    }
  
    dac_data=(short unsigned)slice_value;//16 bit
    //printf("%d\n", dac_data);
    //DAC_fifo[DAC_wptr]=dac_data; //put slice value into dac fifo        
    DAC_fifo[DAC_wptr]=(dac_data & 0xFFC0) | (1<<16);
    free(slice_buf);
  } else if (feof(wavefile))
  {
    file_end = 1; 
       
  }
}

/*
Config and enable for the DMA
*/
void startDMA(int numOfSlices)
{
  //printf("num of slices to send - %d\r\n", numOfSlices);
  conf0 = new MODDMA_Config;

  conf0
   ->channelNum    ( MODDMA::Channel_0 )
   ->srcMemAddr    ( (uint32_t) &DAC_fifo)
   ->dstMemAddr    ( MODDMA::DAC )
   ->transferSize  ( numOfSlices )
   ->transferType  ( MODDMA::m2p )
   ->dstConn       ( MODDMA::DAC )
   ->attach_tc     ( &TC0_callback )
   ->attach_err    ( &ERR0_callback )     
  ; // config end
  
  //DAC frequency
  LPC_DAC->DACCNTVAL = 4; // 24 MHz / 2 bytes for 1 hz... /  =  
                                       // 24 MHz / 2 / 44.1 KHz = 272.1
                                       // 24 MHz / 2 / 22.1 KHz = 542.98 
                                       // 24 MHZ / 256/ 22.1 Khz = 4.24
  // Prepare first configuration.
  if (!dma.Prepare( conf0 )) {
      error("dma conf0 not loaded");
  }
  // Begin (enable DMA and counter). Note, don't enable
  // DBLBUF_ENA as we are using DMA double buffering.
  LPC_DAC->DACCTRL |= (3UL << 2); //CNT_ENA time out counter is enabled, DMA_ENA is enabled
}

int main()
{
  fseek(wav_file4, 0, SEEK_SET);
  int slicesRead = 0;
  int slice_num = 0;
  file_end = 0;
  for (i=0;i<BUFFER_SIZE;i+=2) {
    DAC_fifo[i]=0;
    DAC_fifo[i+1]=3000;
  }
  while (file_end == 0)
  {
    do
    {
      if(read_slices == 0) 
      {
        read_wav_file(wav_file4); // otherwise wav file data is read until slice data is found, feof sets file_end to 1
      } else if (read_slices == 1)           
      {      
        read_and_avg_slices(wav_file4, slicesRead);
        slicesRead = (slicesRead+1); //increment to the next DAC_fifo position
        //slices ++;
        if (num_slices == slice_num) //if all slices are read for this file, turn off read_slices flag 
        {
          read_slices = 0;
        } else 
        {
          slice_num++; //increment to the next slice
        }
      }
    } while (slicesRead != BUFFER_SIZE && file_end == 0); //256 slices read, time for DMA
    if (slice_num == 0)
    {
        // nothing to send
        break; 
    } else 
    {
      //printf("Slice Number %d\n", slice_num);
      startDMA(slicesRead);
      slicesRead = 0;
      //while (!(LPC_GPDMA->DMACIntTCClear & (1UL << 0))) //wait for the DMA completion flag to set
      //{
      //  ;
      //}
      while(1)
      {
        //printf("Slice Number %d\n", slice_num);
      ;
      }
    }
  }
}

// Configuration callback on TC
void TC0_callback(void) {
    // Just show sending complete.
    led3 = !led3;
    // Get configuration pointer.
    MODDMA_Config *config = dma.getConfig();
    
    // Finish the DMA cycle by shutting down the channel.
    dma.Disable( (MODDMA::CHANNELS)config->channelNum() );

    //setup and enable
    //dma.Prepare( conf0 ); //cant prepare conf0 in TC0

    // Clear DMA IRQ flags.
    if (dma.irqType() == MODDMA::TcIrq) dma.clearTcIrq();

    //DMA_complete = 1;
}

// Configuration callback on Error
void ERR0_callback(void) {
    error("TC0 Callback error");
}