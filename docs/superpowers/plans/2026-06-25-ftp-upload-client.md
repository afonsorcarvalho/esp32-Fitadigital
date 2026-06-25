# FTP-Upload Client Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Push `/CICLOS` cycle files from the SD card to a remote FTP server, uploading only new/changed files (size-based journal), verifying each upload via the FTP `SIZE` command, without ever stalling the RS485 capture task.

**Architecture:** A pure-logic core (`ftp_journal_core`, no Arduino deps, host-unit-tested) handles journal parse/serialize, change detection, and remote-path mapping. A firmware module (`ftp_upload`) owns one FreeRTOS task `ftp_up` that, each interval or on manual trigger, connects via the ldab `ESP32_FTPClient` library, walks `/CICLOS`, and uploads pending files in ~2 KB chunks. Each SD read is a serialized job through the existing `sd_io` task, so RS485 writes are never blocked by network latency. Configuration lives in NVS (`app_settings`) and is exposed in the SRV settings tab.

**Tech Stack:** ESP32-S3, Arduino-ESP32 3.0.3, PlatformIO, FreeRTOS, LVGL 8.3, Arduino `SD`, `ldab/ESP32_FTPClient`, NVS `Preferences`, Unity (native unit tests).

---

## Reference Facts (verified against the codebase)

- ldab `ESP32_FTPClient` API (confirmed, no `Size()` method exists):
  - `ESP32_FTPClient(char* host, uint16_t port, char* user, char* pass, uint16_t timeout=10000, uint8_t verbose=1);`
  - `void OpenConnection();` / `void CloseConnection();` / `bool isConnected();`
  - `void InitFile(const char* type);` (use `"Type I"` = binary)
  - `void NewFile(const char* fileName);` (issues `STOR`)
  - `void WriteData(unsigned char* data, int dataLength);`
  - `void CloseFile();`
  - `void MakeDir(const char* dir);`
  - `void Write(const char* str);` (raw control-socket write — does NOT append CRLF)
  - `void GetFTPAnswer(char* result = NULL, int offsetStart = 0);` (reads control reply into `result`)
- SD paths use `/CICLOS/YYYY/MM/...` (Arduino `SD`, root `/`). The `sd_access` API serializes all SD I/O through the `sd_io` task: `sd_access_sync(std::function<void()>)` runs the lambda in `sd_io` context (inline if already there).
- Settings idiom (`src/app_settings.cpp`): a static `Preferences s_prefs;` (NVS namespace opened in `app_settings_init`). String getter: `return s_prefs.getString("key", "default");`. Setter: `s_prefs.putString("key", v);`. SD-cfg mirror: `ParsedSdCfg` struct + `cfg_parse_kv()` + `cfg_apply_parsed()` + the writer block near `src/app_settings.cpp:680`.
- Settings UI: SRV tab built in `src/ui/ui_app.cpp` from line 2419; `srv_scroll` flex column with local lambda `srv_section_header(const char* title)`; shared keyboard `s_sett_ftp_kb`; textarea focus uses `settings_ftp_ta_kb_event_cb`. Existing FTP-server save cb: `settings_save_ftp_cb` (`src/ui/ui_app.cpp:1737`).
- Boot sequence (`src/app.cpp`): `sd_access_register_tick(...)` (line 227) → `sd_access_start_task()` (228) → `service_supervisor_init()` (299) → `net_services_start_background_task()` (350) → `net_mqtt_init()` (352) → `screenshot_init()` (359).

## File Structure

- **Create** `src/ftp_journal_core.h` — pure-logic interface (no Arduino).
- **Create** `src/ftp_journal_core.cpp` — pure-logic implementation.
- **Create** `test/test_ftp_journal/test_ftp_journal.cpp` — native Unity tests for the core.
- **Create** `src/ftp_upload.h` — firmware module interface (`init`, `request_now`).
- **Create** `src/ftp_upload.cpp` — `ftp_up` task, FTP client glue, chunked upload, SIZE verify.
- **Modify** `platformio.ini` — add `ESP32_FTPClient` to `lib_deps`; add `[env:native]` test env.
- **Modify** `src/app_settings.h` / `src/app_settings.cpp` — 7 new settings (getters/setters + SD-cfg mirror).
- **Modify** `src/ui/ui_app.cpp` — "FTP↑ Upload" section in the SRV tab + manual-sync button.
- **Modify** `src/app.cpp` — call `ftp_upload_init()` in boot.

---

## Phase A — Pure-logic core (native TDD)

### Task A1: Native test environment

**Files:**
- Modify: `platformio.ini` (append a new env block at end of file)

- [ ] **Step 1: Add the native test env**

Append to `platformio.ini`:

```ini

; Ambiente nativo (host) apenas para testes unitarios de logica pura.
; test_build_src fica desligado: os testes incluem o .cpp puro directamente,
; evitando compilar codigo Arduino no host.
[env:native]
platform = native
test_framework = unity
build_flags =
    -std=c++17
    -Isrc
```

- [ ] **Step 2: Commit**

```bash
git add platformio.ini
git commit -m "build(test): add native unity env for pure-logic unit tests"
```

### Task A2: Journal parse/serialize (TDD)

**Files:**
- Create: `src/ftp_journal_core.h`
- Create: `src/ftp_journal_core.cpp`
- Test: `test/test_ftp_journal/test_ftp_journal.cpp`

- [ ] **Step 1: Write the failing test**

Create `test/test_ftp_journal/test_ftp_journal.cpp`:

```cpp
#include <unity.h>
#include "../../src/ftp_journal_core.cpp"   // pure logic; native env does not build src/

void test_parse_basic(void) {
    auto m = ftpj::parse_journal("2026/06/20260625.txt|14320\n2026/06/cycles.ndjson|512\n");
    TEST_ASSERT_EQUAL_INT(2, (int)m.size());
    TEST_ASSERT_EQUAL_INT64(14320, m["2026/06/20260625.txt"]);
    TEST_ASSERT_EQUAL_INT64(512, m["2026/06/cycles.ndjson"]);
}

void test_parse_ignores_blank_and_malformed(void) {
    auto m = ftpj::parse_journal("\n  \nbadline_no_pipe\n2026/a.txt|10\nx|notanumber\n");
    TEST_ASSERT_EQUAL_INT(1, (int)m.size());
    TEST_ASSERT_EQUAL_INT64(10, m["2026/a.txt"]);
}

void test_serialize_roundtrip(void) {
    ftpj::JournalMap m;
    m["2026/06/a.txt"] = 100;
    m["2026/06/b.txt"] = 200;
    std::string s = ftpj::serialize_journal(m);
    auto m2 = ftpj::parse_journal(s);
    TEST_ASSERT_EQUAL_INT(2, (int)m2.size());
    TEST_ASSERT_EQUAL_INT64(100, m2["2026/06/a.txt"]);
    TEST_ASSERT_EQUAL_INT64(200, m2["2026/06/b.txt"]);
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_parse_basic);
    RUN_TEST(test_parse_ignores_blank_and_malformed);
    RUN_TEST(test_serialize_roundtrip);
    return UNITY_END();
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `pio test -e native -f test_ftp_journal`
Expected: FAIL — `ftp_journal_core.cpp` / `ftpj::` not found (compile error).

- [ ] **Step 3: Write minimal implementation**

Create `src/ftp_journal_core.h`:

```cpp
/**
 * @file ftp_journal_core.h
 * @brief Logica pura do cliente FTP-upload (sem dependencias Arduino):
 *        journal (relpath->size), deteccao de mudanca, mapeamento de caminhos remotos.
 *        Testavel no host (env:native).
 */
