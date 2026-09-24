package admin

import (
	"context"
	"encoding/json"
	"errors"
	"fmt"
	"os"
	"strconv"
	"strings"
	"time"

	"wydgo/internal/account"
	"wydgo/internal/game"
	"wydgo/internal/serverlist"
	"wydgo/internal/store"
)

// Backend implementa Handler sobre World (runtime) + Store (persistencia).
type Backend struct {
	World *game.World
	Store *store.SQLiteStore

	// Stop encerra o servidor de forma graciosa (server.stop).
	Stop func()
	// SaveAll persiste o estado de todos os jogadores online (server.save).
	SaveAll func() error
	// Actor identifica quem executou (console, gui@ip) para a auditoria.
	Actor string
}

// ErrUnknownCommand para comandos fora da tabela.
var ErrUnknownCommand = errors.New("comando desconhecido (help lista os disponiveis)")

// Commands e a tabela publica para help.
var Commands = []string{
	"server.status", "server.save", "server.stop",
	"players.list", "players.kick", "players.kick_account",
	"server.broadcast", "server.panel",
	"accounts.list", "account.info", "account.create", "account.delete",
	"account.ban", "account.unban",
	"chars.list", "char.set",
	"serverlist.get", "serverlist.generate", "serverlist.apply", "serverlist.set",
	"config.list", "config.set", "audit.tail", "help",
}

func argString(args map[string]any, key string) string {
	if v, ok := args[key].(string); ok {
		return strings.TrimSpace(v)
	}
	return ""
}

func argInt(args map[string]any, key string) int64 {
	switch v := args[key].(type) {
	case float64:
		return int64(v)
	case int:
		return int64(v)
	case int64:
		return v
	default:
		return 0
	}
}

