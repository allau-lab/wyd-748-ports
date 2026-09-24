#!/usr/bin/env bash
# Compara TMProject748 arquivo a arquivo: codigo-fonte (baseline) vs WYDLINUX (port).
# Uso: ./scripts/audit-port-diff.sh [resumo|detalhe <rel-path>]
set -uo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
A="$ROOT/../codigo-fonte/client-source/tmproject/TMProject748"
B="$ROOT/TMProject748"
MODE="${1:-resumo}"

if [[ ! -d "$A" || ! -d "$B" ]]; then
  echo "Baseline ou port ausente:" >&2
  echo "  A=$A" >&2
  echo "  B=$B" >&2
  exit 1
fi

if [[ "$MODE" == "detalhe" ]]; then
  REL="${2:?passe o caminho relativo sob TMProject748}"
  diff -u "$A/$REL" "$B/$REL" | less -F
  exit 0
fi

echo "=== WYDLINUX port audit vs codigo-fonte ==="
echo "baseline: $A"
echo "port:     $B"
echo

identical=0
differ=0
DIFF_FILES=()
while IFS= read -r -d '' f; do
  rel="${f#"$A"/}"
  case "$rel" in
    Dependencies/*) continue ;;
  esac
  if [[ -f "$B/$rel" ]]; then
    if cmp -s "$A/$rel" "$B/$rel"; then
      identical=$((identical + 1))
    else
      differ=$((differ + 1))
      n=$(diff -u "$A/$rel" "$B/$rel" | wc -l) || true
      DIFF_FILES+=("$n $rel")
    fi
  fi
done < <(find "$A" -type f \( -name '*.cpp' -o -name '*.h' \) -print0)

echo "cpp/h idênticos: $identical"
echo "cpp/h divergentes: $differ"
echo
echo "=== Ranking por tamanho do diff (linhas unificadas) ==="
if ((${#DIFF_FILES[@]})); then
  printf '%s\n' "${DIFF_FILES[@]}" | sort -rn | head -40
fi
echo
echo "=== Camada Linux (fora do TMProject — sempre extra) ==="
find "$ROOT/platform/linux" -type f \( -name '*.cpp' -o -name '*.h' \) | wc -l
echo "arquivos em platform/linux"
echo
echo "Próximo: $0 detalhe internal/app/scenes/TMFieldScene.cpp"
echo "         e revisar platform/linux/compat/ (EventTranslator, winuser, D3DX)"
