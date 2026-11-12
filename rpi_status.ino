// ===================== RPI STATUS (/wl-host) =====================
// The state of the Raspberry Pi that runs WIFILT + local-trx with this OI3 as
// local-trx's CAT/CW/PTT, shown in the LCD menu item "RPI" (LCD_RPI_ITEM).
// The Pi usually has no screen; when its web page does not answer, this is
// where the operator sees whether it is up, on which WiFi, at which address,
// rebooting, updating, or unlocked.
//
// WIFILT (native build, on a Pi only) sends TrxNet /wl-host as TRX_NON every
// 5 s, at once on a change and twice on the way down. Payload (TrxNet
// INTEGRATION.md 7.4; sender: wifilt host_status.h):
//   0 ver(1)  1 state  2 flags  3 rssi int8 dBm (0 = none)  4 update % (255 = none)
//   5 reserved  6..9 IPv4 a.b.c.d  10 SSID length (<=32)  11.. SSID
// Nothing is stored in EEPROM; after an OI3 reset the item waits for the next
// packet. The item is skipped while browsing until the first packet that says
// local-trx is present has arrived.
//
// Row 0 (12-char value):  G-62 Domov L   G = signal bars / AP mast / '?' stale
//                                        -62 = dBm, "AP " or "ETH"
//                                        Domov = SSID, marquee when longer than 6
//                                        L = lock: closed, open (blinking) or none
// Row 1 (16 chars): the address to browse to, or what the Pi is doing with a
// running clock. A warning (local-trx off, TRX not linked) alternates with the
// address every 2 s. CW/RTTY text the keyer sends takes row 1 over and hands
// it back RPI_TX_HOLD_MS after the transmission (see keyer_lcd_putc).

const uint8_t RPI_HDR = 11;
const uint8_t RPI_SSID_MAX = 32;

enum : uint8_t { RPI_RUN = 0, RPI_RESTART = 1, RPI_STOP = 2, RPI_REBOOT = 3, RPI_UPDATE = 4 };
enum : uint8_t {
  RPI_F_LOCKED = 0x01, RPI_F_LOCKABLE = 0x02, RPI_F_AP = 0x04, RPI_F_WIFI = 0x08,
  RPI_F_LTX = 0x10, RPI_F_TRXLINK = 0x20, RPI_F_IP_ETH = 0x40
};

const unsigned long RPI_FRESH_MS      = 15000;   // RUN quiet longer than this = offline
const unsigned long RPI_DOWN_MS       = 180000;  // RESTART/STOP/REBOOT: give up after
const unsigned long RPI_UPDATE_MS     = 600000;  // UPDATE: locked Pi reboots twice
const unsigned long RPI_PULSE_MS      = 300;     // heart after each packet
const unsigned long RPI_MARQUEE_MS    = 500;
const unsigned long RPI_ALT_MS        = 2000;    // warning <-> address

// CGRAM slots. Not 0: the LCD code builds C strings and compares them with
// strcmp(), and glyph 0 would end the string.
const uint8_t RPI_G_BARS = 1, RPI_G_LOCK = 2, RPI_G_UNLOCK = 3, RPI_G_AP = 4, RPI_G_HEART = 5;

// Signal bars, level 0..3 (three columns, base dots show the missing ones).
const uint8_t rpiBars[4][8] PROGMEM = {
  {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x15, 0x00},
  {0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0x15, 0x00},
  {0x00, 0x00, 0x00, 0x04, 0x04, 0x14, 0x15, 0x00},
  {0x01, 0x01, 0x01, 0x05, 0x05, 0x15, 0x15, 0x00},
};
// The two locks differ in three ways, so the open one is never mistaken for
// the closed one: shackle (closed / lifted and open on the right), body
// (solid with a keyhole / hollow), and behaviour (steady / blinking).
const uint8_t rpiGlyphs[4][8] PROGMEM = {
  {0x0E, 0x11, 0x11, 0x1F, 0x1B, 0x1B, 0x1F, 0x00},   // RPI_G_LOCK
  {0x0E, 0x11, 0x10, 0x10, 0x1F, 0x11, 0x1F, 0x00},   // RPI_G_UNLOCK
  {0x15, 0x0E, 0x04, 0x04, 0x0A, 0x0A, 0x11, 0x00},   // RPI_G_AP (mast)
  {0x00, 0x0A, 0x1F, 0x1F, 0x0E, 0x04, 0x00, 0x00},   // RPI_G_HEART
};

