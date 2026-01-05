/*
  SerialPassthrough - Use tool to flash the CC1352 module

  Andres Sabas @ Electronic Cats
  Eduardo Contreras @ Electronic Cats
  Original Creation Date: Jan 16, 2024

  This code is beerware; if you see me (or any other Electronic Cats
  member) at the local, and you've found our code helpful,
  please buy us a round!
  Distributed as-is; no warranty is given.
  
*/
#include <Catsniffer.h>

static void help(){
  char *arg = CatCMDHandler.next(); 
  Serial.println("SerialCommands");
  if (arg != NULL){
    Serial.print("Args: ");
    Serial.println(arg);
  }
}

static void showLoRaCommands(){
  CatCMDHandler.showCommands();
}

void setup() {
  CatCMDHandler.addCommand("help", help);
  CatCMDHandler.addCommand("cmd", showLoRaCommands);

  catsnifferPassCommandBegin();
  
}

void loop() {
  catsnifferCommandProcess();
}