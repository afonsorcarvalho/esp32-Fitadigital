# v2.2.0 — Parser config NVS+UI (cycle_detector)

**Date:** 2026-05-19
**Target version:** 2.2.0
**Base:** main HEAD `50d785e` (v2.1.2 production-ready)

## Goal

Tornar `cycle_detector` configurável via UI web (start_pattern, end_pattern, idle_timeout_s)
e persistir em NVS. Hoje hardcoded em `app.cpp:245`:

```cpp
cycle_detector_init("OPERACAO", "FIM CICLO", 900U);
```

Diferentes equipamentos hospitalares emitem keywords diferentes (autoclaves,
lavadoras, esterilizadores). Cada deploy deve poder ajustar sem rebuild firmware.

## Non-goals

- NVS schema migration / versioning (1ª vez = lê NVS keys ausentes → defaults atuais)
- UI multi-language (mantém PT como resto do portal)
- Multiple parser profiles (uma config global; futura v2.3+ pode introduzir profiles)
- WebSocket push de eventos cycle (separado, é tarefa "UI events" pendente)

## Decisões (brainstorm 2026-05-19)

| Decisão | Valor |
|---------|-------|
| UI placement | Tab dedicada "Ciclo" nível topo |
| Defaults 1ª vez | "OPERACAO" / "FIM CICLO" / 900 (zero-surprise para devices deployed) |
| Apply mode | Live re-init pós-POST; cycle ACTIVE emite INTERRUPTED + NDJSON antes |
| Pattern length | 0-47 chars (cycle_detector kPatternMax=48 com NUL) |
| idle_timeout range | 0-86400s (0 = sem timeout, 24h max) |
| Detector OFF | `start_pattern==""` desactiva (reaproveita logica `cycle_detector_init`) |

## Architecture

```
┌────────────────────────────────────────────────────────┐
│  UI tab "Ciclo" (web_portal_html.h)                    │
│  3 inputs + save button + status badge (enabled)       │
└────────────────────────┬───────────────────────────────┘
                         │ POST /api/cycles/config
                         ▼
┌────────────────────────────────────────────────────────┐
│  handle_cycle_settings_body (web_portal.cpp)           │
│  validar → app_settings_set_cycle_config → reconfigure │
└──────────┬──────────────────────────────┬──────────────┘
           │ NVS persist                  │ live apply
           ▼                              ▼
┌──────────────────────┐    ┌──────────────────────────────┐
│ app_settings (NVS)   │    │ cycle_detector_reconfigure() │
│ keys: cyc_st cyc_en  │    │ if ACTIVE → emit INTERRUPTED │
│       cyc_to         │    │ then init + update RTC base  │
└──────────────────────┘    └──────────────────────────────┘
           ▲
           │ boot read
           │
┌──────────────────────┐
│ app.cpp setup()      │
│ read settings → init │
└──────────────────────┘
```

## Components

### 1. `app_settings` (NVS storage)

**New API (`src/app_settings.h/cpp`):**

```cpp
/** Pattern de inicio (substring case-insensitive). Default "OPERACAO".
 *  String vazia desactiva detector. Max 47 chars. */
String app_settings_cycle_start_pattern(void);

/** Pattern de fim. Default "FIM CICLO". Max 47 chars. */
String app_settings_cycle_end_pattern(void);

/** Idle timeout em segundos. 0 = sem timeout. Default 900 (15min). Clamp 0-86400. */
uint32_t app_settings_cycle_idle_timeout_s(void);

/** Persiste todos os 3 atomicamente. Trunca patterns a 47 chars,
 *  clampa idle_s a [0, 86400]. */
void app_settings_set_cycle_config(const char *start, const char *end, uint32_t idle_s);
```

**NVS keys** (Preferences lib, max 15 chars cada):
- `cyc_st` — start_pattern (String, max 48 bytes incluindo NUL)
- `cyc_en` — end_pattern
- `cyc_to` — idle_timeout_s (uint32_t)

**Defaults logic:** getter retorna NVS value se existe; senão retorna constante hardcoded
defaults (`"OPERACAO"`, `"FIM CICLO"`, `900`).

