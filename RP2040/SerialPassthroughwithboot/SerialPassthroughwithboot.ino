/*
  SerialPassthrough - Use tool to flash the CC1352 module

  Andres Sabas @ Electronic Cats
  Eduardo Contreras @ Electronic Cats
  Original Creation Date: Jul 10, 2023

  This code is beerware; if you see me (or any other Electronic Cats
  member) at the local, and you've found our code helpful,
  please buy us a round!
  Distributed as-is; no warranty is given.
  
*/

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
   PASSTRHOUGH = 0, 
   BOOT, 
   LORA
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

catsniffer_t catsniffer;

void bootModeCC(void);
void changeBaud(catsniffer_t *cs, unsigned long newBaud);
void changeBand(catsniffer_t *cs, unsigned long newBand);

//Mode flag = 1; for bootloader options @ 500000 bauds
//Mode flag = 0; for passthrough @ 921600 bauds
bool MODE_FLAG = 0;

uint8_t LEDs[3]={LED1,LED2,LED3};
int i=0;

unsigned long interval = 0;    // interval to blink LED
unsigned long previousMillis = 0;  // will store last time blink happened

void setup() {
  pinMode(Pin_Button, INPUT_PULLUP);
  pinMode(Pin_Boot, INPUT_PULLUP);
  pinMode(Pin_Reset, OUTPUT);
  pinMode(Pin_Reset_Viewer, INPUT);
  
  pinMode(LED1,OUTPUT);
  pinMode(LED2,OUTPUT);
  pinMode(LED3,OUTPUT);
  pinMode(CTF1, OUTPUT);
  pinMode(CTF2, OUTPUT);
  pinMode(CTF3, OUTPUT);

  //Make all cJTAG pins an input 
  for(int i=11;i<15;i++){
    pinMode(i,INPUT);
  }

  //Select mode and speed
  if(!digitalRead(Pin_Boot)){
    catsniffer.led_interval=200;
    catsniffer.baud=500000;
    catsniffer.mode=BOOT;
  }
  else{
    catsniffer.led_interval=1000;
    catsniffer.baud=921600;
    catsniffer.mode=PASSTRHOUGH;
    pinMode(Pin_Reset, INPUT);
  }
  while(!digitalRead(Pin_Boot));

  //Begin Serial ports
  Serial.begin(catsniffer.baud);
  Serial1.begin(catsniffer.baud);

  if(catsniffer.mode==BOOT){
    bootModeCC();
  }
  
  if(catsniffer.mode==PASSTRHOUGH){
    //Switch Radio for 2.4Ghz BLE by default can be changed on the fly
    changeBand(&catsniffer, GIG);
  }
  
  digitalWrite(LED1, 0);
  digitalWrite(LED2, 0);
  digitalWrite(LED3, catsniffer.mode);
}

uint8_t commandID[2]={0xC3, 0xB1};
bool asciiRecognized=0;
bool commandRecognized=0;
String commandData="";

void loop() {
  // ñ<Payload>ñ Catsnifffer Commands
  //SerialPassthrough 
  if (Serial.available()) {      // If anything comes in Serial (USB),
    int data = Serial.read();
    if(!commandRecognized){
      if(data==commandID[0] && !asciiRecognized){
        asciiRecognized=1;
      }
      if(data==commandID[1] && asciiRecognized){
        commandRecognized=1;
      }
    }
    if(commandRecognized){
      commandData+=String((char)data);
      if(commandData.endsWith(">ñ")){
        Serial.print(commandData);
        commandData="";
        commandRecognized=0;
      }
    }else{
      Serial1.write(data);   // read it and send it out Serial1 (pins 0 & 1)
    }

  }

  if (Serial1.available()) {     // If anything comes in Serial1 (pins 0 & 1)
    Serial.write(Serial1.read());   // read it and send it out Serial (USB)
  }
  
  
  if(millis() - catsniffer.previousMillis > catsniffer.led_interval) {
    catsniffer.previousMillis = millis(); 
    if(catsniffer.mode){
      digitalWrite(LEDs[i], !digitalRead(LEDs[i]));
      i++;
      if(i>2)i=0;
    }else{
      digitalWrite(LED3, !digitalRead(LED3));
    }
    
  }
}

void resetCC(void){
  delay(100);
  digitalWrite(Pin_Reset, LOW);
  delay(100);
  digitalWrite(Pin_Reset, HIGH);
  delay(100);
  }

void bootModeCC(void){
  pinMode(Pin_Boot, OUTPUT);
  //Enter bootloader mode function
  digitalWrite(Pin_Boot, LOW);
  delay(100);
  digitalWrite(Pin_Reset, LOW);
  delay(100);
  digitalWrite(Pin_Reset, HIGH);
  delay(100);
  digitalWrite(Pin_Boot, HIGH);
  }

void changeBaud(catsniffer_t *cs, unsigned long newBaud){
  if(newBaud==cs->baud)
    return;
  Serial.flush();
  Serial1.flush();
  Serial.end();
  Serial1.end();
  cs->baud = newBaud;
  Serial.begin(cs->baud);
  Serial1.begin(cs->baud);
  return;
  }

void changeBand(catsniffer_t *cs, unsigned long newBand){
  if(newBand==cs->band)
    return;
  switch(newBand){
    case GIG:
      digitalWrite(CTF1,  LOW);
      digitalWrite(CTF2,  HIGH);
      digitalWrite(CTF3,  LOW);
    break;

    case SUBGIG_1: //Check pin config
      digitalWrite(CTF1,  LOW);
      digitalWrite(CTF2,  HIGH);
      digitalWrite(CTF3,  LOW);
    break;

    case SUBGIG_2: //Check pin config
      digitalWrite(CTF1,  LOW);
      digitalWrite(CTF2,  HIGH);
      digitalWrite(CTF3,  LOW);
    break;    
    }
  return;
  }

//void processCommand(){
//  
//  }
