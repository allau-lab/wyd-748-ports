// Console administrativo do servidor: subcomandos one-shot (sem abrir porta
// de jogo) e REPL interativo (--cli) sobre o mesmo backend do painel TCP.
//
//	server account create gilmar senha123
//	server account list
//	server account ban botinho "farm bot" 7
//	server migrate            # contas JSON -> SQLite
//	server db status
//	server --cli              # console interativo com o servidor no ar
package main

import (
	"bufio"
	"context"
	"encoding/json"
	"flag"
	"fmt"
	"io"
	"os"
	"path/filepath"
	"sort"
	"strconv"
	"strings"
	"time"

	"wydgo/internal/account"
	"wydgo/internal/admin"
	"wydgo/internal/data"
	"wydgo/internal/serverlist"
	"wydgo/internal/store"
)

// storeFlags e o subconjunto de flags que os comandos de persistencia usam
// (nao precisam de NPCs/terreno/volatiles — abrem so o banco).
type storeFlags struct {
	accountsDir string
	dbPath      string
	guildsTxt   string
	slPath      string // serverlist.bin do client (comando serverlist)
}

// parseStoreFlags faz o parse das flags (em qualquer posicao) e devolve
// tambem os tokens posicionais, na ordem. flag.Parse para no primeiro
// nao-flag, entao iteramos: parse -> guarda posicional -> parse do resto.
func parseStoreFlags(cfg *data.ServerConfig, args []string) (*storeFlags, []string) {
	f := &storeFlags{
		accountsDir: cfg.AccountsPath,
		dbPath:      cfg.DatabasePath,
		guildsTxt:   cfg.GuildsTxtPath,
		slPath:      cfg.ClientServerListPath,
	}
	fs := flag.NewFlagSet("store", flag.ContinueOnError)
	fs.SetOutput(io.Discard)
	fs.StringVar(&f.accountsDir, "accounts", f.accountsDir, "dir de contas JSON (legado)")
	fs.StringVar(&f.dbPath, "db", f.dbPath, "arquivo do banco SQLite")
	fs.StringVar(&f.guildsTxt, "guilds-txt", f.guildsTxt, "Guilds.txt exportado")
	fs.StringVar(&f.slPath, "sl-path", f.slPath, "serverlist.bin do client")
	// ignora -config (ja processado pelo main)
	fs.StringVar(new(string), "config", "", "")

	var positional []string
	remaining := args
	for {
		if err := fs.Parse(remaining); err != nil {
			break
		}
		remaining = fs.Args()
		if len(remaining) == 0 {
			break
		}
		positional = append(positional, remaining[0])
		remaining = remaining[1:]
	}
	return f, positional
}

func openSQLite(f *storeFlags) (*store.SQLiteStore, error) {
	if err := os.MkdirAll(filepath.Dir(f.dbPath), 0o755); err != nil {
		return nil, err
	}
	ctx, cancel := context.WithTimeout(context.Background(), 10*time.Second)
	defer cancel()
	return store.NewSQLiteStore(ctx, store.SQLiteConfig{
		Path: f.dbPath, GuildsTxtPath: f.guildsTxt,
	})
}

// runStoreCommand executa os subcomandos que mexem so na persistencia e
// devolve o codigo de saida do processo. Flags e posicionais podem vir em
// qualquer ordem: `server account ban gilmar bot 7 -db data/wydgo.db`.
func runStoreCommand(cfg *data.ServerConfig, args []string) int {
	if len(args) == 0 {
		fmt.Fprintln(os.Stderr, "uso: server <migrate|account|db> [flags]")
		return 2
	}
	sub := args[0]
	f, positional := parseStoreFlags(cfg, args[1:])
	action := ""
	params := positional
	if sub == "account" {
		if len(positional) == 0 {
			fmt.Fprintln(os.Stderr, "uso: server account <create|list|info|ban|unban|delete> ...")
			return 2
		}
		action = positional[0]
		params = positional[1:]
	}

	switch sub {
	case "serverlist":
		return runServerListCommand(cfg, f.slPath, positional)

	case "migrate":
		if st, err := os.Stat(f.accountsDir); err != nil || !st.IsDir() {
			fmt.Fprintf(os.Stderr, "dir de contas %q inexistente — nada a migrar\n", f.accountsDir)
			return 1
		}
		started := time.Now()
		imported, err := store.MigrateJSONToSQLite(context.Background(), f.accountsDir, f.dbPath,
			store.WithSQLiteGuildsTxt(f.guildsTxt))
		if err != nil {
			fmt.Fprintf(os.Stderr, "migracao: %v\n", err)
			return 1
		}
		fmt.Printf("migrados %d conta(s) de %s para %s em %s\n",
			imported, f.accountsDir, f.dbPath, time.Since(started).Round(time.Millisecond))
		return 0

	case "account":
		return runAccountCommand(f, action, params)

	case "db":
		return runDBCommand(f)

	default:
		fmt.Fprintf(os.Stderr, "subcomando desconhecido %q (use migrate, account ou db)\n", sub)
		return 2
	}
}

