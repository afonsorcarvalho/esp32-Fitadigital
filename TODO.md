# TODO — FitaDigital (ESP32-S3-Touch-LCD-4.3B)

## Em curso


## Pendente
- **MQTT — Fase 2: UI LVGL** — secção "MQTT" na aba SRV (ao lado FTP/WG): switch on/off, textareas host/port/user/pass/base_topic, slider intervalo 10–3600s, textarea keywords, label estado refresh 1s, botão Aplicar.
- **MQTT — Fase 3: cliente real** — adicionar `bertmelis/espMqttClient` lib_deps, implementar task `mqtt_svc` (core 0, prio 1, 4KB stack), LWT, backoff exponencial, telemetria JSON periódica.
- **MQTT — Fase 4: keyword detector** — implementar task `mqtt_kw` (core 1, prio 1, 3KB stack), tick 5s, leitura offset SD, match `strcasestr`, publish `/keyword`.
- **MQTT — Fase 5: testes** — soak 30 min Mosquitto local, validar `boot_count`/`heap_guard_reboots` no JSON, LWT, heap drain <50 B/min.
- Realizar teste no servidor produção do WireGuard (validar enroll + ping ICMP + conectividade dashboard apos deploy do servidor de produção).
- Commit das mudancas v1.13..v1.17 + archive script + svg_to_lvgl (branch main tem diff por commitar).
- Organizar `SoftwareQualification_*.docx` (3 versoes untracked na raiz): mover para pasta dedicada ou adicionar ao `.gitignore`.

