# v2.2.0 Cycle Config NVS+UI Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Tornar `cycle_detector` (start_pattern, end_pattern, idle_timeout_s) configurável via portal web + persistir em NVS, com live re-init e graceful INTERRUPTED quando ACTIVE.

**Architecture:** Extender `app_settings` com 3 chaves NVS (`cyc_st`, `cyc_en`, `cyc_to`). Adicionar API `cycle_detector_reconfigure()` que emite INTERRUPTED+NDJSON antes de re-init. Boot lê NVS em vez de hardcoded. Novo endpoint `/api/cycles/config` GET/POST. Nova tab "Ciclo" no portal HTML/JS. Validation: patterns 0-47 chars, idle 0-86400, start vazio desactiva.

**Tech Stack:** ESP32-S3 + Arduino framework + ESPAsyncWebServer + ArduinoJson + Preferences (NVS). Build via PlatformIO COM3. Integration tests via Python `requests` + `HTTPDigestAuth`.

**Spec:** `docs/superpowers/specs/2026-05-19-cycle-config-nvs-ui-design.md`
**Base:** main HEAD `b243cef` (post-v2.1.2 production-ready)
**Target tag:** v2.2.0

## File Structure

| File | Action | Responsibility |
|------|--------|----------------|
| `src/app_settings.h` | Modify | +4 function declarations |
| `src/app_settings.cpp` | Modify | +getter/setter implementations, NVS keys, defaults |
| `src/cycle_detector.h` | Modify | +1 declaration `cycle_detector_reconfigure` |
| `src/cycle_detector.cpp` | Modify | +reconfigure function (graceful INTERRUPTED) |
| `src/app.cpp` | Modify | line ~245: substituir hardcoded por reads NVS |
| `src/web_portal/web_portal.cpp` | Modify | +handle_cycle_settings_get/body + route registration |
| `src/web_portal/web_portal_html.h` | Modify | +tab "Ciclo" HTML + JS handler |
| `platformio.ini` | Modify | bump FITADIGITAL_VERSION 2.12 → 2.20 |
| `tools/test_cycle_config.py` | Create | Integration test probe (HTTP GET/POST + smoke) |

---

### Task 1: NVS getter/setter para start_pattern

**Files:**
- Modify: `src/app_settings.h` (após line 193, antes do fim)
- Modify: `src/app_settings.cpp` (no final do ficheiro)

- [ ] **Step 1: Adicionar declaração em `src/app_settings.h`**

Adicionar antes da última `}` (ou no fim se sem closing brace, antes de qualquer `extern "C"` closing):

```cpp
/* ------------------------------------------------------------------ */
/* Cycle detector (v2.2.0)                                              */
/* ------------------------------------------------------------------ */

/** Pattern de inicio de ciclo (substring case-insensitive).
 *  Default "OPERACAO". String vazia desactiva detector. Max 47 chars. */
String app_settings_cycle_start_pattern(void);

/** Pattern de fim de ciclo. Default "FIM CICLO". Max 47 chars. */
String app_settings_cycle_end_pattern(void);

/** Idle timeout em segundos. 0=sem timeout. Default 900. Range [0, 86400]. */
uint32_t app_settings_cycle_idle_timeout_s(void);

/** Persiste config atomicamente. Patterns truncados a 47 chars,
 *  idle_s clampa [0, 86400]. */
void app_settings_set_cycle_config(const char *start, const char *end, uint32_t idle_s);
```

- [ ] **Step 2: Adicionar implementação no fim de `src/app_settings.cpp`**

