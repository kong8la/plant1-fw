#!/usr/bin/env bash
# Build the PLANT-1 M0 firmware: fm1-base (pinned) + the app in app/.
# Runs in the repository root, on Linux (the JieLi toolchain is Linux x86-64 only).
set -euo pipefail
cd "$(dirname "$0")"

PIN=c8e05997ce970875038156d2bbd69e712eac1543   # fm1-base, the commit this app was written against

if [ ! -d base/tools ]; then
    git clone --recurse-submodules https://github.com/charlesvestal/fm1-base.git base
    git -C base checkout --quiet "$PIN"
    git -C base submodule update --init --recursive
fi

python3 base/tools/build.py "$PWD/app" "$@"
