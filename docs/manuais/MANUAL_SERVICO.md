# FitaDigital — Manual de Serviço

**Versão firmware:** v2.0.0
**Público-alvo:** técnico nível 2 — suporte / firmware ops / engenharia clínica avançada

---

## 1. Acesso de serviço

| Recurso | Endereço | Credenciais |
|---------|----------|-------------|
| Portal web (config + logs + ficheiros + OTA) | `http://<IP>/` | `admin` + PIN |
| Health (sem auth) | `http://<IP>/api/health` | — |
| WebSocket logs | `ws://<IP>/ws/logs` | (sessão portal) |
| FTP server | `<IP>:21` | user/pass configurável (default `esp32:esp32`) |
| Serial console (debug local) | USB-C COM, 115200 8N1 | — |

> **Auth HTTP:** AsyncWebServer aceita Basic e Digest. PIN persiste em NVS (sobrevive reflash/OTA). Reset PIN só via Configurações na UI ou flash limpa (apaga NVS via `esptool erase_flash`).

---

## 2. Atualização de firmware (OTA)

### 2.1 Pré-requisitos
- Arquivo `FitaDigital_vX.YZ.bin` validado (baixar de `firmware_versions/` do repo ou suporte).
- PC mesma rede do FitaDigital.
- Recomendado: **soak 30 min mínimo no laboratório antes** de deploy produção.

### 2.2 Procedimento OTA via portal
1. Aceder `http://<IP>/` → tab **OTA**.
2. **Choose file** → selecionar `.bin`.
3. **Upload** → progresso em tempo real.
4. Após upload (~30 s) device **reboot automático**.
5. Aguardar ~30 s → portal volta com nova versão visível em Configurações → Sobre.

### 2.3 Procedimento OTA via curl (scripted)
```bash
curl --digest -u admin:<PIN> \
     -F "firmware=@FitaDigital_v2.00.bin" \
     http://<IP>/api/ota/upload
```

### 2.4 Verificação pós-upgrade
- Health: `curl -s http://<IP>/api/health` → `{"online":true,"uptime_s":<baixo>}`
- Versão: portal → Configurações → Sobre
- Boot logs: portal → Logs → procurar `[BOOT] FitaDigital firmware vX.YZ`
- Soak passivo 10 min mínimo antes de validar.

### 2.5 Rollback
- OTA reversa: fazer upload da versão anterior pelo mesmo método.
- Se device em boot loop pós-OTA: **reflash via cabo** (Seção 3).

---

## 3. Reflash via cabo (recovery)

Caso device não esteja acessível pela rede (Wi-Fi morto, boot loop):

