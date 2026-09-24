// Package serverlist escreve o serverlist.bin consumido pelo client 7.48.
//
// Formato legado confirmado no source do client (BASE_InitializeServerList em
// TMProject748/internal/core/Basedef.cpp): um bloco de 10 grupos x 11 slots x
// 64 bytes lido com fread(ptr, 0x6E, 0x40) — os bytes sobressalentes do stride
// de 110 (0x6E) ficam zerados e nao sao usados. Cada string tem no maximo 63
// bytes uteis (slot de 64 sem garantia de NUL no ultimo). Grupo g usa
// g_pServerList[g][0] como NOME exibido na selecao e g_pServerList[g][1..10]
// como IP/porta de cada servidor da lista. A cifra e aditiva latin-1:
//
//	decodificar: byte -= key[63 - (i % 64)]
//	codificar:   byte += key[63 - (i % 64)]
//
// Toda a aritmetica e sobre bytes (wrap de uint8), exatamente como o C faz
// com char sem sinal. Textos sao latin-1: acentos do Go (UTF-8) sao
// transliterados para nao corromper a fonte bitmap do client.
package serverlist

import (
	"errors"
	"fmt"
	"os"
	"path/filepath"
	"strings"
)

const (
	// Groups e o MAX_SERVERGROUP do client.
	Groups = 10
	// SlotsPerGroup e o MAX_SERVERNUMBER do client (MAX_SERVER+1): slot 0 =
	// nome do grupo, slots 1..10 = servidores.
	SlotsPerGroup = 11
	// CellSize e o stride de cada string (64).
	CellSize = 64
	// Stride e o tamanho de leitura por slot no client (0x6E = 110 bytes).
	Stride = 110
	// FileSize e o tamanho exato do arquivo que o client le (7040 bytes).
	FileSize = Groups * SlotsPerGroup * CellSize

	// DefaultPath e o arquivo que o client abre com fopen("./serverlist.bin").
	DefaultPath = "serverlist.bin"
)

// key e a chave de cifra extraida do binario Win32 (CP1252/latin-1).
var key = [64]byte{
	0xA4, 0xA1, 0xA4, 0xA4, 0xA4, 0xA7, 0xA4, 0xA9, 0xA4, 0xB1, 0xA4, 0xB2, 0xA4, 0xB5, 0xA4, 0xB7,
	0xA4, 0xB8, 0xA4, 0xBA, 0xA4, 0xBB, 0xA4, 0xBC, 0xA4, 0xBD, 0xA4, 0xBE, 0xA4, 0xBF, 0xA4, 0xC1,
	0xA4, 0xC3, 0xA4, 0xC5, 0xA4, 0xC7, 0xA4, 0xCB, 0xA4, 0xCC, 0xA4, 0xD0, 0xA4, 0xD1, 0xA4, 0xD3,
	0xA4, 0xBF, 0xA4, 0xC4, 0xA4, 0xD3, 0xA4, 0xC7, 0xA4, 0xCC, 0xB0, 0xA1, 0xB3, 0xAA, 0xB4, 0xD9,
}

// Entry e um servidor da lista (ou o nome de um grupo).
type Entry struct {
	Name string `json:"name"`
	IP   string `json:"ip,omitempty"`
}

// Group e a linha exibida na tela de selecao do client: um nome e ate 10
// servidores. Groups vazio aparece em branco no client.
type Group struct {
	Name    string   `json:"name"`
	Servers []string `json:"servers"`
}

// List e a lista completa (10 grupos fixos, para o formato do client).
type List struct {
	Groups []Group `json:"groups"`
}

// decode latin-1 -> unicode rune por rune (1 byte = 1 rune).
func latin1ToString(b []byte) string {
	runes := make([]rune, 0, len(b))
	for _, c := range b {
		if c == 0 {
			break
		}
		runes = append(runes, rune(c))
	}
	return string(runes)
}

// translitera os caracteres que existem no CP1252 para caber na fonte do
// client (que so tem latin-1). O que nao tem traducao vira '?'.
func toLatin1(s string) []byte {
	repl := map[rune]byte{
		'á': 0xE1, 'à': 0xE0, 'â': 0xE2, 'ã': 0xE3, 'ä': 0xE4,
		'é': 0xE9, 'è': 0xE8, 'ê': 0xEA, 'ë': 0xEB,
		'í': 0xED, 'ì': 0xEC, 'î': 0xEE, 'ï': 0xEF,
		'ó': 0xF3, 'ò': 0xF2, 'ô': 0xF4, 'õ': 0xF5, 'ö': 0xF6,
		'ú': 0xFA, 'ù': 0xF9, 'û': 0xFB, 'ü': 0xFC,
		'ç': 0xE7, 'ñ': 0xF1,
		'Á': 0xC1, 'À': 0xC0, 'Â': 0xC2, 'Ã': 0xC3, 'Ä': 0xC4,
		'É': 0xC9, 'È': 0xC8, 'Ê': 0xCA, 'Ë': 0xCB,
		'Í': 0xCD, 'Ì': 0xCC, 'Î': 0xCE, 'Ï': 0xCF,
		'Ó': 0xD3, 'Ò': 0xD2, 'Ô': 0xD4, 'Õ': 0xD5, 'Ö': 0xD6,
		'Ú': 0xDA, 'Ù': 0xD9, 'Û': 0xDB, 'Ü': 0xDC,
		'Ç': 0xC7, 'Ñ': 0xD1,
	}
	out := make([]byte, 0, len(s))
	for _, r := range s {
		switch {
		case r < 128:
			out = append(out, byte(r))
		case repl[r] != 0:
			out = append(out, repl[r])
		default:
			out = append(out, '?')
		}
	}
	return out
}

