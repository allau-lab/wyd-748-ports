// Ponte da janela (C) para o Supervisor (Go): estado do processo do jogo e
// acoes iniciar/parar/reiniciar chamadas pelos botoes da aba Status.
// Compilado sempre: Go puro.
package gui

import (
	"fmt"
	"log"
	"sync"
	"time"
)

var (
	supMu sync.Mutex
	sup   *Supervisor
)

// BindSupervisor registra o supervisor usado pela janela.
func BindSupervisor(s *Supervisor) {
	supMu.Lock()
	defer supMu.Unlock()
	sup = s
}

// ProcState devolve "rodando (pid N)", "parado" ou "parado (motivo)".
func ProcState() string {
	supMu.Lock()
	s := sup
	supMu.Unlock()
	if s == nil {
		return "supervisor ausente"
	}
	running, lastExit, pid := s.Status()
	if running {
		return fmt.Sprintf("rodando (pid %d)", pid)
	}
	if lastExit != "" {
		return "parado (" + lastExit + ")"
	}
	return "parado"
}

// ProcAction executa 1=iniciar 2=parar 3=reiniciar e devolve mensagem para a
// janela exibir.
func ProcAction(action int) string {
	supMu.Lock()
	s := sup
	supMu.Unlock()
	if s == nil {
		return "supervisor ausente"
	}
	var err error
	switch action {
	case 1:
		err = s.Start()
	case 2:
		err = s.Stop(20 * time.Second)
	case 3:
		err = s.Restart()
	}
	if err != nil {
		log.Printf("[gui] acao %d: %v", action, err)
		return "erro: " + err.Error()
	}
	names := map[int]string{1: "iniciado", 2: "parado (estado persistido)", 3: "reiniciado"}
	return names[action]
}