## Feito
- 2026-04-25 — MQTT Fase 1: settings NVS + skeleton stub. Adicionadas chaves `mq_on/h/p/u/pw/b/iv/kw` + contadores `bc`/`hgc` em `app_settings.h/.cpp`. Hooks `boot_count_increment` em `boot_journal_init` e `heap_guard_count_increment` em `heap_monitor` antes de `esp_restart`. Criados `net_mqtt.h/.cpp` e `net_mqtt_keywords.h/.cpp` como stubs com enum `MqttStatus`. `app.cpp` chama `net_mqtt_init()` + `net_mqtt_keywords_start()` após `net_services_start_background_task()`. Build pendente de validação pelo deployer.
- 2026-04-26 — Heap leak ~1432 B/min eliminado: `tm_to_epoch_utc` (src/net_time.cpp) reescrito p/ aritmética pura UTC, sem `setenv("TZ","UTC0")` + `tzset()` + `mktime()`. Bissecção (envs bisect_a/a2/a3a/a3b/a4) provou: leak NÃO em web_portal/AsyncTCP/WireGuard/FTP/WiFi/lwip nem em LVGL/SD/RS485 — exclusivamente em `update_bar_wifi_text()` chamado a 1Hz por `status_timer_cb`, cadeia até `tm_to_epoch_utc` que invocava 4× setenv + 4× tzset por tick. Match aritmético: 4×6 B × 60 = 1440 B/min ≈ 1432 medido (newlib leak documentado). Soak validação 30 min (61 pontos `[HEAP]`): drain OLS 0.0 B/min, heap interna flat em 39068 B. Antes fix: -1432 B/min linear R²=1.000 → OOM em ~40 min.
- 2026-04-26 — Instrumentação heap_monitor (src/heap_monitor.cpp/.h): task FreeRTOS dedicada (`heap_mon`, core 1, prio 1, 3KB stack) imprime via `ets_printf` linha CSV `[HEAP] t=<ms> int=<bytes> min=<bytes> psram=<bytes>` cada 30s. Watchdog: se `heap_int_free < 6 KB` regista entrada no boot_journal (`HEAP_GUARD: free=... min=... threshold=...`), faz `boot_journal_flush_to_spiffs`, `vTaskDelay(150ms)` p/ flush log, depois `esp_restart()` — reboot graceful em vez de panic OOM. Inicializado em `app.cpp` após `net_services_start_background_task()`.
- 2026-04-25 — v1.35 fix2: stack sd_io 8192->32768B + log_w/log_e removidos de sdWait/sdSelectCard/sdCommand/sdReadSectors (sd_diskio_waveshare.cpp + sd_access.cpp). Root cause: VFS logging no caminho FatFs profundo causava stack overflow (EXCCAUSE=2, EXCVADDR=0xb33ffffc, sentinel poison). Fix compilado + flashado COM3, ciclo monitor 180s STABLE (web portal 192.168.0.197 up, sem crash markers). Diff uncommitted aguarda commit.
- 2026-04-25 — v1.35: fix TOCTOU race SD hotplug vs LVGL timer — sd_access_sync em dashboard_refresh_values (totalBytes/usedBytes), sd_access_set_mounted(false) antes de SD.end() no ramo remove, remover SD.end() churn no ramo insert. SD pode ser removido em runtime sem crash. Build + flash COM3 sucesso, 180s estavel.
- 2026-04-24 — v1.33: debounce SD hotplug (3 falhas SPI consecutivas, ~3s) antes de marcar SD como removido. Filtra glitches ocasionais que causavam oscilação visível ("SEM SD" piscando + dashboard alternando). Build + flash COM3 sucesso.
- 2026-04-24 — v1.31: badge SD removido das duas topbars; overlay grande (280x96) centralizado na tela via `lv_layer_top()` — sempre acima de qualquer screen; texto "SEM SD" + ícone, font montserrat_28, vermelho com animação respiração.
- 2026-04-23 — Politica `firmware_versions/`: adicionado ao `.gitignore` (binarios reproduziveis do source, evita bloat do repo). Distribuicao externa fica com releases/OTA, nao com git. Docs untracked ja estavam commitados em sessao anterior (nao havia acao pendente real).
- 2026-04-23 — Botao "Trocar senha" movido da aba Scr para a aba Sistema nas Definicoes (mais logico: sistema agrupa configuracoes de seguranca/OTA). Bump v1.17.
- 2026-04-23 — Dark mode nos modais (audit fase 2): `ui_pin_entry`, `ui_date_goto`, `ui_wg_enroll`, `ui_share_qr`, e goto-line do file_browser agora usam `ui_color_surface(app_settings_dark_mode())` em vez de `UI_COLOR_WHITE` hardcoded. Splash skip (transient, antes do theme settle). Bump v1.16.
- 2026-04-23 — Logo AFR dashboard regerada a partir de SVG (nitidez superior): novo `tools/svg_to_lvgl.py` (svglib+reportlab+Pillow) rasteriza SVG com oversample 4x, aplica white→alpha preservando antialias, downsample LANCZOS para tamanho alvo, delega a `png_to_lvgl.py`. `AFR LOGO.svg` → `src/ui/afr_logo_verde.c` (120x37, LV_IMG_CF_TRUE_COLOR_ALPHA). Bump v1.15.
- 2026-04-23 — Archive automatico de firmware por versao: post-script PlatformIO `tools/save_firmware_version.py` copia `.pio/.../firmware.bin` → `firmware_versions/FitaDigital_v<VER>.bin` a cada build. Registado em `extra_scripts = post:...` no platformio.ini. Backfill v1.14 gerado.
- 2026-04-23 — Fix dark mode no viewer (file_browser): gutter de numeracao de linhas agora usa bg dark-aware (`UI_COLOR_VIEWER_GUTTER_BG_DARK` #1E1E1E). Helper `ui_color_viewer_gutter_bg(dark)` em ui_theme.h. Bump v1.14.
- 2026-04-23 — Fix dark mode nos cards do dashboard: bg e border usam helpers dark-aware (`ui_color_surface(dark)`, `ui_color_border(dark)`). Toggle dark faz rebuild do dashboard (cards tem estilo local que o tema LVGL nao sobrepoe sozinho). Novos tokens UI_COLOR_SURFACE_DARK (#2A2A2A) + UI_COLOR_BORDER_DARK (#444). Bump v1.13.
- 2026-04-23 — Modo dark: toggle aba Scr, persistencia NVS (key `dark`, default false), aplica lv_theme_default_init(dark) ao boot e ao mudar. Bump v1.12.
- 2026-04-23 — Feedback LVGL para OTA HTTP: timer 500ms mostra toasts em tempo real durante upload .bin via browser — "OTA HTTP: XX%" durante upload, "OK! A reiniciar..." no final com reboot automatico em 2s, ou "erro: ..." se falhar. Integração via std::atomic em ota_manager.cpp (thread-safe entre async_tcp core 0 e LVGL core 1). Handler web_portal.cpp chama ota_http_*() em cada fase. Timer criado em ui_app_run(). Commit 54cb4a1.
- 2026-04-23 — Logo AFR no portal web: logo verde dark (9.8KB PNG) embutida em data URI base64 no header HTML; altura 32px responsiva; exibida ao lado de "FitaDigital". Build + flash sucesso.
- 2026-04-23 — Dashboard web OTA (HTTP upload .bin): aba "OTA" no portal web (porta 80); input file .bin + botao "Enviar e Flashar"; progresso bar durante upload (XHR onprogress); endpoint POST /api/ota/upload multipart/form-data; integra classe Arduino Update (independente de ArduinoOTA); reboot automatico apos sucesso; response JSON {"ok":true/false, "error":"..."}. Código em web_portal.cpp (handlers + registar endpoint) e web_portal_html.h (painel + JS doUpload). Build sucesso, flash sucesso, endpoint testado. Sem bump versão (feature menor, sem mudança NVS/LVGL).
- 2026-04-22 — OTA via WiFi (ArduinoOTA push): aba "Sistema" nas Definicoes; botao Activar OTA inicia escuta ArduinoOTA; barra de progresso LVGL 0-100%; reboot automatico apos sucesso; erro apresentado na UI; ota_manager.cpp/h novo modulo; loop integrado em net_services; bump v1.11. Flashado e verificado estavel.
- 2026-04-22 — Spinner no modal de entrada de senha (modo Validate): ao clicar "Entrar", desabilita teclado/fundo, mostra lv_spinner centrado, timer 300 ms conclui validacao e fecha modal. Spinner nos botoes "Abrir ciclo de hoje" e "Ver historico": spinner 40x40 sobreposto ao botao, label oculto, botao desabilitado durante acao; restore automatico no final. Bump v1.10.
- 2026-04-22 — Text viewer: coluna de numeracao de linhas — largura fixa (kViewerGutterW=56px) p/ 4 digitos, fundo cinza claro (UI_COLOR_VIEWER_GUTTER_BG) em toda a celula, texto alinhado a direita, cor muted; highlight piscante RS485 com prioridade. Novo token ui_theme.h. Bump v1.09.
- 2026-04-22 — Tokens de tema centralizados (ui_theme.h): audit de cores, novos tokens UI_COLOR_PRIMARY_DARKER e UI_COLOR_BOOT_*, substituicao em 7 ficheiros. Bump v1.08.
- 2026-04-22 — Trocar senha de configuracao pela aba Scr (UI + NVS): modal teclado alfanumerico 4-16 chars, fluxo senha-actual->nova->confirmar, persistencia NVS (pin_sett), validacao tamanho. Bump v1.07.
- 2026-04-22 — Jump-to-line no viewer: botao GPS abre modal com textarea numerica + teclado LVGL, valida 1..s_total_lines, scroll animado ou recarrega janela se fora. Bump v1.06.
- 2026-04-22 — Trocar codigo de acesso a Definicoes pela UI: botao na aba Scr, fluxo actual->novo->confirmar via `ui_pin_entry_capture_show`, persistencia NVS (4 digitos). Bump FITADIGITAL_VERSION 1.04->1.05.
- 2026-04-22 — Team de agentes Claude: fw-orchestrator, firmware-coder, hardware-deployer (haiku), serial-monitor (haiku), crash-analyzer (opus) em .claude/agents/ para ciclo code→flash→monitor→analyze→fix automatizado.
- 2026-04-21 — Screensaver configuravel: toggle on/off + slider timeout 10-300 s em Definicoes → Scr.
- 2026-04-21 — Botoes SD: "Executar formatacao" sempre vermelho; "Armar" laranja quando armado.
- 2026-04-21 — WireGuard enrollment via QR: keygen Curve25519 (hardware RNG), POST /api/enroll, poll + backoff, modal LVGL com QR+countdown, auto-close, refresh campos UI.
- 2026-04-20 — PIN de acesso a Definições: 4 rollers 0-9, NVS, default 1234, toast erro (commit 8439408).
- 2026-04-20 — Screensaver: logo bounce fundo preto, 1s/step, fecha ao toque ou dado RS485.
- 2026-04-20 — Modal "Ir para data" completo: rollers dia/mes/ano, anos ate 2050, altura corrigida, botoes Hoje/Ontem, commit 02c86db.
- 2026-04-20 — Build e flash do "Ir para data" (integracao com `file_browser_open_cycle_by_date`).
- Share de ciclo via QR code (commit 8014bb8).
- Toast reutilizavel (commit e437bb3).
- Breadcrumb e lista maior no file_browser (commit f5f5a0d).
- Dashboard como tela principal (commit 67f7865).
- Indicador de conectividade remota via ping ICMP (commit f510c4f).