#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace ftpj {

using JournalMap = std::map<std::string, long>;  // relpath (relativo a /CICLOS) -> tamanho

/** Faz parse de linhas "relpath|size"; ignora vazias/malformadas. */
JournalMap parse_journal(const std::string &text);

/** Serializa o mapa de volta para texto "relpath|size\n" (ordem por chave). */
std::string serialize_journal(const JournalMap &m);

/** True se o ficheiro precisa de (re)upload: ausente no journal OU tamanho diferente. */
bool needs_upload(const JournalMap &j, const std::string &relpath, long cur_size);

/** Junta base + relpath em caminho remoto com '/' unico (sem barra dupla, sem barra final). */
std::string remote_path(const std::string &base, const std::string &relpath);

/** Lista ordenada de directorios a criar (MKD) para o relpath, incluindo a base.
 *  Ex.: base="/up", relpath="2026/06/a.txt" -> {"/up","/up/2026","/up/2026/06"}. */
std::vector<std::string> remote_dir_components(const std::string &base, const std::string &relpath);

}  // namespace ftpj
```

Create `src/ftp_journal_core.cpp`:

```cpp
#include "ftp_journal_core.h"

#include <cstdlib>
#include <sstream>

namespace ftpj {

JournalMap parse_journal(const std::string &text) {
    JournalMap m;
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) {
        size_t bar = line.find('|');
        if (bar == std::string::npos || bar == 0) {
            continue;
        }
        std::string relpath = line.substr(0, bar);
        std::string num = line.substr(bar + 1);
        // tamanho: precisa de pelo menos um digito e so digitos
        if (num.empty()) {
            continue;
        }
        bool ok = true;
        for (char c : num) {
            if (c < '0' || c > '9') {
                ok = false;
                break;
            }
        }
        if (!ok) {
            continue;
        }
        m[relpath] = std::strtol(num.c_str(), nullptr, 10);
    }
    return m;
}

std::string serialize_journal(const JournalMap &m) {
    std::string out;
    for (const auto &kv : m) {
        out += kv.first;
        out += '|';
        out += std::to_string(kv.second);
        out += '\n';
    }
    return out;
}

bool needs_upload(const JournalMap &j, const std::string &relpath, long cur_size) {
    auto it = j.find(relpath);
    if (it == j.end()) {
        return true;
    }
    return it->second != cur_size;
}

static std::string join_slash(const std::string &a, const std::string &b) {
    if (a.empty()) {
        return b;
    }
    bool a_slash = !a.empty() && a.back() == '/';
    bool b_slash = !b.empty() && b.front() == '/';
    if (a_slash && b_slash) {
        return a + b.substr(1);
    }
    if (!a_slash && !b_slash) {
        return a + "/" + b;
    }
    return a + b;
}

std::string remote_path(const std::string &base, const std::string &relpath) {
    std::string r = join_slash(base, relpath);
    if (r.size() > 1 && r.back() == '/') {
        r.pop_back();
    }
    return r;
}

std::vector<std::string> remote_dir_components(const std::string &base, const std::string &relpath) {
    std::vector<std::string> out;
    std::string b = base.empty() ? "/" : base;
    if (b.size() > 1 && b.back() == '/') {
        b.pop_back();
    }
    out.push_back(b);
    std::string acc = b;
    // percorre os segmentos de directorio do relpath (exclui o ultimo = nome do ficheiro)
    std::istringstream in(relpath);
    std::string seg;
    std::vector<std::string> segs;
    while (std::getline(in, seg, '/')) {
        if (!seg.empty()) {
            segs.push_back(seg);
        }
    }
    if (segs.empty()) {
        return out;
    }
    for (size_t i = 0; i + 1 < segs.size(); ++i) {
        acc = join_slash(acc, segs[i]);
        out.push_back(acc);
    }
    return out;
}

}  // namespace ftpj
```

- [ ] **Step 4: Run test to verify it passes**

Run: `pio test -e native -f test_ftp_journal`
Expected: PASS (3 tests).

- [ ] **Step 5: Commit**

```bash
git add src/ftp_journal_core.h src/ftp_journal_core.cpp test/test_ftp_journal/test_ftp_journal.cpp
git commit -m "feat(ftp): journal parse/serialize core + native tests"
```

### Task A3: Change detection + remote-path mapping (TDD)

**Files:**
- Test: `test/test_ftp_journal/test_ftp_journal.cpp` (extend)

- [ ] **Step 1: Write the failing tests**

Add these functions to `test/test_ftp_journal/test_ftp_journal.cpp` and register them in `main()`:

```cpp
void test_needs_upload(void) {
    ftpj::JournalMap j;
    j["a.txt"] = 100;
    TEST_ASSERT_TRUE(ftpj::needs_upload(j, "b.txt", 50));    // ausente
    TEST_ASSERT_TRUE(ftpj::needs_upload(j, "a.txt", 101));   // tamanho mudou
    TEST_ASSERT_FALSE(ftpj::needs_upload(j, "a.txt", 100));  // igual
}

void test_remote_path(void) {
    TEST_ASSERT_EQUAL_STRING("/up/2026/06/a.txt",
        ftpj::remote_path("/up", "2026/06/a.txt").c_str());
    TEST_ASSERT_EQUAL_STRING("/2026/06/a.txt",
        ftpj::remote_path("/", "2026/06/a.txt").c_str());
    TEST_ASSERT_EQUAL_STRING("/up/a.txt",
        ftpj::remote_path("/up/", "a.txt").c_str());
}

