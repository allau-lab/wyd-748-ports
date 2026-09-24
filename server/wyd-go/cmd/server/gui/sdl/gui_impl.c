/* Implementacao C da janela. Compilado UMA vez (pacote sdl, tag gui). */
#include <stdlib.h>
#include <stdio.h>
#include "_cgo_export.h"
#include "gui_impl.h"

/* Estado do processo: delega ao Go via callback global (SetBridges). */
const char *WydGuiProcState(void) { return WydGuiProcStateC(); }

/* Acao dos botoes Iniciar/Parar/Reiniciar. */
void WydGuiProcAction(int action) { WydGuiProcActionC(action); }

/*
 * wydadmin - GUI de administracao do servidor WYD-Go (SDL3).
 *
 * Conecta no painel TCP admin do servidor (protocolo JSON-lines:
 * {"cmd":"auth","args":{"password":"..."}} etc.) e permite gerenciar
 * status, contas, jogadores online, broadcast, configuracao e o
 * serverlist.bin do client.
 *
 * Design: tema escuro consistente (slate + accent azul), cards por secao,
 * tabs com underline, estados hover/foco em todos os widgets. A fonte e o
 * atlas bitmap embutido (ASCII), entao a hierarquia visual vem de escala,
 * espacamento e cor — nao de acentos.
 *
 * Uso:   ./wydadmin [host[:porta]] [senha]
 *        padrao: 127.0.0.1:7480, senha via campo de login
 */
#include <SDL3/SDL.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>

/* ------------------------------------------------------------------ */
/* Parametros e tema                                                   */
/* ------------------------------------------------------------------ */
#define WIN_W 1120
#define WIN_H 700
#define TEXTBUF 4096

/* Paleta (tema "slate"): uma unica fonte de verdade para toda a UI. */
static const SDL_FColor C_BG       = { 0.055f, 0.075f, 0.118f, 1.0f }; /* fundo      */
static const SDL_FColor C_SURFACE  = { 0.086f, 0.110f, 0.165f, 1.0f }; /* cards      */
static const SDL_FColor C_SURFACE2 = { 0.110f, 0.140f, 0.208f, 1.0f }; /* campos     */
static const SDL_FColor C_INPUTBG  = { 0.067f, 0.086f, 0.129f, 1.0f }; /* editaveis  */
static const SDL_FColor C_BORDER   = { 0.180f, 0.230f, 0.330f, 1.0f };
static const SDL_FColor C_ACCENT   = { 0.290f, 0.500f, 0.840f, 1.0f }; /* azul       */
static const SDL_FColor C_ACCENT_H = { 0.365f, 0.575f, 0.910f, 1.0f }; /* hover      */
static const SDL_FColor C_OK       = { 0.345f, 0.770f, 0.440f, 1.0f }; /* verde      */
static const SDL_FColor C_WARN     = { 0.880f, 0.640f, 0.250f, 1.0f }; /* laranja    */
static const SDL_FColor C_DANGER   = { 0.880f, 0.340f, 0.310f, 1.0f }; /* vermelho   */
static const SDL_FColor C_TEXT     = { 0.910f, 0.930f, 0.960f, 1.0f };
static const SDL_FColor C_TEXT_SUB = { 0.600f, 0.660f, 0.750f, 1.0f };
static const SDL_FColor C_TEXT_DIM = { 0.380f, 0.430f, 0.510f, 1.0f };

static SDL_Window   *g_win  = NULL;
static SDL_Renderer *g_ren = NULL;
static SDL_Texture  *g_font = NULL;
static int g_fw = 8, g_fh = 14;        /* tamanho da celula de glifo */
static int g_atlas_cols = 16;

/* login / sessao */
static char g_host[128] = "127.0.0.1";
static int  g_port      = 7480;
static char g_pw[64] = "";
static bool g_connected = false;
static bool g_authed    = false;
static char g_status[256] = "desconectado";
static char g_lastResult[TEXTBUF] = "";
static bool g_lastError = false;

/* foco de edicao (buffer real resolvido em bufferForFocus):
 * 0=host 1=senha 2=cmd 3=val 4=lvl 5=gold 6=x 7=y 8=motivo 9=dias
 * 10=sl_grupo 11=sl_nome 12=sl_ip */
static int  g_focus = 0;
static char g_cmd[64] = "";
static char g_val[256] = "";
static char g_broadcast[256] = "";
static char g_lvl[16] = "";
static char g_gold[16] = "";
static char g_posx[16] = "";
static char g_posy[16] = "";
static char g_motivo[256] = "";
static char g_dias[16] = "";
static char g_slGroup[32] = "0";
static char g_slName[64] = "";
static char g_slIP[64] = "";

/* aba Lista: dados decodificados de serverlist.get */
typedef struct { char name[64]; char ip0[64]; int count; } SLRow;
static SLRow g_slRows[10];
static int   g_slCount = 0;
static char  g_slPath[256] = "";

/* estatisticas ao vivo (resposta de server.status) */
static int    g_stOnline = 0, g_stInWorld = 0, g_stQueue = 0;
static long   g_stAccounts = 0;
static double g_stMem = 0;
static long   g_stUptime = 0, g_stStarted = 0;
static Uint64 g_lastPoll = 0;

static int  g_tab = 0;
static const char *g_tabNames[] = { "Status", "Contas", "Personagens", "Online", "Lista", "Config", "Comandos" };
#define NTABS 7

static bool g_quit = false;
static Uint64 g_lastClick = 0;

/* ------------------------------------------------------------------ */
/* Fonte bitmap embutida (atlas 16x6, ASCII 32..127)                   */
/* ------------------------------------------------------------------ */
#include "font_atlas.h"

/* ------------------------------------------------------------------ */
/* Texto                                                               */
/* ------------------------------------------------------------------ */
static void drawText(int x, int y, const char *s, SDL_FColor col) {
    SDL_SetRenderDrawColorFloat(g_ren, col.r, col.g, col.b, col.a);
    for (const unsigned char *p = (const unsigned char *)s; *p; p++) {
        unsigned int c = *p;
        if (c < 32 || c > 127) c = '?';
        int idx = c - 32;
        SDL_FRect src = { (float)((idx % g_atlas_cols) * g_fw),
                          (float)((idx / g_atlas_cols) * g_fh),
                          (float)g_fw, (float)g_fh };
        SDL_FRect dst = { (float)x, (float)y, (float)g_fw, (float)g_fh };
        SDL_RenderTexture(g_ren, g_font, &src, &dst);
        x += g_fw;
    }
}

/* Titulos: glifos em escala inteira (crisp com NEAREST). */
static void drawTextScaled(int x, int y, const char *s, SDL_FColor col, int scale) {
    SDL_SetRenderDrawColorFloat(g_ren, col.r, col.g, col.b, col.a);
    for (const unsigned char *p = (const unsigned char *)s; *p; p++) {
        unsigned int c = *p;
        if (c < 32 || c > 127) c = '?';
        int idx = c - 32;
        SDL_FRect src = { (float)((idx % g_atlas_cols) * g_fw),
                          (float)((idx / g_atlas_cols) * g_fh),
                          (float)g_fw, (float)g_fh };
        SDL_FRect dst = { (float)x, (float)y, (float)(g_fw * scale), (float)(g_fh * scale) };
        SDL_RenderTexture(g_ren, g_font, &src, &dst);
        x += g_fw * scale;
    }
}

static int textW(const char *s) { return (int)strlen(s) * g_fw; }
static int textWScaled(const char *s, int scale) { return (int)strlen(s) * g_fw * scale; }

static void drawTextCentered(int y, const char *s, SDL_FColor col) {
    drawText((WIN_W - textW(s)) / 2, y, s, col);
}

/* ------------------------------------------------------------------ */
/* Widgets                                                             */
/* ------------------------------------------------------------------ */
typedef struct { float x, y, w, h; } Rect;

static Rect rc(float x, float y, float w, float h);

static bool inRect(Rect r, float mx, float my) {
    return mx >= r.x && mx < r.x + r.w && my >= r.y && my < r.y + r.h;
}

static void fillRect(Rect r, SDL_FColor col) {
    SDL_SetRenderDrawColorFloat(g_ren, col.r, col.g, col.b, col.a);
    SDL_FRect f = { r.x, r.y, r.w, r.h };
    SDL_RenderFillRect(g_ren, &f);
}

static void strokeRect(Rect r, SDL_FColor col) {
    SDL_SetRenderDrawColorFloat(g_ren, col.r, col.g, col.b, col.a);
    SDL_FRect f = { r.x, r.y, r.w, r.h };
    SDL_RenderRect(g_ren, &f);
}

