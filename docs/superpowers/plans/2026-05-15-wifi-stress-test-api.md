# WiFi Stress Test API Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add `POST /api/wifi/stress?down_s=N` endpoint to v1.83 firmware that simulates WiFi outages (disable autoReconnect + esp_wifi_disconnect + sleep N), plus `tools/wifi_stress.py` orchestrator that drives 6 cycles (3 SOFT + 3 HARD) and reports recovery metrics.

**Architecture:** Stateless HTTP endpoint spawns one-shot FreeRTOS task that holds disconnect for N seconds, then re-arms autoReconnect and exits. Self-healing layered escalation in `wg_keepalive_task` (v1.82) recovers naturally — endpoint does not call `WiFi.begin()`. External Python script polls `/api/health` to measure recovery; pulls `fdigi.log` via FTP to validate self-healing log markers.

**Tech Stack:** ESP-IDF + Arduino-ESP32, ESPAsyncWebServer (handlers), FreeRTOS (xTaskCreate), Python 3 + `requests` (orchestrator).

**Spec:** `docs/superpowers/specs/2026-05-15-wifi-stress-test-api-design.md`

---

## File Structure

| File | Action | Responsibility |
|---|---|---|
| `platformio.ini` | Modify L52 | Bump `FITADIGITAL_VERSION` 1.82→1.83 |
| `src/web_portal/web_portal.cpp` | Modify | Add `wifi_stress_task` + `handle_wifi_stress_post` + route registration |
| `tools/wifi_stress.py` | Create | Orchestrator: 6 cycles, CSV output, summary |
| `TODO.md` | Modify | Add "Em curso" entry; move to "Feito" at end |

---

## Task 1: Bump version + add TODO entry

**Files:**
- Modify: `platformio.ini:52`
- Modify: `TODO.md` (append "Em curso")

- [ ] **Step 1: Bump FITADIGITAL_VERSION**

Change `platformio.ini:52` from:
```
    '-DFITADIGITAL_VERSION="1.82"'
```
To:
```
    '-DFITADIGITAL_VERSION="1.83"'
```

- [ ] **Step 2: Add TODO entry under "Em curso"**

Insert at top of `## Em curso` section in `TODO.md`:
```markdown
- **WiFi stress test API v1.83** — endpoint `POST /api/wifi/stress?down_s=N` + `tools/wifi_stress.py` orchestrator (6 ciclos: 3 SOFT 35s + 3 HARD 305s). Valida self-healing layered v1.82 (`wifi_keepalive_tick` SOFT/HARD). Spec: `docs/superpowers/specs/2026-05-15-wifi-stress-test-api-design.md`. Plan: `docs/superpowers/plans/2026-05-15-wifi-stress-test-api.md`.
```

- [ ] **Step 3: Commit**

```powershell
git add platformio.ini TODO.md docs/superpowers/specs/2026-05-15-wifi-stress-test-api-design.md docs/superpowers/plans/2026-05-15-wifi-stress-test-api.md
git commit -m "chore(v1.83): bump version + spec/plan WiFi stress API"
```

---

## Task 2: Add wifi_stress_task + handler skeleton in web_portal.cpp

**Files:**
- Modify: `src/web_portal/web_portal.cpp` (locate near other static handler functions — search for `handle_health_get`)

- [ ] **Step 1: Add includes if missing**

At top of `src/web_portal/web_portal.cpp`, after existing includes, ensure these are present (only add lines that are not already there):
```cpp
#include <WiFi.h>
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
```
Verify with Grep before adding.

- [ ] **Step 2: Add `wifi_stress_task` static function**

Insert immediately above `handle_health_get` (around line 854):
```cpp
/* v1.83 WiFi stress test — task one-shot. Disable autoReconnect, force
 * esp_wifi_disconnect, sleep down_s segundos para wifi_keepalive_tick (v1.82)
 * disparar SOFT (>=30s) ou HARD (>=300s). Re-arma autoReconnect no fim.
 * NAO chama WiFi.begin() — deixa self-healing recover naturalmente. */
static void wifi_stress_task(void *arg)
{
    uint32_t down_s = *(uint32_t *)arg;
    free(arg);
    app_log_feature_writef("WARN", "WIFI",
        "Stress disconnect down_s=%u (auto-reconnect off)",
        (unsigned)down_s);
    WiFi.setAutoReconnect(false);
    (void)esp_wifi_disconnect();
    vTaskDelay(pdMS_TO_TICKS(down_s * 1000UL));
    WiFi.setAutoReconnect(true);
    app_log_feature_writef("INFO", "WIFI",
        "Stress window done (down_s=%u) — self-healing tomara conta",
        (unsigned)down_s);
    vTaskDelete(NULL);
}
```

