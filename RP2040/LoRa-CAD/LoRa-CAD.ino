/*
  CatSniffer - Use LoRa for communication with the SX1262 module
  
  Eduardo Contreras @ Electronic Cats
  Kevin Leon @ Electronic Cats
  Original Creation Date: Jan 10, 2025

  This code is beerware; if you see me (or any other Electronic Cats
  member) at the local, and you've found our code helpful,
  please buy us a round!
  Distributed as-is; no warranty is given.
*/

#define SERIALCOMMAND_HARDWAREONLY

#include <SerialCommand.h>
#include <RadioLib.h>

#define CTF1 8
#define CTF2 9
#define CTF3 10

#define LED1 (27)
#define LED2 (26)
#define LED3 (28)

// #define VERBOSE

uint8_t LEDs[3] = { LED1, LED2, LED3 };

// SX1262 has the following connections:
// NSS pin:   17
// DIO1 pin:  5
// NRST pin:  24
// BUSY pin:  4
SX1262 radio = new Module(17, 5, 24, 4);
SerialCommand SCmd;

struct RadioContext {
  float frequency;
  int bandWidth;
  int spreadFactor;
  int codingRate;
  byte syncWord;
  int preambleLength;
  int outputPower;
};

RadioContext radioCtx;

// whether we are receiving, or scanning
bool receiving = false;
bool runningScan = false;
// flag to indicate that a packet was successfully printed
bool recivedPacket = false;
const unsigned long interval = 5000;    // 5 s interval to send message
unsigned long previousMillis = 0;  // will store last time message sent

bool hoppChannel = true;
float startFreq = 150;
float endFreq = 960;

// flag to indicate that a packet was detected or CAD timed out
volatile bool scanFlag = false;

void setFlag(void) {
  // something happened, set the flag
  scanFlag = true;
}

void setup() {
  Serial.begin(921600);
  while (!Serial);
  
  pinMode(CTF1, OUTPUT);
  pinMode(CTF2, OUTPUT);
  pinMode(CTF3, OUTPUT);

  pinMode(LED1, OUTPUT);
  pinMode(LED2, OUTPUT);
  pinMode(LED3, OUTPUT);

  digitalWrite(CTF1, HIGH);
  digitalWrite(CTF2, LOW);
  digitalWrite(CTF3, LOW);

  digitalWrite(LED1, 0);
  digitalWrite(LED2, 0);
  digitalWrite(LED3, 0);

  SCmd.addCommand("set_freq", cmdSetFrequency);
  SCmd.addCommand("set_sf", cmdSetSpreadFactor);
  SCmd.addCommand("set_bw", cmdSetBandWidth);
  SCmd.addCommand("set_cr", cmdSetCodingRate);
  SCmd.addCommand("set_sw", cmdSetSyncWord);
  SCmd.addCommand("set_pl", cmdSetPreambleLength);
  SCmd.addCommand("set_op", cmdSetOutputPower);
  SCmd.addCommand("start_fixed", cmdSetScanningStart);
  SCmd.addCommand("stop_fixed", cmdSetScanningStop);
  // TODO: Fix the frequency when stop
  SCmd.addCommand("start_hop", cmdStartChannelHopp);
  SCmd.addCommand("stop_hop", cmdStopChannelHopp);
  
  SCmd.addCommand("get_config", cmdGetConfiguration);
  SCmd.addCommand("get_state", cmdGetScanning);
  SCmd.addCommand("help", help);
  

  SCmd.setDefaultHandler(unrecognized);
  // initialize SX1262 with default settings
  radioCtx.frequency = 433;//903.9;
  radioCtx.bandWidth = 250;
  radioCtx.spreadFactor = 7;
  radioCtx.codingRate = 5;
  radioCtx.syncWord = 0x34;
  radioCtx.outputPower = 20;
  radioCtx.preambleLength = 10;

  startFreq = 433;
  
  Serial.print(F("[SX1262] Initializing ... "));
  int state = radio.begin(radioCtx.frequency, radioCtx.bandWidth, radioCtx.spreadFactor, radioCtx.codingRate, radioCtx.syncWord, radioCtx.outputPower, radioCtx.preambleLength, 0, false);
  if (state == RADIOLIB_ERR_NONE) {
    Serial.println(F("success!"));
  } else {
    Serial.print(F("failed, code "));
    Serial.println(state);
    while (true)
      ;
  }

  radio.setRfSwitchPins(21, 20);

  // set the function that will be called
  // when LoRa packet or timeout is detected
  radio.setDio1Action(setFlag);
  startRadioScanning();
}

void resetScan(){
  if (!receiving && runningScan) {
    #ifdef VERBOSE
    Serial.print(F("[SX1262] Starting scan for LoRa preamble ... "));
    #endif

    if(hoppChannel){
      radioCtx.frequency+=0.1;
    
      if(radioCtx.frequency > endFreq){
        radioCtx.frequency = startFreq;
      }
      handleErrorCodePrint(radio.setFrequency(radioCtx.frequency));
    }else{
      handleErrorCodePrint(radio.setFrequency(startFreq));
    }
    
    int state = radio.startChannelScan();
    handleErrorCodePrint(state);
  }
}