/* Card: superficie com titulo em caps pequenas + filete accent. */
static void drawCard(Rect r, const char *title) {
    fillRect(r, C_SURFACE);
    strokeRect(r, C_BORDER);
    if (title && title[0]) {
        drawText((int)r.x + 12, (int)r.y + 10, title, C_TEXT_SUB);
        Rect bar = { r.x + 12, r.y + 10 + g_fh + 3, (float)textW(title), 2 };
        fillRect(bar, C_ACCENT);
    }
}

/* Indicador circular (quadrado arredondado pela celula): estado pontual. */
static void drawDot(int x, int y, SDL_FColor col) {
    SDL_SetRenderDrawColorFloat(g_ren, col.r, col.g, col.b, col.a);
    SDL_FRect d = { (float)x, (float)y + 3, 8, 8 };
    SDL_RenderFillRect(g_ren, &d);
    d = (SDL_FRect){ (float)x + 1, (float)y + 2, 6, 10 };
    SDL_RenderFillRect(g_ren, &d);
}

static void drawButton(Rect r, const char *label, bool hot, bool danger) {
    /* sombra sutil */
    fillRect(rc(r.x, r.y + 1, r.w, r.h), (SDL_FColor){ 0.02f, 0.03f, 0.05f, 0.6f });
    SDL_FColor fill = hot
        ? (danger ? (SDL_FColor){ 0.62f, 0.24f, 0.22f, 1.0f } : (SDL_FColor){ 0.22f, 0.36f, 0.60f, 1.0f })
        : (danger ? (SDL_FColor){ 0.26f, 0.14f, 0.14f, 1.0f } : C_SURFACE2);
    fillRect(r, fill);
    strokeRect(r, hot ? C_ACCENT_H : (danger ? C_DANGER : C_BORDER));
    int tw = textW(label);
    drawText((int)(r.x + (r.w - tw) / 2), (int)(r.y + (r.h - g_fh) / 2), label,
             hot ? C_TEXT : (danger ? (SDL_FColor){ 0.95f, 0.62f, 0.58f, 1.0f } : C_TEXT));
}

/* Campo editavel com rotulo acima, caret piscante e borda de foco. */
static void drawField(Rect r, const char *label, const char *value,
                      bool masked, bool focused) {
    fillRect(r, C_INPUTBG);
    strokeRect(r, focused ? C_ACCENT : C_BORDER);
    char shown[300];
    if (masked) {
        size_t n = strlen(value);
        if (n > 40) n = 40;
        memset(shown, '*', n);
        shown[n] = 0;
    } else {
        snprintf(shown, sizeof shown, "%s", value);
    }
    int tx = (int)r.x + 8;
    int ty = (int)(r.y + (r.h - g_fh) / 2);
    drawText(tx, ty, shown[0] ? shown : "", C_TEXT);
    if (focused) {
        if ((SDL_GetTicks() / 500) % 2 == 0) {
            Rect caret = { (float)tx + textW(shown) + 1, (float)ty + 1, 1, (float)g_fh - 2 };
            fillRect(caret, C_ACCENT_H);
        }
    }
    if (label && label[0]) {
        drawText((int)r.x, (int)r.y - g_fh - 3, label, C_TEXT_SUB);
    }
}

/* Caixa informativa (nao editavel): caminhos, valores somente leitura. */
static void drawInfo(Rect r, const char *label, const char *value) {
    fillRect(r, C_SURFACE2);
    strokeRect(r, C_BORDER);
    drawText((int)r.x + 8, (int)(r.y + (r.h - g_fh) / 2), value, C_TEXT_SUB);
    if (label && label[0]) {
        drawText((int)r.x, (int)r.y - g_fh - 3, label, C_TEXT_SUB);
    }
}

static Rect rc(float x, float y, float w, float h) {
    Rect r = { x, y, w, h };
    return r;
}

/* ------------------------------------------------------------------ */
/* Rede (TCP + JSON-lines simples, sem dependencias)                   */
/* ------------------------------------------------------------------ */
static int  g_sock = -1;
static char g_rbuf[TEXTBUF];
static int  g_rlen = 0;
static int  g_reqId = 1;

static void netCloseReal(void) {
    if (g_sock >= 0) { close(g_sock); g_sock = -1; }
    g_connected = false;
    g_authed = false;
    snprintf(g_status, sizeof g_status, "desconectado");
}

static bool netConnect(const char *host, int port, char *err, size_t errsz) {
    netCloseReal();
    int s = (int)socket(AF_INET, SOCK_STREAM, 0);
    if (s < 0) { snprintf(err, errsz, "socket falhou"); return false; }
    struct sockaddr_in sa;
    memset(&sa, 0, sizeof sa);
    sa.sin_family = AF_INET;
    sa.sin_port = htons((unsigned short)port);
    if (inet_pton(AF_INET, host, &sa.sin_addr) != 1) {
        snprintf(err, errsz, "host invalido (use IP)");
        close(s);
        return false;
    }
    /* NAO-BLOQUEANTE desde o nascimento: connect e read/write nunca travam a
     * GUI. connect em andamento (EINPROGRESS) e resolvido com poll de 2s. */
    int fl = fcntl(s, F_GETFL, 0);
    fcntl(s, F_SETFL, fl | O_NONBLOCK);
    int cr = connect(s, (struct sockaddr *)&sa, sizeof sa);
    if (cr != 0 && errno != EINPROGRESS) {
        snprintf(err, errsz, "conectou falhou em %s:%d", host, port);
        close(s);
        return false;
    }
    if (cr != 0) {
        struct pollfd p = { .fd = s, .events = POLLOUT, .revents = 0 };
        int pr = poll(&p, 1, 2000);
        if (pr <= 0) {
            snprintf(err, errsz, "sem resposta de %s:%d (timeout 2s)", host, port);
            close(s);
            return false;
        }
        int soerr = 0;
        socklen_t slen = sizeof soerr;
        getsockopt(s, SOL_SOCKET, SO_ERROR, &soerr, &slen);
        if (soerr != 0) {
            snprintf(err, errsz, "conectou falhou em %s:%d", host, port);
            close(s);
            return false;
        }
    }
    g_sock = s;
    g_rlen = 0;
    g_connected = true;
    snprintf(g_status, sizeof g_status, "conectado a %s:%d", host, port);
    return true;
}

static bool netSendLine(const char *line) {
    if (g_sock < 0) return false;
    size_t len = strlen(line);
    ssize_t w = write(g_sock, line, len);
    if (w != (ssize_t)len) return false;
    return write(g_sock, "\n", 1) == 1;
}

/* Processa linhas recebidas; devolve a ultima linha completa (ou NULL).
 * NAO BLOQUEIA: socket em modo nao-bloqueante; EAGAIN apenas encerra a
 * varredura. O loop da GUI chama isto a cada frame — travar aqui congela a
 * janela inteira. */
static const char *netPoll(void) {
    static char line[TEXTBUF];
    if (g_sock < 0) return NULL;
    /* 1) drena linhas completas ja no buffer. */
    if (g_rlen > 0) {
        char *nl = memchr(g_rbuf, '\n', (size_t)g_rlen);
        if (nl) {
            size_t n = (size_t)(nl - g_rbuf);
            if (n >= sizeof line) n = sizeof line - 1;
            memcpy(line, g_rbuf, n);
            line[n] = 0;
            memmove(g_rbuf, nl + 1, (size_t)(g_rlen - (int)n - 1));
            g_rlen -= (int)n + 1;
            return line;
        }
    }
    /* 2) le o que houver sem bloquear (uma chamada por frame). */
    if (g_rlen >= (int)sizeof g_rbuf - 1) g_rlen = 0; /* overflow: descarta */
    int r = (int)read(g_sock, g_rbuf + g_rlen, sizeof g_rbuf - 1 - (size_t)g_rlen);
    if (r > 0) {
        g_rlen += r;
        char *nl = memchr(g_rbuf, '\n', (size_t)g_rlen);
        if (nl) {
            size_t n = (size_t)(nl - g_rbuf);
            if (n >= sizeof line) n = sizeof line - 1;
            memcpy(line, g_rbuf, n);
            line[n] = 0;
            memmove(g_rbuf, nl + 1, (size_t)(g_rlen - (int)n - 1));
            g_rlen -= (int)n + 1;
            return line;
        }
    } else if (r == 0) {
        netCloseReal(); /* servidor fechou */
    }
    /* r < 0 com EAGAIN = nada novo agora: segue o jogo. */
    return NULL;
}

