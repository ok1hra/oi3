// ui.cpp — display, encoder, button, WiFi management, NVS save. Task on core 0.
//
// Encoder (PCNT, x4 quadrature): 1 detent = 1 step of the selected function.
// Short press: next function (WPM / tone / volume / delay D / iambic A-B);
// the display falls back to WPM 6 s after the last change.
// Long press: info page (IP, RSSI, OI3, counters).
#include "shared.h"
#include "rkey_proto.h"
#include <M5Dial.h>
#include <WiFi.h>
#include "driver/pcnt.h"
#include "esp_timer.h"

void web_begin();
void web_loop();
bool web_take_peer_changed();

enum Func : uint8_t { F_WPM, F_TONE, F_VOL, F_DELAY, F_MODE, F_COUNT };
static Func     func = F_WPM;
static uint32_t funcMs = 0;
static bool     infoPage = false;
static bool     dirty = false;
static uint32_t dirtyMs = 0;
static bool     apOn = false, apForced = false;
static uint32_t apIdleMs = 0;
static char     apName[24];

static M5Canvas canvas(&M5Dial.Display);
static bool     haveCanvas = false;

// ---------------- encoder ----------------
static int16_t encLast = 0;
static int     encAcc  = 0;

static void enc_begin() {
  // ESP-IDF rotary encoder example: both channels, x4 decoding.
  pcnt_config_t c = {};
  c.unit = PCNT_UNIT_0;
  c.channel = PCNT_CHANNEL_0;
  c.pulse_gpio_num = PIN_ENC_A; c.ctrl_gpio_num = PIN_ENC_B;
  c.pos_mode = PCNT_COUNT_DEC;  c.neg_mode = PCNT_COUNT_INC;
  c.lctrl_mode = PCNT_MODE_REVERSE; c.hctrl_mode = PCNT_MODE_KEEP;
  c.counter_h_lim = 32767; c.counter_l_lim = -32768;
  pcnt_unit_config(&c);
  c.channel = PCNT_CHANNEL_1;
  c.pulse_gpio_num = PIN_ENC_B; c.ctrl_gpio_num = PIN_ENC_A;
  c.pos_mode = PCNT_COUNT_INC;  c.neg_mode = PCNT_COUNT_DEC;
  pcnt_unit_config(&c);
  pcnt_set_filter_value(PCNT_UNIT_0, 1023);      // ~12.8 µs glitch filter
  pcnt_filter_enable(PCNT_UNIT_0);
  pcnt_counter_pause(PCNT_UNIT_0);
  pcnt_counter_clear(PCNT_UNIT_0);
  pcnt_counter_resume(PCNT_UNIT_0);
}

static int enc_steps() {
  int16_t v = 0;
  pcnt_get_counter_value(PCNT_UNIT_0, &v);
  encAcc += (int16_t)(v - encLast);
  encLast = v;
  if (v > 16000 || v < -16000) { pcnt_counter_clear(PCNT_UNIT_0); encLast = 0; }
  int s = encAcc / ENC_COUNTS_PER_DETENT;
  encAcc -= s * ENC_COUNTS_PER_DETENT;
  return s;
}