```cpp
/* ------------------------------------------------------------------ */
/* Cycle detector (v2.2.0)                                              */
/* ------------------------------------------------------------------ */

static constexpr size_t kCyclePatternMax = 47U;  /* +1 NUL = 48 = cycle_detector kPatternMax */
static constexpr uint32_t kCycleIdleMaxS = 86400U; /* 24h */

String app_settings_cycle_start_pattern(void) {
  return s_prefs.getString("cyc_st", "OPERACAO");
}

String app_settings_cycle_end_pattern(void) {
  return s_prefs.getString("cyc_en", "FIM CICLO");
}

uint32_t app_settings_cycle_idle_timeout_s(void) {
  return s_prefs.getUInt("cyc_to", 900U);
}

void app_settings_set_cycle_config(const char *start, const char *end, uint32_t idle_s) {
  char st[kCyclePatternMax + 1U] = {0};
  char en[kCyclePatternMax + 1U] = {0};
  if (start != nullptr) {
    strncpy(st, start, kCyclePatternMax);
  }
  if (end != nullptr) {
    strncpy(en, end, kCyclePatternMax);
  }
  if (idle_s > kCycleIdleMaxS) {
    idle_s = kCycleIdleMaxS;
  }
  s_prefs.putString("cyc_st", st);
  s_prefs.putString("cyc_en", en);
  s_prefs.putUInt("cyc_to", idle_s);
}
```

- [ ] **Step 3: Build (compile-check)**

Run: `C:/Users/Afonso/.platformio/penv/Scripts/platformio.exe run`
Expected: SUCCESS, sem warnings novos sobre as funções adicionadas.

- [ ] **Step 4: Commit**

```bash
git add src/app_settings.h src/app_settings.cpp
git commit -m "feat(settings): NVS keys cyc_st/cyc_en/cyc_to com defaults atuais"
```

---

### Task 2: cycle_detector_reconfigure API

**Files:**
- Modify: `src/cycle_detector.h` (após `cycle_detector_init` declaração)
- Modify: `src/cycle_detector.cpp` (no final, após `cycle_detector_status_json`)

- [ ] **Step 1: Declarar em `src/cycle_detector.h`**

Inserir antes de `cycle_detector_process_line`:

```cpp
/**
 * Re-aplica configuracao em runtime. Se state==ACTIVE, fecha ciclo como
 * CYCLE_STATUS_INTERRUPTED + escreve NDJSON antes de re-init.
 * Atualiza watchdog RTC baseline. Thread-safe.
 *
 * @param start_pattern  novo start; NULL/"" desactiva detector
 * @param end_pattern    novo end; NULL/"" -> so' fecha por timeout
 * @param idle_timeout_s novo timeout (0 = sem timeout)
 */
void cycle_detector_reconfigure(const char *start_pattern,
                                const char *end_pattern,
                                uint32_t idle_timeout_s);
```

- [ ] **Step 2: Implementar em `src/cycle_detector.cpp`**

Inserir antes do final `} /* extern "C" */` (ou no fim do ficheiro se sem extern C closing):

```cpp
void cycle_detector_reconfigure(const char *start_pattern,
                                const char *end_pattern,
                                uint32_t idle_timeout_s) {
  Current snap;
  bool emit_interrupted = false;
  {
    Lock lk;
    if (s_cur.state == State::ACTIVE) {
      snap = s_cur;
      emit_interrupted = true;
    }
  }  /* lock released antes de emit_cycle_close (faz sd_access_sync) */
  if (emit_interrupted) {
    emit_cycle_close(snap, CYCLE_STATUS_INTERRUPTED);
  }
  cycle_detector_init(start_pattern, end_pattern, idle_timeout_s);
}
```

- [ ] **Step 3: Build (compile-check)**

Run: `C:/Users/Afonso/.platformio/penv/Scripts/platformio.exe run`
Expected: SUCCESS.

- [ ] **Step 4: Commit**

```bash
git add src/cycle_detector.h src/cycle_detector.cpp
git commit -m "feat(cycle): cycle_detector_reconfigure live API com INTERRUPTED graceful"
```

---

### Task 3: Boot wiring app.cpp lê NVS em vez de hardcoded

**Files:**
- Modify: `src/app.cpp` (lines ~239-246)

- [ ] **Step 1: Substituir bloco em `src/app.cpp`**

Localizar (search by line content "cycle_detector_init"):

```cpp
  if (sd_ok) {
    cycles_rs485_init();
    /* v2.1.0: cycle detector state machine. Patterns default:
     *   start "OPERACAO" (ajustar via futuro NVS/UI conforme equipamento)
     *   end   "FIM CICLO" (substring; "FIM", "RESULTADO" tambem matcham parcial)
     *   idle_timeout 900s = 15min (auto-fecha ciclo orfao). */
    cycle_detector_init("OPERACAO", "FIM CICLO", 900U);
  }
```

