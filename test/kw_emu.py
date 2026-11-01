#!/usr/bin/env python3
"""TS-480 CAT emulator for bench-testing the OI3 Kenwood backend (kenwood.ino).

Wire a USB-serial adapter to OI3 Serial2 (RX2=17 / TX2=16, TTL) and run with
OI3 at BAND_DECODER_IN=2, Debug on (LCD item 22) to watch [CAT ...] lines.

  ./kw_emu.py /dev/ttyUSB0                 # silent rig: OI3 must poll IF; every ~2 s
  ./kw_emu.py /dev/ttyUSB0 --logger 0.5    # fake logger traffic: OI3 must stay SILENT

Keys (stdin, then Enter):
  f <hz>    set VFO A frequency       m <1-9>  set mode (MD digit)
  b         toggle VFO A/B            s        toggle split
  q         quit
Every command from OI3 is printed with a timestamp; in --logger mode any
OI3 request ("IF;") is flagged as a violation of the sniff-first rule.
"""
import argparse
import select
import sys
import time

import serial


class Rig:
    def __init__(self):
        self.fa = 14025000
        self.fb = 7030000
        self.mode = 3
        self.vfo = 0
        self.split = 0
        self.tx = 0

    def if_frame(self):
        f = self.fb if (self.vfo == 1) != (self.tx and self.split) else self.fa
        return ("IF%011d" % f + " " * 5 + "+0000" + "00" + "0" + "00"
                + "%d%d%d" % (self.tx, self.mode, self.vfo) + "0"
                + "%d" % self.split + "0" + "00" + " ;")

    def handle(self, cmd):
        """cmd without ';'. Returns the answer string or ''."""
        c = cmd[:2].upper()
        p = cmd[2:]
        if c == "IF" and not p:
            return self.if_frame()
        if c in ("FA", "FB"):
            if not p:
                return "%s%011d;" % (c, self.fa if c == "FA" else self.fb)
            if c == "FA":
                self.fa = int(p)
            else:
                self.fb = int(p)
            return ""
        if c == "MD":
            if not p:
                return "MD%d;" % self.mode
            self.mode = int(p)
            return ""
        if c == "FR":
            if not p:
                return "FR%d;" % self.vfo
            self.vfo = int(p)
            return ""
        if c == "TX":
            self.tx = 1
            return ""
        if c == "RX":
            self.tx = 0
            return ""
        return "?;"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("port")
    ap.add_argument("--baud", type=int, default=9600)
    ap.add_argument("--logger", type=float, default=0.0,
                    help="emit an unsolicited IF frame every N seconds (fake logger poll)")
    a = ap.parse_args()

    ser = serial.Serial(a.port, a.baud,
                        stopbits=serial.STOPBITS_TWO if a.baud == 4800 else serial.STOPBITS_ONE,
                        timeout=0)
    rig = Rig()
    buf = ""
    t0 = time.time()
    next_logger = t0
    violations = 0

    def log(msg):
        print("%8.3f  %s" % (time.time() - t0, msg), flush=True)

    while True:
        data = ser.read(256).decode("ascii", "replace")
        for ch in data:
            if ch == ";":
                log("OI3 -> %s;" % buf)
                if a.logger and buf.upper() == "IF":
                    violations += 1
                    log("!!! OI3 requested while logger traffic is present (%d)" % violations)
                ans = rig.handle(buf)
                if ans:
                    ser.write(ans.encode())
                buf = ""
            elif ch not in "\r\n":
                buf += ch

        if a.logger and time.time() >= next_logger:
            ser.write(rig.if_frame().encode())
            next_logger = time.time() + a.logger

        r, _, _ = select.select([sys.stdin], [], [], 0.01)
        if r:
            w = sys.stdin.readline().split()
            if not w:
                continue
            if w[0] == "q":
                break
            if w[0] == "f" and len(w) > 1:
                rig.fa = int(w[1])
            elif w[0] == "m" and len(w) > 1:
                rig.mode = int(w[1])
            elif w[0] == "b":
                rig.vfo ^= 1
            elif w[0] == "s":
                rig.split ^= 1
            log("rig: FA=%d FB=%d MD=%d VFO=%d split=%d tx=%d"
                % (rig.fa, rig.fb, rig.mode, rig.vfo, rig.split, rig.tx))

    print("logger-mode violations: %d" % violations)


if __name__ == "__main__":
    main()