void test_remote_dirs(void) {
    auto d = ftpj::remote_dir_components("/up", "2026/06/a.txt");
    TEST_ASSERT_EQUAL_INT(3, (int)d.size());
    TEST_ASSERT_EQUAL_STRING("/up", d[0].c_str());
    TEST_ASSERT_EQUAL_STRING("/up/2026", d[1].c_str());
    TEST_ASSERT_EQUAL_STRING("/up/2026/06", d[2].c_str());
}
```

Register in `main()` before `UNITY_END()`:

```cpp
    RUN_TEST(test_needs_upload);
    RUN_TEST(test_remote_path);
    RUN_TEST(test_remote_dirs);
```

- [ ] **Step 2: Run test to verify it passes**

Run: `pio test -e native -f test_ftp_journal`
Expected: PASS (6 tests). These functions are already implemented in Task A2, so they pass immediately — this task locks their behavior with tests.

- [ ] **Step 3: Commit**

```bash
git add test/test_ftp_journal/test_ftp_journal.cpp
git commit -m "test(ftp): cover change-detection and remote-path mapping"
```

---

## Phase B — Settings (NVS + SD-cfg mirror)

> No unit harness for Arduino code in this project; validation is the compile gate plus runtime save/reload. Use NVS keys exactly as below (≤15 chars, `Preferences` limit).

### Task B1: Settings getters/setters

**Files:**
- Modify: `src/app_settings.h`
- Modify: `src/app_settings.cpp`

- [ ] **Step 1: Declare the API**

In `src/app_settings.h`, after the existing FTP-server block (after line 46), add:

```cpp
/* ------------------------------------------------------------------ */
/* FTP-upload client (push de /CICLOS para servidor FTP remoto)         */
/* ------------------------------------------------------------------ */

/** Liga/desliga o cliente de upload FTP. Default false. */
bool app_settings_ftp_up_enabled(void);
void app_settings_set_ftp_up_enabled(bool on);

/** Host do servidor FTP remoto (IP/hostname). Max 127 chars. Default "". */
String app_settings_ftp_up_host(void);
void   app_settings_set_ftp_up_host(const char *host);

/** Porta FTP. Default 21. */
uint16_t app_settings_ftp_up_port(void);
void     app_settings_set_ftp_up_port(uint16_t port);

/** Utilizador FTP (max 31 chars). */
String app_settings_ftp_up_user(void);
/** Password FTP (max 31 chars). */
String app_settings_ftp_up_pass(void);
/** Grava utilizador+password atomicamente. */
void   app_settings_set_ftp_up_creds(const char *user, const char *pass);

/** Directorio base remoto (ex.: "/" ou "/fitadigital"). Max 127 chars. Default "/". */
String app_settings_ftp_up_remote_dir(void);
void   app_settings_set_ftp_up_remote_dir(const char *dir);

/** Intervalo entre passagens de upload, em segundos. Clamp [30..86400]. Default 300. */
uint16_t app_settings_ftp_up_interval_s(void);
void     app_settings_set_ftp_up_interval_s(uint16_t secs);
```

- [ ] **Step 2: Implement the getters/setters**

In `src/app_settings.cpp`, add near the other FTP code (after the `app_settings_ftp_pass` setter region, around line 855). NVS keys: `fup_en`, `fup_h`, `fup_port`, `fup_u`, `fup_p`, `fup_rd`, `fup_iv`.

```cpp
/* ---- FTP-upload client ---- */

bool app_settings_ftp_up_enabled(void) {
  return s_prefs.getBool("fup_en", false);
}
void app_settings_set_ftp_up_enabled(bool on) {
  s_prefs.putBool("fup_en", on);
}

String app_settings_ftp_up_host(void) {
  return s_prefs.getString("fup_h", "");
}
void app_settings_set_ftp_up_host(const char *host) {
  char buf[128] = {0};
  if (host) {
    strncpy(buf, host, sizeof(buf) - 1);
  }
  s_prefs.putString("fup_h", buf);
}

uint16_t app_settings_ftp_up_port(void) {
  return (uint16_t)s_prefs.getUShort("fup_port", 21);
}
void app_settings_set_ftp_up_port(uint16_t port) {
  s_prefs.putUShort("fup_port", port == 0 ? 21 : port);
}

String app_settings_ftp_up_user(void) {
  return s_prefs.getString("fup_u", "");
}
String app_settings_ftp_up_pass(void) {
  return s_prefs.getString("fup_p", "");
}
void app_settings_set_ftp_up_creds(const char *user, const char *pass) {
  char u[32] = {0};
  char p[32] = {0};
  if (user) {
    strncpy(u, user, sizeof(u) - 1);
  }
  if (pass) {
    strncpy(p, pass, sizeof(p) - 1);
  }
  s_prefs.putString("fup_u", u);
  s_prefs.putString("fup_p", p);
}

String app_settings_ftp_up_remote_dir(void) {
  String d = s_prefs.getString("fup_rd", "/");
  if (d.length() == 0) {
    d = "/";
  }
  return d;
}
void app_settings_set_ftp_up_remote_dir(const char *dir) {
  char buf[128] = {0};
  if (dir && dir[0] != '\0') {
    strncpy(buf, dir, sizeof(buf) - 1);
  } else {
    buf[0] = '/';
  }
  s_prefs.putString("fup_rd", buf);
}

uint16_t app_settings_ftp_up_interval_s(void) {
  uint16_t v = (uint16_t)s_prefs.getUShort("fup_iv", 300);
  if (v < 30) {
    v = 30;
  }
  return v;
}
void app_settings_set_ftp_up_interval_s(uint16_t secs) {
  if (secs < 30) {
    secs = 30;
  }
  s_prefs.putUShort("fup_iv", secs);
}
```

- [ ] **Step 3: Build to verify it compiles**

Run: `pio run -e esp32-s3-touch-lcd-4_3b`
Expected: SUCCESS (no link/compile errors).

- [ ] **Step 4: Commit**

```bash
git add src/app_settings.h src/app_settings.cpp
git commit -m "feat(ftp): NVS settings for FTP-upload client"
```

### Task B2: SD-cfg mirror for the new settings

**Files:**
- Modify: `src/app_settings.cpp` (`ParsedSdCfg` struct, `cfg_parse_kv`, `cfg_apply_parsed`, the `/fdigi.cfg` writer block)

- [ ] **Step 1: Add fields to `ParsedSdCfg`**

In `src/app_settings.cpp`, inside the `ParsedSdCfg` struct (ends at line 138), add before the closing `}`:

```cpp
  bool have_fup_en;
  bool fup_en;
  bool have_fup_h;
  char fup_h[128];
  bool have_fup_port;
  uint32_t fup_port;
  bool have_fup_u;
  char fup_u[32];
  bool have_fup_p;
  char fup_p[32];
  bool have_fup_rd;
  char fup_rd[128];
  bool have_fup_iv;
  uint32_t fup_iv;
