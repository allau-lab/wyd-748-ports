package game

import (
	"errors"
	"fmt"
	"log"
	"os"
	"path/filepath"
	"runtime"
	"strings"
	"time"

	"wydgo/internal/net"
	"wydgo/internal/serverlist"
	"wydgo/internal/wire"
)

// Operações administrativas (console --cli e GUI admin). TUDO roda dentro do
// game-loop via canal dedicado: a mesma single-goroutine que processa pacotes
// de client garante zero corrida com o tick e com o mapa de players.

// adminJob e uma tarefa administrativa com barreira opcional.
type adminJob struct {
	run  func()
	done chan struct{}
}

// AdminCallCapacity limita o backlog de comandos admin; um console/GUI não
// deve jamais enfileirar mais que isso.
const AdminCallCapacity = 64

// ErrAdminOverload acontece quando a fila admin estourou (game-loop travado).
var ErrAdminOverload = errors.New("game: fila admin cheia")

// EnqueueAdmin agenda uma função para rodar no game-loop sem esperar.
func (w *World) EnqueueAdmin(run func()) error {
	select {
	case w.adminCommands <- adminJob{run: run}:
		return nil
	default:
		return ErrAdminOverload
	}
}

// AdminCall enfileira e ESPERA a execução no game-loop (chamada síncrona do
// CLI/GUI). Retorna ErrAdminOverload se a fila estourar.
func (w *World) AdminCall(run func()) error {
	job := adminJob{run: run, done: make(chan struct{})}
	select {
	case w.adminCommands <- job:
	default:
		return ErrAdminOverload
	}
	select {
	case <-job.done:
		return nil
	case <-time.After(5 * time.Second):
		return errors.New("game: timeout aguardando game-loop")
	}
}

// drainAdminJobs consome o backlog pendente (chamado no Run).
func (w *World) drainAdminJobs() {
	for {
		select {
		case job := <-w.adminCommands:
			if job.run != nil {
				job.run()
			}
			if job.done != nil {
				close(job.done)
			}
		default:
			return
		}
	}
}

// AdminPlayerInfo e a projeção de um jogador online para administração.
type AdminPlayerInfo struct {
	Name      string `json:"name"`
	Account   string `json:"account"`
	Class     int    `json:"class"`
	Level     int    `json:"level"`
	X         uint16 `json:"x"`
	Y         uint16 `json:"y"`
	IP        string `json:"ip"`
	ClientID  uint16 `json:"client_id"`
	InWorld   bool   `json:"in_world"`
	SessionID int64  `json:"session_id"`
}

// AdminListPlayers devolve os jogadores conectados (autenticados ou não).
func (w *World) AdminListPlayers() []AdminPlayerInfo {
	out := make([]AdminPlayerInfo, 0, len(w.players))
	for _, p := range w.players {
		info := AdminPlayerInfo{
			InWorld:  p.InWorld,
			X:        p.X,
			Y:        p.Y,
			ClientID: p.ID,
		}
		if p.Session != nil {
			info.IP = p.Session.RemoteAddr()
			info.SessionID = p.Session.ID
		}
		if p.Account != nil {
			info.Account = p.Account.Name
		}
		if p.Char != nil {
			info.Name = p.Char.Name
			info.Class = int(p.Char.Class)
			if p.Char.Score != nil {
				info.Level = int(p.Char.Score.Level)
			}
		}
		out = append(out, info)
	}
	return out
}

// AdminKickByName desconecta um jogador pelo nome do personagem. O save de
// disconnect acontece pelo caminho normal (fechamento de sessão é tratado como
// logout), então o kick não perde estado.
func (w *World) AdminKickByName(name string) (bool, error) {
	name = strings.TrimSpace(name)
	if name == "" {
		return false, errors.New("nome vazio")
	}
	var session *net.Session
	for _, p := range w.players {
		if p.Char != nil && strings.EqualFold(p.Char.Name, name) {
			session = p.Session
			break
		}
	}
	if session == nil {
		return false, nil
	}
	session.Close()
	return true, nil
}

// AdminKickAccount desconecta todas as sessões de uma conta.
func (w *World) AdminKickAccount(account string) (int, error) {
	account = strings.TrimSpace(account)
	if account == "" {
		return 0, errors.New("conta vazia")
	}
	kicked := 0
	for _, p := range w.players {
		if p.Account != nil && strings.EqualFold(p.Account.Name, account) && p.Session != nil {
			p.Session.Close()
			kicked++
		}
	}
	return kicked, nil
}

