#include "Catsniffer.h"

CatSerCommand CatCMDHandler;

static catsniffer_t cs_context;

static const uint8_t commandID[5] = { 0xC3, 0xB1, 0xC3, 0xBF, 0x3C };
static uint8_t cmdCheck[5] = { 0 };
static uint8_t cmdCounter = 0;
static bool cmdRecognized = 0;
static String cmdString = "";
static int ledsIdx = 0;

uint8_t LEDs[3] = { LED1, LED2, LED3 };
static void catsnifferPassProcessCommand(String *cmd);

void catsnifferButtonsConfigure(void)
{
	pinMode(PIN_BUTTON, INPUT_PULLUP);
	pinMode(PIN_BOOT, INPUT_PULLUP);
	pinMode(PIN_RESET, OUTPUT);
	pinMode(PIN_RESET_VIEWER, INPUT);
}

void catsnifferLedsConfigure(void)
{
	pinMode(LED1, OUTPUT);
	pinMode(LED2, OUTPUT);
	pinMode(LED3, OUTPUT);
}

void catsnifferCtfConfigure(void)
{
	pinMode(CTF1, OUTPUT);
	pinMode(CTF2, OUTPUT);
	pinMode(CTF3, OUTPUT);
}

void catsnifferCC1352Reset(void)
{
	digitalWrite(PIN_RESET, LOW);
	delay(100);
	digitalWrite(PIN_RESET, HIGH);
	delay(100);
}

void catsnifferCC1352Boot(void)
{
	pinMode(PIN_BOOT, OUTPUT);
	digitalWrite(PIN_BOOT, LOW);
	delay(100);
	catsnifferCC1352Reset();
}

void catsnifferjTAGBoot(void)
{
	for (int i = JTAG_PIN_START; i < JTAG_PIN_END; i++) {
		pinMode(i, INPUT);
	}
}

void catsnifferSerial1ChangeBaudrate(catsniffer_t *cs, unsigned long newBaud)
{
	Serial.flush();
	Serial.end();
	Serial.begin(cs->baud);
}

void catsnifferSerial2ChangeBaudrate(catsniffer_t *cs, unsigned long newBaud)
{
	Serial1.flush();
	Serial1.end();
	Serial1.begin(cs->baud);
}

void catsnifferSerialChangeBaudrate(catsniffer_t *cs, unsigned long newBaud)
{
	if (newBaud == cs->baud)
		return;

	cs->baud = newBaud;

	catsnifferSerial1ChangeBaudrate(cs, newBaud);
	catsnifferSerial2ChangeBaudrate(cs, newBaud);
}

void catsnifferRFChangeBand(catsniffer_t *cs, catsniffer_band_t newBand)
{
	if (newBand == cs->band)
		return;

	switch (newBand) {
	case GIG:
		digitalWrite(CTF1, LOW);
		digitalWrite(CTF2, HIGH);
		digitalWrite(CTF3, LOW);
		break;
	case SUBGIG_1: // Sub-ghz CC1352
		digitalWrite(CTF1, LOW);
		digitalWrite(CTF2, LOW);
		digitalWrite(CTF3, HIGH);
		break;
	case SUBGIG_2: // LoRa
		digitalWrite(CTF1, HIGH);
		digitalWrite(CTF2, LOW);
		digitalWrite(CTF3, LOW);
		break;
	default:
		break;
	}
}

void catsnifferChangeMode(catsniffer_t *cs, catsniffer_mode_t newMode)
{
	if (cs->mode == newMode)
		return;

	cs->mode = newMode;

	if (cs->mode == BOOT) {
		cs->led_interval = 200;
		catsnifferCC1352Boot();
		catsnifferCC1352Reset();
		delay(200);
		catsnifferSerialChangeBaudrate(cs, MODE_BOOT_BAUDRATE);
	}

	if (cs->mode == PASSTRHOUGH) {
		// Update boot state
		digitalWrite(PIN_BOOT, HIGH);
		catsnifferCC1352Reset();
		cs->led_interval = 1000;
		catsnifferSerialChangeBaudrate(cs, MODE_PASS_BAUDRATE);
	}

	if (cs->mode == LORA) {
		catsnifferSerialChangeBaudrate(cs, MODE_LORA_BAUDRATE);
	}
}

