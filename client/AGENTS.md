# AGENTS.md — WYDLINUX (port Linux do client)

Ler também o `AGENTS.md` da raiz do monorepo / `client748/` quando a tarefa
tocar assets ou protocolo. Este arquivo governa **só** o tree `WYDLINUX/`.

## Escopo

- Trabalho **apenas** em `WYDLINUX/`. Não alterar `codigo-fonte/` (source
  Windows / WYD-Go) salvo pedido explícito do usuário.
- Binário de validação do jogo: `client748/project` (x86_64) ou
  `client748/WYD Arm64` (aarch64). Smokes não substituem o client real.

## Documentação obrigatória

Antes de “reinventar” o port ou portar outra versão:

1. [PORT.md](PORT.md) — playbook 7.60+ e pitfalls
2. [docs/RUNTIME.md](docs/RUNTIME.md) — sintoma→causa
3. [docs/COMPAT-WIN32.md](docs/COMPAT-WIN32.md) — camada compat

## Arquivos sensíveis (não regenerar / não sobrescrever à toa)

Patches Linux concentrados — um sync cego do Windows apaga comportamento:

| Arquivo | Por quê |
|---|---|
| `platform/linux/compat/winuser_sdl.cpp` | SDL/Wayland, fullscreen desktop, mouse livre, ícone, TEXTINPUT |
| `platform/linux/compat/EventTranslator_linux.cpp` | Input + touch fallthrough + LoL |
| `platform/linux/compat/TouchControlsUI.*` | HUD touch |
| `platform/linux/compat/d3dx9_linux.cpp` | TGA/DXT/resize fonte |
| `platform/linux/compat/win32_extras.h` | paths, ShellExecute, CRT |
| `CMakeLists.txt` POST_BUILD | paths `project` vs `WYD Arm64` — não misturar arches |

Ao mergear TMProject novo: diff **dentro** de `TMProject*/`; manter compat.

## Build

- x86_64: `build/` → `client748/project`
- aarch64/cross: `build-aarch64/` → `WYD Arm64` (+ `touch_icons/`); **nunca**
  sobrescrever `project` x86_64
- DXVK: `third_party/dxvk-native` vs `dxvk-native-aarch64` conforme arch
- Não commitar segredos; `third_party/dxvk-native*` pode ser artefato local

## Critério de aceite

login → selserver (texto legível) → selchar → campo. Sem stub no caminho crítico.

## Idioma

Docs e respostas ao usuário deste port: **português** (alinhar ao restante do repo).
