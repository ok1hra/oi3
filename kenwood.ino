// ===================== KENWOOD CAT (TS-480) =====================
// Second CAT backend next to ICOM CI-V, selected by BAND_DECODER_IN == 2.
// Protocol: Kenwood PC control commands - ASCII, two-letter command,
// fixed-width parameters, ';' terminator (TS-480 PC command manual).
//
// Wiring: Serial2 is SHARED with a PC / logger. OI3 RX listens to the rig's
// answers to the logger's queries; OI3 TX is merged into the rig's RXD. So the
// module is primarily a passive sniffer:
//   - every IF / FA / FB / MD / FR answer on the line updates the rig state,
//     whoever asked for it (logger poll, AI auto-information, our own request)
//   - only when no freq/mode data came for KW_INFO_IDLE_MS AND the line has
//     been quiet for KW_QUIET_GAP_MS (not in the middle of a logger
//     transaction) does OI3 send "IF;" itself, at most once per KW_INFO_IDLE_MS
// A logger that polls IF/FA/MD keeps OI3 completely silent on the line.
//
// Rig state stays in CI-V encoding (CivFreq / CivMode / CivValid): the PTT
// mode map, keyer routing, LCD, TrxNet /hz /mode /s-mode and rkey all read it
// unchanged. MD digits are translated both ways (kw_md_to_civ / kw_civ_to_md).
// Watchdog shares civLastParseMs / CIV_WATCHDOG_MS with the CI-V backend (the
// two are mutually exclusive).
//
// IF answer layout (0-based, 37 chars + ';'):
//   [0..1] "IF"  [2..12] freq Hz  [13..17] spaces  [18..22] RIT/XIT offset
//   [23] RIT  [24] XIT  [25] bank  [26..27] M.CH  [28] 0=RX 1=TX  [29] mode
//   [30] VFO (0=A 1=B 2=M.CH)  [31] scan  [32] split  [33] tone  [34..35] tone#
//   [36] space
//
// Low-power TUNE: the rig's own "TX2;" (TX Tune transmission at reduced
// power) and "RX;" - no mode or power changes needed (see LOW-POWER TUNE).
//
// Public API: kw_setup(), kw_loop(), kw_send_setfreq(hz), kw_send_setmode(civ),
//             kw_tx_tune(on)

const unsigned long KW_INFO_IDLE_MS  = 2000;   // no freq/mode data this long -> request "IF;"
const unsigned long KW_QUIET_GAP_MS  = 100;    // line must be silent this long before we talk
const unsigned long KW_QUIET_WAIT_MS = 150;    // max wait for a quiet gap before a set / tune
const unsigned long KW_RX_VERIFY_MS  = 250;    // wait for the IF answer after "RX;"

static char          kwBuf[40];
static uint8_t       kwLen         = 0;
static unsigned long kwLastRxMs    = 0;       // any byte on the line
static unsigned long kwLastInfoMs  = 0;       // last parsed freq/mode
static unsigned long kwLastReqMs   = 0;       // last own "IF;"
static uint8_t       kwVfo         = 0;       // 0=VFO A, 1=VFO B, 2=M.CH (from IF P10 / FR)
static bool          kwTx          = false;   // IF P8: rig transmitting
static uint8_t       kwIfSeq       = 0;       // bumped on every parsed IF (for kw_tx_tune verify)

// TS-480 MD digit -> CI-V mode byte; 0xFF = no CI-V equivalent.
static uint8_t kw_md_to_civ(char md) {
  switch (md) {
    case '1': return 0x00;   // LSB
    case '2': return 0x01;   // USB
    case '3': return 0x03;   // CW
    case '4': return 0x05;   // FM
    case '5': return 0x02;   // AM
    case '6': return 0x04;   // FSK  -> RTTY
    case '7': return 0x07;   // CW-R
    case '9': return 0x08;   // FSK-R -> RTTY-R
    default:  return 0xFF;
  }
}

// CI-V mode byte -> TS-480 MD digit; 0 = no Kenwood equivalent.
static char kw_civ_to_md(uint8_t civ) {
  switch (civ) {
    case 0x00: return '1';
    case 0x01: return '2';
    case 0x02: return '5';
    case 0x03: return '3';
    case 0x04: return '6';
    case 0x05: return '4';
    case 0x07: return '7';
    case 0x08: return '9';
    default:   return 0;
  }
}

// 11 ASCII digits -> Hz; false when any char is not a digit.
static bool kw_parse_freq(const char* p, unsigned long* hz) {
  unsigned long f = 0;
  for (uint8_t i = 0; i < 11; i++) {
    if (p[i] < '0' || p[i] > '9') return false;
    f = f * 10UL + (unsigned long)(p[i] - '0');
  }
  *hz = f;
  return true;
}

static void kw_apply_freq(unsigned long f) {
  civLastParseMs = millis();
  kwLastInfoMs   = civLastParseMs;
  bool wasInvalid = !CivValid;
  CivValid = true;
  if (wasInvalid || f != CivFreq) {
    CivFreq = f;
    if (serialDebug) { Serial.print(F("[CAT freq=")); Serial.print(CivFreq); Serial.println(']'); }
    civ_publish_freq();
  }
}

static void kw_apply_mode(char md) {
  uint8_t m = kw_md_to_civ(md);
  if (m == 0xFF) return;
  civLastParseMs = millis();
  kwLastInfoMs   = civLastParseMs;
  if (m != CivMode) {
    CivMode = m;
    if (serialDebug) {
      Serial.print(F("[CAT mode="));
      if (m < 0x10) Serial.print('0');
      Serial.print(m, HEX);
      Serial.println(']');
    }
    civ_publish_mode();
  }
}

