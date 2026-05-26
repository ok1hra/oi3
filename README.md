# OI3 — Firmware Manual

Firmware for **Open Interface III** — a station interface between PC and an
Icom-family transceiver (CAT, PTT/sequencer, CW/RTTY/FSK keying, optional
Ethernet/TrxNet networking). This document describes what the firmware
does, how the front-panel UI works, and what every menu item / config key
means.

> For the hardware itself (front/rear panel layout, DB25 wiring,
> schematics, jumpers, per-rig connector diagrams), see
> [doc/open-interface-iii.md](doc/open-interface-iii.md) — linked again at
> the bottom of this document.

---

## First boot

Power-up sequence on the 16×2 LCD:

1. `FW <date>` banner (~0.5 s).
2. SD card status on row 1 — `loaded oi3.cfg`, `SD card missing`, or
   `SD init fail`. Missing SD just means the firmware boots with compiled
   defaults; nothing is broken.
3. If an Ethernet module is fitted: `Net-ID: NN` / `Eth: waiting…`, then
   either the assigned `IP address:` for ~1 s, or `Eth: no link` /
   `DHCP fail`.
4. Runtime view appears on row 0. Whatever menu item was active when you
   last powered off is restored (persisted in EEPROM). Row 1 is reserved
   for keyer TX text and transient WPM notifications.

The boot default CI-V mode is **CW** (0x03). As soon as the rig answers
CI-V polls, the rig's real mode takes over.

---

## Display

The display is a 16×2 character LCD. The two rows have separate jobs.

### Row 0 — menu / value / mode

```
HHHHHHHHHHHH|MMM
└─ value ──┘│└mode┘
         separator
```

