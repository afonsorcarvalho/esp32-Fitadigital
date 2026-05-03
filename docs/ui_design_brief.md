# Brief de design de telas — FitaDigital

Documento para passar ao Claude Design (ou outro designer) produzir mockups
de alta fidelidade das duas telas centrais do aparelho.

---

## 0. Parametros fixos do produto

- **Hardware:** Waveshare ESP32-S3-Touch-LCD-4.3B.
- **Resolucao do ecra:** 800 x 480 pixels, landscape (orientacao unica).
- **Touch:** capacitivo (GT911), gestos basicos e tap.
- **Renderer:** LVGL 8.3 (o designer nao precisa gerar codigo, mas convem
  pensar em elementos que existam como widgets LVGL: label, button, roller,
  list, table, keyboard, qrcode, img).
- **Tipografia disponivel:** Montserrat 14 / 16 / 18 / 20 / 24 / 28
  (bundled na build). Sem fallback CJK, sem italic. Preferir Montserrat em
  todas as mockups.
- **Paleta (JA DECIDIDA, nao alterar):**
  - Primaria: `#449D48` (verde institucional AFR).
  - Primaria escura: `#2A6B2E`.
  - Primaria clara: `#E8F1E9` (fundos de chip navegavel).
  - Sucesso: `#2E7D32`.
  - Erro: `#C62828`.
  - Aviso: `#F5B841`.
  - Info / neutro: `#606060`.
  - Fundo da app: `#FFFFFF`.
  - Superficie de cartao: `#FAFAFA` com borda `#CCCCCC`.
