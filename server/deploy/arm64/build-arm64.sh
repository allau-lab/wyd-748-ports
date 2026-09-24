#!/usr/bin/env bash
# Compila WYD-Go headless para linux/arm64 a partir de ../codigo-fonte
set -euo pipefail
ROOT="$(cd "$(dirname "$0")" && pwd)"
SRC="$(cd "$ROOT/../codigo-fonte" && pwd)"
mkdir -p "$ROOT/bin"

cd "$SRC"
echo "[build] GOOS=linux GOARCH=arm64 CGO_ENABLED=0 (headless, sem -tags gui)"
CGO_ENABLED=0 GOOS=linux GOARCH=arm64 \
  go build -trimpath -ldflags='-s -w' \
  -o "$ROOT/bin/wydserver" ./cmd/server

CGO_ENABLED=0 GOOS=linux GOARCH=arm64 \
  go build -trimpath -ldflags='-s -w' \
  -o "$ROOT/bin/account-create" ./cmd/account-create

file "$ROOT/bin/wydserver" "$ROOT/bin/account-create"
echo "[build] OK → $ROOT/bin/"
