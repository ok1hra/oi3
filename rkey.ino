// ===================== REMOTE CW KEY (RKEY) =====================
// Receiver side of the remote CW key: an operator's M5Dial ("CTRL") runs the
// iambic keyer + local sidetone and streams the generated key EDGES over UDP;
// this module replays them on CW1/CW2 with a fixed playout delay.
//
// Protocol: rkey_proto.h (shared with the CTRL firmware).
//
// Timing model
//   playAt = t_sender + rkOffset,  rkOffset = min(arrive - t_sender) + D
//   - min() over a sliding 1–2 s window = network latency without jitter
//   - D = playout delay (RkeyDelayMs, or the one requested by CTRL) + adaptive
//     extra that grows on late edges and decays at the start of each over
//   - edges are emitted by a Timer1 compare ISR (4 µs resolution), so nothing
//     in loop() (LCD, CI-V, SPI to W5500, blocking keyer waits) adds jitter
//   - a late KEY-DOWN shifts the rest of the over (gap absorbs it, mark length
//     preserved); a late KEY-UP is applied immediately (counted)
//   - clock drift CTRL vs. RIG (crystal vs. resonator, up to 0.5 %) is slewed
//     out at most 1.5 ms per mark start, i.e. always inside a gap
//
// Safety
//   - nothing is keyed unless: RkeyEnable, key set, CW/CWR mode, no interlock,
//     no low-power tune running
//   - key-down with no packet for RkeyLinkTimeoutMs, or longer than
//     RkeyMaxDownMs -> abort + mute
//   - edge gap larger than the redundancy (lost edges) -> abort + mute
//   - after an abort the stream stays muted until CTRL reports idle, so a
//     stale/partial over is never transmitted
//   - ptt_abort_all() (interlock) calls rkey_abort()
//   - SipHash-2-4 tag + RIG-issued nonce + monotonic seq: no forged or replayed
//     packet can key the transmitter
//
// HW resources: Timer1 (free on OI3 — tone() uses Timer2, millis() Timer0),
// one W5500/W5100 UDP socket on RkeyPort.
//
// Public API: rkey_setup(), rkey_loop(), rkey_ptt_wanted(), rkey_busy(),
//             rkey_abort(), rkey_cfg(name, value)

#if defined(FEATURE_NET)
#include "rkey_proto.h"

// ---------------- configuration (oi3.cfg) ----------------
bool     RkeyEnable        = false;
uint16_t RkeyPort          = RKEY_DEFAULT_PORT;
uint16_t RkeyDelayMs       = 120;    // default playout delay (CTRL may override)
uint16_t RkeyLinkTimeoutMs = 300;    // key-down without packets -> abort
uint16_t RkeyMaxDownMs     = 3000;   // longest allowed single key-down
bool     RkeyRequireCiv    = true;   // false: use local CivMode fallback when CI-V silent
uint8_t  RkeyKey[16];
bool     RkeyKeySet        = false;