// runServerListCommand gerencia o serverlist.bin do CLIENT sem subir o mundo:
//
//	server serverlist show                              # le e exibe
//	server serverlist generate WYD748 192.168.1.21      # gera padrao
//	server serverlist apply lista.json                  # aplica JSON completo
//	flags: -sl-path <caminho> (padrao: client_serverlist do server.txt)
func runServerListCommand(cfg *data.ServerConfig, slPath string, args []string) int {
	action := ""
	rest := []string{}
	if len(args) > 0 {
		action = args[0]
		rest = args[1:]
	}
	showUsage := func() int {
		fmt.Fprintln(os.Stderr, "uso: server serverlist <show|generate|apply> [flags]")
		fmt.Fprintln(os.Stderr, "  show                    le e exibe a lista atual")
		fmt.Fprintln(os.Stderr, "  generate <grupo> <ip>   gera a lista padrao apontando para o servidor")
		fmt.Fprintln(os.Stderr, "  apply <arquivo.json>    aplica {\"groups\":[{\"name\":...,\"servers\":[...]}]}")
		fmt.Fprintln(os.Stderr, "  -sl-path <caminho>      serverlist.bin do client (padrao: client_serverlist)")
		return 2
	}
	if slPath == "" {
		fmt.Fprintln(os.Stderr, "client_serverlist nao configurado no server.txt e nenhum -sl-path informado")
		return showUsage()
	}
	switch action {
	case "show", "get":
		if _, err := os.Stat(slPath); err != nil {
			fmt.Printf("%s: ainda nao existe (o client mostraria a lista vazia)\n", slPath)
			return 0
		}
		l, err := serverlist.ReadFile(slPath)
		if err != nil {
			fmt.Fprintf(os.Stderr, "ler %s: %v\n", slPath, err)
			return 1
		}
		fmt.Printf("lista de %s:\n", slPath)
		empty := true
		for g, grp := range l.Groups {
			if grp.Name == "" && len(grp.Servers) == 0 {
				continue
			}
			empty = false
			fmt.Printf("  [%d] %-16s -> %s\n", g, grp.Name, strings.Join(grp.Servers, ", "))
		}
		if empty {
			fmt.Println("  (vazia)")
		}
		return 0

	case "generate":
		if len(rest) < 2 {
			fmt.Fprintln(os.Stderr, "uso: server serverlist generate <nome-do-grupo> <ip-do-servidor>")
			return 2
		}
		l, err := serverlist.FromTemplate(rest[0], rest[1])
		if err != nil {
			fmt.Fprintf(os.Stderr, "gerar: %v\n", err)
			return 1
		}
		if err := l.WriteFile(slPath); err != nil {
			fmt.Fprintf(os.Stderr, "gravar %s: %v\n", slPath, err)
			return 1
		}
		fmt.Printf("serverlist.bin gerado em %s (grupo %q -> %s)\n", slPath, rest[0], rest[1])
		fmt.Println("copie/distribua para a pasta do client e reinicie o client.")
		return 0

	case "apply":
		if len(rest) < 1 {
			fmt.Fprintln(os.Stderr, "uso: server serverlist apply <arquivo.json>")
			return 2
		}
		data, err := os.ReadFile(rest[0])
		if err != nil {
			fmt.Fprintf(os.Stderr, "ler %s: %v\n", rest[0], err)
			return 1
		}
		var l serverlist.List
		if err := json.Unmarshal(data, &l); err != nil {
			fmt.Fprintf(os.Stderr, "JSON invalido: %v\n", err)
			return 1
		}
		if err := l.WriteFile(slPath); err != nil {
			fmt.Fprintf(os.Stderr, "gravar %s: %v\n", slPath, err)
			return 1
		}
		fmt.Printf("serverlist.bin gravado em %s (%d grupo(s))\n", slPath, len(l.Groups))
		return 0

	default:
		return showUsage()
	}
}

