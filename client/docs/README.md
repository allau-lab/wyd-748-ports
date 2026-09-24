# Documentação WYDLINUX (WYD 7.48 → Linux/Wayland)

Índice para manter este port e portar **outra versão** (7.60+) do client.

| Documento | Conteúdo |
|---|---|
| **[../PORT.md](../PORT.md)** | **Playbook 7.60+** + regras/estado do 7.48 |
| [PORTING.md](PORTING.md) | Checklist curto (bloqueios já vistos) |
| [ARCHITECTURE.md](ARCHITECTURE.md) | Árvore, CMake, binários, touch |
| [COMPAT-WIN32.md](COMPAT-WIN32.md) | Win32→Linux (paths, GDI, janela, touch) |
| [GRAPHICS.md](GRAPHICS.md) | DXVK Native, D3DX, WYT/WYS, fontes |
| [RUNTIME.md](RUNTIME.md) | Build/run x86_64+ARM64, env, sintomas→causa |
| [../DXVK.md](../DXVK.md) | Histórico de fases DXVK / smokes |
| [../AGENTS.md](../AGENTS.md) | Guardas para agents |
| [../README.md](../README.md) | Visão rápida + build |

## Regra de ouro

1. **Não alterar** `codigo-fonte/`. Trabalho só em `WYDLINUX/`.
2. **Proibido stub/no-op** para feature usada pelo client — backend real.
3. Contrato de assets/packets da versão alvo deve ser preservado.
4. Binários: `client748/project` (x86_64) / `client748/WYD Arm64` (aarch64)
   ← target `wyd_client`.
5. Não sobrescrever `winuser_sdl` / touch / EventTranslator Linux ao mergear
   source Windows.