// One complete frame, n chars without the ';'. Anything not recognised (SM,
// PS, "?", "E", "O", ...) is just line activity.
static void kw_parse_frame(const char* f, uint8_t n) {
  if (n < 3) return;
  unsigned long hz;
  if (f[0] == 'I' && f[1] == 'F' && n == 37) {
    if (!kw_parse_freq(&f[2], &hz)) return;
    kwTx = (f[28] == '1');
    if (f[30] >= '0' && f[30] <= '2') kwVfo = (uint8_t)(f[30] - '0');
    kwIfSeq++;
    // TX in split shows the TX VFO - don't bounce /hz (band decoders) on every over
    if (!(kwTx && f[32] == '1')) kw_apply_freq(hz);
    kw_apply_mode(f[29]);
  } else if (f[0] == 'F' && (f[1] == 'A' || f[1] == 'B') && n == 13) {
    if (kwVfo != (uint8_t)(f[1] - 'A')) return;   // not the RX VFO (or M.CH)
    if (kw_parse_freq(&f[2], &hz)) kw_apply_freq(hz);
  } else if (f[0] == 'F' && f[1] == 'R' && n == 3) {
    if (f[2] >= '0' && f[2] <= '2') kwVfo = (uint8_t)(f[2] - '0');
  } else if (f[0] == 'M' && f[1] == 'D' && n == 3) {
    kw_apply_mode(f[2]);
  }
}

// Non-blocking reader: drains Serial2 into the frame buffer.
static void kw_rx_poll() {
  while (Serial2.available() > 0) {
    char c = (char)Serial2.read();
    kwLastRxMs = millis();
    if (c == ';') {
      kw_parse_frame(kwBuf, kwLen);
      kwLen = 0;
    } else if (c == '\r' || c == '\n') {
      // some loggers terminate with CR/LF too - ignore
    } else if (c < 0x20 || c > 0x7E || kwLen >= sizeof(kwBuf)) {
      kwLen = 0;                                   // noise / overflow: resync on next ';'
    } else {
      kwBuf[kwLen++] = c;
    }
  }
}

// Wait (bounded) for a silent gap so we don't talk over a logger transaction.
// Pumps only the RX parser and the sequencer - callable from trxnet_loop().
static void kw_wait_quiet() {
  unsigned long t0 = millis();
  while ((millis() - kwLastRxMs) < KW_QUIET_GAP_MS && (millis() - t0) < KW_QUIET_WAIT_MS) {
    kw_rx_poll();
    ptt_loop();
  }
}

static void kw_send(const char* s) {
  Serial2.print(s);
  Serial2.flush();
}

void kw_setup() {
  Serial.println(F("--- KENWOOD CAT ---"));
  kwLen        = 0;
  kwLastRxMs   = millis();
  kwLastInfoMs = millis() - KW_INFO_IDLE_MS;    // first request as soon as the line is quiet
  kwLastReqMs  = millis() - KW_INFO_IDLE_MS;
}

void kw_loop() {
  if (BAND_DECODER_IN != 2) return;

  kw_rx_poll();

  unsigned long now = millis();
  if ((now - kwLastInfoMs) >= KW_INFO_IDLE_MS
      && (now - kwLastRxMs)  >= KW_QUIET_GAP_MS
      && (now - kwLastReqMs) >= KW_INFO_IDLE_MS) {
    kw_send("IF;");
    kwLastReqMs = now;
  }

  // watchdog
  if (CivValid && (millis() - civLastParseMs) > CIV_WATCHDOG_MS) {
    CivValid = false;
    if (serialDebug) Serial.println(F("[CAT watchdog: no data]"));
  }
}

// Kenwood does not acknowledge a set - the trailing "IF;" makes the rig
// report back through the parser, which re-publishes /hz /mode.
void kw_send_setfreq(uint32_t hz) {
  if (kwVfo > 1) return;                         // M.CH: FA/FB would not move the rig
  char buf[18];
  snprintf(buf, sizeof(buf), "F%c%011lu;IF;", kwVfo == 1 ? 'B' : 'A', (unsigned long)hz);
  kw_wait_quiet();
  kw_send(buf);
  kwLastReqMs = millis();
}

void kw_send_setmode(uint8_t civmode) {
  char md = kw_civ_to_md(civmode);
  if (!md) return;                               // no TS-480 equivalent
  char buf[8] = { 'M', 'D', md, ';', 'I', 'F', ';', 0 };
  kw_wait_quiet();
  kw_send(buf);
  kwLastReqMs = millis();
}

// on: "TX2;" (TX Tune transmission). off: "RX;" and confirm via IF P8 that the
// rig really is back in RX; one retry. Returns false when the rig still
// reports TX (or never answered) - the caller warns the operator.
// "RX;" goes out at once, without waiting for a quiet gap: carrier off must
// not lag (interlock abort); a collision with the logger is caught by the
// IF check and the retry.
bool kw_tx_tune(bool on) {
  if (on) { kw_wait_quiet(); kw_send("TX2;"); return true; }
  for (uint8_t attempt = 0; attempt < 2; attempt++) {
    uint8_t seq0 = kwIfSeq;
    kw_send("RX;IF;");
    kwLastReqMs = millis();
    unsigned long t0 = millis();
    while ((millis() - t0) < KW_RX_VERIFY_MS) {
      kw_rx_poll();
      ptt_loop();
      if (kwIfSeq != seq0 && !kwTx) return true;
    }
  }
  return false;
}
// =================== END KENWOOD CAT ===================
