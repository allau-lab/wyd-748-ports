// Package gui: controla o processo do servidor para a janela de
// administracao. No build `-tags gui` a janela SDL3 vive no subpacote sdl;
// sem a tag StartWindow falha (headless/VPS).
package gui

// ChildEnvVar marca o processo-filho do supervisor.
const ChildEnvVar = "__wyd_child"

// IsChild diz se este processo foi spawnado pelo supervisor.
func IsChild() bool {
	// supervisor.go define a checagem real (Go puro, sempre compilado).
	return isChildEnv()
}
