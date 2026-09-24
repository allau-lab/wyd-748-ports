package store

import (
	"bytes"
	"context"
	"database/sql"
	"encoding/json"
	"errors"
	"fmt"
	"log"
	"os"
	"path/filepath"
	"strings"
	"sync"
	"time"

	_ "modernc.org/sqlite" // driver pure-Go (sem CGo)

	"wydgo/internal/model"
)

// ErrSQLiteClosed e devolvida quando o store ja foi fechado.
var ErrSQLiteClosed = errors.New("store: sqlite fechado")

// SQLiteConfig configura o store embutido.
type SQLiteConfig struct {
	// Path do arquivo do banco (ex.: data/wydgo.db). Obrigatorio.
	Path string
	// GuildsTxtPath e o artefato DERIVADO para o client 7.48 (Guilds.txt).
	// Vazio desliga a exportacao, igual ao JSONStore.
	GuildsTxtPath string
}

// SQLiteStore e a persistencia EMBUTIDA (SQLite/WAL) — substitui a gestao por
// arquivos espalhados: contas, estado de guild, estado de sessao e agregados
// vivem num unico banco transacional. O payload da conta continua o documento
// JSON autoritativo (mesma forma do PostgreSQL), com tabelas de indice para
// consultas administrativas.
type SQLiteStore struct {
	db *sql.DB

	// writeMu serializa transacoes de escrita (SQLite admite um escritor).
	writeMu sync.Mutex

	// itemOwners e o indice global de unicidade de itens offline, igual ao
	// JSONStore (UID nao vai ao wire).
	itemOwners map[string]itemUIDOwner

	guildsTxtPath string

	// Fila de autosave: mesma forma da JSONStore (coalescing por chave).
	writeQueue chan writeJob
	queueMu    sync.Mutex
	overflow   []writeJob

	closeOnce sync.Once
	closed    chan struct{}
}

// SQLOption configura o SQLiteStore.
type SQLOption func(*SQLiteStore)

// WithSQLiteGuildsTxt liga a exportacao do Guilds.txt derivado.
func WithSQLiteGuildsTxt(path string) SQLOption {
	return func(s *SQLiteStore) { s.guildsTxtPath = path }
}

const sqliteSchema = `
CREATE TABLE IF NOT EXISTS accounts (
	name_key   TEXT PRIMARY KEY,
	payload    TEXT NOT NULL,
	version    INTEGER NOT NULL DEFAULT 1,
	created_at INTEGER NOT NULL DEFAULT (unixepoch()),
	updated_at INTEGER NOT NULL DEFAULT (unixepoch()),
	CHECK (name_key = lower(name_key) AND name_key <> '')
);

CREATE TABLE IF NOT EXISTS character_names (
	name_key    TEXT PRIMARY KEY,
	account_key TEXT NOT NULL REFERENCES accounts(name_key) ON DELETE CASCADE,
	CHECK (name_key = lower(name_key) AND name_key <> '')
);
CREATE INDEX IF NOT EXISTS character_names_account_idx ON character_names(account_key);

CREATE TABLE IF NOT EXISTS characters (
	character_uid TEXT PRIMARY KEY,
	account_key   TEXT NOT NULL REFERENCES accounts(name_key) ON DELETE CASCADE,
	slot          INTEGER NOT NULL CHECK (slot BETWEEN 0 AND 7),
	name_key      TEXT NOT NULL CHECK (name_key = lower(name_key) AND name_key <> ''),
	UNIQUE (account_key, slot)
);
CREATE INDEX IF NOT EXISTS characters_account_idx ON characters(account_key);

CREATE TABLE IF NOT EXISTS char_states (
	character_uid TEXT PRIMARY KEY,
	payload       TEXT NOT NULL,
	updated_at    INTEGER NOT NULL DEFAULT (unixepoch())
);

CREATE TABLE IF NOT EXISTS guild_state (
	id         INTEGER PRIMARY KEY CHECK (id = 1),
	payload    TEXT NOT NULL,
	updated_at INTEGER NOT NULL DEFAULT (unixepoch())
);

CREATE TABLE IF NOT EXISTS instance_state (
	id         INTEGER PRIMARY KEY CHECK (id = 1),
	payload    TEXT NOT NULL,
	updated_at INTEGER NOT NULL DEFAULT (unixepoch())
);

-- Gestao administrativa: contas banidas, config runtime e auditoria.
CREATE TABLE IF NOT EXISTS account_bans (
	account_key TEXT PRIMARY KEY,
	reason      TEXT NOT NULL DEFAULT '',
	banned_at   INTEGER NOT NULL DEFAULT (unixepoch()),
	expires_at  INTEGER NOT NULL DEFAULT 0
);

CREATE TABLE IF NOT EXISTS kv_config (
	key        TEXT PRIMARY KEY,
	value      TEXT NOT NULL,
	updated_at INTEGER NOT NULL DEFAULT (unixepoch())
);

CREATE TABLE IF NOT EXISTS audit_log (
	id     INTEGER PRIMARY KEY AUTOINCREMENT,
	at     INTEGER NOT NULL DEFAULT (unixepoch()),
	actor  TEXT NOT NULL DEFAULT '',
	action TEXT NOT NULL,
	detail TEXT NOT NULL DEFAULT ''
);
CREATE INDEX IF NOT EXISTS audit_log_at_idx ON audit_log(at);
`