// AdminSaveAll persiste as contas de todos os jogadores autenticados e devolve
// quantas foram gravadas com sucesso. Executa DENTRO da game-loop (chame via
// AdminCall): saveAccount não é thread-safe fora dela.
func (w *World) AdminSaveAll() int {
	saved := 0
	for _, p := range w.players {
		if p.Account == nil {
			continue
		}
		if err := w.saveAccount(p.Account); err != nil {
			log.Printf("[admin] ERRO ao salvar conta %q: %v", p.Account.Name, err)
			continue
		}
		saved++
	}
	return saved
}

// AdminBroadcast envia chat global assinado pelo servidor e devolve quantos
// jogadores receberam.
func (w *World) AdminBroadcast(message string) (int, error) {
	message = strings.TrimSpace(message)
	if message == "" {
		return 0, errors.New("mensagem vazia")
	}
	sent := 0
	for _, p := range w.players {
		if p.Session == nil {
			continue
		}
		p.Session.Send(wire.MessageWhisper(0, "SERVER", message, 3))
		sent++
	}
	return sent, nil
}

// AdminSystemPanel manda painel de sistema (janela modal no client).
func (w *World) AdminSystemPanel(message string) (int, error) {
	message = strings.TrimSpace(message)
	if message == "" {
		return 0, errors.New("mensagem vazia")
	}
	sent := 0
	for _, p := range w.players {
		if p.Session == nil {
			continue
		}
		p.Session.Send(wire.MessagePanel(message))
		sent++
	}
	return sent, nil
}

// --- serverlist.bin do client ---------------------------------------------

// ErrServerListDisabled avisa que client_serverlist nao esta configurado.
var ErrServerListDisabled = errors.New("serverlist: client_serverlist nao configurado (adicione client_serverlist=<caminho> no server.txt)")

// AdminServerListRead devolve a lista decodificada do serverlist.bin do client
// (ou a gerada a partir do listen_address quando o arquivo ainda nao existe).
func (w *World) AdminServerListRead() (*serverlist.List, string, error) {
	path := strings.TrimSpace(w.serverListPath)
	if path == "" {
		return nil, "", ErrServerListDisabled
	}
	if _, err := os.Stat(path); err != nil {
		// Primeira execucao: sugere a partir do endereco de escuta do servidor.
		l, terr := serverlist.FromTemplate("WYD748", w.gameAddress)
		if terr != nil {
			return nil, path, err
		}
		return l, path, nil
	}
	l, err := serverlist.ReadFile(path)
	if err != nil {
		return nil, path, err
	}
	return l, path, nil
}

// AdminServerListWrite valida e grava o serverlist.bin do client. Caminho
// relativo e resolvido a partir do diretorio de trabalho do SERVIDOR — o
// painel (TCP/GUI/CLI) nao tem como adivinhar onde esta a pasta do client.
func (w *World) AdminServerListWrite(l *serverlist.List) (string, error) {
	path := strings.TrimSpace(w.serverListPath)
	if path == "" {
		return "", ErrServerListDisabled
	}
	if !filepath.IsAbs(path) {
		if abs, err := filepath.Abs(path); err == nil {
			path = abs
		}
	}
	if err := l.WriteFile(path); err != nil {
		return path, err
	}
	log.Printf("[admin] serverlist.bin gravado em %s", path)
	return path, nil
}

// AdminStatus é o resumo operacional do dashboard.
type AdminStatus struct {
	UptimeSeconds int64                  `json:"uptime_seconds"`
	Online        int                    `json:"online"`
	InWorld       int                    `json:"in_world"`
	QueueDepth    int                    `json:"queue_depth"`
	Accounts      int64                  `json:"accounts,omitempty"`
	MemAllocMB    float64                `json:"mem_alloc_mb"`
	StartedAt     int64                  `json:"started_at"`
	Extra         map[string]interface{} `json:"extra,omitempty"`
}

var worldStartedAt = time.Now()

// AdminStatusSnapshot coleta métricas do mundo (chamado via AdminCall).
func (w *World) AdminStatusSnapshot() AdminStatus {
	inWorld := 0
	for _, p := range w.players {
		if p.InWorld {
			inWorld++
		}
	}
	var mem runtime.MemStats
	runtime.ReadMemStats(&mem)
	status := AdminStatus{
		UptimeSeconds: int64(time.Since(worldStartedAt) / time.Second),
		Online:        len(w.players),
		InWorld:       inWorld,
		QueueDepth:    w.commandQueueDepth(),
		StartedAt:     worldStartedAt.Unix(),
		MemAllocMB:    float64(mem.Alloc) / (1024 * 1024),
	}
	return status
}

// Describe imprime um jogador em uma linha (CLI).
func (p AdminPlayerInfo) Describe() string {
	status := "auth"
	if p.InWorld {
		status = "world"
	}
	return fmt.Sprintf("%-16s conta=%-16s classe=%d nivel=%d pos=(%d,%d) %s %s",
		p.Name, p.Account, p.Class, p.Level, p.X, p.Y, status, p.IP)
}