// clampString corta para caber numa celula de 64 bytes (63 uteis + NUL).
func clampString(s string) string {
	b := toLatin1(s)
	if len(b) > CellSize-1 {
		b = b[:CellSize-1]
	}
	return latin1ToString(b)
}

// Encode monta os 7040 bytes cifrados a partir da lista. Grupos/servidores
// alem do limite sao rejeitados (nao silenciosamente descartados: o operador
// precisa saber que a lista nao coube).
func (l *List) Encode() ([]byte, error) {
	if len(l.Groups) > Groups {
		return nil, fmt.Errorf("serverlist: %d grupos excedem o limite do client (%d)",
			len(l.Groups), Groups)
	}
	plain := make([]byte, FileSize)
	for g, grp := range l.Groups {
		if len(grp.Servers) > SlotsPerGroup-1 {
			return nil, fmt.Errorf("serverlist: grupo %q tem %d servidores; o client aceita %d por grupo",
				grp.Name, len(grp.Servers), SlotsPerGroup-1)
		}
		base := g * SlotsPerGroup * CellSize
		copy(plain[base:base+CellSize], toLatin1(clampString(grp.Name)))
		for s, ip := range grp.Servers {
			off := base + (s+1)*CellSize
			copy(plain[off:off+CellSize], toLatin1(clampString(strings.TrimSpace(ip))))
		}
	}
	out := make([]byte, FileSize)
	for i, b := range plain {
		out[i] = byte(int(b) + int(key[63-(i%64)])) // wrap uint8, igual ao C
	}
	return out, nil
}

// Decode le o arquivo cifrado e devolve a lista interpretada (para edicao).
// Aceita qualquer tamanho >= FileSize; o client sempre le exatamente FileSize.
func Decode(data []byte) (*List, error) {
	if len(data) < FileSize {
		return nil, fmt.Errorf("serverlist: arquivo com %d bytes; o client le %d", len(data), FileSize)
	}
	plain := make([]byte, FileSize)
	for i := 0; i < FileSize; i++ {
		plain[i] = byte(int(data[i]) - int(key[63-(i%64)]))
	}
	l := &List{Groups: make([]Group, 0, Groups)}
	for g := 0; g < Groups; g++ {
		base := g * SlotsPerGroup * CellSize
		name := strings.TrimSpace(latin1ToString(plain[base : base+CellSize]))
		grp := Group{Name: name}
		for s := 1; s < SlotsPerGroup; s++ {
			off := base + s*CellSize
			ip := strings.TrimSpace(latin1ToString(plain[off : off+CellSize]))
			if ip != "" {
				grp.Servers = append(grp.Servers, ip)
			}
		}
		l.Groups = append(l.Groups, grp)
	}
	return l, nil
}

// ReadFile decodifica o serverlist.bin do caminho dado.
func ReadFile(path string) (*List, error) {
	data, err := os.ReadFile(path)
	if err != nil {
		return nil, err
	}
	return Decode(data)
}

// WriteFile codifica e grava atomicamente (tmp + rename), criando o diretorio
// se necessario. Permissoes 0644: o client le o arquivo, nao e segredo.
func (l *List) WriteFile(path string) error {
	bin, err := l.Encode()
	if err != nil {
		return err
	}
	if dir := filepath.Dir(path); dir != "." && dir != "" {
		if err := os.MkdirAll(dir, 0o755); err != nil {
			return err
		}
	}
	tmp := path + ".tmp"
	if err := os.WriteFile(tmp, bin, 0o644); err != nil {
		return err
	}
	if err := os.Rename(tmp, path); err != nil {
		os.Remove(tmp)
		return err
	}
	return nil
}

// FromTemplate gera a lista padrao a partir de um nome de grupo e um
// host:porta do servidor (ex.: "WYD748", "192.168.1.21:8281"). O client nao
// le porta daqui (porta fixa da ABI), entao a porta e validada e descartada —
// serve so para o operador conferir de onde veio o IP.
func FromTemplate(groupName, hostPort string) (*List, error) {
	host := strings.TrimSpace(hostPort)
	if host == "" {
		return nil, errors.New("serverlist: host vazio")
	}
	if h, _, err := splitHostPort(hostPort); err == nil && h != "" {
		host = h
	}
	l := &List{}
	for g := 0; g < 2; g++ { // os dois primeiros grupos com o mesmo servidor
		l.Groups = append(l.Groups, Group{
			Name:    groupName,
			Servers: []string{host},
		})
	}
	for g := 2; g < Groups; g++ {
		l.Groups = append(l.Groups, Group{})
	}
	return l, nil
}

func splitHostPort(s string) (string, string, error) {
	i := strings.LastIndex(s, ":")
	if i < 0 {
		return s, "", nil
	}
	return s[:i], s[i+1:], nil
}
