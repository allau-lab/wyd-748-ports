//go:build !gui

// Stub da janela para o build headless (sem SDL3/cgo).
package gui

import "errors"

// StartWindow sempre falha: compile com -tags gui para ter a janela.
func StartWindow(hostPort, password string) error {
	return errors.New("gui nao incluida neste build (compile com -tags gui)")
}
