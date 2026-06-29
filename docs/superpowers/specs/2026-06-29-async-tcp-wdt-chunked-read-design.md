# Design — Fix TASK_WDT em async_tcp: leitura SD chunked nos handlers de ficheiro

Data: 2026-06-29
Estado: aprovado (brainstorming)

## Problema

O device reinicia com `prev_reset=TASK_WDT(6)`. O bloco de watchdog capturado no
serial (soak 2026-06-29) mostra, em 2/2 eventos:

```
task_wdt: Task watchdog got triggered. The following tasks/users did not reset the watchdog in time:
task_wdt:  - async_tcp (CPU 1)
Backtrace: ... esp_cpu_wait_for_intr / prvIdleTask   (CPU1 a IDLE = async_tcp BLOQUEADO, fora de CPU)
```

A task `async_tcp` (servidor HTTP do ESPAsyncWebServer) está **subscrita ao Task
Watchdog**. Quando bloqueia mais de 5 s sem alimentar o WDT, a CPU reinicia.

### Causa raiz (gatilho comum)

`sd_access_sync()` / `sd_access_sync_front()` esperam `xSemaphoreTake(done,
portMAX_DELAY)` — **sem timeout**. Qualquer handler que corra na task async_tcp e
chame estas funções bloqueia indefinidamente enquanto o `sd_io` não terminar o
trabalho. Se o `sd_io` demorar >5 s, o async_tcp dispara o TASK_WDT.

### Mecanismos de stall do sd_io (mapeados na investigação)

| # | Mecanismo | Local | Gatilho |
|---|-----------|-------|---------|
| **M2** | `/api/fs/file` e o handler NDJSON lêem o ficheiro **inteiro num único `f.read(blob, body_len)`** dentro de um job sd_access | `web_portal.cpp` (`handle_fs_file` ~L736; NDJSON ~L1364) | Ficheiro grande → leitura longa → async_tcp preso >5 s |
| M1 | FTP server faz `data.write()` (envio TCP) **dentro do contexto sd_io** via `handleFTP()` | `net_services.cpp:380` | Cliente FTP lento → write TCP bloqueia o sd_io |
| M3 | Hang do cartão SD num bloco lento/mau dentro de `f_read` | qualquer `f_read` | Hardware |

Os 2 reboots observados no soak foram disparados por polling agressivo de
diagnóstico (pulls de logs grandes por FTP = M1, e `/api/fs/file` + `/api/logs/tail`
= M2) em simultâneo. Sintomas concorrentes: `/api/logs/tail` vazio, FTP-LIST a
falhar, pílula de monitorização "Desconectado" — todos sinais do sd_io preso.

## Âmbito

**Neste design corrige-se apenas M2** (leitura de ficheiro inteiro nos handlers
async_tcp). Aprovado pelo user.

**Fora de âmbito (documentado, não implementado aqui):**
- **M1** — envio TCP do FTP server dentro do sd_io. É a lib externa SimpleFTPServer;
  fix mais intrusivo (bound de tempo por tick ou mover o transfer p/ fora do sd_io).
- **M3** — hang de cartão SD. Hardware; mitigável só por rede de segurança.
- **Rede de segurança (A)** — subir o Task WDT timeout (5 s → ~15 s) e/ou
  `CONFIG_ASYNC_TCP_USE_WDT=0`. Garantiria zero-reboot contra M1/M3 transitórios,
  mas não foi escolhida agora. Candidata a follow-up se M1/M3 reaparecerem.

## Solução (M2): preload chunked + reset do WDT

Chunkar a leitura **não basta** sozinho — o handler corre na task async_tcp; se o
loop de preload demorar >5 s no total, o WDT dispara na mesma. A correção combina
duas coisas: **chunks pequenos** (justiça do sd_io) **+ `esp_task_wdt_reset()` entre
chunks** (alimenta o WDT do async_tcp durante o preload).

### Antes (padrão actual, ambos os handlers)

```cpp
sd_access_sync_front([&]() {
    f = SD.open(path);
    f.read(blob, body_len);   // 1 leitura gigante -> bloqueio longo do async_tcp
    f.close();
});
```

### Depois

