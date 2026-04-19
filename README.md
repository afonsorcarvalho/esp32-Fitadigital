# FitaDigital — Firmware ESP32-S3-Touch-LCD-4.3B

Firmware embarcado para a placa **Waveshare ESP32-S3-Touch-LCD-4.3B**, desenvolvido com
**Arduino (ESP-IDF)** e **LVGL 8.3** sobre **PlatformIO**.

O sistema oferece interface gráfica touch, explorador de arquivos no cartão SD,
servidor FTP, portal web de configuração, sincronização de relógio (NTP + RTC externo)
e túnel VPN WireGuard — tudo gerenciado por tarefas FreeRTOS dedicadas.

---

## Sumário

- [Hardware Alvo](#hardware-alvo)
- [Estrutura do Projeto](#estrutura-do-projeto)
- [Arquitetura Geral](#arquitetura-geral)
- [Sequência de Boot](#sequência-de-boot)
- [Módulos e Features](#módulos-e-features)
  - [Interface Gráfica (LVGL)](#interface-gráfica-lvgl)
  - [Explorador de Arquivos](#explorador-de-arquivos)
  - [Acesso ao Cartão SD](#acesso-ao-cartão-sd)
  - [Wi-Fi (STA)](#wi-fi-sta)
  - [Servidor FTP](#servidor-ftp)
  - [Portal Web (HTTP + WebSocket)](#portal-web-http--websocket)
  - [Relógio — NTP e RTC Externo](#relógio--ntp-e-rtc-externo)
  - [WireGuard (VPN)](#wireguard-vpn)
  - [Sistema de Configurações (NVS + SD)](#sistema-de-configurações-nvs--sd)
  - [Sistema de Log](#sistema-de-log)
  - [Journal de Boot](#journal-de-boot)
  - [mDNS](#mdns)
  - [Depuração de Tarefas](#depuração-de-tarefas)
- [Tarefas FreeRTOS](#tarefas-freertos)
- [Mapa de Pinos e Periféricos](#mapa-de-pinos-e-periféricos)
- [Dependências e Bibliotecas](#dependências-e-bibliotecas)
- [Configuração de Build](#configuração-de-build)
- [API REST do Portal Web](#api-rest-do-portal-web)
- [Arquivo de Configuração SD (`fdigi.cfg`)](#arquivo-de-configuração-sd-fdigicfg)

---

## Hardware Alvo


| Item             | Especificação                                          |
| ---------------- | ------------------------------------------------------ |
| **SoC**          | ESP32-S3 (dual-core Xtensa LX7)                        |
| **Placa**        | Waveshare ESP32-S3-Touch-LCD-4.3B                      |
| **Display**      | 4.3" LCD RGB 800×480 (ST7262), touch capacitivo        |
| **Flash**        | 16 MB (QIO, 80 MHz)                                    |
| **PSRAM**        | OPI (Octal SPI)                                        |
| **Expansor I/O** | CH422G (I2C) — controla CS do cartão SD                |
| **Cartão SD**    | Slot TF via SPI (GPIO11/12/13, CS via CH422G EXIO4)    |
| **RTC externo**  | PCF85063A, I2C endereço `0x51` (GPIO8 SDA / GPIO9 SCL) |
| **USB**          | UART para upload/log (`ARDUINO_USB_CDC_ON_BOOT=0`)     |
| **Buzzer**       | Opcional (GPIO livre, desativado por padrão)           |


---

## Estrutura do Projeto

```
ESP32-S3-TOUCH-LCD-4.3B/
├── platformio.ini              # Configuração de build (PlatformIO)
├── sdkconfig.defaults          # Opções ESP-IDF (FreeRTOS stats, lwIP, PM)
├── src/
│   ├── app.cpp                 # Ponto de entrada: setup() e loop()
│   ├── app_settings.cpp/.h     # Preferências persistentes (NVS + espelho SD)
│   ├── app_settings_sd.h       # Import/export de configuração via fdigi.cfg
│   ├── app_log.cpp/.h          # Logger rotativo em arquivo no SD
│   ├── boot_journal.cpp/.h     # Journal de boot (RAM → SPIFFS → SD)
│   ├── net_services.cpp/.h     # Wi-Fi STA + servidor FTP (SimpleFTPServer)
│   ├── net_time.cpp/.h         # NTP + RTC PCF85063A (I2C)
│   ├── net_wireguard.cpp/.h    # Túnel VPN WireGuard
│   ├── sd_access.cpp/.h        # Fila serializada de acesso ao SD (task sd_io)
│   ├── lvgl_port_v8.cpp/.h     # Port LVGL 8: display RGB, touch, anti-tear
│   ├── ui_feedback.cpp/.h      # Bip ao toque (buzzer opcional via LEDC)
│   ├── task_debug.cpp/.h       # Snapshot de tarefas FreeRTOS (Serial)
│   ├── board_pins.h            # Mapeamento de pinos SPI TF e expansor
│   ├── waveshare_sd_cs.cpp/.h  # Controle de CS do SD via CH422G
│   ├── lv_conf.h               # Configuração LVGL (16bpp, heap PSRAM)
│   ├── ESP_Panel_Board_Supported.h  # Seleção da placa Waveshare
│   ├── ESP_Panel_Conf.h        # Limites do painel touch
│   ├── ESP_Panel_Board_Custom.h     # Template custom (não utilizado)
│   ├── ui/
│   │   ├── ui_app.cpp/.h       # Telas principais, definições e navegação
│   │   ├── boot_screen.cpp/.h  # Tela de boot(checklist terminal)
│   │   ├── splash_screen.cpp/.h  # Splash com logo AFR
│   │   ├── file_browser.cpp/.h # Explorador de arquivos do SD
│   │   ├── ui_loading.cpp/.h   # Overlay spinner de carregamento
│   │   └── afr_logo.c          # Asset LVGL (imagem do logo)
│   ├── web_portal/
│   │   ├── web_portal.cpp/.h   # AsyncWebServer (HTTP + WebSocket)
│   │   └── web_portal_html.h   # HTML/CSS/JS embutido do portal
│   └── fs/
│       ├── arduino_SD.cpp      # SD.begin adaptado (SPI Waveshare + CS CH422G)
│       ├── SD.h                # Classe SDFS (substitui lib padrão)
│       ├── sd_diskio_waveshare.cpp/.h  # Driver low-level SD + expansor
│       ├── sd_diskio_crc.c     # CRC para protocolo SD
│       ├── sd_defines.h        # Tipos de cartão SD
│       ├── fatfs_time.cpp      # __wrap_get_fattime (timestamps via RTC)
│       └── fs_time_fix.cpp/.h  # Correção de timestamps em árvore (utime)
└── tools/
    └── png_to_lvgl.py          # Conversão PNG → array C para LVGL
```

---

## Arquitetura Geral

O firmware segue uma arquitetura **modular baseada em tarefas FreeRTOS**, onde cada
subsistema possui responsabilidades isoladas e se comunica via filas, callbacks e flags
atômicas.

```
┌─────────────────────────────────────────────────────────┐
│                      setup() / app.cpp                  │
│  Inicializa hardware → boot screen → SD → RTC → Wi-Fi  │
│  → NTP → splash → UI principal → portal → tarefas bg   │
└──────────┬──────────────┬────────────────┬──────────────┘
           │              │                │
     ┌─────▼──────┐ ┌────▼─────┐   ┌──────▼──────┐
     │  Task LVGL │ │ Task     │   │ Task        │
     │  (display  │ │ sd_io    │   │ net_svc     │
     │   + touch) │ │ (SD+FTP) │   │ (WiFi+NTP+  │
     │            │ │          │   │  WG+serviço)│
     └─────┬──────┘ └────┬─────┘   └──────┬──────┘
           │              │                │
           │         sd_access_sync()      │
           │              │                │
     ┌─────▼──────────────▼────────────────▼──────┐
     │              Periféricos                     │
     │  LCD RGB │ Touch │ SD (SPI) │ RTC (I2C)     │
     │  CH422G (CS) │ Wi-Fi │ Flash (NVS/SPIFFS)   │
     └─────────────────────────────────────────────┘
```

### Princípios de design

- **Acesso serializado ao SD**: toda operação de leitura/escrita no cartão SD é
enfileirada via `sd_access_sync()` / `sd_access_async()` e executada exclusivamente
pela task `sd_io`, eliminando condições de corrida entre UI, FTP e portal web.
- **Desacoplamento via tickers**: o `sd_access` não conhece os módulos que precisam
de ciclos periódicos (FTP, etc.). Esses módulos registam-se via
`sd_access_register_tick(fn)` e recebem chamadas no contexto exclusivo da task `sd_io`,
respeitando o princípio de responsabilidade única (SRP).
- **Mutex LVGL**: o port LVGL expõe `lvgl_port_lock()` / `lvgl_port_unlock()` (mutex
recursivo) para proteger operações gráficas feitas fora da task LVGL.
- **Anti-tear**: o display RGB usa double buffer + direct mode (modo 3) para evitar
artefatos visuais durante a renderização.
- **Heap em PSRAM**: a memória de widgets LVGL (`LV_MEM_CUSTOM`) e buffers grandes
são alocados na PSRAM via `heap_caps_malloc`.

---

## Sequência de Boot

O `setup()` em `app.cpp` segue esta ordem:


| #   | Etapa         | Módulo                   | Descrição                                                                                   |
| --- | ------------- | ------------------------ | ------------------------------------------------------------------------------------------- |
| 1   | Serial + NVS  | `app_settings`           | Inicializa porta serial (115200) e preferências NVS (`fdigi`)                               |
| 2   | Journal       | `boot_journal`           | Inicializa journal em RAM e reinicia ciclo                                                  |
| 3   | Painel        | `ESP_Panel`              | Configura display LCD RGB e touch; aplica bounce buffers para anti-tear                     |
| 4   | LVGL          | `lvgl_port_v8`           | Inicializa port LVGL com LCD e touch; cria task LVGL                                        |
| 5   | Boot Screen   | `boot_screen`            | Exibe tela terminal com 7 linhas de status                                                  |
| 6   | SD Card       | `fs/`, `waveshare_sd_cs` | Bind CS via CH422G → monta SD com fallback de velocidade (10 MHz → 400 kHz) → self-test R/W |
| 7   | App Log       | `app_log`                | Inicializa logger rotativo no SD (`/fdigi.log`)                                             |
| 8   | Task SD       | `sd_access`              | Cria task `sd_io` com fila de operações                                                     |
| 9   | RTC           | `net_time`               | Sonda PCF85063A via I2C → se válido, carrega hora no relógio do sistema                     |
| 10  | Wi-Fi         | `net_services`           | Se credenciais configuradas, conecta em modo STA; aguarda até 15s                           |
| 11  | NTP           | `net_time`               | Se Wi-Fi OK e NTP ativo, sincroniza hora (bloqueante ~3s) → grava no RTC                    |
| 12  | Journal flush | `boot_journal`           | Grava buffer RAM → SPIFFS (`/boot.log`) → copia para SD                                     |
| 13  | Splash        | `splash_screen`          | Exibe logo AFR pelo tempo configurado (padrão 3s)                                           |
| 14  | UI principal  | `ui_app`                 | Importa `fdigi.cfg`, cria telas (principal, Wi-Fi, configurações)                           |
| 15  | Portal Web    | `web_portal`             | Inicia AsyncWebServer na porta 80                                                           |
| 16  | Task Rede     | `net_services`           | Cria task `net_svc` (Wi-Fi, FTP, NTP loop, WireGuard)                                       |
| 17  | mDNS          | Arduino mDNS             | Registra `fitadigital.local`, serviço HTTP tcp/80                                           |


Após o `setup()`, o `loop()` executa `vTaskDelay(portMAX_DELAY)` — todo o trabalho
acontece nas tarefas FreeRTOS dedicadas.

---

## Módulos e Features

### Interface Gráfica (LVGL)

**Arquivos**: `src/ui/ui_app.cpp`, `src/lvgl_port_v8.cpp`, `src/lv_conf.h`

A interface é construída com **LVGL 8.3** e possui as seguintes telas:

#### Tela de Boot (`boot_screen`)

- Fundo preto, estilo terminal (fonte `lv_font_unscii_16`)
- Título "FitaDigital - arranque"
- 7 linhas de checklist: SD, RTC, Wi-Fi, NTP, FTP, WireGuard, HTTP
- Cada linha mostra `[OK]`, `[WARN]` ou `[ERROR]` conforme resultado
- Subtítulo e rodapé dinâmicos

#### Tela Splash (`splash_screen`)

- Logo AFR centralizado (`afr_logo.c`)
- Texto "Desenvolvido por AFR Solucoes Inteligentes"
- Duração configurável (0–10 segundos, padrão 3)

#### Tela Principal (`s_scr_main`)

- **Barra de status** (46px): intensidade Wi-Fi (%), data/hora (atualizada a cada 1s), botão engrenagem
- **Área de conteúdo**: explorador de arquivos (se SD montado) ou mensagem de erro

#### Tela Wi-Fi (`s_scr_wifi`)

- Campos SSID e senha com teclado virtual
- Botão "Guardar e ligar" com temporizador de tentativas (até 60 × 500ms)
- Indicador de estado da conexão
- Botão Voltar (visível quando acessada pelas configurações)

#### Tela de Configurações (`s_scr_settings`)

Barra superior com botão Voltar + **TabView** com 8 abas:


| Aba        | Conteúdo                                                                                 |
| ---------- | ---------------------------------------------------------------------------------------- |
| **Wi-Fi**  | Estado da conexão, botão "Alterar Wi-Fi"                                                 |
| **FTP**    | Informações do servidor, campos user/pass, botão Guardar                                 |
| **Hora**   | Toggle NTP, servidor NTP, roller de fuso horário (UTC−12 a UTC+14), hora manual UTC      |
| **WG**     | Switch WireGuard + campos (IP local, chave privada, chave pública peer, endpoint, porta) |
| **Portal** | URL do portal web (`http://<IP>/`)                                                       |
| **Logs**   | Visualização tail do `fdigi.log`, botões Atualizar e Limpar                              |
| **SD**     | Formatação FAT do cartão (armar → executar), suspende FTP durante operação               |
| **Font**   | Slider de tamanho 0–3 (14px, 16px, 18px, 20px — Montserrat)                              |


**Timer de status** (1s): atualiza barra (Wi-Fi %, hora), labels de configurações,
e `file_browser_refresh_silent()` se o conteúdo do SD foi alterado externamente.

#### Overlay de Carregamento (`ui_loading`)

- Fundo semitransparente + spinner + texto descritivo
- Usado durante operações demoradas (listagem de diretórios, formatação)

---

### Explorador de Arquivos

**Arquivo**: `src/ui/file_browser.cpp`

- Lista até **48 entradas** por diretório
- Navegação hierárquica com entrada `..` para diretório pai
- Colunas: Nome, Tamanho, Data de modificação
- Clique em diretório navega para dentro
- Arquivos `.txt` e `.log` abrem em **visualizador de texto**:
  - Leitura de até 8 KiB do arquivo
  - Fonte monoespaçada (Unscii)
  - Tabela numerada com linhas
  - Botões de navegação: ↑, ↓, Home, End
- Data obtida diretamente do FAT32 via `sd_access_fat_mtime()`
- Auto-refresh silencioso quando `sd_access_last_modified_ms()` indica alteração

---

### Acesso ao Cartão SD

**Arquivos**: `src/sd_access.cpp`, `src/fs/`

O acesso ao cartão SD é **completamente serializado** por uma fila FreeRTOS:

```
Qualquer task                      Task sd_io
     │                                  │
     ├── sd_access_sync(callback) ──►   │ executa callback
     │   (bloqueia até concluir)        │ (acesso exclusivo ao SD)
     │                                  │
     ├── sd_access_async(callback) ──►  │ executa callback
     │   (retorna imediatamente)        │ (ordem preservada)
     │                                  │
     └── sd_access_register_tick(fn) ── │ fn() chamado a cada iteração (~2 ms)
                                        │ (ex.: FTP handleFTP)
```

**Desacoplamento**: o `sd_access` não depende de nenhum módulo externo.
Serviços que precisam de ciclos periódicos no contexto SD (como o FTP) registam
um ticker via `sd_access_register_tick()` — o wiring é feito no `app.cpp`.
Suporta até **4 tickers** simultâneos.

**Detalhes da implementação**:

- Se o chamador já está na task `sd_io`, executa inline (evita deadlock)
- `sd_access_notify_changed()` atualiza timestamp para auto-refresh da UI
- `sd_access_fat_mtime()` lê data/hora de modificação diretamente do FAT32

**Driver SD customizado** (`src/fs/`):

- Substituição da biblioteca `SD` padrão do Arduino (`lib_ignore = SD`)
- CS do cartão controlado pelo expansor **CH422G** (EXIO4, índice 4) via I2C
- SPI dedicado nos GPIOs 11 (MOSI), 12 (SCK), 13 (MISO)
- Montagem com fallback de velocidade: 10 MHz → 4 MHz → 1 MHz → 400 kHz

**Timestamps FAT** (`fatfs_time.cpp`):

- `__wrap_get_fattime` intercepta chamadas FatFs via linker wrap
- Retorna hora do relógio do sistema (populado pelo RTC ou NTP)

---

### Wi-Fi (STA)

**Arquivo**: `src/net_services.cpp`

- Modo exclusivo **Station (STA)**
- Hostname: `fitadigital`
- Power save desativado (`WIFI_PS_NONE` + `esp_wifi_force_wakeup_acquire`)
- Auto-reconnect ativado
- Credenciais armazenadas em NVS (namespace `fdigi`)
- Eventos monitorados: conexão, desconexão, obtenção de IP
- Task `net_svc` executa no **mesmo core** que Arduino/LVGL (requisito da API Wi-Fi)

---

### Servidor FTP

**Arquivo**: `src/net_services.cpp` (biblioteca SimpleFTPServer v2.1.10)

- Inicia automaticamente quando Wi-Fi está conectado e SD montado
- Credenciais padrão: `esp32` / `esp32` (configuráveis via UI ou portal)
- Buffers estáticos de 16 bytes para user/pass (evita invalidação de ponteiro `String`)
- Callbacks em fim de transferência e espaço livre notificam `sd_access_notify_changed()`
- Processamento FTP executado na task `sd_io` via ticker registado com
  `sd_access_register_tick(net_services_sd_worker_tick)` no `app.cpp` —
  o `sd_access` não conhece o FTP (desacoplado por inversão de dependência)
- **Polling adaptativo**: sem cliente FTP conectado, `handleFTP()` é chamado a ~10 Hz
  (apenas verifica `accept()`); com cliente ativo, frequência máxima (~500 Hz) para
  throughput ideal de transferência
- Histerese de **30 segundos** antes de parar FTP após queda de Wi-Fi
- Suspensão explícita durante formatação do SD (`net_services_set_ftp_suspended`)
- Timeouts: autenticação 45s, inatividade 600s

---

### Portal Web (HTTP + WebSocket)

**Arquivos**: `src/web_portal/web_portal.cpp`, `src/web_portal/web_portal_html.h`

Servidor **AsyncWebServer** na porta 80, sem autenticação (rede local).

#### Interface HTML

Página única com tema escuro e três painéis:

- **Config**: formulário com todas as configurações (Wi-Fi, fonte, FTP, NTP, fuso, WireGuard)
- **Logs**: console de logs em tempo real via WebSocket + tail histórico
- **Ficheiros**: explorador de arquivos do SD com download

#### Endpoints REST


| Rota             | Método | Descrição                                                            |
| ---------------- | ------ | -------------------------------------------------------------------- |
| `/`              | GET    | Página HTML do portal                                                |
| `/api/settings`  | GET    | Retorna JSON com todas as configurações e status                     |
| `/api/settings`  | POST   | Aplica configurações (JSON); reinicia Wi-Fi/FTP se necessário        |
| `/api/logs/tail` | GET    | Últimas linhas do log (até 16 KiB de heap)                           |
| `/api/logs`      | DELETE | Limpa o arquivo de log                                               |
| `/api/fs/list`   | GET    | Lista diretório (`?path=...`): `entries[]` com `name`, `dir`, `size` |
| `/api/fs/file`   | GET    | Download (`?path=...`): até 512 KiB em RAM; acima streaming até 32 MiB; suporta `Range: bytes=` (206); 416/413 conforme caso |


#### WebSocket `/ws/logs`

- Na conexão, envia tail do log existente
- Linhas novas encaminhadas em tempo real via fila interna (24 slots × 384 bytes)
- Task dedicada `log_forward_task` consome a fila e distribui para clientes WS

---

### Relógio — NTP e RTC Externo

**Arquivo**: `src/net_time.cpp`

#### RTC PCF85063A

- Conectado via **I2C** (port 0, endereço `0x51`)
- Registros BCD a partir do offset 0x04 (segundos, minutos, horas, dia, weekday, mês, ano)
- Bit OS (Oscillator Stopped) indica hora inválida
- Sondagem com retries no boot

#### Fluxo de sincronização

1. **Boot**: RTC → relógio do sistema (antes de montar SD, para timestamps FAT)
2. **Wi-Fi conectado**: NTP `configTime()` → relógio do sistema → grava no RTC
3. **Periódico**: re-sincronização RTC a cada **1 hora** (sem reiniciar SNTP)
4. **Correção de arquivos**: `fs_time_fix_touch_all()` corrige timestamps no SD (utime)
  executado uma vez quando o relógio se torna válido

#### Configurações

- NTP ativo por padrão, servidor padrão: `pool.ntp.org`
- Fuso horário: offset em segundos (UTC−12 a UTC+14)
- Hora manual UTC disponível via UI (campos ano/mês/dia/hora/minuto)

#### Validação

- Hora considerada válida se epoch > 2020-01-01 UTC (`1577836800`)

---

### WireGuard (VPN)

**Arquivo**: `src/net_wireguard.cpp`

- Biblioteca: fork **Tinkerforge** do WireGuard-ESP32-Arduino (compatível com `esp_netif`)
- Ativado após Wi-Fi conectado e hora válida
- Configuração: IP local (/32), chave privada, chave pública do peer, endpoint, porta
- Tráfego **não forçado** exclusivamente pelo túnel (FTP continua acessível via Wi-Fi local)
- Buffers estáticos de 128 bytes para chaves e endpoint
- Desativação automática ao perder Wi-Fi

---

### Sistema de Configurações (NVS + SD)

**Arquivos**: `src/app_settings.cpp`, `src/app_settings_sd.h`

#### Armazenamento NVS

Namespace `fdigi` na flash interna (Preferences do Arduino):


| Chave      | Tipo   | Padrão         | Descrição                                  |
| ---------- | ------ | -------------- | ------------------------------------------ |
| `ssid`     | String | vazio          | SSID da rede Wi-Fi                         |
| `pass`     | String | vazio          | Senha Wi-Fi                                |
| `wifi_ok`  | bool   | false          | Rede configurada com sucesso               |
| `font_i`   | uint8  | 0              | Índice da fonte (0=14px, 1=16, 2=18, 3=20) |
| `splash_s` | uint8  | 3              | Duração do splash em segundos (0–10)       |
| `ftp_u`    | String | `esp32`        | Usuário FTP                                |
| `ftp_p`    | String | `esp32`        | Senha FTP                                  |
| `ntp_on`   | bool   | true           | NTP habilitado                             |
| `ntp_srv`  | String | `pool.ntp.org` | Servidor NTP                               |
| `tz_sec`   | int32  | 0              | Offset do fuso em segundos                 |
| `wg_on`    | bool   | false          | WireGuard habilitado                       |
| `wg_ip`    | String | `10.0.0.2`     | IP local do túnel                          |
| `wg_pk`    | String | vazio          | Chave privada WireGuard                    |
| `wg_pub`   | String | vazio          | Chave pública do peer                      |
| `wg_ep`    | String | vazio          | Endpoint do peer                           |
| `wg_pt`    | uint16 | 51820          | Porta UDP do peer                          |


#### Espelho no SD (`/fdigi.cfg`)

Arquivo de texto no cartão SD em formato INI simplificado:

```ini
format=1
[wifi]
wifi_ok=1
ssid=MinhaRede
pass=MinhaSenha
[ui]
font_i=1
splash_s=3
[ftp]
ftp_u=admin
ftp_p=senha123
[time]
ntp_on=1
ntp_srv=pool.ntp.org
tz_sec=-10800
[wireguard]
wg_on=0
wg_ip=10.0.0.2
wg_pk=...
wg_pub=...
wg_ep=vpn.example.com
wg_pt=51820
```

- Na inicialização, `app_settings_try_load_from_sd_config()` importa do SD para NVS
- Após importar ou se não existir, `app_settings_sync_config_file_to_sd()` exporta NVS → SD
- Permite configurar o dispositivo colocando um `fdigi.cfg` no cartão SD antes de ligar

---

### Sistema de Log

**Arquivo**: `src/app_log.cpp`

- Arquivo: `/fdigi.log` na raiz do cartão SD
- Formato de linha: `AAAA-MM-DD HH:MM:SS | NIVEL | mensagem`
- Fallback para `millis()` quando o relógio do sistema não está válido
- **Rotação automática**: máximo **10 MiB**, mantém **8 MiB** após rotação
(arquivo temporário `/fdigi.log.tmp`)
- Mutex interno para acesso thread-safe
- Todo acesso ao SD via `sd_access_sync()`
- Notificação de novas linhas para o portal WebSocket via callback
- Suporte a log com tag de feature: `[FEATURE] mensagem`

---

### Journal de Boot

**Arquivo**: `src/boot_journal.cpp`

Registro detalhado do processo de boot, separado do log principal:

1. **RAM** (buffer 8192 bytes): acumula linhas durante o boot
2. **SPIFFS** (`/boot.log`): flush único após boot (evita múltiplos open/close)
3. **SD** (`/boot.log`): cópia opcional após montagem do SD

Formato: `millis | LEVEL | mensagem`

---

### mDNS

Configurado em `app.cpp` após conexão Wi-Fi:

- Hostname: `fitadigital` → acessível como `fitadigital.local`
- Serviço HTTP registrado na porta TCP 80

---

### Depuração de Tarefas

**Arquivo**: `src/task_debug.cpp`

- Imprime snapshot de tarefas FreeRTOS via Serial (`vTaskList`, `vTaskGetRunTimeStats`)
- Requer `CONFIG_FREERTOS_USE_TRACE_FACILITY=y` e
`CONFIG_FREERTOS_GENERATE_RUN_TIME_STATS=y` no `sdkconfig.defaults`

---

## Tarefas FreeRTOS


| Task             | Core                  | Função                                                    |
| ---------------- | --------------------- | --------------------------------------------------------- |
| **Arduino loop** | 1                     | `vTaskDelay(portMAX_DELAY)` — inativa após setup          |
| **LVGL**         | (port)                | Renderização de display + processamento de touch          |
| **sd_io**        | —                     | Fila serializada de acesso ao SD + tickers registados (FTP, etc.) |
| **net_svc**      | 1 (mesmo que Arduino) | Wi-Fi, NTP loop, WireGuard, gestão de FTP                 |
| **log_forward**  | —                     | Encaminha linhas de log da fila para WebSocket            |


O core 1 é compartilhado entre Arduino, LVGL e `net_svc` por requisito
da API Wi-Fi do ESP32 (não é thread-safe entre cores).

---

## Mapa de Pinos e Periféricos


| Periférico      | GPIO / Barramento                       |
| --------------- | --------------------------------------- |
| **SPI SD MOSI** | GPIO 11                                 |
| **SPI SD SCK**  | GPIO 12                                 |
| **SPI SD MISO** | GPIO 13                                 |
| **SD CS**       | CH422G EXIO4 (via I2C, ativo baixo)     |
| **RTC I2C SDA** | GPIO 8                                  |
| **RTC I2C SCL** | GPIO 9                                  |
| **Display LCD** | RGB paralelo (gerenciado por ESP_Panel) |
| **Touch**       | I2C (gerenciado por ESP_Panel)          |
| **Buzzer**      | Desativado (`BOARD_UI_BEEP_GPIO = -1`)  |


---

## Dependências e Bibliotecas


| Biblioteca                  | Versão               | Finalidade                            |
| --------------------------- | -------------------- | ------------------------------------- |
| **Arduino-ESP32**           | 3.0.3                | Framework base (pinado via Git)       |
| **LVGL**                    | 8.3 (release branch) | Interface gráfica touch               |
| **ESP32_Display_Panel**     | v0.2.2               | Driver LCD RGB + touch para Waveshare |
| **ESP32_IO_Expander**       | v0.1.0               | Driver do expansor CH422G             |
| **ArduinoJson**             | ^7.2.0               | Serialização JSON (portal web)        |
| **SimpleFTPServer**         | v2.1.10              | Servidor FTP sobre SD                 |
| **WireGuard-ESP32-Arduino** | Tinkerforge fork     | Túnel VPN WireGuard (esp_netif)       |
| **AsyncTCP**                | ESP32Async           | TCP assíncrono para AsyncWebServer    |
| **ESPAsyncWebServer**       | ESP32Async           | Servidor HTTP + WebSocket             |
| **WiFi**                    | Arduino-ESP32        | API Wi-Fi STA                         |


---

## Configuração de Build

`**platformio.ini`** — principais parâmetros:


| Parâmetro                       | Valor                   | Justificativa                         |
| ------------------------------- | ----------------------- | ------------------------------------- |
| `platform`                      | espressif32             | Plataforma ESP32                      |
| `board`                         | esp32-s3-devkitc-1      | Board base para ESP32-S3              |
| `framework`                     | arduino                 | Arduino sobre ESP-IDF                 |
| `flash_size`                    | 16MB                    | Flash total da placa                  |
| `psram_type`                    | opi                     | PSRAM Octal SPI                       |
| `memory_type`                   | qio_opi                 | Flash QIO + PSRAM OPI                 |
| `partitions`                    | default_16MB.csv        | Esquema de partições 16 MB            |
| Otimização                      | `-O2` (substitui `-Os`) | Melhor desempenho no desenho LVGL     |
| `ARDUINO_USB_CDC_ON_BOOT`       | 0                       | UART para upload/log                  |
| `CORE_DEBUG_LEVEL`              | 1                       | Log mínimo do core (ERROR)            |
| `--wrap=get_fattime`            | linker                  | Redireciona FatFs timestamps para RTC |
| `CONFIG_ASYNC_TCP_RUNNING_CORE` | 1                       | AsyncTCP no mesmo core da app         |
| `CONFIG_ASYNC_TCP_STACK_SIZE`   | 4096                    | Stack reduzida para economizar RAM    |
| `lib_ignore`                    | SD                      | Usa cópia local adaptada para CH422G  |


`**sdkconfig.defaults**`:

- `CONFIG_FREERTOS_USE_TRACE_FACILITY=y` — estatísticas de tarefas
- `CONFIG_FREERTOS_GENERATE_RUN_TIME_STATS=y` — tempo de CPU por task
- `CONFIG_LWIP_MAX_SOCKETS=16` — suporta conexões simultâneas (HTTP + FTP + WS)
- `CONFIG_PM_ENABLE=n` — sem power management (dispositivo sempre alimentado)

---

## API REST do Portal Web

### `GET /api/settings`

Retorna JSON com todas as configurações e status do dispositivo:

```json
{
  "wifi_ssid": "MinhaRede",
  "wifi_pass": "...",
  "font_index": 1,
  "splash_seconds": 3,
  "ftp_user": "esp32",
  "ftp_pass": "esp32",
  "ntp_enabled": true,
  "ntp_server": "pool.ntp.org",
  "tz_offset_sec": -10800,
  "wg_enabled": false,
  "wg_local_ip": "10.0.0.2",
  "wg_private_key": "",
  "wg_peer_public_key": "",
  "wg_endpoint": "",
  "wg_port": 51820,
  "ip": "192.168.1.100",
  "sd_mounted": true
}
```

### `POST /api/settings`

Corpo JSON com os mesmos campos (todos opcionais). Aplica as alterações na NVS,
reinicia Wi-Fi se SSID/senha mudaram, reinicia FTP se credenciais FTP mudaram.

Resposta: `{"ok": true}`

### `GET /api/logs/tail`

```json
{
  "text": "2025-04-11 14:30:00 | INFO | Boot FitaDigital...\n...",
  "truncated": false,
  "fileSize": 4096
}
```

### `DELETE /api/logs`

Limpa o arquivo de log. Resposta: `{"ok": true}`

### `GET /api/fs/list?path=/`

```json
{
  "entries": [
    {"name": "fdigi.cfg", "dir": false, "size": 256},
    {"name": "fotos", "dir": true, "size": 0}
  ]
}
```

### `GET /api/fs/file?path=/fdigi.cfg`

Retorna o conteúdo do arquivo como `application/octet-stream`. Ficheiros até **512 KiB** são lidos de uma vez para RAM (como antes). Entre **512 KiB** e **32 MiB** o envio é feito por **streaming** (cada bloco lê o SD na tarefa `sd_io` via `sd_access_sync`). O cabeçalho **`Accept-Ranges: bytes`** é anunciado e um pedido **`Range: bytes=lo-hi`** válido recebe **206 Partial Content** com **`Content-Range`**. Intervalo inválido → **416**; acima de **32 MiB** → **413** `file too large`.

### `WebSocket /ws/logs`

Na conexão, recebe tail do log. Novas linhas são enviadas em tempo real como mensagens de texto.

---

## Arquivo de Configuração SD (`fdigi.cfg`)

O arquivo `/fdigi.cfg` na raiz do cartão SD permite configurar o dispositivo
sem interface gráfica. O firmware lê este arquivo no boot e importa as configurações
para a NVS.

### Formato

```ini
format=1

[wifi]
wifi_ok=1
ssid=NomeDaRede
pass=SenhaDaRede

[ui]
font_i=1
splash_s=3

[ftp]
ftp_u=admin
ftp_p=senha123

[time]
ntp_on=1
ntp_srv=pool.ntp.org
tz_sec=-10800

[wireguard]
wg_on=0
wg_ip=10.0.0.2
wg_pk=ChavePrivadaBase64
wg_pub=ChavePublicaPeerBase64
wg_ep=vpn.example.com
wg_pt=51820
```

### Campos

- `format=1` — obrigatório, identifica a versão do formato
- `wifi_ok` — `1` para marcar rede como configurada
- `font_i` — 0 a 3 (tamanho da fonte: 14, 16, 18, 20 px)
- `splash_s` — 0 a 10 (segundos de splash; 0 desativa)
- `ntp_on` — `1` habilitado, `0` desabilitado
- `tz_sec` — offset em segundos (ex: `-10800` para UTC−3 / Brasília)
- `wg_on` — `1` habilitado, `0` desabilitado
- `wg_pt` — porta UDP (padrão 51820)

---

*Desenvolvido por AFR Soluções Inteligentes*