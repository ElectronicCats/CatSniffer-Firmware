#ifndef SERIAL_PASSTHROUGH_H
#define SERIAL_PASSTHROUGH_H

//Pin declaration to enter bootloader mode on CC1352
#define Pin_Reset (15)
#define Pin_Reset_Viewer (3)
#define Pin_Boot (2)
#define Pin_Button (2)
#define LED1 (27)
#define LED2 (26)
#define LED3 (28)
//Pin Declaration for RF switch
#define CTF1 8
#define CTF2 9
#define CTF3 10

enum MODE{
   PASSTRHOUGH = 0,   //Mode flag = 0; for passthrough @ 921600 bauds 
   BOOT,              //Mode flag = 1; for bootloader options @ 500000 bauds
   LORA               //Mode flag = 2; for LoRaWAN @ 921600 bauds
 };

enum BAND{
   GIG = 0, 
   SUBGIG_1, 
   SUBGIG_2
 };

typedef struct {
  uint8_t mode;
  bool mode_tested;
  uint8_t band;
  unsigned long led_interval;
  unsigned long previousMillis;  // will store last time blink happened
  unsigned long baud;
} catsniffer_t;

void resetCC(void);
void bootModeCC(void);
void changeBaud(catsniffer_t *cs, unsigned long newBaud);
void changeBand(catsniffer_t *cs, unsigned long newBand);
void changeMode(catsniffer_t *cs, unsigned long newMode);
void processCommand(String *cmd);


#endif