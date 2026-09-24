# Gráficos: DXVK + D3DX + texturas + fontes

## DXVK Native

- Lib x86_64: `third_party/dxvk-native/usr/lib/libdxvk_d3d9.so`
- Lib aarch64: `third_party/dxvk-native-aarch64/usr/lib/libdxvk_d3d9.so`
  (gerada pelo `build-aarch64-docker.sh` / build nativo do DXVK)
- Override CMake: `WYD_DXVK_NATIVE_ROOT`
- Env: `DXVK_WSI_DRIVER=SDL2` (definir no `main` e na criação da janela)
- Contrato: `CreateDevice(..., hFocus = SDL_Window*, ...)`
- Janela: `SDL_WINDOW_VULKAN` (+ `FULLSCREEN_DESKTOP` se `WS_POPUP`)
- Log útil: `Presenter: Actual swapchain properties` = surface OK
- `VK_ERROR_OUT_OF_HOST_MEMORY` na surface costuma ser **HWND/WSI errado**, não OOM
- Não misturar `.so` x86_64 no link aarch64 (e vice-versa)

## D3DX (`d3dx9_linux.cpp`)

Implementação real usada pelo TMProject:

| Entrada | Comportamento |
|---|---|
| DDS | FourCC DXT1/3/5; upload nativo com pitch por bloco; se Format uncompressed → decode RGBA |
| TGA | type 2/10, 16/24/32 bpp (UI após strip `WT10`) |
| BMP | 24/32 bpp |
| Width/Height ≠ 0/-1 | **Redimensiona** (nearest) — obrigatório para TMFont2 |
| ColorKey | RGB match → alpha 0 |

### Hack da fonte (TMFont2)

```text
D3DXCreateTextureFromFileInMemoryEx(
  minimap.wyt como TGA,
  Width=512, Height=64,
  Format=A4R4G4B4, ColorKey=preto)
→ LockRect e grava glyphs uint16 A4R4G4B4 usando d3dlr.Pitch
```

Se Width/Height forem ignorados, a textura fica 128×128 e o texto sai **esticado/scanline**.

## Assets WYT / WYS

| | WYT | WYS |
|---|---|---|
| Magic | `WT10` | `WS10` |
| Uso | UI | mesh/effect/env |
| Transform | −4 bytes + footer TGA | −1 byte + `"DDS"` + FourCC@84 (`2`→DXT1 senão DXT3) |

Decoders de referência: `wyt_decode.cpp`, `wys_decode.cpp` (também usados em smokes).

## Flags D3DDevice

Inicializar no construtor:

- `m_bDXT1 = 1`, `m_bDXT3 = 1`, `m_dwBitCount = 32`
- `D3DDevice::m_bDxt` static = 1 (pode ser desligado por config)

Sem isso, `TextureManager` pede A8R8G8B8 em cima de payload DXT → noise arco-íris.
