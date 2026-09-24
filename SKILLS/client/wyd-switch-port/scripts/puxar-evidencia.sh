#!/usr/bin/env bash
# Baixa do console o diag e as capturas do backend e converte TGA -> PNG.
#
# Uso:
#   WYD_SWITCH_FTP=ftp://<IP-DO-CONSOLE>:5000/switch/<pasta> puxar-evidencia.sh [destino]
set -euo pipefail

if [ -z "${WYD_SWITCH_FTP:-}" ]; then
  echo "erro: defina WYD_SWITCH_FTP=ftp://IP:5000/switch/<pasta>" >&2
  exit 2
fi
FTP="${WYD_SWITCH_FTP%/}"
DEST="${1:-/tmp/wyd-switch-evidencia}"
DIAG="${WYD_SWITCH_DIAG:-wyd_diag.txt}"
mkdir -p "$DEST"

if ! timeout 5 curl -s "$FTP/" >/dev/null; then
  echo "erro: FTP inacessível em $FTP — ligue o servidor FTP no console" >&2
  exit 2
fi

curl -s -o "$DEST/$DIAG" "$FTP/$DIAG" || true
echo "== diag: $DEST/$DIAG"
rg -n '\[BUILD\]|\[NET\]|\[FONT\]|\[VS\]|FALHOU|FFP ok|\[SHOT\]' "$DEST/$DIAG" | head -40 || true

shots=$(curl -s "$FTP/shots/" | awk '{print $NF}' | grep -E '\.tga$' || true)
for s in $shots; do
  curl -s -o "$DEST/$s" "$FTP/shots/$s"
done

python3 - "$DEST" <<'PY'
import pathlib, sys
from PIL import Image
for tga in sorted(pathlib.Path(sys.argv[1]).glob("*.tga")):
    png = tga.with_suffix(".png")
    Image.open(tga).save(png)
    print("== captura:", png)
PY
