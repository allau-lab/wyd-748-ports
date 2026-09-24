// Comando server -- servidor WYD 7.48 nativo em Go.
//
// Entry fino: carrega dados estaticos + store, sobe o World (game loop) numa
// goroutine e abre o listener, servindo cada conexao pro loop.
package main

import (
	"bufio"
	"context"
	"flag"
	"fmt"
	"log"
	stdhttp "net/http"
	"os"
	"os/signal"
	"strconv"
	"strings"
	"syscall"
	"time"

	// Registra /debug/pprof no mux padrao. So fica acessivel se debug_address
	// estiver configurado, e a configuracao exige loopback.
	_ "net/http/pprof"

	stdnet "net"
	"wydgo/cmd/server/gui"
	"wydgo/internal/admin"
	"wydgo/internal/data"
	"wydgo/internal/game"
	"wydgo/internal/net"
	"wydgo/internal/store"
)

// shutdownTimeout limita a persistencia final. Generoso o bastante para gravar
// centenas de contas, curto o bastante para o systemd nao matar o processo
// antes (o padrao do TimeoutStopSec e 90 s).
const shutdownTimeout = 20 * time.Second

// defaultGeneratorExtraPath mantem conteudo de teste fora do boot normal. O
// overlay continua disponivel por opt-in explicito com -gener-extra.
const defaultGeneratorExtraPath = ""

// serveDebug sobe expvar (/debug/vars) e pprof (/debug/pprof) em loopback. A
// validacao de que o endereco NAO e publico fica em data.LoadServerConfig, que
// derruba o boot em vez de expor o diagnostico.
func serveDebug(address string) {
	log.Printf("diagnostico em http://%s/debug/vars e /debug/pprof (somente loopback)", address)
	server := &stdhttp.Server{
		Addr:              address,
		ReadHeaderTimeout: 5 * time.Second,
	}
	if err := server.ListenAndServe(); err != nil {
		log.Printf("diagnostico: %v", err)
	}
}

func configPathFromArgs(args []string) string {
	const defaultPath = "data/server.txt"
	for i, arg := range args {
		if arg == "-config" || arg == "--config" {
			if i+1 < len(args) {
				return args[i+1]
			}
			return defaultPath
		}
		if value, ok := strings.CutPrefix(arg, "-config="); ok {
			return value
		}
		if value, ok := strings.CutPrefix(arg, "--config="); ok {
			return value
		}
	}
	return defaultPath
}