// NewSQLiteStore abre (ou cria) o banco embutido e aplica o schema. Na abertura
// reconstrói o índice de UIDs a partir dos payloads persistidos.
func NewSQLiteStore(ctx context.Context, cfg SQLiteConfig, opts ...SQLOption) (*SQLiteStore, error) {
	if strings.TrimSpace(cfg.Path) == "" {
		return nil, errors.New("store: caminho do banco sqlite vazio")
	}
	s := &SQLiteStore{closed: make(chan struct{})}
	for _, opt := range opts {
		opt(s)
	}
	db, err := sql.Open("sqlite", cfg.Path)
	if err != nil {
		return nil, fmt.Errorf("store: abrir sqlite %q: %w", cfg.Path, err)
	}
	// Conexoes extra so dividem o cache; o escritor e unico por design.
	db.SetMaxOpenConns(1)
	pragmas := []string{
		"PRAGMA journal_mode=WAL",
		"PRAGMA synchronous=NORMAL",
		"PRAGMA busy_timeout=5000",
		"PRAGMA foreign_keys=ON",
	}
	for _, p := range pragmas {
		if _, err := db.ExecContext(ctx, p); err != nil {
			db.Close()
			return nil, fmt.Errorf("store: pragma %q: %w", p, err)
		}
	}
	if _, err := db.ExecContext(ctx, sqliteSchema); err != nil {
		db.Close()
		return nil, fmt.Errorf("store: schema sqlite: %w", err)
	}
	s.db = db
	if err := s.initializeItemUIDs(ctx); err != nil {
		db.Close()
		return nil, err
	}
	s.writeQueue = make(chan writeJob, 256)
	go s.persistLoop()
	return s, nil
}

// Ping verifica o banco (health check do admin).
func (s *SQLiteStore) Ping(ctx context.Context) error {
	if err := s.checkOpen(); err != nil {
		return err
	}
	return s.db.PingContext(ctx)
}

func (s *SQLiteStore) checkOpen() error {
	select {
	case <-s.closed:
		return ErrSQLiteClosed
	default:
		return nil
	}
}

// initializeItemUIDs reconstrói o indice global de itens offline a partir dos
// payloads; UIDs ausentes sao gerados e persistidos imediatamente (mesma
// migracao unica do boot do JSONStore).
func (s *SQLiteStore) initializeItemUIDs(ctx context.Context) error {
	s.itemOwners = make(map[string]itemUIDOwner)
	rows, err := s.db.QueryContext(ctx, `SELECT name_key, payload FROM accounts`)
	if err != nil {
		return fmt.Errorf("store: indexar UIDs: %w", err)
	}
	defer rows.Close()
	type pending struct {
		key     string
		payload []byte
	}
	var changedAccounts []pending
	migratedItems := 0
	for rows.Next() {
		var key, payload string
		if err := rows.Scan(&key, &payload); err != nil {
			return fmt.Errorf("store: indexar UIDs: %w", err)
		}
		var acc model.Account
		if err := decodeAccountJSON([]byte(payload), &acc); err != nil {
			return fmt.Errorf("store: indexar UIDs em %q: %w", key, err)
		}
		charactersChanged, err := prepareAccountCharacterUIDs(&acc)
		if err != nil {
			return err
		}
		if err := acc.Validate(); err != nil {
			return fmt.Errorf("store: indexar UIDs em %q: %w", key, err)
		}
		next, changed, err := prepareItemUIDsInto(s.itemOwners, &acc)
		if err != nil {
			return err
		}
		if changed != 0 || charactersChanged != 0 {
			encoded, err := json.MarshalIndent(&acc, "", "  ")
			if err != nil {
				return err
			}
			changedAccounts = append(changedAccounts, pending{key: key, payload: encoded})
			migratedItems += changed
		}
		s.itemOwners = next
	}
	if err := rows.Err(); err != nil {
		return fmt.Errorf("store: indexar UIDs: %w", err)
	}
	for _, p := range changedAccounts {
		if _, err := s.db.ExecContext(ctx,
			`UPDATE accounts SET payload = ?, updated_at = unixepoch() WHERE name_key = ?`,
			string(p.payload), p.key); err != nil {
			return fmt.Errorf("store: migrar UIDs em %q: %w", p.key, err)
		}
	}
	if migratedItems != 0 {
		log.Printf("store sqlite: %d item(ns) com UID migrado", migratedItems)
	}
	return nil
}

// ---- persistencia assincrona (autosave), mesmo padrao do JSONStore ----

func (s *SQLiteStore) persistLoop() {
	for {
		select {
		case <-s.closed:
			return
		default:
		}
		job, ok := s.nextWrite()
		if !ok {
			return
		}
		if job.run != nil {
			job.run()
		}
		if job.done != nil {
			close(job.done)
		}
	}
}