static uint8_t       rpiState = RPI_RUN;
static uint8_t       rpiFlags = 0;
static int8_t        rpiRssi = 0;
static uint8_t       rpiPct = 255;
static uint8_t       rpiIp[4] = {0, 0, 0, 0};
static char          rpiSsid[RPI_SSID_MAX + 1] = "";
static bool          rpiHaveData = false;
bool                 rpiSeen = false;            // read by the menu navigation
static unsigned long rpiLastRxMs = 0;
static unsigned long rpiStateSinceMs = 0;
static int8_t        rpiBarsLevel = -1;          // what CGRAM slot 1 holds now

static void rpi_load_glyph(uint8_t slot, const uint8_t* progmemBits) {
  uint8_t bits[8];
  memcpy_P(bits, progmemBits, 8);
  lcd.createChar(slot, bits);
  lcd.setCursor(0, 0);   // createChar leaves the address counter in CGRAM
}

void rpi_lcd_glyphs_init() {
  rpi_load_glyph(RPI_G_LOCK,   rpiGlyphs[0]);
  rpi_load_glyph(RPI_G_UNLOCK, rpiGlyphs[1]);
  rpi_load_glyph(RPI_G_AP,     rpiGlyphs[2]);
  rpi_load_glyph(RPI_G_HEART,  rpiGlyphs[3]);
  rpi_load_glyph(RPI_G_BARS,   rpiBars[0]);
  rpiBarsLevel = 0;
}

// TrxNet callback (runs inside net.loop(), not an interrupt).
void onWlHost(const char* /*from*/, const uint8_t* d, size_t len) {
  if (len < RPI_HDR || d[0] < 1) return;
  uint8_t n = d[10];
  if (n > RPI_SSID_MAX || (size_t)RPI_HDR + n > len) return;
  unsigned long now = millis();
  if (!rpiHaveData || d[1] != rpiState) rpiStateSinceMs = now;
  rpiState = d[1];
  rpiFlags = d[2];
  rpiRssi  = (int8_t)d[3];
  rpiPct   = d[4];
  memcpy(rpiIp, d + 6, 4);
  memcpy(rpiSsid, d + RPI_HDR, n);
  rpiSsid[n] = '\0';
  rpiLastRxMs = now;
  rpiHaveData = true;
  if (rpiFlags & RPI_F_LTX) rpiSeen = true;
}

// What to show, from the last packet and how long ago it came.
enum : uint8_t { RPI_V_WAIT, RPI_V_RUN, RPI_V_DOWN, RPI_V_UPDATE, RPI_V_OFFLINE };

static uint8_t rpi_view(unsigned long now, unsigned long* since) {
  if (!rpiHaveData) return RPI_V_WAIT;
  unsigned long age = now - rpiLastRxMs;
  switch (rpiState) {
    case RPI_RUN:
      if (age > RPI_FRESH_MS) { *since = rpiLastRxMs; return RPI_V_OFFLINE; }
      return RPI_V_RUN;
    case RPI_UPDATE:
      if (age > RPI_UPDATE_MS) { *since = rpiLastRxMs; return RPI_V_OFFLINE; }
      *since = rpiLastRxMs;   // the clock runs while the updater keeps it dark
      return RPI_V_UPDATE;
    default:   // RESTART, STOP, REBOOT and anything newer
      if (age > RPI_DOWN_MS) { *since = rpiLastRxMs; return RPI_V_OFFLINE; }
      *since = rpiStateSinceMs;
      return RPI_V_DOWN;
  }
}

static bool rpi_stale(unsigned long now) {
  return rpiHaveData && now - rpiLastRxMs > RPI_FRESH_MS;
}

// Copy a flash string into a row buffer, without its terminator. The texts
// live in flash: on the Mega every literal would otherwise cost RAM.
static void rpi_put(char* out, PGM_P text) {
  memcpy_P(out, text, strlen_P(text));
}

// Elapsed time right-aligned into out[0..4], never wider than 4 so a space
// always separates it from an 11-char label: "m:ss" below 10 minutes, then
// whole minutes "37m", then "99m+".
static void rpi_clock5(char* out, unsigned long ms) {
  unsigned long s = ms / 1000;
  char t[8];
  if (s >= 100UL * 60)     strcpy_P(t, PSTR("99m+"));
  else if (s >= 10UL * 60) snprintf_P(t, sizeof(t), PSTR("%lum"), s / 60);
  else                     snprintf_P(t, sizeof(t), PSTR("%lu:%02lu"), s / 60, s % 60);
  uint8_t len = strlen(t);
  for (uint8_t i = 0; i < 5; i++) out[i] = (i < 5 - len) ? ' ' : t[i - (5 - len)];
}

static void rpi_set_bars(int8_t rssi) {
  int8_t level = rssi == 0 ? 0 : rssi >= -60 ? 3 : rssi >= -70 ? 2 : rssi >= -80 ? 1 : 0;
  if (level == rpiBarsLevel) return;
  rpi_load_glyph(RPI_G_BARS, rpiBars[level]);
  rpiBarsLevel = level;
}

