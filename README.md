# OI3 — ICOM Interface

## PTT outputs

| Pin (MCU)       | DB25 pin | ICOM connector                       | Typical use           |
|-----------------|----------|--------------------------------------|-----------------------|
| PTT1 (D41)      | 8        | REMOTE miniDIN (DATA audio in OFF)   | FSK / RTTY            |
| PTT2 (D22)      | 20       | DATA miniDIN (MIC audio in OFF)      | DIGI / AFSK           |
| PTT3 (D25)      | 22       | MIC                                  | SSB / CW              |
| PTTPA (D31)     | —        | rear RCA + DB25 pin 7                | external PA           |
| SEQUENCER (D40) | —        | rear RCA                             | shared sequencer line |

CI-V mode → PTT output assignment is configured per CI-V mode byte in
`oi3.cfg` (`PTTmodeLSB/USB/AM/CW/RTTY/FM/CWR/RTR` + `PTTmodeDefault`).
The currently selected PTT output (1/2/3) is shown on the LCD menu
(item index 8) and prefixed with `TX` while the sequencer is active.