Substituir por:

```cpp
  if (sd_ok) {
    cycles_rs485_init();
    /* v2.2.0: patterns + idle timeout lidos de NVS via app_settings.
     * Defaults baked nos getters (OPERACAO / FIM CICLO / 900s) se 1o boot. */
    const String cyc_start = app_settings_cycle_start_pattern();
    const String cyc_end = app_settings_cycle_end_pattern();
    cycle_detector_init(cyc_start.c_str(), cyc_end.c_str(),
                        app_settings_cycle_idle_timeout_s());
  }
```

- [ ] **Step 2: Confirmar include `app_settings.h`**

Verificar com Grep que `src/app.cpp` ja inclui `"app_settings.h"`. Se não, adicionar no topo dos `#include`.

Run: `grep -n "app_settings.h" src/app.cpp`
Expected: 1+ match.

- [ ] **Step 3: Build (compile-check)**

Run: `C:/Users/Afonso/.platformio/penv/Scripts/platformio.exe run`
Expected: SUCCESS.

- [ ] **Step 4: Commit**

```bash
git add src/app.cpp
git commit -m "feat(boot): cycle_detector lê config NVS via app_settings"
```

---

### Task 4: HTTP GET /api/cycles/config handler

**Files:**
- Modify: `src/web_portal/web_portal.cpp` (junto com outros `handle_*_get`, recomendado após `handle_rs485_get` perto da linha 939)

- [ ] **Step 1: Adicionar handler em `src/web_portal/web_portal.cpp`**

Inserir após a função `handle_rs485_body` (perto da linha 971), antes do comentário `/* --- /api/settings/mqtt --- */`:

```cpp
/* --- /api/cycles/config (v2.2.0) --- */

static void handle_cycle_settings_get(AsyncWebServerRequest *request)
{
    JsonDocument doc;
    const String s = app_settings_cycle_start_pattern();
    const String e = app_settings_cycle_end_pattern();
    doc["startPattern"] = s;
    doc["endPattern"] = e;
    doc["idleTimeoutS"] = app_settings_cycle_idle_timeout_s();
    doc["enabled"] = (s.length() > 0);
    String out;
    serializeJson(doc, out);
    request->send(200, "application/json", out);
}
```

- [ ] **Step 2: Registar route GET**

Localizar bloco de routes `/api/settings/...` (perto linha 1643 `s_srv->on("/api/settings/rs485"`). Inserir após bloco RS485, antes do `/api/settings/mqtt`:

```cpp
    /* --- /api/cycles/config (v2.2.0) --- */
    s_srv->on("/api/cycles/config", HTTP_GET, [](AsyncWebServerRequest *request) {
        if (!require_basic_auth(request)) return;
        handle_cycle_settings_get(request);
    });
```

(Manter o pattern auth dos vizinhos — copiar exactamente o estilo de `s_srv->on("/api/settings/rs485", HTTP_GET, ...)`.)

- [ ] **Step 3: Build + flash COM3**

Run:
```bash
C:/Users/Afonso/.platformio/penv/Scripts/platformio.exe run
C:/Users/Afonso/.platformio/penv/Scripts/platformio.exe run --target upload --upload-port COM3
```
Expected: SUCCESS ambos.

- [ ] **Step 4: Smoke test GET endpoint**

Aguardar device boot (até `/api/cycles/status` responder 200).

Run:
```bash
curl -s --digest -u admin:0000 --max-time 5 http://192.168.0.197/api/cycles/config
```
Expected output (JSON):
```json
{"startPattern":"OPERACAO","endPattern":"FIM CICLO","idleTimeoutS":900,"enabled":true}
```

- [ ] **Step 5: Commit**

```bash
git add src/web_portal/web_portal.cpp
git commit -m "feat(api): /api/cycles/config GET handler retorna NVS state"
```

---

### Task 5: HTTP POST /api/cycles/config handler

**Files:**
- Modify: `src/web_portal/web_portal.cpp` (adicionar handler body + route)

- [ ] **Step 1: Adicionar handler body**

Após `handle_cycle_settings_get` adicionar:

