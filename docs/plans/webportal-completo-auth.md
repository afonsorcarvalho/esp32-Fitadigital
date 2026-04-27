# Plano — Web portal completo + autenticação

## Contexto

O dispositivo já tem um web portal HTTP (porta 80) servido pelo ESPAsyncWebServer. Hoje cobre apenas um subconjunto das configurações (WiFi/FTP/NTP/WireGuard) e expõe APIs de logs, ficheiros SD, OTA e WebSocket. As novas tabs LVGL (RS485, SD, Scr, Sys, MQTT, EXPORT/IMPORT) estão fora do portal — utilizador hoje só consegue editá-las pelo touchscreen.

Adicionalmente, o portal está aberto sem autenticação. Qualquer um na mesma rede WiFi pode mexer em tudo (config, OTA, ficheiros SD).

Outcome desejado:
1. **Paridade UI ↔ Web**: tudo o que se configura na UI (8 tabs do tabview) é editável também no web portal.
2. **Auth**: portal protegido por basic auth com a mesma senha do PIN do device (`app_settings_settings_pin`, default "1234"). Username fixo `admin`. Todas as rotas `/api/*` e `/` exigem auth; excepção possível: `/api/health` para monitorização.

---

## Arquitetura

### Auth — fase A (1-2 h)

**Padrão**: HTTP Basic Auth (`Authorization: Basic base64(user:pass)`). ESPAsyncWebServer tem `request->authenticate(const char *user, const char *pass)` e `request->requestAuthentication()` para emitir 401 com `WWW-Authenticate`.

Helper local em `web_portal.cpp`:

```cpp
static bool web_auth_check(AsyncWebServerRequest *req) {
    String pin = app_settings_settings_pin();
    if (!req->authenticate("admin", pin.c_str())) {
        req->requestAuthentication();
        return false;
    }
    return true;
}
```

Aplicar em todos os handlers:

```cpp
s_srv->on("/api/settings", HTTP_GET, [](AsyncWebServerRequest *r) {
    if (!web_auth_check(r)) return;
    handle_settings_get(r);
});
```

Excepção: rota `/api/health` (GET) sem auth — útil para monitorização externa (curl simples). Devolve `{"online":true,"uptime_s":N}`.

Nota: PIN é a senha do device (default 1234, mas user deve mudar). User entra `admin` + PIN no diálogo do browser. Cache de auth pelo browser por sessão.

### Web portal completo — fase B (4-6 h)

**Endpoints novos `/api/*` (GET retorna JSON, POST aceita JSON parcial):**

| Endpoint                      | Método      | Cobre                                              |
|-------------------------------|-------------|----------------------------------------------------|
| `/api/settings`               | GET, POST   | (já existe; adicionar campos em falta)             |
| `/api/settings/rs485`         | GET, POST   | baud, frame_profile                                |
| `/api/settings/mqtt`          | GET, POST   | enabled, host, port, user, pass, base_topic, iv, kw|
| `/api/settings/ui`            | GET, POST   | font_idx, splash_s, dark_mode, scr_enabled, scr_timeout |
| `/api/settings/net`           | GET, POST   | mon_ip, dl_url                                     |
| `/api/settings/pin`           | POST        | mudar PIN (requer PIN actual no body)              |
| `/api/system/reboot`          | POST        | reboot device (com delay 2s p/ resposta)           |
| `/api/system/export`          | POST        | escreve fdigi.cfg para SD                          |
| `/api/system/import`          | POST        | lê fdigi.cfg do SD para NVS                        |
| `/api/system/status`          | GET         | uptime, heap, sd, mqtt status, fw_ver              |
| `/api/health`                 | GET         | sem auth, mínimo                                   |

**Reuso de `/api/settings` existente**: estender o JSON para incluir todos os campos. Manter retrocompatibilidade (campos antigos continuam a vir).

Alternativa: manter `/api/settings` legacy + criar endpoints novos por seção. Recomendo **endpoints por seção** (`/api/settings/rs485` etc.) porque:
- HTML pode fazer fetch independente por aba (lazy load).
- Menos contenção (um POST de RS485 não toca em WiFi).
- Mais fácil de manter.

**HTML novo** em `web_portal_html.h`:

Layout vertical com tabs (HTML/CSS puro, sem framework — reuse o estilo já presente). 8 abas:
- Wi-Fi (existe, completar com status RSSI + IP).
- SRV (FTP + WireGuard + MQTT, mesma agrupação que UI). Headers visuais por subsecção.
- Hora (NTP + manual + TZ).
- RS485 (baud roller + frame profile).
- Logs (já existe `/api/logs/tail`; adicionar download integral + clear).
- SD (lista de ficheiros já existe; adicionar formatar com confirmação).
- Sys (OTA upload já existe; adicionar export/import/reboot/change-pin).
- (Scr opcional — settings de UI no web podem não fazer sentido se não dá feedback visual; deixar para iteração 2.)

JS: `fetch('/api/settings/rs485')` enche os campos; on submit `POST` JSON. Toast de feedback.

Tamanho HTML: actual é ~5 KB (texto). Estimativa final: **15-20 KB**. Cabe em flash (~36% usado em 16 MB), mas inflando a string compilada `WEB_PORTAL_HTML` aumenta o `.bss`. Decidir:

- **A**: manter tudo num único `WEB_PORTAL_HTML[]` em PROGMEM (`extern const char WEB_PORTAL_HTML[] PROGMEM`). Pesado mas simples.
- **B**: servir o HTML do SD (`/web/index.html`). Mais rápido para iterar, mas requer SD montado para o portal funcionar — no boot, problemático.
- **C**: dividir em 8 ficheiros em SPIFFS (já montado para boot_journal). Médio. Permite update sem recompile.

Recomendo **A** para v1 (mais robusto). C é melhoria futura.

### Status MQTT no portal — fase C (1 h, parte de B)

`/api/system/status` retorna estado MQTT (status enum + last_error). HTML mostra na aba MQTT/SRV.

### Mudança de PIN via web — fase D (1 h, parte de B)

`POST /api/settings/pin` com body `{"current": "1234", "new": "9876"}`. Valida current contra `app_settings_settings_pin()`, verifica new len 4..16, chama `app_settings_set_settings_pin(new)`. Retorna 200/400/401.

Após mudar PIN, próximas requests vão exigir nova senha — browser pede re-auth.

---

## Mudanças propostas (por fase)

### Fase A — Auth (1-2 h)

1. Adicionar `web_auth_check()` em `web_portal.cpp`.
2. Wrap todos os handlers existentes (`/`, `/api/settings`, `/api/logs/*`, `/api/fs/*`, `/api/ota/*`, `/api/files/*`).
3. Endpoint `/api/health` sem auth (excepção explícita).
4. Build + flash. User testa: ao abrir `http://192.168.0.197/`, browser pede user/pass. `admin/<pin>` deve passar.

### Fase B — endpoints novos (3-4 h)

1. Em `web_portal.cpp` adicionar handlers:
   - `handle_rs485_get/post`, `handle_mqtt_get/post`, `handle_ui_get/post`, `handle_net_get/post`.
   - `handle_pin_post`, `handle_reboot_post`, `handle_export_post`, `handle_import_post`, `handle_status_get`, `handle_health_get`.
2. Cada handler em <40 linhas: parse JSON in/out, chama setters/getters de `app_settings`, devolve 200 com confirmação.
3. Reboot: mete a chamada `esp_restart()` num `lv_async_call` ou timer 2s para a resposta HTTP ter tempo de sair.

### Fase C — HTML completo (3-4 h)

1. Reescrever `web_portal_html.h` com tabs (CSS puro `display:none` por aba).
2. JS de page-load chama `fetch('/api/settings/...')` para cada aba só na primeira vez que é aberta (lazy).
3. Cada aba tem botão Salvar que faz POST e mostra toast.
4. Aba Sys: 4 botões — Exportar, Importar, Reboot, Mudar PIN.

### Fase D — testes (1 h)

1. Browser:
   - Abrir `http://192.168.0.197/`.
   - Login `admin/1234` → vê todas as abas.
   - Editar um campo em cada aba, salvar, refresh, confirmar persistiu.
   - Mudar PIN → re-prompt → entrar com PIN novo OK.
   - Reboot → device reinicia, browser perde conexão, reconnect manual.
   - Export → pequeno toast "OK". Import → confirmação.
2. Sem auth: `curl http://192.168.0.197/api/settings` → 401. `curl -u admin:1234 ...` → 200.
3. `/api/health` sem auth → 200.

### Fase E — soak validation (30 min)

Soak normal com tráfego RS485 + browser aberto a fazer poll a `/api/system/status` cada 5s. Confirmar:
- Drain heap continua ~0 (auth + endpoints novos não introduzem leak).
- Sem crashes.
- MQTT continua a publicar.

---

## Ficheiros a modificar

**Modificados:**
- [src/web_portal/web_portal.cpp](src/web_portal/web_portal.cpp) — +400-500 linhas (handlers novos + auth wrapper).
- [src/web_portal/web_portal_html.h](src/web_portal/web_portal_html.h) — reescrita major (~15-20 KB final).
- [src/app_settings.h/.cpp](src/app_settings.cpp) — possíveis getters extras se faltarem (ex: `app_settings_font_index_set`, etc.).
- [TODO.md](TODO.md) — registo.

**Novos:** nenhum (tudo em `web_portal/`).

---

## Riscos

- **Heap**: HTML maior em PROGMEM consome flash (`.text`), não heap interna. AsyncWebServer aloca buffer por request — endpoints novos podem aumentar peak. Validar com `[HEAP]` em soak.
- **Auth quebra integrações existentes**: se houver scripts ou OTA push que usam `/api/*` sem auth, vão falhar. Documentar credenciais.
- **PIN no Basic Auth**: enviado em base64 (não criptografado). HTTP é cleartext — se quiser segurança, precisa HTTPS (esp32 suporta TLS mas custa heap). Aceitar trade-off para v1.
- **Reboot via web**: utilizador acidentalmente clicar no botão. Modal de confirmação no HTML.

---

## Verificação

Após cada fase:
- `pio run` passa sem warnings novos.
- Browser → `http://192.168.0.197/` exige PIN.
- Cada endpoint testado manualmente com curl.
- Heap drain ~0 em soak.
- PIN mudado via web → próximas requests pedem novo PIN.