/* Envia comando JSON e marca resposta pendente. */
static char g_pending[256];
static bool g_hasPending = false;

static void sendCmd(const char *cmd, const char *argsJson) {
    char line[1024];
    if (argsJson && argsJson[0]) {
        snprintf(line, sizeof line, "{\"id\":%d,\"cmd\":\"%s\",\"args\":%s}",
                 g_reqId++, cmd, argsJson);
    } else {
        snprintf(line, sizeof line, "{\"id\":%d,\"cmd\":\"%s\"}", g_reqId++, cmd);
    }
    if (netSendLine(line)) {
        snprintf(g_pending, sizeof g_pending, "%s", cmd);
        g_hasPending = true;
    }
}

/* ------------------------------------------------------------------ */
/* Util                                                                */
/* ------------------------------------------------------------------ */
static void escKey(const char *s, char *out, size_t outsz) {
    size_t j = 0;
    for (size_t i = 0; s[i] && j + 2 < outsz; i++) {
        if (s[i] == '"' || s[i] == '\\') out[j++] = '\\';
        out[j++] = s[i];
    }
    out[j] = 0;
}

/* Extracao minimalista de campos JSON ("key":value) da resposta. */
static bool jsonFind(const char *json, const char *key, char *out, size_t outsz) {
    char pat[64];
    snprintf(pat, sizeof pat, "\"%s\":", key);
    const char *p = strstr(json, pat);
    if (!p) return false;
    p += strlen(pat);
    while (*p == ' ') p++;
    if (*p == '"') {
        p++;
        size_t j = 0;
        while (*p && *p != '"' && j + 1 < outsz) {
            if (*p == '\\' && p[1]) p++;
            out[j++] = *p++;
        }
        out[j] = 0;
        return true;
    }
    /* numero/objeto: copia bruta ate , ou } */
    size_t j = 0;
    while (*p && *p != ',' && *p != '}' && j + 1 < outsz) out[j++] = *p++;
    out[j] = 0;
    return true;
}

/* Extrai a lista de grupos da resposta serverlist.get:
 * {"path":"...","groups":[{"name":"N","servers":["a","b"]},...]}. */
static void parseServerList(const char *json) {
    g_slCount = 0;
    g_slPath[0] = 0;
    jsonFind(json, "path", g_slPath, sizeof g_slPath);
    const char *p = strstr(json, "\"groups\"");
    if (!p) return;
    p = strchr(p, '[');
    if (!p) return;
    while (g_slCount < 10) {
        const char *nx = strstr(p, "\"name\"");
        if (!nx) break;
        const char *sv = strstr(p, "\"servers\"");
        /* nome do grupo */
        char name[64] = "";
        const char *q = nx + 6;
        while (*q == ' ') q++;
        if (*q == '"') {
            q++;
            size_t j = 0;
            while (*q && *q != '"' && j + 1 < sizeof name) {
                if (*q == '\\' && q[1]) q++;
                name[j++] = *q++;
            }
        }
        SLRow *row = &g_slRows[g_slCount];
        memset(row, 0, sizeof *row);
        snprintf(row->name, sizeof row->name, "%s", name);
        if (sv && (!nx || sv < nx + 6)) {
            const char *b = strchr(sv, '[');
            if (b) {
                b++;
                while (*b && *b != ']') {
                    if (*b == '"') {
                        b++;
                        char ip[64] = "";
                        size_t j = 0;
                        while (*b && *b != '"' && j + 1 < sizeof ip) {
                            if (*b == '\\' && b[1]) b++;
                            ip[j++] = *b++;
                        }
                        if (row->count == 0) snprintf(row->ip0, sizeof row->ip0, "%s", ip);
                        row->count++;
                        if (*b == '"') b++;
                    } else {
                        b++;
                    }
                }
            }
        }
        g_slCount++;
        p = nx + 6;
    }
}

/* ------------------------------------------------------------------ */
/* Acoes da UI                                                         */
/* ------------------------------------------------------------------ */
static void doConnect(void) {
    char err[128];
    if (!netConnect(g_host, g_port, err, sizeof err)) {
        snprintf(g_status, sizeof g_status, "erro: %s", err);
        return;
    }
    char pw[140];
    escKey(g_pw, pw, sizeof pw);
    char args[192];
    snprintf(args, sizeof args, "{\"password\":\"%s\"}", pw);
    sendCmd("auth", args);
    snprintf(g_status, sizeof g_status, "autenticando...");
}

static void tabAction(int t);
static bool drawConfirmModal(float mx, float my, bool mdown);
static int drawTabBody(float mx, float my, bool click);

/* ------------------------------------------------------------------ */
/* Foco: buffer ativo por aba (evita campos disputando o mesmo foco)   */
/* ------------------------------------------------------------------ */
static char *bufferForFocus(size_t *maxlen) {
    static char *map[] = { g_host, g_pw, g_cmd, g_val, g_lvl, g_gold,
                           g_posx, g_posy, g_motivo, g_dias,
                           g_slGroup, g_slName, g_slIP };
    static size_t lens[] = { 128, 64, 64, 256, 16, 16, 16, 16, 256, 16, 32, 64, 64 };
    int f = g_focus;
    if (f < 0) f = 0;
    if (f > 12) f = 12;
    if (maxlen) *maxlen = lens[f];
    return map[f];
}

/* Ordem de tabulacao por aba (o TAB cicla dentro da aba ativa). */
static const int *tabOrder(int tab, int *n) {
    static const int login[]  = { 0, 1 };
    static const int status[] = { 2 };
    static const int acc[]    = { 2, 3, 8, 9 };
    static const int chars[]  = { 2, 3, 4, 5, 6, 7 };
    static const int online[] = { 2 };
    static const int list[]   = { 10, 11, 12 };
    static const int conf[]   = { 2, 3 };
    static const int cmds[]   = { 2 };
    switch (tab) {
    case 0:  *n = 1; return status;
    case 1:  *n = 4; return acc;
    case 2:  *n = 6; return chars;
    case 3:  *n = 1; return online;
    case 4:  *n = 3; return list;
    case 5:  *n = 2; return conf;
    case 6:  *n = 1; return cmds;
    default: *n = 2; return login;
    }
}

static void focusNext(void) {
    int n = 0;
    const int *ord = tabOrder(g_authed ? g_tab : -1, &n);
    for (int i = 0; i < n; i++) {
        if (ord[i] == g_focus) {
            g_focus = ord[(i + 1) % n];
            return;
        }
    }
    g_focus = ord[0];
}

/* ------------------------------------------------------------------ */
/* Layout principal                                                    */
/* ------------------------------------------------------------------ */
#define HEADER_H 62
#define TABS_Y   70
#define TABS_H   32
#define BODY_Y   116
#define LOG_H    140
#define LOG_Y    (WIN_H - LOG_H - 34)
#define FOOT_Y   (WIN_H - 28)

/* widgets da aba ativa; click devolve indice de botao acionado (-1 nenhum) */