```

- [ ] **Step 2: Parse `[ftp_up]` keys**

In `cfg_parse_kv()`, the section number for `[mqtt]` is the highest existing one. Add a new section. First, find where section names map to integers (the `[section]` header parser earlier in the file uses a comparison chain). Add `"ftp_up"` as a new section id — locate the header-matching code (search for the string `"mqtt"` near the section detector) and add an `else if` mapping `[ftp_up]` to the next integer (call it `SEC_FTP_UP`). Then in the `switch (sec)` in `cfg_parse_kv`, add a `case` for that section:

```cpp
  case SEC_FTP_UP: /* [ftp_up] */
    if (!strcmp(key, "fup_en")) {
      c->fup_en = strtol(val, nullptr, 10) != 0;
      c->have_fup_en = true;
    } else if (!strcmp(key, "fup_h")) {
      c->have_fup_h = copy_key_str(c->fup_h, sizeof(c->fup_h), val);
    } else if (!strcmp(key, "fup_port")) {
      c->fup_port = (uint32_t)strtol(val, nullptr, 10);
      c->have_fup_port = true;
    } else if (!strcmp(key, "fup_u")) {
      c->have_fup_u = copy_key_str(c->fup_u, sizeof(c->fup_u), val);
    } else if (!strcmp(key, "fup_p")) {
      c->have_fup_p = copy_key_str(c->fup_p, sizeof(c->fup_p), val);
    } else if (!strcmp(key, "fup_rd")) {
      c->have_fup_rd = copy_key_str(c->fup_rd, sizeof(c->fup_rd), val);
    } else if (!strcmp(key, "fup_iv")) {
      c->fup_iv = (uint32_t)strtol(val, nullptr, 10);
      c->have_fup_iv = true;
    }
    return true;
```

> Implementation note: match the EXACT mechanism the file already uses to turn a `[name]` header into the `sec` integer (it is a small if/else chain near the top of the cfg reader). Define `SEC_FTP_UP` consistently there and in the `switch`. Do not invent a different dispatch.

- [ ] **Step 3: Apply parsed values to NVS**

In `cfg_apply_parsed()` (starts line 473), add near the MQTT block:

```cpp
    if (c->have_fup_en) {
      s_prefs.putBool("fup_en", c->fup_en);
    }
    if (c->have_fup_h) {
      s_prefs.putString("fup_h", c->fup_h);
    }
    if (c->have_fup_port) {
      s_prefs.putUShort("fup_port", (uint16_t)c->fup_port);
    }
    if (c->have_fup_u) {
      s_prefs.putString("fup_u", c->fup_u);
    }
    if (c->have_fup_p) {
      s_prefs.putString("fup_p", c->fup_p);
    }
    if (c->have_fup_rd) {
      s_prefs.putString("fup_rd", c->fup_rd);
    }
    if (c->have_fup_iv) {
      s_prefs.putUShort("fup_iv", (uint16_t)c->fup_iv);
    }
```

- [ ] **Step 4: Write the section to `/fdigi.cfg`**

In the `/fdigi.cfg` writer (the block ending near line 694), after the `[mqtt]` lines (`f.print(app_settings_mqtt_keywords().c_str()); f.print("\n");`), add:

```cpp
    f.print("\n[ftp_up]\n");
    f.printf("fup_en=%d\n", app_settings_ftp_up_enabled() ? 1 : 0);
    f.print("fup_h=");
    f.print(app_settings_ftp_up_host().c_str());
    f.printf("\nfup_port=%u\n", (unsigned)app_settings_ftp_up_port());
    f.print("fup_u=");
    f.print(app_settings_ftp_up_user().c_str());
    f.print("\nfup_p=");
    f.print(app_settings_ftp_up_pass().c_str());
    f.print("\nfup_rd=");
    f.print(app_settings_ftp_up_remote_dir().c_str());
    f.printf("\nfup_iv=%u\n", (unsigned)app_settings_ftp_up_interval_s());
```

- [ ] **Step 5: Build to verify it compiles**

Run: `pio run -e esp32-s3-touch-lcd-4_3b`
Expected: SUCCESS.

- [ ] **Step 6: Commit**

```bash
git add src/app_settings.cpp
git commit -m "feat(ftp): mirror FTP-upload settings to /fdigi.cfg"
```

---

## Phase C — FTP-upload module (task + chunked upload + verify)

### Task C1: Add the FTP client library

**Files:**
- Modify: `platformio.ini` (`lib_deps`)

- [ ] **Step 1: Add the dependency**

In `platformio.ini`, inside `lib_deps` (after the `JPEGENC` line, line 93), add:

```ini
    ; Cliente FTP (push de ficheiros do SD para servidor remoto).
    https://github.com/ldab/ESP32_FTPClient.git
```

- [ ] **Step 2: Build to verify the lib resolves**

Run: `pio run -e esp32-s3-touch-lcd-4_3b`
Expected: SUCCESS (library downloaded and linked; no symbol clashes).

- [ ] **Step 3: Commit**

```bash
git add platformio.ini
git commit -m "build(ftp): add ldab/ESP32_FTPClient dependency"
```

### Task C2: Module skeleton + interface

**Files:**
- Create: `src/ftp_upload.h`
- Create: `src/ftp_upload.cpp`

- [ ] **Step 1: Write the header**

Create `src/ftp_upload.h`:

```cpp
/**
 * @file ftp_upload.h
 * @brief Cliente FTP-upload: empurra /CICLOS para um servidor FTP remoto,
 *        com journal por tamanho e verificacao via comando SIZE.
 *        Task dedicada `ftp_up`; leitura do SD em chunks via sd_access (RS485-safe).
 */
#pragma once

/** Cria a task `ftp_up`. Chamar no boot, depois de sd_access_start_task(). */
void ftp_upload_init(void);

/** Pede uma passagem de upload imediata (acordar a task). Thread-safe. */
void ftp_upload_request_now(void);
```

- [ ] **Step 2: Write the skeleton implementation**

Create `src/ftp_upload.cpp`:

```cpp
#include "ftp_upload.h"

#include <Arduino.h>
#include <SD.h>
#include <WiFi.h>
#include <ESP32_FTPClient.h>

#include "app_settings.h"
#include "app_log.h"
#include "ftp_journal_core.h"
#include "sd_access.h"

static TaskHandle_t s_task = nullptr;

void ftp_upload_request_now(void) {
  if (s_task != nullptr) {
    xTaskNotifyGive(s_task);
  }
}

static void ftp_upload_task(void *arg);