func runAccountCommand(f *storeFlags, action string, args []string) int {
	if action == "" {
		fmt.Fprintln(os.Stderr, "uso: server account <create|list|info|ban|unban|delete> ...")
		return 2
	}
	st, err := openSQLite(f)
	if err != nil {
		fmt.Fprintf(os.Stderr, "abrir banco: %v\n", err)
		return 1
	}
	defer st.Close()
	ctx := context.Background()

	switch action {
	case "create":
		if len(args) < 2 {
			fmt.Fprintln(os.Stderr, "uso: server account create <nome> <senha> [confirmacao]")
			return 2
		}
		name, pass := args[0], args[1]
		confirm := pass
		if len(args) >= 3 {
			confirm = args[2]
		}
		acc, err := account.Create(st, name, pass, confirm)
		if err != nil {
			fmt.Fprintf(os.Stderr, "criar conta: %v\n", err)
			return 1
		}
		fmt.Printf("conta %q criada (%d slot(s) de personagem)\n", acc.Name, len(acc.Chars))
		return 0

	case "list":
		sums, err := st.ListAccountSummaries(ctx)
		if err != nil {
			fmt.Fprintf(os.Stderr, "listar: %v\n", err)
			return 1
		}
		fmt.Printf("%-16s %6s  %s\n", "CONTA", "CHARS", "SITUACAO")
		for _, s := range sums {
			sit := "ativa"
			if s.Banned {
				sit = "BANIDA: " + s.BanReason
			}
			fmt.Printf("%-16s %6d  %s\n", s.Name, len(s.Chars), sit)
		}
		fmt.Printf("total: %d\n", len(sums))
		return 0

	case "info":
		if len(args) < 1 {
			fmt.Fprintln(os.Stderr, "uso: server account info <nome>")
			return 2
		}
		sums, err := st.ListAccountSummaries(ctx)
		if err != nil {
			fmt.Fprintf(os.Stderr, "listar: %v\n", err)
			return 1
		}
		for _, s := range sums {
			if !strings.EqualFold(s.Name, args[0]) {
				continue
			}
			b, _ := json.MarshalIndent(s, "", "  ")
			fmt.Println(string(b))
			if ban, err := st.AccountBan(ctx, s.Name); err == nil && ban != nil {
				until := "permanente"
				if ban.ExpiresAt > 0 {
					until = time.Unix(ban.ExpiresAt, 0).Format(time.RFC3339)
				}
				fmt.Printf("BANIDO ate %s (%s)\n", until, ban.Reason)
			}
			return 0
		}
		fmt.Fprintf(os.Stderr, "conta %q nao existe\n", args[0])
		return 1

	case "ban":
		if len(args) < 1 {
			fmt.Fprintln(os.Stderr, `uso: server account ban <nome> [motivo] [dias]  (sem dias = permanente)`)
			return 2
		}
		name := args[0]
		reason := "suspenso pelo administrador"
		if len(args) >= 2 {
			reason = args[1]
		}
		var expires int64
		if len(args) >= 3 {
			days, err := strconv.ParseInt(args[2], 10, 64)
			if err != nil || days < 0 {
				fmt.Fprintf(os.Stderr, "dias invalidos %q\n", args[2])
				return 2
			}
			if days > 0 {
				expires = time.Now().Add(time.Duration(days) * 24 * time.Hour).Unix()
			}
		}
		if err := st.SetAccountBan(name, reason, expires); err != nil {
			fmt.Fprintf(os.Stderr, "banir: %v\n", err)
			return 1
		}
		if expires > 0 {
			fmt.Printf("conta %q banida ate %s (%s)\n", name,
				time.Unix(expires, 0).Format(time.RFC3339), reason)
		} else {
			fmt.Printf("conta %q banida permanentemente (%s)\n", name, reason)
		}
		return 0

	case "unban":
		if len(args) < 1 {
			fmt.Fprintln(os.Stderr, "uso: server account unban <nome>")
			return 2
		}
		if err := st.SetAccountBan(args[0], "", 0); err != nil {
			fmt.Fprintf(os.Stderr, "desbanir: %v\n", err)
			return 1
		}
		fmt.Printf("conta %q desbanida\n", args[0])
		return 0

	case "delete":
		if len(args) < 1 {
			fmt.Fprintln(os.Stderr, "uso: server account delete <nome>")
			return 2
		}
		if err := st.DeleteAccount(args[0]); err != nil {
			fmt.Fprintf(os.Stderr, "excluir: %v\n", err)
			return 1
		}
		fmt.Printf("conta %q excluida\n", args[0])
		return 0

	default:
		fmt.Fprintf(os.Stderr, "acao desconhecida %q (create|list|info|ban|unban|delete)\n", action)
		return 2
	}
}

