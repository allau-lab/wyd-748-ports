#!/usr/bin/env bash
# Servidor WYD-Go ARM64 (headless). Sem janela SDL — use --cli ou flags do binário.
#
#   ./iniciar.sh              # servidor + console admin no terminal
#   ./iniciar.sh --daemon     # só o jogo (sem REPL); logs em logs/
#   WYD_ADMIN_PASSWORD=x ./iniciar.sh
set -euo pipefail
cd "$(dirname "$0")"

export WYD_ADMIN_PASSWORD="${WYD_ADMIN_PASSWORD:-CHANGE_ME}"

BIN=./bin/wydserver
if [ ! -x "$BIN" ]; then
  echo "binário ausente: rode ./build-arm64.sh primeiro" >&2
  exit 1
fi

# Recusa executar binário da arch errada neste host (aviso)
ARCH=$(uname -m)
if [ "$ARCH" != "aarch64" ] && [ "$ARCH" != "arm64" ]; then
  echo "[aviso] host=$ARCH — este binário é linux/arm64; use qemu ou rode num device ARM64." >&2
  echo "        Para smoke local: qemu-aarch64 -L /usr/aarch64-linux-gnu $BIN --help" >&2
fi

if [ "${1:-}" = "--daemon" ]; then
  shift
  mkdir -p logs
  exec "$BIN" -admin 127.0.0.1:7480 "$@"
fi

exec "$BIN" --cli -admin 127.0.0.1:7480 "$@"
