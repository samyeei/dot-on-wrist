#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
export ANNA_CHROME_CONTROL_DIRECTORY="$HOME/Library/Application Support/ANNA Chrome Extension"
exec "${ANNA_PYTHON:-$root/.venv/bin/python}" "$root/mac/run.py" \
    --private "${ANNA_PRIVATE_DIR:-$root/private}" --bind "${ANNA_BIND:-0.0.0.0}" --apple-watch-port 8767
