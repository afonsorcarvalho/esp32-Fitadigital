#!/usr/bin/env python3
"""Envia linhas RS485 alternando entre uma lista de keywords.

Uso:
  python rs485_send_keywords_alt.py --port COM7 --baud 9600 \
      --keywords OPERACAO,FINAL --interval 60 --max-hours 8 --log soak.log
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
    ap.add_argument("--keywords", default="OPERACAO,FINAL",
                    help="lista separada por virgula")
    ap.add_argument("--interval", type=float, default=60.0,
                    help="segundos entre envios (alterna a cada envio)")
    ap.add_argument("--max-hours", type=float, default=8.0)
    ap.add_argument("--log", default=None)
    args = ap.parse_args()

    keywords = [k.strip() for k in args.keywords.split(",") if k.strip()]
    if not keywords:
        print("FATAL: lista de keywords vazia", flush=True)
        return 2

    sp = serial.Serial(args.port, args.baud, timeout=0.5)
    print(f"[alt] open {args.port}@{args.baud} keywords={keywords} "
          f"interval={args.interval}s max_hours={args.max_hours}", flush=True)

    deadline = time.time() + args.max_hours * 3600.0
    log_fh = open(args.log, "a", encoding="utf-8") if args.log else None
    n = 0
    try:
        while time.time() < deadline:
            kw = keywords[n % len(keywords)]
            ts = datetime.datetime.now().isoformat(timespec="seconds")
            line = f"[{ts}] {kw} ciclo={n+1:04d} status=OK temperatura=121C\n"
            sp.write(line.encode("ascii", errors="replace"))
            sp.flush()
            print(f"  sent #{n+1}: {line.strip()}", flush=True)
            if log_fh:
                log_fh.write(line)
                log_fh.flush()
            n += 1
            time.sleep(args.interval)
    finally:
        sp.close()
        if log_fh:
            log_fh.close()
    print(f"[alt] done — {n} envios", flush=True)
    return 0


if __name__ == "__main__":
    sys.exit(main())
