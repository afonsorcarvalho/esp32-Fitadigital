# TODO — FitaDigital v2.x.x

Main HEAD em v2.23 (commits `c153487` wifi self-heal + `0f9a2eb` ui teclado,
push origin). Worktree v2 merged + apagado.
Scope: WiFi + HTTP + RS485 + NTP + FTP server. Sem WG, sem MQTT, sem SCREENSHOT.

## *** FIX reboot TASK_WDT(async_tcp) — SHIPPED main 3d3e1eb (2026-06-29) ***

Device travava a tela + rebootava em loop. Razao: `prev_reset=TASK_WDT(6)`,
sempre task **async_tcp**. Causa: QUALQUER leitura SD via async_tcp (handlers HTTP)
bloqueia >5s quando o sd_io esta lento (cartao SD a degradar ~33KB/s) e async_tcp
esta subscrita ao Task WDT -> reboot. Confirmado: ate ficheiro 749B e handler
`/api/logs/tail` (nao tocado) rebootavam; idle estavel.

- [x] **M2 leitura chunked** (commit a4b4e84): handlers /api/fs/file + NDJSON leem
      em chunks de 8KB com esp_task_wdt_reset entre chunks. Melhora justica do sd_io.
- [x] **A rede de seguranca** (commit 3d3e1eb): `-DCONFIG_ASYNC_TCP_USE_WDT=0` no
      platformio.ini — tira async_tcp do Task WDT. Verificado on-device: 5x download
      fdigi.log 499KB -> 200 OK completo, **ZERO reboots** (boot_count fixo 647).
- [x] Merge main + push GitHub. Spec/plan em docs/superpowers/.
- [x] **Soak ~24h PASS (2026-06-29 12:09 → 06-30 12:04):** 283 iters, **0 reboots**
      (boot_count fixo 647, heap_guard 97 inalterado), uptime ~86565s contínuo,
      stress /api/fs/file **94/94 HTTP 200** (o caminho que rebootava), wifi/ftp ok.
      Monitor so' HTTP (COM3 nao tocado). **Reboot TASK_WDT declarado ESTAVEL.**
- [ ] **Causa raiz (hardware): trocar/testar o cartao SD** (~33KB/s = lento/degradado).
      Devolve velocidade das leituras web/FTP. Unit fragil (boot_count 647, heap_guard 97, RTC morto).

Licao: verificacao on-device FALSIFICOU a hipotese M2 (build+code-review passaram
na mesma) — o reboot nao era duracao de leitura, era sd_io lento. Abrir COM3 reseta
a placa (RTS), logo soak/monitor so' por HTTP.

## *** INTEGRIDADE .txt — cadeia HMAC inline (v2.24, 2026-06-26) ***

Cada linha gravada leva `\t<seq>\t<mac8hex>` (HMAC-SHA256/32 truncado, encadeado
na anterior). Password hardcoded global (`FDIGI_INT_SECRET`, gitignored). Deteta
editar/inserir/remover/reordenar. Verificacao offline no PC: `tools/verify_integrity.py`.
Plano: `.claude/plans/vamos-estudar-o-caso-giggly-bumblebee.md`.

### Validado no rig do user (SD + RS485 reais — 2026-06-26)
- [x] Toggle "Integridade dos .txt (HMAC)" ligado em Definicoes.
- [x] Ciclo real RS485 -> .txt com cabecalho `#FDIGI-INT` seq0 -> `verify_integrity.py` ⇒ `OK`
      (4 linhas, dev=8032B19E139C).
- [x] Teste adulteracao: editar linha ⇒ `ADULTERADO ... mac nao bate`; remover ⇒ seq gap;
      reordenar ⇒ seq gap. Tudo aponta a linha exacta.
- [x] **Fix de ordenacao** (revisao): abrir SD ANTES de prepare()/compose() nos 2
      escritores; senao open falhado avancava a cadeia sem gravar -> header perdido +
      gap de seq -> falso ADULTERADO. Flashado v2.24.
- [x] Teste reboot a meio do dia (2026-06-26): pre seq0-4 -> reboot (RAM limpa) ->
      pos seq5-7 SEM novo header, recover_from_tail leu seq4 -> `verify_integrity.py`
      ⇒ `OK (7 linhas)`. Cadeia atravessa reboot sem costura nem gap.
- [ ] **TROCAR** `src/integrity_secret.h` para password real antes de producao.