void ftp_upload_init(void) {
  if (s_task != nullptr) {
    return;
  }
  // 6 KB stack: WiFiClient + buffers do ESP32_FTPClient + chunk de 2 KB.
  xTaskCreatePinnedToCore(ftp_upload_task, "ftp_up", 6144, nullptr, 1, &s_task, 1);
}

static void ftp_upload_task(void *arg) {
  (void)arg;
  for (;;) {
    uint32_t iv = app_settings_ftp_up_interval_s();
    // Acorda no intervalo OU quando ftp_upload_request_now() notifica.
    ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS((uint32_t)iv * 1000U));
    if (!app_settings_ftp_up_enabled()) {
      continue;
    }
    if (WiFi.status() != WL_CONNECTED) {
      app_log_feature_write("INFO", "FTPUP", "Sem Wi-Fi; passagem adiada.");
      continue;
    }
    // sync_pass() preenchido na Task C5.
  }
}
```

- [ ] **Step 3: Build to verify it compiles**

Run: `pio run -e esp32-s3-touch-lcd-4_3b`
Expected: SUCCESS. (Header name `ESP32_FTPClient.h` — if the build reports it not found, check the installed lib's `src/` for the exact filename and adjust the include.)

- [ ] **Step 4: Commit**

```bash
git add src/ftp_upload.h src/ftp_upload.cpp
git commit -m "feat(ftp): ftp_upload module skeleton + task"
```

### Task C3: Chunked file upload helper (RS485-safe)

**Files:**
- Modify: `src/ftp_upload.cpp`

- [ ] **Step 1: Add the upload helper**

In `src/ftp_upload.cpp`, add above `ftp_upload_task`:

```cpp
static const size_t kChunk = 2048;
static uint8_t s_chunk[kChunk];

// Le `vfs_path` em chunks via sd_io e envia por FTP. Devolve bytes enviados,
// ou -1 em erro de leitura. O File e' aberto/lido/fechado SEMPRE no contexto sd_io;
// o WriteData (rede) corre na task ftp_up entre chunks, sem bloquear o sd_io.
static long ftp_stream_file(ESP32_FTPClient &ftp, const char *vfs_path, const char *remote_path) {
  File f;
  bool open_ok = false;
  sd_access_sync([&]() {
    f = SD.open(vfs_path, FILE_READ);
    open_ok = (bool)f;
  });
  if (!open_ok) {
    return -1;
  }

  ftp.InitFile("Type I");
  ftp.NewFile(remote_path);  // STOR

  long sent = 0;
  for (;;) {
    int n = 0;
    sd_access_sync([&]() {
      n = (int)f.read(s_chunk, kChunk);
    });
    if (n <= 0) {
      break;
    }
    ftp.WriteData(s_chunk, n);
    sent += n;
  }
  ftp.CloseFile();

  sd_access_sync([&]() {
    f.close();
  });
  return sent;
}
```

- [ ] **Step 2: Build to verify it compiles**

Run: `pio run -e esp32-s3-touch-lcd-4_3b`
Expected: SUCCESS. (`ftp_stream_file` is unused for now — acceptable; a `-Wunused-function` warning may appear and is fine. It is wired up in Task C5.)

- [ ] **Step 3: Commit**

```bash
git add src/ftp_upload.cpp
git commit -m "feat(ftp): chunked RS485-safe file upload helper"
```

### Task C4: Remote SIZE verification helper

**Files:**
- Modify: `src/ftp_upload.cpp`

- [ ] **Step 1: Add the SIZE helper**

In `src/ftp_upload.cpp`, add above `ftp_upload_task`:

```cpp
// Emite "SIZE <path>" no socket de controlo e devolve o tamanho remoto, ou -1.
// O servidor responde "213 <n>". Em binario (Type I) o SIZE e' fiavel.
static long ftp_remote_size(ESP32_FTPClient &ftp, const char *remote_path) {
  char cmd[160];
  snprintf(cmd, sizeof(cmd), "SIZE %s\r\n", remote_path);
  ftp.Write(cmd);

  char resp[64] = {0};
  ftp.GetFTPAnswer(resp, 0);
  // Procura "213" e o numero que se segue.
  char *p = strstr(resp, "213");
  if (p == nullptr) {
    return -1;
  }
  p += 3;
  while (*p == ' ') {
    p++;
  }
  if (*p < '0' || *p > '9') {
    return -1;
  }
  return strtol(p, nullptr, 10);
}
```

- [ ] **Step 2: Build to verify it compiles**

Run: `pio run -e esp32-s3-touch-lcd-4_3b`
Expected: SUCCESS.

- [ ] **Step 3: Commit**

```bash
git add src/ftp_upload.cpp
git commit -m "feat(ftp): remote SIZE verification helper"
```

### Task C5: Scan + sync pass (walk /CICLOS, journal, MKD, upload, verify)

**Files:**
- Modify: `src/ftp_upload.cpp`

- [ ] **Step 1: Add journal load/flush helpers**

In `src/ftp_upload.cpp`, add above `ftp_upload_task`:

```cpp
static const char *kCiclosRoot = "/CICLOS";
static const char *kJournalPath = "/CICLOS/.ftpjournal";

// Le o journal do SD para um std::string (vazio se nao existir).
static std::string journal_read(void) {
  std::string out;
  sd_access_sync([&]() {
    File f = SD.open(kJournalPath, FILE_READ);
    if (!f) {
      return;
    }
    while (f.available()) {
      out += (char)f.read();
    }
    f.close();
  });
  return out;
}

// Escreve o journal inteiro (temp + rename = atomico).
static void journal_write(const ftpj::JournalMap &m) {
  std::string text = ftpj::serialize_journal(m);
  sd_access_sync([&]() {
    const char *tmp = "/CICLOS/.ftpjournal.tmp";
    SD.remove(tmp);
    File f = SD.open(tmp, FILE_WRITE);
    if (!f) {
      return;
    }
    f.write((const uint8_t *)text.data(), text.size());
    f.close();
    SD.remove(kJournalPath);
    SD.rename(tmp, kJournalPath);
  });
  sd_access_notify_changed();
}
```

`#include <string>` and `#include <vector>` at the top of the file if not already pulled in by `ftp_journal_core.h` (it is — but add them explicitly for clarity).

- [ ] **Step 2: Add the recursive scan**

Add above `ftp_upload_task`. The scan runs each directory listing inside `sd_access_sync` and records `{relpath, size}` pairs; it does NOT upload during the walk (uploads happen after, so the SD listing handle is not held across network I/O).

