# FitaDigital — Manual do Portal Web

Servidor HTTP embutido no ESP32-S3 (firmware ≥ v1.41) para configurar, monitorar
e gerir o cartão SD via browser ou API REST.

- **Porta:** 80
- **URL base:** `http://<IP-do-device>/` (ex.: `http://192.168.0.197/`)
- **Auth:** Basic Auth (`admin` / PIN do device, default `1234`)
- **WebSocket logs:** `ws://<IP>/ws/logs`
- **Endpoint sem auth:** `GET /api/health`

---

## 1. Acesso pelo browser

1. Ligue o device à rede Wi-Fi (configurado pela UI ou portal).
2. Abra `http://<IP>/` no navegador.
3. Browser pede usuário/palavra-passe → `admin` + PIN.
4. PIN actual visível no device (ecrã *Definições → PIN*); pode ser alterado
   pelo PIN screen ou via `POST /api/settings/pin`.

> O PIN é **persistido em NVS** e usado para todas as rotas excepto `/api/health`.

### 1.1 Layout da UI

Header com 5 tabs:

| Tab | Função |
|-----|--------|
| **Config** | Wi-Fi, Interface, FTP, NTP/Fuso, WireGuard |
| **Logs** | Consola tempo-real (WebSocket) + tail de `/fdigi.log` |
| **Ficheiros** | Explorador SD (navegar, descarregar) |
| **Screen** | Capturar screenshot do display + ver capturas |
| **OTA** | Upload `.bin` para flash via HTTP |

---

## 2. Configurações (tab Config)

Envia `POST /api/settings` agregando os campos. Cada secção também tem endpoint
dedicado (`/api/settings/{rs485,mqtt,ui,net,pin}`).

### 2.1 Wi-Fi
- **SSID:** rede a associar.
- **Palavra-passe:** vazio = manter actual; preencher = trocar e re-associar.

### 2.2 Interface
- **fontIndex (0–3):** tamanho da fonte do viewer .txt (0=menor, 3=maior).

### 2.3 FTP
- Utilizador / palavra-passe do servidor FTP local (porta 21).
- `POST /api/settings` aplica e reinicia o serviço FTP.

### 2.4 NTP / Fuso
- **NTP activo:** sincroniza relógio interno.
- **Servidor NTP:** ex. `pool.ntp.org`.
- **Fuso (UTC±h):** offset em horas; armazenado em segundos.

### 2.5 WireGuard
- Activo + IP local + chave privada + chave pública peer + endpoint + porta.
- Aplicado em runtime ao guardar.

### 2.6 Botão "Guardar"
- Envia tudo de uma vez via `POST /api/settings`.
- Resposta `{"ok":true}` → recarrega a config.

---

## 3. Logs (tab Logs)

- **Tempo real:** WebSocket `/ws/logs` (linha por mensagem).
- **Tail inicial:** `GET /api/logs/tail` envia últimos ~16 KB de `/fdigi.log`.
- **Recarregar:** botão re-fetch do tail.
- **Limpar log:** botão `DELETE /api/logs` (apaga `/fdigi.log` no SD).

---

## 4. Ficheiros (tab Ficheiros)

Navega o cartão SD começando em `/`.

- Click numa pasta abre dentro dela.
- Click em **Descarregar** abre `GET /api/fs/file?path=...` em nova aba.
  - JPG/PNG/BMP/TXT/LOG abrem inline no browser.
  - Outros tipos descarregam como anexo.
  - Suporta `Range:` (HTTP 206 partial).

> Apagar ficheiros não está exposto na UI mas funciona via
> `POST /api/fs/delete?path=...` (ver §7.5).

---

## 5. Screen (tab Screen)

- **Capturar tela atual:** `POST /api/screenshot/take`.
  - Acorda o screensaver, espera 1.5 s, depois encoda JPEG e grava em
    `/screenshots/screen-AAAAMMDD-HHMMSS.jpg` (ou `screen-uptime-N.jpg`
    se o relógio não tiver hora válida).
  - Resposta 202 imediato; preview aparece após ~7.5 s.
- **Capturas no SD:** lista `/screenshots/` com link de download.

> Rotation/cleanup automático **desactivada na v1.45** (corromp. SD sob carga).
> Se quiser limpar, use `POST /api/fs/delete` por ficheiro.

---

## 6. OTA (tab OTA)

