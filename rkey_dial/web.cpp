// web.cpp — configuration page (HTTP basic auth: user "admin", password = webPass).
//
// Secrets are write-only: the WiFi password and the shared key are never sent
// back to the browser, except a freshly generated key, shown once so it can be
// copied into oi3.cfg ([RkeyKey=...]).
#include "shared.h"
#include <WiFi.h>
#include <WebServer.h>

static WebServer server(80);
static volatile bool peerChanged = false;

bool web_take_peer_changed() { bool c = peerChanged; peerChanged = false; return c; }

static String esc(const char* s) {
  String o;
  for (; *s; s++) {
    switch (*s) {
      case '&': o += F("&amp;"); break;
      case '<': o += F("&lt;"); break;
      case '>': o += F("&gt;"); break;
      case '"': o += F("&quot;"); break;
      case '\'': o += F("&#39;"); break;
      default: o += *s;
    }
  }
  return o;
}

static bool auth() {
  if (server.authenticate("admin", g_set.webPass)) return true;
  server.requestAuthentication(BASIC_AUTH, "rkey-dial");
  return false;
}

static const char HEAD[] PROGMEM =
  "<!doctype html><html><head><meta charset=utf-8>"
  "<meta name=viewport content='width=device-width,initial-scale=1'><title>RKEY Dial</title>"
  "<style>body{font-family:sans-serif;max-width:34em;margin:1em auto;padding:0 1em}"
  "label{display:block;margin-top:.6em}input,select{width:100%;padding:.3em;box-sizing:border-box}"
  "input[type=checkbox]{width:auto}fieldset{margin-top:1em}code{word-break:break-all}"
  "button{margin-top:1em;padding:.5em 1.2em}</style></head><body><h2>RKEY Dial</h2>";

static void field(String& h, const char* label, const char* name, const String& value,
                  const char* type = "text", const char* extra = "") {
  h += F("<label>"); h += label;
  h += F("<input type="); h += type; h += F(" name="); h += name;
  h += F(" value=\""); h += value; h += F("\" "); h += extra; h += F("></label>");
}

static void check(String& h, const char* label, const char* name, bool on) {
  h += F("<label><input type=checkbox name="); h += name; h += on ? F(" checked") : F("");
  h += F("> "); h += label; h += F("</label>");
}

static void handle_root() {
  if (!auth()) return;
  String h = FPSTR(HEAD);
  h += F("<p>"); h += FW_VERSION; h += F(" &middot; ");
  h += WiFi.isConnected() ? ("WiFi " + esc(WiFi.SSID().c_str()) + " " + WiFi.localIP().toString()) : String(F("WiFi not connected"));
  h += F(" &middot; <a href=/status>status</a></p><form method=post action=/save>");
  h += F("<fieldset><legend>WiFi</legend>");
  field(h, "SSID", "ssid", esc(g_set.ssid));
  field(h, "Password (empty = keep)", "pass", "", "password", "autocomplete=new-password");
  h += F("</fieldset><fieldset><legend>OI3</legend>");
  field(h, "OI3 IP or hostname", "host", esc(g_set.host));
  field(h, "UDP port (RkeyPort)", "port", String(g_set.port), "number", "min=1 max=65535");
  h += F("<label>Shared key (RkeyKey): <b>");
  h += g_set.keySet ? F("set") : F("NOT SET");
  h += F("</b><input name=key placeholder='32 hex chars, empty = keep' autocomplete=off></label>");
  field(h, "Playout delay D ms (0 = OI3 RkeyDelayMs; LAN 30, internet 80-150, LTE 200+)", "delay",
        String(g_set.delayMs), "number", "min=0 max=1000");
  h += F("</fieldset><fieldset><legend>Keyer</legend>");
  field(h, "WPM", "wpm", String(g_set.wpm), "number", "min=5 max=60");
  field(h, "Sidetone Hz", "tone", String(g_set.toneHz), "number", "min=300 max=1200");
  field(h, "Volume % (buzzer duty 1-50)", "vol", String(g_set.vol), "number", "min=1 max=50");
  h += F("<label>Iambic mode<select name=mode><option");
  h += (g_set.mode == 'A') ? F(" selected") : F("");
  h += F(">A</option><option");
  h += (g_set.mode == 'B') ? F(" selected") : F("");
  h += F(">B</option></select></label>");
  check(h, "Swap paddles (dit = G1, dah = G2)", "swap", g_set.swap);
  check(h, "Straight key on Port A G13", "sk", g_set.skEnable);
  check(h, "Headphone tone on Port A G15 (reboot)", "hp", g_set.hpEnable);
  h += F("</fieldset><fieldset><legend>Web</legend>");
  field(h, "Web/AP password, min. 8 chars (empty = keep)", "webpass", "", "password", "autocomplete=new-password");
  h += F("</fieldset><button>Save</button></form>"
         "<form method=post action=/genkey onsubmit=\"return confirm('Replace the shared key? OI3 must get the new RkeyKey too.')\">"
         "<button>Generate new key</button></form></body></html>");
  server.send(200, "text/html", h);
}

static void copy_arg(const char* name, char* dst, size_t n) {
  if (!server.hasArg(name)) return;
  String v = server.arg(name); v.trim();
  strlcpy(dst, v.c_str(), n);
}

