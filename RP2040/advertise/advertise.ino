#include <base64.hpp>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

// Define advertisement data, hardcoded for now
uint8_t advData[] = {
  0x02, 0x01, 0x1A, 0x02, 0x0A, 0x0C, 0x11, 0x07,
  0x64, 0x14, 0xEA, 0xD7, 0x2F, 0xDB, 0xA3, 0xB0,
  0x59, 0x48, 0x16, 0xD4, 0x30, 0x82, 0xCB, 0x27,
  0x05, 0x03, 0x0A, 0x18, 0x0D, 0x18
};
uint8_t advDataLen = sizeof(advData) / sizeof(advData[0]);


//Define the scan response data, hardcoded for now 
uint8_t devName[] = "CatSniffer";
uint8_t scanRspData[] = {
  0x0A, 0x09, 0x43, 0x61, 0x74, 0x53, 0x6E, 0x69,
  0x66, 0x66, 0x65, 0x72
};
uint8_t scanRspDataLen = sizeof(scanRspData) / sizeof(scanRspData[0]);

//Function definitions
uint8_t cmdAdvertise(uint8_t *advData, uint8_t *scanRspData, uint8_t mode);
void cmdSend(int mode, byte* paddedAdvData, byte* paddedScanRspData);


void setup(){
Serial.begin(9600);
while (!Serial) ;

// For Testing
Serial.println("Advertisement Data is:");
for(int i = 0; i < advDataLen; i++)
{
  Serial.print(advData[i],HEX);
  Serial.print(" ");
}
Serial.print("\n");

//delay(5000);
Serial.println("Scan Response Data is:");
for(int i = 0; i < scanRspDataLen; i++)
{
  Serial.print(scanRspData[i]); //Add , HEX again if needed
  Serial.print(" ");
}
  Serial.print("\n");
//


uint8_t error=cmdAdvertise(advData,scanRspData,0);

}



void loop() {
  Serial1.write("FxwAHgIBGgIKDBEHZBTq1y/bo7BZSBbUMILLJwUDChgNGAAMCwlDYXRTbmlmZmVyAAAAAAAAAAAAAAAAAAAAAAAAAA==\r\n");
}



uint8_t cmdAdvertise(uint8_t *advData, uint8_t *scanRspData, uint8_t mode) {
    if (advDataLen > 31) {
        return -1;// Error: advData too long
    }

    if (scanRspDataLen > 31) {
        return -2;// Error: scanRspData too long
    }

    if (mode != 0 && mode != 2 && mode != 3) {
        return -3;// Error: Mode must be 0 (connectable), 2 (non-connectable), or 3 (scannable)
    }
    
    uint8_t paddedAdvData[31];
    uint8_t paddedScanRspData[31];

    paddedAdvData[0] = (uint8_t)advDataLen;
    memcpy(paddedAdvData, (const void*)advData, advDataLen);
    memset(paddedAdvData + advDataLen, 0, 31 - advDataLen - 1);

    paddedScanRspData[0] = (uint8_t)scanRspDataLen;
    memcpy(paddedScanRspData, (const void*)scanRspData, scanRspDataLen);
    memset(paddedScanRspData+ scanRspDataLen, 0, 31 - scanRspDataLen - 1);

    // For Testing
    Serial.println("Padded Advertisement Data is:");
    for(int i = 0; i < 31; i++)
    {
      Serial.print(paddedAdvData[i],HEX);
      Serial.print(" ");

    }
      Serial.print("\n");

    Serial.println("Padded Scan Response Data is:");
    for(int i = 0; i < 31; i++)
    {
      Serial.print(paddedScanRspData[i],HEX);
      Serial.print(" ");
    }
    Serial.print("\n");
    //
    cmdSend(mode,paddedAdvData,paddedScanRspData);

    return 0;
}


void cmdSend(int mode, byte* paddedAdvData, byte* paddedScanRspData) {
    // Create a byte array to hold the command
    byte cmdByteList[1 + 1 + 31 + 31];
    
    // Assign b0 to the first byte of cmd
    cmdByteList[0] = 0X1C;
    
    // Copy mode byte to cmd
    cmdByteList[1] = mode;
    
    // Copy paddedAdvData into cmd starting from index 2
    for (int i = 0; i < 31; i++) {
        cmdByteList[i + 2] = paddedAdvData[i];
    }
    
    // Copy paddedScanRspData into cmd starting from index 2 + advDataLength
    for (int i = 0; i < 31; i++) {
        cmdByteList[i + 2 + 31] = paddedScanRspData[i];
    }
    // For Testing
    Serial.println("Command Byte List Data is:");
    for(int i = 0; i < 64; i++)
    {
      Serial.print(cmdByteList[i],HEX);
      Serial.print(" ");
    }
      Serial.print("\n");
    //
    uint8_t cmdByteListLen = sizeof(cmdByteList) / sizeof(cmdByteList[0]);

    int b0=(cmdByteListLen+3)/3; //Create the valuo for cmd[0]'b' which is the lenght of the command 
    byte cmd[cmdByteListLen+1]; //Create the command array with lenght of the byte list + 1 for b0

    cmd[0]=b0; //Make b0 the value for cmd[0]

    for (int i = 0; i < cmdByteListLen; i++) { //Copy the rest of the command data as 
    cmd[i+1] = cmdByteList[i];
    }

    int cmdLen =  sizeof(cmd) / sizeof(cmd[0]); //Get the cmd length

    //For testing only
    Serial.println("Command Data is:");
    for(int i = 0; i < cmdLen; i++)
    {
      Serial.print(cmd[i],HEX);
      Serial.print(" ");
    }
      Serial.print("\n");
    //
    
    /*
    unsigned char msg[cmdLen+1];// Prepare a byte array to hold the encoded message
    unsigned int msgLen = encode_base64(cmd,cmdLen,msg); //Create a variable to hold the length of the msg and encode the msg

    //For testing only
    Serial.println("MSG Data is:");
    for(int i = 0; i < msgLen; i++)
    {
      Serial.print(msg[i],HEX);
      Serial.print(" ");
    }
    Serial.print("\n");
    //

    
     Append "\r\n" to encoded_msg
    encoded_msg[encoded_length] = '\r';
    encoded_msg[encoded_length + 1] = '\n';
    encoded_length += 2;
    */

    char *encodedMsg = "FxwAHgIBGgIKDBEHZBTq1y/bo7BZSBbUMILLJwUDChgNGAAMCwlDYXRTbmlmZmVyAAAAAAAAAAAAAAAAAAAAAAAAAA==\r\n";
     int encodedMsgLength = strlen(encodedMsg);
    // Write the encoded message to serial
    //Serial1.write(encodedMsg, encodedMsgLength);
    
    
}


