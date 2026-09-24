# DXVK no port WYDLINUX

## Situação

| Camada | Estado |
|---|---|
| Janela Wayland + Vulkan (SPIR-V) | OK |
| Campo MSA 3D + stand-in DX9 ×18 | OK (G/G+) |
| **DXVK Native** `libdxvk_d3d9.so` | OK (Fase H) — smoke dedicado |
| Wine DXVK | Fora do alvo Wayland-nativo |

## Fase H (entregue)

Device D3D9 real via **DXVK Native 3.0.2** (Steam Runtime sniper):

- `Direct3DCreate9` → `CreateDevice` (HWND = `SDL_Window*`, `DXVK_WSI_DRIVER=SDL2`)
- `Clear` / `Present`
- `CreateVertexShader` / `CreatePixelShader` nos **18** bins `Shader/*.bin`

Smoke (não mistura swapchain com `vulkan_textured` do `project`):

```bash
cd WYDLINUX
./scripts/fetch-dxvk-native.sh   # se ainda não baixou
./scripts/build.sh
cd client748
export DXVK_WSI_DRIVER=SDL2
export WYD_DXVK_LIBDIR="$(pwd)/../third_party/dxvk-native/usr/lib"
export LD_LIBRARY_PATH="$WYD_DXVK_LIBDIR:$LD_LIBRARY_PATH"
export SDL_VIDEODRIVER=wayland VK_LOADER_LAYERS_DISABLE='*steam*'
./dxvk_native_smoke
# esperado: CreateDevice OK + Create*Shader nativo 18/18
```

## Fase H+ (entregue)

`client748/project` usa **DXVK Native como backend único** quando a `.so` está
disponível (sem criar swapchain SPIR-V na mesma janela):

- Splash / selchar / mapa 2D: `CreateTexture` + `StretchRect` + `Present`
- Campo 3D (T): Clear tint por stand-in shader (mesh MSA via D3D9 ainda não)
- Fallback: `WYD_USE_DXVK_NATIVE=0` ou lib ausente → Vulkan SPIR-V (G+)

```bash
cd WYDLINUX/client748
export DXVK_WSI_DRIVER=SDL2
export WYD_DXVK_LIBDIR="$(pwd)/../third_party/dxvk-native/usr/lib"
export LD_LIBRARY_PATH="$WYD_DXVK_LIBDIR:$LD_LIBRARY_PATH"
export SDL_VIDEODRIVER=wayland VK_LOADER_LAYERS_DISABLE='*steam*'
WYD_LOGIN_MOCK=1 WYD_CHAR_SLOT=0 ./project
# título: "... | dxvk"  / log: backend DXVK Native
# força SPIR-V: WYD_USE_DXVK_NATIVE=0 ./project
```

O smoke `dxvk_native_smoke` permanece o gate isolado CreateDevice/Create*Shader.

## Fase I (mesh MSA via D3D9)

Campo 3D no `project` (backend DXVK): `DrawIndexedPrimitiveUP` com FVF
`XYZ|DIFFUSE`, Z-buffer, tint do stand-in shader. Toggle `T` volta ao blit 2D.

## Fase J (bind real skinmesh*.bin)

Mesh 3D no DXVK: `CreateVertexDeclaration` (layout skinmesh1) +
`SetVertexShader(skinmeshN)` + constantes c1/c2/c9/c92 + `DrawIndexedPrimitiveUP`.
Cicla `skinmesh1..8` via stand-in. Fallback FVF se o draw shader falhar.

## Fase K (textura WYS + pseffect)

- Decoder `WS10` → DDS → DXT1/3 → RGBA (`wys_decode`)
- Hint MSA (`spark01`) resolve `Effect/spark01.wys`
- `SetTexture` + alpha blend; ciclo `pseffect1..6` no draw

## Fase L (VB/IB GPU)

`CreateVertexBuffer` + `CreateIndexBuffer` + `DrawIndexedPrimitive`
(layout skinmesh1). Fallback `DrawIndexedPrimitiveUP` se a criação falhar.

## Fase M (multi-MSA no campo)

Cena DXVK com vários MSA (`sphere`, `sphere2`, `ankh01`, `arrow`, `FireBall`):
`SceneBegin` → N× `MeshDrawInScene` (offset/scale) → `SceneEnd`.
Textura por mesh; câmera comum ao conjunto.

## Fase N (bone palette / skinning)

- Upload: blend indices por faixa de altura Y (`boneCount` default 4)
- Draw: `SetVertexShaderConstantF(9+3*i, …, 3)` para cada bone
- Força `skinmesh1` + VertexDecl1 quando `bone_count > 1`
- Animação procedural (onda) via `boneTime`; log `bone=1` no fim

## Fase O (bones reais de personagem)

Vertical slice: `mesh/ch0101165.msh` + `ch01.bon` + `ch010101.ani`
(+ textura `ch0101165.wys`).

- Loaders: `msh_loader`, `bone_skin` (hierarquia + clip)
- Palette: `bindPose[i] * combined[boneNames[i]]` → c9+
- Draw com mats externas; log `bone_real=1`

## Fase P (multi-parte)

- Catálogo `mesh/BoneAni4.txt` (`bone_ani_catalog`)
- Default histórico: look **102** (6× skinmesh1)
- Env: `WYD_CHAR_LOOK`, `WYD_CHAR_BONEANI`
- Mesmo `.bon`/`.ani` para todas as partes; log `parts=6 multiparts=1`

## Fase Q (skinmesh2+ + ValidIndex)

- `VertexDecl2..4` + VS `skinmesh2..4` por `influences`
- `.msh` infl 2/3/4 (stride 40/44/48)
- `ValidIndex.bin` → clip `.ani` (`WYD_CHAR_ANI`)
- Default look **1** (mistura infl 2–4); log `skin2=1`

## Fetch (x86_64)

`scripts/fetch-dxvk-native.sh` baixa
`dxvk-native-${VER}-steamrt-sniper.tar.gz` (default **3.0.2**; v3.1 release
ainda sem pacote native no GitHub).

```bash
export WYD_DXVK_NATIVE_VER=3.0.2   # ou 2.7.1
./scripts/fetch-dxvk-native.sh
# → third_party/dxvk-native/
```

## ARM64

Não use o tarball sniper x86_64 no link aarch64. O fluxo oficial:

```bash
./scripts/build-aarch64-docker.sh
# compila DXVK Native no container → third_party/dxvk-native-aarch64/
# (headers reutilizados de third_party/dxvk-native/usr/include/dxvk/)
```

CMake escolhe o prefix pela arch (`WYD_DXVK_NATIVE_ROOT` para override).

## Política

- Source Win32 original intacto.
- Client real (`wyd_client`) **exige** headers + `.so` DXVK Native.
- `third_party/dxvk-native{,-aarch64}/` são artefatos locais (não obrigatórios no git).
- Playbook de port para outra versão: [PORT.md](PORT.md).