func (s *SQLiteStore) nextWrite() (writeJob, bool) {
	select {
	case job, ok := <-s.writeQueue:
		return job, ok
	default:
	}
	s.queueMu.Lock()
	if len(s.overflow) > 0 {
		job := s.overflow[0]
		copy(s.overflow, s.overflow[1:])
		s.overflow[len(s.overflow)-1] = writeJob{}
		s.overflow = s.overflow[:len(s.overflow)-1]
		s.queueMu.Unlock()
		return job, true
	}
	s.queueMu.Unlock()
	select {
	case job, ok := <-s.writeQueue:
		return job, ok
	case <-s.closed:
		return writeJob{}, false
	}
}

func (s *SQLiteStore) enqueueWrite(job writeJob) {
	if s.writeQueue == nil {
		if job.run != nil {
			job.run()
		} else if job.done != nil {
			close(job.done)
		}
		return
	}
	s.queueMu.Lock()
	defer s.queueMu.Unlock()
	if len(s.overflow) == 0 {
		select {
		case s.writeQueue <- job:
			return
		default:
		}
	}
	if job.key != "" {
		for i := range s.overflow {
			if s.overflow[i].key == job.key {
				s.overflow[i] = job
				return
			}
		}
	}
	s.overflow = append(s.overflow, job)
}

func (s *SQLiteStore) flushWrites() {
	if s.writeQueue == nil {
		return
	}
	done := make(chan struct{})
	s.enqueueWrite(writeJob{done: done})
	select {
	case <-done:
	case <-s.closed:
	}
}

// Flush barreira do desligamento controlado.
func (s *SQLiteStore) Flush() { s.flushWrites() }

// Close drena a fila e fecha o banco.
func (s *SQLiteStore) Close() error {
	var err error
	s.closeOnce.Do(func() {
		close(s.closed)
		s.flushWrites()
		err = s.db.Close()
	})
	return err
}

// ---- indices/consultas de nome ----

func (s *SQLiteStore) AccountNameExists(name string) (bool, error) {
	if err := s.checkOpen(); err != nil {
		return false, err
	}
	var exists bool
	err := s.db.QueryRow(`SELECT EXISTS (SELECT 1 FROM accounts WHERE name_key = lower(?))`,
		name).Scan(&exists)
	return exists, err
}

func (s *SQLiteStore) CharacterNameExists(name string) (bool, error) {
	if err := s.checkOpen(); err != nil {
		return false, err
	}
	var exists bool
	err := s.db.QueryRow(
		`SELECT EXISTS (SELECT 1 FROM character_names WHERE name_key = lower(?))`,
		name).Scan(&exists)
	return exists, err
}

func (s *SQLiteStore) CharacterNames() (map[string]struct{}, error) {
	if err := s.checkOpen(); err != nil {
		return nil, err
	}
	names := make(map[string]struct{})
	rows, err := s.db.Query(`SELECT name_key FROM character_names`)
	if err != nil {
		return nil, err
	}
	defer rows.Close()
	for rows.Next() {
		var key string
		if err := rows.Scan(&key); err != nil {
			return nil, err
		}
		names[key] = struct{}{}
	}
	return names, rows.Err()
}

// ---- contas ----

// accountRow persiste o documento JSON + indices derivados numa transacao.
func (s *SQLiteStore) writeAccountTx(ctx context.Context, tx *sql.Tx, acc *model.Account) error {
	b, err := json.MarshalIndent(acc, "", "  ")
	if err != nil {
		return err
	}
	key := strings.ToLower(acc.Name)
	if _, err := tx.ExecContext(ctx, `
		INSERT INTO accounts (name_key, payload) VALUES (?, ?)
		ON CONFLICT(name_key) DO UPDATE SET payload = excluded.payload,
			version = accounts.version + 1, updated_at = unixepoch()
	`, key, string(b)); err != nil {
		return err
	}
	if _, err := tx.ExecContext(ctx,
		`DELETE FROM character_names WHERE account_key = ?`, key); err != nil {
		return err
	}
	if _, err := tx.ExecContext(ctx,
		`DELETE FROM characters WHERE account_key = ?`, key); err != nil {
		return err
	}
	for slot := range acc.Chars {
		ch := &acc.Chars[slot]
		if ch.Name == "" || ch.UID == "" {
			continue
		}
		if _, err := tx.ExecContext(ctx,
			`INSERT OR IGNORE INTO character_names (name_key, account_key) VALUES (lower(?), ?)`,
			ch.Name, key); err != nil {
			return err
		}
		if _, err := tx.ExecContext(ctx, `
			INSERT INTO characters (character_uid, account_key, slot, name_key)
			VALUES (?, ?, ?, lower(?))
			ON CONFLICT(character_uid) DO UPDATE SET account_key = excluded.account_key,
				slot = excluded.slot, name_key = excluded.name_key
		`, ch.UID, key, slot, ch.Name); err != nil {
			return err
		}
	}
	return nil
}

