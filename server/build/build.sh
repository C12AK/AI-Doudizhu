#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
mkdir -p lib deploy/bin deploy/log
cmake -S src -B cmake-build
cmake --build cmake-build --target ddz-server ddz-cards-test -j"$(nproc)"