### Feito (2026-06-26)
- [x] Modulo `src/cycle_integrity.{h,cpp}`: cadeia HMAC, seed por path, cabecalho
      `#FDIGI-INT`, recuperacao por tail, init lazy.
- [x] Integracao nos 2 escritores (`cycles_rs485.cpp` + `rs485_buffer.cpp` flush),
      ambos no contexto `sd_io` (cadeia partilhada, sem locks).
- [x] Setting NVS `int_en` (default OFF) + toggle na aba Definicoes (ui_app.cpp).
- [x] Password via `FDIGI_INT_SECRET` (header gitignored + `.example`); `#error` se ausente.
- [x] `tools/verify_integrity.py` (+ `--clean` p/ leitura humana). Paridade firmware↔PC
      validada por simulacao (good/tamper/delete/forge/wrong-key).
- [x] Build v2.24 OK (RAM 39.3%, flash 37.3%), flash COM3, boot estavel sem crash.
- [ ] Commit (a pedido do user).

## *** BRANCH `feature/ftp-upload` — cliente FTP-upload (2026-06-25) ***

Nova feature: empurra `/CICLOS` do SD para um servidor FTP remoto, so' os
ficheiros novos/modificados (deteccao por TAMANHO), com journal e verificacao
por SIZE antes de marcar como sincronizado. Task dedicada `ftp_up`, leitura do
SD em chunks via sd_access (RS485-safe). Spec `docs/superpowers/specs/2026-06-25-ftp-upload-client-design.md`,
plano `docs/superpowers/plans/2026-06-25-ftp-upload-client.md`.

### Validado em hardware contra servidor real (2026-06-26)
- [x] **Upload e2e OK** contra `sistema.fitadigital.com.br:21` (user `fitadigital`,
      rdir `/fitas`). Pass: `19 enviados, 19 verificados, 0 falhas`. 18 ficheiros
      reais no server (`/fitas/2026/{04,05,06}/`), tamanhos batem com locais.
- [x] **ROOT CAUSE do "0 verificados / remoto=-1" eterno (NAO era o server):**
      `ESP32_FTPClient::MakeDir()` num dir que ja' existe recebe `550` do vsftpd;
      `GetFTPAnswer()` marca QUALQUER 4xx/5xx como `_isConnected=false`, envenenando
      o resto do pass — `InitFile`/`NewFile`/`WriteData`/`Size` abortavam em
      `if(!isConnected())`. Logo: data socket PASV nunca abria (`data_conn=0` em
      100%, provado via instrumentacao app_log), STOR nunca corria, mas
      `ftp_stream_file` contava `sent+=n` cego -> `enviado=N` FALSO; `Size()` -> -1.
      Server estava 100% saudavel (login/MKD/STOR/SIZE/RETR testados de cliente externo).
