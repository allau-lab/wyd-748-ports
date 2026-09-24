#!/usr/bin/env bash
# Abre o configurador gráfico do cliente WYD 7.48 (resolução, qualidade, áudio).
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
exec python3 "$ROOT/scripts/wyd-settings.py"
