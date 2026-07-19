#include <ELECHOUSE_CC1101_SRC_DRV.h>
#include <RCSwitch.h>
#include <Preferences.h>
#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include "config.h"
#include "secrets.h"

RCSwitch rc = RCSwitch();
Preferences prefs;
WebServer server(80);
String g_lastAction = "(none)";

const int CODE_BITS  = 32;
const int CODE_PROTO = 2;
const int CODE_PULSE = 498;
const int MAX_BTN    = 40;
const int MAX_CODES  = 10;   // a button's rolling cycle is ~8 states
const int STORE_VER  = 4;    // bump if struct layout changes (forces reseed)

struct Btn {
  char fan[16];
  char name[12];
  uint8_t ncodes;
  uint8_t sendIdx;
  unsigned long codes[MAX_CODES];
  bool used;
};
Btn buttons[MAX_BTN];
int  nbtn = 0;
char currentFan[16] = "masterbedroom";

bool          learning = false;
int           learnIdx = -1;
unsigned long learnLastMs = 0;
String        buf;

// ---------- storage ----------
void saveAll() {
  prefs.begin("fanrc", false);
  prefs.putInt("ver", STORE_VER);
  prefs.putInt("nbtn", nbtn);
  prefs.putBytes("btns", buttons, sizeof(buttons));
  prefs.putString("fan", currentFan);
  prefs.end();
}
void loadAll() {
  prefs.begin("fanrc", true);
  int ver = prefs.getInt("ver", 0);
  if (ver != STORE_VER) { prefs.end(); nbtn = 0; return; }  // schema changed -> reseed
  nbtn = prefs.getInt("nbtn", 0);
  prefs.getBytes("btns", buttons, sizeof(buttons));
  String f = prefs.getString("fan", "masterbedroom");
  prefs.end();
  if (nbtn < 0 || nbtn > MAX_BTN) nbtn = 0;
  strncpy(currentFan, f.c_str(), 15); currentFan[15] = 0;
}

// ---------- button table (scoped to currentFan) ----------
int findBtn(String name) {
  name.toLowerCase();
  for (int i = 0; i < nbtn; i++)
    if (buttons[i].used && name == buttons[i].name && strcmp(buttons[i].fan, currentFan) == 0)
      return i;
  return -1;
}
int ensureBtn(String name) {
  name.toLowerCase();
  int idx = findBtn(name);
  if (idx >= 0) return idx;
  if (nbtn >= MAX_BTN) return -1;
  idx = nbtn++;
  strncpy(buttons[idx].fan, currentFan, 15); buttons[idx].fan[15] = 0;
  strncpy(buttons[idx].name, name.c_str(), 11); buttons[idx].name[11] = 0;
  buttons[idx].ncodes = 0; buttons[idx].sendIdx = 0; buttons[idx].used = true;
  return idx;
}
int ensureBtnInFan(const char* fan, String name) {
  name.toLowerCase();
  for (int i = 0; i < nbtn; i++)
    if (buttons[i].used && name == buttons[i].name && strcmp(buttons[i].fan, fan) == 0) return i;
  if (nbtn >= MAX_BTN) return -1;
  int idx = nbtn++;
  strncpy(buttons[idx].fan, fan, 15); buttons[idx].fan[15] = 0;
  strncpy(buttons[idx].name, name.c_str(), 11); buttons[idx].name[11] = 0;
  buttons[idx].ncodes = 0; buttons[idx].sendIdx = 0; buttons[idx].used = true;
  return idx;
}
bool addCodeToBtn(int idx, unsigned long code) {
  if (idx < 0) return false;
  Btn &b = buttons[idx];
  for (int i = 0; i < b.ncodes; i++) if (b.codes[i] == code) return false;
  if (b.ncodes >= MAX_CODES) return false;
  b.codes[b.ncodes++] = code;
  return true;
}
int identify(unsigned long code) {   // searches ALL fans
  for (int i = 0; i < nbtn; i++) {
    if (!buttons[i].used) continue;
    for (int j = 0; j < buttons[i].ncodes; j++)
      if (buttons[i].codes[j] == code) return i;
  }
  return -1;
}

