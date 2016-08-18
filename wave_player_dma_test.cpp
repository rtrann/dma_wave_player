//-----------------------------------------------------------------------------
// a sample mbed library to play back wave files.
//
// explanation of wave file format.
// https://ccrma.stanford.edu/courses/422/projects/WaveFormat/

// if VERBOSE is uncommented then the wave player will enter a verbose
// mode that displays all data values as it reads them from the file
// and writes them to the DAC.  Very slow and unusable output on the DAC,
// but useful for debugging wave files that don't work.
//#define VERBOSE


#include <mbed.h>
#include <stdio.h>
#include <wave_player.h>
#include "MODDMA.h"

DigitalOut led3(LED3);
DigitalOut led4(LED4);

MODDMA dma;
//MODDMA_Config *conf0, *conf1;
MODDMA_Config *conf0;

void TC0_callback(void);
void ERR0_callback(void);

void TC1_callback(void);
void ERR1_callback(void);

//-----------------------------------------------------------------------------
// constructor -- accepts an mbed pin to use for AnalogOut.  Only p18 will work
wave_player::wave_player(AnalogOut *_dac)
{
  wave_DAC=_dac;
  wave_DAC->write_u16(32768);        //DAC is 0-3.3V, so idles at ~1.6V
}

//-----------------------------------------------------------------------------
// player function.  Takes a pointer to an opened wave file.  The file needs
// to be stored in a filesystem with enough bandwidth to feed the wave data.
// LocalFileSystem isn't, but the SDcard is, at least for 22kHz files.  The
// SDcard filesystem can be hotrodded by increasing the SPI frequency it uses
// internally.
//-----------------------------------------------------------------------------
void wave_player::play(FILE *wavefile)
{
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
  DAC_wptr=0;
  DAC_rptr=0;
  for (i=0;i<256;i+=2) {
    DAC_fifo[i]=0;
    DAC_fifo[i+1]=3000;
  }
  DAC_wptr=4;
  //DAC_on=0;        
  fread(&chunk_id,4,1,wavefile); //get chunk id
  printf("chunk_id - %d\n", chunk_id);
  fread(&chunk_size,4,1,wavefile); //get chunk size
  printf("chunk_size - %d\n", chunk_size);
  while (!feof(wavefile)) { //sequentially reading the wave file seperating it into different cases
    switch (chunk_id) {
      case 0x46464952:
        fread(&data,4,1,wavefile); //get data
        break;
      case 0x20746d66:
        fread(&wav_format,sizeof(wav_format),1,wavefile); //get wave format info
        //printf("wav_format - %d\n", wav_format.block_align);
        if (chunk_size > sizeof(wav_format))
          fseek(wavefile,chunk_size-sizeof(wav_format),SEEK_CUR);
        break;
      case 0x61746164:
// allocate a buffer big enough to hold a slice
        slice_buf=(char *)malloc(wav_format.block_align); //set a 16 bit slice buffer (block_align is type short)
        if (!slice_buf) {
          printf("Unable to malloc slice buffer");
          exit(1);
        }
        num_slices=chunk_size/wav_format.block_align; //atleast 16 bits / 16 bits
        samp_int=1000000/(wav_format.sample_rate);

// starting up ticker to write samples out -- no printfs until tick.detach is called

        //tick.attach_us(this,&wave_player::dac_out, samp_int); 
        //DAC_on=1; 

// start reading slices, which contain one sample each for however many channels
// are in the wave file.  one channel=mono, two channels=stereo, etc.  Since
// mbed only has a single AnalogOut, all of the channels present are averaged
// to produce a single sample value.  This summing and averaging happens in
// a variable of type signed long long, to make sure that the data doesn't
// overflow regardless of sample size (8 bits, 16 bits, 32 bits).
//
// note that from what I can find that 8 bit wave files use unsigned data,
// while 16 and 32 bit wave files use signed data
//
        conf0 = new MODDMA_Config;

        for (slice=0;slice<num_slices;slice+=1) 
            {
            fread(slice_buf,wav_format.block_align,1,wavefile);
            if (feof(wavefile)) 
                {
                printf("Oops -- not enough slices in the wave file\n");
                exit(1);
                }
            data_sptr=(short *)slice_buf;     // 16 bit samples
            data_bptr=(unsigned char *)slice_buf;     // 8 bit samples
            data_wptr=(int *)slice_buf;     // 32 bit samples
            slice_value=0;
            for (channel=0;channel<wav_format.num_channels;channel++) 
                {
                switch (wav_format.sig_bps) 
                    {
                    case 16:
                        if (verbosity)
                            printf("16 bit channel %d data=%d ",channel,data_sptr[channel]);
                            slice_value+=data_sptr[channel];
                        break;
                    case 32:
                        if (verbosity)
                            printf("32 bit channel %d data=%d ",channel,data_wptr[channel]);
                            slice_value+=data_wptr[channel];
                        break;
                    case 8:
                        if (verbosity)
                            printf("8 bit channel %d data=%d ",channel,(int)data_bptr[channel]);
                            slice_value+=data_bptr[channel];
                        break;
                    }
                }
            slice_value/=wav_format.num_channels; // summed and averaged
          
// slice_value is now averaged.  Next it needs to be scaled to an unsigned 16 bit value
// with DC offset so it can be written to the DAC.
          switch (wav_format.sig_bps) 
            {
            case 8:     slice_value<<=8;
                        break;
            case 16:    slice_value+=32768; //scaling to unsigned 16 bit
                        break;
            case 32:    slice_value>>=16;
                        slice_value+=32768;
                        break;
            }
          dac_data=(short unsigned)slice_value;//16 bit
          
          DAC_fifo[DAC_wptr]=dac_data; //put slice value into dac fifo 
          
          conf0
           ->channelNum    ( MODDMA::Channel_0 )
           ->srcMemAddr    ( (uint32_t) &DAC_fifo[DAC_wptr])
           ->dstMemAddr    ( MODDMA::DAC )
           ->transferSize  ( sizeof(DAC_fifo[DAC_wptr]) ) //in bytes
           ->transferType  ( MODDMA::m2p )
           ->dstConn       ( MODDMA::DAC )
           ->attach_tc     ( &TC0_callback )
           ->attach_err    ( &ERR0_callback )     
          ; // config end
          
          LPC_DAC->DACCNTVAL = 542.98; // 24 MHz / 2 bytes for 1 hz... /  =  
                                       // 24 MHz / 2 / 44.1 KHz = 272.1
                                       // 24 MHz / 2 / 22.1 KHz = 542.98 

          // Prepare first configuration.
          if (!dma.Prepare( conf0 )) {
              error("dma conf0 not loaded");
          }
          //dma.Prepare(conf0);
          // Begin (enable DMA and counter). Note, don't enable
          // DBLBUF_ENA as we are using DMA double buffering.
          LPC_DAC->DACCTRL |= (3UL << 2); //CNT_ENA time out counter is enabled, DMA_ENA is enabled
          
          DAC_wptr=(DAC_wptr+1) & 0xff; // increment element

          //while (DAC_wptr==DAC_rptr) { // waits for a tick to increment rptr
          //}
        }
            //printf("sizeof DAC_fifo[5]- %d\r\n", sizeof(DAC_fifo[5]));
            //printf("sizeof DAC_fifo- %d\r\n", sizeof(DAC_fifo));
            //printf("num_slices - %d\r\n", num_slices);
          // Prepare the GPDMA system for buffer0.
          
        //DAC_on=0;
        //tick.detach();
        free(slice_buf);
        break;
      case 0x5453494c:
        fseek(wavefile,chunk_size,SEEK_CUR);
        break;
      default:
        printf("unknown chunk type 0x%x, size %d\n",chunk_id,chunk_size);
        data=fseek(wavefile,chunk_size,SEEK_CUR);
        break;
    }
    fread(&chunk_id,4,1,wavefile);
    fread(&chunk_size,4,1,wavefile);
  }
}


// Configuration callback on TC
void TC0_callback(void) {
    
    // Just show sending complete.
    led3 = !led3;
    //printf("testttt\r\n");
    // Get configuration pointer.
    MODDMA_Config *config = dma.getConfig();
    
    // Finish the DMA cycle by shutting down the channel.
    dma.Disable( (MODDMA::CHANNELS)config->channelNum() );

    //setup and enable
    //dma.Prepare( conf0 ); //cant prepare conf0 in TC0

    // Clear DMA IRQ flags.
    if (dma.irqType() == MODDMA::TcIrq) dma.clearTcIrq();
    //DAC_rptr=(DAC_rptr+1) & 0xff;
}

// Configuration callback on Error
void ERR0_callback(void) {
    error("TC0 Callback error");
}