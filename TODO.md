# TODO — FitaDigital (ESP32-S3-Touch-LCD-4.3B)

## Em curso

## Pendente

- Nova Feature de gravação por OTA via wifi
- Modal de entrada de senha: ao clicar "ok", mostrar spinner de carregando ate validacao concluir (sucesso ou erro).
- Spinner em cima do botao (feedback de clique reconhecido) nos botoes "Abrir ciclo de hoje" e "Ver historico".

## Feito
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
