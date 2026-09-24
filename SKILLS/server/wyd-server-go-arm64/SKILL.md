---
name: wyd-server-go-arm64
description: >-
  Porta o servidor WYD-Go (protocolo 7.48, Go) de linux/amd64 para linux/arm64:
  build headless sem CGO, SQLite pure-Go, layout de deploy, smoke com QEMU e
  empacote Docker no device. Usar ao criar tree ARM64 do servidor, cross-compilar
  wydserver, subir em Orange Pi/VPS aarch64, ou depurar boot/SQLite/listen no ARM.
disable-model-invocation: false
---

# Servidor WYD-Go → linux/arm64

Idioma: **português**. Companion de deploy: `wyd-server-docker-deploy`.

O servidor nativo deste ecossistema é **WYD-Go** (Go), não o pack Wine
DBSRV/TMSRV. Protocolo client 7.48; porta de jogo típica **8281**.

## Arquitetura

```text
cmd/server (Go)  →  binário único (jogo + admin CLI)
                     ├─ headless: CGO_ENABLED=0  (VPS / Docker / cross)
                     └─ GUI SDL3: -tags gui + CGO + libSDL3  (só host nativo)
Persistência: modernc.org/sqlite (pure-Go — sem CGo)
```

**Não fazer:** misturar DBSRV/TMSRV Wine com este binário; cross com `-tags gui`
sem toolchain aarch64+SDL3; gravar paths absolutos do host de desenvolvimento em
`server.txt`; sobrescrever o binário amd64 com o arm64.

## Isolamento

1. Tree de trabalho ARM64 **separada** do deploy amd64 (dados podem ser cópia).
2. Source fica no repo Go; o tree ARM64 leva `bin/`, `data/`, scripts de build/run.
3. Não versionar `*.db-wal` / `*.db-shm`.

## Layout alvo (nomes genéricos)

```text
<SERVER-ARM64>/
  bin/wydserver          # linux/arm64, preferir estático/headless
  bin/account-create     # opcional
  data/                  # mapas, npcs, server.txt, wydgo.db, …
  logs/  backup/
  build-arm64.sh
  iniciar.sh             # --cli / --daemon (sem GUI)
  docker/                # ver skill de deploy
```

## Build (cross a partir de amd64)

```bash
cd <REPO-GO>
CGO_ENABLED=0 GOOS=linux GOARCH=arm64 \
  go build -trimpath -ldflags='-s -w' -o <SERVER-ARM64>/bin/wydserver ./cmd/server
```

Validar:

```bash
file bin/wydserver   # ARM aarch64
```

### Por que headless no cross

| Modo | CGO | Quando |
|---|---|---|
| Headless (padrão ARM64) | `0` | VPS, Docker, Orange Pi headless |
| `-tags gui` | `1` + libSDL3 aarch64 | Só **no** device aarch64 com headers SDL3 |

GUI no cross amd64→arm64 sem sysroot SDL é armadilha — não insistir.

## Portabilidade (o que já está resolvido no Go)

- Endian / alinhamento: protocolo e structs via tipos fixos no Go — não há asm x86
  no caminho crítico do servidor.
- SQLite: driver **pure-Go** (`modernc.org/sqlite`) — funciona com `CGO_ENABLED=0`.
- Unsafe/cgo só na GUI (`cmd/server/gui/sdl`) — fora do build headless.

Se portar **outro** servidor C++ (TMSRV clássico): aí sim auditar `#pragma pack`,
`long` LP64, inline asm e alinhamento — fora do escopo desta skill.

## Config para ARM / Docker

Em `data/server.txt` (ou equivalente):

- `listen_address=0.0.0.0:8281` — necessário em container / multi-NIC
- `database_driver=sqlite` + `database_path=data/wydgo.db`
- **Remover** paths absolutos de `client_serverlist` do host de dev
- Senha admin: env `WYD_ADMIN_PASSWORD` > `admin_password` no txt

## Smoke

### QEMU no host amd64

```bash
qemu-aarch64 -L /usr/aarch64-linux-gnu ./bin/wydserver -addr 127.0.0.1:18281
```

Esperado no log: mapas/NPCs carregados, `TMSrv escutando em …`.

### Device aarch64

```bash
./iniciar.sh --daemon
# ou
./bin/wydserver -config data/server.txt -addr 0.0.0.0:8281
```

## Critério de aceite

- [ ] `file` = ARM aarch64
- [ ] Boot: HeightMap/AttributeMap, NPCGener, SQLite sem panic
- [ ] Listen na porta do jogo; admin TCP opcional em loopback
- [ ] SIGTERM persiste estado limpo
- [ ] Client da versão conecta (serverlist aponta IP:porta certos)

## Laço

```text
build cross → file → smoke QEMU → rsync tree → (Docker ou binário nativo) →
client login → anotar falha de protocolo/path
```