// Row 0, 12 chars + NUL.
void rpi_format_row0(char* out) {
  unsigned long now = millis();
  for (uint8_t i = 0; i < 12; i++) out[i] = ' ';
  out[12] = 0;
  if (!rpiHaveData) { rpi_put(out, PSTR("RPI wait")); return; }

  bool wifi = rpiFlags & RPI_F_WIFI;
  bool ap   = wifi && (rpiFlags & RPI_F_AP);
  if (rpi_stale(now))  out[0] = '?';
  else if (ap)         out[0] = RPI_G_AP;
  else if (wifi)       { rpi_set_bars(rpiRssi); out[0] = RPI_G_BARS; }
  if (ap) rpi_put(out + 1, PSTR("AP "));
  else if (wifi) {
    if (rpiRssi < 0) {
      char t[5];
      snprintf_P(t, sizeof(t), PSTR("%d"), rpiRssi < -99 ? -99 : (int)rpiRssi);
      uint8_t len = strlen(t);
      memcpy(out + 1 + (3 - len), t, len);
    } else rpi_put(out + 1, PSTR(" --"));
  } else rpi_put(out + 1, PSTR("ETH"));

  // SSID in cols 5..10; a longer one scrolls, pausing at its start.
  uint8_t n = strlen(rpiSsid);
  if (n <= 6) memcpy(out + 5, rpiSsid, n);
  else {
    const uint8_t gap = 3, pause = 4;
    uint8_t cycle = n + gap;
    uint16_t step = (now / RPI_MARQUEE_MS) % (cycle + pause);
    uint8_t off = step < pause ? 0 : step - pause;
    for (uint8_t i = 0; i < 6; i++) {
      uint8_t k = (off + i) % cycle;
      out[5 + i] = k < n ? rpiSsid[k] : ' ';
    }
  }

  if (rpiFlags & RPI_F_LOCKED) out[11] = RPI_G_LOCK;
  else if (rpiFlags & RPI_F_LOCKABLE) out[11] = ((now / 500) & 1) ? ' ' : RPI_G_UNLOCK;
}

// Row 1, 16 chars + NUL.
void rpi_format_row1(char* out) {
  unsigned long now = millis(), since = 0;
  for (uint8_t i = 0; i < 16; i++) out[i] = ' ';
  out[16] = 0;
  uint8_t view = rpi_view(now, &since);
  bool pulse = rpiHaveData && now - rpiLastRxMs < RPI_PULSE_MS;
  switch (view) {
    case RPI_V_WAIT:
      rpi_put(out, PSTR("waiting for RPI"));
      return;
    case RPI_V_OFFLINE:
      rpi_put(out, PSTR("RPI OFFLINE"));
      rpi_clock5(out + 11, now - since);
      return;
    case RPI_V_DOWN: {
      PGM_P what = rpiState == RPI_RESTART ? PSTR("RESTART") :
                   rpiState == RPI_STOP    ? PSTR("WIFILT STOP") :
                   rpiState == RPI_REBOOT  ? PSTR("RPI REBOOT") : PSTR("RPI DOWN");
      rpi_put(out, what);
      rpi_clock5(out + 11, now - since);
      return;
    }
    case RPI_V_UPDATE:
      if (now - rpiLastRxMs <= RPI_FRESH_MS) {
        char t[12];
        if (rpiPct <= 100) snprintf_P(t, sizeof(t), PSTR("UPDATE %u%%"), rpiPct);
        else strcpy_P(t, PSTR("UPDATE"));
        memcpy(out, t, strlen(t));
        if (pulse) out[15] = RPI_G_HEART;
      } else {
        rpi_put(out, PSTR("UPDATE"));
        rpi_clock5(out + 11, now - since);
      }
      return;
    default:
      break;
  }
  // RUN: the address, or a warning every other RPI_ALT_MS.
  PGM_P warn = !(rpiFlags & RPI_F_LTX)     ? PSTR("local-trx off") :
               !(rpiFlags & RPI_F_TRXLINK) ? PSTR("TRX not linked") : nullptr;
  if (warn && ((now / RPI_ALT_MS) & 1)) {
    rpi_put(out, warn);
  } else if (rpiIp[0] || rpiIp[1] || rpiIp[2] || rpiIp[3]) {
    char t[16];
    snprintf_P(t, sizeof(t), PSTR("%u.%u.%u.%u"), rpiIp[0], rpiIp[1], rpiIp[2], rpiIp[3]);
    memcpy(out, t, strlen(t));
  } else {
    rpi_put(out, PSTR("no IP"));
  }
  if (pulse) out[15] = RPI_G_HEART;
}
// =================== END RPI STATUS ===================
