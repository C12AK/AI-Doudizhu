#!/bin/bash

DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$DIR/.." || exit
. "$DIR/env.sh" || exit

docker compose build
docker save doudizhu:latest | gzip > doudizhu-image.tar.gz
ssh -t -p "$SSH_PORT" "$SSH_TARGET" "sudo mkdir -p '$REMOTE_DIR' && sudo chown '${SSH_TARGET%%@*}:${SSH_TARGET%%@*}' '$REMOTE_DIR'"
scp -P "$SSH_PORT" doudizhu-image.tar.gz docker-compose.yml init.sql "$SSH_TARGET:$REMOTE_DIR/"