void loop() {
  SCmd.readSerial();
  // check if the flag is set
  if (scanFlag && runningScan) {
    SCmd.readSerial();
    int state = RADIOLIB_ERR_NONE;

    // reset flag
    scanFlag = false;

    // check ongoing reception
    if (receiving) {
      // DIO triggered while reception is ongoing
      // that means we got a packet
      // you can read received data as an Arduino String
      String str;
      state = radio.readData(str);

      if (state == RADIOLIB_ERR_NONE) {
        recivedPacket = true;
        // packet was successfully received
        Serial.println(F("[SX1262] Received packet!"));

        // print data of the packet
        Serial.print(F("[SX1262] Data:\t\t"));
        Serial.println(str);

        Serial.print(F("[SX1262] Freq:\t\t"));
        Serial.println(radioCtx.frequency, 2);

        // print RSSI (Received Signal Strength Indicator)
        Serial.print(F("[SX1262] RSSI:\t\t"));
        Serial.print(radio.getRSSI());
        Serial.println(F(" dBm"));

        // print SNR (Signal-to-Noise Ratio)
        Serial.print(F("[SX1262] SNR:\t\t"));
        Serial.print(radio.getSNR());
        Serial.println(F(" dB"));

        // print frequency error
        Serial.print(F("[SX1262] Frequency error:\t"));
        Serial.print(radio.getFrequencyError());
        Serial.println(F(" Hz"));

      } else {
        handleErrorCodePrint(state);
      }

      // reception is done now
      receiving = false;
      recivedPacket = false;

    } else {
      // check CAD result
      state = radio.getChannelScanResult();

      if (state == RADIOLIB_LORA_DETECTED) {
        // LoRa packet was detected
        state = radio.startReceive();
        handleErrorCodePrint(state);
        // set the flag for ongoing reception
        receiving = true;

      }else {
        handleErrorCodePrint(state);
      }
    }

    // if we're not receiving, start scanning again
    resetScan();
  }
  if(receiving){
    if(millis() - previousMillis > interval){
      previousMillis = millis(); 
      if(!recivedPacket){
        recivedPacket = false;
        receiving = false;
        resetScan();
      }
    }
  }
}

void startRadioScanning(){
  // start scanning the channel
  #ifdef VERBOSE
  Serial.print(F("[SX1262] Starting scan for LoRa preamble ... "));
  #endif
  int state = radio.startChannelScan();
  if (state == RADIOLIB_ERR_NONE) {
    runningScan = true;
    resetScan();
  } else {
    Serial.print(F("failed, code "));
    Serial.println(state);
  }
}

void cmdSetPreambleLength(){
  char *arg;
  arg = SCmd.next();
  if(arg != NULL){
    int tmp_value = atoi(arg);
    if(radio.setPreambleLength(tmp_value) == RADIOLIB_ERR_INVALID_PREAMBLE_LENGTH){
      Serial.println(F("Selected preamble length is invalid for this module!"));
      return;
    }
    Serial.println("Preamble Length set to " + String(tmp_value));
    radioCtx.preambleLength = tmp_value;
  }
}

void cmdSetSyncWord(){
  char *arg;
  byte syncWord;
  arg = SCmd.next();
  if(arg != NULL){
    if ((arg[0] > 64 && arg[0] < 71 || arg[0] > 47 && arg[0] < 58) && (arg[1] > 64 && arg[1] < 71 || arg[1] > 47 && arg[1] < 58) && arg[2] == 0){
      syncWord = 0;
      syncWord = nibble(*(arg)) << 4;
      syncWord = syncWord | nibble(*(arg + 1));
      if (radio.setSyncWord(syncWord) != RADIOLIB_ERR_NONE) {
        Serial.println(F("Unable to set sync word!"));
        return;
      }
      Serial.print("Sync word set to 0x");
      Serial.println(syncWord, HEX);
      radioCtx.syncWord = syncWord;
    }else{
      Serial.println("Use yy value. The value yy represents any pair of hexadecimal digits. ");
      return;
    }
  }
}

void cmdSetOutputPower(){
  char *arg;
  arg = SCmd.next();
  if(arg != NULL){
    int tmp_value = atoi(arg);
    if(radio.setOutputPower(tmp_value) == RADIOLIB_ERR_INVALID_CODING_RATE){
      Serial.println(F("Selected output power is invalid for this module!"));
      return;
    }
    Serial.println("Output Power set to " + String(tmp_value));
    radioCtx.outputPower = tmp_value;
  }
}

void cmdSetCodingRate(){
  char *arg;
  arg = SCmd.next();
  if(arg != NULL){
    int tmp_value = atoi(arg);
    if(radio.setCodingRate(tmp_value) == RADIOLIB_ERR_INVALID_CODING_RATE){
      Serial.println(F("Selected coding rate is invalid for this module!"));
      return;
    }
    Serial.println("Coding Rate set to " + String(tmp_value));
    radioCtx.codingRate = tmp_value;
  }
}