// ---------- transmit ----------
void sendRaw(unsigned long code) {
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
void sendBtnOnce(int idx) {
  Btn &b = buttons[idx];
  if (b.ncodes == 0) return;
  unsigned long code = b.codes[b.sendIdx % b.ncodes];  // rotate through the captured states (like fresh taps)
  b.sendIdx = (b.sendIdx + 1) % b.ncodes;
  sendRaw(code);
}

// ---------- learn ----------
void startLearn(String name) {
  name.trim(); name.toLowerCase();
  if (!name.length()) { Serial.println(F("usage: learn <name>")); return; }
  if (learning) finishLearn(false);   // finish/save the previous button first
  learnIdx = ensureBtn(name);
  if (learnIdx < 0) { Serial.println(F("no room for more buttons")); return; }
  buttons[learnIdx].ncodes = 0;
  buttons[learnIdx].sendIdx = 0;
  learning = true;
  learnLastMs = millis();
  Serial.print(F("learning '")); Serial.print(currentFan); Serial.print('/'); Serial.print(name);
  Serial.println(F("': press it ~10x (or hold). auto-saves after 4s idle, or type 'done'."));
}
void finishLearn(bool manual) {
  if (!learning) { if (manual) Serial.println(F("not learning")); return; }
  learning = false;
  saveAll();
  Serial.print(F("learned '")); Serial.print(buttons[learnIdx].name);
  Serial.print(F("' = ")); Serial.print(buttons[learnIdx].ncodes);
  Serial.println(F(" code(s), saved."));
}

// ---------- ui ----------
void listButtons() {
  Serial.print(F("buttons for '")); Serial.print(currentFan); Serial.println(F("':"));
  int cnt = 0;
  for (int i = 0; i < nbtn; i++) {
    if (!buttons[i].used || strcmp(buttons[i].fan, currentFan) != 0) continue;
    cnt++;
    Serial.print("  "); Serial.print(buttons[i].name);
    Serial.print(" ("); Serial.print(buttons[i].ncodes); Serial.println(F(" codes)"));
  }
  if (cnt == 0) Serial.println(F("  (none -- use 'learn <name>')"));
}
void listFans() {
  Serial.println(F("fans:"));
  for (int i = 0; i < nbtn; i++) {
    if (!buttons[i].used) continue;
    bool firstSeen = true;
    for (int j = 0; j < i; j++)
      if (buttons[j].used && strcmp(buttons[j].fan, buttons[i].fan) == 0) { firstSeen = false; break; }
    if (!firstSeen) continue;
    int cnt = 0;
    for (int j = 0; j < nbtn; j++)
      if (buttons[j].used && strcmp(buttons[j].fan, buttons[i].fan) == 0) cnt++;
    Serial.print("  "); Serial.print(buttons[i].fan);
    Serial.print(" ("); Serial.print(cnt); Serial.print(F(" buttons)"));
    if (strcmp(buttons[i].fan, currentFan) == 0) Serial.print(F("  <-- active"));
    Serial.println();
  }
}
void setFan(String name) {
  name.trim(); name.toLowerCase();
  if (!name.length()) { Serial.print(F("active fan: ")); Serial.println(currentFan); return; }
  strncpy(currentFan, name.c_str(), 15); currentFan[15] = 0;
  saveAll();
  Serial.print(F("active fan: ")); Serial.println(currentFan);
  listButtons();
}
void help() {
  Serial.println(F("commands:"));
  Serial.println(F("  fan <name>    - select the fan to train/control (e.g. 'fan patio')"));
  Serial.println(F("  fans          - list all fans"));
  Serial.println(F("  learn <name>  - capture a button: press it ~10x or hold (auto-groups codes)"));
  Serial.println(F("  done          - finish learning now"));
  Serial.println(F("  <name>        - send a button once      | <name> <n> - send n times (e.g. 'dim+ 6')"));
  Serial.println(F("  dump          - print all codes as 'add' lines (back up / restore)"));
  Serial.println(F("  list | del <name> | clear | help"));
}

void dumpAll() {
  Serial.println(F("=== dump: paste 'add' lines back to restore, or send to Claude to bake in ==="));
  for (int i = 0; i < nbtn; i++) {
    if (!buttons[i].used) continue;
    Serial.print(F("add ")); Serial.print(buttons[i].fan); Serial.print(' '); Serial.print(buttons[i].name);
    for (int j = 0; j < buttons[i].ncodes; j++) { Serial.print(' '); Serial.print(buttons[i].codes[j]); }
    Serial.println();
  }
  Serial.println(F("=== end dump ==="));
}

void handleImport(String arg) {   // add <fan> <name> <code> [code ...]
  arg.trim();
  int p1 = arg.indexOf(' ');
  int p2 = (p1 < 0) ? -1 : arg.indexOf(' ', p1 + 1);
  if (p1 < 0 || p2 < 0) { Serial.println(F("usage: add <fan> <name> <code> [code ...]")); return; }
  String fan = arg.substring(0, p1); fan.toLowerCase();
  String name = arg.substring(p1 + 1, p2);
  String rest = arg.substring(p2 + 1); rest.trim();
  int idx = ensureBtnInFan(fan.c_str(), name);
  if (idx < 0) { Serial.println(F("no room")); return; }
  buttons[idx].ncodes = 0; buttons[idx].sendIdx = 0;
  int added = 0;
  while (rest.length()) {
    int sp = rest.indexOf(' ');
    String tok = (sp < 0) ? rest : rest.substring(0, sp);
    unsigned long code = strtoul(tok.c_str(), NULL, 10);
    if (code && addCodeToBtn(idx, code)) added++;
    if (sp < 0) break;
    rest = rest.substring(sp + 1); rest.trim();
  }
  saveAll();
  Serial.print(F("imported ")); Serial.print(fan); Serial.print('/'); Serial.print(name);
  Serial.print(F(" = ")); Serial.print(added); Serial.println(F(" code(s)"));
}

void seedDefaults() {
  int i;
  i = ensureBtnInFan("masterbedroom", "1"); addCodeToBtn(i, 1689515907UL); addCodeToBtn(i, 1689516028UL); addCodeToBtn(i, 1689516013UL); addCodeToBtn(i, 1689515998UL); addCodeToBtn(i, 1689515983UL); addCodeToBtn(i, 1689515952UL); addCodeToBtn(i, 1689515937UL); addCodeToBtn(i, 1689515922UL);
  i = ensureBtnInFan("masterbedroom", "2"); addCodeToBtn(i, 1689515877UL); addCodeToBtn(i, 1689515862UL); addCodeToBtn(i, 1689515847UL); addCodeToBtn(i, 1689515832UL); addCodeToBtn(i, 1689515817UL); addCodeToBtn(i, 1689515802UL); addCodeToBtn(i, 1689515787UL); addCodeToBtn(i, 1689515892UL);
  i = ensureBtnInFan("masterbedroom", "3"); addCodeToBtn(i, 1689515743UL); addCodeToBtn(i, 1689515712UL); addCodeToBtn(i, 1689515697UL); addCodeToBtn(i, 1689515682UL); addCodeToBtn(i, 1689515667UL); addCodeToBtn(i, 1689515652UL); addCodeToBtn(i, 1689515773UL); addCodeToBtn(i, 1689515758UL);
  i = ensureBtnInFan("masterbedroom", "4"); addCodeToBtn(i, 1689515592UL); addCodeToBtn(i, 1689515577UL); addCodeToBtn(i, 1689515562UL); addCodeToBtn(i, 1689515547UL); addCodeToBtn(i, 1689515532UL); addCodeToBtn(i, 1689515637UL); addCodeToBtn(i, 1689515622UL); addCodeToBtn(i, 1689515607UL);
  i = ensureBtnInFan("masterbedroom", "5"); addCodeToBtn(i, 1689515442UL); addCodeToBtn(i, 1689515427UL); addCodeToBtn(i, 1689515412UL); addCodeToBtn(i, 1689515397UL); addCodeToBtn(i, 1689515518UL); addCodeToBtn(i, 1689515503UL); addCodeToBtn(i, 1689515472UL); addCodeToBtn(i, 1689515457UL);
  i = ensureBtnInFan("masterbedroom", "6"); addCodeToBtn(i, 1689515322UL); addCodeToBtn(i, 1689515307UL); addCodeToBtn(i, 1689515292UL); addCodeToBtn(i, 1689515277UL); addCodeToBtn(i, 1689515382UL); addCodeToBtn(i, 1689515367UL);
  i = ensureBtnInFan("masterbedroom", "off"); addCodeToBtn(i, 1689517663UL); addCodeToBtn(i, 1689517632UL); addCodeToBtn(i, 1689517617UL); addCodeToBtn(i, 1689517602UL); addCodeToBtn(i, 1689517587UL); addCodeToBtn(i, 1689517572UL); addCodeToBtn(i, 1689517693UL); addCodeToBtn(i, 1689517678UL);
  i = ensureBtnInFan("masterbedroom", "rev"); addCodeToBtn(i, 1689515187UL); addCodeToBtn(i, 1689515172UL); addCodeToBtn(i, 1689515157UL); addCodeToBtn(i, 1689515142UL); addCodeToBtn(i, 1689515263UL); addCodeToBtn(i, 1689515232UL); addCodeToBtn(i, 1689515217UL); addCodeToBtn(i, 1689515202UL);
  i = ensureBtnInFan("masterbedroom", "home"); addCodeToBtn(i, 1689517857UL); addCodeToBtn(i, 1689517842UL); addCodeToBtn(i, 1689517827UL); addCodeToBtn(i, 1689517948UL); addCodeToBtn(i, 1689517933UL); addCodeToBtn(i, 1689517918UL); addCodeToBtn(i, 1689517903UL); addCodeToBtn(i, 1689517872UL);
  i = ensureBtnInFan("masterbedroom", "wind"); addCodeToBtn(i, 1689517483UL); addCodeToBtn(i, 1689517468UL); addCodeToBtn(i, 1689517453UL); addCodeToBtn(i, 1689517558UL); addCodeToBtn(i, 1689517543UL); addCodeToBtn(i, 1689517528UL); addCodeToBtn(i, 1689517513UL); addCodeToBtn(i, 1689517498UL);
  i = ensureBtnInFan("masterbedroom", "light"); addCodeToBtn(i, 1689516672UL); addCodeToBtn(i, 1689516793UL); addCodeToBtn(i, 1689516778UL); addCodeToBtn(i, 1689516763UL); addCodeToBtn(i, 1689516748UL); addCodeToBtn(i, 1689516733UL); addCodeToBtn(i, 1689516718UL); addCodeToBtn(i, 1689516703UL);
  i = ensureBtnInFan("masterbedroom", "dim-"); addCodeToBtn(i, 1689517198UL); addCodeToBtn(i, 1689517303UL); addCodeToBtn(i, 1689517288UL); addCodeToBtn(i, 1689517273UL); addCodeToBtn(i, 1689517258UL); addCodeToBtn(i, 1689517243UL); addCodeToBtn(i, 1689517228UL); addCodeToBtn(i, 1689517213UL);
  i = ensureBtnInFan("masterbedroom", "dim+"); addCodeToBtn(i, 1689517048UL); addCodeToBtn(i, 1689517033UL); addCodeToBtn(i, 1689517018UL); addCodeToBtn(i, 1689517003UL); addCodeToBtn(i, 1689516988UL); addCodeToBtn(i, 1689516973UL); addCodeToBtn(i, 1689516958UL); addCodeToBtn(i, 1689516943UL);
  i = ensureBtnInFan("masterbedroom", "1hoff"); addCodeToBtn(i, 1689517813UL); addCodeToBtn(i, 1689517798UL); addCodeToBtn(i, 1689517783UL); addCodeToBtn(i, 1689517768UL); addCodeToBtn(i, 1689517753UL); addCodeToBtn(i, 1689517738UL); addCodeToBtn(i, 1689517723UL); addCodeToBtn(i, 1689517708UL);
  i = ensureBtnInFan("masterbedroom", "2hoff"); addCodeToBtn(i, 1689516642UL); addCodeToBtn(i, 1689516627UL); addCodeToBtn(i, 1689516612UL); addCodeToBtn(i, 1689516597UL); addCodeToBtn(i, 1689516582UL); addCodeToBtn(i, 1689516567UL); addCodeToBtn(i, 1689516552UL); addCodeToBtn(i, 1689516657UL);
  i = ensureBtnInFan("masterbedroom", "4hoff"); addCodeToBtn(i, 1689516268UL); addCodeToBtn(i, 1689516253UL); addCodeToBtn(i, 1689516238UL); addCodeToBtn(i, 1689516223UL); addCodeToBtn(i, 1689516192UL); addCodeToBtn(i, 1689516177UL); addCodeToBtn(i, 1689516162UL); addCodeToBtn(i, 1689516283UL);
  i = ensureBtnInFan("masterbedroom", "8hoff"); addCodeToBtn(i, 1689516508UL); addCodeToBtn(i, 1689516493UL); addCodeToBtn(i, 1689516478UL); addCodeToBtn(i, 1689516463UL); addCodeToBtn(i, 1689516432UL); addCodeToBtn(i, 1689516417UL); addCodeToBtn(i, 1689516538UL); addCodeToBtn(i, 1689516523UL); addCodeToBtn(i, 1689516380UL);
  i = ensureBtnInFan("patio", "1"); addCodeToBtn(i, 1561872297UL); addCodeToBtn(i, 1561872282UL); addCodeToBtn(i, 1561872267UL); addCodeToBtn(i, 1561872372UL); addCodeToBtn(i, 1561872357UL); addCodeToBtn(i, 1561872342UL); addCodeToBtn(i, 1561872327UL); addCodeToBtn(i, 1561872312UL);
  i = ensureBtnInFan("patio", "2"); addCodeToBtn(i, 1561872131UL); addCodeToBtn(i, 1561872252UL); addCodeToBtn(i, 1561872237UL); addCodeToBtn(i, 1561872222UL); addCodeToBtn(i, 1561872207UL); addCodeToBtn(i, 1561872176UL); addCodeToBtn(i, 1561872161UL); addCodeToBtn(i, 1561872146UL);
  i = ensureBtnInFan("patio", "3"); addCodeToBtn(i, 1561872102UL); addCodeToBtn(i, 1561872087UL); addCodeToBtn(i, 1561872072UL); addCodeToBtn(i, 1561872057UL); addCodeToBtn(i, 1561872042UL); addCodeToBtn(i, 1561872027UL); addCodeToBtn(i, 1561872012UL); addCodeToBtn(i, 1561872117UL);
  i = ensureBtnInFan("patio", "4"); addCodeToBtn(i, 1561871967UL); addCodeToBtn(i, 1561871936UL); addCodeToBtn(i, 1561871921UL); addCodeToBtn(i, 1561871906UL); addCodeToBtn(i, 1561871891UL); addCodeToBtn(i, 1561871876UL); addCodeToBtn(i, 1561871997UL); addCodeToBtn(i, 1561871982UL);
  i = ensureBtnInFan("patio", "5"); addCodeToBtn(i, 1561871787UL); addCodeToBtn(i, 1561871772UL); addCodeToBtn(i, 1561871757UL); addCodeToBtn(i, 1561871862UL); addCodeToBtn(i, 1561871847UL); addCodeToBtn(i, 1561871832UL); addCodeToBtn(i, 1561871817UL); addCodeToBtn(i, 1561871802UL);
  i = ensureBtnInFan("patio", "6"); addCodeToBtn(i, 1561871621UL); addCodeToBtn(i, 1561871742UL); addCodeToBtn(i, 1561871727UL); addCodeToBtn(i, 1561871696UL); addCodeToBtn(i, 1561871681UL); addCodeToBtn(i, 1561871666UL); addCodeToBtn(i, 1561871651UL); addCodeToBtn(i, 1561871636UL);
  i = ensureBtnInFan("patio", "off"); addCodeToBtn(i, 1561874007UL); addCodeToBtn(i, 1561873992UL); addCodeToBtn(i, 1561873977UL); addCodeToBtn(i, 1561873962UL); addCodeToBtn(i, 1561873947UL); addCodeToBtn(i, 1561873932UL); addCodeToBtn(i, 1561874037UL); addCodeToBtn(i, 1561874022UL);
  i = ensureBtnInFan("patio", "rev"); addCodeToBtn(i, 1561871532UL); addCodeToBtn(i, 1561871517UL); addCodeToBtn(i, 1561871502UL); addCodeToBtn(i, 1561871607UL); addCodeToBtn(i, 1561871592UL); addCodeToBtn(i, 1561871577UL); addCodeToBtn(i, 1561871562UL); addCodeToBtn(i, 1561871547UL);
  i = ensureBtnInFan("patio", "home"); addCodeToBtn(i, 1561874262UL); addCodeToBtn(i, 1561874247UL); addCodeToBtn(i, 1561874232UL); addCodeToBtn(i, 1561874217UL); addCodeToBtn(i, 1561874202UL); addCodeToBtn(i, 1561874187UL); addCodeToBtn(i, 1561874292UL); addCodeToBtn(i, 1561874277UL);
  i = ensureBtnInFan("patio", "wind"); addCodeToBtn(i, 1561873857UL); addCodeToBtn(i, 1561873842UL); addCodeToBtn(i, 1561873827UL); addCodeToBtn(i, 1561873812UL); addCodeToBtn(i, 1561873797UL); addCodeToBtn(i, 1561873918UL); addCodeToBtn(i, 1561873903UL); addCodeToBtn(i, 1561873872UL);
  i = ensureBtnInFan("patio", "1hoff"); addCodeToBtn(i, 1561874082UL); addCodeToBtn(i, 1561874067UL); addCodeToBtn(i, 1561874052UL); addCodeToBtn(i, 1561874173UL); addCodeToBtn(i, 1561874158UL); addCodeToBtn(i, 1561874143UL); addCodeToBtn(i, 1561874112UL); addCodeToBtn(i, 1561874097UL);
  i = ensureBtnInFan("patio", "2hoff"); addCodeToBtn(i, 1561872896UL); addCodeToBtn(i, 1561873017UL); addCodeToBtn(i, 1561873002UL); addCodeToBtn(i, 1561872987UL); addCodeToBtn(i, 1561872972UL); addCodeToBtn(i, 1561872957UL); addCodeToBtn(i, 1561872942UL); addCodeToBtn(i, 1561872927UL);
  i = ensureBtnInFan("patio", "4hoff"); addCodeToBtn(i, 1561872627UL); addCodeToBtn(i, 1561872612UL); addCodeToBtn(i, 1561872552UL); addCodeToBtn(i, 1561872537UL); addCodeToBtn(i, 1561872522UL); addCodeToBtn(i, 1561872597UL); addCodeToBtn(i, 1561872582UL); addCodeToBtn(i, 1561872567UL);
  i = ensureBtnInFan("patio", "8hoff"); addCodeToBtn(i, 1561872777UL); addCodeToBtn(i, 1561872882UL); addCodeToBtn(i, 1561872867UL); addCodeToBtn(i, 1561872852UL); addCodeToBtn(i, 1561872837UL); addCodeToBtn(i, 1561872822UL); addCodeToBtn(i, 1561872807UL); addCodeToBtn(i, 1561872792UL);
  strncpy(currentFan, "masterbedroom", 15); currentFan[15] = 0;
  saveAll();
  Serial.println(F("seeded both fans (masterbedroom + patio) with full button cycles"));
}

// ---------- rx ----------
void handleReceived() {
  if (!rc.available()) return;
  unsigned long v = rc.getReceivedValue();
  unsigned int bits = rc.getReceivedBitlength();
  rc.resetAvailable();
  if (v == 0 || bits != CODE_BITS) return;   // ignore noise / short-decode glitches

  if (learning) {
    if (addCodeToBtn(learnIdx, v)) {
      Serial.print(F("  + ")); Serial.print(v);
      Serial.print(F("  (")); Serial.print(buttons[learnIdx].ncodes); Serial.println(F(" total)"));
    }
    learnLastMs = millis();
    return;
  }

  static unsigned long last = 0, lastMs = 0;
  if (v == last && millis() - lastMs < 400) { lastMs = millis(); return; }
  last = v; lastMs = millis();
  int idx = identify(v);
  Serial.print(F("[rx] "));
  if (idx >= 0) {
    Serial.print(buttons[idx].fan); Serial.print('/'); Serial.print(buttons[idx].name);
    Serial.print(F("  (")); Serial.print(v); Serial.println(F(")"));
  } else {
    Serial.print(v); Serial.println(F("  (unknown -- learn it?)"));
  }
}

// ---------- commands ----------
void handleLine(String line) {
  line.trim();
  if (!line.length()) return;

  String cmd = line, arg = "";
  int sp = line.indexOf(' ');
  if (sp >= 0) { cmd = line.substring(0, sp); arg = line.substring(sp + 1); arg.trim(); }
  cmd.toLowerCase();

  if (cmd == "help")  { help(); return; }
  if (cmd == "dump")  { dumpAll(); return; }
  if (cmd == "add")   { handleImport(arg); return; }
  if (cmd == "fans")  { listFans(); return; }
  if (cmd == "fan")   { setFan(arg); return; }
  if (cmd == "list")  { listButtons(); return; }
  if (cmd == "done")  { finishLearn(true); return; }
  if (cmd == "learn") { startLearn(arg); return; }
  if (cmd == "clear") {   // clears current fan only
    int w = 0;
    for (int i = 0; i < nbtn; i++)
      if (!(buttons[i].used && strcmp(buttons[i].fan, currentFan) == 0)) buttons[w++] = buttons[i];
    nbtn = w; saveAll();
    Serial.print(F("cleared fan ")); Serial.println(currentFan);
    return;
  }
  if (cmd == "del") {
    int idx = findBtn(arg);
    if (idx < 0) { Serial.println(F("no such button")); return; }
    for (int i = idx; i < nbtn - 1; i++) buttons[i] = buttons[i + 1];
    nbtn--; buttons[nbtn].used = false;
    saveAll(); Serial.println(F("deleted"));
    return;
  }

  // otherwise: send a button in the current fan (optional repeat count)
  int idx = findBtn(cmd);
  if (idx < 0) { Serial.print(F("unknown: ")); Serial.println(cmd); help(); return; }
  int reps = arg.length() ? arg.toInt() : 1;
  if (reps < 1) reps = 1;
  if (reps > 100) reps = 100;
  if (reps == 1) {
    sendBtnOnce(idx);                                  // single tap
  } else {
    unsigned long code = buttons[idx].codes[0];        // emulate a HOLD: one frozen code
    for (int i = 0; i < reps; i++) { sendRaw(code); delay(106); }   // ...repeated at the remote's ~106ms cadence
  }
  Serial.print(F("-> sent ")); Serial.print(currentFan); Serial.print('/'); Serial.print(buttons[idx].name);
  if (reps > 1) { Serial.print(F(" x")); Serial.print(reps); }
  Serial.println();
}

// ---------- Home Assistant over HTTP ----------
int findInFan(const char* fan, String name) {
  name.toLowerCase();
  for (int i = 0; i < nbtn; i++)
    if (buttons[i].used && name == buttons[i].name && strcmp(buttons[i].fan, fan) == 0) return i;
  return -1;
}

void doSend(const char* fan, String name, int reps) {
  int idx = findInFan(fan, name);
  if (idx < 0) return;
  if (reps < 1) reps = 1;
  if (reps > 100) reps = 100;
  if (reps == 1) sendBtnOnce(idx);
  else { unsigned long code = buttons[idx].codes[0]; for (int i = 0; i < reps; i++) { sendRaw(code); delay(106); } }
  g_lastAction = String(fan) + "/" + name;
  Serial.print(F("http -> ")); Serial.print(fan); Serial.print('/'); Serial.println(name);
}

void handleSend() {
  String fan = server.arg("fan");
  String btn = server.arg("btn");
  int n = server.hasArg("n") ? server.arg("n").toInt() : 1;
  if (!fan.length() || !btn.length()) { server.send(400, "text/plain", "need fan & btn"); return; }
  if (findInFan(fan.c_str(), btn) < 0) { server.send(404, "text/plain", "unknown fan/btn"); return; }
  doSend(fan.c_str(), btn, n);
  server.send(200, "text/plain", "ok");
}

void handleStatus() {
  String j = "{\"ip\":\"" + WiFi.localIP().toString() + "\",\"rssi\":" + String(WiFi.RSSI()) +
             ",\"uptime_s\":" + String(millis() / 1000) + ",\"buttons\":" + String(nbtn) +
             ",\"last\":\"" + g_lastAction + "\"}";
  server.send(200, "application/json", j);
}

void handleClear() {
  if (server.hasArg("all")) {
    nbtn = 0; memset(buttons, 0, sizeof(buttons)); saveAll();
    g_lastAction = "cleared all";
    server.send(200, "text/plain", "cleared ALL buttons (reboot reseeds the baked-in defaults)");
    return;
  }
  String fan = server.hasArg("fan") ? server.arg("fan") : String(currentFan);
  fan.toLowerCase();
  int removed = 0, w = 0;
  for (int i = 0; i < nbtn; i++) {
    if (buttons[i].used && strcmp(buttons[i].fan, fan.c_str()) == 0) { removed++; continue; }
    buttons[w++] = buttons[i];
  }
  nbtn = w; saveAll();
  server.send(200, "text/plain", "cleared " + fan + ": " + String(removed) + " buttons");
}

void handleRoot() {
  String h = F("<!doctype html><meta name=viewport content='width=device-width,initial-scale=1'>"
               "<style>body{font-family:sans-serif;background:#111;color:#eee;margin:16px}"
               "a.b{display:inline-block;margin:4px;padding:10px 14px;background:#2a6;color:#fff;"
               "text-decoration:none;border-radius:6px}h2{margin:18px 0 6px}"
               "#st{background:#222;padding:8px 12px;border-radius:6px;font-size:13px;color:#9cf;margin-bottom:8px}</style>"
               "<script>"
               "function s(f,b){fetch('/send?fan='+encodeURIComponent(f)+'&btn='+encodeURIComponent(b)).then(u);return false}"
               "function u(){fetch('/status').then(r=>r.json()).then(d=>{document.getElementById('st').innerHTML="
               "'IP '+d.ip+' \\u00b7 '+d.rssi+' dBm \\u00b7 up '+d.uptime_s+'s \\u00b7 '+d.buttons+' buttons \\u00b7 last: '+d.last})}"
               "setInterval(u,2000)</script>"
               "<h1>Fan RF Bridge</h1><div id=st>loading status...</div>");
  String lastFan = "";
  for (int i = 0; i < nbtn; i++) {
    if (!buttons[i].used) continue;
    if (String(buttons[i].fan) != lastFan) { h += "<h2>" + String(buttons[i].fan) + "</h2>"; lastFan = buttons[i].fan; }
    h += "<a class=b href='#' onclick=\"return s('" + String(buttons[i].fan) + "','" + String(buttons[i].name) + "')\">" + String(buttons[i].name) + "</a>";
  }
  h += "<script>u()</script>";
  server.send(200, "text/html", h);
}

void wifiConnect() {
  WiFi.mode(WIFI_STA);
  WiFi.setHostname(HOSTNAME);
#if USE_STATIC_IP
  IPAddress ip(STATIC_IP), gw(GATEWAY_IP), sn(SUBNET_MASK), dns(DNS_IP);
  if (!WiFi.config(ip, gw, sn, dns)) Serial.println(F("static IP config failed"));
#endif
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  Serial.print(F("WiFi connecting"));
  int n = 0;
  while (WiFi.status() != WL_CONNECTED && n < 40) { delay(500); Serial.print('.'); n++; }
  Serial.println();
  if (WiFi.status() == WL_CONNECTED) {
    Serial.print(F("WiFi OK, IP=")); Serial.println(WiFi.localIP());
    if (MDNS.begin(HOSTNAME)) {
      MDNS.addService("http", "tcp", 80);
      Serial.print(F("name: http://")); Serial.print(HOSTNAME); Serial.println(F(".local"));
    }
  } else Serial.println(F("WiFi FAILED (check secrets.h)"));
}

void setup() {
  Serial.begin(115200);
  delay(2000);
  Serial.println(F("\nFan remote trainer (multi-fan)"));

  ELECHOUSE_cc1101.setSpiPin(PIN_SCK, PIN_MISO, PIN_MOSI, PIN_CS);
  ELECHOUSE_cc1101.setGDO0(PIN_GDO0);
  ELECHOUSE_cc1101.Init();
  ELECHOUSE_cc1101.setModulation(2);
  ELECHOUSE_cc1101.setMHZ(DEFAULT_FREQ_MHZ);
  ELECHOUSE_cc1101.setPktFormat(3);
  ELECHOUSE_cc1101.SetRx();
  rc.enableReceive(PIN_GDO0);

  loadAll();
  if (nbtn == 0) seedDefaults();

  help();
  Serial.print(F("active fan: ")); Serial.println(currentFan);
  listButtons();

  wifiConnect();
  server.on("/send", handleSend);
  server.on("/status", handleStatus);
  server.on("/clear", handleClear);
  server.on("/", handleRoot);
  server.begin();
  Serial.println(F("HTTP server started on port 80"));
}

void loop() {
  handleReceived();
  server.handleClient();
  if (learning) {
    unsigned long idle = millis() - learnLastMs;                 // learnLastMs updates on every received code
    if (buttons[learnIdx].ncodes > 0 && idle > 4000) {
      finishLearn(false);                                        // done: 4s after last captured press
    } else if (idle > 25000) {
      learning = false;
      Serial.println(F("learn timed out -- heard no signal. Re-check antenna/RX, then 'learn <name>' again."));
    }
  }
  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\n' || c == '\r') { handleLine(buf); buf = ""; }
    else buf += c;
  }
}