static int clampi(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

static void apply_steps(int s) {
  switch (func) {
    case F_WPM:  g_set.wpm = clampi(g_set.wpm + s, WPM_MIN, WPM_MAX); break;
    case F_TONE: g_set.toneHz = clampi(g_set.toneHz + 10 * s, TONE_MIN, TONE_MAX); keyer_set_tone(g_set.toneHz); break;
    case F_VOL:  g_set.vol = clampi(g_set.vol + s, VOL_MIN, VOL_MAX); break;
    case F_DELAY: {                               // 0 = OI3 default, else 10..1000 ms
      int d = g_set.delayMs + 10 * s;
      g_set.delayMs = (d < 10) ? 0 : clampi(d, 10, DELAY_MAX);
      break;
    }
    case F_MODE: if (s & 1) g_set.mode = (g_set.mode == 'A') ? 'B' : 'A'; break;
    default: break;
  }
  dirty = true; dirtyMs = millis(); funcMs = millis();
}

// ---------------- WiFi ----------------
static void wifi_begin(bool forceSetup) {
  WiFi.persistent(false);                         // our NVS only; no flash writes by the WiFi lib
  uint8_t mac[6]; WiFi.macAddress(mac);
  snprintf(apName, sizeof(apName), "RKEY-Dial-%02X%02X", mac[4], mac[5]);
  if (forceSetup || !g_set.ssid[0]) {
    WiFi.mode(WIFI_AP);
    WiFi.softAP(apName, g_set.webPass);
    apOn = apForced = true;
  } else {
    WiFi.mode(WIFI_STA);
    WiFi.setSleep(false);                         // no modem sleep: it adds 100+ ms latency
    WiFi.setAutoReconnect(true);
    WiFi.begin(g_set.ssid, g_set.pass);
  }
  WiFi.setSleep(false);
}

static void wifi_service() {
  if (apForced) return;
  uint32_t ms = millis();
  if (!WiFi.isConnected() && !apOn && ms > 20000) {  // STA not up: open the setup AP as well
    WiFi.mode(WIFI_AP_STA);
    WiFi.softAP(apName, g_set.webPass);
    WiFi.setSleep(false);
    apOn = true; apIdleMs = ms;
  }
  if (apOn && WiFi.isConnected()) {               // STA works again: drop the AP when unused
    if (WiFi.softAPgetStationNum() > 0) apIdleMs = ms;
    else if (ms - apIdleMs > 30000) { WiFi.softAPdisconnect(true); WiFi.mode(WIFI_STA); WiFi.setSleep(false); apOn = false; }
  }
}

// ---------------- OI3 address ----------------
static uint32_t resolveMs = (uint32_t)-60000;   // resolve at once after boot
static bool     resolveOk = false;

static void peer_service(bool force) {
  uint32_t ms = millis();
  if (!force && ms - resolveMs < (resolveOk ? 60000u : 3000u)) return;
  resolveMs = ms;
  IPAddress ip;
  if (!g_set.host[0]) { net_set_peer(0); resolveOk = false; return; }
  if (ip.fromString(g_set.host)) { net_set_peer((uint32_t)ip); resolveOk = true; return; }
  if (!WiFi.isConnected()) return;
  if (WiFi.hostByName(g_set.host, ip) == 1 && (uint32_t)ip != 0) { net_set_peer((uint32_t)ip); resolveOk = true; }
  else resolveOk = false;                         // keep the last known address
}

// ---------------- display ----------------
static const char* mode_name(uint8_t m) {
  switch (m) {
    case 0x00: return "LSB";  case 0x01: return "USB"; case 0x02: return "AM";
    case 0x03: return "CW";   case 0x04: return "RTTY"; case 0x05: return "FM";
    case 0x07: return "CW-R"; case 0x08: return "RTTY-R"; case 0x17: return "DV";
    default:   return "?";
  }
}

static const char* warning(const LinkState& L, uint16_t* col) {
  *col = TFT_RED;
  if (apForced)                 { *col = TFT_YELLOW; return "SETUP MODE"; }
  if (!g_set.keySet)            return "NO KEY";
  if (!L.wifi)                  return "NO WIFI";
  if (!L.peer)                  return "NO OI3 ADDR";
  if (!L.fresh)                 return "NO LINK";
  if (L.flags & RKEY_SF_NONCE_BAD) { *col = TFT_ORANGE; return "SYNC"; }
  if (L.flags & RKEY_SF_INTERLOCK) return "INTERLOCK";
  if (L.flags & RKEY_SF_TUNE)      { *col = TFT_ORANGE; return "TUNE"; }
  if (!(L.flags & RKEY_SF_CW_MODE)) return "TRX NOT CW";
  if (L.flags & RKEY_SF_MUTED)     { *col = TFT_ORANGE; return "MUTED"; }
  return nullptr;
}

template <class G> static void draw_main(G& g, const LinkState& L) {
  g.fillScreen(TFT_BLACK);
  bool txDown = L.fresh && (L.flags & RKEY_SF_KEY_DOWN);
  uint16_t ring = txDown ? TFT_RED : (L.fresh && L.sessionOk ? 0x03E0 /*dark green*/ : 0x4208 /*grey*/);
  g.fillArc(120, 120, 119, txDown ? 108 : 114, 0, 360, ring);

  g.setTextDatum(middle_center);
  // top: rig frequency + mode
  g.setTextColor(TFT_WHITE);
  g.setFont(&fonts::Font4);
  char s[40];
  if (L.fresh && (L.flags & RKEY_SF_CIV_VALID) && L.freqHz) {
    snprintf(s, sizeof(s), "%lu.%03lu.%02lu", (unsigned long)(L.freqHz / 1000000UL),
             (unsigned long)((L.freqHz / 1000UL) % 1000UL), (unsigned long)((L.freqHz % 1000UL) / 10UL));
    g.drawString(s, 120, 42);
    g.setFont(&fonts::Font2);
    g.setTextColor((L.flags & RKEY_SF_CW_MODE) ? TFT_GREEN : TFT_ORANGE);
    snprintf(s, sizeof(s), "%s  SEQ %u", mode_name(L.civMode), L.seqLevel);
    g.drawString(s, 120, 66);
  } else {
    g.setTextColor(0x8410);
    g.drawString("---.---.--", 120, 42);
  }

  // centre: big value of the selected function
  const char* label = "WPM";
  bool num = true;
  int  val = g_set.wpm;
  switch (func) {
    case F_TONE:  label = "TONE Hz";  val = g_set.toneHz; break;
    case F_VOL:   label = "VOLUME %"; val = g_set.vol; break;
    case F_DELAY: label = g_set.delayMs ? "DELAY ms" : "DELAY (OI3)"; val = g_set.delayMs ? g_set.delayMs : L.delayMs; break;
    case F_MODE:  label = "IAMBIC"; num = false; break;
    default: break;
  }
  g.setTextColor(func == F_WPM ? TFT_WHITE : TFT_YELLOW);
  if (num) {
    g.setFont(&fonts::Font7);
    g.setTextSize(func == F_TONE || func == F_DELAY ? 1.2f : 1.6f);
    snprintf(s, sizeof(s), "%d", val);
  } else {
    g.setFont(&fonts::FreeSansBold24pt7b);
    g.setTextSize(1.5f);
    snprintf(s, sizeof(s), "%c", g_set.mode);
  }
  g.drawString(s, 120, 116);
  g.setTextSize(1);
  g.setFont(&fonts::Font2);
  g.drawString(label, 120, 160);

  // warning line
  uint16_t wc;
  const char* w = warning(L, &wc);
  if (w) { g.setFont(&fonts::FreeSansBold9pt7b); g.setTextColor(wc); g.drawString(w, 120, 182); }

  // bottom: RTT, delay, loss
  g.setFont(&fonts::Font2);
  g.setTextColor(TFT_LIGHTGREY);
  if (L.fresh && L.rttUs >= 0)
    snprintf(s, sizeof(s), "RTT %ld/%ld  D%u", (long)(L.rttUs / 1000), (long)(L.rttMinUs / 1000), L.delayMs);
  else
    snprintf(s, sizeof(s), "RTT --  D%u", L.fresh ? L.delayMs : g_set.delayMs);
  g.drawString(s, 120, 203);
  char up[6], dn[6];
  if (L.upLossPct == 255) strcpy(up, "-"); else snprintf(up, sizeof(up), "%u", L.upLossPct);
  if (L.dnLossPct == 255) strcpy(dn, "-"); else snprintf(dn, sizeof(dn), "%u", L.dnLossPct);
  snprintf(s, sizeof(s), "%s/%s%% L%u/%u", up, dn, L.lostEv, L.lateEv);
  g.drawString(s, 120, 220);
}

template <class G> static void draw_info(G& g, const LinkState& L) {
  g.fillScreen(TFT_BLACK);
  g.setTextDatum(middle_center);
  g.setFont(&fonts::Font2);
  g.setTextColor(TFT_CYAN);
  g.drawString(FW_VERSION, 120, 36);
  g.setTextColor(TFT_WHITE);
  char s[64];
  int y = 60;
  auto line = [&](const char* t) { g.drawString(t, 120, y); y += 18; };
  if (apOn) {
    snprintf(s, sizeof(s), "AP %s", apName); line(s);
    if (apForced) { snprintf(s, sizeof(s), "pass %s", g_set.webPass); line(s); }
    snprintf(s, sizeof(s), "http://%s", WiFi.softAPIP().toString().c_str()); line(s);
  }
  if (WiFi.isConnected()) {
    snprintf(s, sizeof(s), "%.20s %ddBm", WiFi.SSID().c_str(), WiFi.RSSI()); line(s);
    line(WiFi.localIP().toString().c_str());
  } else { snprintf(s, sizeof(s), "STA %.20s ...", g_set.ssid); line(s); }
  snprintf(s, sizeof(s), "OI3 %.24s:%u", g_set.host[0] ? g_set.host : "-", g_set.port); line(s);
  snprintf(s, sizeof(s), "sess %04X tx %lu", net_session(), (unsigned long)L.txPkts); line(s);
  snprintf(s, sizeof(s), "bad tag %lu  %c %s", (unsigned long)L.badTag, g_set.mode, g_set.swap ? "swap" : ""); line(s);
  snprintf(s, sizeof(s), "tone %u Hz vol %u%%", g_set.toneHz, g_set.vol); line(s);
}

static void draw() {
  LinkState L;
  net_get(&L);
  if (haveCanvas) {
    if (infoPage || apForced) draw_info(canvas, L); else draw_main(canvas, L);
    canvas.pushSprite(0, 0);
  } else {
    M5Dial.Display.startWrite();
    if (infoPage || apForced) draw_info(M5Dial.Display, L); else draw_main(M5Dial.Display, L);
    M5Dial.Display.endWrite();
  }
}

// ---------------- task ----------------
static void ui_task(void*) {
  uint32_t drawMs = 0;
  for (;;) {
    M5Dial.update();
    uint32_t ms = millis();
    if (M5Dial.BtnA.wasHold()) { infoPage = !infoPage; drawMs = 0; }
    else if (M5Dial.BtnA.wasClicked()) {
      if (infoPage) infoPage = false;
      else { func = (Func)((func + 1) % F_COUNT); funcMs = ms; }
      drawMs = 0;
    }
    int s = enc_steps();
    if (s) { apply_steps(s); drawMs = 0; }
    if (func != F_WPM && ms - funcMs > 6000) { func = F_WPM; drawMs = 0; }

    // NVS write stalls both cores -> only while the keyer has been idle for 2 s
    if (dirty && ms - dirtyMs > 3000 && keyer_idle_us((uint32_t)esp_timer_get_time()) > 2000000u) {
      settings_save();
      dirty = false;
    }

    wifi_service();
    peer_service(web_take_peer_changed());
    web_loop();

    if (ms - drawMs >= 100) { draw(); drawMs = ms; }
    vTaskDelay(pdMS_TO_TICKS(10));
  }
}

void ui_begin(bool forceSetup) {
  M5Dial.Display.setBrightness(128);
  canvas.setColorDepth(16);
  haveCanvas = canvas.createSprite(240, 240) != nullptr;
  if (!haveCanvas) { canvas.setColorDepth(8); haveCanvas = canvas.createSprite(240, 240) != nullptr; }
  enc_begin();
  wifi_begin(forceSetup);
  web_begin();
  xTaskCreatePinnedToCore(ui_task, "rkey-ui", 8192, nullptr, 2, nullptr, 0);
}
