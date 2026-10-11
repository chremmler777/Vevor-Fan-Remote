// Universal RF sniffer -- for identifying an unknown remote before training it.
// Prints: every rc-switch decode (ANY protocol/bit length), RF activity seen via
// RSSI, and (in raw mode) the pulse timings of each burst.
// Serial commands: f <MHz> (e.g. f 315) | raw | dec | scan
// Self-contained: pins hardcoded (same as the trainer). Flashing this does NOT
// touch the trainer's saved data in flash -- reflash fan_remote afterwards.
#include <ELECHOUSE_CC1101_SRC_DRV.h>
#include <RCSwitch.h>

#define PIN_SCK  4
#define PIN_MISO 5
#define PIN_MOSI 6
#define PIN_CS   7
#define PIN_GDO0 10

RCSwitch rc = RCSwitch();
float freq = 433.92;
bool rawMode = false;
String buf;

// ---- raw pulse capture ----
const int MAXP = 400;
volatile uint16_t pulses[MAXP];
volatile int np = 0;
volatile unsigned long lastEdge = 0;
void IRAM_ATTR onEdge() {
  unsigned long now = micros();
  unsigned long d = now - lastEdge;
  lastEdge = now;
  if (d > 20000) { if (np < 20) np = 0; return; }   // long silence: start a new burst unless one is collecting
  if (np < MAXP) pulses[np++] = d > 65535 ? 65535 : d;
}

void radio(float mhz) {
  freq = mhz;
  ELECHOUSE_cc1101.setMHZ(freq);
  ELECHOUSE_cc1101.SetRx();
  Serial.print("listening @ "); Serial.print(freq, 2); Serial.println(" MHz");
}
void setRaw(bool on) {
  rawMode = on;
  rc.disableReceive();
  detachInterrupt(digitalPinToInterrupt(PIN_GDO0));
  if (on) { np = 0; attachInterrupt(digitalPinToInterrupt(PIN_GDO0), onEdge, CHANGE); }
  else rc.enableReceive(PIN_GDO0);
  Serial.println(on ? "mode: RAW pulse timings" : "mode: rc-switch decode");
}

void setup() {
  Serial.begin(115200);
  delay(1500);
  Serial.println("\nRF sniffer -- commands: f <MHz> | raw | dec | scan");
  ELECHOUSE_cc1101.setSpiPin(PIN_SCK, PIN_MISO, PIN_MOSI, PIN_CS);
  ELECHOUSE_cc1101.setGDO0(PIN_GDO0);
  ELECHOUSE_cc1101.Init();
  Serial.print("cc1101 ok: "); Serial.println(ELECHOUSE_cc1101.getCC1101() ? "yes" : "NO");
  ELECHOUSE_cc1101.setModulation(2);
  ELECHOUSE_cc1101.setPktFormat(3);
  radio(433.92);
  rc.enableReceive(PIN_GDO0);
}

void scan() {   // peak RSSI per frequency for ~0.5s each -- press & hold a button while it runs
  const float fs[] = {300.0, 303.87, 310.0, 315.0, 318.0, 390.0, 418.0, 433.42, 433.92, 434.42};
  Serial.println("scan: HOLD a remote button now...");
  for (float f : fs) {
    ELECHOUSE_cc1101.setMHZ(f); ELECHOUSE_cc1101.SetRx(); delay(5);
    int peak = -200;
    unsigned long t = millis();
    while (millis() - t < 500) { int r = ELECHOUSE_cc1101.getRssi(); if (r > peak) peak = r; }
    Serial.print("  "); Serial.print(f, 2); Serial.print(" MHz  peak rssi "); Serial.println(peak);
  }
  radio(freq);
}

void loop() {
  if (!rawMode && rc.available()) {
    Serial.print("[dec] proto "); Serial.print(rc.getReceivedProtocol());
    Serial.print("  bits "); Serial.print(rc.getReceivedBitlength());
    Serial.print("  pulse "); Serial.print(rc.getReceivedDelay());
    Serial.print("us  value "); Serial.print(rc.getReceivedValue());
    Serial.print("  (0x"); Serial.print(rc.getReceivedValue(), HEX); Serial.println(")");
    rc.resetAvailable();
  }
  if (rawMode && np > 0 && micros() - lastEdge > 30000) {   // burst ended
    int n = np;
    if (n >= 20) {
      Serial.print("[raw] "); Serial.print(n); Serial.print(" pulses:");
      for (int i = 0; i < n; i++) { Serial.print(' '); Serial.print(pulses[i]); }
      Serial.println();
    }
    np = 0;
  }
  static unsigned long lastAct = 0;
  int r = ELECHOUSE_cc1101.getRssi();
  if (r > -65 && millis() - lastAct > 300) {
    lastAct = millis();
    Serial.print("[rf] activity rssi "); Serial.println(r);
  }
  while (Serial.available()) {
    char c = Serial.read();
    if (c != '\n' && c != '\r') { buf += c; continue; }
    buf.trim();
    if (buf.startsWith("f ")) radio(buf.substring(2).toFloat());
    else if (buf == "raw") setRaw(true);
    else if (buf == "dec") setRaw(false);
    else if (buf == "scan") scan();
    buf = "";
  }
}
