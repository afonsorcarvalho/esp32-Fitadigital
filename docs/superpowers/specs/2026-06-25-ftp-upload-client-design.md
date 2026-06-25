# FTP-Upload Client — Design Spec

**Date:** 2026-06-25
**Status:** Approved (design), pending implementation plan
**Module:** `ftp_upload.cpp` / `ftp_upload.h` (new)

## Purpose

Push captured cycle files from the device SD card up to a remote FTP server.
Keep a journal of what was already uploaded so each sync pass only transfers
files that are new or changed. Verify each file landed on the server before
marking it synced.

This is an FTP **client** (device pushes out). It is independent of the
existing FTP **server** (`SimpleFTPServer` in `net_services.cpp`), which serves
the SD card to inbound connections. The two share nothing except the SD card.

## Requirements

- Upload contents of `/CICLOS` recursively (`YYYY/MM/*.txt` + `cycles.ndjson`),
  mirroring the directory tree on the server under a configurable remote base dir.
- Run periodically (configurable interval) **and** on manual trigger from the UI.
- Track per-file state in a journal so only pending/modified files upload.
- Change detection: **file size only** (no mtime — board RTC is unreliable;
  no content hash — logs are append-only so size grows when content changes).
- Never stall the RS485 capture task. Capture is the core product function
  (digital replacement for an autoclave paper printer); losing cycle lines is
  unacceptable.
- Verify each upload: remote file size must equal bytes sent before the file is
  recorded as synced.

## Non-Goals (YAGNI)

- No delta/append upload — a changed daily file is re-STOR'd whole. Files are
  small at 9600 baud, so full re-upload is cheap.
- No FTPS/TLS — board free SRAM (~36 KB) makes `WiFiClientSecure` impractical.
- No content checksum (CRC/MD5) — size gate + TCP integrity is accepted.
- No configurable source directory — `/CICLOS` is hardcoded.

## Architecture

New module `ftp_upload` owning one FreeRTOS task `ftp_up`
(~6 KB stack, priority 1, Arduino core — same class as `net_svc`).

Library: **ESP32FtpClient** (lightweight FTP client over `WiFiClient`,
plain FTP). Added to `platformio.ini` `lib_deps`.

### Task loop

```
ftp_up task:
  forever:
    wait( interval OR manual-trigger notify )
    if !settings.enable: continue
    if WiFi.status() != WL_CONNECTED: log+skip, continue
    if free_heap < HEAP_GUARD: log+skip, continue
    if !ftpc.connect(host, port, user, pass): log WARN, continue
    sync_pass()
    ftpc.disconnect()
    supervisor_heartbeat()
```

### sync_pass()

1. Load journal from `/CICLOS/.ftpjournal` into an in-RAM map
   `relpath -> size` (one `sd_access_sync` read).
2. Walk `/CICLOS` recursively (directory listing via `sd_access_sync` jobs).
   Skip the journal file itself (leading-dot hidden file).
3. For each regular file:
   - `cur_size = file size`
   - `j = journal[relpath]`
   - upload needed if `j` absent OR `j != cur_size`.
4. For each file needing upload: `ensure_remote_dirs()` then `upload_file()`.
5. After the pass, if the in-RAM journal changed, flush it whole to
   `/CICLOS/.ftpjournal` once (temp file + rename for atomicity) via
   `sd_access_sync`. One write per pass, not per file — avoids sd_io churn.

### upload_file(relpath, cur_size) — chunked, RS485-safe

```
remotePath = remote_base_dir + "/" + relpath        (forward slashes)
ftpc.initFile("Type I")           # binary
ftpc.newFile(remotePath)          # STOR
sent = 0
open SD file handle for read (serialized)
loop:
  n = read up to CHUNK (~2 KB) from SD via sd_access_sync   # serialized job
  if n == 0: break
  ftpc.writeData(buf, n)          # network send, OUTSIDE sd_io context
  sent += n
ftpc.closeFile()
# --- verification gate ---
remote_size = ftpc.size(remotePath)     # FTP SIZE command (RFC 3659)
if sent == cur_size AND remote_size == sent:
    journal[relpath] = cur_size         # commit (in RAM; flushed at pass end)
    log INFO
else:
    # leave journal untouched -> retried next pass
    log WARN (sent, cur_size, remote_size)
```

