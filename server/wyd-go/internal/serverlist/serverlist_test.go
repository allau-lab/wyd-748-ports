package serverlist

import (
	"os"
	"path/filepath"
	"testing"
)

func TestRoundTripBytes(t *testing.T) {
	src := List{Groups: []Group{
		{Name: "WYD748", Servers: []string{"192.168.1.21", "10.0.0.5"}},
		{Name: "Teste", Servers: []string{"127.0.0.1"}},
	}}
	bin, err := src.Encode()
	if err != nil {
		t.Fatal(err)
	}
	if len(bin) != FileSize {
		t.Fatalf("tamanho %d; esperado %d", len(bin), FileSize)
	}
	dst, err := Decode(bin)
	if err != nil {
		t.Fatal(err)
	}
	if dst.Groups[0].Name != "WYD748" ||
		len(dst.Groups[0].Servers) != 2 ||
		dst.Groups[0].Servers[0] != "192.168.1.21" ||
		dst.Groups[0].Servers[1] != "10.0.0.5" {
		t.Fatalf("round-trip diferiu: %+v", dst.Groups[0])
	}
	if dst.Groups[1].Name != "Teste" || len(dst.Groups[1].Servers) != 1 {
		t.Fatalf("grupo 1 diferiu: %+v", dst.Groups[1])
	}
}

func TestLimits(t *testing.T) {
	// 11 grupos: acima do limite do client.
	tooMany := List{}
	for i := 0; i < Groups+1; i++ {
		tooMany.Groups = append(tooMany.Groups, Group{Name: "x"})
	}
	if _, err := tooMany.Encode(); err == nil {
		t.Fatal("encode aceitou mais grupos que o limite")
	}
	// 11 servidores num grupo: acima do limite.
	grp := Group{Name: "g"}
	for i := 0; i < SlotsPerGroup; i++ {
		grp.Servers = append(grp.Servers, "1.2.3.4")
	}
	if _, err := (&List{Groups: []Group{grp}}).Encode(); err == nil {
		t.Fatal("encode aceitou mais servidores que o limite")
	}
}

func TestClampLongStrings(t *testing.T) {
	long := ""
	for i := 0; i < 200; i++ {
		long += "a"
	}
	bin, err := (&List{Groups: []Group{{Name: long, Servers: []string{long}}}}).Encode()
	if err != nil {
		t.Fatal(err)
	}
	dst, err := Decode(bin)
	if err != nil {
		t.Fatal(err)
	}
	if got := dst.Groups[0].Name; len(got) > CellSize-1 {
		t.Fatalf("nome com %d bytes; maximo %d", len(got), CellSize-1)
	}
	if got := dst.Groups[0].Servers[0]; len(got) > CellSize-1 {
		t.Fatalf("ip com %d bytes; maximo %d", len(got), CellSize-1)
	}
}

func TestLatin1Transliteration(t *testing.T) {
	// Acentos PT-BR existem no latin-1: devem round-tripar intactos (a fonte
	// do client os tem). Caracteres FORA do latin-1 viram '?'.
	bin, err := (&List{Groups: []Group{{Name: "Servidorção ÁéÍ α"}}}).Encode()
	if err != nil {
		t.Fatal(err)
	}
	dst, err := Decode(bin)
	if err != nil {
		t.Fatal(err)
	}
	if got := dst.Groups[0].Name; got != "Servidorção ÁéÍ ?" {
		t.Fatalf("transliteracao: %q", got)
	}
}

func TestWriteFileRoundTrip(t *testing.T) {
	dir := t.TempDir()
	path := filepath.Join(dir, DefaultPath)
	src := List{Groups: []Group{
		{Name: "WYD748", Servers: []string{"192.168.1.21"}},
	}}
	if err := src.WriteFile(path); err != nil {
		t.Fatal(err)
	}
	st, err := os.Stat(path)
	if err != nil {
		t.Fatal(err)
	}
	if st.Size() != FileSize {
		t.Fatalf("arquivo com %d bytes; esperado %d", st.Size(), FileSize)
	}
	dst, err := ReadFile(path)
	if err != nil {
		t.Fatal(err)
	}
	if dst.Groups[0].Name != "WYD748" || dst.Groups[0].Servers[0] != "192.168.1.21" {
		t.Fatalf("releitura diferiu: %+v", dst.Groups[0])
	}
	// Nenhum tmp deve sobrar.
	if _, err := os.Stat(path + ".tmp"); !os.IsNotExist(err) {
		t.Fatal("arquivo temporario nao foi removido")
	}
}

func TestFromTemplate(t *testing.T) {
	l, err := FromTemplate("MeuWYD", "192.168.1.21:8281")
	if err != nil {
		t.Fatal(err)
	}
	if len(l.Groups) != Groups {
		t.Fatalf("template com %d grupos; esperado %d", len(l.Groups), Groups)
	}
	if l.Groups[0].Name != "MeuWYD" || l.Groups[0].Servers[0] != "192.168.1.21" {
		t.Fatalf("template: %+v", l.Groups[0])
	}
}
