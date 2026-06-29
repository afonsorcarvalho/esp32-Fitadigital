# Fix TASK_WDT async_tcp — leitura SD chunked Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Eliminar reboots TASK_WDT da task `async_tcp` causados por leituras SD longas (ficheiro inteiro num só `f.read`) nos handlers `/api/fs/file` e cycles NDJSON.

**Architecture:** Substituir o `f.read(blob, len)` único (que prende a task async_tcp >5 s) por um loop de chunks de 8 KB, cada chunk num job `sd_access_sync_front`, com `esp_task_wdt_reset()` entre chunks (alimenta o WDT do async_tcp durante o preload). Streaming via blob PSRAM e `onDisconnect` ficam inalterados. Um helper partilhado `portal_preload_file_chunked()` evita duplicação entre os 2 handlers.

**Tech Stack:** C++ / Arduino-ESP32 / ESPAsyncWebServer (task async_tcp) / FatFs (SD) / FreeRTOS / `esp_task_wdt`. Build PlatformIO env `esp32-s3-touch-lcd-4_3b`, flash COM3.

**Nota de teste:** o firmware não tem harness de unit test para handlers web (dependem de SD/FreeRTOS/AsyncTCP em hardware). Verificação = build + teste on-device (integridade do download + anti-regressão WDT), conforme a secção Verificação do spec. Não há passos de TDD nativo nesta área.

**Spec:** `docs/superpowers/specs/2026-06-29-async-tcp-wdt-chunked-read-design.md`

---

## File Structure

- **Modify:** `src/web_portal/web_portal.cpp`
  - Adicionar `#include <esp_task_wdt.h>` (em falta).
  - Adicionar helper estático `portal_preload_file_chunked()` antes de `handle_fs_file` (~L632).
  - `handle_fs_file`: substituir o bloco de leitura (L721–746) por chamada ao helper.
  - `handle_cycles_list_get`: substituir o bloco de leitura (L1356–1373) por chamada ao helper.

Nenhum outro ficheiro muda. Sem alterações a `sd_access`, `net_services`, ou libs.

---

## Task 1: Helper de leitura chunked + include

**Files:**
- Modify: `src/web_portal/web_portal.cpp` (include ~L20; helper novo antes de `handle_fs_file` ~L632)

- [ ] **Step 1: Adicionar o include do task watchdog**

Localizar a linha (~L20):
```cpp
#include <esp_heap_caps.h>
```
Adicionar logo a seguir:
```cpp
#include <esp_task_wdt.h>
```

- [ ] **Step 2: Adicionar o helper estático antes de `handle_fs_file`**

Inserir imediatamente antes da linha `static void handle_fs_file(AsyncWebServerRequest *request)` (~L632):

```cpp
/*
 * Pre-carrega [off, off+len) de `path` para `blob`, em chunks de 8 KB, cada
 * chunk num job sd_access_sync_front (contexto sd_io). Entre chunks chama
 * esp_task_wdt_reset() — esta funcao corre na task chamadora (async_tcp, que
 * executa o handler HTTP), logo alimenta o Task WDT do async_tcp durante todo
 * o preload e evita o TASK_WDT mesmo em ficheiros grandes. Os jobs curtos
 * deixam o sd_io intercalar outros trabalhos (UI/log/ticks) entre chunks.
 *
 * Devolve true se leu exactamente `len` bytes. `f` e' aberto/lido/fechado
 * sempre dentro dos lambdas (contexto sd_io); persiste na stack entre chunks.
 */
static bool portal_preload_file_chunked(const char *path, size_t off,
                                        uint8_t *blob, size_t len) {
    static constexpr size_t kReadChunk = 8192U;
    File f;
    bool open_ok = false;
    sd_access_sync_front([&]() {
        f = SD.open(path, FILE_READ);
        if (!f || f.isDirectory()) {
            if (f) {
                f.close();
            }
            return;
        }
        if (off != 0U && !f.seek(static_cast<uint32_t>(off))) {
            f.close();
            return;
        }
        open_ok = true;
    });
    if (!open_ok) {
        return false;
    }

    size_t done = 0;
    bool read_ok = true;
    while (done < len) {
        const size_t want = (len - done < kReadChunk) ? (len - done) : kReadChunk;
        size_t got = 0;
        sd_access_sync_front([&]() { got = f.read(blob + done, want); });
        esp_task_wdt_reset();  /* corre na task async_tcp: alimenta o WDT */
        if (got == 0U) {
            read_ok = false;
            break;
        }
        done += got;
    }

    sd_access_sync_front([&]() {
        if (f) {
            f.close();
        }
    });
    return read_ok && (done == len);
}
```

- [ ] **Step 3: Verificar que compila (build incremental)**

Run: `"C:/Users/Afonso/.platformio/penv/Scripts/pio.exe" run -e esp32-s3-touch-lcd-4_3b`
Expected: `[SUCCESS]` + linhas `RAM:` / `Flash:`. (O helper ainda não é chamado — só verifica que include + assinatura compilam, sem warning de "defined but not used" porque é `static` e será usado nas tasks seguintes; se o compilador queixar de unused nesta task isolada, ignorar — Task 2/3 usam-no. Em alternativa, fazer Task 1+2+3 e só depois build.)

