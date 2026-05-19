#!/usr/bin/env python3
"""Smoke E2E cycle_detector: OPERACAO + N dummy lines + FIM CICLO numa unica abertura COM.

Evita workaround re-open COM8 (adapter trava entre invocacoes).

Uso: python rs485_smoke_cycle.py --port COM8 --baud 9600 --dummy 3 --interval 1.0
"""
import argparse
import datetime
import sys
import time

import serial


def send(sp, log_fh, payload: str) -> None:
    ts = datetime.datetime.now().isoformat(timespec="seconds")
    line = f"[{ts}] {payload}\n"
    sp.write(line.encode("ascii", errors="replace"))
    sp.flush()
    print(f"  TX: {line.strip()}", flush=True)
    if log_fh:
        log_fh.write(line)
        log_fh.flush()


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", default="COM8")
    ap.add_argument("--baud", type=int, default=9600)
    ap.add_argument("--dummy", type=int, default=3)
    ap.add_argument("--interval", type=float, default=1.0)
    ap.add_argument("--start", default="OPERACAO ciclo=smoke status=START temperatura=121C")
    ap.add_argument("--end", default="FIM CICLO ciclo=smoke status=DONE")
    ap.add_argument("--log", default=None)
    args = ap.parse_args()

    sp = serial.Serial(args.port, args.baud, timeout=0.5)
    print(f"[smoke] open {args.port}@{args.baud}", flush=True)

    log_fh = open(args.log, "a", encoding="utf-8") if args.log else None
    try:
        send(sp, log_fh, args.start)
        time.sleep(args.interval)
        for i in range(args.dummy):
            send(sp, log_fh, f"DADO_PROCESSO seq={i+1:03d} temp=121C press=2.1bar")
            time.sleep(args.interval)
        send(sp, log_fh, args.end)
    finally:
        sp.close()
        if log_fh:
            log_fh.close()
    print("[smoke] done", flush=True)
    return 0


if __name__ == "__main__":
    sys.exit(main())
