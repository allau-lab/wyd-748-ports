package store

import (
	"context"
	"errors"
	"os"
	"path/filepath"
	"testing"

	"wydgo/internal/model"
)

func newTestSQLiteStore(t *testing.T) *SQLiteStore {
	t.Helper()
	s, err := NewSQLiteStore(context.Background(), SQLiteConfig{
		Path: filepath.Join(t.TempDir(), "wydgo.db"),
	})
	if err != nil {
		t.Fatal(err)
	}
	t.Cleanup(func() { s.Close() })
	return s
}

func TestSQLiteStoreAccountRoundTrip(t *testing.T) {
	s := newTestSQLiteStore(t)
	acc := &model.Account{Name: "felipe", PasswordHash: "hash", Chars: []model.Char{validStoredChar("felipe", 10)}}
	if err := s.SaveAccount(acc); err != nil {
		t.Fatal(err)
	}
	acc.Chars[0].Gold = 20
	if err := s.SaveAccount(acc); err != nil {
		t.Fatal(err)
	}
	loaded, err := s.LoadAccount("FELIPE") // case-insensitive
	if err != nil {
		t.Fatal(err)
	}
	if loaded.Chars[0].Gold != 20 {
		t.Fatalf("gold=%d, quer 20", loaded.Chars[0].Gold)
	}
}

func TestSQLiteStoreNameUniqueness(t *testing.T) {
	s := newTestSQLiteStore(t)
	acc := &model.Account{Name: "alice", PasswordHash: "hash", Chars: []model.Char{validStoredChar("Alice", 100)}}
	if err := s.CreateAccount(acc); err != nil {
		t.Fatal(err)
	}
	if err := s.CreateAccount(acc); !errors.Is(err, ErrAccountExists) {
		t.Fatalf("quer ErrAccountExists, veio %v", err)
	}
	if ok, _ := s.AccountNameExists("ALICE"); !ok {
		t.Fatal("conta alice deveria existir")
	}
	if ok, _ := s.CharacterNameExists("alice"); !ok {
		t.Fatal("personagem alice deveria existir")
	}
	if ok, _ := s.CharacterNameExists("bob"); ok {
		t.Fatal("bob nao deveria existir")
	}
	names, err := s.CharacterNames()
	if err != nil {
		t.Fatal(err)
	}
	if _, ok := names["alice"]; !ok {
		t.Fatalf("indice de nomes sem alice: %v", names)
	}
}

func TestSQLiteStoreGuildTransaction(t *testing.T) {
	s := newTestSQLiteStore(t)
	acc := &model.Account{Name: "gilmar", PasswordHash: "hash"}
	if err := s.CreateAccount(acc); err != nil {
		t.Fatal(err)
	}
	registry := &model.GuildRegistry{Version: model.GuildRegistryVersion}
	if err := s.SaveGameState(registry, acc); err != nil {
		t.Fatal(err)
	}
	loaded, err := s.LoadGuilds()
	if err != nil {
		t.Fatal(err)
	}
	if loaded.Version != model.GuildRegistryVersion {
		t.Fatalf("versao guild=%d", loaded.Version)
	}
}

func TestSQLiteStoreCharStateRoundTrip(t *testing.T) {
	s := newTestSQLiteStore(t)
	if err := s.SaveCharState("uid-123", &model.CharState{Version: model.CharStateVersion}); err != nil {
		t.Fatal(err)
	}
	if err := s.SaveCharState("uid-123", &model.CharState{Version: model.CharStateVersion}); err != nil {
		t.Fatal(err)
	}
	// Estado vazio => remocao (mesma semantica do JSONStore).
	if err := s.SaveCharState("uid-123", nil); err != nil {
		t.Fatal(err)
	}
	state, err := s.LoadCharState("uid-123")
	if err != nil {
		t.Fatal(err)
	}
	if state != nil {
		t.Fatalf("charstate vazio deveria ser removido, veio %+v", state)
	}
}

func TestSQLiteStoreDeleteAccountCascades(t *testing.T) {
	s := newTestSQLiteStore(t)
	acc := &model.Account{Name: "deletar", PasswordHash: "hash", Chars: []model.Char{validStoredChar("Deletar", 1)}}
	if err := s.CreateAccount(acc); err != nil {
		t.Fatal(err)
	}
	if err := s.DeleteAccount("deletar"); err != nil {
		t.Fatal(err)
	}
	if _, err := s.LoadAccount("deletar"); !os.IsNotExist(err) {
		t.Fatalf("quer ErrNotExist, veio %v", err)
	}
	if ok, _ := s.CharacterNameExists("deletar"); ok {
		t.Fatal("nome deveria ser liberado apos delete")
	}
}

func TestSQLiteStoreMigrateFromJSON(t *testing.T) {
	root := t.TempDir()
	accDir := filepath.Join(root, "accounts")
	if err := os.MkdirAll(accDir, 0o755); err != nil {
		t.Fatal(err)
	}
	jsonStore := NewJSONStore(accDir)
	if err := jsonStore.CreateAccount(&model.Account{
		Name: "migrada", PasswordHash: "hash",
		Chars: []model.Char{validStoredChar("Migrada", 42)},
	}); err != nil {
		t.Fatal(err)
	}
	jsonStore.Flush()

	dbPath := filepath.Join(root, "wydgo.db")
	imported, err := MigrateJSONToSQLite(context.Background(), accDir, dbPath)
	if err != nil {
		t.Fatal(err)
	}
	if imported != 1 {
		t.Fatalf("quer 1 conta migrada, veio %d", imported)
	}
	s, err := NewSQLiteStore(context.Background(), SQLiteConfig{Path: dbPath})
	if err != nil {
		t.Fatal(err)
	}
	defer s.Close()
	loaded, err := s.LoadAccount("migrada")
	if err != nil {
		t.Fatal(err)
	}
	if loaded.Chars[0].Name != "Migrada" || loaded.Chars[0].Gold != 42 {
		t.Fatalf("conta migrada com dados errados: %+v", loaded.Chars[0])
	}
	sums, err := s.ListAccountSummaries(context.Background())
	if err != nil || len(sums) != 1 {
		t.Fatalf("summaries: %v err=%v", sums, err)
	}
}
