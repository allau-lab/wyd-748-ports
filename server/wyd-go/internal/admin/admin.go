// Package admin expoe o plano de controle do servidor: um dispatcher de
// comandos (mesmo para TCP, console --cli e futura GUI) e um servico TCP com
// autenticacao para acesso remoto do painel SDL3.
package admin

import (
	"bufio"
	"context"
	"crypto/rand"
	"encoding/hex"
	"encoding/json"
	"errors"
	"fmt"
	"log"
	"net"
	"strings"
	"sync"
	"time"
)

// Handler processa um comando administrativo e devolve o resultado (ou erro).
// Implementado pelo backend em backend.go sobre World+Store.
type Handler interface {
	Handle(ctx context.Context, cmd string, args map[string]any) (any, error)
}

// Command e uma requisicao do protocolo (newline-delimited JSON).
type Command struct {
	ID   int64          `json:"id"`
	Cmd  string         `json:"cmd"`
	Args map[string]any `json:"args,omitempty"`
}

// Response e a resposta do protocolo.
type Response struct {
	ID     int64  `json:"id"`
	OK     bool   `json:"ok"`
	Result any    `json:"result,omitempty"`
	Error  string `json:"error,omitempty"`
}

// Config do servico TCP admin.
type Config struct {
	// ListenAddress (host:porta). Vazio desliga o TCP (só console local).
	ListenAddress string
	// Password obrigatoria para autenticar no TCP. Se vazia, uma senha
	// aleatoria e gerada e impressa no log (uma vez).
	Password string
	// ReadTimeout por comando.
	ReadTimeout time.Duration
}

// Service e o servidor TCP admin. Zero estado de jogo: tudo via Handler.
type Service struct {
	cfg  Config
	h    Handler
	auth map[string]struct{} // senha aceita (suporta 1)
	ln   net.Listener

	mu      sync.Mutex
	failed  map[string]int // ip -> falhas consecutivas
	stopped chan struct{}
	once    sync.Once
}

// NewService cria o servico (nao escuta ainda — chame ListenAndServe).
func NewService(cfg Config, h Handler) *Service {
	if cfg.ReadTimeout <= 0 {
		cfg.ReadTimeout = 30 * time.Second
	}
	if cfg.Password == "" {
		cfg.Password = generatePassword()
		log.Printf("[admin] senha aleatoria gerada (defina admin_password ou WYD_ADMIN_PASSWORD): %s",
			cfg.Password)
	}
	return &Service{
		cfg:     cfg,
		h:       h,
		auth:    map[string]struct{}{cfg.Password: {}},
		failed:  make(map[string]int),
		stopped: make(chan struct{}),
	}
}

// Password devolve a senha efetiva (para o main logar ou o console local usar).
func (s *Service) Password() string { return s.cfg.Password }

// ListenAndServe escuta e atende conexoes ate Close/Stop.
func (s *Service) ListenAndServe(ctx context.Context) error {
	if strings.TrimSpace(s.cfg.ListenAddress) == "" {
		return errors.New("admin: listen vazio (servico TCP desligado)")
	}
	ln, err := net.Listen("tcp", s.cfg.ListenAddress)
	if err != nil {
		return fmt.Errorf("admin: listen %s: %w", s.cfg.ListenAddress, err)
	}
	s.ln = ln
	log.Printf("[admin] painel escutando em %s", s.cfg.ListenAddress)
	go func() {
		<-ctx.Done()
		s.Stop()
	}()
	for {
		conn, err := ln.Accept()
		if err != nil {
			select {
			case <-s.stopped:
				return nil
			default:
			}
			return fmt.Errorf("admin: accept: %w", err)
		}
		go s.serveConn(conn)
	}
}

// Stop encerra o listener e as conexoes em atendimento.
func (s *Service) Stop() {
	s.once.Do(func() {
		close(s.stopped)
		if s.ln != nil {
			s.ln.Close()
		}
	})
}

func clientIP(addr string) string {
	host, _, err := net.SplitHostPort(addr)
	if err != nil {
		return addr
	}
	return host
}

func (s *Service) serveConn(conn net.Conn) {
	defer conn.Close()
	remote := conn.RemoteAddr().String()
	ip := clientIP(remote)
	log.Printf("[admin] conexao de %s", remote)
	defer func() {
		s.mu.Lock()
		delete(s.failed, ip)
		s.mu.Unlock()
	}()

	reader := bufio.NewReaderSize(conn, 64*1024)
	authenticated := false
	for {
		line, err := readLine(reader)
		if err != nil {
			return
		}
		if strings.TrimSpace(line) == "" {
			continue
		}
		var cmd Command
		if err := json.Unmarshal([]byte(line), &cmd); err != nil {
			writeResponse(conn, Response{OK: false, Error: "json invalido"})
			return
		}
		if !authenticated {
			if cmd.Cmd != "auth" {
				writeResponse(conn, Response{ID: cmd.ID, OK: false,
					Error: "autentique-se primeiro (cmd=auth)"})
				return
			}
			password, _ := cmd.Args["password"].(string)
			s.mu.Lock()
			if _, ok := s.auth[password]; ok {
				authenticated = true
				delete(s.failed, ip)
				s.mu.Unlock()
				writeResponse(conn, Response{ID: cmd.ID, OK: true,
					Result: map[string]any{"welcome": "wyd-admin"}})
				continue
			}
			s.failed[ip]++
			failures := s.failed[ip]
			s.mu.Unlock()
			log.Printf("[admin] senha errada de %s (tentativa %d)", remote, failures)
			writeResponse(conn, Response{ID: cmd.ID, OK: false, Error: "senha invalida"})
			if failures >= 3 {
				log.Printf("[admin] desconectando %s apos 3 falhas", remote)
				return
			}
			continue
		}
		if cmd.Cmd == "quit" || cmd.Cmd == "bye" {
			writeResponse(conn, Response{ID: cmd.ID, OK: true, Result: "bye"})
			return
		}
		ctx, cancel := context.WithTimeout(context.Background(), s.cfg.ReadTimeout)
		result, err := s.h.Handle(ctx, cmd.Cmd, cmd.Args)
		cancel()
		resp := Response{ID: cmd.ID}
		if err != nil {
			resp.Error = err.Error()
		} else {
			resp.OK = true
			resp.Result = result
		}
		if err := writeResponse(conn, resp); err != nil {
			return
		}
	}
}

func readLine(r *bufio.Reader) (string, error) {
	line, err := r.ReadString('\n')
	if err != nil {
		return "", err
	}
	return strings.TrimRight(line, "\r\n"), nil
}

func writeResponse(conn net.Conn, resp Response) error {
	b, err := json.Marshal(resp)
	if err != nil {
		return err
	}
	b = append(b, '\n')
	_, err = conn.Write(b)
	return err
}

func generatePassword() string {
	b := make([]byte, 12)
	if _, err := rand.Read(b); err != nil {
		return "wyd-admin-" + fmt.Sprint(time.Now().Unix())
	}
	return hex.EncodeToString(b)
}