- [ ] **Step 4: Commit**

```bash
git add src/web_portal/web_portal.cpp
git commit -m "feat(web): helper portal_preload_file_chunked (leitura SD chunked + wdt_reset)"
```

---

## Task 2: Ligar `handle_fs_file` ao helper

**Files:**
- Modify: `src/web_portal/web_portal.cpp` (`handle_fs_file`, bloco de leitura ~L721–746)

- [ ] **Step 1: Substituir o bloco de leitura único pelo helper**

Localizar este bloco (dentro de `handle_fs_file`, ~L721–746):
```cpp
    int read_err = 0;
    sd_access_sync_front([&]() {
        File f = SD.open(p.c_str(), FILE_READ);
        if (!f || f.isDirectory()) {
            if (f) {
                f.close();
            }
            read_err = 1;
            return;
        }
        if (!f.seek(static_cast<uint32_t>(body_off))) {
            f.close();
            read_err = 2;
            return;
        }
        const size_t r = f.read(blob, body_len);
        f.close();
        if (r != body_len) {
            read_err = 3;
        }
    });
    if (read_err != 0) {
        heap_caps_free(blob);
        request->send(500, "text/plain", "read failed");
        return;
    }
```
Substituir por:
```cpp
    if (!portal_preload_file_chunked(p.c_str(), body_off, blob, body_len)) {
        heap_caps_free(blob);
        request->send(500, "text/plain", "read failed");
        return;
    }
```

- [ ] **Step 2: Build**

Run: `"C:/Users/Afonso/.platformio/penv/Scripts/pio.exe" run -e esp32-s3-touch-lcd-4_3b`
Expected: `[SUCCESS]`. Sem erros. (`body_off`/`body_len`/`blob`/`p` já estão em scope no handler.)

- [ ] **Step 3: Commit**

```bash
git add src/web_portal/web_portal.cpp
git commit -m "fix(web): /api/fs/file usa leitura chunked (evita TASK_WDT async_tcp)"
```

---

## Task 3: Ligar `handle_cycles_list_get` (NDJSON) ao helper

**Files:**
- Modify: `src/web_portal/web_portal.cpp` (`handle_cycles_list_get`, bloco de leitura ~L1356–1373)

- [ ] **Step 1: Substituir o bloco de leitura único pelo helper**

Localizar este bloco (dentro de `handle_cycles_list_get`, ~L1356–1373):
```cpp
    int read_err = 0;
    sd_access_sync_front([&]() {
        File f = SD.open(path, FILE_READ);
        if (!f || f.isDirectory()) {
            if (f) f.close();
            read_err = 1;
            return;
        }
        const size_t r = f.read(blob, file_sz);
        f.close();
        if (r != file_sz) read_err = 2;
    });
    if (read_err != 0) {
        heap_caps_free(blob);
        release_lock();
        request->send(500, "application/json", "{\"error\":\"read failed\"}");
        return;
    }
```
Substituir por:
```cpp
    if (!portal_preload_file_chunked(path, 0U, blob, file_sz)) {
        heap_caps_free(blob);
        release_lock();
        request->send(500, "application/json", "{\"error\":\"read failed\"}");
        return;
    }
```

Nota: o mutex `s_cycles_list_mtx` e o `panic_breadcrumb` mantêm-se. O preload
chunked corre com o mutex tomado (igual a hoje) — aceitável: agora o async_tcp
alimenta o WDT durante a leitura, logo segurar o mutex mais tempo não causa reboot.

- [ ] **Step 2: Build**

Run: `"C:/Users/Afonso/.platformio/penv/Scripts/pio.exe" run -e esp32-s3-touch-lcd-4_3b`
Expected: `[SUCCESS]`. (`path`/`file_sz`/`blob` já em scope.)

- [ ] **Step 3: Commit**

```bash
git add src/web_portal/web_portal.cpp
git commit -m "fix(web): cycles NDJSON usa leitura chunked (evita TASK_WDT async_tcp)"
```

---

## Task 4: Flash + verificação funcional (integridade)

**Files:** nenhum (deploy + teste on-device)

- [ ] **Step 1: Flash COM3**

Run: `"C:/Users/Afonso/.platformio/penv/Scripts/pio.exe" run -e esp32-s3-touch-lcd-4_3b -t upload --upload-port COM3`
Expected: `[SUCCESS]`, `Hard resetting`. Boot ~14 s. (COM3 livre — nenhum monitor serial aberto.)

- [ ] **Step 2: Confirmar device online**

Run: `curl -s --digest -u admin:0000 --max-time 12 "http://192.168.0.197/api/system/status"`
Expected: JSON com `"fw_ver":"2.24"`, `"sd_mounted":true`, `boot_count` anotado como BASELINE.