```cpp
static void handle_cycle_settings_body(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total)
{
    JsonDocument doc;
    if (!accumulate_body(data, len, index, total, doc)) {
        if (index + len < total) return; /* incompleto */
        request->send(400, "application/json", "{\"error\":\"json\"}");
        return;
    }

    /* Defaults para campos ausentes: mantém valor actual. */
    String start_s = app_settings_cycle_start_pattern();
    String end_s = app_settings_cycle_end_pattern();
    uint32_t idle_s = app_settings_cycle_idle_timeout_s();

    if (!doc["startPattern"].isNull()) {
        const char *s = doc["startPattern"] | "";
        start_s = String(s);
    }
    if (!doc["endPattern"].isNull()) {
        const char *e = doc["endPattern"] | "";
        end_s = String(e);
    }
    if (!doc["idleTimeoutS"].isNull()) {
        if (!doc["idleTimeoutS"].is<int>() && !doc["idleTimeoutS"].is<unsigned long>()) {
            request->send(400, "application/json",
                          "{\"error\":\"idleTimeoutS invalid\"}");
            return;
        }
        long v = (long)doc["idleTimeoutS"];
        if (v < 0) v = 0;
        if (v > 86400) v = 86400;
        idle_s = (uint32_t)v;
    }

    /* Persistir NVS (atomico, clamp+truncate dentro de set_cycle_config). */
    app_settings_set_cycle_config(start_s.c_str(), end_s.c_str(), idle_s);

    /* Live re-init via cycle_detector_reconfigure (fecha ACTIVE como INTERRUPTED).
     * Use os getters pos-persist para reflectir truncate/clamp do app_settings. */
    String applied_start = app_settings_cycle_start_pattern();
    String applied_end = app_settings_cycle_end_pattern();
    uint32_t applied_idle = app_settings_cycle_idle_timeout_s();
    cycle_detector_reconfigure(applied_start.c_str(), applied_end.c_str(), applied_idle);

    JsonDocument resp;
    resp["ok"] = true;
    JsonObject ap = resp["applied"].to<JsonObject>();
    ap["startPattern"] = applied_start;
    ap["endPattern"] = applied_end;
    ap["idleTimeoutS"] = applied_idle;
    ap["enabled"] = (applied_start.length() > 0);
    String out;
    serializeJson(resp, out);
    request->send(200, "application/json", out);
}
```

- [ ] **Step 2: Verificar include `cycle_detector.h` em `web_portal.cpp`**

Run: `grep -n "cycle_detector.h" src/web_portal/web_portal.cpp`
Expected: 1+ match (já existe de v2.1.0).

- [ ] **Step 3: Registar route POST**

No bloco de routes, logo após o `HTTP_GET` da cycle adicionado em Task 4:

```cpp
    s_srv->on(
        "/api/cycles/config", HTTP_POST,
        [](AsyncWebServerRequest *request) {
            if (!require_basic_auth(request)) return;
        },
        nullptr,
        [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
            handle_cycle_settings_body(request, data, len, index, total);
        });
```

(Replicar exactamente o pattern de `/api/settings/rs485` HTTP_POST nas linhas vizinhas.)

- [ ] **Step 4: Build + flash COM3**

Run:
```bash
C:/Users/Afonso/.platformio/penv/Scripts/platformio.exe run
C:/Users/Afonso/.platformio/penv/Scripts/platformio.exe run --target upload --upload-port COM3
```
Expected: SUCCESS.

- [ ] **Step 5: Smoke test POST endpoint**

Aguardar device boot.

POST com config customizada:
```bash
curl -s --digest -u admin:0000 --max-time 8 -X POST -H "Content-Type: application/json" \
  -d '{"startPattern":"INICIO","endPattern":"TERMINO","idleTimeoutS":60}' \
  http://192.168.0.197/api/cycles/config
```
Expected:
```json
{"ok":true,"applied":{"startPattern":"INICIO","endPattern":"TERMINO","idleTimeoutS":60,"enabled":true}}
```

Confirmar live apply via `/api/cycles/status`:
```bash
curl -s --digest -u admin:0000 --max-time 5 http://192.168.0.197/api/cycles/status
```
Expected JSON inclui `"start_pattern":"INICIO","end_pattern":"TERMINO","idle_timeout_s":60`.