```cpp
struct ScanEntry {
  std::string relpath;  // relativo a /CICLOS, com '/'
  long size;
};

// Lista recursiva de /CICLOS. relbase comeca "" e cresce "2026", "2026/06".
static void scan_dir(const std::string &relbase, std::vector<ScanEntry> &out) {
  std::string vfs = std::string(kCiclosRoot);
  if (!relbase.empty()) {
    vfs += "/" + relbase;
  }
  std::vector<ScanEntry> files;       // ficheiros neste nivel
  std::vector<std::string> subdirs;   // subdirectorios a descer
  sd_access_sync([&]() {
    File dir = SD.open(vfs.c_str());
    if (!dir || !dir.isDirectory()) {
      if (dir) {
        dir.close();
      }
      return;
    }
    File e = dir.openNextFile();
    while (e) {
      const char *name = e.name();  // pode vir como basename ou caminho completo
      std::string base = name ? name : "";
      size_t slash = base.find_last_of('/');
      if (slash != std::string::npos) {
        base = base.substr(slash + 1);
      }
      std::string rel = relbase.empty() ? base : (relbase + "/" + base);
      if (e.isDirectory()) {
        subdirs.push_back(rel);
      } else if (rel != ".ftpjournal" && rel != ".ftpjournal.tmp") {
        ScanEntry se;
        se.relpath = rel;
        se.size = (long)e.size();
        files.push_back(se);
      }
      e = dir.openNextFile();
    }
    dir.close();
  });
  for (auto &f : files) {
    out.push_back(f);
  }
  for (auto &d : subdirs) {
    scan_dir(d, out);  // a recursao reentra em sd_access_sync (inline se ja em sd_io)
  }
}
```

- [ ] **Step 3: Add `sync_pass()`**

Add above `ftp_upload_task`:

```cpp
static void ftp_sync_pass(void) {
  String host = app_settings_ftp_up_host();
  if (host.length() == 0) {
    app_log_feature_write("WARN", "FTPUP", "Host nao configurado.");
    return;
  }
  String user = app_settings_ftp_up_user();
  String pass = app_settings_ftp_up_pass();
  String rdir = app_settings_ftp_up_remote_dir();
  uint16_t port = app_settings_ftp_up_port();

  // Buffers mutaveis (a lib pede char*).
  char hbuf[128], ubuf[32], pbuf[32];
  strncpy(hbuf, host.c_str(), sizeof(hbuf) - 1); hbuf[sizeof(hbuf) - 1] = 0;
  strncpy(ubuf, user.c_str(), sizeof(ubuf) - 1); ubuf[sizeof(ubuf) - 1] = 0;
  strncpy(pbuf, pass.c_str(), sizeof(pbuf) - 1); pbuf[sizeof(pbuf) - 1] = 0;

  ESP32_FTPClient ftp(hbuf, port, ubuf, pbuf, 15000, 1);
  ftp.OpenConnection();
  if (!ftp.isConnected()) {
    app_log_feature_write("WARN", "FTPUP", "Ligacao FTP falhou.");
    return;
  }

  // 1) Carrega journal.
  ftpj::JournalMap journal = ftpj::parse_journal(journal_read());

  // 2) Varre /CICLOS.
  std::vector<ScanEntry> entries;
  scan_dir("", entries);

  std::string base = rdir.c_str();
  std::vector<std::string> made_dirs;  // cache de MKD nesta passagem
  bool journal_dirty = false;
  int uploaded = 0, verified = 0, failed = 0;

  // 3) Upload dos pendentes.
  for (auto &e : entries) {
    if (!ftpj::needs_upload(journal, e.relpath, e.size)) {
      continue;
    }
    // Garante directorios remotos.
    auto dirs = ftpj::remote_dir_components(base, e.relpath);
    for (auto &d : dirs) {
      bool already = false;
      for (auto &m : made_dirs) {
        if (m == d) { already = true; break; }
      }
      if (!already) {
        ftp.MakeDir(d.c_str());  // ignora erro "ja existe"
        made_dirs.push_back(d);
      }
    }

    std::string rp = ftpj::remote_path(base, e.relpath);
    std::string vfs = std::string(kCiclosRoot) + "/" + e.relpath;

    long sent = ftp_stream_file(ftp, vfs.c_str(), rp.c_str());
    if (sent < 0) {
      app_log_feature_write("WARN", "FTPUP", "Erro a ler ficheiro do SD.");
      failed++;
      continue;
    }
    uploaded++;

    // 4) Verificacao: SIZE remoto == bytes enviados == tamanho local.
    long rsize = ftp_remote_size(ftp, rp.c_str());
    if (sent == e.size && rsize == sent) {
      journal[e.relpath] = e.size;  // commit (em RAM)
      journal_dirty = true;
      verified++;
    } else {
      char msg[160];
      snprintf(msg, sizeof(msg),
               "Verif. falhou %s: enviado=%ld local=%ld remoto=%ld",
               e.relpath.c_str(), sent, e.size, rsize);
      app_log_feature_write("WARN", "FTPUP", msg);
      failed++;
    }
  }

  ftp.CloseConnection();

  // 5) Flush do journal uma vez por passagem.
  if (journal_dirty) {
    journal_write(journal);
  }

  char done[128];
  snprintf(done, sizeof(done), "Passagem: %d enviados, %d verificados, %d falhas.",
           uploaded, verified, failed);
  app_log_feature_write("INFO", "FTPUP", done);
}
```

- [ ] **Step 4: Wire `ftp_sync_pass()` into the task**

In `ftp_upload_task`, replace the comment `// sync_pass() preenchido na Task C5.` with:

```cpp
    ftp_sync_pass();
```

- [ ] **Step 5: Build to verify it compiles**

Run: `pio run -e esp32-s3-touch-lcd-4_3b`
Expected: SUCCESS.

- [ ] **Step 6: Commit**

```bash
git add src/ftp_upload.cpp
git commit -m "feat(ftp): scan /CICLOS, upload pending, verify via SIZE, journal commit"
```

---

## Phase D — UI (SRV tab section + manual sync)

### Task D1: Static state + save/sync callbacks

**Files:**
- Modify: `src/ui/ui_app.cpp`

- [ ] **Step 1: Add static widget pointers + include**

Near the other settings statics (after line 79), add:

```cpp
/* Aba SRV — seccao FTP-upload (cliente). */
static lv_obj_t *s_ta_fup_host = nullptr;
static lv_obj_t *s_ta_fup_port = nullptr;
static lv_obj_t *s_ta_fup_user = nullptr;
static lv_obj_t *s_ta_fup_pass = nullptr;
static lv_obj_t *s_ta_fup_rdir = nullptr;
static lv_obj_t *s_ta_fup_iv = nullptr;
static lv_obj_t *s_sw_fup_en = nullptr;
```

At the top includes of `ui_app.cpp`, ensure `#include "ftp_upload.h"` is present (add it next to the other module includes).