1. Selecione `.bin` (gerado por `pio run`, p.ex.
   `firmware_versions/FitaDigital_v1.45.bin`).
2. Click **Enviar e Flashar**.
3. Barra de progresso → "Flash OK" → device reinicia em ~2 s.

Endpoint: `POST /api/ota/upload` (multipart, campo `firmware`).

Conexão pode cair durante reboot — comportamento normal.

---

## 7. Referência REST API

Todas as rotas (excepto `/api/health`) requerem Basic Auth.

### 7.1 `GET /api/health` *(sem auth)*
```json
{"online":true,"uptime_s":12345}
```

### 7.2 `GET /api/system/status`
```json
{
  "uptime_s": 12345,
  "heap_free": 18924,
  "heap_min": 11428,
  "sd_mounted": true,
  "wifi_rssi": -52,
  "ip": "192.168.0.197",
  "fw_ver": "1.45",
  "mqtt_status": "connected",
  "mqtt_last_error": "",
  "boot_count": 42,
  "heap_guard_reboots": 0
}
```

### 7.3 `POST /api/system/reboot`
```json
{"ok":true,"message":"reiniciando em 2s"}
```

### 7.4 `GET /api/fs/list?path=/abs/path`
```json
{"entries":[
  {"name":"20260509.txt","dir":false,"size":86065},
  {"name":"sub","dir":true,"size":0}
]}
```

### 7.5 `POST /api/fs/delete?path=/abs/path`
- Apaga ficheiro **ou** directório vazio.
- Resposta:
```json
{"ok":true,"path":"/CICLOS/2026/05/20260509.txt","dir":false}
```
- Erros: `400 path`, `403 forbidden` (path com `..`), `503 SD nao montado`,
  `409 falhou` (não existe ou ocupado).

### 7.6 `GET /api/fs/file?path=/abs/path`
- Stream do ficheiro (suporta `Range:`).
- Inline para `image/*` e `text/*`; resto vai como `attachment`.
- Limite **32 MiB** por ficheiro; chunk em PSRAM (máx. 2 MiB).

### 7.7 `GET /api/logs/tail`
```json
{"text":"...últimos ~16 KB...","truncated":true,"fileSize":102400}
```

### 7.8 `DELETE /api/logs`
```json
{"ok":true}
```

### 7.9 `GET /api/settings`
Devolve config completa (wifi, font, ftp, ntp, tz, wireguard, status).

### 7.10 `POST /api/settings`
Body JSON com qualquer subset de campos:
```json
{
  "wifi": {"ssid":"X","password":"Y"},
  "fontIndex": 2,
  "ftp": {"user":"u","password":"p"},
  "ntp": {"enabled":true,"server":"pool.ntp.org"},
  "tzOffsetSec": 0,
  "wireguard": {"enabled":false,"localIp":"","privateKey":"","peerPublicKey":"","endpoint":"","port":51820}
}
```

### 7.11 `GET|POST /api/settings/rs485`
```json
{"baud":9600,"frameProfile":0,"bauds":[1200,2400,4800,9600,19200,38400,57600,115200]}
```
POST aceita `baud` e/ou `frameProfile`.

### 7.12 `GET|POST /api/settings/mqtt`
```json
{
  "enabled": true,
  "host": "192.168.0.188",
  "port": 1883,
  "user": "",
  "hasPassword": false,
  "baseTopic": "fitadigital",
  "intervalS": 30,
  "keywords": "OPERACAO,FALHA"
}
```

### 7.13 `GET|POST /api/settings/ui`
```json
{
  "fontIndex": 2,
  "splashSeconds": 3,
  "darkMode": true,
  "screensaverEnabled": true,
  "screensaverTimeout": 60
}
```

### 7.14 `GET|POST /api/settings/net`
```json
{"monitorIp":"","downloadUrl":""}
```

### 7.15 `POST /api/settings/pin`
```json
{"current":"1234","new":"0000"}
```
- 401 se `current` errado; 400 se `new` < 4 ou > 16 chars.

### 7.16 `POST /api/system/export`
- Grava `/fdigi.cfg` no SD com snapshot completo das settings.

### 7.17 `POST /api/system/import`
- Lê `/fdigi.cfg` e aplica os campos presentes.

### 7.18 `POST /api/screenshot/take`
- Captura tela; retorna 202 com path + URL de download.

