#include "mbed.h"
#include "wave_player.h"
#include "SDFileSystem.h"

SDFileSystem    sd(p5, p6, p7, p8, "sd"); //PinName mosi, PinName miso, PinName sclk, PinName cs, const char* name
AnalogOut       DACout(p18); //Choose correct pin for DAC at a later date
wave_player     waver(&DACout);
DigitalOut led1(LED1);


FILE *wav_file1 = fopen("/sd/wf/909rim01_16_22.wav", "r");
//FILE *wav_file2 = fopen("/sd/wf/bd_02.wav", "r");
//FILE *wav_file3 = fopen("/sd/wf/bd_03.wav", "r");
FILE *wav_file4 = fopen("/sd/wf/bd_04.wav", "r"); //maybe best sounding one
FILE *wav_file5 = fopen("/sd/wf/clap_03.wav", "r"); //decent
FILE *wav_file6 = fopen("/sd/wf/clap_04.wav", "r"); //decent
FILE *wav_file7 = fopen("/sd/wf/hat_02.wav", "r"); //decent
FILE *wav_file8 = fopen("/sd/wf/hat_03.wav", "r"); //decent

int wav_play(FILE *& wav_file) {
    fseek(wav_file, 0, SEEK_SET);  // set file pointer to beginning
    waver.play(wav_file);
    return 1;
}
int main() {
    volatile int life_counter = 0;    
    //printf("Start\r\n");
    mkdir("/sd/wf/", 0777);     // make the directory to the SD card
                                // 0777 is the default mode so to have the widest access     
    if (wav_play(wav_file4) == 1) { //play wav file, returning a 1 
        //printf("Playing\r\n");
    }
    while (1) { 
        // There's not a lot to do as DMA and interrupts are
        // now handling the buffer transfers. So we'll just
        // flash led1 to show the Mbed is alive and kicking.
        if (life_counter++ > 1000000) {
            led1 = !led1; // Show some sort of life.
            life_counter = 0;
        }
    } 
}