- [ ] **Step 2: Add the save + sync callbacks**

Add near `settings_save_ftp_cb` (after line 1754):

```cpp
static void settings_save_fup_cb(lv_event_t *e) {
  (void)e;
  if (s_ta_fup_host == nullptr) {
    return;
  }
  const char *host = lv_textarea_get_text(s_ta_fup_host);
  const char *ports = lv_textarea_get_text(s_ta_fup_port);
  const char *user = lv_textarea_get_text(s_ta_fup_user);
  const char *pass = lv_textarea_get_text(s_ta_fup_pass);
  const char *rdir = lv_textarea_get_text(s_ta_fup_rdir);
  const char *ivs = lv_textarea_get_text(s_ta_fup_iv);

  long port = (ports && *ports) ? strtol(ports, nullptr, 10) : 21;
  long iv = (ivs && *ivs) ? strtol(ivs, nullptr, 10) : 300;

  app_settings_set_ftp_up_host(host ? host : "");
  app_settings_set_ftp_up_port((uint16_t)(port <= 0 ? 21 : port));
  app_settings_set_ftp_up_creds(user ? user : "", pass ? pass : "");
  app_settings_set_ftp_up_remote_dir((rdir && *rdir) ? rdir : "/");
  app_settings_set_ftp_up_interval_s((uint16_t)(iv < 30 ? 30 : iv));
  app_settings_set_ftp_up_enabled(s_sw_fup_en && lv_obj_has_state(s_sw_fup_en, LV_STATE_CHECKED));

  ui_toast_show(ToastKind::Success, "Upload FTP guardado");
}

static void settings_sync_now_cb(lv_event_t *e) {
  (void)e;
  ftp_upload_request_now();
  ui_toast_show(ToastKind::Info, "Sincronizacao FTP pedida");
}
```

> Implementation note: confirm the exact `ToastKind` enumerators in use (`Success`/`Warn`/`Info`) match those already used in this file (`ui_toast_show(ToastKind::Success, ...)` appears at line 1753). If `Info` does not exist, use `Success`.

- [ ] **Step 3: Build to verify it compiles**

Run: `pio run -e esp32-s3-touch-lcd-4_3b`
Expected: SUCCESS. (Callbacks unused until D2 — warning acceptable.)

- [ ] **Step 4: Commit**

```bash
git add src/ui/ui_app.cpp
git commit -m "feat(ui): FTP-upload save + manual-sync callbacks"
```

### Task D2: SRV-tab UI section

**Files:**
- Modify: `src/ui/ui_app.cpp`

- [ ] **Step 1: Add the UI block**

In `create_settings_screen`, inside the SRV tab, insert a new section. Place it right after the FTP-server block's keyboard creation (after line 2507, before `/* --- Bloco WireGuard --- */` at line 2509). It reuses the existing `srv_section_header` lambda, `srv_scroll` parent, shared keyboard `s_sett_ftp_kb`, and `settings_ftp_ta_kb_event_cb` focus handler:

```cpp
  /* --- Bloco FTP-upload (cliente) --- */
  srv_section_header(LV_SYMBOL_UPLOAD " Upload FTP");

  {
    lv_obj_t *en_row = lv_obj_create(srv_scroll);
    lv_obj_set_width(en_row, LV_PCT(100));
    lv_obj_set_height(en_row, LV_SIZE_CONTENT);
    lv_obj_set_layout(en_row, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(en_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(en_row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(en_row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *en_lbl = lv_label_create(en_row);
    lv_label_set_text(en_lbl, "Ativar upload");
    s_sw_fup_en = lv_switch_create(en_row);
    if (app_settings_ftp_up_enabled()) {
      lv_obj_add_state(s_sw_fup_en, LV_STATE_CHECKED);
    }
  }

  auto fup_field = [&](const char *label, bool pwd, bool numeric, const char *val) -> lv_obj_t * {
    lv_obj_t *l = lv_label_create(srv_scroll);
    lv_label_set_text(l, label);
    lv_obj_t *ta = lv_textarea_create(srv_scroll);
    lv_textarea_set_one_line(ta, true);
    lv_obj_set_width(ta, LV_PCT(100));
    if (pwd) {
      lv_textarea_set_password_mode(ta, true);
    }
    if (numeric) {
      lv_textarea_set_accepted_chars(ta, "0123456789");
    }
    lv_textarea_set_text(ta, val ? val : "");
    lv_obj_add_event_cb(ta, settings_ftp_ta_kb_event_cb, LV_EVENT_FOCUSED, nullptr);
    lv_obj_add_event_cb(ta, settings_ftp_ta_kb_event_cb, LV_EVENT_DEFOCUSED, nullptr);
    return ta;
  };

  s_ta_fup_host = fup_field("Host:", false, false, app_settings_ftp_up_host().c_str());
  {
    char pbuf[8];
    snprintf(pbuf, sizeof(pbuf), "%u", (unsigned)app_settings_ftp_up_port());
    s_ta_fup_port = fup_field("Porta:", false, true, pbuf);
  }
  s_ta_fup_user = fup_field("Utilizador:", false, false, app_settings_ftp_up_user().c_str());
  s_ta_fup_pass = fup_field("Senha:", true, false, app_settings_ftp_up_pass().c_str());
  s_ta_fup_rdir = fup_field("Dir remoto:", false, false, app_settings_ftp_up_remote_dir().c_str());
  {
    char ibuf[8];
    snprintf(ibuf, sizeof(ibuf), "%u", (unsigned)app_settings_ftp_up_interval_s());
    s_ta_fup_iv = fup_field("Intervalo (s):", false, true, ibuf);
  }

  lv_obj_t *bt_save_fup = lv_btn_create(srv_scroll);
  lv_obj_t *lbf = lv_label_create(bt_save_fup);
  lv_label_set_text(lbf, LV_SYMBOL_SAVE " Salvar Upload");
  lv_obj_center(lbf);
  lv_obj_add_event_cb(bt_save_fup, settings_save_fup_cb, LV_EVENT_CLICKED, nullptr);

  lv_obj_t *bt_sync_now = lv_btn_create(srv_scroll);
  lv_obj_t *lbn = lv_label_create(bt_sync_now);
  lv_label_set_text(lbn, LV_SYMBOL_UPLOAD " Sincronizar agora");
  lv_obj_center(lbn);
  lv_obj_add_event_cb(bt_sync_now, settings_sync_now_cb, LV_EVENT_CLICKED, nullptr);
```

