# Plano — MQTT (telemetria saúde + alerta por palavras-chave)

## Contexto

A FitaDigital está estável (heap leak fix, SD hardening, file_browser overflow fix todos commitados em main). Agora vamos adicionar duas funcionalidades dependentes de uma única ligação MQTT a um broker externo:

1. **Telemetria de saúde** — o dispositivo publica periodicamente um JSON de status (online, heap interna livre, número de boots/crashes desde fábrica, espaço livre no SD) num tópico previsível. Permite monitorizar a frota a partir de fora.
2. **Alerta por palavras-chave RS485** — quando a fita imprime uma linha que contém uma palavra-chave configurada (ex.: "ALARME", "FALHA", "CICLO COMPLETO"), o dispositivo publica essa linha num segundo tópico. Detecção feita por uma task que corre a cada 5 s e olha apenas para as últimas 10 linhas do ficheiro do dia corrente.

Ambas as funcionalidades têm de ser configuráveis pelo utilizador via UI LVGL e persistidas em NVS, à semelhança do que já existe para WiFi/FTP/WireGuard. A telemetria precisa de período configurável; as palavras-chave precisam de uma lista editável.

Outcome esperado: dispositivo conectado a broker MQTT publica `home/fitadigital/<id>/status` com cadência configurável e `home/fitadigital/<id>/keyword` quando uma das palavras é detectada. Configurações persistem em reboot e podem ser alteradas no ecrã.

---

## Arquitetura proposta

### Biblioteca

**`PubSubClient` (Knolleary)** ou **`espMqttClient` (bertmelis)**.

Recomendação: **`bertmelis/espMqttClient`** — assíncrono, integrado com AsyncTCP (já presente no projeto via web portal), suporta TLS, callbacks claros, sem polling em loop principal. PubSubClient é mais leve mas requer `client.loop()` no loop principal e usa WiFiClient síncrono que ocupa o core. Como o firmware já depende de AsyncTCP/ESPAsyncWebServer, usar espMqttClient é coerente.

Adicionar em `platformio.ini lib_deps`:
```
bertmelis/espMqttClient @ ^1.7.0
```

### Novo módulo: `src/net_mqtt.cpp/.h`

**API pública:**

```cpp
// Inicializa cliente MQTT a partir das settings NVS. Idempotente.
void net_mqtt_init(void);

// Aplica novas settings (chamada da UI quando user altera config).
void net_mqtt_apply_settings(void);

// Estado para a UI mostrar:
enum class MqttStatus : uint8_t { Disabled, Connecting, Connected, Error };
MqttStatus net_mqtt_status(void);
const char *net_mqtt_last_error(void);  // pequeno buffer estático

// Publicar manualmente (útil para keyword task).
bool net_mqtt_publish(const char *topic_suffix, const char *payload, bool retain = false);

// Suspender (para OTA, formatação SD, etc., mesma API que net_services_set_ftp_suspended).
void net_mqtt_set_suspended(bool s);
```

**Estado interno:**

- `MqttClient s_client` (não-TLS para v1; TLS pode ficar para iteração 2).
- Task FreeRTOS própria `mqtt_svc` (core 0, prio 1, stack 4 KB) para gerir conexão/reconexão e publicação periódica de telemetria. Não meter no `net_svc` para evitar interferência com WireGuard/FTP/NTP.
- Tópico base: `app_settings_mqtt_base_topic()` (default `fitadigital/<chipid>`). Sub-tópicos: `/status`, `/keyword`, `/will` (LWT).
- LWT (last will testament): publicado pelo broker quando a conexão cai. Payload `{"online":false}` retain.
- Reconexão exponencial: 5 s, 10 s, 20 s, 40 s, 60 s, 60 s... (capped).

### Novo módulo: `src/net_mqtt_keywords.cpp/.h`

Task FreeRTOS `mqtt_kw` (core 1, prio 1, stack 3 KB) que corre a cada 5 s e:

1. Lê `cycles_rs485_today_line_count()` e compara com último valor visto (`s_last_seen_count`).
2. Se aumentou, lê apenas as `min(novas, 10)` linhas mais recentes do ficheiro do dia (`/CICLOS/AAAA/MM/AAAAMMDD.txt`) via `sd_access_sync` — usar `f.seek` no offset que estiver guardado (ou no fim - N bytes se primeiro arranque).
3. Para cada linha, faz `strstr` case-insensitive contra cada palavra-chave configurada.
4. Em caso de match, chama `net_mqtt_publish("/keyword", json_payload, false)`.
5. Atualiza `s_last_seen_count` e `s_last_seen_offset`.