static void frame(float mx, float my, bool mdown) {
    SDL_SetRenderDrawColorFloat(g_ren, C_BG.r, C_BG.g, C_BG.b, C_BG.a);
    SDL_RenderClear(g_ren);

    /* ------------------------- HEADER ------------------------- */
    Rect accentBar = { 0, 0, WIN_W, 3 };
    fillRect(accentBar, C_ACCENT);
    drawTextScaled(14, 14, "WYD-Go Admin", C_ACCENT, 2);
    drawText(16 + textWScaled("WYD-Go Admin", 2) + 18, 22, "Servidor 7.48", C_TEXT_SUB);
    /* pill de estado a direita */
    {
        const char *authTxt = g_authed ? "AUTENTICADO" : (g_connected ? "conectado" : "desconectado");
        SDL_FColor dot = g_authed ? C_OK : (g_connected ? C_WARN : C_DANGER);
        int w = textW(authTxt) + 44;
        Rect pill = { WIN_W - w - 14, 16, (float)w, 30 };
        fillRect(pill, C_SURFACE2);
        strokeRect(pill, C_BORDER);
        drawDot((int)pill.x + 10, (int)pill.y + 7, dot);
        drawText((int)pill.x + 26, (int)pill.y + 8, authTxt, g_authed ? C_OK : C_TEXT_SUB);
        drawText(WIN_W - 14 - textW(g_status), 50, g_status, C_TEXT_DIM);
    }
    Rect hr = { 0, HEADER_H, WIN_W, 1 };
    fillRect(hr, C_BORDER);

    /* ------------------------- LOGIN -------------------------- */
    if (!g_authed) {
        int cw = 420, cx = (WIN_W - cw) / 2, cy = 180;
        drawCard(rc((float)cx - 24, (float)cy - 24, (float)cw + 48, 250), NULL);
        drawTextScaled(cx, cy, "Painel de Administracao", C_TEXT, 2);
        drawText(cx, cy + 2 * g_fh + 8, "WYD-Go Servidor 7.48", C_TEXT_DIM);

        Rect fHost = rc((float)cx, (float)cy + 70, (float)cw, 28);
        Rect fPw   = rc((float)cx, (float)cy + 128, (float)cw, 28);
        Rect bGo   = rc((float)cx, (float)cy + 176, (float)cw, 32);
        drawField(fHost, "Servidor (IP:porta)", g_host, false, g_focus == 0);
        drawField(fPw, "Senha do painel", g_pw, true, g_focus == 1);
        bool hotGo = inRect(bGo, mx, my);
        drawButton(bGo, "Conectar", hotGo, false);
        if (mdown) {
            if (inRect(fHost, mx, my)) g_focus = 0;
            else if (inRect(fPw, mx, my)) g_focus = 1;
            else if (hotGo && SDL_GetTicks() - g_lastClick > 200) {
                g_lastClick = SDL_GetTicks();
                doConnect();
            }
        }
        if (g_lastResult[0]) {
            drawText((WIN_W - textW(g_lastResult)) / 2, cy + 232, g_lastResult,
                     g_lastError ? C_DANGER : C_OK);
        }
        drawText(14, FOOT_Y + 7,
                 "sem usuario: apenas a senha. Padrao: admin748 (server.txt admin_password / WYD_ADMIN_PASSWORD)",
                 C_TEXT_DIM);
        drawConfirmModal(mx, my, mdown);
        return;
    }

    /* ------------------------- TABS --------------------------- */
    {
        float tw = (WIN_W - 16.0f) / NTABS;
        float tx = 8;
        for (int t = 0; t < NTABS; t++) {
            Rect r = rc(tx, TABS_Y, tw - 8, TABS_H);
            bool hot = inRect(r, mx, my);
            fillRect(r, g_tab == t ? C_SURFACE2 : C_SURFACE);
            strokeRect(r, g_tab == t ? C_ACCENT : C_BORDER);
            if (g_tab == t) {
                Rect under = { r.x + 8, r.y + r.h - 3, r.w - 16, 3 };
                fillRect(under, C_ACCENT);
            } else if (hot) {
                Rect under = { r.x + 8, r.y + r.h - 3, r.w - 16, 3 };
                fillRect(under, C_BORDER);
            }
            int w = textW(g_tabNames[t]);
            drawText((int)(r.x + (r.w - w) / 2), (int)(r.y + (TABS_H - g_fh) / 2),
                     g_tabNames[t], g_tab == t ? C_TEXT : C_TEXT_SUB);
            if (mdown && hot && g_tab != t) {
                g_tab = t;
                int n = 0;
                const int *ord = tabOrder(g_tab, &n);
                g_focus = ord[0];
            }
            tx += tw;
        }
        Rect hr2 = { 0, TABS_Y + TABS_H, WIN_W, 1 };
        fillRect(hr2, C_BORDER);
    }

    /* ------------------- LOG (saida do comando) ---------------- */
    /* Na aba Status o corpo desenha o proprio console (mockup). */
    if (!(g_authed && g_tab == 0)) {
        Rect logR = rc(8, LOG_Y, WIN_W - 16, LOG_H);
        drawCard(logR, g_hasPending ? "Console Log — aguardando servidor..." : "Console Log");
        const char *p = g_lastResult;
        int y = (int)logR.y + 26;
        char buf[128];
        int maxcols = (int)(logR.w - 24) / g_fw;
        if (maxcols > 126) maxcols = 126;
        SDL_FColor col = g_lastError ? (SDL_FColor){ 0.95f, 0.55f, 0.50f, 1.0f }
                                     : (SDL_FColor){ 0.55f, 0.85f, 0.60f, 1.0f };
        while (*p && y < (int)(logR.y + logR.h - g_fh - 4)) {
            size_t n = strlen(p);
            if (n > (size_t)maxcols) n = (size_t)maxcols;
            memcpy(buf, p, n);
            buf[n] = 0;
            drawText((int)logR.x + 12, y, buf, col);
            p += n;
            y += g_fh + 2;
        }
        if (!g_lastResult[0] && !g_hasPending) {
            drawText((int)logR.x + 12, y, "(sem saida ainda — use os botoes acima)", C_TEXT_DIM);
        }
    }

    /* ------------------------- CORPO --------------------------- */
    int action = drawTabBody(mx, my, mdown);
    if (mdown && action >= 0) tabAction(action);

    /* ------------------------- FOOTER -------------------------- */
    Rect foot = { 0, FOOT_Y, WIN_W, 28 };
    fillRect(foot, C_SURFACE);
    Rect hr3 = { 0, FOOT_Y, WIN_W, 1 };
    fillRect(hr3, C_BORDER);
    drawText(14, FOOT_Y + 7, g_status, C_TEXT_DIM);
    if (g_hasPending) {
        const char *w = "aguardando resposta...";
        drawText(WIN_W - 14 - textW(w), FOOT_Y + 7, w, C_WARN);
    }

    /* modal de confirmacao por cima de tudo (consumiu o clique/teclado). */
    drawConfirmModal(mx, my, mdown);
}

/* ------------------------------------------------------------------ */
/* Abas                                                                */
/* ------------------------------------------------------------------ */
/* ---- dialogo de confirmacao (modal) ----
 * confirmAction: 0 nenhum, 71 parar, 72 reiniciar. O modal captura mouse e
 * teclado; so Confirmar dispara tabAction(). */
static int  g_confirmAction = 0;
static const char *g_confirmText = "";

static void askConfirm(int action) {
    g_confirmAction = action;
    g_confirmText = (action == 71) ? "Encerrar o servidor? Jogadores serao desconectados e o estado sera persistido."
                                   : "Reiniciar o servidor? Breve indisponibilidade; o estado sera persistido.";
}

static Rect bConfYes = { WIN_W/2 - 220, WIN_H/2 + 34, 200, 34 };
static Rect bConfNo  = { WIN_W/2 + 20,  WIN_H/2 + 34, 200, 34 };

/* Desenha o modal; devolve true se ele consumiu o clique. */
static bool drawConfirmModal(float mx, float my, bool mdown) {
    if (!g_confirmAction) return false;
    fillRect(rc(0, 0, WIN_W, WIN_H), (SDL_FColor){ 0, 0, 0, 0.62f });

    Rect box = { WIN_W/2 - 250, WIN_H/2 - 90, 500, 160 };
    fillRect(rc(box.x + 2, box.y + 4, box.w, box.h), (SDL_FColor){ 0, 0, 0, 0.45f }); /* sombra */
    fillRect(box, C_SURFACE);
    strokeRect(box, C_ACCENT);
    drawTextCentered((int)box.y + 16, "Confirmar acao", C_TEXT);
    Rect under = { box.x + (box.w - textW("Confirmar acao")) / 2, box.y + 16 + g_fh + 3,
                   (float)textW("Confirmar acao"), 2 };
    fillRect(under, C_ACCENT);
    /* wrap do texto em ate 3 linhas de 56 colunas */
    {
        const char *p = g_confirmText;
        char buf[64];
        int y = (int)box.y + 48;
        for (int line = 0; line < 3 && *p; line++) {
            size_t n = strlen(p);
            if (n > 56) n = 56;
            memcpy(buf, p, n);
            buf[n] = 0;
            drawTextCentered(y, buf, C_TEXT_SUB);
            p += n;
            y += g_fh + 3;
        }
    }
    bool hotYes = inRect(bConfYes, mx, my);
    bool hotNo  = inRect(bConfNo, mx, my);
    drawButton(bConfYes, "Confirmar", hotYes, true);
    drawButton(bConfNo, "Cancelar", hotNo, false);
    if (mdown) {
        if (hotYes) {
            int a = g_confirmAction;
            g_confirmAction = 0;
            tabAction(a);
        } else if (hotNo) {
            g_confirmAction = 0;
        }
        return true; /* modal consome qualquer clique */
    }
    return true; /* e o teclado tambem */
}

/* ---- geometria compartilhada das abas ---- */
#define COL1 12.0f
#define CARD_W_L 520.0f
#define CARD_W_R (WIN_W - CARD_W_L - 24.0f)
#define CARD_X_R (12.0f + CARD_W_L + 12.0f)