- [x] **Fix (2 partes):** (1) `MakeDir()` restaura `_isConnected=true` apos erro
      benigno de comando (mantem false so' se socket caiu mesmo = outBuf "Offline").
      (2) `ftp_stream_file` aborta com `-1` se `ftp.DataConnected()` for false
      (deixa de mascarar falha com bytes falsos). Novo metodo `DataConnected()` no lib.
      Flashed COM3, validado on-rig. Bonus: loop de re-upload eterno tambem resolvido
      (journal commita -> proximos passes saltam ja-enviados).
- [x] **Soak leve PASS (2026-06-26 19:28-21:53, ~2h25m):** RS485 6 linhas/min +
      ftpup uploads reais sob carga. **ZERO reboots** (boot_count 629 fixo), uptime
      monotonico, heap flat. ftpup `2 enviados, 2 verificados, 0 falhas` por passagem.
      Sender 1084 linhas, 0 erros write. **Zero linhas perdidas no soak leve** (as
      unicas perdas — SOAK#36-45 — foram na janela dos 2 reboots-artefacto iniciais:
      verifier HTTP pesado 66KB=TASK_WDT, e crash-analyzer a abrir COM3=auto-reset RTS;
      NAO o fix ftpup). Licao: nao puxar ficheiros grandes via /api/fs/file durante
      carga (sd_io), e abrir COM3 reseta a placa (DTR/RTS).
- [x] Commit do fix: `ac8ea27` em main, pushed GitHub.

### Nota menor (nao-bug)
- ftpup pode logar 1 falha pontual num ficheiro em escrita activa (ex `cycles.ndjson`):
  `enviado=205748 local=205550 remoto=205748` — o ficheiro CRESCEU (RS485 append)
  entre o scan do tamanho e o fim do stream. Verificacao rejeita (size != scan) e
  re-envia na passagem seguinte. Auto-corrige; inerente a uploadar ficheiro activo.

### Feito (2026-06-25)
- [x] Core logica pura `ftp_journal_core` + testes Unity nativos (6/6 PASS via
      LLVM-MinGW host, env `[env:native]`).
- [x] Settings NVS (`fup_*`) + espelho `/fdigi.cfg`.
- [x] Motor `ftp_upload.cpp`: scan recursivo, MKD arvore, upload chunked, gate SIZE, journal.
- [x] UI: seccao "Upload FTP" na aba SRV + botao "Sincronizar agora" + botao olho mostrar senha.
- [x] Init no boot (apos screenshot, fora do `#if FITA_ENABLE_SCREENSHOT`).
- [x] **Fix 2 bugs criticos** (revisao final): fork local `lib/ESP32_FTPClient`
      com `Size()` correcto — upstream `Write()` ia para o socket de DADOS e
      `GetFTPAnswer()` estourava buffer 64->128B. Sem isto, verificacao nunca
      passava (re-upload eterno) + stack overflow por verify.

## *** BRANCH `feature/tailscale-microlink` — viabilidade Tailscale (2026-06-25) ***

Avaliacao de adoptar o microlink (https://github.com/CamM2325/microlink) para
acesso remoto VPN via Tailscale. Relatorio completo: `docs/tailscale-microlink-viability.md`.

**Veredicto:**
- Hardware da placa **compativel** (PSRAM octal OPI, 16MB flash, Arduino 3.0.3 = IDF 5.1). Licenca MIT.
- POC standalone **viavel** (recomendado como 1o passo de de-risk).
- Integracao no firmware Arduino actual **NAO viavel as-is** — 2 bloqueios:
  1. SRAM interna: microlink precisa ~116KB estatica + 42KB stacks; FitaDigital
     corre com ~36KB livres e ja' tem 97 heap_guard_reboots (OOM cronico).
  2. Build: microlink e' ESP-IDF puro (idf.py/sdkconfig); projeto e' Arduino/PlatformIO.
- Riscos: wireguard-lwip (mesma familia buggy de antes), contencao PSRAM↔LCD RGB,
  protocolo ts2021 por eng-reversa (1 autor, nao-afiliado Tailscale).
- Merito: control plane gerido + DERP relay e' mais robusto que o WG custom abandonado.

**Proximos passos (decisao do user — nada decidido ainda):**
- [ ] POC standalone: placa S3 dedicada + ESP-IDF + Headscale/cloud, validar join
      tailnet + ping cross-network (o ponto fraco do WG antigo). Risco zero ao
      firmware de producao. Criterios pass/fail no relatorio.
- [ ] Alternativa: co-processador/gateway separado (Tailscale noutra placa, bridge LAN).
- [ ] Alternativa longo prazo: migrar FitaDigital Arduino -> ESP-IDF (esforco grande).
- [ ] Ou arquivar e manter WG custom como unica opcao de acesso remoto.

## *** v2.1.2 PRODUCTION-READY *** (validado 2026-05-19 soak 8h)

Soak 8h cycle+stress: 294 cycles, 1470/1470 list hits (100%), avg_t 1.50s,
max_t 4.21s, NDJSON linear 6.7KB->63KB, active_seen 293/294, idle_seen
294/294. Serial COM3 8h: zero panic, zero Guru. Device pos-soak:
enabled=true IDLE prev_reset=UNKNOWN(0).



Tag `v2.0.0` production. v2.0.1 NTP. v2.1.0 cycle_detector. v2.1.1 /list crash fix.
v2.1.2 SMP yield crash fix + single-flight mutex + watchdog RTC baseline.
Archive: `firmware_versions/FitaDigital_v2.12.bin` (2.4 MB).

### Soak 20min v2.1.2 PASS (2026-05-19 01:39-01:59)
- 21 cycles enviados, list_200 **105/105 (100%)**, errors=0
- avg_t **0.71s** (vs 2.23s v2.11), max_t **1.18s** (vs 8.06s)
- active_seen 21/21, idle_seen 21/21 (state machine 100%)
- NDJSON growth linear +192B/cycle, sem plateau
- prev_reset=UNKNOWN(0) — ZERO reboots, ZERO panics

### Root cause backtrace v2.1.1 (decodificado)
PC=0x403828fe EXCCAUSE=0x01 IllegalInstruction em xPortEnterCriticalTimeout.
Call stack: reader_task -> cycle_detector_process_line -> emit_cycle_close
(line 161 `app_log_feature_writef("CYCLE","fechado")`) -> app_log_write ->
log_notify_cb (web_portal.cpp:92 xQueueSend) -> xQueueGenericSend ->
xTaskRemoveFromEventList -> prvCheckForYieldUsingPrioritySMP ->
vPortYieldOtherCore -> esp_crosscore_int_send -> vPortEnterCritical -> CRASH.

SMP cross-core yield IPI hit FreeRTOS portMUX race quando /api/cycles/list
HTTP task em outro core competia simultaneamente. Stack OK (16KB), heap OK,
queue OK — bug FreeRTOS interno sob alta contencao.

### Fixes v2.1.2 aplicados
1. **Remove log enqueue de dentro de reader_task** (cycle_detector.cpp:
   emit_cycle_close, process_line, tick). NDJSON + /api/cycles/status sao
   suficientes para observabilidade — log redundante.
2. **Single-flight mutex `/api/cycles/list`** (handle_cycles_list_get):
   2o req concurrente -> 503 imediato. Onership do mutex passa ao
   onDisconnect lambda em sucesso, release explicito em error paths.
3. **Watchdog cycle_detector RTC baseline** (cycle_detector.cpp):
   `RTC_NOINIT_ATTR ConfigBaseline s_baseline` + magic 0xC0DE2026.
   cycle_detector_tick detecta `s_cfg.enabled==false` + magic OK e
   restaura baseline (auto-heal contra BSS corruption futura).
4. **Panic breadcrumb** em handle_cycles_list_get enter/exit
   (panic_breadcrumb_set/clear via panic_logger.h). Next-boot dump
   identifica crash dentro do handler.

## *** v2.2.0 SHIPPED *** (validado 2026-05-20 soak 30min)

Cycle detector configuravel via portal web + NVS persist. Tab "Ciclo"
no portal: start_pattern / end_pattern / idle_timeout_s editaveis.
Endpoint `/api/cycles/config` GET/POST (URL movido de /api/settings/cycle
por bug ESPAsyncWebServer GET prefix-match). Live reconfigure fecha
ciclo ACTIVE como INTERRUPTED antes de re-init.
Archive: `firmware_versions/FitaDigital_v2.20.bin` (2.4 MB).

### Soak 30min v2.2.0 PASS (2026-05-19 19:34-20:04)
- 23 cycles, list_200 115/115 (100%), errors=0
- active_seen 23/23, idle_seen 23/23
- NDJSON linear, COM3 serial zero panic
- prev_reset=UNKNOWN(0) — zero reboots
- Smoke custom patterns OK (INICIO_CICLO/FIM_CICLO detectados + NDJSON)
- NVS persistence cross-reboot validado
- tools/test_cycle_config.py 6 tests PASS

## *** v2.2.1 SHIPPED *** (fix WiFi self-heal — 2026-06-25)

**Bug:** device ficava offline indefinidamente apos `ASSOC_LEAVE` (reason 8)
do AP. ICMP/FTP/HTTP mortos, mas firmware vivo por USB (heap flat, uptime
monotonico). "Nao responde ping" era so' o sintoma.

**Root cause:** a logica de WiFi self-heal `wifi_keepalive_tick` (SOFT @30s
net_wifi_begin_saved / HARD @5min esp_wifi_stop+start, repetivel) vivia DENTRO
do bloco `#else FITA_ENABLE_WG` de `net_wireguard.cpp`. Como v2.x tem
`FITA_ENABLE_WG=0`, esse bloco inteiro NAO compila (so' o stub) — a funcao
nem existia no binario v2.20. Unico reconnect restante era
`WiFi.setAutoReconnect(true)`, que o proprio codigo ja' documentava como
"confirmado estagnar pos-ASSOC_LEAVE" (net_wireguard.cpp:91). Regressao
introduzida ao remover o WG para a v2.x — o self-heal estava acoplado ao
modulo errado. Soaks de 8h nao apanharam (AP nunca disassociou nos testes).

**Fix:** movido `net_wifi_keepalive_tick` (+ state) para `net_services.cpp`
(sempre compilado), exposto em `net_services.h`, chamado a cada tick do
`net_services_loop` (corre no core do Arduino => calls WiFi seguros).
`net_wireguard.cpp` reaponta para a versao partilhada (single source).
Sem `esp_restart()` (zero-reboot mantido). Sem service_supervisor (era o
race do PANIC v1.88). `setAutoReconnect(true)` mantido (coexistencia
provada v1.x soak 24h).

**Validacao (2026-06-25):** device reproduziu ASSOC_LEAVE @17s no boot,
ficou offline ~5min, depois o HARD path recuperou o WiFi **sem reboot**
(uptime_s=690 continuo, ping 4/4 0% loss @192.168.0.197). Em v2.20 ficaria
offline para sempre. Archive: `firmware_versions/FitaDigital_v2.21.bin`.
Files: net_services.cpp/.h, net_wireguard.cpp, platformio.ini (2.20->2.21),
+ `#include <cstdint>` em net_services.h.

**Diagnostico — licao:** crash-analyzer reportou "[NET-KA] nao disparou" =
FALSO NEGATIVO. `[NET-KA]` usa `Serial.printf` (bufferizado); so' `[HEAP]`
(`ets_printf` directo) sobrevive nas capturas. Presenca do `[HEAP]` provou
que o net_svc arrancou. Ver [[session_2026_05_11_wg_enrollment_odoo]].

## *** v2.2.2 SHIPPED *** (observabilidade [NET-KA] + recovery 90s — 2026-06-25)

`[NET-KA]` `Serial.printf` -> `ets_printf` (ROM directa, nao bufferizada) em
`net_wifi_keepalive_tick`: 4 chamadas substituidas (voltou/DOWN/HARD/SOFT).
`"—"` (UTF-8 multi-byte) substituido por `"-"` ASCII nas strings HARD/SOFT
para evitar lixo no terminal ROM. `kHardThresholdMs` 300000 -> 90000 (5min ->
90s): HARD path e' o que de facto recupera; SOFT@30s nao recuperou (AP re-kick).
Comentarios/doc em net_services.cpp e net_services.h actualizados.
Files: net_services.cpp, net_services.h, platformio.ini (2.21->2.22).

## Pendente (follow-ups v2.2.2)

### *** TOUCH GT911 latch — FIX a fazer (re-init live, zero-reboot) ***
**Sintoma (2026-06-25):** touch da tela deixa de responder em runtime; device
fica vivo (rede/HTTP/SD/heap OK, UI a renderizar), so' o input morre. Reboot
recupera. Intermitente.
**Root cause:** GT911 (touch, I2C0) latcha e passa a NACK. `readPoints()`
devolve -1 (esp_lcd_touch_read_data falha) — ver
ESP_PanelTouch.cpp:213. Mas `touchpad_read` (lvgl_port_v8.cpp:651) trata
-1 == 0 == "sem toque" (so' >0 conta) => GT911 morto e' silenciosamente
ignorado, SEM watchdog/recuperacao. NAO bloqueia (readPoints retorna logo)
=> sem WDT => fica morto indefinidamente. **NAO e' lock do bus I2C0**
(CH422G/SD no mesmo bus funcionou durante o freeze; i2c0_timeouts_total=0).
Independente do fix WiFi.
**Fix decidido (re-init live, zero-reboot):** watchdog conta N leituras
`readPoints()==-1` consecutivas (no `touchpad_read` ou task dedicada);
ao exceder threshold, recupera o GT911 sem reboot: `tp->del()` +
re-`begin()`/init (re-pulsa RST do GT911). O objecto ESP_PanelTouch persiste
(LVGL indev->user_data continua valido) — so' o handle esp_lcd_touch interno
e' recriado. Fazer sob `lvgl_port_lock(-1)` + `board_i2c0_bus_lock` e no core
certo. Adicionar contador exposto em /api/system/status.
**Caveat validacao:** latch e' intermitente/nao-reproduzivel on-demand —
validar so' que o forced del+begin re-inicia o touch sem partir o display
(RGB/tearing). Refinar/medir em campo.
**Nota:** device tem historico fragil (boot_count 612, heap_guard_reboots 97,
RTC PCF85063A "indisponivel" em todo boot — tambem I2C0). Avaliar se o RTC
morto e o GT911 latch tem causa comum (hardware/power I2C0) ao implementar.

### AP-side ASSOC_LEAVE (analise feita — benigno)
ASSOC_LEAVE reason 8 @~17s no boot e' BENIGNO: auto-recupera ~506ms
(validado Event A v2.22). Logo apos `wifi_connect_wait(15s)` + RSSI -41..-63
=> provavel band-steering / renegociacao DHCP do AP. O ASSOC_LEAVE
*problematico* (bug original) era tardio/intermitente — coberto agora pelo
self-heal v2.21+. Investigacao router-side fica pendente (precisa logs do AP).

### Follow-up "voltou" — NAO e' bug
SD app_log confirma `[WIFI] Voltou apos N s` a ser escrito no tick
WL_CONNECTED. O "missing" na captura serial foi timing (janela/resets), nao
gap de codigo. Sem accao.

## Em curso
(nada — v2.22 shipped; aguardar decisao proxima feature)

## Pendente

### Sugestoes v2.2.x+
- **UI events**: badge ciclo concluido, lista de ciclos no portal
- **Pub/sub formal cycle_detector**: API observadores para outros modulos
- **Watchdog edge case** (review v2.2.0): se config set a start vazio
  (enabled=false intencional), `watchdog_heal_locked` pode restaurar
  baseline antigo non-empty e re-activar silenciosamente. Low risk.

### FTP client (ULTIMA feature)
- Push delta para servidor remoto. So' depois resto estavel.
- Decidir lib (ESP32FtpClient, SimpleFTPClient, lwip socket directo)
- Configuravel via UI (server IP, user, password, remote path, intervalo)

### Mitigacao NTP DNS
- Device loga "falhou (NTP timeout 10s)" quando `pool.ntp.org` DNS partido
- Workaround: setar NTP IP literal (ex `200.160.0.8` a.ntp.br) na UI Configuracoes
- Long term: fallback chain pools/IPs hardcoded

## Feito
- **2026-06-26 — Documentação técnica completa** (`docs/DOCUMENTACAO_TECNICA.md`):
  24 secções — arquitetura, boot, tarefas FreeRTOS, captura RS485, cadeia HMAC,
  sd_access, NTP/RTC, WG/MQTT/screenshot (presentes-desativados v2.24), FTP-upload,
  UI LVGL, portal web REST, confiabilidade, NVS, partições, build flags. Lida do
  código v2.24 (README antigo estava stale).
- **2026-05-18 — v2.1.0 cycle_detector + NDJSON index** (commit `3441208` push origin):
  - State machine ACTIVE/IDLE com timeout 900s default
  - `/api/cycles/status` + `/api/cycles/list`
  - NDJSON `/CICLOS/AAAA/MM/cycles.ndjson`
- **2026-05-18 — v2.0.1 NTP honest sync** (commit `42d6f75`):
  - `sntp_get_sync_status() == SNTP_SYNC_STATUS_COMPLETED` em vez de getLocalTime
  - Retry NTP a cada 1h em `net_time_loop`
- **2026-05-18 — `tools/soak_run.py --warmup 30` default** (commit `7477d04`):
  - Elimina false-fail startup race (WinError 10061 antes WiFi+HTTP up)
- **2026-05-18 — docs/manuais USUARIO+INSTALACAO+SERVICO** v2.0.0 (commit `f48d921`)
- **2026-05-18 — v2.0.0 PRODUCTION-READY validacao soak 8h + RS485 stress** (commit `644d0f3`):
  - Soak 28800s real (8h) zero reboot zero crash
  - Heap int 42748 -> 45036 (+2.3KB liberado healthy)
  - Heap min seen: 36540 B (massive margin vs HEAP_GUARD 5K)
  - PSRAM drift -28KB em 8h (~3.5KB/h, nao critico)
  - FTP probe LIST 468/468 OK (100%)
  - Health probe HTTP 476/477 (1 false-fail startup race conhecido)
  - **RS485 stress 100%**: PC TX 958 bursts (OPERACAO + 47ch random = 55 chars) cada 30s via COM8 9600; SD captou 958/958 OPERACAO em `/CICLOS/2026/05/20260518.txt` (81322 B)
  - Live post-soak uptime 37464s = 10.4h continuous
  - Archive: `firmware_versions/FitaDigital_v2.00.bin` (2398784 B)
  - Tag: `v2.0.0`
- **2026-05-17 — Worktree v2 baseline 2.0.0** (commit `4325738`):
  - build_features.h: MQTT=0, SCREENSHOT=0, WG=0 herdado
  - Worktree apagado pos-merge
