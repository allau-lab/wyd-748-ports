# Client WYD 7.48 — Linux (x86_64 / ARM64) + Switch

Source do port (TMProject + `platform/linux` + `platform/switch`).

Documentação de entrada: **[PORT.md](PORT.md)**, **[AGENTS.md](AGENTS.md)**, **[DXVK.md](DXVK.md)**.

Skills: `../SKILLS/client/`.

## Targets

| Arch | Como |
|---|---|
| linux/amd64 | `cmake` + `wyd_client` → binário no seu dir de assets |
| linux/arm64 | `scripts/build-aarch64-docker.sh` (não sobrescreve o bin amd64) |
| Switch | `scripts/build-switch-docker.sh` → `.nro` |

Não versionamos o pack de assets nem o DXVK Native baixado.