func (s *SQLiteStore) CreateAccount(acc *model.Account) error {
	if err := s.checkOpen(); err != nil {
		return err
	}
	s.writeMu.Lock()
	defer s.writeMu.Unlock()
	// Gera UIDs de personagem/itens ANTES de validar: o Validate exige identidade
	// materializada para chars novos.
	next, _, err := prepareItemUIDsInto(s.itemOwners, acc)
	if err != nil {
		return err
	}
	if err := acc.Validate(); err != nil {
		return fmt.Errorf("store: criar conta: %w", err)
	}
	exists, err := s.AccountNameExists(acc.Name)
	if err != nil {
		return err
	}
	if exists {
		return ErrAccountExists
	}
	ctx := context.Background()
	tx, err := s.db.BeginTx(ctx, nil)
	if err != nil {
		return err
	}
	defer tx.Rollback()
	if err := s.writeAccountTx(ctx, tx, acc); err != nil {
		return err
	}
	if err := tx.Commit(); err != nil {
		return err
	}
	s.itemOwners = next
	return nil
}

func (s *SQLiteStore) LoadAccount(name string) (*model.Account, error) {
	if err := s.checkOpen(); err != nil {
		return nil, err
	}
	var payload string
	err := s.db.QueryRow(`SELECT payload FROM accounts WHERE name_key = lower(?)`, name).
		Scan(&payload)
	if errors.Is(err, sql.ErrNoRows) {
		return nil, os.ErrNotExist
	}
	if err != nil {
		return nil, err
	}
	var acc model.Account
	if err := decodeAccountJSON([]byte(payload), &acc); err != nil {
		return nil, fmt.Errorf("store: parse conta %q: %w", name, err)
	}
	if err := acc.Validate(); err != nil {
		return nil, fmt.Errorf("store: validar conta %q: %w", name, err)
	}
	return &acc, nil
}

func (s *SQLiteStore) SaveAccount(acc *model.Account) error {
	if err := s.checkOpen(); err != nil {
		return err
	}
	s.writeMu.Lock()
	defer s.writeMu.Unlock()
	next, _, err := prepareItemUIDsInto(s.itemOwners, acc)
	if err != nil {
		return err
	}
	if err := acc.Validate(); err != nil {
		return fmt.Errorf("store: salvar conta: %w", err)
	}
	s.flushWrites()
	ctx := context.Background()
	tx, err := s.db.BeginTx(ctx, nil)
	if err != nil {
		return err
	}
	defer tx.Rollback()
	if err := s.writeAccountTx(ctx, tx, acc); err != nil {
		return err
	}
	if err := tx.Commit(); err != nil {
		return err
	}
	s.itemOwners = next
	return nil
}

func (s *SQLiteStore) SaveAccountAsync(acc *model.Account) error {
	if err := s.checkOpen(); err != nil {
		return err
	}
	s.writeMu.Lock()
	next, _, err := prepareItemUIDsInto(s.itemOwners, acc)
	if err != nil {
		s.writeMu.Unlock()
		return err
	}
	if err := acc.Validate(); err != nil {
		s.writeMu.Unlock()
		return fmt.Errorf("store: autosave conta: %w", err)
	}
	name := acc.Name
	s.itemOwners = next
	s.writeMu.Unlock()
	s.enqueueWrite(writeJob{key: "account:" + name, run: func() {
		s.writeMu.Lock()
		defer s.writeMu.Unlock()
		ctx := context.Background()
		tx, err := s.db.BeginTx(ctx, nil)
		if err != nil {
			log.Printf("store: autosave conta %q: %v", name, err)
			return
		}
		defer tx.Rollback()
		if err := s.writeAccountTx(ctx, tx, acc); err != nil {
			log.Printf("store: autosave conta %q: %v", name, err)
			return
		}
		if err := tx.Commit(); err != nil {
			log.Printf("store: autosave conta %q: %v", name, err)
		}
	}})
	return nil
}

// exportGuildsTxt regrava o artefato derivado do client 7.48 (igual ao JSONStore).
func (s *SQLiteStore) exportGuildsTxt(guilds *model.GuildRegistry) error {
	if s.guildsTxtPath == "" {
		return nil
	}
	// Instancia unica: grupo e canal 0, que casa com o WORD (canal<<12)|id
	// enviado no wire.
	return writeFileAtomic(s.guildsTxtPath, guilds.GuildsTxt(0, 0))
}

// ---- guild + transacao multi-conta ----

func (s *SQLiteStore) SaveAccounts(accounts ...*model.Account) error {
	return s.SaveGameState(nil, accounts...)
}

func (s *SQLiteStore) SaveGameState(guilds *model.GuildRegistry, accounts ...*model.Account) error {
	return s.saveGameState(nil, guilds, accounts...)
}

func (s *SQLiteStore) SaveGameStateWithInstanceState(guilds *model.GuildRegistry,
	state *model.InstanceStateSnapshot, accounts ...*model.Account) error {
	return s.saveGameState(state, guilds, accounts...)
}