### 7.19 `POST /api/ota/upload`
- Multipart form-data, campo `firmware` com `.bin`.
- 200 `{"ok":true}` → reboot.

---

## 8. WebSocket de logs

```js
var ws = new WebSocket("ws://192.168.0.197/ws/logs");
ws.onmessage = (ev) => console.log(ev.data);
```

- Ao conectar, envia primeiro o tail (~16 KB de `/fdigi.log`).
- Cada linha de log subsequente vai como mensagem texto.

---

## 9. Receitas práticas

### 9.1 PowerShell — listar ciclos do mês

```powershell
$cred = New-Object System.Management.Automation.PSCredential("admin",
        (ConvertTo-SecureString "0000" -AsPlainText -Force))
Invoke-RestMethod "http://192.168.0.197/api/fs/list?path=/CICLOS/2026/05" `
                  -Credential $cred
```

### 9.2 PowerShell — apagar todos os .txt do mês corrente

```powershell
$cred = New-Object System.Management.Automation.PSCredential("admin",
        (ConvertTo-SecureString "0000" -AsPlainText -Force))
$ip = "192.168.0.197"
$ym = (Get-Date -Format "yyyy/MM")
$path = "/CICLOS/$ym"
$lst = Invoke-RestMethod "http://$ip/api/fs/list?path=$path" -Credential $cred
foreach ($f in $lst.entries | Where-Object { -not $_.dir -and $_.name -like "*.txt" }) {
  Invoke-RestMethod "http://$ip/api/fs/delete?path=$path/$($f.name)" `
                    -Method Post -Credential $cred
}
```

### 9.3 curl — tail dos logs

```bash
curl -u admin:0000 http://192.168.0.197/api/logs/tail | jq -r .text
```

### 9.4 curl — flash OTA

```bash
curl -u admin:0000 -F "firmware=@firmware_versions/FitaDigital_v1.45.bin" \
     http://192.168.0.197/api/ota/upload
```

### 9.5 curl — capturar screenshot e baixar

```bash
curl -u admin:0000 -X POST http://192.168.0.197/api/screenshot/take
sleep 8
# usar campo "download" da resposta acima
curl -u admin:0000 -o screen.jpg \
     "http://192.168.0.197/api/fs/file?path=/screenshots/screen-XYZ.jpg"
```

### 9.6 curl — reboot remoto

```bash
curl -u admin:0000 -X POST http://192.168.0.197/api/system/reboot
```

### 9.7 curl — alterar PIN

```bash
curl -u admin:1234 -H "Content-Type: application/json" \
     -d '{"current":"1234","new":"0000"}' \
     http://192.168.0.197/api/settings/pin
```

---

## 10. Códigos HTTP comuns

| Code | Significado |
|------|-------------|
| 200  | OK |
| 202  | Aceite, processamento async (screenshot) |
| 206  | Partial content (Range) |
| 400  | JSON/parâmetro inválido |
| 401  | Auth falhou (PIN errado) |
| 403  | Path forbidden (contém `..`) |
| 404  | Ficheiro não existe |
| 409  | Operação SD falhou |
| 413  | Ficheiro demasiado grande (>32 MiB / >2 MiB blob) |
| 416  | Range inválido |
| 500  | Erro interno (OOM, leitura SD) |
| 503  | SD não montado |

---

## 11. Limites e notas operacionais

- **Auth:** Basic em texto-claro — usar só em rede confiável ou via WireGuard.
- **PIN default:** `1234`. **Trocar em produção.**
- **Tail de log:** buffer 16 KiB em heap interno; se OOM devolve nota.
- **Stream de ficheiros:** carrega blob inteiro em PSRAM antes de servir
  (evita stall do AsyncTCP). Limite efectivo 2 MiB por request.
- **Reboot:** timer FreeRTOS de 2 s para resposta sair antes do `esp_restart()`.
- **OTA:** usa `Update.h`; reboot automático no fim. Não há rollback.
- **CORS:** GET sem `Content-Type`; POST com JSON envia `application/json`.

---

## 12. Referência cruzada

- Código: `src/web_portal/web_portal.cpp`, `src/web_portal/web_portal_html.h`
- Settings: `src/app_settings.{h,cpp}` (NVS-backed)
- OTA: `src/ota_manager.cpp`
- Screenshots: `src/screenshot.cpp`
- SD I/O: `src/sd_access.cpp` (queue de operações p/ não bloquear AsyncTCP)