**Limite enforced:** nunca processa mais de 10 linhas por iteração. Se chegaram 50 linhas no intervalo de 5 s, processa só as últimas 10. Documentado no header com aviso explícito.

**Heurística de leitura barata:**

Manter `static size_t s_last_offset = 0;` no módulo. Em cada tick:

- Se `s_file_size < s_last_offset` (ficheiro rodou para o dia seguinte) → `s_last_offset = max(0, s_file_size - 4096)` (relê últimos 4 KB no novo ficheiro).
- Caso contrário, ler de `s_last_offset` até `s_file_size` num único `f.read` chunk (até 4 KB, mais que suficiente para 10 linhas de 35 chars + folga). Parsear até 10 linhas (descartar o resto).

### Settings & UI

Novas chaves NVS em `app_settings.cpp/.h`:

| Chave    | Tipo      | Default                          | Descrição                       |
|----------|-----------|----------------------------------|---------------------------------|
| `mq_on`  | bool      | false                            | Ligar/desligar MQTT             |
| `mq_h`   | String    | ""                               | Host broker (IP ou DNS)         |
| `mq_p`   | uint16    | 1883                             | Porta                           |
| `mq_u`   | String    | ""                               | Username (vazio = sem auth)     |
| `mq_pw`  | String    | ""                               | Password                        |
| `mq_b`   | String    | "fitadigital/<chipid>"           | Tópico base                     |
| `mq_iv`  | uint16    | 60                               | Intervalo telemetria (segundos) |
| `mq_kw`  | String    | ""                               | Lista de palavras separada por  |
|          |           |                                  | `;` (case-insensitive)          |

**API pública em `app_settings.h`:**

```cpp
bool app_settings_mqtt_enabled(void);
void app_settings_set_mqtt_enabled(bool);
String app_settings_mqtt_host(void);
void app_settings_set_mqtt_host(const char *);
uint16_t app_settings_mqtt_port(void);
void app_settings_set_mqtt_port(uint16_t);
String app_settings_mqtt_user(void);
String app_settings_mqtt_pass(void);
void app_settings_set_mqtt_creds(const char *u, const char *p);
String app_settings_mqtt_base_topic(void);
void app_settings_set_mqtt_base_topic(const char *);
uint16_t app_settings_mqtt_telemetry_interval_s(void);   /* clamp [10..3600] */
void app_settings_set_mqtt_telemetry_interval_s(uint16_t);
String app_settings_mqtt_keywords(void);                 /* string ";"-separada */
void app_settings_set_mqtt_keywords(const char *);
```

**UI (LVGL):** juntar em uma seção chamada SRV o FTP, Wireguard, Mqtt (crie titulos identificando cada para divi-los na page):

- Switch ligar/desligar.
- TextArea host, port, user, pass.
- TextArea base_topic.
- Slider/spinner intervalo telemetria 10–3600 s (passo 10).
- TextArea palavras-chave (separadas por `;`).
- Label de estado: "Conectado a host:port" / "Desconectado: <último erro>" / "Desativado". Refrescado a cada 1 s pelo `status_timer_cb` do `ui_app.cpp` (só quando o ecrã de definições MQTT está visível, para não pagar render desnecessário).
- Botão "Aplicar" que chama `net_mqtt_apply_settings()` para reconectar.

### Payload JSON

Usar `ArduinoJson` (já presente em `lib_deps`) com `JsonDocument` em stack (`StaticJsonDocument<256>` para telemetria, `<384>` para keyword event).

**Telemetria (`/status`)** — publicado no boot, depois cada `mq_iv` segundos, e LWT no disconnect:

```json
{
  "online": true,
  "ts": 1782820800,
  "uptime_s": 12345,
  "heap_int_free": 38600,
  "heap_int_min": 30200,
  "heap_psram_free": 4356784,
  "boot_count": 42,
  "heap_guard_reboots": 0,
  "sd_mounted": true,
  "sd_total_b": 31739936768,
  "sd_used_b": 12345678,
  "sd_free_b": 31727591090,
  "wifi_rssi_dbm": -67,
  "ip": "192.168.0.197",
  "fw_ver": "1.35"
}
```

`boot_count` e `heap_guard_reboots` são novos contadores em NVS (`bc`, `hgc`), incrementados em `boot_journal_init` e `heap_monitor` watchdog respectivamente.

**Keyword (`/keyword`)** — publicado quando uma linha contém match:

```json
{
  "ts": 1782820800,
  "kw": "ALARME",
  "line": "12:34:56 ALARME TEMP 95C",
  "file": "/CICLOS/2026/04/20260426.txt",
  "line_no": 1247
}
```

### Inicialização

Em `app.cpp` após `net_services_start_background_task()`:

```cpp
net_mqtt_init();
net_mqtt_keywords_start();
```

`net_mqtt_init` cria a task `mqtt_svc` mesmo se desativado nas settings (a task verifica `app_settings_mqtt_enabled()` periodicamente — assim toggle via UI funciona sem reboot). Similarmente `net_mqtt_keywords_start` cria task que verifica enabled em cada iter.

### Suspensão / interacção com OTA

Quando OTA HTTP está em curso, AsyncTCP não tolera bem clientes a competir. Adicionar `net_mqtt_set_suspended(true)` ao mesmo lugar onde já se chama `net_services_set_ftp_suspended(true)` (procurar callers em `web_portal.cpp` / `ota_manager.cpp`).

### Heap orçamento (preocupação)

Bisecção do leak passada provou que AsyncTCP + ESPAsyncWebServer já consomem ~30 KB internos. Adicionar MQTT (espMqttClient usa AsyncTCP partilhado, +5–8 KB) deve estar dentro da margem actual (~38 KB livres pós-boot). Validar com `[HEAP]` log do `heap_monitor` em soak após implementação.

Se OOM, alternativas:
- Mover MQTT broker buffer para PSRAM (espMqttClient suporta config buffer size).
- Limitar tamanho do payload de keyword (truncar linha a 200 chars).

---

## Mudanças propostas (por fase)

### Fase 1 — settings + skeleton sem rede (1–2 h)

1. Adicionar campos NVS em `src/app_settings.cpp/.h` com getters/setters acima.
2. Adicionar contadores `boot_count` e `heap_guard_reboots` em NVS:
   - `boot_count` incrementado em [src/boot_journal.cpp](src/boot_journal.cpp) `boot_journal_init`.
   - `heap_guard_reboots` incrementado em [src/heap_monitor.cpp](src/heap_monitor.cpp) imediatamente antes de `esp_restart()`.
3. Criar `src/net_mqtt.h` + `src/net_mqtt.cpp` com API stub (sem ligação real ainda — só estado em RAM e logs).
4. Criar `src/net_mqtt_keywords.h` + `src/net_mqtt_keywords.cpp` com task stub que só lê `cycles_rs485_today_line_count` e loga.
5. Compilar — confirmar que a árvore continua a build.

### Fase 2 — UI (2–3 h)

1. Em `src/ui/ui_app.cpp`, adicionar secção "MQTT" na aba recem criada SRV. Reusar padrões existentes (textareas, sliders, botão Aplicar) — copiar do bloco FTP existente. 
2. Refresh label de estado MQTT em `status_timer_cb` (1 s) só quando o ecrã está visível, alinhado com `refresh_settings_ftp_label` que já existe.
3. Botão "Aplicar" chama `net_mqtt_apply_settings()` + toast de feedback.

### Fase 3 — implementar cliente MQTT real (3–4 h)

1. Adicionar `bertmelis/espMqttClient` ao `platformio.ini`.
2. Implementar `net_mqtt.cpp`:
   - Task `mqtt_svc` com loop principal: verifica `app_settings_mqtt_enabled()`; se on e não conectado, tenta conectar com backoff exponencial; se conectado, publica telemetria a cada `mq_iv` segundos.
   - Callbacks: `onConnect`, `onDisconnect`, `onMessage` (não usado v1, mas armado para subscribe futuro).
   - LWT configurado em `setWill()` antes de `connect()`.
   - Buffer JSON em stack (`StaticJsonDocument<256>`).
3. Hook em `boot_journal_init` para incrementar `boot_count`.
4. Hook em `heap_monitor` watchdog para incrementar `heap_guard_reboots` antes do `esp_restart()`.

### Fase 4 — keyword detector (1–2 h)

1. Implementar `net_mqtt_keywords.cpp` task `mqtt_kw`:
   - Tick 5 s.
   - Compara `s_last_seen_count` com `cycles_rs485_today_line_count()`.
   - Se mudou: monta caminho do dia via `cycles_rs485_format_today_path` (já existe), lê chunk do offset guardado para o final do ficheiro via `sd_access_sync` (com `SD.open` + `seek` + `read`).
   - Splita em linhas (no máximo as últimas 10) via parser local.
   - Para cada linha: itera palavras-chave (split de `app_settings_mqtt_keywords()` por `;`) e faz `strcasestr` (newlib tem; senão, implementar).
   - Match → JSON + `net_mqtt_publish("/keyword", json, false)`.