func (s *SQLiteStore) saveGameState(state *model.InstanceStateSnapshot,
	guilds *model.GuildRegistry, accounts ...*model.Account) error {
	if err := s.checkOpen(); err != nil {
		return err
	}
	s.writeMu.Lock()
	defer s.writeMu.Unlock()
	if state == nil && guilds == nil && len(accounts) == 0 {
		return nil
	}
	if state != nil && state.Version != model.InstanceStateVersion {
		return fmt.Errorf("store: estado de instancias versao %d; esperado %d",
			state.Version, model.InstanceStateVersion)
	}
	if guilds != nil {
		if err := guilds.Validate(); err != nil {
			return fmt.Errorf("store: salvar guilds: %w", err)
		}
	}
	var next map[string]itemUIDOwner
	if len(accounts) != 0 {
		var err error
		if next, _, err = prepareItemUIDsInto(s.itemOwners, accounts...); err != nil {
			return err
		}
	}
	seen := make(map[string]struct{}, len(accounts))
	type accountDoc struct {
		key string
		b   []byte
	}
	docs := make([]accountDoc, 0, len(accounts))
	for _, acc := range accounts {
		if acc == nil || acc.Name == "" {
			return errors.New("store: conta invalida na transacao")
		}
		key := strings.ToLower(acc.Name)
		if _, duplicate := seen[key]; duplicate {
			return errors.New("store: conta duplicada na transacao")
		}
		if err := acc.Validate(); err != nil {
			return fmt.Errorf("store: salvar transacao: %w", err)
		}
		seen[key] = struct{}{}
		b, err := json.MarshalIndent(acc, "", "  ")
		if err != nil {
			return err
		}
		docs = append(docs, accountDoc{key: key, b: b})
	}

	ctx := context.Background()
	tx, err := s.db.BeginTx(ctx, nil)
	if err != nil {
		return err
	}
	defer tx.Rollback()
	for _, doc := range docs {
		if _, err := tx.ExecContext(ctx, `
			INSERT INTO accounts (name_key, payload) VALUES (?, ?)
			ON CONFLICT(name_key) DO UPDATE SET payload = excluded.payload,
				version = accounts.version + 1, updated_at = unixepoch()
		`, doc.key, string(doc.b)); err != nil {
			return err
		}
		var acc model.Account
		if err := decodeAccountJSON(doc.b, &acc); err != nil {
			return err
		}
		if err := s.writeAccountTx(ctx, tx, &acc); err != nil {
			return err
		}
	}
	if guilds != nil {
		b, err := json.MarshalIndent(guilds, "", "  ")
		if err != nil {
			return err
		}
		if _, err := tx.ExecContext(ctx, `
			INSERT INTO guild_state (id, payload) VALUES (1, ?)
			ON CONFLICT(id) DO UPDATE SET payload = excluded.payload, updated_at = unixepoch()
		`, string(b)); err != nil {
			return err
		}
	}
	if state != nil {
		b, err := json.MarshalIndent(state, "", "  ")
		if err != nil {
			return err
		}
		if _, err := tx.ExecContext(ctx, `
			INSERT INTO instance_state (id, payload) VALUES (1, ?)
			ON CONFLICT(id) DO UPDATE SET payload = excluded.payload, updated_at = unixepoch()
		`, string(b)); err != nil {
			return err
		}
	}
	if err := tx.Commit(); err != nil {
		return err
	}
	if next != nil {
		s.itemOwners = next
	}
	if guilds != nil {
		if err := s.exportGuildsTxt(guilds); err != nil {
			log.Printf("store: Guilds.txt derivado pendente apos commit autoritativo: %v", err)
		}
	}
	return nil
}

func (s *SQLiteStore) LoadGuilds() (*model.GuildRegistry, error) {
	if err := s.checkOpen(); err != nil {
		return nil, err
	}
	var payload string
	err := s.db.QueryRow(`SELECT payload FROM guild_state WHERE id = 1`).Scan(&payload)
	if errors.Is(err, sql.ErrNoRows) {
		return &model.GuildRegistry{}, nil
	}
	if err != nil {
		return nil, err
	}
	decoder := json.NewDecoder(strings.NewReader(payload))
	decoder.DisallowUnknownFields()
	var registry model.GuildRegistry
	if err := decoder.Decode(&registry); err != nil {
		return nil, fmt.Errorf("store: parse guilds: %w", err)
	}
	return &registry, nil
}

func (s *SQLiteStore) LoadInstanceState() (*model.InstanceStateSnapshot, error) {
	if err := s.checkOpen(); err != nil {
		return nil, err
	}
	var payload string
	err := s.db.QueryRow(`SELECT payload FROM instance_state WHERE id = 1`).Scan(&payload)
	if errors.Is(err, sql.ErrNoRows) {
		return nil, nil
	}
	if err != nil {
		return nil, err
	}
	decoder := json.NewDecoder(strings.NewReader(payload))
	decoder.DisallowUnknownFields()
	var state model.InstanceStateSnapshot
	if err := decoder.Decode(&state); err != nil {
		return nil, fmt.Errorf("store: parse instance state: %w", err)
	}
	return &state, nil
}

func (s *SQLiteStore) SaveInstanceState(state *model.InstanceStateSnapshot) error {
	if err := s.checkOpen(); err != nil {
		return err
	}
	if state == nil {
		return nil
	}
	if state.Version != model.InstanceStateVersion {
		return fmt.Errorf("store: estado de instancias versao %d; esperado %d",
			state.Version, model.InstanceStateVersion)
	}
	b, err := json.MarshalIndent(state, "", "  ")
	if err != nil {
		return err
	}
	s.writeMu.Lock()
	defer s.writeMu.Unlock()
	_, err = s.db.Exec(`
		INSERT INTO instance_state (id, payload) VALUES (1, ?)
		ON CONFLICT(id) DO UPDATE SET payload = excluded.payload, updated_at = unixepoch()
	`, string(b))
	return err
}

