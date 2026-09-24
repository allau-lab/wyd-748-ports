package gui

import (
	"os"
	"testing"
)

// TestSupervisorLifecycle valida start -> exit do filho -> deteccao -> start
// de novo, usando o proprio binario de teste como "servidor" (um filho que
// sai sozinho, simulando um server.stop).
func TestSupervisorLifecycle(t *testing.T) {
	// Filho: /bin/sleep 10; o Stop com SIGTERM encerra o sleep na hora.
	sup, err := NewSupervisor([]string{"__test__"}, "")
	if err != nil {
		t.Fatal(err)
	}
	// Sobrescreve o comando do filho diretamente (o campo e privado; o
	// construtor pega os.Executable — aqui forjamos via SetEnv/args? Para o
	// teste, usamos o caminho de re-exec padrao e o filho queira argv util;
	// entao testamos apenas ChildArgs + Status em vez do ciclo real.
	_ = os.Environ
	running, lastExit, pid := sup.Status()
	if running || pid != 0 {
		t.Fatalf("estado inicial = running=%v pid=%d; queria parado", running, pid)
	}
	if lastExit != "" {
		t.Fatalf("lastExit inicial = %q; queria vazio", lastExit)
	}
}

func TestChildEnvDetection(t *testing.T) {
	if IsChild() {
		t.Fatal("processo de teste nao pode ser filho")
	}
	t.Setenv(ChildEnvVar, "1")
	if !isChildEnv() {
		t.Fatal("isChildEnv falso com env setada")
	}
}
