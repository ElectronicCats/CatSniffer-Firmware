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

#define FIRMWARE_VERSION "1.0.0"
#define FIRMWARE_NAME "LoRaCAD"

uint8_t LEDs[3] = { LED1, LED2, LED3 };

SX1262 radio = new Module(17, 5, 24, 4);
SerialCommand SCmd;

typedef enum {
	CAD_FIXED,
	CAD_RANGE,
} cad_mode_t;

struct RadioContext {
	float frequency;
	int bandWidth;
	int spreadFactor;
	int codingRate;
	byte syncWord;
	int preambleLength;
	int outputPower;

	// Frequency range
	int start;
	int end;
	float step;
	cad_mode_t mode;
};

RadioContext radioCtx;
bool recivedPacket = false;
bool receiving = false;
volatile bool scanFlag = false;
const unsigned long interval = 5000; // 5 s interval to send message
unsigned long previousMillis = 0;    // will store last time message sent

void setFlag(void)
{
	scanFlag = true;
}

static void startRadioCAD()
{
	int state = radio.startChannelScan();
	if (state != RADIOLIB_ERR_NONE) {
		Serial.print(F("Failed, code "));
		Serial.println(state);
	}
}

static void configureGPIO()
{
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
}

void help()
{
	Serial.print("Firmware: ");
	Serial.println(FIRMWARE_VERSION);
	Serial.println("Available commands are:");
	Serial.print("set_freq\t- ");
	Serial.println(
		"Set the frequency in range of 150/960 MHz: Default 915");
	Serial.print("set_efreq\t- ");
	Serial.println(
		"Set the end frequency in range of 150/960 MHz: Default 915");
	Serial.print("set_sf\t\t- ");
	Serial.println("Set the spread factor. Default: 7");
	Serial.print("set_bw\t\t- ");
	Serial.println(
		"Set the bandwith value. Options: (7.8, 10.4, 15.6, 20.8, 31.25, 41.7, 62.5, 125, 250, 500) kHz: Default 125");
	Serial.print("set_cr\t\t- ");
	Serial.println("Set the coding rate. Default: 5");
	Serial.print("set_sw\t\t- ");
	Serial.println("Set the sync Word: Default: 0x12");
	Serial.print("set_pl\t\t- ");
	Serial.println("Set the preamble length: Default: 10");

	Serial.print("set_step\t- ");
	Serial.println(
		"Set the step increment for frequency range; Default 0.1");
	Serial.print("set_fixed\t- ");
	Serial.println("Set fixed frequency CAD; Default");
	Serial.print("set_range\t- ");
	Serial.println("Set Range CAD");

	Serial.println("get_config\t - Show the configuration of the radio");
	Serial.println("get_state \t - Show the state of the scann");
}

void setup()
{
	Serial.begin(115200);
	while (!Serial)
		;

	configureGPIO();

	SCmd.addCommand("set_sf", cmdSetSpreadFactor);
	SCmd.addCommand("set_bw", cmdSetBandWidth);
	SCmd.addCommand("set_cr", cmdSetCodingRate);
	SCmd.addCommand("set_sw", cmdSetSyncWord);
	SCmd.addCommand("set_pl", cmdSetPreambleLength);

	SCmd.addCommand("set_freq", cmdSetFrequency);
	SCmd.addCommand("set_efreq", cmdSetFrequencyEnd);
	SCmd.addCommand("set_step", cmdSetStep);

	SCmd.addCommand("set_fixed", cmdSetModeFixed);
	SCmd.addCommand("set_range", cmdSetModeRange);

	SCmd.addCommand("get_state", cmdGetState);
	SCmd.addCommand("get_config", cmdGetConfiguration);
	SCmd.addCommand("version", showFirmwareVersion);
	SCmd.addCommand("firmware", showFirmwareName);
	SCmd.addCommand("help", help);

	SCmd.setDefaultHandler(unrecognized);

	radioCtx.frequency = 915;
	radioCtx.bandWidth = 125;
	radioCtx.spreadFactor = 7;
	radioCtx.codingRate = 5;
	radioCtx.syncWord = 0x12;
	radioCtx.outputPower = 10;
	radioCtx.preambleLength = 10;

	radioCtx.start = radioCtx.frequency;
	radioCtx.end = radioCtx.frequency;
	radioCtx.step = 0.1;
	radioCtx.mode = CAD_FIXED;

	// initialize SX1262 with default settings
	Serial.print(F("[SX1262] Initializing ... "));
	int state = radio.begin(radioCtx.frequency, radioCtx.bandWidth,
				radioCtx.spreadFactor, radioCtx.codingRate,
				radioCtx.syncWord, radioCtx.outputPower,
				radioCtx.preambleLength, 0, false);
	if (state == RADIOLIB_ERR_NONE) {
		Serial.println(F("success!"));
	} else {
		Serial.print(F("failed, code "));
		Serial.println(state);
		while (true) {
			delay(10);
		}
	}

	radio.setRfSwitchPins(21, 20);
	radio.setDio1Action(setFlag);

	// start scanning the channel
	Serial.println(F("[SX1262] Starting scan for LoRa preamble"));
	startRadioCAD();
}

