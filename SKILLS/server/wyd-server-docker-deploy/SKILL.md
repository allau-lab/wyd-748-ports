---
name: wyd-server-docker-deploy
description: >-
  Empacota e sobe o servidor WYD-Go (linux/arm64 ou amd64) via Docker Compose:
  imagem a partir do binário já compilado, volume de data/, porta 8281, grace
  SIGTERM. Usar ao publicar em Orange Pi/VPS, “subir com docker”, rebuild da
  imagem runtime ou depurar container que não escuta / perde o SQLite.
disable-model-invocation: false
---

# Deploy Docker — servidor WYD-Go

Companion de `wyd-server-go-arm64`. Sem paths de máquina específica.

## Princípio

Preferir **imagem runtime** que **copia o binário já buildado** (especialmente
em ARM com pouco disco/CPU). Evitar `golang:` multi-stage no device alvo se o
cross no host já produziu `wydserver` aarch64.

```text
host (cross)  →  bin/wydserver (arch correta)
device        →  docker build (debian-slim + COPY bin) + volume data/
```

## Layout mínimo

```text
<SERVER-TREE>/
  bin/wydserver
  data/                 # bind-mount (persistência)
  logs/
  docker/
    Dockerfile
    docker-compose.yml
  .dockerignore         # logs/*, backup/*, *.db-wal, *.db-shm
```

## Dockerfile (padrão)

```dockerfile
FROM debian:bookworm-slim
RUN apt-get update \
 && apt-get install -y --no-install-recommends ca-certificates \
 && rm -rf /var/lib/apt/lists/* \
 && useradd --system --uid 1000 --home /opt/wyd --shell /usr/sbin/nologin wyd
WORKDIR /opt/wyd
COPY bin/wydserver /opt/wyd/
USER wyd
EXPOSE 8281
STOPSIGNAL SIGTERM
ENTRYPOINT ["/opt/wyd/wydserver"]
CMD ["-config", "data/server.txt", "-addr", "0.0.0.0:8281", "-admin", "127.0.0.1:7480"]
```

Notas:

- UID **1000** alinha com user comum no host (bind mount gravável).
- `data/` **não** vai na imagem — só volume.
- Admin TCP em `127.0.0.1` dentro do container (não publicar 7480 sem necessidade).

## Compose (padrão)

```yaml
services:
  tm:
    build:
      context: ..
      dockerfile: docker/Dockerfile
    image: wyd-server:local
    container_name: wyd-server
    restart: unless-stopped
    user: "1000:1000"
    ports:
      - "8281:8281"
    volumes:
      - ../data:/opt/wyd/data
      - ../logs:/opt/wyd/logs
    working_dir: /opt/wyd
    environment:
      WYD_ADMIN_PASSWORD: "${WYD_ADMIN_PASSWORD:-changeme}"
    stop_grace_period: 40s
    stop_signal: SIGTERM
```

## Pré-flight no `data/server.txt`

| Item | Valor |
|---|---|
| `listen_address` | `0.0.0.0:8281` |
| Mapas | `HeightMap.dat` + `AttributeMap.dat` presentes |
| Paths absolutos de outro host | **remover** |
| SQLite | `database_path=data/wydgo.db` relativo ao cwd `/opt/wyd` |

## Subir

```bash
# no device (já com bin da arch certa)
rsync -az <SERVER-TREE>/ user@host:~/ServidorArm64/   # exemplo de destino
ssh user@host 'cd ~/ServidorArm64 && docker compose -f docker/docker-compose.yml up -d --build'
```

Verificar:

```bash
docker ps --filter name=wyd-server
docker logs --tail 40 <container>
# log bom: "TMSrv escutando em 0.0.0.0:8281"
ss -ltn | grep 8281
```

## Falhas comuns

| Sintoma | Causa | Fix |
|---|---|---|
| Container sobe e morre | path de mapa / `server.txt` | logs; conferir volume `data/` |
| Não conecta de fora | `listen_address` = IP velho / 127.0.0.1 | `0.0.0.0:8281` + publish `8281:8281` |
| DB read-only / panic SQLite | UID do container ≠ dono do volume | `user:` = uid do host; `chown` data |
| Imagem amd64 no Pi | binário errado no `COPY` | `file bin/wydserver` = aarch64 |
| Disco cheio no build Go | multi-stage no Pi | usar Dockerfile de binário pré-compilado |
| Grace kill perde contas | stop curto | `stop_grace_period: 40s` + SIGTERM |

## Critério de aceite

- [ ] Container `Up`; porta 8281 no host
- [ ] Log de boot completo (NPCs/mapas/SQLite)
- [ ] Client autentica contra o IP do device
- [ ] `docker compose restart` persiste personagens