func runDBCommand(f *storeFlags) int {
	st, err := openSQLite(f)
	if err != nil {
		fmt.Fprintf(os.Stderr, "abrir banco: %v\n", err)
		return 1
	}
	defer st.Close()
	sums, err := st.ListAccountSummaries(context.Background())
	if err != nil {
		fmt.Fprintf(os.Stderr, "consultar: %v\n", err)
		return 1
	}
	size := int64(0)
	if fi, err := os.Stat(f.dbPath); err == nil {
		size = fi.Size()
	}
	cfgs, _ := st.ConfigList(context.Background())
	keys := make([]string, 0, len(cfgs))
	for k := range cfgs {
		keys = append(keys, k)
	}
	sort.Strings(keys)
	fmt.Printf("banco:      %s (%d bytes)\n", f.dbPath, size)
	fmt.Printf("contas:     %d\n", len(sums))
	fmt.Printf("config_keys: %v\n", keys)
	return 0
}

func yesNo(b bool) string {
	if b {
		return "sim"
	}
	return "-"
}

// --- console interativo (--cli) -------------------------------------------

// positionalArgs mapeia os argumentos posicionais de cada comando para chaves
// nomeadas, para o console aceitar `ban gilmar "farm bot" 7`.
var positionalArgs = map[string][]string{
	"players.kick":         {"name"},
	"players.kick_account": {"account"},
	"server.broadcast":     {"message"},
	"server.panel":         {"message"},
	"account.create":       {"name", "password"},
	"account.delete":       {"name"},
	"account.info":         {"name"},
	"account.ban":          {"name", "reason", "days"},
	"account.unban":        {"name"},
	"config.set":           {"key", "value"},
	"audit.tail":           {"limit"},
	"serverlist.generate":  {"group", "host"},
	"serverlist.set":       {"group", "name", "host"},
}

// messageKeyFor diz se o comando aceita texto livre no fim da linha
// (broadcast/panel sem aspas: `broadcast Servidor reinicia em 10 min`).
func messageKeyFor(cmd string) (string, bool) {
	switch cmd {
	case "server.broadcast", "server.panel":
		return "message", true
	}
	return "", false
}

// consoleAliases encurta o uso interativo.
var consoleAliases = map[string]string{
	"status":    "server.status",
	"save":      "server.save",
	"stop":      "server.stop",
	"players":   "players.list",
	"kick":      "players.kick",
	"kickacc":   "players.kick_account",
	"broadcast": "server.broadcast",
	"panel":     "server.panel",
	"accounts":  "accounts.list",
	"acc":       "account.info",
	"ban":       "account.ban",
	"unban":     "account.unban",
	"cfg":       "config.list",
	"set":       "config.set",
	"audit":     "audit.tail",
	"slist":     "serverlist.get",
	"slgen":     "serverlist.generate",
	"help":      "help",
	"?":         "help",
}

// splitConsoleLine divide a linha respeitando aspas simples/duplas.
func splitConsoleLine(line string) []string {
	var out []string
	var cur strings.Builder
	inQuote := rune(0)
	for _, r := range line {
		switch {
		case inQuote != 0:
			if r == inQuote {
				inQuote = 0
			} else {
				cur.WriteRune(r)
			}
		case r == '"' || r == '\'':
			inQuote = r
		case r == ' ' || r == '\t':
			if cur.Len() > 0 {
				out = append(out, cur.String())
				cur.Reset()
			}
		default:
			cur.WriteRune(r)
		}
	}
	if cur.Len() > 0 {
		out = append(out, cur.String())
	}
	return out
}

