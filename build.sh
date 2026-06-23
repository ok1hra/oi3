#!/usr/bin/env bash
#
# Build oi3.ino for both PCB revisions into out/<rev>/.
#
# Produces, per revision, files prefixed with the PCB revision so an image
# can never be mistaken for the wrong board (even outside its directory):
#   out/<rev>/PCB_<rev>_oi3.ino.hex                  - flash image (no bootloader)
#   out/<rev>/PCB_<rev>_oi3.ino.with_bootloader.hex  - flash image incl. bootloader
#
# The PCB revision is selected with a -D build flag; the sketch defaults to
# PCB_REV_3_1415 if none is passed (so it still opens in the Arduino IDE).
#
# Verification (no hardware needed): both builds must compile, both .hex must
# exist and be non-empty, and the 3_141 image must be SMALLER than 3_1415 -
# that proves the Ethernet/TrxNet feature-gate actually compiled out (a build
# that forgot the -D flag would produce two identical sizes).

set -euo pipefail

cd "$(dirname "$0")"

# arduino-cli may live in ~/bin (see install in README) - make it findable.
export PATH="$HOME/bin:$PATH"

FQBN="arduino:avr:mega"          # ATmega2560
REVS=(3_141 3_1415)

command -v arduino-cli >/dev/null 2>&1 || {
  echo "error: arduino-cli not found in PATH" >&2
  exit 1
}

rm -rf out
for rev in "${REVS[@]}"; do
  echo "=== building PCB_REV_${rev} ==="
  arduino-cli compile \
    -b "$FQBN" \
    --build-property "build.extra_flags=-DPCB_REV_${rev}" \
    --output-dir "out/${rev}" \
    .

  # Prefix the images with the PCB revision so they cannot be swapped.
  for base in oi3.ino.hex oi3.ino.with_bootloader.hex; do
    mv "out/${rev}/${base}" "out/${rev}/PCB_${rev}_${base}"
  done
done

echo "=== verifying outputs ==="
fail=0
for rev in "${REVS[@]}"; do
  for f in "out/${rev}/PCB_${rev}_oi3.ino.hex" \
           "out/${rev}/PCB_${rev}_oi3.ino.with_bootloader.hex"; do
    if [[ ! -s "$f" ]]; then
      echo "error: missing or empty $f" >&2
      fail=1
    fi
  done
done
[[ $fail -eq 0 ]] || exit 1

size_141=$(stat -c%s out/3_141/PCB_3_141_oi3.ino.hex)
size_1415=$(stat -c%s out/3_1415/PCB_3_1415_oi3.ino.hex)
echo "3_141  PCB_3_141_oi3.ino.hex:  ${size_141} bytes"
echo "3_1415 PCB_3_1415_oi3.ino.hex: ${size_1415} bytes"
if [[ "$size_141" -ge "$size_1415" ]]; then
  echo "error: 3_141 image is not smaller than 3_1415 - feature-gate may not" >&2
  echo "       have applied (was the -D flag passed?)." >&2
  exit 1
fi

echo "OK: both revisions built; 3_141 < 3_1415 as expected."
