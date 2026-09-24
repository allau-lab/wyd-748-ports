# WYD — Nintendo Switch (Homebrew)

Client TMProject no Switch: backend D3D9 → OpenGL ES 3 (SDL3). Detalhe: `SKILL.md`.

## Build (Docker — nada instalado no host)

```bash
cd <PORT-TREE>
./scripts/build-switch-docker.sh              # → OUT/switch/<nome>.nro
./scripts/build-switch-docker.sh reconfigure  # depois de adicionar .cpp ao CMake
./scripts/deploy-switch-ftp.sh                # envia ao console (FTP ligado)
```

## Instalação no console

Copiar o pack de assets da versão + o `.nro` para `sdmc:/switch/<pasta>/`,
com TTF reais em `UI/` / `fonts/`. Abrir pelo Homebrew Menu.

## Controles (base)

| Botão | Ação |
|---|---|
| Analógico esquerdo | cursor / movimento (conforme mapa do port) |
| A / B | clique esquerdo / direito |
| Direcional | setas |
| X | PROBE de diagnóstico (quando habilitado) |
| Toque | cursor + clique |

Mapeamento fino de Joy-Con/HUD: ver `references/ui-switch.md`.

## Evidência

```bash
WYD_SWITCH_FTP=ftp://<IP>:5000/switch/<pasta> ./scripts/puxar-evidencia.sh /tmp/ev
```

Nunca afirmar “funciona” sem diag + captura do console.