### 2. `cycle_detector` (graceful reconfigure)

**New API (`src/cycle_detector.h`):**

```cpp
/** Re-aplica config em runtime. Se state==ACTIVE, fecha ciclo como
 *  CYCLE_STATUS_INTERRUPTED e escreve NDJSON entry antes de re-init.
 *  Atualiza RTC baseline do watchdog. Thread-safe. */
void cycle_detector_reconfigure(const char *start_pattern,
                                const char *end_pattern,
                                uint32_t idle_timeout_s);
```

**Implementation:**
```cpp
void cycle_detector_reconfigure(const char *start, const char *end, uint32_t idle_s) {
  Current snap;
  bool emit_interrupted = false;
  {
    Lock lk;
    if (s_cur.state == State::ACTIVE) {
      snap = s_cur;
      emit_interrupted = true;
    }
  }  /* lock released */
  if (emit_interrupted) {
    emit_cycle_close(snap, CYCLE_STATUS_INTERRUPTED);
  }
  cycle_detector_init(start, end, idle_s);  /* já atualiza s_baseline + magic */
}
```

### 3. Boot wiring (`src/app.cpp` line ~245)

```cpp
if (sd_ok) {
    cycles_rs485_init();
    /* v2.2.0: read parser config from NVS (defaults baked in app_settings). */
    const String cyc_start = app_settings_cycle_start_pattern();
    const String cyc_end   = app_settings_cycle_end_pattern();
    cycle_detector_init(cyc_start.c_str(), cyc_end.c_str(),
                        app_settings_cycle_idle_timeout_s());
}
```

### 4. HTTP endpoint (`src/web_portal/web_portal.cpp`)

**GET `/api/cycles/config`:**
```json
{
  "startPattern": "OPERACAO",
  "endPattern": "FIM CICLO",
  "idleTimeoutS": 900,
  "enabled": true
}
```
`enabled` derivado de `start_pattern != ""` (read-only no GET, conveniência UI).

**POST `/api/cycles/config`:**
```json
{"startPattern": "INICIO", "endPattern": "TERMINO", "idleTimeoutS": 600}
```

Server-side handler:
1. Parse JSON body (existing `accumulate_body` helper).
2. Trim + truncate patterns a 47 chars.
3. Clamp `idleTimeoutS` a [0, 86400].
4. `app_settings_set_cycle_config(start, end, idle)` → NVS.
5. `cycle_detector_reconfigure(start, end, idle)` → live apply.
6. Response 200: `{"ok":true,"applied":{"startPattern":"...","endPattern":"...","idleTimeoutS":N,"enabled":bool}}`.

**Error responses:**
- 400 JSON inválido → `{"error":"json"}`
- 400 idleTimeoutS não-numérico ou negativo → `{"error":"idleTimeoutS invalid"}`

(Patterns sempre aceitos, truncados silenciosamente como `set_rs485` faz com baud.)

**Route registration** (junto com outras `/api/settings/*`):
```cpp
s_srv->on("/api/cycles/config", HTTP_GET, handle_cycle_settings_get);
s_srv->on("/api/cycles/config", HTTP_POST, ..., handle_cycle_settings_body, ...);
```

### 5. UI — tab nova "Ciclo" (`src/web_portal/web_portal_html.h`)

**Tab structure** (replicar pattern existente RS485/MQTT):
```html
<button class="tab" data-tab="cycle">Ciclo</button>
...
<div id="tab-cycle" class="tab-content">
  <h2>Detector de Ciclos</h2>
  <p class="hint">Patterns case-insensitive. Vazio em "Inicio" desactiva detector.</p>
  <label>Inicio (palavra-chave OPERACAO etc):
    <input type="text" id="cycStart" maxlength="47" />
  </label>
  <label>Fim (palavra-chave FIM CICLO etc):
    <input type="text" id="cycEnd" maxlength="47" />
  </label>
  <label>Timeout idle (segundos, 0=sem timeout, max 86400):
    <input type="number" id="cycIdle" min="0" max="86400" />
  </label>
  <button id="cycSave">Guardar</button>
  <div id="cycStatus" class="status-row"></div>
</div>
```

