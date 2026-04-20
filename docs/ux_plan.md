# Plano de UX — FitaDigital

**Contexto do produto:** Firmware para ESP32-S3-Touch-LCD-4.3B (800×480, LVGL 8.3)
que substitui impressora de papel em autoclaves e equipamentos industriais/hospitalares.
Captura linhas por RS485 (9600 8N1 tipico) e grava em `/CICLOS/YYYY/MM/YYYYMMDD.txt`
no SD. Portal web (AsyncWebServer) e FTP (SimpleFTPServer) disponiveis.

**Usuario-alvo:** tecnico ou operador em chao de fabrica / sala hospitalar. Precisa
confiar rapido que o aparelho esta a funcionar sem ler manual.

**Tema visual:**
- Cor primaria: `#449d48` (verde institucional)
- Cor de destaque (accent): a definir, harmonico com a primaria (candidatos: laranja `#F29E38`, ambar `#F5B841`, azul complementar)
- Cor sucesso: variacao da primaria (`#2E7D32` ou similar)
- Cor erro/destrutivo: vermelho industrial (`#C62828`)
- Cor aviso: ambar

**Como usar este plano:** sempre atualizar com decisoes, variacoes e estados
(Pendente / Em progresso / Feito). Ordem de execucao: Tier 1 antes de Tier 2.

---

## Tier 1 — Essenciais (operador precisa confiar no aparelho)

### 1. Indicador de conectividade remota (ping monitor)  `Feito`

**Decisao:** o indicador NAO e' do RS485. E' de um IP de referencia configuravel,
testado com ping a cada 1 min.

**Comportamento:**
- Campo novo em Configuracoes → Rede: "IP de monitorizacao" (default vazio ou
  placeholder tipo `192.168.0.1`).
- Task que faz ICMP ping a cada 60 s para o IP configurado.
- Na status bar, icone/LED com 3 estados:
  - Verde: ultimo ping OK (ou dentro da janela)
  - Amarelo: a testar / pending
  - Vermelho: falhou o ultimo ping
- Se campo vazio, indicador oculto.
- Resultado do ultimo ping pode ir para `app_log` (timestamp + latencia).

**Arquivos provaveis:** `app_settings.*` (NVS), `net_services.*` (task de ping),
`ui_app.cpp` (campo na tabview Settings + icone na status bar).

**Notas tecnicas:** Arduino-ESP32 tem `Ping.h`? Se nao, TCP connect na porta 80
serve como proxy de reachability. Prefer ICMP se viavel (lwip_raw).

---

### 2. Dashboard / tela home com resumo de estado  `Pendente`

Substitui (ou precede) a entrada direta no file_browser.

