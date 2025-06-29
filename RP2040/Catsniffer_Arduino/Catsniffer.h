#ifndef __CATSNIFFER_H
#define __CATSNIFFER_H

#include <stdint.h>
#include <Arduino.h>
#include "CatSerCommand.h"

#define LIB_VERSION "0.0.1"

//Pin declaration to enter bootloader mode on CC1352
#define PIN_RESET (3)
#define PIN_RESET_VIEWER (15)
#define PIN_BOOT (2)
#define PIN_BUTTON (2)

// LEDS
#define LED1 (27)
#define LED2 (26)
#define LED3 (28)

//Pin Declaration for RF switch
#define CTF1 8
#define CTF2 9
#define CTF3 10

// jTAG Pin range
#define JTAG_PIN_START 11
#define JTAG_PIN_END 15

// Serial
#define CMD_BUFFER_LEN 360

#define MODE_BOOT_BAUDRATE 500000 // For CC1452 tool don't change this
#define MODE_PASS_BAUDRATE 921600 // Default passtrhough baudrate
#define MODE_LORA_BAUDRATE 921600 // Default passtrhough baudrate

#define CMD_SUFFIX ">ÿñ"

extern CatSerCommand CatCMDHandler;

typedef enum{
  PASSTRHOUGH = 0,   //Mode flag = 0; for passthrough @ 921600 bauds 
  BOOT,              //Mode flag = 1; for bootloader options @ 500000 bauds
  LORA               //Mode flag = 2; for LoRaWAN @ 921600 bauds
} catsniffer_mode_t;

typedef enum{
  GIG = 0,    // Band = 0; for 2.4 GHz band for CC1352
  SUBGIG_1,   // Band = 1; for Sub-ghz for CC1352
  SUBGIG_2    // Band = 2; for LoRa for RP2040 and SX126
} catsniffer_band_t;

typedef struct {
  catsniffer_mode_t mode;
  catsniffer_band_t band;
  unsigned long led_interval;
  unsigned long previousMillis;  // will store last time blink happened
  unsigned long baud;
} catsniffer_t;


void changeBaud(catsniffer_t *cs, unsigned long newBaud); // Deprecated
void changeBand(catsniffer_t *cs, unsigned long newBand); // Deprecated
void changeMode(catsniffer_t *cs, unsigned long newMode); // Deprecated

// Serialpassthrough
void catsnifferCC1352Reset(void);
void catsnifferCC1352Boot(void);
void catsnifferButtonsConfigure(void);
void catsnifferLedsConfigure(void);
void catsnifferCtfConfigure(void);
void catsnifferjTAGBoot(void);

void catsnifferSerialChangeBaudrate(catsniffer_t *cs, unsigned long newBaud);
void catsnifferSerial1ChangeBaudrate(catsniffer_t *cs, unsigned long newBaud);
void catsnifferSerial2ChangeBaudrate(catsniffer_t *cs, unsigned long newBaud);

void catsnifferRFChangeBand(catsniffer_t *cs, catsniffer_band_t newBand);
void catsnifferChangeMode(catsniffer_t *cs, catsniffer_mode_t newMode);

void catsnifferUpdateLedsAnimation(void);
void catsnifferResetPasstrhough(void);
void catsnifferPassCommandBegin(void);
void catsnifferCommandProcess(void);
#endif