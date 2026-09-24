//go:build gui

// Package sdl embute a janela de administracao SDL3 (C). Compilado somente
// com `-tags gui`; o pacote pai (gui) escolhe entre este e o stub em tempo
// de build. As pontes de estado/acao do processo chegam por SetBridges —
// o C chama ponteiros globais, sem conhecer Go alem das duas exportadas.
package sdl

/*
#cgo CFLAGS: -I/usr/include/SDL3 -O2 -Wno-discarded-qualifiers
#cgo LDFLAGS: -lSDL3 -lm
#include <stdlib.h>

// Somente DECLARACOES aqui: o preamble do cgo e copiado para varias
// translation units (_cgo_export.c inclui), e definicoes duplicam simbolos.
// As definicoes dos wrappers WydGuiProcState/WydGuiProcAction e o corpo da
// janela vivem em gui_impl.c (compilado UMA vez).
int WydGuiMain(int argc, char **argv);
int wydgui_start(const char *hostport, const char *password);
*/
import "C"

import (
	"fmt"
	"runtime"
	"sync"
	"unsafe"
)

var (
	bridgesMu sync.Mutex
	stateFn   func() string
	actionFn  func(int) string
)

// SetBridges registra as funcoes Go chamadas pelos botoes da janela.
func SetBridges(state func() string, action func(int) string) {
	bridgesMu.Lock()
	defer bridgesMu.Unlock()
	stateFn, actionFn = state, action
}

//export WydGuiProcStateC
func WydGuiProcStateC() *C.char {
	bridgesMu.Lock()
	fn := stateFn
	bridgesMu.Unlock()
	if fn == nil {
		return C.CString("indisponivel")
	}
	return C.CString(fn())
}

//export WydGuiProcActionC
func WydGuiProcActionC(action C.int) {
	bridgesMu.Lock()
	fn := actionFn
	bridgesMu.Unlock()
	if fn == nil {
		return
	}
	_ = fn(int(action))
}

// Start roda a janela na thread chamadora (SDL3 exige o loop de eventos na
// thread que criou a janela). Bloqueia ate a janela fechar.
func Start(hostPort, password string) error {
	// SDL3: janela/eventos/render PRECISAM viver em uma unica thread do SO.
	// Sem LockOSThread o scheduler do Go migra esta goroutine e o SDL aborta
	// (SIGABRT visto em producao ao clicar Conectar).
	runtime.LockOSThread()
	defer runtime.UnlockOSThread()

	chp := C.CString(hostPort)
	defer C.free(unsafe.Pointer(chp))
	var cpw *C.char
	if password != "" {
		cpw = C.CString(password)
		defer C.free(unsafe.Pointer(cpw))
	}
	rc := C.wydgui_start(chp, cpw)
	if rc != 0 {
		return fmt.Errorf("gui terminou com codigo %d", rc)
	}
	return nil
}