func main() {
	// Filho do supervisor (--gui): herda __wyd_child=1 e roda o servidor
	// headless normal. A porta ja foi decidida pelo pai (WYD_GAME_ADDR).
	isChild := gui.IsChild()
	if isChild {
		os.Unsetenv(gui.ChildEnvVar)
	}
	configPath := configPathFromArgs(os.Args[1:])
	// Subcomandos de administracao rodam sem subir o mundo (so persistencia)
	// e saem antes do parse das flags do servidor. Sem server.txt usam os
	// padroes — nao faz sentido exigir a config inteira para criar uma conta.
	if len(os.Args) > 1 {
		switch os.Args[1] {
		case "migrate", "account", "db", "serverlist":
			cfg, err := data.LoadServerConfig(configPath)
			if err != nil {
				log.Printf("aviso: %v (usando configuracao padrao)", err)
				cfg = data.DefaultServerConfig()
			}
			os.Exit(runStoreCommand(&cfg, os.Args[1:]))
		}
	}
	cfg, err := data.LoadServerConfig(configPath)
	if err != nil {
		log.Fatal(err)
	}
	flag.String("config", configPath, "arquivo texto de configuracao")
	addr := flag.String("addr", cfg.ListenAddress, "endereco de escuta (host:porta)")
	npcPath := flag.String("npcs", cfg.NPCPath, "pasta de NPCs (um .json por NPC)")
	generPath := flag.String("gener", cfg.GeneratorPath, "arquivo padrao de spawn NPCGener.txt")
	generExtraPath := flag.String("gener-extra", defaultGeneratorExtraPath,
		"arquivo adicional de geradores (ex.: data/NPCGenerTest.txt); vazio desliga")
	teleportPath := flag.String("teleports", cfg.TeleportPath, "arquivo server-side de portais")
	networkAdmissionPath := flag.String("network-admission", cfg.NetworkAdmissionPath,
		"politica server-side de redes VPS/VPN/datacenter")
	clientIntegrityPath := flag.String("client-integrity", cfg.ClientIntegrityPath,
		"manifesto server-side de probes do client")
	accDir := flag.String("accounts", cfg.AccountsPath, "diretorio de contas")
	guildsPath := flag.String("guilds", cfg.GuildsPath, "registro de guilds (guilds.json)")
	guildsTxtPath := flag.String("guilds-txt", cfg.GuildsTxtPath, "Guilds.txt exportado para o client 7.48")
	charStatePath := flag.String("charstate", cfg.CharStatePath, "pasta do estado de sessao (buffs/moedas)")
	questsPath := flag.String("quests", cfg.QuestsPath, "definicoes de quest (quests.json)")
	questZonesPath := flag.String("quest_zones", cfg.QuestZonesPath, "zonas de reset de area (quest_zones.json)")
	initItemsPath := flag.String("init_items", cfg.InitItemsPath, "objetos permanentes do mundo (init_items.csv)")
	bossPath := flag.String("boss", cfg.BossPath, "diretorio dos bosses (data/boss/*.lua)")
	itemPath := flag.String("items", cfg.ItemPath, "itemlist.csv autoritativo")
	itemNamePath := flag.String("itemnames", cfg.ItemNamePath, "Itemname.csv autoritativo")
	itemEffectPath := flag.String("itemeffects", cfg.ItemEffectPath, "ItemEffect.h autoritativo")
	skillPath := flag.String("skills", cfg.SkillPath, "SkillData.csv autoritativo")
	dropRatePath := flag.String("droprates", cfg.DropRatePath, "tabela de drop rate por slot")
	volatilePath := flag.String("volatiles", cfg.VolatilePath, "funcoes server-side dos itens volatile")
	instancesPath := flag.String("instances", cfg.InstancesPath, "configuracao server-side das instancias")
	replictionPath := flag.String("repliction", cfg.ReplictionPath, "tabelas nativas do Repliction")
	mountPath := flag.String("mounts", cfg.MountPath, "atributos das montarias por tipo")
	characterTemplatePath := flag.String("characters", cfg.CharacterTemplatePath, "layouts server-side para criacao de personagem")
	heightMapPath := flag.String("heightmap", cfg.HeightMapPath, "HeightMap.dat nativo do mapa")
	attributeMapPath := flag.String("attributemap", cfg.AttributeMapPath, "AttributeMap.dat nativo do mapa")
	debugAddr := flag.String("debug_address", cfg.DebugAddress,
		"endereco loopback do diagnostico (expvar/pprof); vazio desliga")
	adminAddr := flag.String("admin", "127.0.0.1:7480",
		"endereco do painel administrativo TCP (GUI remota); vazio desliga")
	// Senha do painel, em ordem de prioridade: env WYD_ADMIN_PASSWORD >
	// server.txt (admin_password) > aleatoria logada no boot. O admin_user e
	// informativo (o painel usa senha unica).
	pwDefault := os.Getenv("WYD_ADMIN_PASSWORD")
	if pwDefault == "" {
		pwDefault = cfg.AdminPassword
	}
	adminPassword := flag.String("admin-password", pwDefault,
		"senha do painel admin (env WYD_ADMIN_PASSWORD > server.txt admin_password > aleatoria)")
	cliMode := flag.Bool("cli", false, "console administrativo interativo no stdin")
	guiMode := flag.Bool("gui", false, "janela de administracao SDL3 embutida (build -tags gui)")
	flag.Parse()
	// Modo GUI: a JANELA e o app principal. Este processo vira supervisor e
	// sobe o servidor como processo-filho (re-exec do mesmo binario). O boot
	// pesado (dados, mundo) acontece no filho; a janela abre na hora.
	if *guiMode && !isChild {
		if err := runSupervisor(os.Args, *adminAddr, *adminPassword); err != nil {
			log.Fatal(err)
		}
		return
	}
	// Filho do supervisor: a porta foi decidida pelo pai (interativo) e vem
	// por env — tem prioridade sobre config/flag.
	if v := strings.TrimSpace(os.Getenv("WYD_GAME_ADDR")); v != "" {
		*addr = v
	}
	os.Unsetenv("WYD_GAME_ADDR")
	// Portas decididas ANTES de qualquer carga pesada: em modo GUI, porta de
	// jogo ocupada pergunta no terminal e o operador escolhe outra; o painel
	// admin nunca colide (cai na proxima livre). Assim a janela sempre abre.
	*addr = pickGamePort(*addr, isChild)
	if *adminAddr != "" {
		*adminAddr = pickAdminPort(*adminAddr)
	}
	// A flag sobrescreve o arquivo, entao repete a checagem de loopback: sem
	// isso, -debug_address 0.0.0.0:6060 exporia pprof publicamente.
	if *debugAddr != "" {
		if err := data.ValidateDebugAddress(*debugAddr); err != nil {
			log.Fatalf("debug_address: %v", err)
		}
	}
	log.Printf("configuracao carregada de %s", configPath)
	log.Printf("balanceamento global: exp_minimum=%d exp_rate=%d%% party_exp_bonus=%d%%/membro",
		cfg.Gameplay.EXPMinimum, cfg.Gameplay.EXPRatePercent,
		cfg.Gameplay.PartyEXPBonusPercent)

	npcs, err := data.LoadNPCs(*npcPath)
	if err != nil {
		log.Fatalf("carregar NPCs (%s): %v", *npcPath, err)
	}
	log.Printf("%d NPCs carregados de %s", len(npcs), *npcPath)

	geners, err := data.LoadNPCGener(*generPath)
	if err != nil {
		log.Fatalf("carregar NPCGener (%s): %v", *generPath, err)
	}
	log.Printf("%d geradores carregados de %s", len(geners), *generPath)
	if extraPath := strings.TrimSpace(*generExtraPath); extraPath != "" {
		extraGeners, err := data.LoadNPCGener(extraPath)
		if err != nil {
			log.Fatalf("carregar NPCGener adicional (%s): %v", extraPath, err)
		}
		// LoadNPCGener numera cada arquivo a partir de zero, como a tabela nativa.
		// Ao compor dois arquivos, a ordem efetiva passa a ser a lista combinada:
		// reindexar evita colisao de GenerIndex com os geradores do arquivo base.
		baseIndex := len(geners)
		for i := range extraGeners {
			extraGeners[i].Index = baseIndex + i
		}
		geners = append(geners, extraGeners...)
		log.Printf("%d geradores adicionais carregados de %s", len(extraGeners), extraPath)
	}

	teleports, err := data.LoadTeleports(*teleportPath)
	if err != nil {
		log.Fatalf("carregar teleportes (%s): %v", *teleportPath, err)
	}
	log.Printf("%d teleportes carregados de %s", len(teleports), *teleportPath)

	networkAdmission, err := data.LoadNetworkAdmission(*networkAdmissionPath)
	if err != nil {
		log.Fatalf("carregar politica de admissao de rede (%s): %v", *networkAdmissionPath, err)
	}
	log.Printf("politica de admissao de rede: %d faixa(s) carregada(s)", len(networkAdmission.Rules))

	clientIntegrity, err := data.LoadClientIntegrity(*clientIntegrityPath)
	if err != nil {
		log.Fatalf("carregar manifesto de integridade do client (%s): %v", *clientIntegrityPath, err)
	}
	log.Printf("integridade do client: %d probe(s) carregado(s)", len(clientIntegrity.Probes))

	catalog, err := data.LoadCatalog(*itemPath, *itemNamePath, *itemEffectPath, *skillPath)
	if err != nil {
		log.Fatalf("carregar catalogo: %v", err)
	}
	log.Printf("catalogo server-side: %d itens, %d efeitos e %d skills carregados",
		len(catalog.Items), len(catalog.ItemEffects), len(catalog.Skills))

	dropRates, err := data.LoadDropRates(*dropRatePath)
	if err != nil {
		log.Fatalf("carregar drop rates (%s): %v", *dropRatePath, err)
	}
	log.Printf("tabela de drop por slot carregada de %s", *dropRatePath)

	volatiles, err := data.LoadVolatilesWithInstances(
		*volatilePath, *instancesPath, catalog.Items, catalog.Skills)
	if err != nil {
		log.Fatalf("carregar volatiles/instancias (%s, %s): %v",
			*volatilePath, *instancesPath, err)
	}
	repliction, err := data.LoadRepliction(*replictionPath, catalog.Items)
	if err != nil {
		log.Fatalf("carregar repliction (%s): %v", *replictionPath, err)
	}
	volatiles.Repliction = repliction
	active := 0
	for id := range volatiles.ItemCodes {
		rule, _, _ := volatiles.Rule(id)
		// "generic" ainda nao tem comportamento; qualquer outra acao registrada e
		// uma funcao de jogo real (restore/gold/teleport/buff/grant_exp/...).
		if rule.Action != "" && rule.Action != "generic" {
			active++
		}
	}
	log.Printf("volatiles server-side: %d itens, %d codigos, %d itens com funcao ativa",
		len(volatiles.ItemCodes), len(volatiles.Codes), active)

	mounts, err := data.LoadMounts(*mountPath)
	if err != nil {
		log.Fatalf("carregar montarias (%s): %v", *mountPath, err)
	}
	log.Printf("montarias: %d tipos com bonus de stat (fiel ao g_pMountBonus)", len(mounts.Types))

	characterTemplates, err := data.LoadCharacterTemplates(*characterTemplatePath, catalog.Items)
	if err != nil {
		log.Fatalf("carregar layouts de personagem (%s): %v", *characterTemplatePath, err)
	}
	log.Printf("%d layouts de personagem carregados; nascimento em (%d,%d)",
		len(characterTemplates.Classes), characterTemplates.Spawn.X, characterTemplates.Spawn.Y)

	terrain, err := data.LoadTerrain(*heightMapPath, *attributeMapPath)
	if err != nil {
		log.Fatalf("carregar terreno: %v", err)
	}
	log.Printf("mapas de terreno carregados: %dx%d alturas e %dx%d atributos",
		4096, 4096, 1024, 1024)

	quests, err := data.LoadQuests(*questsPath)
	if err != nil {
		log.Fatalf("carregar quests: %v", err)
	}

	questZones, err := data.LoadQuestZones(*questZonesPath)
	if err != nil {
		log.Fatalf("carregar zonas de quest: %v", err)
	}

	bosses, err := data.LoadBossCatalog(*bossPath)
	if err != nil {
		log.Fatalf("carregar bosses: %v", err)
	}
	log.Printf("%d bosses carregados de %s", len(bosses.Bosses), *bossPath)

	initItems, err := data.LoadInitItems(*initItemsPath, catalog.Items)
	if err != nil {
		log.Fatalf("carregar objetos de mundo: %v", err)
	}

	var st store.Store
	var sqliteStore *store.SQLiteStore
	var postgresStore *store.PostgresStore
	switch cfg.DatabaseDriver {
	case "postgres":
		databaseURL := cfg.DatabaseURL
		if databaseURL == "" {
			databaseURL = os.Getenv(cfg.DatabaseURLEnv)
		}
		if databaseURL == "" {
			log.Fatalf("PostgreSQL configurado, mas %s esta vazia", cfg.DatabaseURLEnv)
		}
		postgresStore, err = store.NewPostgresStore(context.Background(), store.PostgresConfig{
			URL: databaseURL, MaxConns: int32(cfg.DatabaseMaxConns), GuildsTxtPath: *guildsTxtPath,
			OperationTimeout: time.Duration(cfg.CriticalPersistenceTimeoutMS) * time.Millisecond,
		})
		if err != nil {
			log.Fatalf("abrir PostgreSQL: %v", err)
		}
		defer postgresStore.Close()
		st = postgresStore
		log.Printf("persistencia autoritativa: PostgreSQL (pool maximo=%d)", cfg.DatabaseMaxConns)
	case "sqlite":
		// Primeira execucao com dados legados: importa contas JSON/guilds/
		// charstate para o banco antes de abrir. Contas ja presentes no banco
		// NAO sao sobrescritas, entao reexecutar e seguro.
		if _, err := os.Stat(*accDir); err == nil {
			if imported, err := store.MigrateJSONToSQLite(context.Background(), *accDir, cfg.DatabasePath,
				store.WithSQLiteGuildsTxt(*guildsTxtPath)); err == nil && imported > 0 {
				log.Printf("migracao automatica: %d conta(s) importadas de %s", imported, *accDir)
			}
		}
		sqliteStore, err = store.NewSQLiteStore(context.Background(), store.SQLiteConfig{
			Path: cfg.DatabasePath, GuildsTxtPath: *guildsTxtPath,
		})
		if err != nil {
			log.Fatalf("abrir SQLite (%s): %v", cfg.DatabasePath, err)
		}
		defer sqliteStore.Close()
		st = sqliteStore
		log.Printf("persistencia autoritativa: SQLite em %s", cfg.DatabasePath)
	case "json":
		st = store.NewJSONStore(*accDir, store.WithGuildsPath(*guildsPath),
			store.WithGuildsTxtPath(*guildsTxtPath), store.WithCharStatePath(*charStatePath))
		log.Printf("persistencia de desenvolvimento: JSON em %s", *accDir)
	default:
		log.Fatalf("database_driver desconhecido %q", cfg.DatabaseDriver)
	}
	worldOptions := []game.WorldOption{
		game.WithNPCGenerLog(cfg.NPCGenerLog),
		game.WithGameplayLog(cfg.GameplayLog),
		// Gestao do serverlist.bin do client pelo painel (aba Lista da GUI,
		// comandos serverlist.* e CLI). Vazio no server.txt desliga a feature.
		game.WithServerList(cfg.ClientServerListPath, *addr),
		game.WithTeleports(teleports), game.WithGameplayConfig(cfg.Gameplay),
		game.WithNetworkAdmission(networkAdmission),
		game.WithClientIntegrity(clientIntegrity),
		game.WithOperationalConfig(game.OperationalConfig{
			AuthAttemptsPerMinuteIP:      int(cfg.AuthAttemptsPerMinIP),
			AuthAttemptsPerMinuteAccount: int(cfg.AuthAttemptsPerMinAccount),
			MaxAuthenticatedClientsPerIP: int(cfg.MaxAuthenticatedClientsPerIP),
			AuthHashConcurrency:          int(cfg.AuthHashConcurrency),
			WorldCommandQueueCapacity:    int(cfg.WorldCommandQueueCapacity),
			ChatLocalPer10Seconds:        int(cfg.ChatLocalPer10Secs),
			ChatWhisperPer10Seconds:      int(cfg.ChatWhisperPer10Secs),
			ChatGlobalPer10Seconds:       int(cfg.ChatGlobalPer10Secs),
			ChannelID:                    byte(cfg.ChannelID),
		}),
		game.WithQuests(quests), game.WithQuestZones(questZones), game.WithMounts(mounts),
		game.WithBossCatalog(bosses), game.WithInitItems(initItems),
		game.WithLoadtestSpawn(cfg.LoadtestSpawn, cfg.LoadtestAccountPrefix),
	}
	if uxmal, ok := volatiles.Instances["uxmal"]; ok && uxmal.Uxmal != nil {
		worldOptions = append(worldOptions, game.WithUxmal(uxmal))
		log.Printf("Uxmal carregado: %d salas, ticket=%d", len(uxmal.Stages), uxmal.Uxmal.TicketItem)
	}
	world, err := game.NewWorld(st, npcs, geners, catalog, dropRates, volatiles,
		characterTemplates, terrain, worldOptions...)
	if err != nil {
		log.Fatalf("criar mundo: %v", err)
	}
	go world.Run()

	// Painel administrativo: TCP para a GUI remota + console local (--cli)
	// compartilham o mesmo backend/ dispatcher de comandos.
	if *adminAddr != "" || *cliMode {
		backend := &admin.Backend{
			World: world,
			Store: sqliteStore,
			Stop: func() {
				go func() {
					log.Print("desligamento solicitado via admin")
					if world.Shutdown(shutdownTimeout) {
						os.Exit(0)
					}
					os.Exit(1)
				}()
			},
			SaveAll: func() error {
				saved := 0
				err := world.AdminCall(func() { saved = world.AdminSaveAll() })
				if err != nil {
					return err
				}
				log.Printf("[admin] %d conta(s) online persistida(s)", saved)
				return nil
			},
		}
		if *adminAddr != "" {
			svc := admin.NewService(admin.Config{
				ListenAddress: *adminAddr,
				Password:      *adminPassword,
			}, backend)
			go func() {
				if err := svc.ListenAndServe(context.Background()); err != nil {
					log.Printf("painel admin: %v", err)
				}
			}()
		}
		if *cliMode {
			go func() {
				if err := runConsole(context.Background(), backend, "console", func() {
					if world.Shutdown(shutdownTimeout) {
						os.Exit(0)
					}
					os.Exit(1)
				}); err != nil {
					log.Printf("console: %v", err)
				}
			}()
		}
	}

	if *debugAddr != "" {
		go serveDebug(*debugAddr)
	}

	// SIGTERM (systemd/deploy) e SIGINT (Ctrl+C) persistem antes de sair. Sem
	// isso o que estiver na fila de autosave e descartado e o jogador volta com
	// estado velho.
	signals := make(chan os.Signal, 1)
	signal.Notify(signals, syscall.SIGTERM, syscall.SIGINT)
	go func() {
		sig := <-signals
		log.Printf("sinal %v recebido: persistindo estado antes de sair", sig)
		if world.Shutdown(shutdownTimeout) {
			log.Print("desligamento concluido")
			os.Exit(0)
		}
		// Drain incompleto ja foi logado por Shutdown; sair com codigo != 0
		// deixa isso visivel no systemd.
		os.Exit(1)
	}()

	listenerConfig := net.ListenerConfig{
		OutputQueueSize:      int(cfg.SessionQueueCapacity),
		MaxConnections:       int(cfg.MaxConnections),
		MaxConnectionsPerIP:  int(cfg.MaxConnectionsPerIP),
		HandshakeTimeout:     time.Duration(cfg.HandshakeTimeoutSecs) * time.Second,
		SessionIdleTimeout:   time.Duration(cfg.SessionIdleTimeoutSecs) * time.Second,
		FrameReadTimeout:     time.Duration(cfg.FrameReadTimeoutSecs) * time.Second,
		InboundPacketsPerSec: int(cfg.InboundPacketsPerSec),
		InboundBytesPerSec:   int(cfg.InboundBytesPerSec),
	}
	if err := net.ListenWithConfig(*addr, listenerConfig, func(s *net.Session) { s.Serve(world.Enqueue) }); err != nil {
		log.Fatalf("listen: %v", err)
	}
}

