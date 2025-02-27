/*
  CatSniffer - Use LoRa for communication with the SX1262 module
  
  Eduardo Contreras @ Electronic Cats
  Original Creation Date: Jan 10, 2025

  This code is beerware; if you see me (or any other Electronic Cats
  member) at the local, and you've found our code helpful,
  please buy us a round!
  Distributed as-is; no warranty is given.
*/

#define SERIALCOMMAND_HARDWAREONLY
#include <SerialCommand.h> // https://github.com/kroimon/Arduino-SerialCommand
#include <RadioLib.h>

// this file contains binary patch for the SX1262
#include <modules/SX126x/patches/SX126x_patch_scan.h>

#define CTF1 8
#define CTF2 9
#define CTF3 10

#define FREQ_RANGE_START 150
#define FREQ_RANGE_END 960
#define SAMPLE_RATE 2048

#define LED1 (27)
#define LED2 (26)
#define LED3 (28)

struct RadioContext{
  float freqStart;
  float freqEnd;
  float freq;
  float biteRate;
  float freqDeviation;
  float bandwidth;
  float power;
};

uint8_t LEDs[3]={LED1,LED2,LED3};
bool runningScan = false;

// SX1262 has the following connections:
// NSS pin:   17
// DIO1 pin:  5
// NRST pin:  24
// BUSY pin:  4
SX1262 radio = new Module(17, 5, 24, 4);

SerialCommand SCmd;

RadioContext radioCtx;

void setup() {
  Serial.begin(921600);
  while(!Serial)

  // Callbacks for serial commands
  SCmd.addCommand("set_start_freq", cmdSetFreqStart);
  SCmd.addCommand("set_end_freq", cmdSetFreqEnd);
  SCmd.addCommand("start", cmdStart);
  SCmd.addCommand("stop", cmdStop);
  SCmd.addCommand("get_state", cmdGetState);
  SCmd.addCommand("get_config", cmdGetConfiguration);
  SCmd.addCommand("help", help);
  SCmd.setDefaultHandler(unrecognized);

  // frequency range in MHz to scan
  radioCtx.freqStart = FREQ_RANGE_START;
  radioCtx.freqEnd = FREQ_RANGE_END;
  radioCtx.biteRate = 4.8;
  radioCtx.freqDeviation = 5.0;
  radioCtx.bandwidth = 156.2;
  radioCtx.power = 10;
  
  // initialize SX1262 FSK modem at the initial frequency
  Serial.println(F("DONE: Initializing ... "));
  int state = radio.beginFSK(radioCtx.freqStart,radioCtx.biteRate,radioCtx.freqDeviation,radioCtx.bandwidth,radioCtx.power,16,0,false);
  if(state == RADIOLIB_ERR_NONE) {
    Serial.println(F("success!"));
  } else {
    Serial.print(F("ERROR:"));
    Serial.println(state);
    while (true) { delay(10); }
  }

  // upload a patch to the SX1262 to enable spectral scan
  // NOTE: this patch is uploaded into volatile memory,
  //       and must be re-uploaded on every power up
  Serial.print(F("DONE Uploading patch ... "));
  state = radio.uploadPatch(sx126x_patch_scan, sizeof(sx126x_patch_scan));
  if(state == RADIOLIB_ERR_NONE) {
    Serial.println(F("DONE"));
  } else {
    Serial.print(F("ERROR:"));
    Serial.println(state);
    while (true) { delay(10); }
  }

  // configure scan bandwidth to 234.4 kHz
  // and disable the data shaping
  // Serial.print(F("[SX1262] Setting scan parameters ... "));
  state = radio.setRxBandwidth(234.3);
  state |= radio.setDataShaping(RADIOLIB_SHAPING_NONE);
  if(state == RADIOLIB_ERR_NONE) {
    Serial.println(F("DONE"));
  } else {
    Serial.print(F("ERROR:"));
    Serial.println(state);
    while (true) { delay(10); }
  }

  runningScan = true;
    // some modules have an external RF switch
  // controlled via two pins (RX enable, TX enable)
  // to enable automatic control of the switch,
  // call the following method
  // RX enable:   4
  // TX enable:   5
  
  radio.setRfSwitchPins(21, 20);
  digitalWrite(LED1, 0);
  digitalWrite(LED2, 1);
  digitalWrite(LED3, 0);
}

