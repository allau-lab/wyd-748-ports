# SKILLS — port WYD (client + servidor)

Skills cirúrgicas e **independentes de máquina**: sem paths absolutos do autor.
Cada pasta é uma skill Cursor (`SKILL.md` + refs opcionais). Copie para
`~/.agents/skills/` / `.cursor/skills/` ou use este tree no repo.

Idioma: **português**.

## Client

| Skill | Quando usar |
|---|---|
| [`client/wyd-linux-client-port`](client/wyd-linux-client-port/) | Port TMProject → Linux (DXVK Native + Wayland + SDL); playbook e fases |
| [`client/wyd-linux-win32-compat`](client/wyd-linux-win32-compat/) | Shims Win32: paths, CRT, janela, GDI, net, touch |
| [`client/wyd-linux-dxvk-d3dx`](client/wyd-linux-dxvk-d3dx/) | DXVK WSI, DXT/WYT/WYS, bpp da fonte, UI oca / arco-íris |
| [`client/wyd-switch-port`](client/wyd-switch-port/) | Port → Nintendo Switch (D3D9→GLES, Docker devkitA64) |
| [`client/wyd-mobile-ui`](client/wyd-mobile-ui/) | UI touch-first (métricas, chat, inventário HD) |

## Servidor

| Skill | Quando usar |
|---|---|
| [`server/wyd-server-go-arm64`](server/wyd-server-go-arm64/) | WYD-Go → linux/arm64 (cross `CGO_ENABLED=0`, SQLite pure-Go, smoke QEMU) |
| [`server/wyd-server-docker-deploy`](server/wyd-server-docker-deploy/) | Subir o binário via Docker Compose (volume `data/`, porta 8281) |

## Regras comuns

1. Isolar **versão** e **arch** — não misturar assets/packets/binários.
2. Backend **real** no caminho crítico — sem stub `S_OK` / textura null.
3. Sintoma novo → documentar na tabela sintoma→causa da skill.
4. Nunca afirmar “funciona” sem evidência (log, captura, login→campo / listen).

## Instalação rápida (agente local)

```bash
# a partir da raiz deste repo
for d in SKILLS/client/* SKILLS/server/*; do
  name=$(basename "$d")
  ln -sfn "$(pwd)/$d" "$HOME/.agents/skills/$name"
  ln -sfn "$(pwd)/$d" "$HOME/.cursor/skills/$name"
done
```
