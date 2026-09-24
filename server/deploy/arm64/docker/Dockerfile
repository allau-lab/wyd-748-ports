# WYD-Go ARM64 — runtime a partir do binário já compilado (sem Go no build).
# Contexto: pasta ServidorArm64/ (pai deste docker/).
FROM debian:bookworm-slim
RUN apt-get update \
 && apt-get install -y --no-install-recommends ca-certificates \
 && rm -rf /var/lib/apt/lists/* \
 && useradd --system --uid 1000 --home /opt/wyd --shell /usr/sbin/nologin wyd

WORKDIR /opt/wyd
COPY bin/wydserver bin/account-create /opt/wyd/
# data/ entra via volume no compose (persistência no host)

USER wyd
EXPOSE 8281
STOPSIGNAL SIGTERM
ENTRYPOINT ["/opt/wyd/wydserver"]
CMD ["-config", "data/server.txt", "-addr", "0.0.0.0:8281", "-admin", "127.0.0.1:7480"]
