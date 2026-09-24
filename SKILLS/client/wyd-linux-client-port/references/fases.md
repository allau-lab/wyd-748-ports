# Checklist por fase — port Linux client

Copiar e marcar. Não pular fases.

```text
- [ ] F0 Linka / sobe processo (PCH, CRT, paths)
- [ ] F1 Janela + Clear/Present DXVK (smoke CreateDevice)
- [ ] F2 TextureManager + splash (WYT/WYS carregam)
- [ ] F3 Texto legível (GDI/CP949) + lista de canais
- [ ] F4 Digitação + mouse livre + clique UI
- [ ] F5 Login → selchar → campo (servidor da versão)
- [ ] F6 Áudio básico
- [ ] F7 Touch/LoL (se alvo mobile) com fallthrough correto
- [ ] F8 ARM64 (se pedido): prefix DXVK aarch64; não apagar x86_64
```

## Gates rápidos

| Fase | Comando / evidência |
|---|---|
| F1 | smoke DXVK: CreateDevice + Present; log “Presenter / swapchain” |
| F2 | sem textura null nos IDs do login; sem “DDS parcial” |
| F3 | comparar visualmente com o `.exe` Windows/Wine |
| F5 | personagem no campo; sem MessageBox fatal |
| F8 | `file` nos dois binários; md5 do x86_64 inalterado após cross |

## Proibido em qualquer fase

- Stub que retorna `S_OK` / textura null em feature usada
- Assumir FS case-insensitive
- Assumir que D3DX ignora Width/Height
- Misturar Wine-DXVK com WSI nativo sem testar
- Big-bang misturando loaders de outra versão com assets da alvo
