#!/usr/bin/env bash
# Envia OUT/switch/wyd748.nro para o FTP do Switch.
# O FTP nem sempre está ligado — só rode depois de ativar no console.
#
# Uso:
#   ./scripts/deploy-switch-ftp.sh
#   WYD_SWITCH_FTP=ftp://192.168.1.6:5000/switch/client748/ ./scripts/deploy-switch-ftp.sh
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
NRO="${WYD_SWITCH_NRO:-$ROOT/OUT/switch/wyd748.nro}"
FTP_URL="${WYD_SWITCH_FTP:-ftp://192.168.1.6:5000/switch/client748/}"

if [[ ! -f "$NRO" ]]; then
  echo "erro: não achei $NRO — rode ./scripts/build-switch-docker.sh primeiro" >&2
  exit 1
fi

# Checagem rápida: se o FTP estiver off, falha cedo com mensagem clara.
host_port="${FTP_URL#ftp://}"
host_port="${host_port%%/*}"
host="${host_port%%:*}"
port="${host_port##*:}"
[[ "$port" == "$host" ]] && port=21

if ! timeout 3 bash -c "echo >/dev/tcp/${host}/${port}" 2>/dev/null; then
  echo "erro: FTP inacessível em ${host}:${port}" >&2
  echo "Ative o servidor FTP no Switch (ex.: sys-ftpd / AIO) e rode de novo." >&2
  exit 2
fi

echo "[WYD_SWITCH] enviando $(basename "$NRO") → $FTP_URL"
curl -T "$NRO" --ftp-create-dirs "${FTP_URL%/}/$(basename "$NRO")"

# --audio: sound/ + music/ + Music.txt originais 7.48 (OUT/switch/audio748, ~116 MB).
if [[ "${1:-}" == "--audio" ]]; then
  AUDIO="${WYD_SWITCH_AUDIO:-$ROOT/OUT/switch/audio748}"
  if [[ ! -f "$AUDIO/sound/soundlist.txt" ]]; then
    echo "erro: pacote de áudio ausente em $AUDIO" >&2
    exit 3
  fi
  count=0
  while IFS= read -r -d '' f; do
    rel="${f#"$AUDIO"/}"
    curl -s -T "$f" --ftp-create-dirs "${FTP_URL%/}/${rel}"
    count=$((count + 1))
  done < <(find "$AUDIO" -type f -print0)
  echo "[WYD_SWITCH] áudio: $count arquivos enviados"
fi
echo "[WYD_SWITCH] OK"
