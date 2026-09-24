#!/usr/bin/env bash
# Build WYDLINUX e instala o candidato em client748/ (para testes locais).
# Não altera o source Win32 em codigo-fonte/.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD="${ROOT}/build"
DEST="${ROOT}/client748"

if [[ ! -d "${DEST}" ]]; then
  echo "ERRO: pasta de teste ausente: ${DEST}" >&2
  echo "Copie o client748 para WYDLINUX/client748/ antes do build." >&2
  exit 1
fi

cmake -S "${ROOT}" -B "${BUILD}" -DCMAKE_BUILD_TYPE=Release

# Candidato principal: cliente real (wyd_client). Smoke project é opcional.
if ! cmake --build "${BUILD}" --target wyd_client -j"$(nproc)"; then
  echo "ERRO: falha ao compilar wyd_client" >&2
  exit 1
fi
cmake --build "${BUILD}" --target wyd_project -j"$(nproc)" 2>/dev/null || true

# Smokes auxiliares (não bloqueiam o binário principal).
cmake --build "${BUILD}" -j"$(nproc)" || true

# Preferir o cliente real se existir; senão o smoke project.
if [[ -x "${BUILD}/wyd_client" ]]; then
  PRIMARY_SRC="${BUILD}/wyd_client"
elif [[ -x "${BUILD}/wyd_project" ]]; then
  PRIMARY_SRC="${BUILD}/wyd_project"
else
  echo "ERRO: nenhum binário principal gerado em ${BUILD}" >&2
  exit 1
fi
PRIMARY_DST="${DEST}/project"

install -m 755 "${PRIMARY_SRC}" "${PRIMARY_DST}"
sha_src="$(sha256sum "${PRIMARY_SRC}" | awk '{print $1}')"
sha_dst="$(sha256sum "${PRIMARY_DST}" | awk '{print $1}')"
if [[ "${sha_src}" != "${sha_dst}" ]]; then
  echo "ERRO: hash divergente após instalar ${PRIMARY_DST}" >&2
  exit 1
fi

# Smokes auxiliares (mesmo diretório de assets).
for bin in wyd-wayland d3d9_min_smoke asset_render_smoke d3pp_smoke net_smoke ArchitectureTests wyd_project; do
  if [[ -x "${BUILD}/${bin}" ]]; then
    install -m 755 "${BUILD}/${bin}" "${DEST}/${bin}"
  fi
done

echo
echo "Instalado em ${DEST}/"
echo "  project  ← wyd_project  SHA-256 ${sha_dst}"
ls -la "${DEST}/project" "${DEST}/wyd_project" 2>/dev/null || true
echo
echo "Teste:"
echo "  cd \"${DEST}\" && ./project"

# Sempre compilar o cliente ARM64 após o x86_64 (árvore e binário separados).
echo
echo "=== build aarch64 (sempre após x86_64) ==="
"${ROOT}/scripts/build-aarch64.sh"
echo
echo "Binários:"
ls -la "${DEST}/project" "${DEST}/project-aarch64" 2>/dev/null || true
