#pragma once
// Tradutor de vertex shader Direct3D 9 SM 1.x (vs_1_0 / vs_1_1) -> GLSL ES 3.00.
// O GLSL gerado escreve as mesmas saídas que o fragment shader FFP consome
// (vColor, vUV0, vUV1, vFogDepth), então o combiner de texture stages é reaproveitado.

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

struct Vs11Input
{
	int reg = 0;        // vN -> layout(location = N)
	int usage = 0;      // D3DDECLUSAGE_*
	int usageIndex = 0;
};

struct Vs11Result
{
	bool ok = false;
	std::string glsl;
	std::vector<Vs11Input> inputs;
	std::string error;
	int instructions = 0;
};

// code: bytecode começando no token de versão (0xFFFE01xx), terminado em 0x0000FFFF.
// Uniforms do GLSL gerado: vec4 uC[96] (constantes c#) e float uYFlip (+1 backbuffer, -1 RT).
Vs11Result TranslateVs11(const uint32_t* code, size_t numDwords);
