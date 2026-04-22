# TODO — FitaDigital (ESP32-S3-Touch-LCD-4.3B)

## Em curso

## Pendente
- Filtro por data no file_browser (campo "aaaa/mm" filtra listagem).
- Jump-to-line no viewer de ciclos.
- Tokens de tema centralizados (ui_theme.h, substituir hardcodes de cor).

## Feito
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