- [ ] **Step 3: Add `handle_wifi_stress_post` static function**

Insert immediately after `wifi_stress_task`:
```cpp
static void handle_wifi_stress_post(AsyncWebServerRequest *request)
{
    if (!request->hasParam("down_s")) {
        request->send(400, "application/json",
            "{\"error\":\"missing down_s param\"}");
        return;
    }
    long down_s = request->getParam("down_s")->value().toInt();
    if (down_s < 5 || down_s > 600) {
        request->send(400, "application/json",
            "{\"error\":\"down_s out of range (5..600)\"}");
        return;
    }
    uint32_t *arg = (uint32_t *)malloc(sizeof(uint32_t));
    if (arg == NULL) {
        request->send(500, "application/json",
            "{\"error\":\"alloc failed\"}");
        return;
    }
    *arg = (uint32_t)down_s;
    BaseType_t ok = xTaskCreatePinnedToCore(wifi_stress_task,
        "wifi_stress", 4096, arg, 1, NULL, 1);
    if (ok != pdPASS) {
        free(arg);
        request->send(500, "application/json",
            "{\"error\":\"task spawn failed\"}");
        return;
    }
    char buf[96];
    snprintf(buf, sizeof(buf),
        "{\"scheduled_down_s\":%ld,\"task_started\":true}", down_s);
    request->send(200, "application/json", buf);
}
```

- [ ] **Step 4: Build to verify compiles (no route yet)**

```powershell
& "C:/Users/Afonso/.platformio/penv/Scripts/pio.exe" run --environment esp32-s3-touch-lcd-4_3b
```
Expected: `[SUCCESS]` line. If errors → fix includes / typos and re-run.

- [ ] **Step 5: Commit**

```powershell
git add src/web_portal/web_portal.cpp
git commit -m "feat(wifi): wifi_stress_task + handler skeleton (v1.83)"
```

---

## Task 3: Register `/api/wifi/stress` route

**Files:**
- Modify: `src/web_portal/web_portal.cpp` (in `web_portal_start()` near other route registrations — locate by Grep `s_srv->on("/api/health"`)

- [ ] **Step 1: Add route registration**

In `web_portal_start()`, immediately after the `/api/health` route block (around line 1334), insert:
```cpp
    /* --- /api/wifi/stress (auth) — v1.83 stress test self-healing --- */
    s_srv->on("/api/wifi/stress", HTTP_POST, [](AsyncWebServerRequest *request) {
        if (!web_auth_check(request)) return;
        handle_wifi_stress_post(request);
    });
```

- [ ] **Step 2: Build**

```powershell
& "C:/Users/Afonso/.platformio/penv/Scripts/pio.exe" run --environment esp32-s3-touch-lcd-4_3b
```
Expected: `[SUCCESS]`. RAM/Flash deltas reasonable (<1KB).

- [ ] **Step 3: Commit**

```powershell
git add src/web_portal/web_portal.cpp
git commit -m "feat(wifi): route POST /api/wifi/stress (v1.83)"
```

---

## Task 4: Flash v1.83 to device

**Files:** none modified (binary only)

- [ ] **Step 1: Upload firmware**

```powershell
& "C:/Users/Afonso/.platformio/penv/Scripts/pio.exe" run --environment esp32-s3-touch-lcd-4_3b --target upload --upload-port COM3
```
Expected: `Hash of data verified` + `Hard resetting via RTS pin`.

- [ ] **Step 2: Verify device alive + new version up**

Wait ~10s for boot, then:
```powershell
$r = curl.exe -s --max-time 5 http://192.168.0.197/api/health
Write-Host "Health: $r"
```
Expected: `{"online":true,"uptime_s":<small>}` (uptime <60s = fresh boot).

Pull boot.log to confirm:
```powershell
curl.exe -s --max-time 30 -u esp32:esp32 ftp://192.168.0.197/boot.log -o ".claude/boot_v183.log"
Get-Content ".claude/boot_v183.log"
```
Expected: contains `BOOT START` line; new boot timestamp.