**JS handler:**
- onload: `fetch('/api/cycles/config')` → popular inputs + mostrar enabled badge
- onclick save: POST com 3 valores, mostrar applied result + auto-refresh status

### Data flow

1. **Boot**: app.cpp lê NVS via app_settings, passa a cycle_detector_init.
2. **UI GET**: AJAX → web_portal lê via app_settings getters → JSON.
3. **UI POST**: AJAX → web_portal valida → app_settings.set + cycle_detector_reconfigure (live).
4. **Reconfigure ACTIVE**: cycle fecha INTERRUPTED → NDJSON entry escrita → init aplica novos patterns.

### Error handling

| Path | Failure | Behavior |
|------|---------|----------|
| Boot read NVS | Keys ausentes | Defaults hardcoded ("OPERACAO"/"FIM CICLO"/900) |
| Boot read NVS | Corrupção (impossível na Preferences API mas defensive) | Fallback defaults |
| POST | JSON malformed | 400 + error msg |
| POST | idleTimeoutS string ou ausente | Mantém valor anterior |
| POST | NVS write fail | Apenas log; continue com live apply (in-RAM ok) |
| Reconfigure ACTIVE | sd_access falha em emit_cycle_close | NDJSON entry perdida, init segue. (mesmo comportamento que existing FIM CICLO path) |

### Testing

**Unit / smoke:**
1. **NVS roundtrip**: setter → reboot → getter retorna mesmo valor.
2. **Defaults vazia**: erase NVS → getter retorna "OPERACAO" / "FIM CICLO" / 900.
3. **Clamp idle**: setter com idle=-1 ou 99999 → getter retorna 0 ou 86400.
4. **Truncate pattern**: setter com 60-char string → getter retorna 47 chars.

**Integration (post-flash):**
1. GET `/api/cycles/config` → JSON com defaults.
2. POST `/api/cycles/config` com `{"startPattern":"START","endPattern":"END","idleTimeoutS":60}` → 200 + applied.
3. GET `/api/cycles/status` → patterns atualizados.
4. RS485 envia "START blah" → state ACTIVE; "END" → DONE NDJSON.
5. Reboot → patterns persistem.

**Edge:**
6. POST com start vazio → enabled=false; `/api/cycles/status` confirma.
7. Live reconfigure ACTIVE → ciclo prévio fecha INTERRUPTED em NDJSON.

## Out of scope / Future work

- **NVS settings export/import** (já existe `/api/system/export` que cobre todos os settings — cycle config beneficia gratis).
- **Per-equipment profiles** (multi-config) — adiar para v2.3+ se demand surgir.
- **UI dropdown com presets** (autoclave/lavadora etc) — adiar; user pode digitar manualmente.

## Files modified

| File | Change |
|------|--------|
| `src/app_settings.h` | +4 declarations |
| `src/app_settings.cpp` | +getter/setter implementations + NVS keys |
| `src/cycle_detector.h` | +1 declaration (`cycle_detector_reconfigure`) |
| `src/cycle_detector.cpp` | +1 function |
| `src/app.cpp` | line ~245: substituir hardcoded por reads de app_settings |
| `src/web_portal/web_portal.cpp` | +handle_cycle_settings_{get,body} + route registration |
| `src/web_portal/web_portal_html.h` | +tab "Ciclo" HTML + JS handler |
| `platformio.ini` | bump FITADIGITAL_VERSION 2.12 → 2.20 |

## Validation criteria

- [ ] Build success (RAM/Flash dentro budget)
- [ ] GET/POST `/api/cycles/config` happy path
- [ ] NVS persistence cross-reboot
- [ ] Live reconfigure: ACTIVE → INTERRUPTED transition + NDJSON
- [ ] Empty start → enabled=false
- [ ] Idle clamp [0, 86400]
- [ ] Pattern truncate 47 chars
- [ ] Soak 30min com 1 reconfigure mid-soak: NDJSON íntegro (entries antes config_change + entries depois)
- [ ] Zero panic durante soak (sanity check v2.1.2 fixes hold sob change)
