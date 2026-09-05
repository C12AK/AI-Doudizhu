#!/bin/bash

DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$DIR/.." || exit
. "$DIR/env.sh" || exit

ssh -t -p "$SSH_PORT" "$SSH_TARGET" "sudo bash -c \"
set -euo pipefail
cd '$REMOTE_DIR'
gzip -dc doudizhu-image.tar.gz | docker load
docker compose up -d --force-recreate --no-build
docker compose ps
\""
