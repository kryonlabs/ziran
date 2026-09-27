#!/bin/sh
set -eu
repo=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
python3 "$repo/tests/packages.py" "${1:?pass ziran launcher}"
