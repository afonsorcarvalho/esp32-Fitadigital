#!/usr/bin/env python3
"""Envia linhas RS485 contendo uma keyword (default OPERACAO).

Uso: python rs485_send_keyword.py --port COM7 --baud 9600 --count 30 \
                                  --interval 2.0 --keyword OPERACAO --log out.txt
"""
import argparse
import datetime
import sys
import time

import serial


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", default="COM7")
    ap.add_argument("--baud", type=int, default=9600)
    ap.add_argument("--count", type=int, default=30)
    ap.add_argument("--interval", type=float, default=2.0)
    ap.add_argument("--keyword", default="OPERACAO")
    ap.add_argument("--log", default=None)
    args = ap.parse_args()

    sp = serial.Serial(args.port, args.baud, timeout=0.5)
    print(f"[send] open {args.port}@{args.baud} keyword={args.keyword}", flush=True)

    log_fh = open(args.log, "a", encoding="utf-8") if args.log else None
    try:
        for i in range(args.count):
            ts = datetime.datetime.now().isoformat(timespec="seconds")
            line = f"[{ts}] {args.keyword} ciclo={i+1:03d} status=OK temperatura=121C\n"
            sp.write(line.encode("ascii", errors="replace"))
            sp.flush()
            print(f"  sent {i+1}/{args.count}: {line.strip()}", flush=True)
            if log_fh:
                log_fh.write(line)
                log_fh.flush()
            if i + 1 < args.count:
                time.sleep(args.interval)
    finally:
        sp.close()
        if log_fh:
            log_fh.close()
    print("[send] done", flush=True)
    return 0


if __name__ == "__main__":
    sys.exit(main())