static int8_t rk_hexval(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

// Called from readSDSettings(). Returns true when the key belonged to RKEY.
bool rkey_cfg(const String& name, const String& value) {
  String v = value; v.trim();
  if (name == "RkeyEnable")        { RkeyEnable = (v.toInt() != 0); return true; }
  if (name == "RkeyPort")          { long p = v.toInt(); if (p >= 1 && p <= 65535) RkeyPort = (uint16_t)p; return true; }
  if (name == "RkeyDelayMs")       { long d = v.toInt(); if (d >= 10 && d <= 1000) RkeyDelayMs = (uint16_t)d; return true; }
  if (name == "RkeyLinkTimeoutMs") { long d = v.toInt(); if (d >= 50 && d <= 5000) RkeyLinkTimeoutMs = (uint16_t)d; return true; }
  if (name == "RkeyMaxDownMs")     { long d = v.toInt(); if (d >= 500 && d <= 30000) RkeyMaxDownMs = (uint16_t)d; return true; }
  if (name == "RkeyRequireCiv")    { RkeyRequireCiv = (v.toInt() != 0); return true; }
  if (name == "RkeyKey") {          // 32 hex chars = 128-bit shared key
    if (v.length() != 32) { RkeyKeySet = false; return true; }
    for (uint8_t i = 0; i < 16; i++) {
      int8_t hi = rk_hexval(v.charAt(2 * i)), lo = rk_hexval(v.charAt(2 * i + 1));
      if (hi < 0 || lo < 0) { RkeyKeySet = false; return true; }
      RkeyKey[i] = (uint8_t)((hi << 4) | lo);
    }
    uint8_t any = 0;
    for (uint8_t i = 0; i < 16; i++) any |= RkeyKey[i];
    RkeyKeySet = (any != 0);                  // all-zero placeholder key = not configured
    return true;
  }
  return false;
}

// ---------------- tunables ----------------
const uint32_t RK_MARGIN_US       = 1500;     // min. lead when handing an edge to the ISR
const int32_t  RK_SLEW_US         = 1500;     // max drift correction per mark start
const uint32_t RK_EXTRA_MAX_US    = 500000;   // cap of adaptive extra delay
const int32_t  RK_STALE_US        = 400000;   // packet older than this beyond min latency = stale
const uint16_t RK_STATUS_MIN_MS   = 100;      // status rate limit
const uint16_t RK_NONCE_ROTATE_MS = 5000;
const uint16_t RK_READY_TIMEOUT_MS = 3000;    // sequencer never got ready -> abort
const int      RK_EEPROM_BOOTCTR  = 4092;     // 4 B boot counter (nonce entropy), 4092..4095

// ---------------- ISR edge queue ----------------
#define RK_Q 32                                // power of two
struct RkEv { uint32_t at; uint8_t down; };
static volatile RkEv     rkQ[RK_Q];
static volatile uint8_t  rkQHead = 0, rkQTail = 0;
static volatile bool     rkOutDown = false;
static volatile uint32_t rkDownSinceUs = 0;
static volatile uint32_t rkLastUpUs = 0;
static volatile uint8_t* rkPort1;  static uint8_t rkMask1;
static volatile uint8_t* rkPort2;  static uint8_t rkMask2;

static inline void rk_out_locked(bool down) {     // interrupts must be off
  if (down) { *rkPort1 |= rkMask1;  *rkPort2 |= rkMask2; }
  else      { *rkPort1 &= ~rkMask1; *rkPort2 &= ~rkMask2; }
}

// Program Timer1 compare A for the queue head. Interrupts must be off.
static void rk_arm_locked(uint32_t now) {
  if (rkQHead == rkQTail) { TIMSK1 &= ~_BV(OCIE1A); return; }
  int32_t d = (int32_t)(rkQ[rkQTail].at - now);
  uint32_t ticks = (d <= 40) ? 10 : ((uint32_t)d >> 2);   // 4 µs/tick, min 40 µs
  if (ticks < 10)    ticks = 10;
  if (ticks > 60000) ticks = 60000;                          // re-check every 240 ms
  OCR1A  = (uint16_t)(TCNT1 + ticks);
  TIFR1  = _BV(OCF1A);
  TIMSK1 |= _BV(OCIE1A);
}

ISR(TIMER1_COMPA_vect) {
  uint32_t now = micros();
  while (rkQHead != rkQTail && (int32_t)(now - rkQ[rkQTail].at) >= 0) {
    bool d = rkQ[rkQTail].down;
    rk_out_locked(d);
    if (d) { rkOutDown = true;  rkDownSinceUs = now; }
    else   { rkOutDown = false; rkLastUpUs = now; }
    rkQTail = (rkQTail + 1) & (RK_Q - 1);
  }
  rk_arm_locked(now);
}

static bool rk_q_push(uint32_t at, bool down) {
  uint8_t s = SREG; cli();
  uint8_t nh = (rkQHead + 1) & (RK_Q - 1);
  if (nh == rkQTail) { SREG = s; return false; }
  rkQ[rkQHead].at = at;
  rkQ[rkQHead].down = down ? 1 : 0;
  rkQHead = nh;
  rk_arm_locked(micros());
  SREG = s;
  return true;
}

static bool rk_q_empty() { uint8_t s = SREG; cli(); bool e = (rkQHead == rkQTail); SREG = s; return e; }

// ---------------- receiver state ----------------
static EthernetUDP rkUdp;
static bool     rkUdpUp = false;
static bool     rkLinkPrev = false;

static bool     rkHaveSession = false;
static uint16_t rkSession = 0;
static uint32_t rkLastSeq = 0;
static uint16_t rkNextEvIdx = 0;
static bool     rkMuted = true;          // until CTRL reports idle
static bool     rkLastState = false;     // state of the last edge accepted

static IPAddress rkPeerIp;
static uint16_t rkPeerPort = 0;
static bool     rkPeerValid = false;
static uint32_t rkEchoTSend = 0, rkLastPktUs = 0;
static unsigned long rkLastPktMs = 0;
static uint8_t  rkWpm = 20;
static uint16_t rkReqDelayMs = 0;

// pending edges (accepted, waiting for TX ready)
static uint32_t rkPend[RK_Q];
static uint8_t  rkPendH = 0, rkPendT = 0;

// timing
static int32_t  rkSkewCur = 0x7FFFFFFFL, rkSkewPrev = 0x7FFFFFFFL;
static unsigned long rkSkewWinMs = 0;
static bool     rkAnchored = false;
static uint32_t rkOffset = 0;
static uint32_t rkExtraUs = 0;
static uint32_t rkLastAt = 0;
static bool     rkPtt = false;
static unsigned long rkReadySinceMs = 0, rkWaitSinceMs = 0;

// nonce
static uint32_t rkNonce = 0, rkNoncePrev = 0, rkNonceCtr = 0, rkBootCtr = 0;
static unsigned long rkNonceMs = 0;
static bool     rkNonceBad = false;

// stats / status
static uint16_t rkLateEv = 0, rkLostEv = 0, rkRxPkts = 0, rkBadAuth = 0;
static uint32_t rkStatusSeq = 0;
static unsigned long rkLastStatusMs = 0;

// ---------------- helpers ----------------
static void rk_skew_reset() { rkSkewCur = rkSkewPrev = 0x7FFFFFFFL; rkSkewWinMs = millis(); }
static void rk_skew_sample(int32_t s) {
  if (millis() - rkSkewWinMs >= 1000) { rkSkewPrev = rkSkewCur; rkSkewCur = 0x7FFFFFFFL; rkSkewWinMs = millis(); }
  if (s < rkSkewCur) rkSkewCur = s;
}
static int32_t rk_skew_min() { return (rkSkewCur < rkSkewPrev) ? rkSkewCur : rkSkewPrev; }

static uint32_t rk_D_us() {
  uint32_t base = (uint32_t)(rkReqDelayMs ? rkReqDelayMs : RkeyDelayMs) * 1000UL;
  return base + rkExtraUs;
}

static void rk_nonce_rotate() {
  rkNoncePrev = rkNonce;
  struct { uint32_t t, ctr, boot; uint16_t a; } s = { micros(), ++rkNonceCtr, rkBootCtr,
                                                       (uint16_t)(analogRead(DC3V) ^ (analogRead(DCIN) << 6)) };
  rkNonce = (uint32_t)rkey_siphash(RkeyKey, (const uint8_t*)&s, sizeof(s));
  if (rkNonce == 0) rkNonce = 1;
  rkNonceMs = millis();
}
static bool rk_nonce_ok(uint32_t n) { return n != 0 && (n == rkNonce || n == rkNoncePrev); }

static bool rk_cw_mode() {
  bool cw = (CivMode == 0x03 || CivMode == 0x07);
  return cw && (CivValid || !RkeyRequireCiv);
}

// Hard permission to key at all. Failing it while busy = abort.
static bool rk_allowed() {
  return RkeyEnable && RkeyKeySet && EthLinkStatus && rk_cw_mode()
         && ptt_interlock_active == 0 && !tunePttRequest;
}

// Sequencer has PTT up and settled (or PTT output is disabled).
static bool rk_tx_ready() {
  if (keyerPttRequest) { rkReadySinceMs = 0; return false; }   // local keyer still owns TX
  if (!PttOutputEnable) return true;
  bool up = pttExternal ? (SequencerLevel == 4) : (SequencerLevel == PttOut);
  if (!up) { rkReadySinceMs = 0; return false; }
  if (rkReadySinceMs == 0) rkReadySinceMs = millis() | 1;
  return (millis() - rkReadySinceMs) >= (unsigned long)PTTlead;
}

static void rk_abort_internal(const __FlashStringHelper* why) {
  uint8_t s = SREG; cli();
  rkQTail = rkQHead;
  TIMSK1 &= ~_BV(OCIE1A);
  rk_out_locked(false);
  if (rkOutDown) rkLastUpUs = micros();
  rkOutDown = false;
  SREG = s;
  rkPendH = rkPendT = 0;
  rkPtt = false;
  rkAnchored = false;
  rkLastState = false;
  rkReadySinceMs = 0;
  rkWaitSinceMs = 0;
  if (why && serialDebug) { Serial.print(F("[RKEY abort ")); Serial.print(why); Serial.println(']'); }
}

// ---------------- status to CTRL ----------------
static void rk_send_status(bool force) {
  if (!rkPeerValid || !rkUdpUp) return;
  if (!force && (millis() - rkLastStatusMs) < RK_STATUS_MIN_MS) return;
  rkLastStatusMs = millis();
  uint8_t buf[sizeof(RkeyStatus) + RKEY_TAG_LEN];
  RkeyStatus st;
  st.magic = RKEY_MAGIC; st.ver = RKEY_VER; st.type = RKEY_T_STATUS;
  st.flags = (CivValid ? RKEY_SF_CIV_VALID : 0)
           | (rk_cw_mode() ? RKEY_SF_CW_MODE : 0)
           | (ptt_interlock_active ? RKEY_SF_INTERLOCK : 0)
           | (rkOutDown ? RKEY_SF_KEY_DOWN : 0)
           | (SequencerLevel != 0 ? RKEY_SF_PTT : 0)
           | (rkNonceBad ? RKEY_SF_NONCE_BAD : 0)
           | (rkMuted ? RKEY_SF_MUTED : 0)
           | (tunePttRequest ? RKEY_SF_TUNE : 0);
  st.session   = rkSession;
  st.civMode   = CivMode;
  st.seqLevel  = SequencerLevel;
  st.seq       = ++rkStatusSeq;
  st.echoTSend = rkEchoTSend;
  st.echoAgeUs = micros() - rkLastPktUs;
  st.nonce     = rkNonce;
  st.freqHz    = CivValid ? (uint32_t)CivFreq : 0;
  st.delayMs   = (uint16_t)(rk_D_us() / 1000UL);
  st.lateEv    = rkLateEv;
  st.lostEv    = rkLostEv;
  st.rxPkts    = rkRxPkts;
  memcpy(buf, &st, sizeof(st));
  uint16_t n = rkey_sign(RkeyKey, buf, sizeof(st));
  rkUdp.beginPacket(rkPeerIp, rkPeerPort);
  rkUdp.write(buf, n);
  rkUdp.endPacket();
}

// ---------------- edge intake ----------------
static void rk_pend_push(uint32_t e) {
  uint8_t nh = (rkPendH + 1) & (RK_Q - 1);
  if (nh == rkPendT) { rk_abort_internal(F("pend full")); rkMuted = true; return; }
  rkPend[rkPendH] = e;
  rkPendH = nh;
}

// Hand one edge to the ISR queue with its absolute play time.
static void rk_schedule_edge(uint32_t e) {
  bool down = RKEY_EV_DOWN(e);
  uint32_t t = RKEY_EV_TIME(e);
  uint32_t now = micros();

  if (!rkAnchored) {
    // New over: anchor on the jitter-free latency estimate. If the sequencer is
    // down, make sure D covers SEQ+PA+PTT lead so the first mark is not late.
    uint32_t D = rk_D_us();
    if (PttOutputEnable) {
      uint32_t lead = ((uint32_t)SEQUENCERlead + PAlead + PTTlead) * 1000UL + 5000UL;
      if (D < lead) D = lead;
    }
    rkOffset   = (uint32_t)rk_skew_min() + D;
    rkAnchored = true;
    rkLastAt   = now;
    if (serialDebug) { Serial.print(F("[RKEY anchor D=")); Serial.print(D / 1000UL); Serial.println(F("ms]")); }
  } else if (down) {
    // Drift correction, only at mark start (i.e. inside a gap).
    int32_t target = rk_skew_min() + (int32_t)rk_D_us();
    int32_t diff = target - (int32_t)rkOffset;
    if (diff >  RK_SLEW_US) diff =  RK_SLEW_US;
    if (diff < -RK_SLEW_US) diff = -RK_SLEW_US;
    rkOffset += (uint32_t)diff;
  }

  uint32_t at = t + rkOffset;
  int32_t lead = (int32_t)(at - now);
  if (down && lead < (int32_t)RK_MARGIN_US) {
    // Late mark start: shift the rest of the over, keep the mark length.
    uint32_t sh = (uint32_t)((int32_t)RK_MARGIN_US - lead);
    rkOffset += sh; at += sh;
    rkExtraUs += sh + 2000UL;
    if (rkExtraUs > RK_EXTRA_MAX_US) rkExtraUs = RK_EXTRA_MAX_US;
    rkLateEv++;
  } else if (!down && lead < 0) {
    at = now;                                    // late key-up: release now
    rkLateEv++;
  }
  if ((int32_t)(at - rkLastAt) < 0) at = rkLastAt;   // keep queue monotonic
  rkLastAt = at;
  if (!rk_q_push(at, down)) { rk_abort_internal(F("queue full")); rkMuted = true; }
}

static void rk_on_packet(const uint8_t* buf, uint16_t n, IPAddress ip, uint16_t port) {
  uint32_t arrive = micros();
  if (n < sizeof(RkeyKeyHdr) + RKEY_TAG_LEN) return;
  RkeyKeyHdr h;
  memcpy(&h, buf, sizeof(h));
  if (h.magic != RKEY_MAGIC || h.ver != RKEY_VER || h.type != RKEY_T_KEY) return;
  if (h.nev > RKEY_MAX_EV) return;
  if (n != sizeof(RkeyKeyHdr) + 4u * h.nev + RKEY_TAG_LEN) return;
  if (!rkey_verify(RkeyKey, buf, n)) { rkBadAuth++; return; }

  // Authenticated: this is our CTRL. Remember where to send status.
  rkPeerIp = ip; rkPeerPort = port; rkPeerValid = true;
  bool nonceOk = rk_nonce_ok(h.nonce);
  rkNonceBad = !nonceOk;

  if (!rkHaveSession || h.session != rkSession) {
    // A new session is accepted only with a fresh nonce -> old captures can't replay.
    if (!nonceOk) { rk_send_status(true); return; }
    if (rkHaveSession) rk_abort_internal(F("new session"));
    rkHaveSession = true;
    rkSession   = h.session;
    rkNextEvIdx = h.evIdx0 + h.nev;              // never replay history of a session we just met
    rkMuted     = true;
    rkLastState = false;
    rkExtraUs   = 0;
    rk_skew_reset();
  } else if ((int32_t)(h.seq - rkLastSeq) <= 0) {
    return;                                      // duplicate / reordered / replayed
  }
  rkLastSeq   = h.seq;
  rkLastPktMs = millis();
  rkLastPktUs = arrive;
  rkEchoTSend = h.tSend;
  rkWpm       = (h.wpm < 5) ? 5 : (h.wpm > 60 ? 60 : h.wpm);
  rkReqDelayMs = (h.delayMs >= 10 && h.delayMs <= 1000) ? h.delayMs : 0;
  rkRxPkts++;

  int32_t s = (int32_t)(arrive - h.tSend);
  rk_skew_sample(s);
  rk_send_status(!nonceOk);
  if (!nonceOk) return;                          // heartbeat only, edges come again in next packets

  const uint8_t* ev = buf + sizeof(RkeyKeyHdr);
  bool stale = (s - rk_skew_min()) > RK_STALE_US;

  // Mute release (checked BEFORE consuming this packet's edges, so a released
  // stream starts with edges that are newer than the word gap):
  //  - CTRL idle (no edges in its window), or
  //  - CTRL's newest edge is a key-up older than 5 dits (longer than a letter
  //    space of 3, shorter than a word space of 7) -> resume at the next word,
  //    never in the middle of a character.
  if (rkMuted && rk_allowed()) {
    bool release = (h.nev == 0);
    if (!release) {
      uint32_t last; memcpy(&last, ev + 4u * (h.nev - 1), 4);
      uint32_t wordUs = 5UL * 1200000UL / rkWpm;
      bool idxKnown = ((int16_t)((uint16_t)(h.evIdx0 + h.nev) - rkNextEvIdx) <= 0);  // nothing unseen
      release = idxKnown && !RKEY_EV_DOWN(last) && (h.tSend - RKEY_EV_TIME(last)) > wordUs;
    }
    if (release) { rkMuted = false; rkLastState = false; }
  }

  for (uint8_t i = 0; i < h.nev; i++) {
    uint16_t idx = h.evIdx0 + i;
    int16_t rel = (int16_t)(idx - rkNextEvIdx);
    if (rel < 0) continue;                       // already have it
    uint32_t e; memcpy(&e, ev + 4u * i, 4);
    rkNextEvIdx = idx + 1;
    if (rel > 0) {                               // edges lost beyond redundancy
      rkLostEv += rel;
      if (!rkMuted) { rk_abort_internal(F("edge gap")); rkMuted = true; }
    }
    if (rkMuted || stale) {
      if (stale && !rkMuted) { rk_abort_internal(F("stale")); rkMuted = true; }
      continue;
    }
    bool down = RKEY_EV_DOWN(e);
    if (down == rkLastState) continue;           // ignore non-transition
    rkLastState = down;
    rk_pend_push(e);
  }
}

// ---------------- service ----------------
static void rk_service() {
  bool pend  = (rkPendH != rkPendT);
  bool qbusy = !rk_q_empty();
  uint8_t sr = SREG; cli();
  bool outDown = rkOutDown;
  uint32_t downSince = rkDownSinceUs, lastUp = rkLastUpUs;
  SREG = sr;

  if (!rk_allowed()) {
    if (pend || qbusy || outDown || rkPtt) { rk_abort_internal(F("not allowed")); rkMuted = true; }
    rkPendH = rkPendT;                           // drop anything that slipped in
    return;
  }

  // Watchdogs
  if (outDown && (millis() - rkLastPktMs) > RkeyLinkTimeoutMs) { rk_abort_internal(F("link")); rkMuted = true; return; }
  if (outDown && (micros() - downSince) > (uint32_t)RkeyMaxDownMs * 1000UL) { rk_abort_internal(F("max down")); rkMuted = true; return; }

  if (pend) {
    if (!rkPtt) {
      rkPtt = true;
      rkWaitSinceMs = millis();
      if (!rkAnchored) rkExtraUs = (rkExtraUs * 3) / 4;   // adaptive extra decays per over
    }
    if (rk_tx_ready()) {
      rkWaitSinceMs = 0;
      while (rkPendT != rkPendH) {
        rk_schedule_edge(rkPend[rkPendT]);
        rkPendT = (rkPendT + 1) & (RK_Q - 1);
      }
    } else if (rkWaitSinceMs && (millis() - rkWaitSinceMs) > RK_READY_TIMEOUT_MS) {
      rk_abort_internal(F("seq timeout")); rkMuted = true; return;
    }
  }

  // PTT hang: 7 dits of silence at sender WPM, then release (sequencer adds PTTtail)
  // (queue/output state re-read here: edges may have just been handed to the ISR)
  sr = SREG; cli();
  qbusy = (rkQHead != rkQTail); outDown = rkOutDown; lastUp = rkLastUpUs;
  SREG = sr;
  if (rkPtt && rkPendH == rkPendT && !qbusy && !outDown) {
    uint32_t hangUs = 7UL * 1200000UL / rkWpm;
    if ((micros() - lastUp) > hangUs) { rkPtt = false; rkAnchored = false; }
  }
}

// ---------------- public API ----------------
bool rkey_ptt_wanted() { return rkPtt; }

bool rkey_busy() {
  if (rkPtt || rkPendH != rkPendT) return true;
  uint8_t s = SREG; cli();
  bool b = rkOutDown || (rkQHead != rkQTail);
  SREG = s;
  return b;
}

void rkey_abort() { rk_abort_internal(F("ext")); rkMuted = true; }

void rkey_setup() {
  rkPort1 = portOutputRegister(digitalPinToPort(CW1)); rkMask1 = digitalPinToBitMask(CW1);
  rkPort2 = portOutputRegister(digitalPinToPort(CW2)); rkMask2 = digitalPinToBitMask(CW2);
  // Timer1: normal mode, clk/64 -> 4 µs per tick, compare A used as one-shot alarm
  TCCR1A = 0;
  TCCR1B = _BV(CS11) | _BV(CS10);
  TIMSK1 = 0;
  // boot counter -> nonce entropy across reboots
  uint32_t bc; EEPROM.get(RK_EEPROM_BOOTCTR, bc); bc++; EEPROM.put(RK_EEPROM_BOOTCTR, bc);
  rkBootCtr = bc;
  Serial.print(F("RKEY ")); Serial.print(RkeyEnable ? F("on") : F("off"));
  Serial.print(F(" port=")); Serial.print(RkeyPort);
  Serial.print(F(" D=")); Serial.print(RkeyDelayMs);
  Serial.println(RkeyKeySet ? F("ms key ok") : F("ms NO KEY -> disabled"));
}

void rkey_loop() {
  if (!RkeyEnable || !RkeyKeySet) return;

  // (Re)open socket on link-up; ethernet_loop() re-inits the stack on link change.
  if (EthLinkStatus && !rkLinkPrev) { rkUdp.stop(); rkUdpUp = (rkUdp.begin(RkeyPort) == 1); rk_nonce_rotate(); rk_nonce_rotate(); }
  if (!EthLinkStatus && rkLinkPrev) { rk_abort_internal(F("eth down")); rkMuted = true; rkUdpUp = false; }
  rkLinkPrev = EthLinkStatus;
  if (!rkUdpUp) return;

  if ((millis() - rkNonceMs) > RK_NONCE_ROTATE_MS) rk_nonce_rotate();

  uint8_t buf[sizeof(RkeyKeyHdr) + 4 * RKEY_MAX_EV + RKEY_TAG_LEN];
  for (uint8_t k = 0; k < 8; k++) {             // bounded work per call
    int n = rkUdp.parsePacket();
    if (n <= 0) break;
    if (n > (int)sizeof(buf)) { while (rkUdp.available()) rkUdp.read(); continue; }
    rkUdp.read(buf, n);
    rk_on_packet(buf, (uint16_t)n, rkUdp.remoteIP(), rkUdp.remotePort());
  }
  rk_service();
}

#else  // !FEATURE_NET — PCB without Ethernet
bool rkey_cfg(const String&, const String&) { return false; }
bool rkey_ptt_wanted() { return false; }
bool rkey_busy()       { return false; }
void rkey_abort()      {}
void rkey_setup()      {}
void rkey_loop()       {}
#endif
// =================== END REMOTE CW KEY ===================