---

## Task 5: Smoke test endpoint (1 short cycle, manual)

**Files:** none

- [ ] **Step 1: Trigger 10s stress (smallest above kSoftThresholdMs is 30s, but 10s validates endpoint plumbing without forcing self-healing)**

Wait — sanity test: 10s does NOT exceed T1=30s, so self-healing won't fire. We just want to confirm endpoint accepts and task runs. Use `down_s=10` for quickest validation:
```powershell
$r = curl.exe -s -u esp32:esp32 -X POST "http://192.168.0.197/api/wifi/stress?down_s=10"
Write-Host "Response: $r"
```
Expected: `{"scheduled_down_s":10,"task_started":true}` returned within 1s.

- [ ] **Step 2: Confirm WiFi went down then recovered**

Immediately poll:
```powershell
1..20 | ForEach-Object {
    $h = curl.exe -s --max-time 3 http://192.168.0.197/api/health 2>$null
    Write-Host "[$_] $h"
    Start-Sleep -Seconds 2
}
```
Expected: ~5 polls fail/timeout (during 10s window), then recovery; uptime monotonic (no reboot).

- [ ] **Step 3: Verify log markers**

```powershell
curl.exe -s --max-time 60 -u esp32:esp32 ftp://192.168.0.197/fdigi.log -o ".claude/fdigi_smoke.log"
Select-String -Path ".claude/fdigi_smoke.log" -Pattern "Stress disconnect|Stress window done|WiFi DOWN" | Select-Object -Last 10
```
Expected: 3 entries (`Stress disconnect down_s=10`, `WiFi DOWN detectado`, `Stress window done`). NO `Soft reconnect` (window <30s).

- [ ] **Step 4: If smoke OK, commit nothing — proceed to Task 6**

Smoke is validation-only. If FAIL → debug before continuing.

---

## Task 6: Create `tools/wifi_stress.py`

**Files:**
- Create: `tools/wifi_stress.py`

- [ ] **Step 1: Write script**

