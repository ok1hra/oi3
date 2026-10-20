# Remote CW key server (Paddle2IP → OI3)

The operator holds the paddle, keyer and sidetone locally in a
[Paddle2IP](https://github.com/ok1hra/paddle2ip) client (M5Stack Dial, "CTRL").
The OI3 at the rig ("RIG") is the server: it replays the key edges on CW1/CW2
through the existing PTT sequencer. Only key **edges with sender timestamps**
travel over the network, so mark/space lengths are reproduced exactly; network
jitter is absorbed by a playout delay.

```
paddle ─► Paddle2IP: iambic keyer ─► sidetone (0 ms)
                      │ edges (t_us, up/down), 8× redundant, SipHash tag
                      ▼ UDP 5690
          OI3: jitter buffer ─► Timer1 ISR ─► CW1/CW2   (+ PTT sequencer)
                      │ status: nonce, RTT echo, CI-V freq/mode, counters
                      ▼
          Paddle2IP display: WPM, link, delay, rig freq/mode
```

The client firmware, its user guide and a guide for implementing other servers
live in the Paddle2IP repository: https://github.com/ok1hra/paddle2ip

## Files

| File | Purpose |
|---|---|
| `rkey_proto.h` | Packet formats + SipHash-2-4. An identical copy is in the Paddle2IP repository; keep both the same. |
| `rkey.ino` | Server module (second sketch tab, compiled together with `oi3.ino`). |
| `oi3.ino` | Hooks: cfg parser, PTT OR, `ptt_abort_all()`, keyer arbitration, loop/setup, pump loops. |
| `test/rkey_sim/` | Host simulation of the server (loss, jitter, clock drift, blackout). |
| `test/rkey_test.py` | PC tool: key text into the OI3, security checks, fake OI3 for Paddle2IP. Identical copy in Paddle2IP `tools/`. |

## Configuration (`oi3.cfg`)

| Key | Default | Meaning |
|---|---|---|
| `RkeyEnable` | 0 | 1 = server on |
| `RkeyPort` | 5690 | UDP port |
| `RkeyKey` | — | 32 hex chars, shared with Paddle2IP. All-zero = not configured = disabled |
| `RkeyDelayMs` | 120 | playout delay; LAN 30, good internet 80–150, LTE 200+. Paddle2IP may request its own |
| `RkeyLinkTimeoutMs` | 300 | key-down with no packet this long → key up, abort |
| `RkeyMaxDownMs` | 3000 | longest single key-down accepted |
| `RkeyRequireCiv` | 1 | 1 = key only when CI-V reports CW/CWR; 0 = also the local MODE fallback |

Keying happens only in CW/CWR, without interlock and without a low-power tune in
progress. While a remote stream is active the local paddle is sidetone-only and
`/s-cw` text waits.

Paddle2IP can generate a key (web page → *Generate new key*) and shows it
as a ready `[RkeyKey=...]` line for `oi3.cfg`.

## LCD

Menu item 25 (`IPkey`) shows the link on row 0 (`IPkey CONN` = a client session
is sending, `IPkey DISC`, `IPkey off` = disabled / no key) and, while selected,
the trace of the elements actually played on CW1/CW2 on row 1, newest on the
right: `.` / `-` by mark length (< 2 dits at the sender's WPM = dot), space =
letter gap (≥ 2 dits), ` / ` = word gap (≥ 5 dits), `*` = mark cut by an
abort. The Timer1 ISR logs each played edge into an 8-entry ring;
`rkey_loop()` builds the trace, so the ISR stays short.

In the PINNED state the separator before the mode field shows `:` instead of
`|` while a client is connected (`IPkey CONN`) and the rig is in CW/CW-R.

## Behaviour

* **Timing**: `playAt = t_sender + min_latency + D`. `min_latency` is the
  minimum of `arrival − t_sender` over a sliding 1–2 s window (jitter-free).
  Edges are emitted by a Timer1 compare ISR (4 µs resolution).
* **Late key-down** shifts the rest of the over (the gap absorbs it, mark length
  is kept) and increases an adaptive extra delay that decays by ¼ at each new over.
* **Clock drift** (Paddle2IP crystal vs. OI3 crystal, typically < 100 ppm;
  handled up to 0.5 %) is corrected by at most 1.5 ms per mark start, i.e.
  always inside a gap.
* **Sequencer**: the first edge waits for `SequencerLevel == PttOut` + `PTTlead`.
  D is raised to cover `SEQUENCERlead + PAlead + PTTlead` when the sequencer is down.
  PTT is held for 7 dits (sender WPM) after the last key-up, then the sequencer
  applies `PTTtail`.
* **Safety**: link timeout, max key-down, edge gap beyond redundancy, stale
  packets, interlock, mode change → key up immediately and **mute**. Link
  timeout and max key-down are also enforced in the Timer1 ISR (checked every
  50 ms while the key is down, and a mark never starts on a link that is
  already timed out), so a blocked `loop()` cannot leave the transmitter keyed.
* **Blocking work**: `Ethernet.maintain()` (DHCP renew, can block up to the
  60 s timeout when the DHCP server does not answer) is skipped while remote
  keying is active or was within the last 5 s; the renew runs in the next
  pause. A static IP (`EthernetDHCP=0`) avoids it completely and is
  recommended for a remote rig. The stream
  resumes only at the next word boundary (sender key-up older than 5 dits) or
  when the sender is idle, so a partial character is never transmitted.
* **Security**: SipHash-2-4 tag with the shared key on every packet. A new
  session is accepted only with a fresh nonce issued by the OI3 (rotates every
  5 s, carried in status packets), and `seq` must increase within a session.
  Forged or replayed packets cannot key the transmitter. Note: `readSDSettings()`
  echoes cfg values to the serial console, including `RkeyKey`.

## Protocol

Little-endian, see `rkey_proto.h`. The full description (send rules, field
meanings, server requirements) is in the Paddle2IP README, section
*Implementing a server*.

* **KEY** (Paddle2IP → OI3), 24 B header + 4 B × `nev` + 8 B tag.
  Each edge is a 32-bit sender µs timestamp with bit 0 = new state (1 = down).
  The client sends a packet immediately on every edge, a heartbeat every 20 ms
  while keying (any edge in the last 2 s), and every 100 ms when idle. Each
  packet carries the edges of the last 500 ms, at most 8 (index `evIdx0`), and
  always the newest edge while the key is down. `nev = 0` means the sender is idle.
* **STATUS** (OI3 → Paddle2IP), 36 B + 8 B tag, at most every 100 ms in reply
  to KEY packets: nonce, RTT echo (`RTT = now − echoTSend − echoAgeUs`), CI-V
  frequency and mode, sequencer level, delay in use, late/lost counters, flags.

## Resources

* Timer1 (free: `millis()` uses Timer0, `tone()` uses Timer2; no library in
  the build touches Timer1), one W5x00 UDP socket, EEPROM 4092–4095 (boot
  counter for nonce entropy).
* Measured with avr-gcc 7.3, ATmega2560: flash 74.7 kB (+11.6 kB), static RAM
  6222 B (+556 B, 76 %). With `./build.sh` (arduino-cli, arduino:avr 1.8.6):
  flash 66.9 kB, static RAM 5855 B (71 %). SipHash of one KEY packet
  ≈ 18.5 k cycles ≈ 1.2 ms (simavr), about 7 % CPU at 50 packets/s.

## Simulation

```
cd test/rkey_sim
g++ -O1 -std=gnu++17 -w -I. -I../.. -include mock.h sim.cpp -o sim
./sim <ppm> <latency_us> <jitter_us> <loss> <wpm> <seed>
BO=<blackout_start_us> BL=<blackout_len_us> ./sim 0 30000 10000 0 30 7
FZ=<freeze_start_us> FL=<freeze_len_us> ./sim 0 30000 10000 0 30 7   # loop() blocked, only the ISR runs
```

Results (text "CQ CQ TEST 5NN K CQ TEST R", 102 edges):

| Scenario | Mark length error | Gap error | Latency |
|---|---|---|---|
| LAN 1 ms, no loss | 29 µs | 83 µs | 121 ms |
| +0.5 % drift, 30 ms + 0–40 ms jitter, 10 % loss, 30 WPM | 25 µs | 1.5 ms | 150–160 ms |
| −0.5 % drift, 25 % loss | 25 µs | 1.5 ms | 142–147 ms |
| 0–200 ms jitter (> D) | 25 µs | 7.5 ms, 2 late edges | D adapts 120 → 177 ms |
| 2 s blackout mid-mark | mark cut 300 ms after the last packet | — | resumes at next word |
| `loop()` blocked 5 s mid-mark | key released by the ISR ≤ 300 ms after the last packet (was 4.9 s without the ISR watchdog) | — | resumes at next word |

(25 µs is the simulation step, not the firmware resolution.)

## PC test tool (`test/rkey_test.py`)

Python 3, no dependencies. The key comes from `--key HEX`, `RKEY_KEY` or `--cfg oi3.cfg`.

```
./rkey_test.py send 192.168.1.50 --cfg ../oi3.cfg --wpm 25 --text "CQ TEST"
./rkey_test.py send 192.168.1.50 --cfg ../oi3.cfg --loss 0.2 --jitter 40 --repeat 3
./rkey_test.py attack 192.168.1.50 --cfg ../oi3.cfg
./rkey_test.py rig --key <hex>          # fake OI3 on UDP 5690, point Paddle2IP at this PC
./rkey_test.py selftest                 # SipHash-2-4 reference vector
```

* `send` acts as Paddle2IP: syncs the nonce, keys the text with ideal edge
  times, prints the STATUS changes, the RTT and the OI3 counters. `--loss` and
  `--jitter` simulate a bad network on the sending side.
* `attack` sends packets with a bad tag, a modified body, a wrong key, replayed
  packets, and new sessions with a stale or no nonce. It then checks that the
  OI3 `rxPkts` counter rose only by the genuine heartbeat.
* `rig` verifies the client's packets, issues nonces, answers STATUS and prints
  every mark/space length from the sender clock with the packet jitter and
  the decoded text.

Not tested on hardware yet.