void cmdSetSpreadFactor(){
  char *arg;
  arg = SCmd.next();
  if(arg != NULL){
    int tmp_value = atoi(arg);
    if(radio.setSpreadingFactor(tmp_value) == RADIOLIB_ERR_INVALID_SPREADING_FACTOR){
      Serial.println(F("Selected spread factor is invalid for this module!"));
      return;
    }
    Serial.println("Spreadfactor set to " + String(tmp_value));
    radioCtx.spreadFactor = tmp_value;
  }
}

void cmdSetBandWidth(){
  char *arg;
  arg = SCmd.next();
  if(arg != NULL){
    float tmp_value = atof(arg);
    if(radio.setBandwidth(tmp_value) == RADIOLIB_ERR_INVALID_BANDWIDTH){
      Serial.println(F("Selected bandwidth is invalid for this module!"));
      return;
    }
    Serial.println("Bandwidth set to " + String(tmp_value) + " kHz");
    radioCtx.bandWidth = tmp_value;
  }
}

void cmdSetFrequency(){
  char *arg;
  arg = SCmd.next();
  if(arg != NULL){
    float tmp_value = atof(arg);
    if(radio.setFrequency(tmp_value) == RADIOLIB_ERR_INVALID_FREQUENCY){
      Serial.println(F("Selected frequency is invalid for this module!"));
      return;
    }
    radioCtx.frequency = tmp_value;
    Serial.println("Frequency set to " + String(tmp_value) + " MHz");
  }
}

void cmdGetConfiguration(){
  Serial.println("Radio Configuration");
  Serial.print("Frequency = ");
  Serial.print(radioCtx.frequency, 2);
  Serial.println(" MHz");
  Serial.println("Bandwidth = " + String(radioCtx.bandWidth));
  Serial.println("Spreading Factor = " + String(radioCtx.spreadFactor));
  Serial.println("Coding Rate = 4/" + String(radioCtx.codingRate));
  Serial.print("Sync Word = 0x");
  Serial.println(radioCtx.syncWord, HEX);
  Serial.println("Preamble Length = " + String(radioCtx.preambleLength));
  Serial.println("Output Power = " + String(radioCtx.outputPower));
}

void cmdSetScanningStart(){
  if(!runningScan){
    startRadioScanning();
  }
}

void cmdSetScanningStop(){
  if(runningScan){
    scanFlag = false;
    runningScan = false;
    receiving = false;
  }
}

void cmdGetScanning(){
  Serial.print("State:" );
  Serial.println(runningScan?"Running" : "Stopped");
  Serial.println(hoppChannel?"Channel Range" : "Fixed");
}

void cmdStopChannelHopp(){
   if(hoppChannel){
    hoppChannel = false;
    handleErrorCodePrint(radio.setFrequency(startFreq));
    startRadioScanning();
  }
}

void cmdStartChannelHopp(){
   if(!hoppChannel){
    hoppChannel = true;
    handleErrorCodePrint(radio.setFrequency(startFreq));
    startRadioScanning();
  }
}


void help(){
  Serial.println("Available commands are:");
  Serial.print("set_freq ");
  Serial.println("Set the frequency in range of 150/960 MHz: Default 903.9");
  Serial.print("set_sf ");
  Serial.println("Set the spread factor. Default: 7");
  Serial.print("set_bw ");
  Serial.println("Set the bandwith value. Options: (7.8, 10.4, 15.6, 20.8, 31.25, 41.7, 62.5, 125, 250, 500) kHz: Default 250");
  Serial.print("set_cr ");
  Serial.println("Set the coding rate. Default: 5");
  Serial.print("set_sw ");
  Serial.println("Set the sync Word: Default: 0x34");
  Serial.print("set_pl ");
  Serial.println("Set the preamble length: Default: 10");
  Serial.print("set_op ");
  Serial.println("Set the output power. Default: 20");
  Serial.println("get_config Show the configuration of the radio");
}

void unrecognized(const char *command) {
  Serial.println("Command not found, type help to get the valid commands");
}


void handleErrorCodePrint(int16_t state){
  switch (state)
  {
  case RADIOLIB_ERR_NONE:
     #ifdef VERBOSE
    Serial.println(F("success!"));
    #endif
    break;
  case RADIOLIB_CHANNEL_FREE:
    #ifdef VERBOSE
    Serial.println(F("[SX1262] Channel is free!"));
    #endif
    break;
  case RADIOLIB_ERR_CRC_MISMATCH:
    Serial.println(F("[SX1262] CRC Failed"));
    break;
  default:
    Serial.print(F("failed, code "));
    Serial.println(state);
    break;
  }
}

byte nibble(char c)
{
  if (c >= '0' && c <= '9')
    return c - '0';

  if (c >= 'a' && c <= 'f')
    return c - 'a' + 10;

  if (c >= 'A' && c <= 'F')
    return c - 'A' + 10;

  return 0;  // Not a valid hexadecimal character
}
