#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
mkdir -p "$ROOT/deploy/www"
cp -a "$ROOT/src/." "$ROOT/deploy/www/"
cp "$ROOT/deploy/config/client.json" "$ROOT/deploy/www/config.json"
echo "web copied to deploy/www"
