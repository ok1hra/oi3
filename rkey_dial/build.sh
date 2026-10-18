#!/usr/bin/env bash
#
# Build the M5Dial remote-key firmware into out/.
# Fails if rkey_dial/rkey_proto.h is not identical to ../rkey_proto.h (the
# protocol header is shared verbatim with the OI3 firmware).

set -euo pipefail
cd "$(dirname "$0")"
export PATH="$HOME/bin:$PATH"

FQBN="m5stack:esp32:m5stack_dial"

if ! cmp -s rkey_proto.h ../rkey_proto.h; then
  echo "error: rkey_dial/rkey_proto.h differs from ../rkey_proto.h" >&2
  echo "       copy it: cp ../rkey_proto.h rkey_dial/" >&2
  exit 1
fi

command -v arduino-cli >/dev/null 2>&1 || { echo "error: arduino-cli not found in PATH" >&2; exit 1; }

rm -rf out
arduino-cli compile -b "$FQBN" --warnings default --output-dir out .
ls -l out/rkey_dial.ino.bin