Create `tools/wifi_stress.py` with this exact content:
```python
"""WiFi stress test orchestrator — v1.83.

Drives N ciclos de POST /api/wifi/stress alternating SOFT (35s) + HARD (305s),
mede recovery via /api/health polling, escreve CSV.

Uso:
  python wifi_stress.py [--ip 192.168.0.197] [--user esp32] [--pwd esp32]
                        [--cycles 3] [--out logs/wifi_stress_<ts>.csv]
                        [--soft-s 35] [--hard-s 305] [--gap-s 60]
"""
import argparse
import csv
import datetime as dt
import sys
import time

import requests
from requests.auth import HTTPBasicAuth


def get_health(base, auth, timeout=5):
    try:
        r = requests.get(f"{base}/api/health", timeout=timeout)
        if r.status_code == 200:
            return r.json()
    except requests.RequestException:
        pass
    return None


def trigger_stress(base, auth, down_s, timeout=5):
    r = requests.post(
        f"{base}/api/wifi/stress",
        params={"down_s": down_s},
        auth=auth,
        timeout=timeout,
    )
    r.raise_for_status()
    return r.json()


def wait_recovery(base, auth, deadline_s, poll_interval_s=5):
    """Poll /api/health until 200 OR deadline. Return (recovered, elapsed_s, health_dict)."""
    t0 = time.monotonic()
    while time.monotonic() - t0 < deadline_s:
        h = get_health(base, auth)
        if h is not None:
            return True, time.monotonic() - t0, h
        time.sleep(poll_interval_s)
    return False, time.monotonic() - t0, None


def run_cycle(idx, label, down_s, base, auth, gap_s):
    print(f"[cycle {idx} {label} down_s={down_s}] start")
    h0 = get_health(base, auth)
    uptime_before = h0["uptime_s"] if h0 else None
    if uptime_before is None:
        print(f"[cycle {idx}] device unreachable BEFORE — abort")
        return None
    disconnect_iso = dt.datetime.now().isoformat(timespec="seconds")
    try:
        trigger_stress(base, auth, down_s)
    except requests.RequestException as e:
        print(f"[cycle {idx}] trigger failed: {e}")
        return None
    print(f"[cycle {idx}] sleeping window {down_s}s ...")
    time.sleep(down_s + 5)
    print(f"[cycle {idx}] polling recovery (max 300s) ...")
    recovered, recovery_s, h1 = wait_recovery(base, auth, deadline_s=300)
    uptime_after = h1["uptime_s"] if h1 else None
    expected_after = uptime_before + down_s + recovery_s if uptime_before else None
    reboot = (
        recovered
        and uptime_after is not None
        and expected_after is not None
        and uptime_after < expected_after - 30
    )
    pass_fail = "PASS" if (recovered and not reboot and recovery_s < 60) else "FAIL"
    print(
        f"[cycle {idx} {label}] {pass_fail} recovery_s={recovery_s:.1f} "
        f"uptime {uptime_before}->{uptime_after} reboot={reboot}"
    )
    print(f"[cycle {idx}] gap {gap_s}s ...")
    time.sleep(gap_s)
    return {
        "cycle": idx,
        "label": label,
        "down_s": down_s,
        "disconnect_at_iso": disconnect_iso,
        "recovery_s": round(recovery_s, 1),
        "uptime_before": uptime_before,
        "uptime_after": uptime_after,
        "reboot_detected": reboot,
        "result": pass_fail,
    }


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--ip", default="192.168.0.197")
    ap.add_argument("--user", default="esp32")
    ap.add_argument("--pwd", default="esp32")
    ap.add_argument("--cycles", type=int, default=3)
    ap.add_argument("--soft-s", type=int, default=35)
    ap.add_argument("--hard-s", type=int, default=305)
    ap.add_argument("--gap-s", type=int, default=60)
    ts = dt.datetime.now().strftime("%Y%m%d-%H%M%S")
    ap.add_argument("--out", default=f"logs/wifi_stress_{ts}.csv")
    args = ap.parse_args()

    base = f"http://{args.ip}"
    auth = HTTPBasicAuth(args.user, args.pwd)
    plan = []
    for i in range(args.cycles):
        plan.append(("SOFT", args.soft_s))
        plan.append(("HARD", args.hard_s))

    print(f"Plan: {len(plan)} cycles, target {args.ip}, out {args.out}")
    h = get_health(base, auth)
    if h is None:
        print("Device unreachable at start — abort")
        sys.exit(1)
    print(f"Initial uptime: {h['uptime_s']}s")

    rows = []
    for idx, (label, down_s) in enumerate(plan, start=1):
        row = run_cycle(idx, label, down_s, base, auth, args.gap_s)
        if row is None:
            print(f"[cycle {idx}] aborting remaining cycles")
            break
        rows.append(row)

    import os
    os.makedirs(os.path.dirname(args.out), exist_ok=True)
    with open(args.out, "w", newline="", encoding="utf-8") as f:
        w = csv.DictWriter(f, fieldnames=list(rows[0].keys()) if rows else
                           ["cycle","label","down_s","disconnect_at_iso",
                            "recovery_s","uptime_before","uptime_after",
                            "reboot_detected","result"])
        w.writeheader()
        for r in rows:
            w.writerow(r)

    pass_n = sum(1 for r in rows if r["result"] == "PASS")
    fail_n = len(rows) - pass_n
    reboots = sum(1 for r in rows if r["reboot_detected"])
    max_recov = max((r["recovery_s"] for r in rows), default=0)
    print("=" * 50)
    print(f"SUMMARY: {pass_n}/{len(rows)} PASS, {fail_n} FAIL, "
          f"{reboots} reboots, max_recovery={max_recov}s")
    print(f"CSV: {args.out}")
    sys.exit(0 if fail_n == 0 and reboots == 0 else 1)


if __name__ == "__main__":
    main()
```

- [ ] **Step 2: Verify Python deps**

```powershell
& "C:/Users/Afonso/.platformio/penv/Scripts/python.exe" -c "import requests; print(requests.__version__)"
```
Expected: prints version. If ImportError → `pip install requests` first.

- [ ] **Step 3: Commit**

```powershell
git add tools/wifi_stress.py
git commit -m "feat(tools): wifi_stress.py orchestrator (v1.83)"
```

---

## Task 7: Run full stress test (3 cycles SOFT + 3 HARD)

**Files:**
- Output: `logs/wifi_stress_<timestamp>.csv`