static int drawTabBody(float mx, float my, bool click) {
    Rect b;
    switch (g_tab) {
    case 0: { /* ---- Status (mockup: Server Control + stats + Aviso + Console) ---- */
        /* ---------- coluna esquerda: Server Control ---------- */
        Rect cardL = rc(COL1, BODY_Y, CARD_W_L, 318);
        drawCard(cardL, "Server Control");
        {
            extern const char *WydGuiProcState(void);
            const char *st = WydGuiProcState();
            bool running = strstr(st, "rodando") != NULL;
            /* pill ONLINE/PARADO no titulo */
            {
                const char *pt = running ? "ONLINE" : "PARADO";
                Rect pill = { cardL.x + cardL.w - 96, cardL.y + 8, 84, 24 };
                fillRect(pill, running ? C_OK : C_WARN);
                int tw2 = textW(pt);
                drawText((int)(pill.x + (pill.w - tw2) / 2), (int)pill.y + 5, pt,
                         (SDL_FColor){ 0.04f, 0.07f, 0.05f, 1.0f });
            }
            /* linha de status grande */
            drawDot((int)cardL.x + 14, (int)cardL.y + 46, running ? C_OK : C_WARN);
            drawText((int)cardL.x + 30, (int)cardL.y + 44, st, running ? C_OK : C_WARN);

            /* botoes grandes (2 fileiras, como no mockup) */
            Rect bStart = rc(cardL.x + 14, cardL.y + 78, 156, 34);
            Rect bStop = rc(cardL.x + 182, cardL.y + 78, 156, 34);
            Rect bRestart = rc(cardL.x + 350, cardL.y + 78, 156, 34);
            drawButton(bStart, "Iniciar", inRect(bStart, mx, my), false);
            drawButton(bStop, "Parar", inRect(bStop, mx, my), true);
            drawButton(bRestart, "Reiniciar", inRect(bRestart, mx, my), true);
            if (click && inRect(bStart, mx, my)) return 70;
            if (click && inRect(bStop, mx, my)) { askConfirm(71); return -1; }
            if (click && inRect(bRestart, mx, my)) { askConfirm(72); return -1; }

            Rect bSave = rc(cardL.x + 14, cardL.y + 122, 156, 34);
            Rect bUpd = rc(cardL.x + 182, cardL.y + 122, 156, 34);
            Rect bQuit = rc(cardL.x + 350, cardL.y + 122, 156, 34);
            drawButton(bSave, "Salvar", inRect(bSave, mx, my), false);
            drawButton(bUpd, "Atualizar", inRect(bUpd, mx, my), false);
            drawButton(bQuit, "Encerrar", inRect(bQuit, mx, my), true);
            if (click && inRect(bSave, mx, my)) return 1;
            if (click && inRect(bUpd, mx, my)) return 0;
            if (click && inRect(bQuit, mx, my)) { askConfirm(71); return -1; }

            /* grade de estatisticas 2x4 (como no mockup) */
            char v1[32], v2[32], v3[32], v4[32], v5[32], v6[32], v7[32], v8[8];
            unsigned up = (unsigned)(g_stUptime < 0 ? 0 : g_stUptime);
            if (up < 60) snprintf(v4, sizeof v4, "%us", up);
            else if (up < 3600) snprintf(v4, sizeof v4, "%um", up / 60);
            else snprintf(v4, sizeof v4, "%uh", up / 3600);
            snprintf(v1, sizeof v1, "%d", g_stOnline);
            snprintf(v2, sizeof v2, "%d", (int)g_stAccounts);
            snprintf(v3, sizeof v3, "%.0fMB", g_stMem);
            snprintf(v5, sizeof v5, "%d", g_stInWorld);
            snprintf(v6, sizeof v6, "%d", g_stQueue);
            if (g_stStarted > 0) {
                time_t tt = (time_t)g_stStarted;
                struct tm tmv;
                localtime_r(&tt, &tmv);
                strftime(v7, sizeof v7, "%H:%M", &tmv);
            } else {
                snprintf(v7, sizeof v7, "--:--");
            }
            snprintf(v8, sizeof v8, "8281");
            const char *labels[8] = { "ONLINE", "CONTAS", "MEMORIA", "UPTIME",
                                      "NO MUNDO", "FILA", "INICIO", "PORTA" };
            const char *vals[8] = { v1, v2, v3, v4, v5, v6, v7, v8 };
            float cw2 = (CARD_W_L - 28.0f - 24.0f) / 4.0f;
            for (int i = 0; i < 8; i++) {
                int col = i % 4, row = i / 4;
                Rect cell = rc(cardL.x + 14 + col * (cw2 + 8), cardL.y + 180 + row * 62, cw2, 54);
                fillRect(cell, C_INPUTBG);
                strokeRect(cell, C_BORDER);
                drawText((int)cell.x + 8, (int)cell.y + 6, labels[i], C_TEXT_DIM);
                drawText((int)cell.x + 8, (int)cell.y + 24, vals[i], C_TEXT);
            }
        }
        /* ---------- coluna direita: Aviso Global ---------- */
        Rect cardR = rc(CARD_X_R, BODY_Y, CARD_W_R, 170);
        drawCard(cardR, "Aviso Global");
        b = rc(cardR.x + 14, cardR.y + 56, CARD_W_R - 122, 28);
        drawField(b, NULL, g_broadcast, false, g_focus == 2);
        Rect bSend = rc(b.x + b.w + 10, b.y - 2, 98, 32);
        drawButton(bSend, "Enviar", inRect(bSend, mx, my), false);
        if (click) {
            if (inRect(b, mx, my)) g_focus = 2;
            if (inRect(bSend, mx, my)) return 10;
        }
        drawText((int)cardR.x + 14, (int)cardR.y + 104,
                 "abre uma janela de sistema no client de cada jogador online.",
                 C_TEXT_DIM);

        /* ---------- coluna direita: Ultimos avisos ---------- */
        Rect cardW = rc(CARD_X_R, BODY_Y + 182, CARD_W_R, 136);
        drawCard(cardW, "Ultimos avisos");
        {
            char l1[128], l2[128];
            snprintf(l1, sizeof l1, "aviso: %s",
                     g_broadcast[0] ? g_broadcast : "(nenhum enviado nesta sessao)");
            snprintf(l2, sizeof l2, "%s", g_lastResult[0] ? g_lastResult : "servidor pronto");
            size_t nn = strcspn(l2, "\r\n");
            if (nn < sizeof l2) l2[nn] = 0;
            if (strlen(l1) > 62) l1[62] = 0;
            if (strlen(l2) > 62) l2[62] = 0;
            Rect bar1 = { cardW.x + 12, cardW.y + 32, 3, (float)g_fh };
            Rect bar2 = { cardW.x + 12, cardW.y + 56, 3, (float)g_fh };
            fillRect(bar1, C_ACCENT);
            fillRect(bar2, C_ACCENT);
            drawText((int)cardW.x + 22, (int)bar1.y, l1, C_TEXT_SUB);
            drawText((int)cardW.x + 22, (int)bar2.y, l2, C_TEXT_SUB);
        }

        /* ---------- Console Log (tela cheia, como no mockup) ---------- */
        Rect logC = rc(COL1, BODY_Y + 330, WIN_W - 24, FOOT_Y - (BODY_Y + 330) - 10);
        drawCard(logC, g_hasPending ? "Console Log - aguardando servidor..." : "Console Log");
        {
            const char *p = g_lastResult;
            int y = (int)logC.y + 26;
            char buf[128];
            int maxcols = (int)(logC.w - 24) / g_fw;
            if (maxcols > 126) maxcols = 126;
            SDL_FColor col = g_lastError ? (SDL_FColor){ 0.95f, 0.55f, 0.50f, 1.0f }
                                         : (SDL_FColor){ 0.55f, 0.85f, 0.60f, 1.0f };
            while (*p && y < (int)(logC.y + logC.h - g_fh - 4)) {
                size_t n = strlen(p);
                if (n > (size_t)maxcols) n = (size_t)maxcols;
                memcpy(buf, p, n);
                buf[n] = 0;
                drawText((int)logC.x + 12, y, buf, col);
                p += n;
                y += g_fh + 2;
            }
            if (!g_lastResult[0] && !g_hasPending)
                drawText((int)logC.x + 12, y, "(sem saida ainda)", C_TEXT_DIM);
        }
        return -1;
    }

    case 1: { /* ---- Contas ---- */
        Rect card = rc(COL1, BODY_Y, WIN_W - 24, 190);
        drawCard(card, "contas");
        Rect fNome = rc(card.x + 14, card.y + 48, 200, 28);
        Rect fSenha = rc(card.x + 224, card.y + 48, 200, 28);
        drawField(fNome, "nome da conta", g_cmd, false, g_focus == 2);
        drawField(fSenha, "senha (criar)", g_val, false, g_focus == 3);
        Rect bCriar = rc(card.x + 434, card.y + 46, 90, 32);
        Rect bInfo = rc(card.x + 532, card.y + 46, 100, 32);
        Rect bDel = rc(card.x + 640, card.y + 46, 90, 32);
        drawButton(bCriar, "Criar", inRect(bCriar, mx, my), false);
        drawButton(bInfo, "Consultar", inRect(bInfo, mx, my), false);
        drawButton(bDel, "Excluir", inRect(bDel, mx, my), true);
        if (click) {
            if (inRect(fNome, mx, my)) g_focus = 2;
            if (inRect(fSenha, mx, my)) g_focus = 3;
            if (inRect(bCriar, mx, my)) return 20;
            if (inRect(bInfo, mx, my)) return 21;
            if (inRect(bDel, mx, my)) return 22;
        }
        Rect bBan = rc(card.x + 14, card.y + 128, 90, 32);
        Rect bUnban = rc(card.x + 114, card.y + 128, 100, 32);
        Rect fMotivo = rc(card.x + 224, card.y + 130, 260, 28);
        Rect fDias = rc(card.x + 494, card.y + 130, 70, 28);
        drawButton(bBan, "Banir", inRect(bBan, mx, my), true);
        drawButton(bUnban, "Desbanir", inRect(bUnban, mx, my), false);
        drawField(fMotivo, "motivo do ban", g_motivo, false, g_focus == 8);
        drawField(fDias, "dias (0 = permanente)", g_dias, false, g_focus == 9);
        if (click) {
            if (inRect(bBan, mx, my)) return 23;
            if (inRect(bUnban, mx, my)) return 24;
            if (inRect(fMotivo, mx, my)) g_focus = 8;
            if (inRect(fDias, mx, my)) g_focus = 9;
        }
        drawText((int)card.x + 14, (int)card.y + 170,
                 "consultar com o nome vazio lista todas as contas.", C_TEXT_DIM);
        return -1;
    }

    case 2: { /* ---- Personagens ---- */
        Rect card = rc(COL1, BODY_Y, WIN_W - 24, 210);
        drawCard(card, "personagens");
        Rect fConta = rc(card.x + 14, card.y + 48, 200, 28);
        Rect fChar = rc(card.x + 224, card.y + 48, 200, 28);
        Rect bList = rc(card.x + 434, card.y + 46, 100, 32);
        drawField(fConta, "conta", g_cmd, false, g_focus == 2);
        drawField(fChar, "personagem", g_val, false, g_focus == 3);
        drawButton(bList, "Listar", inRect(bList, mx, my), false);
        if (click) {
            if (inRect(fConta, mx, my)) g_focus = 2;
            if (inRect(fChar, mx, my)) g_focus = 3;
            if (inRect(bList, mx, my)) return 50;
        }
        Rect fLvl = rc(card.x + 14, card.y + 118, 90, 28);
        Rect fGold = rc(card.x + 114, card.y + 118, 120, 28);
        Rect fX = rc(card.x + 244, card.y + 118, 80, 28);
        Rect fY = rc(card.x + 334, card.y + 118, 80, 28);
        Rect bApp = rc(card.x + 434, card.y + 116, 100, 32);
        drawField(fLvl, "level", g_lvl, false, g_focus == 4);
        drawField(fGold, "gold", g_gold, false, g_focus == 5);
        drawField(fX, "x", g_posx, false, g_focus == 6);
        drawField(fY, "y", g_posy, false, g_focus == 7);
        drawButton(bApp, "Aplicar", inRect(bApp, mx, my), false);
        if (click) {
            if (inRect(fLvl, mx, my)) g_focus = 4;
            if (inRect(fGold, mx, my)) g_focus = 5;
            if (inRect(fX, mx, my)) g_focus = 6;
            if (inRect(fY, mx, my)) g_focus = 7;
            if (inRect(bApp, mx, my)) return 51;
        }
        drawText((int)card.x + 14, (int)card.y + 180,
                 "listar com conta vazia lista tudo. aplicar usa conta + personagem e os campos preenchidos.",
                 C_TEXT_DIM);
        return -1;
    }

    case 3: { /* ---- Online ---- */
        Rect card = rc(COL1, BODY_Y, WIN_W - 24, 150);
        drawCard(card, "jogadores online");
        Rect bList = rc(card.x + 14, card.y + 48, 150, 32);
        drawButton(bList, "Listar jogadores", inRect(bList, mx, my), false);
        Rect fNome = rc(card.x + 14, card.y + 108, 240, 28);
        Rect bKick = rc(card.x + 264, card.y + 106, 130, 32);
        drawField(fNome, "nome / conta para desconectar", g_cmd, false, g_focus == 2);
        drawButton(bKick, "Desconectar", inRect(bKick, mx, my), true);
        if (click) {
            if (inRect(bList, mx, my)) return 30;
            if (inRect(fNome, mx, my)) g_focus = 2;
            if (inRect(bKick, mx, my)) return 31;
        }
        return -1;
    }

    case 4: { /* ---- Lista (serverlist.bin do client) ---- */
        Rect card = rc(COL1, BODY_Y, WIN_W - 24, 320);
        drawCard(card, "serverlist.bin do client (tela de selecao de servidor)");
        /* caminho atual + carregar */
        Rect fPath = rc(card.x + 14, card.y + 48, 480, 28);
        drawInfo(fPath, "arquivo no client (client_serverlist no server.txt)",
                 g_slPath[0] ? g_slPath : "(carregue a lista)");
        Rect bLoad = rc(fPath.x + fPath.w + 12, fPath.y - 2, 140, 32);
        drawButton(bLoad, "Carregar lista", inRect(bLoad, mx, my), false);
        if (click && inRect(bLoad, mx, my)) return 80;
        /* edicao de grupo */
        Rect fGrp = rc(card.x + 14, card.y + 118, 90, 28);
        Rect fNome = rc(card.x + 114, card.y + 118, 200, 28);
        Rect fIP = rc(card.x + 324, card.y + 118, 200, 28);
        drawField(fGrp, "grupo (0-9)", g_slGroup, false, g_focus == 10);
        drawField(fNome, "nome do grupo", g_slName, false, g_focus == 11);
        drawField(fIP, "IP do servidor", g_slIP, false, g_focus == 12);
        if (click) {
            if (inRect(fGrp, mx, my)) g_focus = 10;
            if (inRect(fNome, mx, my)) g_focus = 11;
            if (inRect(fIP, mx, my)) g_focus = 12;
        }
        Rect bSet = rc(fIP.x + fIP.w + 12, fIP.y - 2, 150, 32);
        Rect bGen = rc(bSet.x + bSet.w + 10, fIP.y - 2, 130, 32);
        drawButton(bSet, "Aplicar no grupo", inRect(bSet, mx, my), false);
        drawButton(bGen, "Gerar padrao", inRect(bGen, mx, my), false);
        if (click && inRect(bSet, mx, my)) return 81;
        if (click && inRect(bGen, mx, my)) return 82;
        drawText((int)card.x + 14, (int)card.y + 172,
                 "gerar padrao cria 2 grupos com o nome e o IP informados; aplicar edita o grupo escolhido.",
                 C_TEXT_DIM);
        drawText((int)card.x + 14, (int)card.y + 172 + g_fh + 2,
                 "o client le o arquivo ao abrir a tela de selecao — reinicie o client depois de salvar.",
                 C_TEXT_DIM);
        /* tabela de grupos carregados */
        int y = (int)card.y + 216;
        if (g_slCount == 0) {
            drawText((int)card.x + 14, y, "(nenhuma lista carregada — clique em Carregar lista)",
                     C_TEXT_DIM);
        } else {
            drawText((int)card.x + 14, y, "GRUPO", C_TEXT_SUB);
            drawText((int)card.x + 80, y, "NOME", C_TEXT_SUB);
            drawText((int)card.x + 250, y, "SERVIDORES", C_TEXT_SUB);
            y += g_fh + 4;
            for (int i = 0; i < g_slCount; i++) {
                if (g_slRows[i].name[0] == 0 && g_slRows[i].count == 0) continue;
                char line[128];
                snprintf(line, sizeof line, "%d", i);
                drawText((int)card.x + 14, y, line, C_TEXT_DIM);
                drawText((int)card.x + 80, y, g_slRows[i].name, C_TEXT);
                snprintf(line, sizeof line, "%s%s", g_slRows[i].ip0,
                         g_slRows[i].count > 1 ? " (+)" : "");
                drawText((int)card.x + 250, y, line, C_TEXT_SUB);
                y += g_fh + 2;
                if (y > (int)(card.y + card.h - g_fh)) break;
            }
        }
        return -1;
    }

    case 5: { /* ---- Config ---- */
        Rect card = rc(COL1, BODY_Y, WIN_W - 24, 150);
        drawCard(card, "configuracao (persistida no SQLite)");
        Rect fKey = rc(card.x + 14, card.y + 48, 240, 28);
        Rect fVal = rc(card.x + 264, card.y + 48, 240, 28);
        Rect bSet = rc(card.x + 514, card.y + 46, 100, 32);
        drawField(fKey, "chave", g_cmd, false, g_focus == 2);
        drawField(fVal, "valor", g_val, false, g_focus == 3);
        drawButton(bSet, "Definir", inRect(bSet, mx, my), false);
        if (click) {
            if (inRect(fKey, mx, my)) g_focus = 2;
            if (inRect(fVal, mx, my)) g_focus = 3;
            if (inRect(bSet, mx, my)) return 60;
        }
        Rect bList = rc(card.x + 14, card.y + 108, 140, 32);
        Rect bAudit = rc(card.x + 164, card.y + 108, 150, 32);
        drawButton(bList, "Listar config", inRect(bList, mx, my), false);
        drawButton(bAudit, "Auditoria (50)", inRect(bAudit, mx, my), false);
        if (click) {
            if (inRect(bList, mx, my)) return 61;
            if (inRect(bAudit, mx, my)) return 62;
        }
        return -1;
    }

    case 6: { /* ---- Comandos livres ---- */
        Rect card = rc(COL1, BODY_Y, WIN_W - 24, 120);
        drawCard(card, "comando livre (protocolo do painel)");
        Rect fCmd = rc(card.x + 14, card.y + 48, 300, 28);
        Rect fVal = rc(card.x + 324, card.y + 48, 300, 28);
        Rect bRun = rc(card.x + 634, card.y + 46, 110, 32);
        drawField(fCmd, "comando", g_cmd, false, g_focus == 2);
        drawField(fVal, "argumentos (JSON ou chave=valor)", g_val, false, g_focus == 3);
        drawButton(bRun, "Executar", inRect(bRun, mx, my), false);
        if (click) {
            if (inRect(fCmd, mx, my)) g_focus = 2;
            if (inRect(fVal, mx, my)) g_focus = 3;
            if (inRect(bRun, mx, my)) return 40;
        }
        drawText((int)card.x + 14, (int)card.y + 96,
                 "server.status  players.list  players.kick name=x  account.ban name=x reason=y  config.set key=x value=y",
                 C_TEXT_DIM);
        return -1;
    }
    }
    return -1;
}

