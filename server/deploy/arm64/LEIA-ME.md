# ServidorArm64 — WYD-Go (linux/arm64)

Port ARM64 do servidor Linux (`servidor/`). Base: `codigo-fonte/` (WYD-Go).

| Peça | Notas |
|---|---|
| Binário | `bin/wydserver` — **headless** (`CGO_ENABLED=0`, sem `-tags gui`) |
| Dados | cópia de `servidor/data/` no momento do fork |
| Admin | `--cli` (sem janela SDL3; GUI exige CGO + libSDL3 aarch64) |

## Build

```bash
./build-arm64.sh
file bin/wydserver   # deve dizer ARM aarch64
```

## Rodar num host ARM64

```bash
./iniciar.sh           # jogo + console admin
./iniciar.sh --daemon  # só o jogo
```

Porta do jogo: ver `data/server.txt` (`listen_address`, padrão `8281`).

## Smoke no host x86_64 (opcional)

```bash
sudo apt-get install -y qemu-user-static
qemu-aarch64 -L /usr/aarch64-linux-gnu ./bin/wydserver -h
```

## Rebuild da GUI no ARM64 nativo

Num device aarch64 com `libsdl3-dev`:

```bash
cd ../codigo-fonte
CGO_ENABLED=1 go build -tags gui -o ../ServidorArm64/bin/wydserver ./cmd/server
```

Não misture esse binário com o headless cross-compilado sem renomear.
