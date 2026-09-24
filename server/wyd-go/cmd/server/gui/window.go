//go:build gui

// Janela real: delega ao subpacote sdl (cgo + SDL3) e registra as pontes
// do supervisor antes de abrir.
package gui

import "wydgo/cmd/server/gui/sdl"

// StartWindow abre a janela de administracao (bloqueia ate fechar).
func StartWindow(hostPort, password string) error {
	sdl.SetBridges(ProcState, ProcAction)
	return sdl.Start(hostPort, password)
}