// ---- estado de sessao por personagem ----

func (s *SQLiteStore) SaveCharState(uid string, state *model.CharState) error {
	if err := s.checkOpen(); err != nil {
		return err
	}
	if !safePathElement(uid) {
		return fmt.Errorf("store: UID de personagem invalido %q", uid)
	}
	s.writeMu.Lock()
	defer s.writeMu.Unlock()
	s.flushWrites()
	return s.writeCharStateLocked(uid, state)
}

func (s *SQLiteStore) writeCharStateLocked(uid string, state *model.CharState) error {
	if state == nil || (len(state.Affects) == 0 && len(state.SpecialCoins) == 0) {
		_, err := s.db.Exec(`DELETE FROM char_states WHERE character_uid = ?`, uid)
		return err
	}
	state.Version = model.CharStateVersion
	b, err := json.MarshalIndent(state, "", "  ")
	if err != nil {
		return err
	}
	_, err = s.db.Exec(`
		INSERT INTO char_states (character_uid, payload) VALUES (?, ?)
		ON CONFLICT(character_uid) DO UPDATE SET payload = excluded.payload,
			updated_at = unixepoch()
	`, uid, string(b))
	return err
}

func (s *SQLiteStore) SaveCharStateAsync(uid string, state *model.CharState) error {
	if err := s.checkOpen(); err != nil {
		return err
	}
	if !safePathElement(uid) {
		return fmt.Errorf("store: UID de personagem invalido %q", uid)
	}
	// Snapshot agora (game-loop); a escrita sai para a goroutine.
	snapshot := &model.CharState{}
	if state != nil {
		if err := json.Unmarshal(mustMarshal(state), snapshot); err != nil {
			return err
		}
	}
	s.enqueueWrite(writeJob{key: "charstate:" + uid, run: func() {
		s.writeMu.Lock()
		defer s.writeMu.Unlock()
		if err := s.writeCharStateLocked(uid, snapshot); err != nil {
			log.Printf("store: autosave charstate %q: %v", uid, err)
		}
	}})
	return nil
}

func (s *SQLiteStore) LoadCharState(uid string) (*model.CharState, error) {
	if err := s.checkOpen(); err != nil {
		return nil, err
	}
	var payload string
	err := s.db.QueryRow(`SELECT payload FROM char_states WHERE character_uid = ?`, uid).
		Scan(&payload)
	if errors.Is(err, sql.ErrNoRows) {
		return nil, nil
	}
	if err != nil {
		return nil, err
	}
	decoder := json.NewDecoder(strings.NewReader(payload))
	decoder.DisallowUnknownFields()
	var state model.CharState
	if err := decoder.Decode(&state); err != nil {
		return nil, fmt.Errorf("store: parse charstate %q: %w", uid, err)
	}
	return &state, nil
}

// ---- administracao (usada pelo console --cli e pela GUI admin) ----

// AccountSummary e a projecao leve para listagens administrativas.
type AccountSummary struct {
	Name      string   `json:"name"`
	CreatedAt int64    `json:"created_at"`
	UpdatedAt int64    `json:"updated_at"`
	Chars     []string `json:"chars"`
	Banned    bool     `json:"banned"`
	BanReason string   `json:"ban_reason,omitempty"`
}

// ListAccountSummaries devolve as contas com nomes de personagens e status de ban.
func (s *SQLiteStore) ListAccountSummaries(ctx context.Context) ([]AccountSummary, error) {
	if err := s.checkOpen(); err != nil {
		return nil, err
	}
	rows, err := s.db.QueryContext(ctx, `
		SELECT a.name_key, a.created_at, a.updated_at,
		       (SELECT b.reason FROM account_bans b WHERE b.account_key = a.name_key) AS ban_reason
		FROM accounts a ORDER BY a.name_key`)
	if err != nil {
		return nil, err
	}
	defer rows.Close()
	byKey := make(map[string]*AccountSummary)
	order := make([]string, 0, 64)
	for rows.Next() {
		var sum AccountSummary
		var banReason sql.NullString
		if err := rows.Scan(&sum.Name, &sum.CreatedAt, &sum.UpdatedAt, &banReason); err != nil {
			return nil, err
		}
		if banReason.Valid {
			sum.Banned = true
			sum.BanReason = banReason.String
		}
		sum.Chars = []string{}
		byKey[sum.Name] = &sum
		order = append(order, sum.Name)
	}
	if err := rows.Err(); err != nil {
		return nil, err
	}
	charRows, err := s.db.QueryContext(ctx,
		`SELECT account_key, name_key FROM characters ORDER BY account_key, slot`)
	if err != nil {
		return nil, err
	}
	defer charRows.Close()
	for charRows.Next() {
		var accountKey, charName string
		if err := charRows.Scan(&accountKey, &charName); err != nil {
			return nil, err
		}
		if sum, ok := byKey[accountKey]; ok {
			sum.Chars = append(sum.Chars, charName)
		}
	}
	out := make([]AccountSummary, 0, len(order))
	for _, key := range order {
		out = append(out, *byKey[key])
	}
	return out, nil
}

