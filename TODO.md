# TODO — FitaDigital (ESP32-S3-Touch-LCD-4.3B)

## Em curso
- "Ir para data" no file_browser (modal com 3 rollers dia/mes/ano) — flashado 2026-04-20, falta validar em placa.
- Ajeitar altura do modal "Ir para data": passado a `LV_SIZE_CONTENT` + `max_height`, pads reduzidos, rollers 180→140. Falta validar em placa.
- Extender range de anos ate 2050 no roller do "Ir para data". Falta validar em placa.

## Pendente
- Commit das alteracoes pendentes: `src/ui/ui_date_goto.{h,cpp}`, `src/ui/file_browser.{h,cpp}`, `src/ui/ui_app.cpp`, `src/net_monitor.cpp`, `src/lv_conf.h`, `docs/ux_plan.md`.
- Screensaver com alguma animacao (possivelmente uma logo).

## Feito
- 2026-04-20 — Build e flash do "Ir para data" (integracao com `file_browser_open_cycle_by_date`).
- Share de ciclo via QR code (commit 8014bb8).
- Toast reutilizavel (commit e437bb3).
- Breadcrumb e lista maior no file_browser (commit f5f5a0d).
- Dashboard como tela principal (commit 67f7865).
- Indicador de conectividade remota via ping ICMP (commit f510c4f).