- [ ] **Step 3: Download de ficheiro grande via /api/fs/file e comparar byte-a-byte**

Pull HTTP (digest) e pull FTP do MESMO ficheiro, comparar:
```bash
IP=192.168.0.197
curl -s --digest -u admin:0000 --max-time 60 "http://$IP/api/fs/file?path=/fdigi.log" -o /tmp/http_fdigi.log
curl -s --max-time 60 -u esp32:esp32 "ftp://$IP/fdigi.log" -o /tmp/ftp_fdigi.log
cmp /tmp/http_fdigi.log /tmp/ftp_fdigi.log && echo "IDENTICO" || echo "DIFERE"
```
Expected: `IDENTICO` (conteúdo via leitura chunked == ficheiro real). Se FTP falhar (data channel flaky), repetir o pull FTP; usar um `.txt`/`.ndjson` de ciclos como alternativa.

- [ ] **Step 4: Download NDJSON via API e confirmar conteúdo**

Run: `curl -s --digest -u admin:0000 --max-time 30 "http://192.168.0.197/api/cycles/list?year=2026&month=06" | head -3`
Expected: linhas JSON de ciclos (ou vazio se o mês não tem ciclos — então testar um mês com dados, ex. `month=05`). Sem erro 500.

---

## Task 5: Anti-regressão TASK_WDT (o teste que importa)

**Files:** nenhum (teste on-device com captura serial)

- [ ] **Step 1: Armar captura serial COM3 (background)**

Run (background): `"C:/Users/Afonso/.platformio/penv/Scripts/python.exe" tools/com3_log.py "<scratchpad>/wdt_regress_serial.log" 115200 COM3`
Nota: abrir COM3 pulsa RTS → 1 reboot de arme (boot_count +1). Anotar o novo baseline após este reset.

- [ ] **Step 2: Martelar /api/fs/file com o ficheiro grande, repetidamente**

Run:
```bash
IP=192.168.0.197
for i in $(seq 1 30); do
  curl -s --digest -u admin:0000 --max-time 60 "http://$IP/api/fs/file?path=/fdigi.log" -o /dev/null -w "pull $i: HTTP %{http_code} %{size_download}B\n"
done
```
Expected: 30× `HTTP 200`, tamanho consistente. Sem timeouts/erros em cadeia.

- [ ] **Step 3: Confirmar ZERO reboots durante o martelo**

Run: `curl -s --digest -u admin:0000 --max-time 12 "http://192.168.0.197/api/system/status" | grep -o '"boot_count":[0-9]*\|"uptime_s":[0-9]*'`
Expected: `boot_count` == baseline do Step 1 (NÃO incrementou); `uptime_s` coerente com tempo contínuo.

- [ ] **Step 4: Confirmar ausência do bloco TASK_WDT no serial**

Run: `grep -aiE "Task watchdog got triggered|async_tcp \(CPU" "<scratchpad>/wdt_regress_serial.log"`
Expected: SEM matches (vazio). Antes do fix, martelar /api/fs/file disparava `Task watchdog got triggered: - async_tcp (CPU 1)`.

- [ ] **Step 5: Confirmar sem leak de blob PSRAM**

Run: `curl -s --digest -u admin:0000 --max-time 12 "http://192.168.0.197/api/system/services" | grep -o '"psram_free":[0-9]*'`
Expected: `psram_free` estável (próximo do valor pré-martelo); o `onDisconnect` libertou os blobs. Comparar com uma leitura tirada antes do Step 2.

- [ ] **Step 6: Parar a captura serial**

Parar o processo com3_log (TaskStop do background task). Libera COM3.

- [ ] **Step 7: Commit do resultado (se aplicável) + arquivo**

Não há código a commitar aqui. Se o build gerou novo `firmware_versions/FitaDigital_v2.24.bin` (post-script), confirmar que existe. Registar o resultado do soak/teste no TODO.md e remember.

---

## Self-Review

**Spec coverage:**
- M2 fix (leitura chunked + wdt_reset) → Tasks 1–3. ✓
- Aplicado aos 2 handlers (/api/fs/file + NDJSON) → Tasks 2 e 3. ✓
- Streaming PSRAM/onDisconnect inalterados → confirmado (só o bloco de leitura muda). ✓
- CHUNK 8 KB → `kReadChunk = 8192U` no helper. ✓
- Verificação: integridade (Task 4) + anti-regressão WDT por download repetido + boot_count + serial (Task 5). ✓
- M1/M3/rede-de-segurança A: fora de âmbito, não há tasks — correto, alinhado com o spec. ✓

**Placeholder scan:** sem TBD/TODO; todo o código está completo; comandos têm output esperado. ✓

**Type consistency:** `portal_preload_file_chunked(const char*, size_t, uint8_t*, size_t) -> bool` definido na Task 1; chamado com a mesma assinatura na Task 2 (`p.c_str(), body_off, blob, body_len`) e Task 3 (`path, 0U, blob, file_sz`). ✓