### 3.1 Setup
1. Cabo USB-C do PC ao FitaDigital. Aparece `COM3` (Windows) ou `/dev/ttyACM0` (Linux).
2. Instalar [esptool.py](https://docs.espressif.com/projects/esptool/) ou usar PlatformIO.

### 3.2 Comando esptool
```bash
esptool.py --chip esp32s3 --port COM3 --baud 921600 \
  write_flash 0x10000 FitaDigital_v2.00.bin
```

### 3.3 Erase completo (factory reset)
**⚠️ Apaga NVS — perde PIN, Wi-Fi config, FTP creds, NTP config.**
```bash
esptool.py --chip esp32s3 --port COM3 erase_flash
```
Depois reflashar firmware como em 3.2.

---

## 4. Recuperação de arquivos via FTP

### 4.1 Download em lote
```bash
# Linux/macOS
lftp -u esp32,esp32 <IP> -e "mirror /CICLOS ./backup-fitadigital; quit"

# Windows com WinSCP CLI
winscp.com /command "open ftp://esp32:esp32@<IP>" \
  "synchronize local . /CICLOS" "exit"
```

### 4.2 Estrutura no SD
```
/CICLOS/AAAA/MM/AAAAMMDD.txt      Ciclos do dia
/fdigi.log                         Log da aplicação (rotativo? não — single file)
/boot.log                          Snapshot do último boot
/fdigi.cfg                         Backup config (opcional)
/screenshots/                      Capturas (vazio em v2.0.0, desabilitado)
```

### 4.3 Detalhes formato dos ciclos
- 1 linha por evento RS485 capturado (`\n` terminator).
- Sem timestamp por linha (timestamp = nome do arquivo + posição relativa).
- Encoding ASCII puro (rejeita >127 silenciosamente).
- Tamanho típico ciclo autoclave: 2-10 KB.

---

## 5. Análise de logs

### 5.1 Via portal
- Tab **Logs** → consola tempo real (WebSocket) + tail de `/fdigi.log`.
- Filtros: nível (INFO/WARN/ERROR), feature (WIFI/RS485/FTP/NTP/SYSTEM).

### 5.2 Via curl
```bash
curl --digest -u admin:<PIN> http://<IP>/api/logs/tail?bytes=8192
```

### 5.3 Via FTP / cartão SD
```bash
lftp -u esp32,esp32 <IP> -e "get /fdigi.log; quit"
```

### 5.4 Marcadores importantes no fdigi.log
| Padrão | Significado |
|--------|-------------|
| `[BOOT] FitaDigital firmware vX.YZ` | Boot completo, firmware ativo |
| `[WIFI] Conectado. IP=...` | Wi-Fi associou |
| `[WIFI] DOWN detectado` | Wi-Fi caiu — esperar self-healing |
| `[WIFI] Soft reconnect após Ns` | Recovery soft (30 s sem WiFi) |
| `[WIFI] Hard reset stack após Ns` | Recovery hard (5 min sem WiFi) |
| `[NTP] sincronizado` | NTP OK |
| `[FTP] Inicio` / `Stop` | Servidor FTP up/down |
| `Guru Meditation` no Serial | Crash hardware — anotar backtrace |
| `[HEAP_GUARD] free=X < Y threshold` | Pre-crash low heap, esp_restart |

---

## 6. Telemetria do sistema

### 6.1 Endpoint health
```bash
curl http://<IP>/api/health
# {"online":true,"uptime_s":12345}
```

### 6.2 Endpoint services (supervisor)
```bash
curl --digest -u admin:<PIN> http://<IP>/api/system/services
```
Retorna JSON com:
- `uptime_s`
- `heap.internal_free` / `internal_min` / `psram_free`
- `services[]`: stack high watermark, restart count, last restart reason
- `supervisor.escalations`

### 6.3 Indicadores de saúde
| Métrica | Saudável | Atenção | Crítico |
|---------|----------|---------|---------|
| `heap.internal_free` | > 30 KB | 15-30 KB | < 10 KB |
| `heap.internal_min` | > 20 KB | 8-20 KB | < 5 KB |
| Uptime crescente | sim | — | reset inesperado |
| Restart count por serviço | 0 | 1-2 | ≥ 3 (investigar) |

---

## 7. Troubleshooting avançado

### 7.1 Boot loop
**Sintoma:** device reinicia a cada ~5-10 s indefinidamente.

**Diagnóstico:**
1. Cabo USB ao PC → ferramenta serial (`platformio device monitor -p COM3 -b 115200`).
2. Procurar `Guru Meditation`, `Stack canary`, `HEAP_GUARD`, `Backtrace:`.
3. Anotar marker antes do reset.

**Soluções:**
- HEAP_GUARD → liberar memória (downgrade firmware, factory reset NVS).
- Stack canary em task X → bug firmware, reportar com backtrace.
- Boot loop após OTA → reflash via cabo (Seção 3) com versão estável.

### 7.2 Wi-Fi instável (cai e volta)
1. Portal Logs → contar `[WIFI] DOWN` / `Hard reset` na última hora.
2. Se > 5/h: problema rede (router sobrecarregado, RSSI baixo, canal congestionado).
3. v2.0.0 tem **self-healing layered** (soft 30s, hard 5min) — recupera sozinho mas indica problema infra.

### 7.3 SD não monta
1. Logs procurar `[SD] mount FAIL` ou `[BOOT] SD`.
2. Cartão pode estar corrupto. Backup PC → formatar FAT32 → reinserir.
3. Se persistir após cartão novo → contacto físico do slot (assistência).

### 7.4 FTP rejeita conexão
1. Confirmar serviço up: `curl http://<IP>/api/system/services` → procurar entry `ftp`.
2. `restart_count` alto → reinício automático em loop. Verificar SD montado (FTP precisa SD).
3. Timeouts: SimpleFTPServer config `FTP_AUTH_TIME_OUT=45s` `FTP_TIME_OUT=600s`. Cliente lento pode disparar 530 Timeout.

### 7.5 Captura RS485 perdeu dados
1. Stress test isolado: usar `tools/rs485_soak_gen.py` (no repo) com keyword conhecido.
2. Verificar match: comparar log gerado vs arquivo `/CICLOS/AAAA/MM/AAAAMMDD.txt`.
3. v2.0.0 validado **100% hit rate** sob stress 30 s/55 chars 8 h.

---

## 8. Ferramentas de manutenção (repo)

Localizadas em `tools/` do repositório:

| Script | Função |
|--------|--------|
| `soak_run.py` | Soak overnight: captura serial + probes HTTP/FTP + análise JSON |
| `rs485_soak_gen.py` | Gera tráfego RS485 sintético para stress test (suporta `--keyword`) |
| `rs485_send_keyword.py` | Envio one-shot de palavra-chave |
| `wifi_stress.py` | Stress WiFi (force disconnect cycles) |
| `com3_log.py` | Captura serial COM3 com timestamp |

Exemplo soak validação:
```powershell
python tools\soak_run.py --label v2-validate --duration 1800 \
  --health-probe --ftp-probe --probe-interval 60
```

---

## 9. Backup / Restore configuração

### 9.1 Export
- Portal → Configurações → **Exportar** → download `fdigi.cfg` (JSON com Wi-Fi/NTP/FTP/PIN hash).

### 9.2 Import
- Portal → Configurações → **Importar** → upload `fdigi.cfg` previamente exportado.
- Útil para clonar config entre vários FitaDigital de mesmo cliente.

### 9.3 Backup SD completo
- Via FTP mirror (Seção 4.1) — copia `/CICLOS/` + logs + cfg.
- Recomendado **mensal** ou após incidentes.

---

## 10. Garantia / hardware

### 10.1 Componentes substituíveis
- **Cartão microSD** — qualquer FAT32 ≤ 32 GB.
- **Adaptador USB / cabo** — qualquer 5 V ≥ 1 A.

### 10.2 Hardware não substituível pelo cliente
- Placa principal (ESP32-S3-Touch-LCD-4.3B Waveshare).
- Display, touchscreen, conector RS485.
- Bateria RTC (vida ~10 anos típica).

### 10.3 RMA / reposição
Casos de hardware morto:
1. Coletar info: serial number, MAC (Configurações → Rede), versão firmware, fdigi.log.
2. Reportar ao fornecedor com vídeo do sintoma se possível.
3. Substituir aparelho — restore config via `fdigi.cfg` exportado previamente.

---

## 11. Roadmap conhecido (referência)

Versões futuras (TODO.md no repo):
- **v2.0.x** — fix NTP sync (validação `sntp_get_sync_status`), revisão RS485 lógico
- **v2.1.x** — FTP client (push delta para servidor remoto, sincronização automática de arquivos pendentes)
- **v3.x** — eventualmente retomar WireGuard quando lib upstream estiver estável

---

## 12. Contactos

- **Repo firmware:** https://github.com/afonsorcarvalho/esp32-Fitadigital
- **Tag estável:** `v2.0.0`
- **Issues técnicas:** abrir issue no GitHub com `[fitadigital]` + descrição + logs
