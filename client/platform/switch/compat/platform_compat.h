#pragma once

// platform_compat.h — funil único de caminho/arquivo do port.
//
// Motivo (ver docs/PORT-STUDY-FALLOUT2-SWITCH.md §1): o cliente WYD herdou do
// Windows (a) separador '\\' literal em caminhos e (b) a suposição de que o
// sistema de arquivos é case-insensitive. Nenhuma das duas vale no SD do Switch
// (nem em ext4 com nomes mistos). Em vez de corrigir caso a caso — e esquecer
// alguns — TODA operação de arquivo passa por aqui:
//
//   PathToNative   : '\\' -> '/'
//   ResolvePath    : resolve componente a componente usando a CAIXA REAL do disco
//   Fopen/Access/Exists/Mkdir/Remove/Rename : wrappers que normalizam antes
//
// Custo: um opendir/readdir por componente. Para asset carregado uma vez é
// irrelevante; em streaming agressivo, memoizar (ver §1 do estudo).

#include <cstddef>
#include <cstdio>

namespace wyd_compat
{

// Tamanho máximo de caminho usado pelo port (o cliente tem caminhos longos, mas
// 512 cobre com folga; buffers de chamada devem usar este valor).
enum { WYD_PATH_MAX = 512 };

// Converte separadores Windows em POSIX, in place. Não aloca.
void PathToNative(char* path);

// Resolve, in place, cada componente do caminho para a caixa real do disco.
//   "Release/data/npc/x.bin" (pedido) -> "Release/Data/NPC/x.bin" (real)
// Regras: melhor esforço — componente não encontrado é mantido como veio (não
// falha o caminho inteiro); monta a partir de "/" quando o caminho é absoluto e
// trata a primeira componente "sdmc:"/"romfs:" como ponto de montagem.
// Retorna true se ALGO foi corrigido (útil para auditar os dados no diag).
bool ResolvePath(char* path);

// Quantos componentes foram corrigidos desde o boot (auditoria/diag).
unsigned CaseCorrectedCount();

// ResolvePath + PathToNative numa cópia (para APIs que não deixam mexer no buffer).
bool ResolveCopy(const char* path, char* out);

// --- wrappers de arquivo (sempre normalizam antes) ---------------------------
std::FILE* Fopen(const char* path, const char* mode);
int Access(const char* path, int mode);
bool Exists(const char* path);
bool Mkdir(const char* path);   // recursive (mkdir -p)
int Remove(const char* path);
int Rename(const char* old_path, const char* new_path);

// --- strings/texto ----------------------------------------------------------
int Stricmp(const char* a, const char* b);
int Strnicmp(const char* a, const char* b, std::size_t n);
char* Strupr(char* s);
char* Strlwr(char* s);
// Converte "CRLF" em "LF" in place (config/scripts da era Windows).
void NormalizeCrLf(char* text);

} // namespace wyd_compat