- **Value field** (12 chars) — left-aligned, content depends on the
  current menu item (see [Menu items](#menu-items)).
- **Separator** (1 char) — encodes the menu FSM state:
  - `|` PINNED — value is displayed; DWN/UP edit live WPM.
  - `<` BROWSING — DWN/UP step through menu items.
  - `=` EDITING — DWN/UP change the value of the current item.
- **Mode field** (3 chars) — current CI-V mode, **uppercase** when CI-V
  is alive (`LSB USB AM  CW  RTY FM  CWR RTR`), **lowercase** when the
  rig is silent and the value is the local fallback driven by the MODE
  button. `---` is shown only as a safety fallback if the mode byte is
  unknown.

### Row 1 — keyer TX echo + transient overlays

- During CW/RTTY transmission, every element that actually went on the
  air is echoed here (wrap mode — when col 16 is reached the row is
  cleared and writing restarts at col 0). Trailing spaces at the wrap
  edge are eaten so the row never starts with a space.
- Pressing DWN/UP in PINNED state changes WPM and briefly overlays
  `WPM NN` here for ~0.8 s — keyer chars from that window are dropped
  *from the LCD only* (the air output continues unaffected).
- The paddle decoder writes its decoded ASCII here too — so the LCD is
  a truthful log of what was keyed, whether by paddle, by USB host, or
  by a remote TrxNet peer.
- On SD / Ethernet boot stages, row 1 hosts the status banners.

---

## Buttons

Five physical inputs drive the UI:

| Button | Pin      | Notes                                                |
|--------|----------|------------------------------------------------------|
| SET    | A1 (ADC) | Enter/leave BROWSING; long-press in BROWSING = EDIT  |
| DWN    | A1 (ADC) | Decrement / previous item                            |
| UP     | A1 (ADC) | Increment / next item                                |
| MODE   | D36      | Rotate CI-V mode locally (only when CI-V is silent)  |
| FootSW | D19      | External PTT request (LOW = TX)                      |

SET / DWN / UP share one analog pin via a resistor divider. Decision
levels are the midpoints between measured raw ADC values (SET≈2,
DWN≈89, UP≈167):

| Button | Raw range            |
|--------|----------------------|
| SET    | `tmp < 46`           |
| DWN    | `46 < tmp < 128`     |
| UP     | `128 < tmp < 230`    |

### Auto-repeat

In PINNED and EDITING states, holding DWN or UP for ≥700 ms starts a
3 Hz auto-repeat. BROWSING is **edge-only** — one menu step per press,
no auto-repeat (so you can't accidentally fly past the item you want).

### Tones

A short two-step tone sweep marks **BROWSING ↔ EDITING** transitions only:

- low → high (300 → 800 Hz) entering EDITING
- high → low (800 → 300 Hz) leaving EDITING (after save)

No tones for PINNED↔BROWSING, DWN/UP steps, or MODE rotation. The tone
generator (D4) is shared with the keyer sidetone with no mutex —
overlap is rare in practice and audibly harmless.

---

## Menu navigation (FSM)

The menu has three states. Only the SET button changes state; DWN/UP do
different things depending on which state you're in.

```
        ┌───────────────────────────────────────────┐
        │                                           │
        ▼                                           │
   ┌─────────┐  any SET   ┌────────────┐ short SET  │
   │ PINNED  │ ─────────► │  BROWSING  │────────────┘
   │   '|'   │            │    '<'     │  (or 30 s idle)
   │ DWN/UP  │            │ DWN/UP=    │
   │ = WPM   │            │ prev/next  │ long SET
   │ ± live  │            │            │ (if editable)
   └─────────┘            └────────────┘
                                 ▲ │
                       any SET   │ │
                       (save +   │ │
                        sweep)   │ ▼
                            ┌────────────┐
                            │  EDITING   │
                            │    '='     │
                            │  DWN/UP =  │
                            │  value ±   │
                            └────────────┘
```

Key rules:

- **Short vs. long SET** only matters in BROWSING. Long-press threshold
  is 800 ms; long-press in PINNED or EDITING has no special meaning.
- **PINNED → BROWSING** fires on the SET press edge.
- **BROWSING → PINNED** fires on the SET release edge **and persists the
  current menu index to EEPROM** (so the unit boots back into the same
  view).
- **BROWSING → PINNED** also happens automatically after **30 s of no
  button activity** in BROWSING — same EEPROM save. You can't get
  stranded inside the menu.
- **BROWSING → EDITING** fires on the long-press threshold while still
  held, but only if the current item is editable.
- **EDITING → BROWSING** saves the new value (to EEPROM for persisted
  items) and plays the high→low sweep. There is no cancel path — once
  you press SET in EDITING, the change is committed.
- Read-only items render a space between name and value; editable items
  in BROWSING/EDITING render `=` instead, as a visual hint that
  long-press will start editing.

### MODE button

`MODE` rotates the active CI-V mode locally through the cycle
`LSB → USB → AM → CW → RTY → FM → CWR → RTR → LSB`.

It is **only active while CI-V is silent** (`!CivValid`). Once the rig
is answering polls, the rig is the source of truth — pressing MODE
does nothing. This lets you operate sensibly with the rig powered off
(set CW to use the keyer dry, set RTY to route PTT to the DAT jack for
AFSK from the PC, etc.) without ever lying about the rig's real state.

---

## Menu items

23 items, indexed 0..22. Items 0..6 are read-only status; 7 is browse-
only (peer list); 8..22 are editable settings. The mode cell on the
right of row 0 is the same on every item.

| #  | Name          | Example display      | Editable | Range / values                  | Purpose |
|----|---------------|----------------------|----------|---------------------------------|---------|
| 0  | My call       | `OK1HRA`             | no       | — (from cfg)                    | Operator callsign, set in `oi3.cfg`. |
| 1  | FW version    | `FW 20260526`        | no       | — (compile-time)                | Build date stamp. |
| 2  | IP high       | `192.168.`           | no       | — / `no link`                   | First two octets of the assigned IP. |
| 3  | IP low        | `1.220`              | no       | — / `no link`                   | Last two octets. Items 2+3 together = full IP. |
| 4  | Baud (CAT)    | `baud 9600`          | no       | — (from cfg)                    | Serial2 baudrate used for CI-V. |
| 5  | CI-V addr     | `CI-V 0x98`          | no       | — (from cfg)                    | Configured radio address (0x98=IC-7610, 0x56=IC-746). |
| 6  | Frequency     | `14.074.0 kHz`       | no       | — / `---.---.- kHz`             | Live rig frequency, 100 Hz resolution. Placeholder while `!CivValid`. |
| 7  | Peers         | `Peers 2` / `Peers off` | browse | EDITING = walk peer list       | Number of remote TrxNet peers, EDITING cycles through them showing `name .lastIP`. `off` = NET_ID=0 / no Eth. |
| 8  | Interlock     | `Inter 1`            | yes (not persisted) | 0 ↔ 1                | `1` = D2 is PA safety interlock; `0` = D2 is external PTT-in. See [PTT, sequencer and interlock](#ptt-sequencer-and-interlock). |
| 9  | SEQ lead      | `SeqH 0ms`           | yes (not persisted) | 0..9990 ms step 10   | Delay from SEQUENCER HIGH to PTTPA HIGH. |
| 10 | PA lead       | `PAH 10ms`           | yes (not persisted) | 0..9990 ms step 10   | Delay from PTTPA HIGH to PTT1/2/3 HIGH. |
| 11 | PTT lead      | `PTTH 10ms`          | yes (not persisted) | 0..9990 ms step 10   | Loaded from cfg, **currently unused** by the sequencer (kept for forward compat). |
| 12 | PTT tail      | `PTTL 210ms`         | yes (not persisted) | 0..9990 ms step 10   | Delay after TX-off before PTT1/2/3 LOW. |
| 13 | PA tail       | `PAL 0ms`            | yes (not persisted) | 0..9990 ms step 10   | Delay from PTT1/2/3 LOW to PTTPA LOW. |
| 14 | SEQ tail      | `SeqL 0ms`           | yes (not persisted) | 0..9990 ms step 10   | Delay from PTTPA LOW to SEQUENCER LOW. |
| 15 | PTT output    | `PTT1` / `TX PTT3` / `PTT D` | yes (not persisted) | 1, 2, 3 | Effective PTT pin for the **current CI-V mode**. `D`=default (no mapping / CI-V silent), `TX `=sequencer active. Edit changes the per-mode slot in RAM; `oi3.cfg` repopulates after reset. |
| 16 | WPM           | `WPM 28`             | yes      | 5..50 step 1                    | Keyer speed. Also editable via DWN/UP in PINNED. |
| 17 | Iambic mode   | `Mode B`             | yes      | A ↔ B                           | Iambic A or B. |
| 18 | Paddle        | `Pad N` / `Pad R`    | yes      | Normal ↔ Reverse                | Swap dit/dah paddle assignment. |
| 19 | Sidetone      | `Side 600Hz` / `Side off` | yes | 0 (off) or 300..1200 Hz step 10 | Keyer sidetone frequency. Crosses 300↔0 boundary. |
| 20 | RTTY baud     | `RTY 45.45 Bd`       | yes      | 45, 50, 75, 100                 | FSK Baudot rate (45 stored as 45 but clocked at 45.45 Bd). |
| 21 | FSK polarity  | `FSK N` / `FSK R`    | yes      | Normal ↔ Reverse                | Invert MARK/SPACE drive levels on the FSK pin. |
| 22 | Debug         | `Debug off`          | yes (not persisted) | on ↔ off              | Enable verbose serial logging (`[CIV freq=…]`, `[PTT…]`, etc). Off by default; init banners and errors always print. |

Items 16..21 are persisted to EEPROM on the EDITING→BROWSING transition.
Items 7, 8..15 and 22 are intentionally **not** persisted — sequencer
timings and PTT mapping are config-driven (`oi3.cfg` repopulates them on
reset), debug is per-session, and peer-browse position is live-only.

The PINNED-state WPM live edit uses a separate 5 s debounced flush, so
rapid up/down changes don't burn EEPROM cells.

---

## CI-V (Icom CAT)

The firmware talks to the rig over Icom CI-V on Serial2 (TTL level
shifted by the OI3 hardware). Baudrate and rig address come from
`oi3.cfg` (`SERBAUD2`, `CIV_ADRESS`).

**What the firmware does:**

- Polls the rig every 2 s (slow) for frequency (cmd 0x03), then chains
  a mode request (0x04). After any local SET (freq/mode change
  initiated via TrxNet or keyer), it switches to a 200 ms fast-poll
  window for 10 s so you see the change reflected immediately.
- Sniffs the bus passively too — transceive broadcasts (cmd 0x00) and
  our own outgoing SET echoes (cmd 0x05) both update the local cache.
- For IC-7610 (address 0x98), prepends a `0x07 0xD2 0x00` "MAIN band
  select" sub-command before every set, because dual-receive otherwise
  targets VFO B. Other models NAK this and aren't affected.
- A 10 s watchdog clears `CivValid` if no parsed frame arrives — at
  which point the display switches to lowercase mode letters and the
  MODE button starts working again.

**Why CivValid matters:**

`CivValid` is the single source-of-truth flag. It controls:

- LCD mode field uppercase (rig authoritative) vs lowercase (local
  fallback).
- Whether the MODE button rotates CivMode (only when `!CivValid`).
- Whether the PTT output mapping uses the per-mode slot
  (`PttOutByCivMode[CivMode]`) or falls back to `PTTmodeDefault`.

This lets you keep operating coherently when the rig is off, on
standby, or wired to a different cable.

### CW over CI-V (CIV_CW_VIA_CIV)

When `CIV_CW_VIA_CIV=1` and the keyer needs to send CW (paddle or
buffered text in CW/CWR mode), the firmware uses CI-V command 0x17 to
feed the character to the **radio's built-in keyer**. The local CW1/CW2
pins and the PTT sequencer are bypassed. With `CIV_CW_VIA_CIV=0`, the
keyer keys CW1/CW2 directly and PTT goes through the sequencer.

---

## PTT, sequencer and interlock

### Outputs

| Pin (MCU)        | DB25 pin | Connector / signal                    | Typical use            |
|------------------|----------|---------------------------------------|------------------------|
| PTT1 (D41)       | 8        | REMOTE miniDIN (DATA-IN audio OFF)    | FSK / RTTY             |
| PTT2 (D22)       | 20       | DATA miniDIN (MIC-IN audio OFF)       | DIGI / AFSK            |
| PTT3 (D25)       | 22       | MIC                                   | SSB / CW               |
| PTTPA (D31)      | —        | rear RCA + DB25 pin 7                 | External PA            |
| SEQUENCER (D40)  | —        | rear RCA                              | Shared sequencer line  |

### Mode → output mapping

The PTT pin used depends on the current CI-V mode. Each mode has its
own slot in `oi3.cfg`:

```
LSB/USB/AM/CW/FM/CWR  →  PTT3 (MIC)
RTTY/RTR              →  PTT1 (REM, audio muted on rig side)
unknown / CIV silent  →  PTTmodeDefault (default 3)
```

Why: a PA needs to see a clean PTT regardless of mode; a SSB rig needs
PTT on the MIC connector with mic audio enabled; for FSK/RTTY you want
PTT on the REM/DAT jack where the rig is set to ignore microphone
audio so a stray cough on the headset doesn't end up on the air. The
mapping is set in `oi3.cfg` once; menu item 15 shows the *effective*
PTT for the current mode and lets you change the slot on the fly (RAM
only — it goes back to cfg on reset).

### Sequencer timing

The sequencer is a 6-level non-blocking state machine. Menu items 8..14
expose its timings in the same order as the runtime switching sequence.
These edits are live only; on reset the values are loaded again from
`oi3.cfg`.

| LCD item | Cfg value         | Meaning |
|----------|-------------------|---------|
| `Inter`  | `InterlockEnable` | `1` = D2 is PA interlock, `0` = D2 is external PTT input |
| `SeqH`   | `SEQUENCERlead`   | Delay after `SEQUENCER` HIGH before `PTTPA` HIGH |
| `PAH`    | `PAlead`          | Delay after `PTTPA` HIGH before selected `PTT1/2/3` HIGH |
| `PTTH`   | `PTTlead`         | Loaded and editable, but currently not used by the sequencer |
| `PTTL`   | `PTTtail`         | Delay after TX request ends before selected `PTT1/2/3` LOW |
| `PAL`    | `PAtail`          | Delay after selected `PTT1/2/3` LOW before `PTTPA` LOW |
| `SeqL`   | `SEQUENCERtail`   | Delay after `PTTPA` LOW before `SEQUENCER` LOW |

Typical TX start sequence:

```text
TX request
   |
   v
SEQUENCER  LOW  ___|-------------------------------- HIGH
                    <----------- SeqH ------------>
PTTPA      LOW  __________________|---------------- HIGH
                                    <--- PAH ---->
PTT1/2/3   LOW  _______________________________|--- HIGH
```

Typical TX release sequence:

```text
TX request released
   |
   v
PTT1/2/3   HIGH ----------------|__________________ LOW
                         < PTTL >
PTTPA      HIGH -------------------------|__________ LOW
                                  < PAL >
SEQUENCER  HIGH --------------------------------|___ LOW
                                           < SeqL >
```

Full phase order:

```text
TX ON:
  SEQUENCER HIGH -> wait SeqH -> PTTPA HIGH -> wait PAH -> PTT1/2/3 HIGH

TX OFF:
  wait PTTL -> PTT1/2/3 LOW -> wait PAL -> PTTPA LOW -> wait SeqL -> SEQUENCER LOW
```

Why this matters: external relays/PAs need time to settle before RF
appears. `SeqH` raises the shared sequencer line first (drives external
T/R relays); `PAH` then enables the PA; only then is the rig's own PTT
asserted. On release, PTT drops first, the PA gets time to dump
residual envelope, then the sequencer line releases. Tune to your
relay/PA characteristics.

### Inputs

| Input     | Pin | Idle              | Active | Behaviour                                  |
|-----------|-----|-------------------|--------|--------------------------------------------|
| FootSW    | D19 | HIGH (pullup)     | LOW    | External foot-switch PTT request.          |
| PTT232    | D3  | LOW               | HIGH   | PTT from the USB/serial-port interface.    |
| INTERLOCK | D2  | (see below)       | edge   | Dual behaviour — see below.                |

### InterlockEnable=1 (safety mode)

D2 is treated as a safety interlock from the PA. On every edge, an
`abort` flag toggles. While active, the sequencer is forced to level 0
immediately (all PTT/PA/SEQ lines dropped). Use this when your PA
exposes a fault-out line that should slam the transmit chain shut.

### InterlockEnable=0 (PTT-in mode)

D2 becomes another PTT input (LOW = TX), but with a twist: the
sequencer **stops at level 4** — SEQUENCER + PTTPA are raised, but the
local PTT1/2/3 pin is left LOW. Use this when an upstream system (e.g.
a remote PTT distributor) already drives PTT into the rig itself; OI3
only sequences the PA around it.

---

## Keyer (K3NG-derived, CW + RTTY/FSK)

The keyer is a port of the K3NG iambic keyer with extensions for RTTY,
TrxNet remote sending, and integration with the OI3 PTT sequencer.

### Paddle behaviour

- Paddles are always live. Pressing a paddle produces sidetone (subject
  to item 19) and runs the ASCII decoder regardless of CI-V mode.
- Mode determines whether the element also drives a TX path:
  - **CW / CWR (0x03 / 0x07):** paddle keys CW1/CW2 via the sequencer.
    If `CIV_CW_VIA_CIV=1`, decoded characters are also relayed to the
    radio's internal keyer for transmission.
  - **RTTY / RTR (0x04 / 0x08):** paddle elements still produce
    sidetone in your ear, but the decoded **characters** are enqueued
    to the RTTY FSK state machine instead of being keyed as CW.
  - **LSB / USB / AM / FM, or `!CivValid`:** sidetone + LCD echo only,
    no TX path activated.
- Touching a paddle flushes any pending `/s-cw` send buffer received
  from a TrxNet peer — paddle has priority.

### PTT timing

The first element of a CW transmission **defers until the sequencer
reaches `SequencerLevel == PttOut`** (i.e. SEQ + PA are already raised
and the rig PTT is asserted). After the last element, the keyer holds
PTT until 7 dit-lengths of silence have passed (CW) or until the RTTY
state machine finishes its stop-bit + idle window (RTTY). The K3NG
PTTlead/PTTtail constants were removed — the sequencer alone owns PTT
timing, so there is exactly one place that decides when the rig
transmits.

### CW decoder (paddle → LCD row 1)

Each completed element is accumulated as a decimal number (1=dit,
2=dah). On a letterspace timeout (3-dit silence), the firmware looks
the number up in a table and writes the resulting ASCII char to LCD
row 1. On a wordspace (7-dit silence) it writes a space. Unknown
sequences print `*` (K3NG convention). The table includes prosigns:
`=` (BT), `+` (AR), `(` (KN), `&` (SK), `*` (BK).

### TrxNet remote CW

The keyer subscribes to TrxNet topic `/s-cw`. Any peer can push CW text
to the OI3 and it will be:

- Played as Morse through the local CW1/CW2 + sequencer **if** mode is
  CW/CWR.
- Encoded as Baudot and clocked out on the FSK pin **if** mode is
  RTY/RTR.
- Dropped (sidetone only) in other modes.

A single-byte payload of `0x03` (ETX) or `0xFF` aborts the in-flight
send and flushes the buffer — useful for remote "stop sending" buttons.

### RTTY / FSK output

FSK output is on D23. **Idle = HIGH-Z** (the pin is set to `INPUT`);
the keyer drives it as an `OUTPUT` only during an active TX session.
This is deliberate — the OI3 FSK keying circuit interprets *any*
active MCU drive (HIGH or LOW) as keyed, so floating the pin between
sessions is the only correct idle. During TX:

- Start bit: SPACE
- 5 data bits: LSB first (Baudot 5-bit code)
- Stop: MARK, 1.5 bit-times
- Pin released back to HIGH-Z after 7-dit silence (handled by
  `ptt_tail_service`).

`KeyerFskReverseNow` (item 21) inverts MARK/SPACE drive polarity for
rigs/cables that expect the opposite sense. It does **not** affect the
idle release behaviour.

Baud rate (item 20) cycles through 45, 50, 75, 100 — `45` is clocked at
the standard 45.45 Bd (22 ms per bit), the others at exactly the named
rate.

### Persistence

Keyer settings live in EEPROM at addresses 1..7:

| Addr | Setting                |
|------|------------------------|
| 1    | WPM                    |
| 2    | Iambic mode (0=A, 1=B) |
| 3    | Paddle reverse         |
| 4-5  | Sidetone Hz (16-bit)   |
| 6    | RTTY baud              |
| 7    | FSK polarity           |

EEPROM > cfg on boot — `oi3.cfg` is only consulted if the EEPROM cell
still holds `0xFF` (factory-fresh). PINNED-state WPM edits use a 5 s
debounced flush; menu EDITING-state changes are saved immediately on
SET.

---

## TrxNet (peer-to-peer LAN)

TrxNet is a small UDP-based pub/sub protocol that lets multiple
OI3-class devices in a hamshack share state without a central server.
Each device announces itself as `OI3.NN` (where NN is `NET_ID` in hex)
and discovers peers on the LAN.

**Topics published by OI3:**

| Topic   | Payload                  | When sent                |
|---------|--------------------------|--------------------------|
| `/hz`   | `uint32_t` Hz            | On every CivFreq change  |
| `/mode` | `uint8_t` CIV mode byte  | On every CivMode change  |

**Topics subscribed by OI3 (peers can drive us):**

| Topic     | Payload          | Action on receive |
|-----------|------------------|-------------------|
| `/s-hz`   | `uint32_t` Hz    | Set rig frequency via CI-V. |
| `/s-mode` | `uint8_t` mode   | Set rig mode via CI-V. |
| `/s-cw`   | ASCII bytes      | Enqueue for CW or RTTY send (see Keyer). |
| `/s-cw`   | `0x03` or `0xFF` | Abort current CW/RTTY send and flush buffer. |

When a new peer joins, OI3 sends it a CON snapshot of its current
`/hz` and `/mode` — but only after CI-V is known (i.e. either the rig
has answered at least once, or the local fallback has been set). The
greeting queue is drained one peer per loop iteration to avoid bursts.

Menu item 7 shows the live peer count. Long-press SET in BROWSING on
item 7 enters a special "browse" mode where DWN/UP walk the peer list
showing `name .lastIP` for each — useful for confirming exactly which
remote stations are talking to you.

**Disable:** set `NET_ID=00` in `oi3.cfg`. Item 7 will then display
`Peers off`. TrxNet also requires Ethernet — it stays in waiting state
until link is up and re-attempts `begin()` on link-up, so plugging the
cable after boot works without a reset.

---

## Ethernet

Optional. The W5100/W5500 module is detected at boot via the `ETHINST`
pin pulled low when a module is fitted. If absent the firmware logs
`ETH module n/a` and skips all networking.

- `EthernetEnable=0` in cfg disables Ethernet even when the module is
  present (useful for benchwork without a LAN).
- `EthernetDHCP=1` uses DHCP; `=0` uses the static defaults
  `192.168.1.220 / .1 / 255.255.255.0 / 8.8.8.8`.
- The MAC address is `DE:AD:BE:EF:FE:(0xFF - NET_ID)` — derived from
  NET_ID so multiple OI3s on the same LAN don't collide.
- Link is monitored every 1 s; on link-up the firmware re-inits the
  stack and TrxNet, so unplug/replug cycles recover automatically.

The boot LCD shows the assigned IP for ~1 s. After that, items 2 and 3
of the menu hold the same information — pin item 2 (or 3) and you have
a permanent reminder of where the unit is reachable.

---

## SD card and `oi3.cfg`

A microSD card is optional. At boot the firmware looks for
`/oi3.cfg` and reads `[KEY=VALUE]` pairs into runtime defaults. If the
card is missing, missing the file, or failing to init, the firmware
logs the reason on row 1 for 1 s and proceeds with compiled defaults.

Configuration keys (from `readSDSettings()`):

| Key                   | Default     | Type / range            | Notes |
|-----------------------|-------------|-------------------------|-------|
| `NET_ID`              | `02`        | hex byte, `00` disables | TrxNet identity. MAC = `DE:AD:BE:EF:FE:(0xFF - NET_ID)`. |
| `YOUR_CALL`           | `OK1HRA`    | string                  | Shown on menu item 0. |
| `InterlockEnable`     | `1`         | 0 / 1                   | 1 = safety interlock from PA; 0 = D2 is PTT-in. |
| `SEQUENCERlead`       | `0` ms      | int ms                  | Delay from SEQ HIGH to PA HIGH. |
| `SEQUENCERtail`       | `0` ms      | int ms                  | Delay from PA LOW to SEQ LOW. |
| `PAlead`              | `10` ms     | int ms                  | Delay from PA HIGH to PTT HIGH. |
| `PAtail`              | `0` ms      | int ms                  | Delay from PTT LOW to PA LOW. |
| `PTTlead`             | `10` ms     | int ms                  | (Reserved — sequencer owns PTT timing now.) |
| `PTTtail`             | `210` ms    | int ms                  | Hold PTT this long after last element. |
| `PTTmodeLSB`          | `3`         | 1 / 2 / 3               | CIV 0x00 → PTT slot. |
| `PTTmodeUSB`          | `3`         | 1 / 2 / 3               | CIV 0x01 → PTT slot. |
| `PTTmodeAM`           | `3`         | 1 / 2 / 3               | CIV 0x02 → PTT slot. |
| `PTTmodeCW`           | `3`         | 1 / 2 / 3               | CIV 0x03 → PTT slot. |
| `PTTmodeRTTY`         | `1`         | 1 / 2 / 3               | CIV 0x04 → PTT slot. |
| `PTTmodeFM`           | `3`         | 1 / 2 / 3               | CIV 0x05 → PTT slot. |
| `PTTmodeCWR`          | `3`         | 1 / 2 / 3               | CIV 0x07 → PTT slot. |
| `PTTmodeRTR`          | `1`         | 1 / 2 / 3               | CIV 0x08 → PTT slot. |
| `PTTmodeDefault`      | `3`         | 1 / 2 / 3               | Fallback when CIV silent / mode unknown. |
| `BAND_DECODER_IN`     | `1`         | 0 / 1                   | 0 = disable CI-V loop; 1 = ICOM CI-V. |
| `SERBAUD2`            | `9600`      | bps                     | Serial2 baudrate (rig CAT). |
| `CIV_ADRESS`          | `98` (hex)  | hex byte                | `56`=IC-746, `98`=IC-7610, etc. |
| `CIV_CW_VIA_CIV`      | `1`         | 0 / 1                   | 1 = send CW via radio internal keyer (cmd 0x17); 0 = key CW1/CW2 + local PTT. |
| `EthernetEnable`      | `1`         | 0 / 1                   | Master Ethernet switch. |
| `EthernetDHCP`        | `1`         | 0 / 1                   | 1 = DHCP, 0 = static. |
| `KeyerDefaultWpm`     | `28`        | 5..50                   | Default WPM. EEPROM overrides if not `0xFF`. |
| `KeyerMode`           | `B`         | `A` / `B`               | Iambic mode. |
| `KeyerPaddleReverse`  | `0`         | 0 / 1                   | 1 = swap dit/dah paddles. |
| `KeyerSidetoneHz`     | `600`       | 0 or 300..1200          | 0 = sidetone off. |
| `KeyerRttyBaud`       | `45`        | 45 / 50 / 75 / 100      | 45 is clocked at 45.45 Bd. |
| `KeyerFskReverse`     | `0`         | 0 / 1                   | 1 = invert MARK/SPACE polarity. |

Cfg syntax — one key per line, both sides bracketed, free comment after
the closing bracket:

```
[KeyerSidetoneHz=600] free comment ignored by parser
```

The parser tolerates extra whitespace and silently ignores unknown
keys.

---

## EEPROM map

| Addr | Owner | Purpose                          |
|------|-------|----------------------------------|
| 0    | LCD   | Last menu index (PINNED snapshot) |
| 1    | KEYER | WPM                              |
| 2    | KEYER | Iambic mode (0=A, 1=B)           |
| 3    | KEYER | Paddle reverse (0/1)             |
| 4    | KEYER | Sidetone Hz, low byte            |
| 5    | KEYER | Sidetone Hz, high byte           |
| 6    | KEYER | RTTY baud                        |
| 7    | KEYER | FSK polarity (0/1)               |

Factory-fresh cells (`0xFF`) cause the boot loader to use the cfg
defaults instead. To "reset to cfg" without erasing the chip, write
`0xFF` into the relevant cell from any sketch.

---

## Build / flash

The firmware is an Arduino sketch targeting **ATmega2560** (Arduino
Mega 2560 board profile). Required libraries:

- `LiquidCrystal` (stock Arduino)
- `SD` (stock Arduino)
- `Ethernet` (stock Arduino — works with both W5100 and W5500)
- `EEPROM` (stock Arduino)
- `TrxNet` (in `/home/dan/Arduino/libraries/TrxNet`)

Open `oi3.ino` in the Arduino IDE, select *Arduino Mega 2560*, the
correct serial port, and upload. Pre-built `.hex` files
(`oi3.ino.mega.hex`, `oi3.ino.with_bootloader.mega.hex`) are also
shipped in the repository and can be flashed with `avrdude`:

```sh
avrdude -p atmega2560 -c wiring -P /dev/ttyUSB0 -b 115200 -D \
        -U flash:w:oi3.ino.mega.hex:i
```

---

## Hardware reference

For pinouts, schematics, front/rear panel layout, DB25 wiring, jumpers,
and connector diagrams for specific transceivers (FT-1000MP, FT-2000,
IC-706MkIIG, IC-7610, KX3, TS-480, TS-590), see:

**→ [doc/open-interface-iii.md](doc/open-interface-iii.md)**
