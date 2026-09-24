#!/usr/bin/env bash
# Traduz todos os vertex shaders SM 1.x de um client WYD com vs11_glsl.cpp e valida o
# GLSL gerado com glslangValidator. Rodar antes do primeiro build de uma versão nova.
#
# Uso:
#   vs11-check.sh <WYDLINUX da versão> <pasta do client>
#   vs11-check.sh ~/wyd762/WYDLINUX ~/wyd762/WYDLINUX/client762
set -euo pipefail

ROOT="${1:?informe a raiz WYDLINUX (contém platform/switch/vs11_glsl.cpp)}"
CLIENT="${2:?informe a pasta do client (contém Shader/)}"
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

cat >"$WORK/main.cpp" <<'CPP'
#include "vs11_glsl.h"
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <vector>
int main(int argc, char** argv)
{
	int fails = 0;
	for (int a = 1; a < argc; ++a)
	{
		std::ifstream f(argv[a], std::ios::binary);
		std::vector<char> bytes((std::istreambuf_iterator<char>(f)), {});
		std::vector<uint32_t> code(bytes.size() / 4);
		std::memcpy(code.data(), bytes.data(), code.size() * 4);
		for (auto& d : code) // mesmo patch do RenderDevice no port Linux
			if (d == 0x90c60002u)
				d = 0x90e40002u;
		const Vs11Result r = TranslateVs11(code.data(), code.size());
		std::printf("%s: ok=%d instr=%d entradas=%zu %s\n", argv[a], r.ok, r.instructions, r.inputs.size(), r.error.c_str());
		if (!r.ok) { ++fails; continue; }
		std::string out = std::string(argv[a]) + ".vert";
		std::ofstream(out.substr(out.find_last_of('/') + 1)) << r.glsl;
	}
	return fails;
}
CPP

g++ -std=c++17 -O0 -I"$ROOT/platform/switch" "$WORK/main.cpp" "$ROOT/platform/switch/vs11_glsl.cpp" -o "$WORK/vs11"

mapfile -t bins < <(find "$CLIENT" -iname '*.bin' -size -64k -print0 | xargs -0 -I{} sh -c \
  'head -c4 "{}" | od -An -tx4 | grep -qi "fffe01" && echo "{}"' || true)
if [[ ${#bins[@]} -eq 0 ]]; then
  echo "nenhum vertex shader vs_1_x encontrado em $CLIENT" >&2
  exit 1
fi

cd "$WORK"
status=0
"$WORK/vs11" "${bins[@]}" || status=1
if command -v glslangValidator >/dev/null; then
  for v in *.vert; do
    if glslangValidator -S vert "$v" >/dev/null; then echo "GLSL ok: $v"; else echo "GLSL FALHOU: $v"; glslangValidator -S vert "$v" | head -5; status=1; fi
  done
else
  echo "aviso: glslangValidator ausente (apt install glslang-tools) — GLSL não validado"
fi
exit $status