// runSupervisor executa o modo --gui: este processo NAO carrega o mundo —
// vira apenas o app da janela de administracao e supervisiona o servidor do
// jogo como processo-filho (re-exec deste binario). Antes de spawnar o filho,
// decide a porta do jogo (pergunta no terminal se ocupada — e este processo
// tem stdin interativo, entao a pergunta funciona) e grava a escolha na env
// para o filho respeitar.
func runSupervisor(argv []string, adminAddr, adminPassword string) error {
	// A porta do jogo e resolvida AQUI (pai, interativo). O resultado vai para
	// o filho via WYD_GAME_ADDR, que tem prioridade sobre config/flag.
	configPath := configPathFromArgs(argv[1:])
	cfg, err := data.LoadServerConfig(configPath)
	if err != nil {
		cfg = data.DefaultServerConfig()
	}
	gameAddr := cfg.ListenAddress
	for i, a := range argv {
		if a == "-addr" || a == "--addr" {
			if i+1 < len(argv) {
				gameAddr = argv[i+1]
			}
		} else if v, ok := strings.CutPrefix(a, "-addr="); ok {
			gameAddr = v
		} else if v, ok := strings.CutPrefix(a, "--addr="); ok {
			gameAddr = v
		}
	}
	gameAddr = pickGamePort(gameAddr, true)

	sup, err := gui.NewSupervisor(argv, "logs/gui-child.log")
	if err != nil {
		return err
	}
	sup.SetEnv(append(os.Environ(), "WYD_GAME_ADDR="+gameAddr))
	gui.BindSupervisor(sup)
	// Auto-start: o operador pediu para subir o servidor; Iniciar na janela
	// re-starta depois de um stop manual.
	if err := sup.Start(); err != nil {
		return err
	}
	log.Printf("[gui] servidor do jogo como processo-filho (log: %s)", sup.LogPath())
	// Janela conecta no painel do FILHO. A porta do painel e do pai; o filho
	// escolhe a propria (pode ser alternativa se 7480 estiver ocupada). A
	// senha vai vazia: o login manual mostra a senha default (admin748) —
	// e WYD_ADMIN_PASSWORD herdada pelo filho respeita a escolha do operador.
	_, port, perr := stdnet.SplitHostPort(adminAddr)
	if perr != nil || port == "" {
		port = "7480"
	}
	return gui.StartWindow("127.0.0.1:"+port, adminPassword)
}

