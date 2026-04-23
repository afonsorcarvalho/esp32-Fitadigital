# Firmware de provisionamento WireGuard — ESP32-S3 com LVGL

Você é um engenheiro de firmware embarcado. Escreva o firmware completo para um dispositivo IoT que se provisiona sozinho como peer de um túnel WireGuard, usando um QR code na tela como mecanismo de pareamento.

## Hardware alvo

- **MCU**: ESP32-S3 (dual-core, com PSRAM)
- **Display**: 480 × 600 pixels (retrato). Controlador: **[PREENCHA — ex: ILI9488, ST7796, GC9A01]**
- **Touchscreen**: **[PREENCHA — ex: FT6236, GT911, ou sem touch]**
- **Conectividade**: Wi-Fi 2.4 GHz embutido

## Stack

- **Framework**: ESP-IDF v5.1+
- **UI**: LVGL v8.3+ (incluindo `lv_qrcode` de `extra/libs/qrcode`)
- **WireGuard**: componente `esp_wireguard` (github.com/trombik/esp_wireguard)
- **Crypto**: mbedTLS (já vem no IDF) para TLS e geração Curve25519
- **HTTP**: `esp_http_client`
- **Persistência**: NVS (encriptado quando possível)

## Protocolo de enrollment (backend já existe, não implemente)

O servidor expõe três endpoints HTTPS que você **consome**:

### 1. `POST /api/enroll`

Chamado logo após gerar o par de chaves.

Request body:
```json
{"device_id": "<eFuse MAC em hex>", "public_key": "<base64 WG pubkey>"}
```

Response:
```json
{
  "activation_code": "xK3-pA9",
  "activation_url": "https://admin.servidor.com/activate?code=xK3-pA9",
  "poll_url": "https://vpn.servidor.com/api/enroll/status/xK3-pA9"
}
```

### 2. `GET /api/enroll/status/<code>` (long-poll)

Chamado em loop depois de exibir o QR.

- **204**: ainda pendente, continue pollando.
- **200** com body:
  ```json
  {
    "address": "10.8.0.42/24",
    "server_public_key": "<base64>",
    "server_endpoint": "vpn.servidor.com:51820",
    "allowed_ips": "10.8.0.0/24",
    "dns": "10.8.0.1"
  }
  ```
- **410**: código expirou; recomeçe o fluxo do zero.

### 3. `POST /api/activate`

**NÃO** é chamado pelo device — é o admin quem chama após escanear o QR. Você só precisa garantir que a `activation_url` retornada no enroll vai no QR.

## Máquina de estados

Implemente o firmware como uma state machine explícita.

| Estado | Ação | Tela LVGL |
|---|---|---|
| `BOOT` | Inicializa HW, NVS, LVGL | Logo + "Inicializando..." |
| `WIFI_SETUP` | Se não há SSID salvo, AP mode ou BLE prov | Instruções para conectar |
| `WIFI_CONNECTING` | `esp_wifi_connect` | Spinner + SSID |
| `CHECK_STORED_CONFIG` | Se NVS já tem config válida, pula para `APPLYING_CONFIG` | — |
| `KEYGEN` | Gera par Curve25519 localmente | Spinner "Gerando chaves..." |
| `ENROLLING` | `POST /api/enroll` | Spinner "Registrando dispositivo..." |
| `SHOWING_QR` | Renderiza QR + código textual + countdown, faz polling | QR grande centralizado |
| `APPLYING_CONFIG` | Escreve no NVS e inicializa `esp_wireguard` | "Conectando túnel..." |
| `CONNECTED` | Keepalive, mostra stats (IP, ping, RSSI) | Estado verde |
| `ERROR` | Mensagem curta + botão retry | Cor de erro + mensagem |

Regras de transição:

- QR expira em 10 min sem ativação → volta para `KEYGEN` e descarta as chaves antigas.
- Polling com backoff exponencial em erro: 3s → 6s → 12s → 30s (máx).
- Perda de túnel em `CONNECTED`: reconecta até 5 vezes, depois re-enrolla automaticamente.
- Botão hardware ou toque longo (se houver touch) → **factory reset** (`nvs_flash_erase` + `esp_restart`).

## Requisitos de segurança (críticos)

1. **Chave privada jamais sai do device.** Gere localmente com mbedTLS Curve25519 (`mbedtls_ecdh_gen_public` com `MBEDTLS_ECP_DP_CURVE25519`). Salve no NVS com encriptação (use `nvs_flash_secure_init` derivado de eFuse key quando possível).
2. **TLS com validação obrigatória.** Use `esp_crt_bundle` ou embuta o cert raiz via `EMBED_TXTFILES` no CMake. Nunca aceite cert inválido, nem mesmo com flag de debug ligada.
3. **Device ID estável.** Derive de `esp_efuse_mac_get_default()` e formate como hex lowercase.
4. **Zero segredo hardcoded no binário.** Tudo em NVS ou Kconfig.
5. **Zeroize buffers de chave** após uso com `mbedtls_platform_zeroize`.
6. **Base64** do WireGuard é padrão (não URL-safe). Use a implementação do mbedTLS.

