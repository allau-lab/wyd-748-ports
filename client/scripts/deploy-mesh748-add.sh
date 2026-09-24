#!/usr/bin/env bash
# Envia OUT/switch/mesh748_add → ftp://…/switch/client748/mesh/
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SRC="${WYD_SWITCH_MESH_ADD:-$ROOT/OUT/switch/mesh748_add}"
FTP_BASE="${WYD_SWITCH_FTP:-ftp://192.168.1.6:5000/switch/client748/}mesh"
FTP_BASE="${FTP_BASE%/}"
[[ -d "$SRC" ]] || { echo "erro: $SRC ausente"; exit 1; }
mapfile -d '' FILES < <(find "$SRC" -type f -print0)
echo "[mesh] ${#FILES[@]} arquivos → $FTP_BASE"
ok=0; fail=0
for f in "${FILES[@]}"; do
  rel="${f#$SRC/}"
  if curl -s -T "$f" --ftp-create-dirs --connect-timeout 5 --max-time 60 "${FTP_BASE}/${rel}" >/dev/null; then
    ok=$((ok+1))
  else
    fail=$((fail+1)); echo "fail $rel"
  fi
  if (( (ok+fail) % 100 == 0 )); then echo "  … $((ok+fail))/${#FILES[@]}"; fi
done
echo "[mesh] ok=$ok fail=$fail"
[[ $fail -eq 0 ]]
