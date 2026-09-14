#!/usr/bin/env bash
set -euo pipefail
WEB="$(cd "$(dirname "$0")/.." && pwd)"
REPO="$(cd "$WEB/.." && pwd)"
mkdir -p "$REPO/deploy/www"
cp -a "$WEB/src/." "$REPO/deploy/www/"
echo "web copied to deploy/www"