// DeleteAccount remove a conta e os indices derivados (FK CASCADE). Somente a
// administracao chama: o jogo nunca deleta contas.
func (s *SQLiteStore) DeleteAccount(name string) error {
	if err := s.checkOpen(); err != nil {
		return err
	}
	s.writeMu.Lock()
	defer s.writeMu.Unlock()
	res, err := s.db.Exec(`DELETE FROM accounts WHERE name_key = lower(?)`, name)
	if err != nil {
		return err
	}
	if n, _ := res.RowsAffected(); n == 0 {
		return os.ErrNotExist
	}
	return nil
}

// SetAccountBan grava/remove o ban de uma conta.
func (s *SQLiteStore) SetAccountBan(name, reason string, expiresAt int64) error {
	if err := s.checkOpen(); err != nil {
		return err
	}
	s.writeMu.Lock()
	defer s.writeMu.Unlock()
	if reason == "" && expiresAt == 0 {
		_, err := s.db.Exec(`DELETE FROM account_bans WHERE account_key = lower(?)`, name)
		return err
	}
	_, err := s.db.Exec(`
		INSERT INTO account_bans (account_key, reason, expires_at) VALUES (lower(?), ?, ?)
		ON CONFLICT(account_key) DO UPDATE SET reason = excluded.reason,
			expires_at = excluded.expires_at, banned_at = unixepoch()
	`, name, reason, expiresAt)
	return err
}

// AccountBan consulta o ban de uma conta (nil = sem ban).
func (s *SQLiteStore) AccountBan(ctx context.Context, name string) (*BanInfo, error) {
	if err := s.checkOpen(); err != nil {
		return nil, err
	}
	var info BanInfo
	var expires int64
	err := s.db.QueryRowContext(ctx,
		`SELECT reason, banned_at, expires_at FROM account_bans WHERE account_key = lower(?)`, name).
		Scan(&info.Reason, &info.BannedAt, &expires)
	if errors.Is(err, sql.ErrNoRows) {
		return nil, nil
	}
	if err != nil {
		return nil, err
	}
	if expires > 0 && time.Now().Unix() >= expires {
		// Ban expirado: limpa e reporta ausente.
		_, _ = s.db.ExecContext(ctx, `DELETE FROM account_bans WHERE account_key = lower(?)`, name)
		return nil, nil
	}
	info.ExpiresAt = expires
	return &info, nil
}

// BanInfo descreve um ban ativo.
type BanInfo struct {
	Reason    string `json:"reason"`
	BannedAt  int64  `json:"banned_at"`
	ExpiresAt int64  `json:"expires_at"`
}

// ConfigGet le uma chave de configuracao runtime (gestao via GUI/CLI).
func (s *SQLiteStore) ConfigGet(ctx context.Context, key string) (string, bool, error) {
	if err := s.checkOpen(); err != nil {
		return "", false, err
	}
	var value string
	err := s.db.QueryRowContext(ctx, `SELECT value FROM kv_config WHERE key = ?`, key).
		Scan(&value)
	if errors.Is(err, sql.ErrNoRows) {
		return "", false, nil
	}
	if err != nil {
		return "", false, err
	}
	return value, true, nil
}

// ConfigSet grava uma chave de configuracao runtime.
func (s *SQLiteStore) ConfigSet(ctx context.Context, key, value string) error {
	if err := s.checkOpen(); err != nil {
		return err
	}
	s.writeMu.Lock()
	defer s.writeMu.Unlock()
	_, err := s.db.Exec(`
		INSERT INTO kv_config (key, value) VALUES (?, ?)
		ON CONFLICT(key) DO UPDATE SET value = excluded.value, updated_at = unixepoch()
	`, key, value)
	return err
}

// ConfigList devolve todas as chaves de configuracao runtime.
func (s *SQLiteStore) ConfigList(ctx context.Context) (map[string]string, error) {
	if err := s.checkOpen(); err != nil {
		return nil, err
	}
	out := make(map[string]string)
	rows, err := s.db.QueryContext(ctx, `SELECT key, value FROM kv_config ORDER BY key`)
	if err != nil {
		return nil, err
	}
	defer rows.Close()
	for rows.Next() {
		var k, v string
		if err := rows.Scan(&k, &v); err != nil {
			return nil, err
		}
		out[k] = v
	}
	return out, rows.Err()
}

// Audit grava um evento administrativo (quem, o que, detalhe).
func (s *SQLiteStore) Audit(ctx context.Context, actor, action, detail string) {
	if s == nil {
		return
	}
	s.writeMu.Lock()
	defer s.writeMu.Unlock()
	_, _ = s.db.ExecContext(ctx,
		`INSERT INTO audit_log (actor, action, detail) VALUES (?, ?, ?)`, actor, action, detail)
}