2. Cuidado com mudança de dia: detectar via comparação de path; se mudou, reset offset para 0 (ou tail do novo ficheiro).

### Fase 5 — testes (1–2 h)

1. **Build + flash** com Mosquitto local (broker rápido em PC para testar):
   ```
   docker run -it --rm -p 1883:1883 eclipse-mosquitto
   ```
2. Configurar host=IP do PC, port 1883, base_topic=`fitatest`, intervalo=15 s, palavras=`ALARME;FALHA`. Aplicar.
3. Verificar telemetria com `mosquitto_sub -t 'fitatest/#' -v`.
4. Enviar via RS485 (`tools/rs485_soak_gen.py` adaptado): linha sem keyword → não publica; linha com `ALARME ...` → publica em `/keyword` em <5 s.
5. Reboot do device → confirmar `boot_count` aumentou no JSON, e LWT publicou `online:false` durante o reboot.
6. **Heap soak** 30 min com MQTT ligado e telemetria 15 s: heap_monitor `[HEAP]` deve manter drain ~0 (regra: <50 B/min).

---

## Ficheiros a criar/modificar

**Novos:**
- `src/net_mqtt.h`
- `src/net_mqtt.cpp`
- `src/net_mqtt_keywords.h`
- `src/net_mqtt_keywords.cpp`

**Modificados:**
- [platformio.ini](platformio.ini) — adicionar `bertmelis/espMqttClient` em `lib_deps`.
- [src/app_settings.h](src/app_settings.h) + [src/app_settings.cpp](src/app_settings.cpp) — novas chaves + contadores.
- [src/boot_journal.cpp](src/boot_journal.cpp) — incrementar `boot_count` em init.
- [src/heap_monitor.cpp](src/heap_monitor.cpp) — incrementar `heap_guard_reboots` antes de `esp_restart()`.
- [src/app.cpp](src/app.cpp) — chamar `net_mqtt_init()` e `net_mqtt_keywords_start()` após `net_services_start_background_task()`.
- [src/ui/ui_app.cpp](src/ui/ui_app.cpp) — secção MQTT em Definições → Sistema, label de estado refresh.
- [src/web_portal/web_portal.cpp](src/web_portal/web_portal.cpp) ou [src/ota_manager.cpp](src/ota_manager.cpp) — `net_mqtt_set_suspended` durante OTA.
- [TODO.md](TODO.md) — mover entry para "Em curso" no início, "Feito" no fim.

**Não tocar (a menos que necessário):**
- Caminho RS485 (`cycles_rs485.cpp`/`rs485_buffer.cpp`) — keyword detector lê o ficheiro SD, não interfere com captura.

---

## Verificação

**Sanidade compile-only (Fase 1):**
- `pio run -e esp32-s3-touch-lcd-4_3b` passa sem warnings novos.
- Stub `net_mqtt_status()` retorna `Disabled`.

**UI (Fase 2):**
- Flash, abrir Definições → SRV (servers) → MQTT, ver campos. Editar, gravar, rebootar, confirmar persistência NVS.

**Conectividade (Fase 3):**
- Mosquitto local: `mosquitto_sub -t '#' -v`.
- Configurar host PC + port 1883 + base_topic `fitatest`, ligar.
- Aparecer 1 mensagem `fitatest/<id>/status` no boot.
- Após `mq_iv` segundos, mais uma. E assim por diante.
- Desligar device (corte de energia) → broker deve emitir LWT `online:false` retain.

**Keyword (Fase 4):**
- Configurar palavras `ALARME;TEMP`.
- Soak script: `tools/rs485_soak_gen.py --interval 5 --lines 1 --length 35`.
- Inserir manualmente uma linha `ALARME 95C` via outro porto serial ou via teste manual.
- Mosquitto deve mostrar `fitatest/<id>/keyword` com a linha em <5 s.
- Linhas sem keyword: nenhum publish em `/keyword`.

**Soak final pós-tudo:**
- 30 min, MQTT on, telemetria 15 s, palavras `TEST`, gerador 5 linhas/60 s sem palavra: heap drain <50 B/min, sem crashes, telemetria publicada continuamente.

**Validação cruzada com bug anterior:**
- File browser viewer aberto durante soak (já tem fix de overflow): nenhum crash.
- Heap monitor `[HEAP_GUARD]` não dispara.
