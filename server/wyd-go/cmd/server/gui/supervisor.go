// Supervisor do modo --gui: a janela de administracao e o app principal e o
// servidor do jogo roda como PROCESSO-FILHO deste mesmo binario (re-exec com
// a env __wyd_child=1). Isso permite Iniciar/Parar/Reiniciar pela janela sem
// depender do ciclo de vida do proprio processo da GUI. Compilado sempre:
// e Go puro (sem SDL3).
package gui

import (
	"fmt"
	"os"
	"os/exec"
	"strings"
	"sync"
	"syscall"
	"time"
)

// childEnv marca o re-exec como processo-filho do supervisor.
const childEnv = "__wyd_child=1"

// isChildEnv verifica a env do filho (usada por IsChild do stub.go).
func isChildEnv() bool {
	for _, kv := range os.Environ() {
		if kv == childEnv {
			return true
		}
	}
	return false
}

// ChildArgs devolve os argumentos do filho: os mesmos do pai menos o --gui
// (o filho roda o servidor headless normal).
func ChildArgs(argv []string) []string {
	out := make([]string, 0, len(argv))
	for _, a := range argv {
		if a == "--gui" || a == "-gui" {
			continue
		}
		out = append(out, a)
	}
	return out
}

// Supervisor controla o ciclo de vida do processo-filho do servidor.
type Supervisor struct {
	mu       sync.Mutex
	exe      string
	args     []string
	env      []string
	dir      string
	logFile  *os.File
	logPath  string
	child    *exec.Cmd
	running  bool
	lastExit string
}

// NewSupervisor prepara o supervisor. O filho re-executa este mesmo binario
// sem --gui e com __wyd_child=1; stdout/stderr vao para logs/gui-child.log
// (ou o caminho informado).
func NewSupervisor(argv []string, logPath string) (*Supervisor, error) {
	exe, err := os.Executable()
	if err != nil {
		return nil, fmt.Errorf("resolver executavel: %w", err)
	}
	if logPath == "" {
		logPath = "logs/gui-child.log"
	}
	if err := os.MkdirAll(strings.TrimSuffix(logPath, "/"+lastSegment(logPath)), 0o755); err != nil {
		return nil, err
	}
	env := os.Environ()
	env = append(env, childEnv)
	return &Supervisor{exe: exe, args: ChildArgs(argv), env: env, logPath: logPath}, nil
}

func lastSegment(p string) string {
	if i := strings.LastIndexAny(p, "/\\"); i >= 0 {
		return p[i+1:]
	}
	return p
}

// SetEnv substitui o ambiente herdado pelo filho (usado pelo pai para
// injetar WYD_GAME_ADDR da porta escolhida interativamente).
func (s *Supervisor) SetEnv(env []string) {
	s.mu.Lock()
	defer s.mu.Unlock()
	s.env = env
}

// Start sobe o processo-filho. Se ja esta rodando, nao faz nada.
func (s *Supervisor) Start() error {
	s.mu.Lock()
	defer s.mu.Unlock()
	if s.running {
		return nil
	}
	lf, err := os.OpenFile(s.logPath, os.O_CREATE|os.O_WRONLY|os.O_APPEND, 0o644)
	if err != nil {
		return fmt.Errorf("abrir log do filho: %w", err)
	}
	cmd := exec.Command(s.exe, s.args...)
	cmd.Env = s.env
	cmd.Stdout = lf
	cmd.Stderr = lf
	cmd.Dir = ""
	if err := cmd.Start(); err != nil {
		lf.Close()
		return fmt.Errorf("iniciar servidor: %w", err)
	}
	s.child = cmd
	s.logFile = lf
	s.running = true
	s.lastExit = ""
	go s.wait(cmd, lf)
	return nil
}

// wait reaps o filho e atualiza o estado quando ele sai.
func (s *Supervisor) wait(cmd *exec.Cmd, lf *os.File) {
	err := cmd.Wait()
	s.mu.Lock()
	defer s.mu.Unlock()
	lf.Close()
	s.running = false
	s.child = nil
	if err != nil {
		s.lastExit = err.Error()
	} else {
		s.lastExit = "saiu limpo"
	}
}

// Stop encerra o filho com SIGTERM (o servidor persiste ao receber SIGTERM).
// Espera ate timeout pelo exit; escaler para SIGKILL se passar.
func (s *Supervisor) Stop(timeout time.Duration) error {
	s.mu.Lock()
	if !s.running || s.child == nil {
		s.mu.Unlock()
		return nil
	}
	cmd := s.child
	s.mu.Unlock()
	// SIGTERM: o handler do servidor persiste e sai com 0.
	if cmd.Process != nil {
		_ = cmd.Process.Signal(syscall.SIGTERM)
	}
	deadline := time.Now().Add(timeout)
	for time.Now().Before(deadline) {
		s.mu.Lock()
		running := s.running && s.child == cmd
		s.mu.Unlock()
		if !running {
			return nil
		}
		time.Sleep(100 * time.Millisecond)
	}
	// Escalada: matar sem salvar.
	s.mu.Lock()
	if s.running && s.child == cmd && cmd.Process != nil {
		_ = cmd.Process.Kill()
	}
	s.mu.Unlock()
	return nil
}

// Restart reinicia o filho (stop gracioso + start).
func (s *Supervisor) Restart() error {
	if err := s.Stop(20 * time.Second); err != nil {
		return err
	}
	return s.Start()
}

// Status devolve o estado atual do filho.
func (s *Supervisor) Status() (running bool, lastExit string, pid int) {
	s.mu.Lock()
	defer s.mu.Unlock()
	if s.child != nil && s.child.Process != nil {
		pid = s.child.Process.Pid
	}
	return s.running, s.lastExit, pid
}

// LogPath devolve onde esta o log do filho.
func (s *Supervisor) LogPath() string { return s.logPath }