## Requisitos de UX

- Fonte legível a 1 metro: registre `LV_FONT_MONTSERRAT_24` e `_32` no Kconfig, não use o default.
- Tela de QR: mínimo **320 × 320 px**, com o `activation_code` impresso abaixo em fonte grande (fallback caso o admin digite manualmente).
- Countdown visível: "Expira em MM:SS".
- Paleta consistente: arquivo `ui_theme.c` com cores, fontes e `lv_style_t` reutilizáveis. Nada de cor mágica espalhada pelo código.
- **Strings de UI em português brasileiro.**
- **Logs (`ESP_LOG*`) em inglês.**

## Estrutura do projeto

```
main/
├── CMakeLists.txt
├── main.c                  — entrypoint, cria tasks
├── app_state.h / .c        — state machine global + event queue
├── wifi_manager.c          — conexão Wi-Fi + provisioning inicial
├── wg_provision.c          — enrollment: orquestra keygen, enroll, poll, apply
├── wg_crypto.c             — Curve25519 keygen, base64 encode/decode
├── http_client.c           — wrapper esp_http_client com cert pinning
├── storage.c               — NVS read/write (chaves, config, ssid)
└── ui/
    ├── ui_main.c           — init LVGL + driver de display
    ├── ui_screens.c        — uma função de render por estado
    └── ui_theme.c          — paleta, fontes, styles

components/
└── esp_wireguard/          — componente externo via git submodule

Kconfig.projbuild            — opções configuráveis
sdkconfig.defaults           — defaults sensatos
partitions.csv               — custom se precisar NVS maior
README.md
```

Use **FreeRTOS tasks separadas**:
- UI task: roda `lv_timer_handler` em loop (prioridade média, core 1).
- Network task: provisioning e polling (prioridade média, core 0).
- WireGuard tem suas próprias tasks internas.

Comunicação entre tasks via **event queue** (`xQueueCreate`), não por variáveis globais com mutex ad-hoc.

## Kconfig

Exponha via `Kconfig.projbuild`:

- `ENROLL_SERVER_URL` (string) — URL base do backend
- `ACTIVATION_TIMEOUT_SEC` (int, default 600)
- `POLL_INTERVAL_INITIAL_MS` (int, default 3000)
- `POLL_INTERVAL_MAX_MS` (int, default 30000)
- `WG_DEFAULT_PORT` (int, default 51820)
- `DEVICE_MODEL` (string) — identificador do modelo para logs

## Robustez

- Todo `esp_err_t` retornado **deve** ser verificado. `ESP_ERROR_CHECK` só em init crítico; no resto, trate e logue com contexto.
- Timeouts HTTP de 10s, com retry no transport-level (3 tentativas).
- Task watchdog habilitado — se alguma task travar, restart.
- Não bloqueie a task de UI com `vTaskDelay` longos — use timers LVGL ou eventos.

## Entregáveis

1. Código-fonte completo estruturado como acima.
2. **README.md** contendo:
   - Dependências (versões de ESP-IDF, LVGL, `esp_wireguard`)
   - Comandos de build e flash (`idf.py set-target esp32s3`, etc)
   - Como configurar o servidor via `idf.py menuconfig`
   - Como fazer factory reset
   - Troubleshooting básico (Wi-Fi não conecta, QR não aparece, etc)
3. `sdkconfig.defaults` com configs recomendadas (PSRAM habilitada, CPU 240 MHz, partition table custom).
4. `partitions.csv` custom se for necessário.
5. Comentários e nomes de variáveis/funções em **inglês** (padrão ESP-IDF).

## O que NÃO fazer

- Não gerar o par de chaves WireGuard no backend.
- Não usar `printf` direto — sempre `ESP_LOG*` com TAG por módulo.
- Não ignorar retornos de função (nada de `esp_http_client_perform(c);` solto).
- Não hardcoded-ar URLs, credenciais Wi-Fi ou chaves.
- Não bloquear a UI task com chamadas de rede síncronas.
- Não misturar LVGL v8 e v9 APIs — escolha uma versão e fique nela.

## Primeira iteração

Entregue um primeiro commit funcional contendo:

1. Estrutura de pastas + `CMakeLists.txt` compilando limpo.
2. LVGL inicializado, driver do display funcionando, boot screen visível.
3. Stub da state machine com log de transições (`ESP_LOGI` em cada `enter`/`exit`).
4. `README.md` com instruções de build.

A partir daí, implemente estado por estado, validando cada transição antes de seguir para a próxima.
