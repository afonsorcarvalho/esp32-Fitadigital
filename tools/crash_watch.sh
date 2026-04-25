#!/usr/bin/env bash
# Monitor log de captura serial. A cada INTERVAL segundos, procura markers de
# crash/reboot/wdt no log. Se encontrar, escreve em ALERT_FILE e sai. Sai
# tambem apos MAX_SECONDS sem achar nada.
#
# Uso: crash_watch.sh <log_file> <alert_file> [interval=600] [max_seconds=7200]

LOG="${1:?log file required}"
ALERT="${2:?alert file required}"
INTERVAL="${3:-600}"
MAX="${4:-7200}"

PATTERN='Guru Meditation|Backtrace:|LoadProhibited|StoreProhibited|IllegalInstruction|abort\(\)|assert failed|Stack canary|stack smash|Task watchdog|task_wdt|wdt reset|rst:0x|ESP-ROM:esp32s3|heap_caps_alloc failed|Out of memory'

# Baseline: ignorar tudo o que ja' existe no log (boot inicial tem rst:/ESP-ROM:
# normais). Matches so contam para bytes adicionados a partir de agora.
BASELINE=$(wc -c < "$LOG" 2>/dev/null || echo 0)
BASELINE=${BASELINE// /}

elapsed=0
while [ $elapsed -lt $MAX ]; do
  sleep "$INTERVAL"
  elapsed=$((elapsed + INTERVAL))
  if [ ! -f "$LOG" ]; then
    continue
  fi
  hits=$(tail -c +$((BASELINE + 1)) "$LOG" | grep -E "$PATTERN" | head -n 40)
  if [ -n "$hits" ]; then
    {
      echo "=== CRASH WATCH HIT at ${elapsed}s ==="
      echo "log: $LOG"
      echo "timestamp: $(date -Iseconds)"
      echo "--- last matching lines ---"
      echo "$hits"
      echo "--- last 40 lines of log ---"
      tail -n 40 "$LOG"
    } > "$ALERT"
    exit 0
  fi
done

{
  echo "=== CRASH WATCH STABLE at ${elapsed}s ==="
  echo "log: $LOG"
  echo "timestamp: $(date -Iseconds)"
  echo "size_bytes: $(wc -c < "$LOG" 2>/dev/null || echo 0)"
  echo "line_count: $(wc -l < "$LOG" 2>/dev/null || echo 0)"
  echo "--- last 20 lines of log ---"
  tail -n 20 "$LOG"
} > "$ALERT"
