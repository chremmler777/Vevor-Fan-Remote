#include <ELECHOUSE_CC1101_SRC_DRV.h>
#include <RCSwitch.h>
#include "config.h"

RCSwitch rc = RCSwitch();

const int CODE_BITS  = 32;
const int CODE_PROTO = 2;
const int CODE_PULSE = 498;
const int MAX_BTN    = 24;

struct Btn { char name[12]; unsigned long code; bool used; };
Btn buttons[MAX_BTN];
int nbtn = 0;

bool   learning = false;
String learnName;
String buf;

void addBtn(const char* n, unsigned long c) {
  if (nbtn >= MAX_BTN) return;
  strncpy(buttons[nbtn].name, n, 11);
  buttons[nbtn].name[11] = 0;
  buttons[nbtn].code = c;
  buttons[nbtn].used = true;
  nbtn++;
}

int findBtn(const String& n) {
  for (int i = 0; i < nbtn; i++)
    if (buttons[i].used && n == buttons[i].name) return i;
  return -1;
}

void sendCode(unsigned long code) {
  rc.disableReceive();
  ELECHOUSE_cc1101.SetTx();
  rc.enableTransmit(PIN_GDO0);
  rc.setProtocol(CODE_PROTO);
  rc.setPulseLength(CODE_PULSE);
  rc.send(code, CODE_BITS);
  rc.disableTransmit();
  ELECHOUSE_cc1101.SetRx();
  rc.enableReceive(PIN_GDO0);
}

void listButtons() {
  Serial.println(F("stored buttons:"));
  for (int i = 0; i < nbtn; i++)
    if (buttons[i].used) {
      Serial.print("  ");
      Serial.print(buttons[i].name);
      Serial.print(" = ");
      Serial.println(buttons[i].code);
    }
}

void help() {
  Serial.println(F("commands:  <name> = send | list | help | learn <name>"));
}

void setup() {
  Serial.begin(115200);
  delay(2000);
  Serial.println(F("\nFan remote -- sniff + replay + learn"));

  ELECHOUSE_cc1101.setSpiPin(PIN_SCK, PIN_MISO, PIN_MOSI, PIN_CS);
  ELECHOUSE_cc1101.setGDO0(PIN_GDO0);
  ELECHOUSE_cc1101.Init();
  ELECHOUSE_cc1101.setModulation(2);
  ELECHOUSE_cc1101.setMHZ(DEFAULT_FREQ_MHZ);
  ELECHOUSE_cc1101.setPktFormat(3);
  ELECHOUSE_cc1101.SetRx();
  rc.enableReceive(PIN_GDO0);

  // captured fixed codes (fan ignores the low-byte counter)
  addBtn("off",   1689517572UL);   // 0x64B3FE04
  addBtn("1",     1689516028UL);   // speed 1 / on
  addBtn("2",     1689515877UL);
  addBtn("3",     1689515743UL);
  addBtn("4",     1689515592UL);
  addBtn("5",     1689515442UL);
  addBtn("6",     1689515307UL);
  addBtn("rev",   1689515157UL);   // reverse
  addBtn("wind",  1689517513UL);   // wind / breeze mode
  addBtn("light", 1689516733UL);   // light on/off
  addBtn("dim-",  1689517273UL);   // dimmer down
  addBtn("dim+",  1689517003UL);   // dimmer up
  addBtn("1hoff", 1689517753UL);   // 1-hour off timer
  addBtn("2hoff", 1689516582UL);   // 2-hour off timer
  addBtn("4hoff", 1689516177UL);   // 4-hour off timer
  addBtn("8hoff", 1689516417UL);   // 8-hour off timer

  help();
  listButtons();
}

void handleReceived() {
  if (!rc.available()) return;
  unsigned long v = rc.getReceivedValue();
  rc.resetAvailable();
  if (v == 0) return;

  if (learning) {
    int idx = findBtn(learnName);
    if (idx < 0 && nbtn < MAX_BTN) {
      idx = nbtn++;
      strncpy(buttons[idx].name, learnName.c_str(), 11);
      buttons[idx].name[11] = 0;
      buttons[idx].used = true;
    }
    if (idx >= 0) {
      buttons[idx].code = v;
      Serial.print(F("learned '"));
      Serial.print(learnName);
      Serial.print(F("' = "));
      Serial.println(v);
    }
    learning = false;
    return;
  }

  // passive sniff (dedup rapid repeats of the same code)
  static unsigned long last = 0, lastMs = 0;
  if (v == last && millis() - lastMs < 500) { lastMs = millis(); return; }
  last = v; lastMs = millis();
  Serial.print(F("[rx] "));
  Serial.println(v);
}

void handleLine(String line) {
  line.trim();
  line.toLowerCase();   // button names are case-insensitive
  if (!line.length()) return;

  if (line == "list") { listButtons(); return; }
  if (line == "help") { help(); return; }
  if (line.startsWith("learn ")) {
    learnName = line.substring(6);
    learnName.trim();
    if (!learnName.length()) { Serial.println(F("usage: learn <name>")); return; }
    learning = true;
    Serial.print(F("learning '"));
    Serial.print(learnName);
    Serial.println(F("' -- press that button on the real remote now"));
    return;
  }

  int idx = findBtn(line);
  if (idx >= 0) {
    Serial.print(F("-> sending "));
    Serial.println(buttons[idx].name);
    sendCode(buttons[idx].code);
  } else {
    Serial.print(F("unknown: "));
    Serial.println(line);
    help();
  }
}

void loop() {
  handleReceived();
  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\n' || c == '\r') { handleLine(buf); buf = ""; }
    else buf += c;
  }
}