Repor defaults:
```bash
curl -s --digest -u admin:0000 -X POST -H "Content-Type: application/json" \
  -d '{"startPattern":"OPERACAO","endPattern":"FIM CICLO","idleTimeoutS":900}' \
  http://192.168.0.197/api/cycles/config
```

- [ ] **Step 6: Commit**

```bash
git add src/web_portal/web_portal.cpp
git commit -m "feat(api): /api/cycles/config POST handler com live reconfigure"
```

---

### Task 6: UI tab "Ciclo" HTML + JS

**Files:**
- Modify: `src/web_portal/web_portal_html.h`

- [ ] **Step 1: Localizar tabs estrutura**

Run: `grep -n 'data-tab' src/web_portal/web_portal_html.h`
Expected: lista de tabs existentes (rs485, mqtt, etc).

- [ ] **Step 2: Adicionar botão tab "Ciclo"**

Encontrar bloco dos botões de tab (pattern `<button class="tab" data-tab="..."`). Adicionar APÓS o botão tab "rs485" (ou onde fizer sentido na ordem da UI):

```html
<button class="tab" data-tab="cycle">Ciclo</button>
```

- [ ] **Step 3: Adicionar div da tab content**

Encontrar bloco onde residem os `<div id="tab-rs485" class="tab-content">` etc. Adicionar após o div rs485:

```html
<div id="tab-cycle" class="tab-content">
  <h2>Detector de Ciclos</h2>
  <p class="hint">Patterns case-insensitive. Vazio em "Inicio" desactiva detector.</p>
  <label>Inicio (palavra-chave OPERACAO etc):
    <input type="text" id="cycStart" maxlength="47" autocomplete="off" />
  </label>
  <label>Fim (palavra-chave FIM CICLO etc):
    <input type="text" id="cycEnd" maxlength="47" autocomplete="off" />
  </label>
  <label>Timeout idle (segundos, 0=sem timeout, max 86400):
    <input type="number" id="cycIdle" min="0" max="86400" />
  </label>
  <button id="cycSave">Guardar</button>
  <div id="cycStatus" class="status-row"></div>
</div>
```

- [ ] **Step 4: Adicionar JS handler**

Encontrar bloco `<script>` no fim do HTML (ou onde rs485 fetch/save handler vive). Adicionar:

```javascript
async function loadCycleConfig() {
  try {
    const r = await fetch('/api/cycles/config', {credentials: 'include'});
    if (!r.ok) throw new Error('http ' + r.status);
    const j = await r.json();
    document.getElementById('cycStart').value = j.startPattern || '';
    document.getElementById('cycEnd').value = j.endPattern || '';
    document.getElementById('cycIdle').value = j.idleTimeoutS != null ? j.idleTimeoutS : 900;
    document.getElementById('cycStatus').textContent =
      j.enabled ? 'Detector activo' : 'Detector desactivado (inicio vazio)';
  } catch (e) {
    document.getElementById('cycStatus').textContent = 'erro: ' + e.message;
  }
}

document.getElementById('cycSave').addEventListener('click', async () => {
  const body = {
    startPattern: document.getElementById('cycStart').value,
    endPattern: document.getElementById('cycEnd').value,
    idleTimeoutS: parseInt(document.getElementById('cycIdle').value, 10) || 0,
  };
  try {
    const r = await fetch('/api/cycles/config', {
      method: 'POST',
      headers: {'Content-Type': 'application/json'},
      credentials: 'include',
      body: JSON.stringify(body),
    });
    const j = await r.json();
    if (!r.ok || !j.ok) throw new Error(j.error || ('http ' + r.status));
    document.getElementById('cycStatus').textContent =
      'guardado: ' + (j.applied.enabled ? 'activo' : 'desactivado') +
      ` (start="${j.applied.startPattern}" end="${j.applied.endPattern}" idle=${j.applied.idleTimeoutS}s)`;
  } catch (e) {
    document.getElementById('cycStatus').textContent = 'erro: ' + e.message;
  }
});

/* Carregar config no inicio + sempre que tab "cycle" for activada. */
document.addEventListener('DOMContentLoaded', () => {
  /* Hook tab activation: replica pattern existente se houver, senão carrega 1x. */
  loadCycleConfig();
});
```

