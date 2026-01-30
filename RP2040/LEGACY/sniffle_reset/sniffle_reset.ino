#include "SerialPassthroughwithboot.h"
unsigned long baud = 2000000;

  byte sync_command[] = {'@', '@', '@', '@','@','@','@','@','\r', '\n'};
  byte reset_command[] = {'A', 'R', 'c', '=', '\r', '\n'};
  byte reset2_command[] = {'A', 'h', 'h', 'x', 'Z', 'e','N','v','\r','\n'};
  byte firmware_v_command1[] = {'A', 'R', 'h', 'A', '\r', '\n'};
  byte firmware_v_command2[] = {'A', 'S', 'Q', '=', '\r', '\n'};


void setup() {

  /*
  pinMode(Pin_Button, INPUT_PULLUP);
  pinMode(Pin_Boot, INPUT_PULLUP);
  pinMode(Pin_Reset, OUTPUT);
  pinMode(Pin_Reset_Viewer, INPUT);
  digitalWrite(Pin_Reset, HIGH);
  
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
*/
  Serial.begin(baud);
  Serial1.begin(baud);



/*
  // Outer loop to repeat the command 5 times
  for (int j = 0; j < 5; j++) {
    // Inner loop to send each byte of the command
    for (int i = 0; i < sizeof(reset_command); i++) {
      Serial.write(reset_command[i]);
    }
  }

    for (int i = 0; i < sizeof(sync_command); i++) {
    Serial1.write(firmware_v_command1[i]);
  }
*/
while(!Serial);


      for (int i = 0; i < sizeof(sync_command); i++) {
    Serial1.write(firmware_v_command1[i]);
    Serial.write(firmware_v_command1[i]);
  }

  
    for (int i = 0; i < sizeof(firmware_v_command2); i++) {
    Serial1.write(firmware_v_command2[i]);
    Serial.write(firmware_v_command2[i]);
  }



Serial1.print("ARhA\r\n");
Serial.print("ARhA\r\n");

Serial1.print("BBAl1r6JiqBVVVUA\r\n");
Serial.print("BBAl1r6JiqBVVVUA\r\n");

Serial1.print("ARBE\r\n");
Serial.print("ARBE\r\n");

Serial1.print("ARUB\r\n");
Serial.print("ARUB\r\n");

Serial1.print("ARKA\r\n");
Serial.print("ARKA\r\n");

Serial1.print("ARM=\r\n");
Serial.print("ARM=\r\n");

Serial1.print("ARYA\r\n");
Serial.print("ARYA\r\n");

Serial1.print("AxsBAqpN56zu\r\n");
Serial.print("AxsBAqpN56zu\r\n");

Serial1.print("Ah3IAA==\r\n");
Serial.print("Ah3IAA==\r\n");

Serial1.print("AScF\r\n");
Serial.print("AScF\r\n");

Serial1.print("ASE=\r\n");
Serial.print("ASE=\r\n");

Serial1.print("Ahi6MPri\r\n");
Serial.print("Ahi6MPri\r\n");

listenForSerial1(1000); 

}



void loop() {
  


}

void listenForSerial1(unsigned long duration) {
  unsigned long startTime = millis();
  while (millis() - startTime < duration) {
    if (Serial1.available()) {
      int incomingByte = Serial1.read();
      Serial.write(incomingByte);  // Send it out to Serial (USB)
    }
  }
}