**Conteudo proposto:**
- Bloco Wi-Fi: SSID + IP + RSSI em barrinha
- Bloco SD: % usado + espaco livre MB
- Bloco FTP: ON/OFF + porta
- Bloco NTP: hora sincronizada + "ultima sync ha X s"
- Bloco Captura RS485: baudrate + total de linhas do dia + timestamp da ultima
- Bloco Remoto: IP monitorizado + resultado ultimo ping (integracao com #1)
- Acao rapida: "Abrir ciclo de hoje" (atalho para viewer do `.txt` do dia)
- Acao rapida: "Ver historico" (abre file_browser)

**Notas:** e' a base arquitetural para toasts, para indicadores visuais e
para onde inserir o QR code do #6.

---

### 3. Breadcrumb + voltar sempre visivel  `Pendente`

No file_browser, quando em `/CICLOS/2026/04`:
- Header com breadcrumb clicavel: `sd > CICLOS > 2026 > 04`
- Cada segmento navega para o nivel correspondente.
- Botao "voltar" grande no topo-esquerda (ja existe entry ".." mas nao e' obvio).

**Arquivos:** `file_browser.cpp` (header + handler de clique por segmento).

---

### 4. Toast / notificacoes transitorias  `Pendente`

Componente reusavel que aparece no canto (bottom-right ou bottom-center),
desaparece em 2-4 s. Usar para:
- "Configuracoes salvas" (atual: label estatica que some ao sair da aba)
- "Wi-Fi conectado / desconectado"
- "FTP reiniciado"
- "Ping falhou 3x seguidas"

API simples: `toast_show(kind, "mensagem")` com kinds `info|success|warn|error`.

---

## Tier 2 — Operacoes que faltam

### 5. Apagar ficheiro via UI  `Descartado`

**Decisao do user:** manter apagamento apenas via FTP; UI nao oferece apagar.

---

### 6. QR code / URL remota para baixar ciclo  `Pendente`

**Caso de uso:** um servidor remoto (central de gestao) vai conectar-se ao
dispositivo e baixar ficheiros. A URL base e' configuravel; o dispositivo
adiciona `?path=<caminho/do/ficheiro>`.

**Configuracao:**
- Campo em Configuracoes → Rede: "URL de download remoto" (ex.:
  `http://servidor.empresa/download`). Validacao: precisa comecar com `http://`
  ou `https://`.

**UI:**
- No viewer do ficheiro, novo botao "Partilhar" / icone QR.
- Ao tocar, overlay com:
  - QR code encodando `<url_configurada>?path=<path_url_encoded>`
  - Texto abaixo com a URL completa em plain (para ler a olho se o QR nao
    for viavel).
- Fechar ao toque fora.

**Libs candidatas:** `qrcode-esp32` (biblioteca leve) ou LVGL `lv_qrcode` se
disponivel no build atual (LVGL 8.3 o tem opcional).

---

### 7. Filtro por data no file_browser  `Pendente`

Campo de input ou picker no topo: "aaaa/mm" → filtra a listagem corrente. Uso
tipico: 300+ dias de historico.

---

### 8. Jump-to-line no viewer  `Pendente`

Botao "Ir para linha N". Util em ciclos longos (2h autoclave = centenas de
linhas).

---

## Tier 3 — Polimento visual

### 9. Tokens de tema centralizados  `Pendente`

**Decisao do user:** primaria `#449d48`. Accent a definir.

**Plano:**
- Novo header `src/ui/ui_theme.h` com constantes:
  - `UI_COLOR_PRIMARY = lv_color_hex(0x449D48)`
  - `UI_COLOR_PRIMARY_DARK`, `UI_COLOR_PRIMARY_LIGHT`
  - `UI_COLOR_ACCENT`, `UI_COLOR_SUCCESS`, `UI_COLOR_WARN`, `UI_COLOR_ERROR`
  - `UI_COLOR_BG`, `UI_COLOR_SURFACE`, `UI_COLOR_TEXT_PRIMARY`, `UI_COLOR_TEXT_MUTED`
- Substituir hardcodes pontuais (ex.: `0x007AFF` no botao Voltar do file_browser).
- Aplicar tema LVGL na inicializacao (`lv_theme_default_init` com a primaria
  ou um theme customizado leve).

**Sugestao de accent** para validar: `#F29E38` (laranja quente) — tem bom
contraste sobre o verde `#449D48` e e' legivel tanto em fundo claro como
escuro. Alternativa sobria: `#2C6E34` (verde mais escuro) para accent do mesmo
hue. Decidir apos mockup.

---

### 10. Modo dark / alto contraste  `Pendente`

Switch em Configuracoes → UI. Valor persistido em NVS.

---

### 11. Estado visual "armed" da formatacao SD  `Pendente`

Hoje e' two-step booleano. Adicionar: botao muda para vermelho ao armar e
mostra contador de 5 s para auto-desarmar.

---

### 12. Animacao do icone de Wi-Fi durante conexao  `Pendente`

Ao inves de `"..."` como label, fazer pulse no icone + animar barras.

---

## Tier 4 — Extras (avaliar depois)

- Mini-grafico de linhas/s ao longo do dia na home.
- Modo quiosque / lock screen.
- Rotacao portrait/landscape automatica.

---

## Historico

- **2026-04-19** — Plano inicial criado. Decisoes do user:
  - Item 1: indicador e' de conectividade remota (ping), nao RS485. IP
    configuravel. ICMP puro. Icone a esquerda da status bar.
  - Item 5: descartado — apagamento so via FTP.
  - Item 6: URL de download configuravel (com `?path=`).
  - Item 9: cor primaria `#449D48`. Accent a decidir.
  - Ordem de execucao: A (Tier 1 completo, um a um) → B → C.
- **2026-04-19** — Item 1 concluido. Refinamentos em cima da proposta inicial:
  - Indicador passou de bolinha 14 px para **oblongo (pill)** centrado, largura
    ~1/5 do ecra, altura 30 px, cantos arredondados, label branco.
  - Quatro rotulos: "Conectado" (verde), "Desconectado" (vermelho), "Configure"
    (cinza) quando IP vazio e Wi-Fi up, "A testar..." (ambar) durante ping.
  - Estado Desconectado ganha animacao de **respiracao** (opacidade 100% ↔ 30%,
    800 ms por ciclo, ease-in-out).
  - Wi-Fi em baixo => Desconectado (antes: Configure). "Configure" reservado ao
    caso de IP do monitor vazio com Wi-Fi ok.

- **2026-04-19** — Item 1 em implementacao. Decisoes tecnicas:
  - Nova secao `[net]` em `fdigi.cfg` com chave `mon_ip` (max. 63 chars).
  - NVS key: `mon_ip`. Getter/setter em `app_settings_monitor_ip()`.
  - `src/net_monitor.cpp/h` novo: task com `esp_ping_new_session` (count=1,
    timeout 2 s). Intervalo de 60 s entre sondagens, gatilha log `INFO/WARN` em
    transicao de estado.
  - Status bar: bolinha 14x14 px a esquerda do icone Wi-Fi (cinza=pending,
    verde=ok, vermelho=fail, oculto=disabled).
  - UI de configuracao: campo "IP de monitorizacao" na aba Wi-Fi das
    Definicoes com teclado partilhado.