// Handle roteia um comando. Operacoes de runtime passam pelo World.AdminCall
// (game-loop); persistencia fala direto com a store.
func (b *Backend) Handle(ctx context.Context, cmd string, args map[string]any) (any, error) {
	if args == nil {
		args = map[string]any{}
	}
	actor := b.Actor
	if actor == "" {
		actor = "admin"
	}
	audit := func(action, detail string) {
		if b.Store != nil {
			b.Store.Audit(ctx, actor, action, detail)
		}
	}

	switch cmd {
	case "help":
		return map[string]any{"commands": Commands}, nil

	case "server.status":
		var status game.AdminStatus
		if err := b.World.AdminCall(func() { status = b.World.AdminStatusSnapshot() }); err != nil {
			return nil, err
		}
		if b.Store != nil {
			if sums, err := b.Store.ListAccountSummaries(ctx); err == nil {
				status.Accounts = int64(len(sums))
			}
		}
		return status, nil

	case "server.save":
		if b.SaveAll == nil {
			return nil, errors.New("save nao disponivel")
		}
		if err := b.SaveAll(); err != nil {
			return nil, err
		}
		audit("server.save", "")
		return "estado persistido", nil

	case "server.stop":
		audit("server.stop", "")
		if b.Stop == nil {
			return nil, errors.New("stop nao disponivel")
		}
		go func() {
			// Deixa a resposta chegar no socket antes do shutdown.
			time.Sleep(200 * time.Millisecond)
			b.Stop()
		}()
		return "desligamento iniciado", nil

	case "players.list":
		var players []game.AdminPlayerInfo
		if err := b.World.AdminCall(func() { players = b.World.AdminListPlayers() }); err != nil {
			return nil, err
		}
		return map[string]any{"count": len(players), "players": players}, nil

	case "players.kick":
		name := argString(args, "name")
		var kicked bool
		var err error
		if callErr := b.World.AdminCall(func() { kicked, err = b.World.AdminKickByName(name) }); callErr != nil {
			return nil, callErr
		}
		if err != nil {
			return nil, err
		}
		if !kicked {
			return nil, fmt.Errorf("jogador %q nao esta online", name)
		}
		audit("players.kick", name)
		return fmt.Sprintf("%s desconectado", name), nil

	case "players.kick_account":
		account := argString(args, "account")
		var kicked int
		var err error
		if callErr := b.World.AdminCall(func() { kicked, err = b.World.AdminKickAccount(account) }); callErr != nil {
			return nil, callErr
		}
		if err != nil {
			return nil, err
		}
		if kicked == 0 {
			return nil, fmt.Errorf("conta %q sem sessoes online", account)
		}
		audit("players.kick_account", fmt.Sprintf("%s (%d)", account, kicked))
		return fmt.Sprintf("%d sessao(oes) de %s desconectadas", kicked, account), nil

	case "server.broadcast":
		message := argString(args, "message")
		var sent int
		var err error
		if callErr := b.World.AdminCall(func() { sent, err = b.World.AdminBroadcast(message) }); callErr != nil {
			return nil, callErr
		}
		if err != nil {
			return nil, err
		}
		audit("server.broadcast", message)
		return fmt.Sprintf("entregue para %d jogador(es)", sent), nil

	case "server.panel":
		message := argString(args, "message")
		var sent int
		var err error
		if callErr := b.World.AdminCall(func() { sent, err = b.World.AdminSystemPanel(message) }); callErr != nil {
			return nil, callErr
		}
		if err != nil {
			return nil, err
		}
		audit("server.panel", message)
		return fmt.Sprintf("painel entregue para %d jogador(es)", sent), nil

	case "accounts.list":
		if b.Store == nil {
			return nil, errors.New("store sqlite nao configurada")
		}
		sums, err := b.Store.ListAccountSummaries(ctx)
		if err != nil {
			return nil, err
		}
		return map[string]any{"count": len(sums), "accounts": sums}, nil

	case "account.info":
		if b.Store == nil {
			return nil, errors.New("store sqlite nao configurada")
		}
		name := argString(args, "name")
		sums, err := b.Store.ListAccountSummaries(ctx)
		if err != nil {
			return nil, err
		}
		for _, sum := range sums {
			if strings.EqualFold(sum.Name, name) {
				return sum, nil
			}
		}
		return nil, fmt.Errorf("conta %q nao existe", name)

	case "account.create":
		if b.Store == nil {
			return nil, errors.New("store sqlite nao configurada")
		}
		name := argString(args, "name")
		password := argString(args, "password")
		confirm := argString(args, "confirm")
		if confirm == "" {
			confirm = password
		}
		if name == "" || password == "" {
			return nil, errors.New("informe name e password")
		}
		acc, err := account.Create(b.Store, name, password, confirm)
		if err != nil {
			return nil, err
		}
		audit("account.create", name)
		return fmt.Sprintf("conta %s criada (%d slot(s) de personagem)", acc.Name, len(acc.Chars)), nil

	case "account.delete":
		if b.Store == nil {
			return nil, errors.New("store sqlite nao configurada")
		}
		name := argString(args, "name")
		if name == "" {
			return nil, errors.New("informe name")
		}
		// Online primeiro: kick evita o jogador continuar numa conta apagada.
		var kicked int
		if callErr := b.World.AdminCall(func() { kicked, _ = b.World.AdminKickAccount(name) }); callErr != nil {
			return nil, callErr
		}
		if err := b.Store.DeleteAccount(name); err != nil {
			return nil, err
		}
		audit("account.delete", name)
		return fmt.Sprintf("conta %s excluida%s", name, map[bool]string{true: " (sessao encerrada)", false: ""}[kicked > 0]), nil

	case "account.ban":
		if b.Store == nil {
			return nil, errors.New("store sqlite nao configurada")
		}
		name := argString(args, "name")
		reason := argString(args, "reason")
		if name == "" {
			return nil, errors.New("informe name")
		}
		expires := argInt(args, "expires_at")
		if err := b.Store.SetAccountBan(name, reason, expires); err != nil {
			return nil, err
		}
		var kicked int
		if callErr := b.World.AdminCall(func() { kicked, _ = b.World.AdminKickAccount(name) }); callErr == nil && kicked > 0 {
			// Ban derruba a sessao ativa.
		}
		audit("account.ban", fmt.Sprintf("%s (%s)", name, reason))
		return fmt.Sprintf("conta %s banida%s", name, map[bool]string{true: " e desconectada", false: ""}[kicked > 0]), nil

	case "account.unban":
		if b.Store == nil {
			return nil, errors.New("store sqlite nao configurada")
		}
		name := argString(args, "name")
		if err := b.Store.SetAccountBan(name, "", 0); err != nil {
			return nil, err
		}
		audit("account.unban", name)
		return fmt.Sprintf("conta %s desbanida", name), nil

	case "chars.list":
		if b.Store == nil {
			return nil, errors.New("store sqlite nao configurada")
		}
		name := argString(args, "account")
		type charInfo struct {
			Account string `json:"account"`
			Name    string `json:"name"`
			Class   int    `json:"class"`
			Level   int    `json:"level"`
			Gold    uint32 `json:"gold"`
			Exp     uint32 `json:"exp"`
			X       int    `json:"x"`
			Y       int    `json:"y"`
			Online  bool   `json:"online"`
		}
		out := []charInfo{}
		accounts := []string{}
		if name != "" {
			accounts = append(accounts, name)
		} else {
			sums, err := b.Store.ListAccountSummaries(ctx)
			if err != nil {
				return nil, err
			}
			for _, s := range sums {
				accounts = append(accounts, s.Name)
			}
		}
		online := map[string]bool{}
		if callErr := b.World.AdminCall(func() {
			for _, p := range b.World.AdminListPlayers() {
				if p.Name != "" {
					online[strings.ToLower(p.Name)] = true
				}
			}
		}); callErr != nil {
			return nil, callErr
		}
		for _, accName := range accounts {
			acc, err := b.Store.LoadAccount(accName)
			if err != nil {
				continue
			}
			for _, ch := range acc.Chars {
				if ch.Name == "" {
					continue
				}
				ci := charInfo{
					Account: acc.Name,
					Name:    ch.Name,
					Class:   int(ch.Class),
					Gold:    ch.Gold,
					Exp:     ch.Exp,
					X:       int(ch.X),
					Y:       int(ch.Y),
					Online:  online[strings.ToLower(ch.Name)],
				}
				if ch.Score != nil {
					ci.Level = int(ch.Score.Level)
				}
				out = append(out, ci)
			}
		}
		return map[string]any{"count": len(out), "chars": out}, nil

	case "char.set":
		if b.Store == nil {
			return nil, errors.New("store sqlite nao configurada")
		}
		accName := argString(args, "account")
		charName := argString(args, "name")
		if accName == "" || charName == "" {
			return nil, errors.New("informe account e name")
		}
		// Personagem online: edit persite na conta, mas o save de logout
		// sobrescreveria. Derruba a sessao (o logout salva) e espera o save.
		var kicked int
		if callErr := b.World.AdminCall(func() { kicked, _ = b.World.AdminKickAccount(accName) }); callErr != nil {
			return nil, callErr
		}
		if kicked > 0 {
			time.Sleep(1500 * time.Millisecond)
		}
		acc, err := b.Store.LoadAccount(accName)
		if err != nil {
			return nil, err
		}
		found := false
		for i := range acc.Chars {
			ch := &acc.Chars[i]
			if !strings.EqualFold(ch.Name, charName) {
				continue
			}
			found = true
			if v, ok := args["level"].(float64); ok && ch.Score != nil && v >= 1 && v <= 199 {
				ch.Score.Level = uint32(v)
			}
			if v, ok := args["gold"].(float64); ok && v >= 0 {
				ch.Gold = uint32(v)
			}
			if v, ok := args["exp"].(float64); ok && v >= 0 {
				ch.Exp = uint32(v)
			}
			if v, ok := args["x"].(float64); ok {
				ch.X = uint16(v)
			}
			if v, ok := args["y"].(float64); ok {
				ch.Y = uint16(v)
			}
		}
		if !found {
			return nil, fmt.Errorf("personagem %q nao esta na conta %q", charName, accName)
		}
		if err := b.Store.SaveAccount(acc); err != nil {
			return nil, err
		}
		audit("char.set", fmt.Sprintf("%s/%s", accName, charName))
		return fmt.Sprintf("personagem %s atualizado%s", charName,
			map[bool]string{true: " (sessao derrubada para aplicar)", false: ""}[kicked > 0]), nil

	case "serverlist.get":
		var list *serverlist.List
		var path string
		var err error
		if callErr := b.World.AdminCall(func() { list, path, err = b.World.AdminServerListRead() }); callErr != nil {
			return nil, callErr
		}
		if err != nil {
			return nil, err
		}
		return map[string]any{"path": path, "groups": list.Groups}, nil

	case "serverlist.generate":
		group := argString(args, "group")
		host := argString(args, "host")
		if group == "" || host == "" {
			return nil, errors.New("informe group (nome) e host (IP do servidor)")
		}
		var path string
		var err error
		list, gerr := serverlist.FromTemplate(group, host)
		if gerr != nil {
			return nil, gerr
		}
		if callErr := b.World.AdminCall(func() { path, err = b.World.AdminServerListWrite(list) }); callErr != nil {
			return nil, callErr
		}
		if err != nil {
			return nil, err
		}
		audit("serverlist.generate", group+"="+host)
		return fmt.Sprintf("serverlist.bin gerado em %s (grupo %q -> %s)", path, group, host), nil

	case "serverlist.apply":
		data := argString(args, "data")
		if data == "" {
			return nil, errors.New("informe data (JSON da lista: {\"groups\":[{\"name\":...,\"servers\":[...]}]})")
		}
		var list serverlist.List
		if err := json.Unmarshal([]byte(data), &list); err != nil {
			return nil, fmt.Errorf("JSON invalido: %w", err)
		}
		var path string
		var err error
		if callErr := b.World.AdminCall(func() { path, err = b.World.AdminServerListWrite(&list) }); callErr != nil {
			return nil, callErr
		}
		if err != nil {
			return nil, err
		}
		audit("serverlist.apply", fmt.Sprintf("%d grupo(s)", len(list.Groups)))
		return fmt.Sprintf("serverlist.bin gravado em %s (%d grupo(s))", path, len(list.Groups)), nil

	case "serverlist.set":
		group := argString(args, "group")
		name := argString(args, "name")
		host := argString(args, "host")
		if group == "" {
			return nil, errors.New("informe group (indice 0-9 ou nome do grupo)")
		}
		var list *serverlist.List
		var err error
		if callErr := b.World.AdminCall(func() { list, _, err = b.World.AdminServerListRead() }); callErr != nil {
			return nil, callErr
		}
		if err != nil {
			return nil, err
		}
		gidx := -1
		if n, perr := strconv.Atoi(group); perr == nil {
			if n >= 0 && n < len(list.Groups) {
				gidx = n
			}
		} else {
			for i := range list.Groups {
				if strings.EqualFold(strings.TrimSpace(list.Groups[i].Name), group) {
					gidx = i
					break
				}
			}
		}
		if gidx < 0 {
			return nil, fmt.Errorf("grupo %q nao encontrado (use indice 0-%d)", group, len(list.Groups)-1)
		}
		if name != "" {
			list.Groups[gidx].Name = name
		}
		if host != "" {
			// IP novo entra no slot 0 do grupo e o resto e limpo: a fila de
			// IPs iguais do arquivo original nao representa servidores reais.
			list.Groups[gidx].Servers = []string{host}
		}
		var path string
		if callErr := b.World.AdminCall(func() { path, err = b.World.AdminServerListWrite(list) }); callErr != nil {
			return nil, callErr
		}
		if err != nil {
			return nil, err
		}
		audit("serverlist.set", fmt.Sprintf("g%d nome=%q host=%q", gidx, name, host))
		return fmt.Sprintf("grupo %d (%s) atualizado em %s", gidx, list.Groups[gidx].Name, path), nil

	case "config.list":
		if b.Store == nil {
			return nil, errors.New("store sqlite nao configurada")
		}
		return b.Store.ConfigList(ctx)

	case "config.set":
		if b.Store == nil {
			return nil, errors.New("store sqlite nao configurada")
		}
		key := argString(args, "key")
		value := argString(args, "value")
		if key == "" {
			return nil, errors.New("informe key")
		}
		if err := b.Store.ConfigSet(ctx, key, value); err != nil {
			return nil, err
		}
		audit("config.set", key+"="+value)
		return fmt.Sprintf("%s gravado", key), nil

	case "audit.tail":
		if b.Store == nil {
			return nil, errors.New("store sqlite nao configurada")
		}
		return b.Store.AuditTail(ctx, int(argInt(args, "limit")))

	default:
		return nil, ErrUnknownCommand
	}
}

// FileExists e utilitario para o main (ex.: checar banco na primeira execucao).
func FileExists(path string) bool {
	st, err := os.Stat(path)
	return err == nil && !st.IsDir()
}
