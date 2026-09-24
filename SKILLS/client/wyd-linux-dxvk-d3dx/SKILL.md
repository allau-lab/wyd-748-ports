---
name: wyd-linux-dxvk-d3dx
description: >-
  Gráficos do port Linux do client WYD: DXVK Native (libdxvk_d3d9.so) + WSI SDL
  Wayland, D3DX (DDS/DXT/TGA/BMP/resize/bpp), flags DXT do device, assets WYT/WYS
  e armadilhas de fonte TMFont2. Usar quando houver UI branca/oca, mesh arco-íris,
  texto esticado, SEGV em create_tex_from_rgba, Init Render Failed, surface OOM,
  cross ARM64 do DXVK, ou “DDS parcial”.
disable-model-invocation: false
---

# DXVK Native + D3DX (client WYD Linux)

Companion de `wyd-linux-client-port`. Sem isso o jogo sobe mas a UI/campo mentem.

## Contrato DXVK

```text
main → setenv DXVK_WSI_DRIVER=SDL2|SDL3
     → CreateWindow (SDL_WINDOW_VULKAN [+ FULLSCREEN_DESKTOP se WS_POPUP])
     → Direct3DCreate9 → CreateDevice(..., hFocus = SDL_Window*, ...)
```

| Regra | Detalhe |
|---|---|
| Prefix por arch | `dxvk-native` (x86_64) ≠ `dxvk-native-aarch64` |
| Link | `libdxvk_d3d9.so` + RPATH/`LD_LIBRARY_PATH` da **mesma** arch |
| Headers | forçar include do prefix Native; não misturar SDK D3D9 antigo |
| aarch64 | **compilar** DXVK Native (não há tarball sniper confiável igual ao x86) |
| Layers | `VK_LOADER_LAYERS_DISABLE=*steam*` |
| Surface OOM | quase sempre HWND/WSI errado, não falta de RAM |
| Log bom | Presenter / swapchain properties |

Smoke isolado: CreateDevice + Create*Shader nos bins `Shader/*.bin`.

## Flags do device

No construtor do device:

- `m_bDXT1 = 1`, `m_bDXT3 = 1`, bit count 32
- Sem isso: TextureManager pede A8R8G8B8 em cima de payload DXT → **arco-íris**

## D3DX real (`d3dx9_linux` ou equivalente)

| Entrada | Comportamento obrigatório |
|---|---|
| DDS | Detectar **primeiro**; FourCC DXT1/3/5; pitch **por bloco**; decode→RGBA se Format uncompressed |
| TGA | type 2/10, 16/24/32 bpp (UI após strip WYT) |
| BMP | 24/32 bpp |
| Width/Height ≠ 0/-1 | **Redimensionar** (nearest) — senão fonte esticada |
| ColorKey | RGB match → alpha 0 |
| Fail | `*ppTexture = nullptr`; **não** cair em TGA se magic for DDS |

### bpp do Format (SEGV clássico)

`TMFont2` pede **512×64** `D3DFMT_A4R4G4B4` (2 B/px). Upload como BGRA8
estoura `LockRect` → SEGV pós-Present em `create_tex_from_rgba`.

- A4R4G4B4 / R5G6B5 / A1R5G5B5 → pack 16-bit
- A8R8G8B8 → BGRA8
- Validar `Pitch >= w * bpp`

### Hack da fonte

```text
D3DXCreateTextureFromFileInMemoryEx(..., Width=512, Height=64,
  Format=A4R4G4B4, ColorKey=preto)
→ LockRect; glyphs uint16 com d3dlr.Pitch
```

Ignorar Width/Height → textura ~128² → texto scanline/esticado.

## Assets WYT / WYS

| | WYT | WYS |
|---|---|---|
| Magic | `WT10` | `WS10` |
| Uso típico | UI | mesh/effect/env |
| Transform | −4 bytes → TGA (+ footer) | −1 byte → prefixo `DDS` + FourCC@84 (`2`→DXT1 senão DXT3) |

Validar **na versão alvo**: tamanho de record das listas, magic, paths, shaders
`.bin`. Não copiar offsets de outra release.

## Sintomas gráficos (atalho)

| Sintoma | Fix |
|---|---|
| UI branca / oca / botões ausentes | TGA + DXT completos; textura não-null |
| Mesh arco-íris | `m_bDXT*=1` + pitch DXT |
| SEGV em create_tex / TMFont2 | bpp A4R4G4B4 |
| Texto esticado | resize 512×64 |
| Init Render + surface OOM | `SDL_WINDOW_VULKAN` + WSI |
| “DDS … parcial” no log | decoder DXT incompleto — regressão |

Tabela completa: `wyd-linux-client-port` → `references/sintomas.md`.
