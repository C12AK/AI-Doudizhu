#!/bin/bash

cd "$(dirname "$0")/.." || exit
docker compose down -v
docker rmi doudizhu:latest || true