void loop() {
  SCmd.readSerial();     // We don't do much, just process serial commands
  // perform scan over the entire frequency range
  radioCtx.freq = radioCtx.freqStart;
  while((radioCtx.freq <= radioCtx.freqEnd) && runningScan) {
    Serial.print("FREQ ");
    Serial.println(radioCtx.freq, 2);

    // start spectral scan
    // number of samples: 2048 (fewer samples = better temporal resolution)
    // Serial.print(F("[SX1262] Starting spectral scan ... "));
    int state = radio.spectralScanStart(SAMPLE_RATE);
    if(state == RADIOLIB_ERR_NONE) {
      Serial.println(F("DONE"));
    } else {
      Serial.print(F("ERROR:"));
      Serial.println(state);
      while (true) { delay(10); }
    }

    // wait for spectral scan to finish
    while(radio.spectralScanGetStatus() != RADIOLIB_ERR_NONE) {
      delay(10);
    }

    // read the results
    uint16_t results[RADIOLIB_SX126X_SPECTRAL_SCAN_RES_SIZE];
    state = radio.spectralScanGetResult(results);
    if(state == RADIOLIB_ERR_NONE) {
      digitalWrite(LED1, 1);
      digitalWrite(LED2, 1);
      digitalWrite(LED3, 1);
      Serial.print("SCAN ");
      for(uint8_t i = 0; i < RADIOLIB_SX126X_SPECTRAL_SCAN_RES_SIZE; i++) {
        Serial.print(results[i]);
        Serial.print(',');
      }
      Serial.println(" END");
    }

    // wait a little bit before the next scan
    delay(5);

    // set the next frequency
    // the frequency step should be slightly smaller
    // or the same as the Rx bandwidth set in setup
    radioCtx.freq += 0.2;
    radio.setFrequency(radioCtx.freq);
  }
  digitalWrite(LED1, 0);
  digitalWrite(LED2, 0);
  digitalWrite(LED3, 0);
  
}


void cmdSetFreqStart(){
  char *arg;
  arg = SCmd.next();
  if (arg != NULL){
    float tmpFreqS = atoi(arg);  
    if(tmpFreqS > radioCtx.freqEnd){
      Serial.println(F("Selected frequency is invalid for this module!"));
      return;
    }
    radioCtx.freqStart = tmpFreqS;
    Serial.println("Frequency set to " + String(tmpFreqS) + " MHz");
  }else{
    Serial.println(F("Invalid argument!"));
  }
}

void cmdSetFreqEnd(){
  char *arg;
  arg = SCmd.next();
  if (arg != NULL){
    float tmpFreqE = atoi(arg);  
    if(tmpFreqE < radioCtx.freqStart){
      Serial.println(F("Selected frequency is invalid for this module!"));
      return;
    }
    radioCtx.freqEnd = tmpFreqE;
    Serial.println("Frequency set to " + String(tmpFreqE) + " MHz");
  }else{
    Serial.println(F("Invalid argument!"));
  }
}

void cmdStart(){
  runningScan = true;
}

void cmdStop(){
  runningScan = false;
}

void cmdGetState(){
  Serial.print("State: ");
  Serial.println(runningScan?"Running" : "Stopped");
}

void cmdGetConfiguration(){
  Serial.println("Radio Configuration");
  Serial.println("Frequency Start= " + String(radioCtx.freqStart) + " MHz");
  Serial.println("Frequency End= " + String(radioCtx.freqEnd) + " MHz");
  Serial.println("BiteRate = " + String(radioCtx.biteRate));
  Serial.println("Freq Deviation = " + String(radioCtx.freqDeviation));
  Serial.println("Bandwidth" + String(radioCtx.bandwidth));
  Serial.println("Power = " + String(radioCtx.power));
}

void help(){
  Serial.println("Available commands are:");
  Serial.print("set_start_freq ");
  Serial.println("Set the frequency start: Default " + String(FREQ_RANGE_START));
  Serial.print("set_end_freq ");
  Serial.println("Set the frequency end: Default " + String(FREQ_RANGE_END));
}

void unrecognized(const char *command) {
  Serial.println("Command not found, type help to get the valid commands");
}
