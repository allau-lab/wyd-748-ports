# WYD 7.48 — ports (client + servidor)

Monorepo com **código-fonte** dos ports e as **Cursor skills** cirúrgicas.
Sem assets de jogo (`client748/`), sem binários DXVK/pré-compilados, sem builds.

| Pasta | Conteúdo |
|---|---|
| [`client/`](client/) | Client TMProject: Linux **x86_64** + **ARM64** (DXVK Native) e **Nintendo Switch** (D3D9→GLES) |
| [`server/wyd-go/`](server/wyd-go/) | Servidor **WYD-Go** (Go) — usado em amd64 e arm64 |
| [`server/deploy/arm64/`](server/deploy/arm64/) | Deploy ARM64 (Docker + scripts; compile o binário aí) |
| [`SKILLS/`](SKILLS/) | Skills Cursor (Linux / Switch / servidor / Docker) |

## Client

Um único tree cobre as três arquiteturas:

- **x86_64 / aarch64 Linux:** Wayland + DXVK Native + SDL — ver `client/PORT.md`
- **Switch:** `platform/switch/` + `switch/` — build só em Docker `devkitpro/devkita64`

DXVK Native **não** vem no repo: use `client/scripts/fetch-dxvk-native.sh` (e o fluxo aarch64 do `PORT.md`).

Assets do jogo (UI, mesh, `serverlist.bin`, …) ficam **fora** deste repo — aponte o CMake/`client*` para o pack da sua versão.

## Servidor

```bash
cd server/wyd-go
# amd64
go build -o ../../server/deploy/amd64-bin/wydserver ./cmd/server
# arm64 (headless)
CGO_ENABLED=0 GOOS=linux GOARCH=arm64 go build -o ../../server/deploy/arm64/bin/wydserver ./cmd/server
```

Deploy no device ARM64: `server/deploy/arm64/` + skill `wyd-server-docker-deploy`.

O código Go deriva do projeto WYD-Go (GPL-3.0). Veja `server/wyd-go/LICENSE`.

## Skills

Copie `SKILLS/client/*` e `SKILLS/server/*` para `~/.agents/skills/` (ou siga `SKILLS/README.md`).

## O que não entra de propósito

- `client748/` / packs de assets
- `third_party/dxvk-native*`
- pastas `build*`, `OUT/`, `.nro`, `.so` de runtime
- bancos SQLite e senhas reais