// reservePort testa se host:porta esta livre. Devolve um closer para liberar
// a porta de teste (chame com defer) ou erro se ocupada/invalida.
func reservePort(addr string) (func(), error) {
	ln, err := stdnet.Listen("tcp", addr)
	if err != nil {
		return nil, err
	}
	return func() { _ = ln.Close() }, nil
}

// pickGamePort garante uma porta livre para o jogo. Em modo GUI, porta
// ocupada pergunta no terminal: aceitar sugestao (proxima livre) ou digitar
// outra. Sem GUI (headless/systemd) falha como antes — ninguem para ler
// pergunta em boot de servico.
func pickGamePort(addr string, gui bool) string {
	release, err := reservePort(addr)
	if err == nil {
		release()
		return addr
	}
	host, portText, perr := stdnet.SplitHostPort(addr)
	if perr != nil {
		return addr // deixa o erro original sair no listen real
	}
	port, _ := strconv.Atoi(portText)
	suggest := port
	for i := 0; i < 32; i++ {
		suggest++
		if suggest > 65535 {
			break
		}
		if r, err := reservePort(stdnet.JoinHostPort(host, strconv.Itoa(suggest))); err == nil {
			r()
			break
		}
	}
	if !gui {
		log.Fatalf("porta do jogo %d ocupada (%v); libere-a ou use -addr %s:%d",
			port, err, host, suggest)
	}
	fmt.Printf("\n=== A porta do jogo %d esta OCUPADA (%v).\n", port, err)
	fmt.Printf("    Sugestao de porta livre: %d\n", suggest)
	fmt.Printf("    ENTER aceita a sugestao, ou digite outra porta (0 = cancelar): ")
	reader := bufio.NewReader(os.Stdin)
	line, _ := reader.ReadString('\n')
	line = strings.TrimSpace(line)
	if line == "" {
		line = strconv.Itoa(suggest)
	}
	choice, cerr := strconv.Atoi(line)
	if cerr != nil || choice == 0 {
		log.Fatalf("iniciulo cancelado pelo operador (porta %d ocupada)", port)
	}
	if choice < 1 || choice > 65535 {
		log.Fatalf("porta %d invalida", choice)
	}
	next := stdnet.JoinHostPort(host, strconv.Itoa(choice))
	if r, rerr := reservePort(next); rerr != nil {
		log.Fatalf("porta %d tambem ocupada: %v", choice, rerr)
	} else {
		r()
	}
	return next
}

// pickAdminPort idem para o painel admin TCP (127.0.0.1:7480): se ocupado
// (outro wydserver rodando, por exemplo), sobe na proxima livre em vez de
// duplicar listener.
func pickAdminPort(addr string) string {
	release, err := reservePort(addr)
	if err == nil {
		release()
		return addr
	}
	host, _, perr := stdnet.SplitHostPort(addr)
	if perr != nil {
		return addr
	}
	for p := 17481; p < 17490; p++ {
		next := stdnet.JoinHostPort(host, strconv.Itoa(p))
		if r, rerr := reservePort(next); rerr == nil {
			r()
			log.Printf("[admin] porta %s ocupada; painel alternativo em %s", addr, next)
			return next
		}
	}
	return addr // deixa o erro original
}
