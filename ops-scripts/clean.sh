#!/bin/bash

DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$DIR/.." || exit
. "$DIR/env.sh" || exit

ssh -t -p "$SSH_PORT" "$SSH_TARGET" "sudo bash -c \"
cd '$REMOTE_DIR'
docker compose down -v
docker rmi doudizhu:latest || true
rm -f doudizhu-image.tar.gz docker-compose.yml init.sql
\""