// parseConsoleLine converte `ban gilmar "farm bot" 7 x=1` em
// (cmd="account.ban", args{name,reason:...,days:7,x:1}).
func parseConsoleLine(line string) (string, map[string]any, error) {
	tokens := splitConsoleLine(line)
	if len(tokens) == 0 {
		return "", nil, nil
	}
	cmd := tokens[0]
	if mapped, ok := consoleAliases[cmd]; ok {
		cmd = mapped
	}
	args := map[string]any{}
	positional := positionalArgs[cmd]
	posIdx := 0
	const freeTextKey = "__freetext__"
	// Forma curta do console: `account create gilmar senha`, `account delete gilmar`.
	if cmd == "account" && len(tokens) > 1 {
		switch tokens[1] {
		case "create":
			cmd = "account.create"
			tokens = append(tokens[:1], tokens[2:]...)
		case "delete":
			cmd = "account.delete"
			tokens = append(tokens[:1], tokens[2:]...)
		}
		positional = positionalArgs[cmd]
	}
	// `ban gilmar farm bot 7` -> name=gilmar, reason="farm bot", days=7.
	// Texto livre no meio, dias opcionais no fim. key=valor desativa o atalho.
	if cmd == "account.ban" && len(tokens) > 1 {
		hasKV := false
		for _, t := range tokens[1:] {
			if strings.Contains(t, "=") {
				hasKV = true
				break
			}
		}
		if !hasKV {
			args["name"] = tokens[1]
			rest := tokens[2:]
			if n := len(rest); n > 0 {
				if d, err := strconv.ParseInt(rest[n-1], 10, 64); err == nil && d > 0 {
					args["expires_at"] = float64(time.Now().Add(time.Duration(d) * 24 * time.Hour).Unix())
					rest = rest[:n-1]
				}
			}
			if len(rest) > 0 {
				args["reason"] = strings.Join(rest, " ")
			}
			return cmd, args, nil
		}
	}
	// Texto livre no fim da linha para broadcast/panel/razao de ban:
	// `broadcast Servidor reinicia em 10 min`, `ban gilmar farm bot 7`.
	for i := 1; i < len(tokens); i++ {
		tok := tokens[i]
		if key, value, ok := strings.Cut(tok, "="); ok && key != "" && !strings.Contains(key, " ") {
			if n, err := strconv.ParseInt(value, 10, 64); err == nil {
				args[key] = float64(n) // JSON-friendly
			} else {
				args[key] = value
			}
			continue
		}
		if posIdx < len(positional) {
			key := positional[posIdx]
			if key == "days" || key == "limit" {
				if n, err := strconv.ParseInt(tok, 10, 64); err == nil {
					// dias -> expires_at; limit fica como inteiro
					if key == "days" {
						if n > 0 {
							args["expires_at"] = float64(time.Now().Add(time.Duration(n) * 24 * time.Hour).Unix())
						}
						posIdx++
						continue
					}
					args[key] = float64(n)
					posIdx++
					continue
				}
			}
			if posIdx == len(positional)-1 && (key == "message" || key == "reason") {
				// ultimo posicional e texto livre: acumula o resto da linha.
				args[key] = tok
				posIdx++
				positional = []string{freeTextKey}
				posIdx = 0
				continue
			}
			args[key] = tok
			posIdx++
			continue
		}
		if posIdx == 0 && len(positional) > 0 && positional[0] == freeTextKey {
			args["message"] = tok
			if key, ok := messageKeyFor(cmd); ok {
				args[key] = tok
			}
			continue
		}
		return cmd, nil, fmt.Errorf("argumento excedente %q (use chave=valor)", tok)
	}
	return cmd, args, nil
}

// runConsole e o REPL do --cli: le linhas do stdin, despacha pro backend
// (mesmo handler do painel TCP) e imprime o resultado formatado.
func runConsole(ctx context.Context, h admin.Handler, actor string, stopFunc func()) error {
	reader := bufio.NewScanner(os.Stdin)
	reader.Buffer(make([]byte, 64*1024), 64*1024)
	fmt.Println("console admin pronto — 'help' lista comandos, 'exit' encerra o servidor")
	for {
		fmt.Print("wyd> ")
		if !reader.Scan() {
			return nil // stdin fechou (Ctrl+D): segue servindo
		}
		line := strings.TrimSpace(reader.Text())
		if line == "" {
			continue
		}
		if line == "exit" || line == "quit" {
			fmt.Println("encerrando servidor...")
			if stopFunc != nil {
				stopFunc()
			}
			return nil
		}
		cmd, args, err := parseConsoleLine(line)
		if err != nil {
			fmt.Println("erro:", err)
			continue
		}
		if cmd == "" {
			continue
		}
		callCtx, cancel := context.WithTimeout(ctx, 30*time.Second)
		result, err := h.Handle(callCtx, cmd, args)
		cancel()
		if err != nil {
			fmt.Println("erro:", err)
			continue
		}
		printResult(result)
	}
}

// printResult formata o resultado: strings direto, o resto como JSON.
func printResult(result any) {
	switch v := result.(type) {
	case nil:
		fmt.Println("ok")
	case string:
		fmt.Println(v)
	default:
		b, err := json.MarshalIndent(v, "", "  ")
		if err != nil {
			fmt.Printf("%v\n", v)
			return
		}
		fmt.Println(string(b))
	}
}
