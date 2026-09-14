#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")" && pwd)"
bash "$ROOT/web/build/build.sh"
bash "$ROOT/server/build/build.sh"
mkdir -p "$ROOT/deploy/config" "$ROOT/deploy/log"
if [[ ! -f "$ROOT/config.ini" ]]; then
    echo "missing config.ini (copy from config.example.ini)" >&2
    exit 1
fi
cp "$ROOT/config.ini" "$ROOT/deploy/config/config.ini"
cp "$ROOT/server/src/deal_prompt.txt" "$ROOT/deploy/config/deal_prompt.txt"
echo "deploy ready"