(NOTA: Se UI ja usa pattern de "carregar tab on click", inserir `loadCycleConfig()` no handler do click da tab "cycle" em vez do DOMContentLoaded. Inspecionar o JS existente das outras tabs para replicar.)

- [ ] **Step 5: Build + flash COM3**

```bash
C:/Users/Afonso/.platformio/penv/Scripts/platformio.exe run
C:/Users/Afonso/.platformio/penv/Scripts/platformio.exe run --target upload --upload-port COM3
```

- [ ] **Step 6: Manual UI check**

Abrir `http://192.168.0.197/` em browser (login admin/0000). Verificar:
- Tab "Ciclo" aparece na lista de tabs
- Click na tab mostra 3 inputs preenchidos com defaults
- Status badge "Detector activo"
- Modificar idle para 60, guardar → status mostra "guardado: activo (start=... idle=60s)"
- Reload page → valores persistem

- [ ] **Step 7: Commit**

```bash
git add src/web_portal/web_portal_html.h
git commit -m "feat(ui): tab Ciclo p/ cycle_detector config (start/end/idle)"
```

---

### Task 7: Integration test script

**Files:**
- Create: `tools/test_cycle_config.py`

- [ ] **Step 1: Escrever script de teste**

Criar `tools/test_cycle_config.py`:

```python
#!/usr/bin/env python3
"""Integration test /api/cycles/config GET/POST + live apply.

Pre-cond: device em 192.168.0.197, admin Digest "0000".
Pos: defaults repostos (OPERACAO/FIM CICLO/900).

Uso: python tools/test_cycle_config.py
"""
import json
import sys
import time

import requests
from requests.auth import HTTPDigestAuth

HOST = "http://192.168.0.197"
AUTH = HTTPDigestAuth("admin", "0000")
CFG_URL = HOST + "/api/cycles/config"
STA_URL = HOST + "/api/cycles/status"
TIMEOUT = 8


def assert_eq(label: str, got, exp) -> None:
    if got != exp:
        print(f"FAIL [{label}]: got={got!r} expected={exp!r}", file=sys.stderr)
        sys.exit(1)
    print(f"  OK [{label}]: {got!r}")


def get_cycle_cfg() -> dict:
    r = requests.get(CFG_URL, auth=AUTH, timeout=TIMEOUT)
    r.raise_for_status()
    return r.json()


def get_status() -> dict:
    r = requests.get(STA_URL, auth=AUTH, timeout=TIMEOUT)
    r.raise_for_status()
    return r.json()


def post_cycle_cfg(start: str, end: str, idle: int) -> dict:
    body = {"startPattern": start, "endPattern": end, "idleTimeoutS": idle}
    r = requests.post(CFG_URL, json=body, auth=AUTH, timeout=TIMEOUT)
    r.raise_for_status()
    return r.json()


def main() -> int:
    print("=== Test 1: GET retorna defaults atuais ===")
    cfg = get_cycle_cfg()
    print(json.dumps(cfg, indent=2))
    # Aceitar defaults E qualquer valor previamente persistido — apenas valida shape
    assert "startPattern" in cfg
    assert "endPattern" in cfg
    assert "idleTimeoutS" in cfg
    assert "enabled" in cfg

    print("\n=== Test 2: POST custom values ===")
    resp = post_cycle_cfg("TEST_INI", "TEST_FIM", 60)
    print(json.dumps(resp, indent=2))
    assert_eq("ok", resp.get("ok"), True)
    ap = resp.get("applied", {})
    assert_eq("applied.startPattern", ap.get("startPattern"), "TEST_INI")
    assert_eq("applied.endPattern", ap.get("endPattern"), "TEST_FIM")
    assert_eq("applied.idleTimeoutS", ap.get("idleTimeoutS"), 60)
    assert_eq("applied.enabled", ap.get("enabled"), True)

    print("\n=== Test 3: status reflete novo config (live apply) ===")
    sta = get_status()
    print(json.dumps(sta, indent=2))
    assert_eq("status.start_pattern", sta.get("start_pattern"), "TEST_INI")
    assert_eq("status.end_pattern", sta.get("end_pattern"), "TEST_FIM")
    assert_eq("status.idle_timeout_s", sta.get("idle_timeout_s"), 60)
    assert_eq("status.enabled", sta.get("enabled"), True)

    print("\n=== Test 4: POST start vazio desactiva detector ===")
    resp = post_cycle_cfg("", "FIM CICLO", 900)
    assert_eq("disabled.applied.enabled", resp["applied"]["enabled"], False)
    sta = get_status()
    assert_eq("status.enabled disabled", sta.get("enabled"), False)

    print("\n=== Test 5: POST idle clamp [0,86400] ===")
    resp = post_cycle_cfg("OPERACAO", "FIM CICLO", 999999)
    assert_eq("idle clamped", resp["applied"]["idleTimeoutS"], 86400)

    print("\n=== Test 6: POST pattern truncate >47 chars ===")
    long_str = "X" * 60
    resp = post_cycle_cfg(long_str, "FIM CICLO", 900)
    truncated = resp["applied"]["startPattern"]
    assert_eq("truncate len", len(truncated), 47)

    print("\n=== Reset to defaults ===")
    post_cycle_cfg("OPERACAO", "FIM CICLO", 900)
    print("  defaults restored")

    print("\nALL TESTS PASSED")
    return 0


if __name__ == "__main__":
    sys.exit(main())
```