static void tabAction(int a) {
    char args[640];
    switch (a) {
    case 0: sendCmd("server.status", NULL); break;
    case 1: sendCmd("server.save", NULL); break;
    case 10: {
        char msg[280];
        escKey(g_broadcast, msg, sizeof msg);
        snprintf(args, sizeof args, "{\"message\":\"%s\"}", msg);
        sendCmd("server.panel", args);
        break;
    }
    case 20: { /* criar conta */
        char n[128], p[128];
        escKey(g_cmd, n, sizeof n);
        escKey(g_val, p, sizeof p);
        snprintf(args, sizeof args, "{\"name\":\"%s\",\"password\":\"%s\"}", n, p);
        sendCmd("account.create", args);
        break;
    }
    case 21: { /* consultar */
        char n[128];
        escKey(g_cmd, n, sizeof n);
        if (n[0]) snprintf(args, sizeof args, "{\"name\":\"%s\"}", n);
        else args[0] = 0;
        sendCmd(n[0] ? "account.info" : "accounts.list", n[0] ? args : NULL);
        break;
    }
    case 22: { /* excluir */
        char n[128];
        escKey(g_cmd, n, sizeof n);
        snprintf(args, sizeof args, "{\"name\":\"%s\"}", n);
        sendCmd("account.delete", args);
        break;
    }
    case 23: { /* banir */
        char n[128], r[256];
        escKey(g_cmd, n, sizeof n);
        escKey(g_motivo, r, sizeof r);
        long days = strtol(g_dias, NULL, 10);
        if (days > 0) {
            long long exp = (long long)time(NULL) + days * 86400LL;
            snprintf(args, sizeof args, "{\"name\":\"%s\",\"reason\":\"%s\",\"expires_at\":%lld}", n, r, exp);
        } else {
            snprintf(args, sizeof args, "{\"name\":\"%s\",\"reason\":\"%s\"}", n, r);
        }
        sendCmd("account.ban", args);
        break;
    }
    case 24: { /* desbanir */
        char n[128];
        escKey(g_cmd, n, sizeof n);
        snprintf(args, sizeof args, "{\"name\":\"%s\"}", n);
        sendCmd("account.unban", args);
        break;
    }
    case 30: sendCmd("players.list", NULL); break;
    case 31: { /* kick */
        char n[128];
        escKey(g_cmd, n, sizeof n);
        snprintf(args, sizeof args, "{\"name\":\"%s\"}", n);
        sendCmd("players.kick", args);
        break;
    }
    case 40: { /* comando livre */
        sendCmd(g_cmd, g_val[0] ? g_val : NULL);
        break;
    }
    case 50: { /* chars.list (conta opcional) */
        char a[128];
        escKey(g_cmd, a, sizeof a);
        if (a[0]) {
            snprintf(args, sizeof args, "{\"account\":\"%s\"}", a);
            sendCmd("chars.list", args);
        } else {
            sendCmd("chars.list", NULL);
        }
        break;
    }
    case 51: { /* char.set: level/gold/x/y preenchidos */
        char a[128], cn[128], parts[6][64];
        int np = 0;
        escKey(g_cmd, a, sizeof a);
        escKey(g_val, cn, sizeof cn);
        if (g_lvl[0])  snprintf(parts[np++], 64, "\"level\":%s", g_lvl);
        if (g_gold[0]) snprintf(parts[np++], 64, "\"gold\":%s", g_gold);
        if (g_posx[0]) snprintf(parts[np++], 64, "\"x\":%s", g_posx);
        if (g_posy[0]) snprintf(parts[np++], 64, "\"y\":%s", g_posy);
        if (!a[0] || !cn[0] || np == 0) {
            snprintf(g_lastResult, sizeof g_lastResult, "informe conta, personagem e ao menos um campo");
            g_lastError = true;
            break;
        }
        int off = snprintf(args, sizeof args, "{\"account\":\"%s\",\"name\":\"%s\"", a, cn);
        for (int i = 0; i < np && off < (int)sizeof args - 16; i++) {
            off += snprintf(args + off, sizeof args - (size_t)off, ",%s", parts[i]);
        }
        snprintf(args + off, sizeof args - (size_t)off, "}");
        sendCmd("char.set", args);
        break;
    }
    case 60: { /* config.set */
        char k[128], v[256];
        escKey(g_cmd, k, sizeof k);
        escKey(g_val, v, sizeof v);
        if (!k[0]) { snprintf(g_lastResult, sizeof g_lastResult, "informe a chave"); g_lastError = true; break; }
        snprintf(args, sizeof args, "{\"key\":\"%s\",\"value\":\"%s\"}", k, v);
        sendCmd("config.set", args);
        break;
    }
    case 61: sendCmd("config.list", NULL); break;
    case 62: sendCmd("audit.tail", "{\"limit\":50}"); break;
    case 70: /* supervisor: iniciar */
        WydGuiProcAction(1);
        break;
    case 71: /* supervisor: parar */
        WydGuiProcAction(2);
        break;
    case 72: /* supervisor: reiniciar */
        WydGuiProcAction(3);
        break;
    case 80: /* serverlist.get */
        sendCmd("serverlist.get", NULL);
        break;
    case 81: { /* serverlist.set */
        char g[40], nm[80], ip[80];
        escKey(g_slGroup, g, sizeof g);
        escKey(g_slName, nm, sizeof nm);
        escKey(g_slIP, ip, sizeof ip);
        if (!g[0]) { snprintf(g_lastResult, sizeof g_lastResult, "informe o grupo (0-9)"); g_lastError = true; break; }
        int off = snprintf(args, sizeof args, "{\"group\":\"%s\"", g);
        if (nm[0]) off += snprintf(args + off, sizeof args - (size_t)off, ",\"name\":\"%s\"", nm);
        if (ip[0]) off += snprintf(args + off, sizeof args - (size_t)off, ",\"host\":\"%s\"", ip);
        snprintf(args + off, sizeof args - (size_t)off, "}");
        sendCmd("serverlist.set", args);
        break;
    }
    case 82: { /* serverlist.generate */
        char nm[80], ip[80];
        escKey(g_slName, nm, sizeof nm);
        escKey(g_slIP, ip, sizeof ip);
        if (!nm[0] || !ip[0]) {
            snprintf(g_lastResult, sizeof g_lastResult, "informe o nome do grupo e o IP");
            g_lastError = true;
            break;
        }
        snprintf(args, sizeof args, "{\"group\":\"%s\",\"host\":\"%s\"}", nm, ip);
        sendCmd("serverlist.generate", args);
        break;
    }
    }
}