- [ ] **Step 1: Confirm baseline**

```powershell
curl.exe -s --max-time 5 http://192.168.0.197/api/health
```
Expected: device responding, uptime stable (no recent unexpected reboot).

- [ ] **Step 2: Launch stress (run in foreground — script ~25min)**

Use Bash `run_in_background: true` so the agent can monitor:
```powershell
& "C:/Users/Afonso/.platformio/penv/Scripts/python.exe" tools/wifi_stress.py --cycles 3
```
Expected duration: ~25min (3 SOFT@35s + 3 HARD@305s + 60s gaps + recovery time).

While running, the script prints per-cycle PASS/FAIL.

- [ ] **Step 3: Pull fdigi.log post-run**

```powershell
curl.exe -s --max-time 180 -u esp32:esp32 ftp://192.168.0.197/fdigi.log -o ".claude/fdigi_post_stress.log"
```

- [ ] **Step 4: Verify self-healing log markers per cycle**

```powershell
Select-String -Path ".claude/fdigi_post_stress.log" -Pattern "Stress disconnect|WiFi DOWN|Soft reconnect|Hard reset stack|Voltou apos" | Select-Object -Last 50
```
Expected (per cycle):
- SOFT cycle: `Stress disconnect down_s=35` → `WiFi DOWN detectado` → `Soft reconnect apos 30 s` → `Voltou apos X s`
- HARD cycle: `Stress disconnect down_s=305` → `WiFi DOWN detectado` → `Soft reconnect apos 30 s` → `Hard reset stack apos 300 s` → `Voltou apos X s`

- [ ] **Step 5: Inspect CSV**

```powershell
Get-Content (Get-ChildItem logs/wifi_stress_*.csv | Sort LastWriteTime -Desc | Select -First 1).FullName
```
Validate: 6 rows, all `result=PASS`, `reboot_detected=False`, `recovery_s < 60`.

---

## Task 8: Finalize — TODO + commit + archive

**Files:**
- Modify: `TODO.md`
- Add: `logs/wifi_stress_*.csv` to git
- Verify: `firmware_versions/FitaDigital_v1.83.bin` exists

- [ ] **Step 1: Move TODO entry to "Feito"**

In `TODO.md`, remove the "Em curso" entry from Task 1 and add to top of `## Feito`:
```markdown
- 2026-05-15 — **WiFi stress test API v1.83 PASS**: endpoint `POST /api/wifi/stress?down_s=N` + `tools/wifi_stress.py` orchestrator. 6 ciclos (3 SOFT@35s + 3 HARD@305s) com 0 reboots, recovery <60s, self-healing layered v1.82 (`wifi_keepalive_tick`) validado: SOFT path (`net_wifi_begin_saved`) e HARD path (`esp_wifi_stop`+`start`+`begin_saved`) ambos disparam e recuperam. CSV: `logs/wifi_stress_<ts>.csv`. Spec/plan: `docs/superpowers/{specs,plans}/2026-05-15-wifi-stress-test-api-*.md`.
```

- [ ] **Step 2: Verify firmware archive**

```powershell
Test-Path firmware_versions/FitaDigital_v1.83.bin
```
Expected: `True` (post-script PIO copia automaticamente per memory note).

- [ ] **Step 3: Commit final**

```powershell
git add TODO.md logs/wifi_stress_*.csv firmware_versions/FitaDigital_v1.83.bin
git commit -m "test(wifi): stress 6 ciclos PASS — self-healing v1.82 validado v1.83"
```

- [ ] **Step 4: Final verification**

```powershell
git log --oneline -8
git status
```
Expected: clean working tree (or only logs/ untracked from previous sessions); v1.83 commits visible.

---

## Self-Review notes

- **Spec coverage:** All endpoint spec items in tasks 2-3; orchestrator spec in task 6; success metrics in task 7. WG handshake check from spec is informational (verified in fdigi.log markers, not enforced as PASS criterion in script — acceptable simplification).
- **Placeholders:** none.
- **Type consistency:** `down_s` is `uint32_t` in handler arg, `long` parsed from query (range-checked), passed via heap `uint32_t*`. Python uses `int`. Consistent.
- **Risk noted:** if HARD cycle recovery exceeds 60s on real hardware (e.g., DHCP slow), bump threshold and document. Initial criterion is best-effort.