The key property: each SD read is a small serialized job through the `sd_io`
queue. Between chunks, `sd_io` is free to service RS485 line writes. Network
latency happens in the `ftp_up` task, never holding the SD bus. Capture
continues throughout the upload.

> **Implementation note:** verify ESP32FtpClient exposes a `SIZE`/`size()`
> method and a manual `writeData` path (`initFile`/`newFile`/`writeData`/
> `closeFile`). If `size()` is absent, send the raw `SIZE` command on the
> control socket and parse the `213 <n>` reply. Confirm exact method names
> against the installed library version during implementation.

### ensure_remote_dirs()

Mirror `YYYY/MM` under remote base dir. Issue `MKD` for each path component,
ignoring "already exists" errors. Cache created dirs in RAM per pass to avoid
redundant MKD calls.

## Journal

- Path: `/CICLOS/.ftpjournal` (hidden, skipped during scan).
- Format: one line per synced file — `relpath|size`
  - e.g. `2026/06/20260625.txt|14320`
  - `relpath` is relative to `/CICLOS`, forward slashes.
- Loaded into RAM at pass start, mutated in memory, flushed whole once per pass.
- A file is synced only after passing the verification gate.
- Missing/corrupt journal -> treated as empty -> everything re-uploads
  (safe; server overwrites via STOR).

## Configuration

New NVS settings (namespace `fdigi`), separate from existing FTP-server creds.
Getter/setter pairs in `app_settings.h`/`app_settings.cpp`, plus entries in
`ParsedSdCfg` for `/fdigi.cfg` mirror (existing settings pattern).

| Setting          | NVS key    | Type   | Default |
|------------------|------------|--------|---------|
| client enable    | `fup_en`   | bool   | false   |
| host             | `fup_h`    | string | ""      |
| port             | `fup_port` | int    | 21      |
| user             | `fup_u`    | string | ""      |
| pass             | `fup_p`    | string | ""      |
| remote base dir  | `fup_rd`   | string | "/"     |
| interval (s)     | `fup_iv`   | int    | 300     |

## UI

New section/tab in the settings screen (`ui_app.cpp`):

- Text inputs: host, port, user, pass, remote base dir, interval.
- Toggle: enable client.
- Button: **"Sincronizar agora"** -> calls `ftp_upload_request_now()`
  (sets task notify/flag for an immediate pass).
- Reuse the existing on-screen-keyboard anchor pattern (matches Wi-Fi/SRV tabs).

## Error Handling

- WiFi not connected -> skip pass, retry next interval. No spin.
- FTP connect failure -> `app_log_feature_write("WARN","FTPUP",...)`, abort pass.
- Per-file upload/verify failure -> journal entry left unchanged (file retried
  next pass), continue with remaining files. WARN log with sizes.
- Low heap (below `HEAP_GUARD` threshold) -> skip pass entirely before connect.
- All network ops bounded by ESP32FtpClient timeouts.

## Integration

- `ftp_upload_init()` called in `setup()` (`app.cpp`) after WiFi begin and
  `sd_access_start_task()`. Spawns `ftp_up` task.
- `ftp_upload_request_now()` — manual trigger, called from UI button.
- Register `ftp_up` with `service_supervisor` (task handle + heartbeat each
  pass) for auto-restart on hang, consistent with other services.

## Testing

- Local FTP server (pyftpdlib / FileZilla Server) on dev machine.
- Verify: directory tree mirrored correctly under remote base dir.
- Verify: size-change re-upload (append to a daily file, confirm re-STOR).
- Verify: journal correctness across reboots (no re-upload of unchanged files).
- Verify: SIZE gate rejects a truncated upload (simulate, confirm retry).
- Verify: WiFi drop mid-upload -> clean abort + retry next pass.
- **Soak:** run RS485 sender continuously during repeated upload passes;
  confirm zero lost cycle lines and stable heap (no capture stall).