/* ------------------------------------------------------------------ */
/* main                                                                */
/* ------------------------------------------------------------------ */
int WydGuiMain(int argc, char **argv) {
    if (argc > 1) {
        const char *colon = strchr(argv[1], ':');
        if (colon) {
            size_t n = (size_t)(colon - argv[1]);
            if (n >= sizeof g_host) n = sizeof g_host - 1;
            memcpy(g_host, argv[1], n);
            g_host[n] = 0;
            g_port = atoi(colon + 1);
        } else {
            snprintf(g_host, sizeof g_host, "%s", argv[1]);
        }
    }
    if (argc > 2) snprintf(g_pw, sizeof g_pw, "%s", argv[2]);

    if (!SDL_Init(SDL_INIT_VIDEO)) {
        SDL_Log("SDL_Init falhou: %s", SDL_GetError());
        return 1;
    }
    g_win = SDL_CreateWindow("WYD-Go Admin — Servidor 7.48", WIN_W, WIN_H, 0);
    if (!g_win) {
        SDL_Log("CreateWindow falhou: %s", SDL_GetError());
        return 1;
    }
    g_ren = SDL_CreateRenderer(g_win, NULL);
    if (!g_ren) {
        SDL_Log("CreateRenderer falhou: %s", SDL_GetError());
        return 1;
    }

    /* atlas de fonte: 16 cols x 6 rows de 8x14 = 128x84 */
    g_font = SDL_CreateTexture(g_ren, SDL_PIXELFORMAT_RGBA32,
                               SDL_TEXTUREACCESS_STATIC, 128, 84);
    if (!g_font) { SDL_Log("textura fonte: %s", SDL_GetError()); return 1; }
    SDL_SetTextureScaleMode(g_font, SDL_SCALEMODE_NEAREST);
    if (!SDL_UpdateTexture(g_font, NULL, font_atlas_pixels, 128 * 4)) {
        SDL_Log("update fonte: %s", SDL_GetError());
        return 1;
    }

    /* senha via linha de comando: conecta automaticamente */
    if (argc > 2 && g_pw[0]) doConnect();

    SDL_StartTextInput(g_win);
    g_quit = false;
    while (!g_quit) {
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            switch (ev.type) {
            case SDL_EVENT_QUIT:
                g_quit = true;
                break;
            case SDL_EVENT_KEY_DOWN: {
                bool ctl = SDL_KMOD_CTRL & SDL_GetModState();
                char *buf = bufferForFocus(NULL);
                size_t maxlen = 256;
                bufferForFocus(&maxlen);
                switch (ev.key.key) {
                case SDLK_ESCAPE:
                    g_quit = true;
                    break;
                case SDLK_BACKSPACE:
                    if (ctl) *buf = 0;
                    else if (*buf) buf[strlen(buf) - 1] = 0;
                    break;
                case SDLK_RETURN:
                case SDLK_KP_ENTER:
                    if (!g_authed) doConnect();
                    else if (g_tab == 0 && g_focus == 2) tabAction(10);
                    else if (g_tab == 1) tabAction(20);
                    else if (g_tab == 2) tabAction(50);
                    else if (g_tab == 3) tabAction(31);
                    else if (g_tab == 4) tabAction(80);
                    else if (g_tab == 5) tabAction(60);
                    else if (g_tab == 6) tabAction(40);
                    break;
                case SDLK_TAB:
                    focusNext();
                    break;
                case SDLK_V:
                    if (ctl) {
                        char *clip = SDL_GetClipboardText();
                        if (clip) {
                            size_t cur = strlen(buf);
                            size_t cl = strcspn(clip, "\r\n");
                            if (cur + cl < maxlen) { strncat(buf, clip, cl); }
                            SDL_free(clip);
                        }
                    }
                    break;
                }
                break;
            }
            case SDL_EVENT_TEXT_INPUT:
                if (ev.text.text[0] >= 32 && (unsigned char)ev.text.text[0] < 127) {
                    char *buf = bufferForFocus(NULL);
                    size_t maxlen = 256;
                    bufferForFocus(&maxlen);
                    if (strlen(buf) + strlen(ev.text.text) < maxlen) {
                        strcat(buf, ev.text.text);
                    }
                }
                break;
            case SDL_EVENT_MOUSE_BUTTON_DOWN:
                frame((float)ev.button.x, (float)ev.button.y, true);
                break;
            case SDL_EVENT_MOUSE_MOTION:
                break;
            }
        }

        /* atualizacao automatica das estatisticas (aba Status, 5s) */
        if (g_authed && g_tab == 0 && !g_hasPending &&
            SDL_GetTicks() - g_lastPoll > 5000) {
            g_lastPoll = SDL_GetTicks();
            sendCmd("server.status", NULL);
        }

        /* poll de rede: processa linhas pendentes */
        if (g_connected) {
            const char *resp;
            while ((resp = netPoll()) != NULL) {
                char ok[16] = "";
                jsonFind(resp, "ok", ok, sizeof ok);
                if (!g_authed) {
                    if (ok[0] == 't') {
                        g_authed = true;
                        snprintf(g_status, sizeof g_status, "autenticado");
                        g_lastError = false;
                        g_lastResult[0] = 0;
                        sendCmd("server.status", NULL);
                    } else {
                        char err[160] = "";
                        jsonFind(resp, "error", err, sizeof err);
                        snprintf(g_lastResult, sizeof g_lastResult, "login falhou: %s",
                                 err[0] ? err : "senha invalida");
                        g_lastError = true;
                        snprintf(g_status, sizeof g_status, "login recusado");
                        netCloseReal();
                    }
                    continue;
                }
                char err[200] = "";
                jsonFind(resp, "error", err, sizeof err);
                if (err[0]) {
                    snprintf(g_lastResult, sizeof g_lastResult, "ERRO %s: %s",
                             g_hasPending ? g_pending : "", err);
                    g_lastError = true;
                } else {
                    char *res = strstr(resp, "\"result\":");
                    snprintf(g_lastResult, sizeof g_lastResult, "%s", res ? res + 9 : resp);
                    g_lastError = false;
                    if (g_hasPending && strcmp(g_pending, "serverlist.get") == 0) {
                        parseServerList(g_lastResult);
                    }
                    if (g_hasPending && strcmp(g_pending, "server.status") == 0) {
                        char f[64];
                        if (jsonFind(g_lastResult, "online", f, sizeof f)) g_stOnline = atoi(f);
                        if (jsonFind(g_lastResult, "accounts", f, sizeof f)) g_stAccounts = atol(f);
                        if (jsonFind(g_lastResult, "mem_alloc_mb", f, sizeof f)) g_stMem = atof(f);
                        if (jsonFind(g_lastResult, "uptime_seconds", f, sizeof f)) g_stUptime = atol(f);
                        if (jsonFind(g_lastResult, "in_world", f, sizeof f)) g_stInWorld = atoi(f);
                        if (jsonFind(g_lastResult, "queue_depth", f, sizeof f)) g_stQueue = atoi(f);
                        if (jsonFind(g_lastResult, "started_at", f, sizeof f)) g_stStarted = atol(f);
                    }
                }
                g_hasPending = false;
            }
        }

        /* render */
        float mx, my;
        Uint32 mb = SDL_GetMouseState(&mx, &my);
        frame(mx, my, false);
        SDL_RenderPresent(g_ren);
        (void)mb;
        SDL_Delay(16);
    }

    if (g_sock >= 0) close(g_sock);
    SDL_Quit();
    return 0;
}

/* Entrada simplificada para o host Go (cgo): host:porta + senha opcionais. */
int wydgui_start(const char *hostport, const char *password) {
    char *argvv[3];
    argvv[0] = (char *)"wydgui";
    argvv[1] = (char *)hostport;
    argvv[2] = (char *)password;
    int argc = password ? 3 : 2;
    return WydGuiMain(argc, argvv);
}