> Implementation note: the existing FTP-server textareas bind the shared keyboard via `settings_ftp_ta_kb_event_cb`, which sets `s_sett_ftp_kb`'s target. Reusing it here gives the new fields the same on-screen keyboard with the viewport anchoring already fixed in v2.23. Verify `lv_textarea_set_accepted_chars` exists in the LVGL 8.3 build (it does); if a numeric field misbehaves, drop the `numeric` restriction — non-blocking for the feature.

- [ ] **Step 2: Reset pointers on settings-screen teardown**

Find where settings statics are nulled on teardown (the block that sets `s_ta_ftp_user = nullptr;` — search for it). Add:

```cpp
  s_ta_fup_host = nullptr;
  s_ta_fup_port = nullptr;
  s_ta_fup_user = nullptr;
  s_ta_fup_pass = nullptr;
  s_ta_fup_rdir = nullptr;
  s_ta_fup_iv = nullptr;
  s_sw_fup_en = nullptr;
```

> If no such teardown block exists for the FTP-server statics, skip this step — the pointers are reassigned every time `create_settings_screen` runs.

- [ ] **Step 3: Build to verify it compiles**

Run: `pio run -e esp32-s3-touch-lcd-4_3b`
Expected: SUCCESS.

- [ ] **Step 4: Commit**

```bash
git add src/ui/ui_app.cpp
git commit -m "feat(ui): FTP-upload config section in SRV tab"
```

---

## Phase E — Boot integration

### Task E1: Initialize the module at boot

**Files:**
- Modify: `src/app.cpp`

- [ ] **Step 1: Include + init call**

In `src/app.cpp`, add the include near the others (e.g. next to `#include "screenshot.h"` at line 51):

```cpp
#include "ftp_upload.h"
```

After `screenshot_init();` (line 359), add:

```cpp
  ftp_upload_init();  /* cliente FTP-upload: task ftp_up (gated por settings) */
```

- [ ] **Step 2: Build to verify it compiles**

Run: `pio run -e esp32-s3-touch-lcd-4_3b`
Expected: SUCCESS.

- [ ] **Step 3: Commit**

```bash
git add src/app.cpp
git commit -m "feat(ftp): init ftp_upload at boot"
```

---

## Phase F — Hardware validation

> No automated harness for on-device behavior; validate against a real FTP server and the RS485 sender, the project's established practice.

### Task F1: Flash + smoke test

- [ ] **Step 1: Start a local FTP server on the dev machine**

Run (PowerShell, project python has the deps):
```
C:\Users\Afonso\.platformio\penv\Scripts\python.exe -m pip install pyftpdlib
C:\Users\Afonso\.platformio\penv\Scripts\python.exe -m pyftpdlib -p 21 -w -d C:\tmp\ftproot -u esp32 -P esp32
```
Note the dev machine's LAN IP (`ipconfig`).

- [ ] **Step 2: Flash the firmware**

Use the `fitadigital-ops` skill / `hardware-deployer` to build and flash COM3. After `pio run` passes, flash directly (user preference: auto-flash on COM3).

- [ ] **Step 3: Configure on-device**

In Settings → SRV → "Upload FTP": set Host = dev-machine IP, Porta = 21, Utilizador = esp32, Senha = esp32, Dir remoto = `/`, Intervalo = 60, toggle "Ativar upload" ON, "Salvar Upload". Then "Sincronizar agora".

- [ ] **Step 4: Verify**

Confirm under `C:\tmp\ftproot` the tree `CICLOS_or_remote_base/YYYY/MM/*.txt` mirrors the SD, file sizes match, and `.ftpjournal` is created on the SD (visible via the SD file browser or the web portal). Capture serial with `crash-analyzer` and confirm `FTPUP` INFO logs report `N enviados, N verificados, 0 falhas`.

### Task F2: Incremental + verification behavior

- [ ] **Step 1: No-op pass**

Trigger "Sincronizar agora" again with no SD changes. Expect `FTPUP` log `0 enviados` (journal suppresses re-upload).

- [ ] **Step 2: Size-change re-upload**

Append a line to today's `/CICLOS/YYYY/MM/YYYYMMDD.txt` (let the RS485 sender push one cycle line, or edit via the portal). Trigger sync. Expect exactly that file re-uploaded and re-verified; others skipped.

- [ ] **Step 3: Verification-gate rejection**

Stop the FTP server mid-pass (or point Host at a server that truncates). Confirm the affected file is NOT recorded in the journal (its `relpath` absent / unchanged) and a `Verif. falhou` WARN is logged, and that it re-uploads cleanly on the next pass once the server is restored.

### Task F3: RS485-capture continuity soak

- [ ] **Step 1: Run the soak**

Start the RS485 sender continuously (per the RS485 sender scripts memory: use `--count 99999`, default baud 9600) so cycle lines are written to the SD throughout. Set the FTP interval to 60 s and let it run ≥30 min while passes repeat.

- [ ] **Step 2: Verify no capture loss / no instability**

Confirm with `crash-analyzer`: 0 panics, heap stable (no downward drift across passes), and the captured cycle file line count matches the number of lines the sender emitted (no dropped lines during uploads). This is the core acceptance criterion — capture must never stall.

- [ ] **Step 3: Final commit / version bump**

If validation passes, bump `FITADIGITAL_VERSION` in `platformio.ini` and add the changelog/TODO entry (use `firmware-coder` for the version bump per project convention).

```bash
git add platformio.ini TODO.md
git commit -m "release: FTP-upload client validated (vX.YZ)"
```

---

## Self-Review Notes

- **Spec coverage:** periodic+manual trigger (C2 task loop + `request_now`/D1); size-only journal (A2/A3 + C5); `/CICLOS` recursive mirror (C5 `scan_dir` + `remote_dir_components`); RS485-safe chunked upload (C3); SIZE verification gate before journal commit (C4 + C5); config in NVS+SD mirror (B1/B2); UI + manual button (D1/D2); supervisor — see note below.
- **Supervisor:** the spec mentions registering `ftp_up` with `service_supervisor`. Deferred from the core plan to avoid coupling the first working version to the watchdog API; the task already self-recovers (each pass reconnects fresh, failures are logged and retried). If desired, add a follow-up task to call `service_supervisor_register` for `ftp_up` with a heartbeat at the end of each pass, mirroring the FTP-server registration in `net_services.cpp:308`. Flagged here rather than silently dropped.
- **Type consistency:** `ftpj::JournalMap` (= `std::map<std::string,long>`) used uniformly; `ftp_stream_file`/`ftp_remote_size`/`ftp_sync_pass`/`scan_dir`/`journal_read`/`journal_write` signatures consistent across C3–C5.
- **Placeholders:** none. Two explicit "Implementation note" callouts (cfg section-id dispatch in B2; `ToastKind`/`accepted_chars` confirmations in D) point at exact existing code to match, not vague TODOs.