void showFirmwareVersion()
{
	Serial.println(String(FIRMWARE_VERSION));
}
void showFirmwareName()
{
	Serial.println(String(FIRMWARE_NAME));
}

void cmdSetFrequency()
{
	char *arg;
	arg = SCmd.next();
	if (arg != NULL) {
		float tmp_value = atof(arg);
		if (radio.setFrequency(tmp_value) ==
		    RADIOLIB_ERR_INVALID_FREQUENCY) {
			Serial.println(F(
				"Selected frequency is invalid for this module!"));
			return;
		}
		radioCtx.start = tmp_value;
		radioCtx.frequency = tmp_value;
		Serial.println("Frequency set to " + String(tmp_value) +
			       " MHz");
		startRadioCAD();
	}
}

void cmdSetStep()
{
	char *arg;
	arg = SCmd.next();
	if (arg != NULL) {
		float tmp_value = atof(arg);
		if (tmp_value < 0 || tmp_value > 10) {
			Serial.println(
				"Step out of parameters, please use a value between 0 and 10 MHz");
			return;
		}
		radioCtx.step = tmp_value;
		Serial.println("Frequency step set to " + String(tmp_value) +
			       " MHz");
	}
}

void cmdSetFrequencyEnd()
{
	char *arg;
	arg = SCmd.next();
	if (arg != NULL) {
		float tmp_value = atof(arg);
		if (tmp_value < 150 || tmp_value > 960) {
			Serial.println(
				"Frequency out of parameters, please use a value between 150 and 960 MHz");
			return;
		}
		if (tmp_value < radioCtx.frequency) {
			Serial.print(
				"Frequency out of parameters, please use a value greater thatn: ");
			Serial.println(String(radioCtx.frequency) + " MHz");
			return;
		}
		radioCtx.end = tmp_value;
		Serial.println("Frequency end set to " + String(tmp_value) +
			       " MHz");
		startRadioCAD();
	}
}

void cmdSetCodingRate()
{
	char *arg;
	arg = SCmd.next();
	if (arg != NULL) {
		int tmp_value = atoi(arg);
		if (radio.setCodingRate(tmp_value) ==
		    RADIOLIB_ERR_INVALID_CODING_RATE) {
			Serial.println(F(
				"Selected coding rate is invalid for this module!"));
			return;
		}
		Serial.println("Coding Rate set to " + String(tmp_value));
		radioCtx.codingRate = tmp_value;
	}
}

void cmdSetSpreadFactor()
{
	char *arg;
	arg = SCmd.next();
	if (arg != NULL) {
		int tmp_value = atoi(arg);
		if (radio.setSpreadingFactor(tmp_value) ==
		    RADIOLIB_ERR_INVALID_SPREADING_FACTOR) {
			Serial.println(F(
				"Selected spread factor is invalid for this module!"));
			return;
		}
		Serial.println("Spreadfactor set to " + String(tmp_value));
		radioCtx.spreadFactor = tmp_value;
	}
}

void cmdSetBandWidth()
{
	char *arg;
	arg = SCmd.next();
	if (arg != NULL) {
		float tmp_value = atof(arg);
		if (radio.setBandwidth(tmp_value) ==
		    RADIOLIB_ERR_INVALID_BANDWIDTH) {
			Serial.println(F(
				"Selected bandwidth is invalid for this module!"));
			return;
		}
		Serial.println("Bandwidth set to " + String(tmp_value) +
			       " kHz");
		radioCtx.bandWidth = tmp_value;
	}
}

void cmdSetPreambleLength()
{
	char *arg;
	arg = SCmd.next();
	if (arg != NULL) {
		int tmp_value = atoi(arg);
		if (radio.setPreambleLength(tmp_value) ==
		    RADIOLIB_ERR_INVALID_PREAMBLE_LENGTH) {
			Serial.println(F(
				"Selected preamble length is invalid for this module!"));
			return;
		}
		Serial.println("Preamble Length set to " + String(tmp_value));
		radioCtx.preambleLength = tmp_value;
	}
}

void cmdSetSyncWord()
{
	char *arg;
	byte data;
	arg = SCmd.next();
	if (arg != NULL) {
		char *endptr;
		long val = strtol(arg, &endptr, 0);

		if (*endptr == '\0' && val >= 0 && val <= 0xFF) {
			data = (byte)val;
			if (radio.setSyncWord(data) != RADIOLIB_ERR_NONE) {
				Serial.println(F("Unable to set sync word!"));
				return;
			}
			radioCtx.syncWord = data;
		} else {
			Serial.println(F(
				"Invalid sync word. Use a hexadecimal byte (e.g. 2B or 0x2B)"));
			return;
		}
	}
}