- [ ] **Step 2: Correr smoke test**

Run:
```bash
C:/Users/Afonso/.platformio/penv/Scripts/python.exe tools/test_cycle_config.py
```
Expected: `ALL TESTS PASSED` no fim, todos os asserts OK.

- [ ] **Step 3: Commit script**

```bash
git add tools/test_cycle_config.py
git commit -m "test(cycles): script integration tests /api/cycles/config"
```

---

### Task 8: Version bump v2.12 → v2.20 + final build

**Files:**
- Modify: `platformio.ini`

- [ ] **Step 1: Bump version**

Em `platformio.ini` linha 52:

```
    '-DFITADIGITAL_VERSION="2.12"'
```

Substituir por:

```
    '-DFITADIGITAL_VERSION="2.20"'
```

- [ ] **Step 2: Build final + flash**

```bash
C:/Users/Afonso/.platformio/penv/Scripts/platformio.exe run
C:/Users/Afonso/.platformio/penv/Scripts/platformio.exe run --target upload --upload-port COM3
```
Expected: `firmware_versions/FitaDigital_v2.20.bin` criado, flash SUCCESS.

- [ ] **Step 3: Persistence test (cross-reboot)**

Aguardar boot. Confirmar:

```bash
curl -s --digest -u admin:0000 --max-time 5 http://192.168.0.197/api/cycles/config
```
Expected JSON com defaults (OPERACAO / FIM CICLO / 900) — NVS preserva entre reboots/OTA.

POST custom + reboot via API + check pos-reboot:
```bash
curl -s --digest -u admin:0000 -X POST -H "Content-Type: application/json" \
  -d '{"startPattern":"PERSIST_TEST","endPattern":"FIM CICLO","idleTimeoutS":120}' \
  http://192.168.0.197/api/cycles/config

curl -s --digest -u admin:0000 -X POST http://192.168.0.197/api/system/reboot
```

Aguardar reboot (~15s):

```bash
until curl -s --digest -u admin:0000 --max-time 3 http://192.168.0.197/api/cycles/config -o /tmp/c.json -w "%{http_code}\n" 2>/dev/null | grep -q 200; do sleep 2; done
cat /tmp/c.json
```
Expected: `startPattern:"PERSIST_TEST"`, `idleTimeoutS:120` — config persistiu NVS.

Repor defaults:
```bash
curl -s --digest -u admin:0000 -X POST -H "Content-Type: application/json" \
  -d '{"startPattern":"OPERACAO","endPattern":"FIM CICLO","idleTimeoutS":900}' \
  http://192.168.0.197/api/cycles/config
```

- [ ] **Step 4: Commit bump**

```bash
git add platformio.ini
git commit -m "chore: bump FITADIGITAL_VERSION 2.12 -> 2.20"
```

---

### Task 9: Smoke E2E + soak 30min revalidação

**Files:**
- Run: `tools/rs485_smoke_cycle.py` + `tools/soak_cycle_endpoint.py`

- [ ] **Step 1: Smoke test cycle_detector end-to-end (defaults)**