```cpp
constexpr size_t kReadChunk = 8192;            // 8 KB por job sd_access
bool open_ok = false;
sd_access_sync_front([&]() { f = SD.open(path); open_ok = (bool)f; /* + seek range se aplicável */ });
if (!open_ok) { /* 500/404 + free(blob); return; }

size_t off = 0;
bool read_ok = true;
while (off < body_len) {
    const size_t want = min(kReadChunk, body_len - off);
    size_t got = 0;
    sd_access_sync_front([&]() { got = f.read(blob + off, want); });
    if (got == 0) { read_ok = false; break; }   // EOF inesperado / erro
    off += got;
    esp_task_wdt_reset();                        // corre na task async_tcp -> alimenta o WDT
}
sd_access_sync_front([&]() { f.close(); });
if (!read_ok || off != body_len) { /* erro: free(blob); 500; return; }
```

- `f` aberto antes do loop, fechado depois; **todas as operações sobre `f`
  continuam em contexto sd_io** (dentro dos lambdas `sd_access_sync_front`).
- Cada job sd_access é curto → o sd_io intercala outros jobs (UI, log, ticks) entre
  chunks → ninguém fica esfomeado.
- `esp_task_wdt_reset()` entre chunks → o async_tcp alimenta o WDT durante todo o
  preload → **nunca >5 s preso**, independente do tamanho do ficheiro.
- **Inalterado:** alocação do blob PSRAM, o `beginResponse` com o callback de
  streaming (lê de RAM, não toca SD), o `onDisconnect` que liberta o blob, e (no
  handler NDJSON) o mutex + breadcrumb existentes.

### Handlers afectados

1. `handle_fs_file` em `src/web_portal/web_portal.cpp` (~L632, leitura ~L704–746).
2. Handler de download NDJSON gémeo (~L1263–1392, leitura ~L1347–1369).

Ambos partilham o mesmo padrão "preload PSRAM blob"; ambos recebem o mesmo loop
chunked + `esp_task_wdt_reset()`.

### Constante de chunk

`kReadChunk = 8192` (8 KB). Compromisso: pequeno o suficiente para um job sd_access
ser rápido (≪5 s mesmo em cartão lento por bloco), grande o suficiente para poucas
iterações (487 KB ≈ 60 chunks). Definir como `static constexpr` local ou partilhada
entre os 2 handlers.

## Componentes / fronteiras

- **Sem novas interfaces.** Usa o `sd_access_sync_front()` existente e
  `esp_task_wdt_reset()` (já disponível via `esp_task_wdt.h`).
- Confina-se a `web_portal.cpp`. Nenhuma alteração a `sd_access`, `net_services`,
  ou à lib FTP.
- Opcional (limpeza): extrair o loop de preload chunked para uma função helper
  estática `static bool preload_file_chunked(File&, uint8_t* blob, size_t len)`
  reutilizada pelos 2 handlers, para evitar duplicação. Decidir no plano.

## Tratamento de erros

- Falha de `SD.open` → resposta de erro existente (404/500) + `free(blob)`.
- `f.read` devolve 0 antes de `body_len` (EOF/erro) → abortar loop, `free(blob)`,
  responder 500. (Hoje o código assume leitura completa; manter ou melhorar o
  check de `r != body_len` já existente.)
- Caminhos de erro libertam o blob PSRAM e fecham `f` (preservar a semântica actual).

## Verificação

1. **Build** PlatformIO `esp32-s3-touch-lcd-4_3b` sem erros, flash COM3.
2. **Funcional:** descarregar via `/api/fs/file` um ficheiro grande (ex. `fdigi.log`
   ~480 KB e um `.txt`/`.ndjson` de ciclos) → conteúdo idêntico byte-a-byte ao SD
   (comparar com pull FTP/cópia local). NDJSON do portal abre na UI.
3. **Anti-regressão WDT:** descarregar repetidamente o ficheiro grande por
   `/api/fs/file` em paralelo com tráfego (vários pedidos seguidos) → `boot_count`
   **não** incrementa; sem `Task watchdog got triggered` no serial.
4. **Observação:** correr captura serial (com3_log) durante o teste de download
   repetido; confirmar ausência do bloco TASK_WDT do async_tcp.
   Nota: abrir COM3 reseta a placa (pulso RTS) — contabilizar 1 reboot de arme.
5. **Heap:** sem leak de blob PSRAM (download N vezes → `psram_free` estável; o
   `onDisconnect` liberta).

## Notas de implementação / gotchas

- `esp_task_wdt_reset()` só alimenta o WDT da task **corrente** (async_tcp, que corre
  o handler) — correto aqui.
- Não chamar `esp_task_wdt_reset()` dentro do lambda sd_access (correria no contexto
  sd_io, não no async_tcp). Chamar **no handler, entre chunks**.
- Manter `sd_access_sync_front` (não `sync`) — coerente com o resto dos handlers web.
- Confirmar include de `esp_task_wdt.h` em `web_portal.cpp`.