static void catsnifferPassProcessCommand(String *cmd)
{
	// ñÿ<Payload>ÿñ Catsnifffer Commands
	cmd->remove(0, 1);
	cmd->remove(cmd->indexOf(">ÿñ"), 5);
	if ("lora" == *cmd) {
		catsnifferChangeMode(&cs_context, LORA);
		Serial.println("LORA");
		digitalWrite(LED1, 0);
		digitalWrite(LED2, 1);
		digitalWrite(LED3, 1);
	}
	if ("boot" == *cmd) {
		catsnifferChangeMode(&cs_context, BOOT);
		Serial.println("BOOT");
		digitalWrite(LED1, 0);
		digitalWrite(LED2, 0);
		digitalWrite(LED3, cs_context.mode);
	}
	if ("exit" == *cmd) {
		catsnifferChangeMode(&cs_context, PASSTRHOUGH);
		Serial.println("PASSTRHOUGH");
		digitalWrite(LED1, 0);
		digitalWrite(LED2, 0);
		digitalWrite(LED3, 0);
	}
	if ("version" == *cmd) {
		Serial.print("Version: ");
		Serial.println(LIB_VERSION);
	}

	if ("band1" == *cmd) {
		catsnifferRFChangeBand(&cs_context, GIG);
		Serial.println("2.4Ghz Band");
		digitalWrite(LED1, 0);
		digitalWrite(LED2, 0);
		digitalWrite(LED3, 0);
	}
	if ("band2" == *cmd) {
		catsnifferRFChangeBand(&cs_context, SUBGIG_1);
		Serial.println("SUB-Ghz Band");
		digitalWrite(LED1, 0);
		digitalWrite(LED2, 0);
		digitalWrite(LED3, 0);
	}
	if ("band3" == *cmd) {
		catsnifferRFChangeBand(&cs_context, SUBGIG_2);
		Serial.println("LoRa Band");
		digitalWrite(LED1, 0);
		digitalWrite(LED2, 0);
		digitalWrite(LED3, 0);
	}
}

void catsnifferUpdateLedsAnimation(void)
{
	if (millis() - cs_context.previousMillis > cs_context.led_interval) {
		cs_context.previousMillis = millis();
		if (cs_context.mode) {
			digitalWrite(LEDs[ledsIdx],
				     !digitalRead(LEDs[ledsIdx]));
			ledsIdx++;
			if (ledsIdx > 2)
				ledsIdx = 0;
		} else {
			digitalWrite(LED3, !digitalRead(LED3));
		}
	}
}

void catsnifferResetPasstrhough(void)
{
	catsnifferChangeMode(&cs_context, PASSTRHOUGH);
}

void catsnifferPassCommandBegin(void)
{
	// Initial Buttons
	catsnifferButtonsConfigure();
	digitalWrite(PIN_RESET, HIGH);
	// Configure PINS
	catsnifferLedsConfigure();
	catsnifferCtfConfigure();
	catsnifferjTAGBoot();
	// Check mode
	if (!digitalRead(PIN_BOOT)) {
		cs_context.led_interval = 200;
		cs_context.baud = MODE_BOOT_BAUDRATE;
		cs_context.mode = BOOT;
	} else {
		cs_context.led_interval = 1000;
		cs_context.baud = MODE_PASS_BAUDRATE;
		cs_context.mode = PASSTRHOUGH;
	}
	while (!digitalRead(PIN_BOOT))
		;

	Serial.begin(cs_context.baud);
	Serial1.begin(cs_context.baud);

	catsnifferCC1352Reset();

	if (cs_context.mode == BOOT) {
		catsnifferCC1352Boot();
	}

	if (cs_context.mode == PASSTRHOUGH) {
		// Switch Radio for 2.4Ghz BLE by default can be changed on the
		// fly
		catsnifferRFChangeBand(&cs_context, GIG);
	}

	digitalWrite(LED1, 0);
	digitalWrite(LED2, 0);
	digitalWrite(LED3, cs_context.mode);

	CatCMDHandler.addCommand("<pass>", catsnifferResetPasstrhough);
}

void catsnifferCommandProcess(void)
{
	// LoRa Handler
	if (cs_context.mode == LORA) {
		CatCMDHandler.readSerial();
		return;
	}
	// SerialPassthrough
	// USB -> RP2040
	if (Serial.available()) {
		int data = Serial.read();
		if (data == commandID[cmdCounter]) {
			cmdCounter++;
			if (cmdCounter == 5)
				cmdRecognized = 1;
		} else if (!cmdRecognized) {
			cmdCounter = 0;
		}
		if (cmdRecognized) {
			cmdString += String((char)data);
			if (cmdString.endsWith(CMD_SUFFIX)) {
				catsnifferPassProcessCommand(&cmdString);
				cmdString = "";
				cmdRecognized = 0;
			}
		} else {
			// Command not recognized, send out serial
			// Read it and send it out Serial1 (pins 0 & 1)
			Serial1.write(data);
		}
	}

	// CC1352 -> RP2040
	if (Serial1.available()) {
		Serial.write(Serial1.read());
	}

	catsnifferUpdateLedsAnimation();
}