Replug COM8 se necessário. Run:
```bash
C:/Users/Afonso/.platformio/penv/Scripts/python.exe tools/rs485_smoke_cycle.py --port COM8 --baud 9600 --dummy 3 --interval 1.0
```
Expected output: 5 linhas TX (1 OPERACAO + 3 DADO + 1 FIM CICLO).

Check /api/cycles/status → state IDLE pos-execução. Check /api/cycles/list → nova entry DONE com duration ~6s, lines=5.

- [ ] **Step 2: Reconfigure mid-test**

POST novo pattern via API:
```bash
curl -s --digest -u admin:0000 -X POST -H "Content-Type: application/json" \
  -d '{"startPattern":"INICIO_CICLO","endPattern":"FIM_CICLO","idleTimeoutS":900}' \
  http://192.168.0.197/api/cycles/config
```

Smoke com novos patterns:
```bash
C:/Users/Afonso/.platformio/penv/Scripts/python.exe tools/rs485_smoke_cycle.py \
  --port COM8 --baud 9600 --dummy 2 \
  --start "INICIO_CICLO equipamento autoclave" \
  --end "FIM_CICLO duracao 30min"
```
Expected: cycle_detector deteta + escreve nova NDJSON entry.

- [ ] **Step 3: Soak 30min**

Reset defaults + replug COM8:
```bash
curl -s --digest -u admin:0000 -X POST -H "Content-Type: application/json" \
  -d '{"startPattern":"OPERACAO","endPattern":"FIM CICLO","idleTimeoutS":900}' \
  http://192.168.0.197/api/cycles/config
```

Arrancar COM3 capture + soak:
```bash
C:/Users/Afonso/.platformio/penv/Scripts/python.exe tools/com3_log.py logs/session-v220-soak.log 115200 COM3 &
sleep 3
C:/Users/Afonso/.platformio/penv/Scripts/python.exe tools/soak_cycle_endpoint.py --duration 1800 --cycle-interval 100 --log logs/soak_v220.log
```
Expected (final stats):
- cycles_sent ≥ 17 (30min / 100s ≈ 18)
- list_200 == list_hits (100%)
- list_err == 0
- active_seen == cycles_sent
- ndjson_max_bytes cresce linear
- COM3 log sem panic markers

- [ ] **Step 4: Confirmar zero panics**

Run:
```bash
grep -E "Guru Meditation|abort|Backtrace|StoreProhibited|HEAP_GUARD|panic" logs/session-v220-soak.log | head
```
Expected: zero matches (apenas `rst:0x15 USB_UART_CHIP_RESET` no boot inicial).

- [ ] **Step 5: Tag + push**

Update TODO.md (mover Parser config para Feito com data + stats), depois:

```bash
git add TODO.md
git commit -m "docs(todo): v2.2.0 Parser config NVS+UI SHIPPED + soak 30min PASS"
git tag -a v2.2.0 -m "v2.2.0: cycle_detector config NVS+UI"
git push origin main
git push origin v2.2.0
```

---

## Self-Review Notes

**Spec coverage check (all sections mapped to tasks):**
- `app_settings` getters/setters → Task 1
- `cycle_detector_reconfigure` → Task 2
- Boot wiring → Task 3
- GET /api/cycles/config → Task 4
- POST /api/cycles/config → Task 5
- UI tab "Ciclo" → Task 6
- Integration tests + persistence → Tasks 7 & 8
- Smoke + soak validation criteria → Task 9

**Out of scope (per spec):** NVS schema versioning, multi-language UI, multi-config profiles, WebSocket events, SD `/fdigi.cfg` mirror for cycle keys (NVS only sufficient; export/import endpoint can be extended later if needed).

**Risks / mitigations:**
- COM8 USB-RS485 adapter trava entre invocacoes Python → use `rs485_smoke_cycle.py` (single open) + replug fisico antes de cada bateria.
- Live reconfigure mid-ACTIVE cycle: emits INTERRUPTED entry in NDJSON (test in Task 9 step 2 valida).
- Watchdog RTC baseline auto-update porque `cycle_detector_init` (chamado de `cycle_detector_reconfigure`) já actualiza `s_baseline` + magic.
