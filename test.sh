#!/usr/bin/env bash
# The M0 checks in the host simulator: a key makes the voice play, a release stops it.
set -euo pipefail
cd "$(dirname "$0")"

python3 base/tools/build.py "$PWD/app" --host
"$PWD/app/build/host/plant1m10_host" "$PWD/app/tests/basic.txt" "$PWD/app/build/host"