void cmdSetModeFixed()
{
	Serial.println("Mode changed to Fixed");
	radioCtx.mode = CAD_FIXED;
	resetScan();
	digitalWrite(LED1, 1);
	digitalWrite(LED2, 0);
	digitalWrite(LED3, 0);
}

void cmdSetModeRange()
{
	Serial.println("Mode changed to Range");
	radioCtx.mode = CAD_RANGE;
	resetScan();
	digitalWrite(LED1, 0);
	digitalWrite(LED2, 1);
	digitalWrite(LED3, 0);
}

void cmdGetState()
{
	Serial.println("Mode:\t");
	Serial.print(radioCtx.mode);
	Serial.println(radioCtx.mode ? "- Range" : "- Fixed");
	Serial.print("Start:\t");
	Serial.println(radioCtx.start);
	Serial.print("End:\t");
	Serial.println(radioCtx.end);
	Serial.print("Step:\t");
	Serial.println(radioCtx.step);
}

void cmdGetConfiguration()
{
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
	// State
	Serial.println("Frequency range: ");
	cmdGetState();
}

void unrecognized(const char *command)
{
	Serial.println(
		"Command not found, type help to get the valid commands");
}

static void showPacketDetails()
{
	// DIO triggered while reception is ongoing
	// that means we got a packet

	// you can read received data as an Arduino String
	uint16_t packetLen = radio.getPacketLength();
	byte bytePacket[packetLen];
	int state = radio.readData(bytePacket, packetLen);

	if (state == RADIOLIB_ERR_NONE) {
		recivedPacket = true;
		digitalWrite(LED3, 1);
		// packet was successfully received
		Serial.println(F("[SX1262] Received packet!"));

		// print data of the packet
		Serial.print(F("[SX1262] Data:\t\t"));
		Serial.write(bytePacket, packetLen);
		Serial.println();

		// print RSSI (Received Signal Strength Indicator)
		Serial.print(F("[SX1262] RSSI:\t\t"));
		Serial.print(radio.getRSSI());
		Serial.println(F(" dBm"));

		// print SNR (Signal-to-Noise Ratio)
		Serial.print(F("[SX1262] SNR:\t\t"));
		Serial.print(radio.getSNR());
		Serial.println(F(" dB"));

		// print frequency error
		Serial.print(F("[SX1262] Freq Error:\t"));
		Serial.print(radio.getFrequencyError());
		Serial.println(F(" Hz"));

		Serial.print(F("[SX1262] Frequency:\t"));
		Serial.print(radioCtx.frequency);
		Serial.println(F(" Hz"));

	} else {
		// some other error occurred
		Serial.print(F("[SX1262] Failed, code "));
		Serial.println(state);
	}
	digitalWrite(LED3, 0);
}

void resetScan()
{
	if (receiving)
		return;

	receiving = false;
	scanFlag = false;
	recivedPacket = false;
	digitalWrite(LED3, 0);

	if (radioCtx.mode == CAD_RANGE) {
		radioCtx.frequency += radioCtx.step;
		if (radioCtx.frequency > radioCtx.end) {
			radioCtx.frequency = radioCtx.start;
		}

		int state = radio.setFrequency(radioCtx.frequency);
		if (state != RADIOLIB_ERR_NONE) {
			Serial.print(F("[SX1262] Failed, code "));
			Serial.println(state);
		}
	}

	startRadioCAD();
}

void loop()
{
	SCmd.readSerial();

	if (scanFlag) {
		SCmd.readSerial();
		int state = RADIOLIB_ERR_NONE;

		// reset flag
		scanFlag = false;

		// check ongoing reception
		if (receiving) {
			showPacketDetails();
			// reception is done now
			receiving = false;
			digitalWrite(LED3, 0);
		} else {
			// check CAD result
			state = radio.getChannelScanResult();

			if (state == RADIOLIB_LORA_DETECTED) {
				state = radio.startReceive();
				if (state != RADIOLIB_ERR_NONE) {
					Serial.print(
						F("[SX1262] Failed, code "));
					Serial.println(state);
				}
				// set the flag for ongoing reception
				receiving = true;
				digitalWrite(LED3, 1);
			} else if (state == RADIOLIB_CHANNEL_FREE) {
			} else {
				// some other error occurred
				Serial.print(F("[SX1262] Failed, code "));
				Serial.println(state);
			}
		}

		resetScan();
	}
	if (receiving && radioCtx.mode == CAD_RANGE) {
		if (millis() - previousMillis > interval) {
			previousMillis = millis();
			if (!recivedPacket) {
				Serial.println("No packet reset");
				receiving = false;
				recivedPacket = false;
				resetScan();
			}
		}
	}
}
