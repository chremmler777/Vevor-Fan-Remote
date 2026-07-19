// Dim-hold monitor -- prints EVERY received frame with the gap since the last.
// HOLD a button (esp. dim+) for a few seconds and watch the raw stream.
// Self-contained: pins hardcoded (same as the trainer). Flashing this does NOT
// touch the trainer's saved data in flash.
#include <ELECHOUSE_CC1101_SRC_DRV.h>
#include <RCSwitch.h>

#define PIN_SCK  4
#define PIN_MISO 5
#define PIN_MOSI 6
#define PIN_CS   7
#define PIN_GDO0 10

RCSwitch rc = RCSwitch();
unsigned long lastMs = 0;
unsigned long lastCode = 0;

void setup() {
  Serial.begin(115200);
  delay(1500);
  Serial.println("\nDim-hold monitor -- HOLD a button, watch the stream");
  ELECHOUSE_cc1101.setSpiPin(PIN_SCK, PIN_MISO, PIN_MOSI, PIN_CS);
  ELECHOUSE_cc1101.setGDO0(PIN_GDO0);
  ELECHOUSE_cc1101.Init();
  ELECHOUSE_cc1101.setModulation(2);
  ELECHOUSE_cc1101.setMHZ(433.92);
  ELECHOUSE_cc1101.setPktFormat(3);
  ELECHOUSE_cc1101.SetRx();
  rc.enableReceive(PIN_GDO0);
}

void loop() {
  if (rc.available()) {
    unsigned long v = rc.getReceivedValue();
    unsigned int bits = rc.getReceivedBitlength();
    unsigned long now = millis();
    unsigned long gap = lastMs ? now - lastMs : 0;
    long delta = (long)v - (long)lastCode;   // how the code changed vs the previous frame
    lastMs = now;
    lastCode = v;
    rc.resetAvailable();
    Serial.print("t+"); Serial.print(gap);
    Serial.print("ms  bits="); Serial.print(bits);
    Serial.print("  code="); Serial.print(v);
    Serial.print("  d="); Serial.println(delta);
  }
}
