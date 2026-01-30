unsigned long baud = 9600;

void setup() {
  //Begin Serial ports
  Serial.begin(baud);
  Serial1.begin(baud);
}

void loop() {
  //SerialPassthrough

  Serial1.write("Hola soy Nano1");
  delay(200);
  

  if (Serial1.available()) {     // If anything comes in Serial1 (pins 0 & 1)
    Serial.write(Serial1.read());   // read it and send it out Serial (USB)
  }
}