// AuditTail devolve os ultimos eventos administrativos.
func (s *SQLiteStore) AuditTail(ctx context.Context, limit int) ([]AuditEntry, error) {
	if err := s.checkOpen(); err != nil {
		return nil, err
	}
	if limit <= 0 || limit > 500 {
		limit = 100
	}
	rows, err := s.db.QueryContext(ctx, `
		SELECT at, actor, action, detail FROM audit_log ORDER BY id DESC LIMIT ?`, limit)
	if err != nil {
		return nil, err
	}
	defer rows.Close()
	var out []AuditEntry
	for rows.Next() {
		var e AuditEntry
		if err := rows.Scan(&e.At, &e.Actor, &e.Action, &e.Detail); err != nil {
			return nil, err
		}
		out = append(out, e)
	}
	return out, rows.Err()
}

// AuditEntry e um evento administrativo.
type AuditEntry struct {
	At     int64  `json:"at"`
	Actor  string `json:"actor"`
	Action string `json:"action"`
	Detail string `json:"detail"`
}

// mustMarshal nunca falha para tipos do modelo (panic seria bug de programa).
func mustMarshal(v any) []byte {
	b, err := json.Marshal(v)
	if err != nil {
		panic(fmt.Sprintf("store: marshal de modelo: %v", err))
	}
	return b
}

// MigrateJSONToSQLite importa o legado baseado em arquivos (dir de contas JSON,
// guilds.json e pasta charstate/ irmaos) para o banco embutido. Contas ja
// presentes no banco NAO sao sobrescritas. Devolve quantas contas foram
// importadas. Guild/charstate seguem a mesma convencao de caminho do JSONStore
// (ao lado do dir de contas).
func MigrateJSONToSQLite(ctx context.Context, accountsDir, dbPath string, opts ...SQLOption) (
	int, error,
) {
	s, err := NewSQLiteStore(ctx, SQLiteConfig{Path: dbPath}, opts...)
	if err != nil {
		return 0, err
	}
	defer s.Close()

	imported := 0
	entries, err := os.ReadDir(accountsDir)
	if err != nil {
		return 0, fmt.Errorf("store: migracao: ler contas %q: %w", accountsDir, err)
	}
	for _, entry := range entries {
		if entry.IsDir() || !strings.EqualFold(filepath.Ext(entry.Name()), ".json") {
			continue
		}
		raw, err := os.ReadFile(filepath.Join(accountsDir, entry.Name()))
		if err != nil {
			return imported, fmt.Errorf("store: migracao: ler %q: %w", entry.Name(), err)
		}
		var acc model.Account
		if err := decodeAccountJSON(raw, &acc); err != nil {
			return imported, fmt.Errorf("store: migracao: parse %q: %w", entry.Name(), err)
		}
		if err := s.CreateAccount(&acc); err != nil {
			if errors.Is(err, ErrAccountExists) {
				continue // banco ja tem a conta: prevalece o estado do banco
			}
			return imported, fmt.Errorf("store: migracao: importar %q: %w", acc.Name, err)
		}
		imported++
	}

	// guilds.json (mesma convencao de caminho do JSONStore).
	guildsPath := filepath.Join(filepath.Dir(filepath.Clean(accountsDir)), "guilds.json")
	if b, err := os.ReadFile(guildsPath); err == nil {
		decoder := json.NewDecoder(bytes.NewReader(b))
		decoder.DisallowUnknownFields()
		var registry model.GuildRegistry
		if err := decoder.Decode(&registry); err != nil {
			return imported, fmt.Errorf("store: migracao: parse guilds: %w", err)
		}
		if err := s.SaveGameState(&registry); err != nil {
			return imported, fmt.Errorf("store: migracao: importar guilds: %w", err)
		}
	} else if !os.IsNotExist(err) {
		return imported, fmt.Errorf("store: migracao: ler guilds: %w", err)
	}

	// Pasta de charstate por UID.
	charStateDir := filepath.Join(filepath.Dir(filepath.Clean(accountsDir)), "charstate")
	if sidecars, err := os.ReadDir(charStateDir); err == nil {
		for _, entry := range sidecars {
			if entry.IsDir() || !strings.EqualFold(filepath.Ext(entry.Name()), ".json") {
				continue
			}
			raw, err := os.ReadFile(filepath.Join(charStateDir, entry.Name()))
			if err != nil {
				return imported, fmt.Errorf("store: migracao: ler charstate %q: %w", entry.Name(), err)
			}
			decoder := json.NewDecoder(bytes.NewReader(raw))
			decoder.DisallowUnknownFields()
			var state model.CharState
			if err := decoder.Decode(&state); err != nil {
				return imported, fmt.Errorf("store: migracao: parse charstate %q: %w", entry.Name(), err)
			}
			uid := strings.TrimSuffix(entry.Name(), filepath.Ext(entry.Name()))
			if err := s.SaveCharState(uid, &state); err != nil {
				return imported, fmt.Errorf("store: migracao: importar charstate %q: %w", uid, err)
			}
		}
	} else if !os.IsNotExist(err) {
		return imported, fmt.Errorf("store: migracao: ler charstate: %w", err)
	}

	return imported, nil
}