static void handle_save() {
  if (!auth()) return;
  bool reboot = false;
  String v;

  char ssid[sizeof(g_set.ssid)]; strlcpy(ssid, g_set.ssid, sizeof(ssid));
  copy_arg("ssid", ssid, sizeof(ssid));
  if (strcmp(ssid, g_set.ssid)) { strcpy(g_set.ssid, ssid); reboot = true; }
  v = server.arg("pass");
  if (v.length()) { strlcpy(g_set.pass, v.c_str(), sizeof(g_set.pass)); reboot = true; }

  char host[sizeof(g_set.host)]; strlcpy(host, g_set.host, sizeof(host));
  copy_arg("host", host, sizeof(host));
  if (strcmp(host, g_set.host)) { strcpy(g_set.host, host); peerChanged = true; }
  long p = server.arg("port").toInt();
  if (p >= 1 && p <= 65535) g_set.port = (uint16_t)p;

  String msg;
  v = server.arg("key"); v.trim();
  if (v.length()) {
    if (settings_parse_key(v.c_str(), g_set.key)) g_set.keySet = true;
    else msg += F("<p style=color:red>Key ignored: needs 32 hex chars, not all zero.</p>");
  }
  long d = server.arg("delay").toInt();
  g_set.delayMs = (d <= 0) ? 0 : (uint16_t)constrain(d, 10, DELAY_MAX);

  long w = server.arg("wpm").toInt();   if (w) g_set.wpm = constrain(w, WPM_MIN, WPM_MAX);
  long t = server.arg("tone").toInt();  if (t) { g_set.toneHz = constrain(t, TONE_MIN, TONE_MAX); keyer_set_tone(g_set.toneHz); }
  long vo = server.arg("vol").toInt();  if (vo) g_set.vol = constrain(vo, VOL_MIN, VOL_MAX);
  g_set.mode = (server.arg("mode") == "A") ? 'A' : 'B';
  g_set.swap = server.hasArg("swap");
  g_set.skEnable = server.hasArg("sk");
  bool hp = server.hasArg("hp");
  if (hp != g_set.hpEnable) { g_set.hpEnable = hp; reboot = true; }

  v = server.arg("webpass");
  if (v.length()) {
    if (v.length() >= 8 && v.length() < sizeof(g_set.webPass)) { strlcpy(g_set.webPass, v.c_str(), sizeof(g_set.webPass)); reboot = true; }
    else msg += F("<p style=color:red>Web password ignored: 8-32 chars.</p>");
  }

  settings_save();
  String h = FPSTR(HEAD);
  h += F("<p>Saved.</p>"); h += msg;
  h += reboot ? F("<p>Rebooting&hellip;</p>") : F("<p><a href=/>Back</a></p>");
  h += F("</body></html>");
  server.send(200, "text/html", h);
  if (reboot) { delay(500); ESP.restart(); }
}

static void handle_genkey() {
  if (!auth()) return;
  uint8_t k[16];
  do { esp_fill_random(k, sizeof(k)); } while (!(k[0] | k[1] | k[2] | k[3]));
  memcpy(g_set.key, k, 16);
  g_set.keySet = true;
  settings_save();
  char hex[33];
  for (int i = 0; i < 16; i++) sprintf(hex + 2 * i, "%02x", k[i]);
  String h = FPSTR(HEAD);
  h += F("<p>New key stored in the Dial. Put this line into <b>oi3.cfg</b> on the OI3 SD card "
         "(it is shown only now):</p><p><code>[RkeyKey="); h += hex; h += F("]");
  h += F("</code></p><p><a href=/>Back</a></p></body></html>");
  server.send(200, "text/html", h);
}

static void handle_status() {
  if (!auth()) return;
  LinkState L; net_get(&L);
  char s[400];
  snprintf(s, sizeof(s),
    "{\"wifi\":%d,\"peer\":%d,\"fresh\":%d,\"sessionOk\":%d,\"flags\":%u,\"freqHz\":%lu,\"civMode\":%u,"
    "\"seqLevel\":%u,\"delayMs\":%u,\"lateEv\":%u,\"lostEv\":%u,\"rttUs\":%ld,\"rttMinUs\":%ld,"
    "\"upLossPct\":%d,\"dnLossPct\":%d,\"badTag\":%lu,\"txPkts\":%lu,\"session\":%u,\"wpm\":%u}",
    L.wifi, L.peer, L.fresh, L.sessionOk, L.flags, (unsigned long)L.freqHz, L.civMode,
    L.seqLevel, L.delayMs, L.lateEv, L.lostEv, (long)L.rttUs, (long)L.rttMinUs,
    L.upLossPct == 255 ? -1 : L.upLossPct, L.dnLossPct == 255 ? -1 : L.dnLossPct,
    (unsigned long)L.badTag, (unsigned long)L.txPkts, net_session(), g_set.wpm);
  server.send(200, "application/json", s);
}

void web_begin() {
  server.on("/", HTTP_GET, handle_root);
  server.on("/save", HTTP_POST, handle_save);
  server.on("/genkey", HTTP_POST, handle_genkey);
  server.on("/status", HTTP_GET, handle_status);
  server.onNotFound([] { server.send(404, "text/plain", "not found"); });
  server.begin();
}

void web_loop() { server.handleClient(); }
