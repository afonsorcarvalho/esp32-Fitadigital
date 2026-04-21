# TODO — FitaDigital (ESP32-S3-Touch-LCD-4.3B)

## Em curso

## Pendente
- Senha de acesso a Definicoes: 4 digitos via 4 rollers (0-9, estilo "Ir para data"), default 1234, configuravel em fdigi.cfg (chave `settings_pin`), gravada em NVS. Modal de entrada mostrado ao tocar no icone de engrenagem.
- WireGuard: modo de configuracao via QR code — gerar par de chaves (privada/publica) no dispositivo. Detalhes ./docs/wireguard-brief.md
- Screensaver configuravel: timeout (actualmente 60 s hardcoded) e logo on/off em Definicoes → UI.
- Filtro por data no file_browser (campo "aaaa/mm" filtra listagem).
- Jump-to-line no viewer de ciclos.
- Tokens de tema centralizados (ui_theme.h, substituir hardcodes de cor).

## Feito
- 2026-04-20 — Screensaver: logo bounce fundo preto, 1s/step, fecha ao toque ou dado RS485.
- 2026-04-20 — Modal "Ir para data" completo: rollers dia/mes/ano, anos ate 2050, altura corrigida, botoes Hoje/Ontem, commit 02c86db.
- 2026-04-20 — Build e flash do "Ir para data" (integracao com `file_browser_open_cycle_by_date`).
- Share de ciclo via QR code (commit 8014bb8).
- Toast reutilizavel (commit e437bb3).
- Breadcrumb e lista maior no file_browser (commit f5f5a0d).
- Dashboard como tela principal (commit 67f7865).
- Indicador de conectividade remota via ping ICMP (commit f510c4f).