- **Status bar** (sempre visivel em main e settings, 46 px de altura):
  - Esquerda: **pill** (oblongo, cantos arredondados completos), largura
    ~1/5 da tela (~160 px), altura 30 px. Conteudo:
    - "Conectado" (verde #2E7D32) + sem animacao.
    - "Desconectado" (vermelho #C62828) com animacao de **respiracao**
      (opacidade 100% ↔ 30%, 800 ms ease-in-out).
    - "A testar..." (ambar #F5B841).
    - "Configure" (cinza #606060) quando Wi-Fi ok mas IP alvo vazio.
  - Centro: icone Wi-Fi + percentagem (ex.: "📶 72%").
  - Direita: data/hora "DD/MM/AAAA HH:MM" + botao Home (icone casa, so
    aparece quando estamos no file browser, nao no dashboard) + botao
    Engrenagem (abre Configuracoes). Ambos em verde primario.
- **Botoes globais:** fundo verde primario `#449D48`, texto branco, cantos
  arredondados suaves (raio ~4 px, default do LVGL), sem sombra.

---

## 1. Tela HOME / Dashboard

### Proposito
E' a primeira tela que o operador ve. Tem de responder em 2 segundos a:

1. "O aparelho esta a funcionar?" (ping, Wi-Fi, SD).
2. "Recebeu ciclo hoje?" (linhas gravadas + hora da ultima).
3. "Como vou ver o historico?" (botoes de acao visiveis).

### Layout (top a bottom)

**Status bar (46 px):** pill central + Wi-Fi + hora + engrenagem
(descrito no topo).

**Grade de 6 cartoes (2 colunas x 3 linhas):** cada cartao tem fundo
`#FAFAFA`, borda `#CCCCCC`, raio 8 px, padding 8 px, espacamento entre
cartoes 8 px. Altura auto (content-size).

Cada cartao tem:
- **Cabecalho:** icone (em verde primario `#449D48`) + titulo em texto
  normal. Fonte Montserrat 14 ou 16, cor primaria.
- **Valor principal** abaixo, em cor texto padrao (`#606060` ou `#000000`).
- Opcional **segunda linha** em cinza mais claro (`#606060` ou `#808080`).

Os 6 cartoes:

1. **🔁 Captura RS485**
   - "9600 8N1" (ou baudrate actual)
   - "Ultima: 14:22:58" (HH:MM:SS da ultima linha gravada)

2. **📄 Hoje**
   - "42 linhas gravadas"
   - "Ultima: 14:22:58"

3. **💾 SD**
   - "12.3 GB livres (4% usado)"
   - (sem segunda linha)

4. **🔔 NTP**
   - "Sincronizado" / "Desativado" / "Sem sincronizacao"

5. **📤 FTP**
   - "Ativo - porta 21" / "Inativo (sem Wi-Fi)" / "Nao configurado"

6. **📶 Wi-Fi**
   - "NomeDaRede"
   - "192.168.0.197 - -47 dBm"

**Linha de accoes (2 botoes grandes):**
- Altura 102 px (grande, tocavel, industrial).
- Fonte Montserrat 20 no label.
- Fundo verde primario `#449D48`, texto branco.
- Ocupam largura igual (flex-grow: 1 cada).

1. **"👁️ Abrir ciclo de hoje"** (icone olho ou simbolo de leitura).
2. **"📁 Ver historico"** (icone pasta).

**Rodape (altura auto, colado a borda inferior com 5 px de padding):**
- Esquerda: logo AFR verde (PNG 120x37 px), imagem da marca.
- Direita: "FITADIGITAL 1.02v" em cinza `#606060`, Montserrat 14.

### Toque industrial
O operador usa este ecra com luvas latex/vinil finas e a distancia de
50-80 cm. Por isso: botoes e pill largos, icones ≥ 20 px, contraste
forte. **Nada abaixo de 44 px de area tocavel util.**

---

## 2. Tela FILE BROWSER / Historico

### Proposito
Navegar o SD (a raiz `/sd` ou sub-pastas como `/CICLOS/2026/04/`) e
seleccionar um ficheiro `.txt` para abrir no viewer.

### Layout (top a bottom)

**Status bar (46 px):** igual a home, mas agora com o botao **Home**
(casa) visivel entre a hora e a engrenagem, para voltar ao dashboard.

**Barra de navegacao (breadcrumb + ir-para-data):** uma linha flex.
- **Breadcrumb (chips clicaveis):** sequencia a partir do Home:
  ```
  [🏠 sd]  [CICLOS]  [2026]  [04]
  ```
  - Chip inicial "🏠 sd" (icone de casa + "sd") em verde-claro
    `#E8F1E9` com texto verde escuro `#2A6B2E`, cantos muito arredondados
    (raio ~14 px). **Clicavel** — salta para a raiz.
  - Chips intermedios (`CICLOS`, `2026`): mesma formatacao verde-claro.
    **Clicaveis** — saltam para esse nivel.
  - Chip final (pasta actual, ex.: `04`): verde solido `#449D48`, texto
    branco. **Nao** clicavel (ja' estamos la).
  - Se o caminho for muito profundo (>3 segmentos alem do Home), mostrar
    `...` em cinza antes dos ultimos 3 segmentos.

- **A direita do breadcrumb:** botao verde primario **"📋 Ir para data"**
  (altura 36 px). So visivel quando estamos dentro de `/CICLOS`. Ao
  clicar abre um **modal** com 3 rollers (dia / mes / ano) em Montserrat
  28, tamanho grande, com backdrop escuro.

**Lista de ficheiros/pastas:** ocupa o resto do ecra (flex-grow: 1).
- Cada item tem altura minima 52 px.
- **Icone a esquerda** (Montserrat 20, cor primaria `#449D48`):
  - Pasta: icone de pasta (LV_SYMBOL_DIRECTORY).
  - Ficheiro: icone de documento (LV_SYMBOL_FILE).
- Texto alinhado em colunas:
  ```
  📁 CICLOS                  <DIR>        15/04/2026 14:22
  📁 2026                    <DIR>        15/04/2026 14:22
  📄 20260419.txt              810 B      19/04/2026 14:23
  ```
- Fonte monoespacada para alinhar valores (o firmware usa Unscii mas
  qualquer monoespacada alfa-numerica serve no mockup).
- **Sem entrada ".." no topo** — navegacao ascendente e' pelo breadcrumb.
- Cor de texto padrao `#000000`; tap feedback subtil (cinza claro).

**Sem barra de botoes no rodape.** A lista vai ate baixo.

### Estados vazios / de erro (incluir no mockup)

- **SD nao montado:** em vez da lista, centralizado: "SD nao montado.\n
  Use cartao FAT32 MBR." em cor neutra.
- **Pasta vazia:** lista em branco (nao mostrar mensagem especial).

---

## 3. Componentes partilhados a documentar

### Toast / notificacao transitoria
Aparece no canto **inferior-direito** (margem 16 px das bordas), sobre
qualquer tela. Single-instance. Fade-in 180 ms, fade-out 220 ms apos
2500 ms. Largura auto-content, altura auto.

- **Success** (fundo `#449D48`, texto branco): "Guardado.", "FTP
  reiniciado.", etc.
- **Error** (fundo `#C62828`): erros criticos.
- **Warn** (fundo `#F5B841`): avisos.
- **Info** (fundo `#606060`): neutros.

Raio 10 px, padding horizontal 18 px / vertical 10 px, sombra discreta
(14 px, cor preto com 30% opacidade).

### Modal (partilha QR e data)
Fundo escurecido (`#000000 @ 60%`) cobre todo o ecra (clicavel para
fechar). Cartao branco centralizado, raio 12 px, padding 16 px. Titulo
no topo com icone + texto em Montserrat 24 (ou 20), cor primaria. Botao
**"Fechar"** em baixo, alinhado a direita ou centralizado conforme o
caso.

### Viewer de ficheiro texto
Quando o utilizador toca num `.txt`/`.log`, aparece overlay que cobre
a area abaixo da status bar. Barra superior com:
- Botao **"← Voltar"** verde primario.
- Botao **"📤 Partilhar"** verde primario (abre modal com QR + URL).
- Nome do ficheiro (monoespacado, cinza `#CCCCCC`).
- Contador "Linhas X-Y de Z (N KB)" em cinza `#888888`.

Conteudo central: tabela com linhas numeradas a esquerda e texto da
linha a direita em monoespacada.

Botoes a direita, empilhados verticalmente:
- `↑` (Page Up)
- `↓` (Page Down)
- `🏠` (Home / inicio)
- `▶️` (End / fim)

Todos em verde primario.

---

## 4. O que pedir ao designer

Entregaveis desejados:

1. **Mockup de alta fidelidade** da **Tela HOME** em 800x480, estado
   "tudo a funcionar" (Wi-Fi ligado, ping OK, SD com 4% ocupado,
   42 linhas hoje, ultima ha 3 s).
2. **Mockup do HOME** em estado **degradado**: pill vermelho com
   "Desconectado" (respiracao), Wi-Fi desligado, FTP "Inativo (sem
   Wi-Fi)". Serve para comunicar o estado de alarme.
3. **Mockup da Tela FILE BROWSER** em 800x480 dentro de `/CICLOS/2026/04`,
   mostrando 3-5 ficheiros `.txt` com tamanhos e datas realistas e o
   botao "Ir para data" visivel.
4. **Mockup do MODAL "Ir para data"** com 3 rollers (dia=19, mes=04,
   ano=2026) pre-seleccionados.
5. **Mockup do MODAL DE PARTILHA (QR)** com um QR code generico
   (240 x 240 px), URL `http://192.168.0.197/api/fs/file?path=...`
   abaixo e botao "Fechar".
6. **Mockup de toast** em cada uma das 4 variantes (success, error,
   warn, info) sobre a home.

Preferencia: **PNG em 2x** (1600 x 960) para aproveitar densidade no
material comercial; formato Figma seria ideal para iteracao futura.

### Exemplos de texto realistas (usar nos mockups)

- Ultimo ciclo: `20260419.txt` (810 bytes, 10 linhas).
- Linhas do ciclo: texto do tipo `PROG P/03 CICLO 124 T=121 C 18.5
  MIN OK` (em monoespacada).
- SSID de exemplo: `FitaDigital_Lab`.
- IP monitorizado: `192.168.0.1`.

---

## 5. Principios transversais

- **Menos e' mais.** Ecra industrial, nao dashboard de consumo — evitar
  ornamentos desnecessarios.
- **Verde primario e' a marca.** Usar generosamente em actions e
  headers; nunca em textos de corpo.
- **Estados extremos** (erro, sem ligacao, sem SD) devem ser
  **evidentes de longe** com cor + texto, nao so icone.
- **Nao usar sombras profundas** (o LVGL aguenta, mas no ecra 800x480
  com gamma industrial fica poluido).
- **Sem dark mode** por agora (pode ser Tier 3 no futuro).

---

**Metadados do projeto:**
- Produto: FitaDigital
- Fabricante: AFR Soluções Inteligentes
- Versao do firmware nos mockups: 1.02v
- Ano dos exemplos: 2026
