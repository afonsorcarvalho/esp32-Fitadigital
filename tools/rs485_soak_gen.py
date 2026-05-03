#!/usr/bin/env python3
"""Gera tráfego RS485 para soak da FitaDigital.

Por defeito: a cada 60 s envia uma rajada de 5 linhas de 35 caracteres
(alfanumericos + espaco), terminadas em '\n'. Pára por Ctrl+C ou ao atingir
--max-hours. Regista cada linha enviada em --log para auditoria posterior.

Baud por defeito: 9600 (alinhado com cycles_rs485 init no firmware actual).
Se o ESP32 estiver configurado para outra taxa, passar via --baud.
"""

import argparse
import datetime
import random
import string
import sys
import time

import serial


def random_line(length: int) -> str:
    chars = string.ascii_letters + string.digits + " "
    return "".join(random.choice(chars) for _ in range(length))


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", default="COM7")
    ap.add_argument("--baud", type=int, default=9600)
    ap.add_argument("--interval", type=float, default=60.0,
                    help="segundos entre rajadas")
    ap.add_argument("--lines", type=int, default=5,
                    help="linhas por rajada")
    ap.add_argument("--length", type=int, default=35,
                    help="caracteres por linha (sem terminador)")
    ap.add_argument("--max-hours", type=float, default=48.0)
    ap.add_argument("--log", default=None,
                    help="ficheiro para registar cada linha enviada (recomendado)")
    args = ap.parse_args()

    sp = None
    for attempt in range(8):
        try:
            sp = serial.Serial(args.port, args.baud, timeout=0.5)
            if attempt > 0:
                print(f"[gen] open OK na tentativa {attempt + 1}", flush=True)
            break
        except serial.SerialException as exc:
            print(f"[gen] open {args.port} @ {args.baud} attempt {attempt + 1}/8: {exc}",
                  file=sys.stderr, flush=True)
            time.sleep(2.0)
    if sp is None:
        print(f"[gen] FATAL: nao foi possivel abrir {args.port} @ {args.baud} apos 8 tentativas",
              file=sys.stderr, flush=True)
        return 2

    print(f"[gen] {args.port} @ {args.baud} | rajada {args.lines}x{args.length} chars "
          f"a cada {args.interval}s | max {args.max_hours}h", flush=True)
    if args.log:
        print(f"[gen] log -> {args.log}", flush=True)

    log_f = open(args.log, "a", encoding="utf-8", buffering=1) if args.log else None
    started = time.time()
    burst = 0
    try:
        while True:
            elapsed_h = (time.time() - started) / 3600.0
            if elapsed_h >= args.max_hours:
                print(f"[gen] max_hours {args.max_hours} atingido — paragem normal",
                      flush=True)
                break

            burst += 1
            ts = datetime.datetime.now().isoformat(timespec="seconds")
            for i in range(args.lines):
                line = random_line(args.length)
                payload = (line + "\n").encode("ascii")
                sp.write(payload)
                sp.flush()
                if log_f is not None:
                    log_f.write(f"{ts}\tb{burst}.l{i + 1}\t{line}\n")
                # pequena folga entre linhas da mesma rajada para dar tempo ao
                # firmware de processar (LF dispara commit + I/O SD)
                time.sleep(0.05)
            print(f"[gen] burst {burst:>5} ({args.lines} linhas) {ts}", flush=True)

            time.sleep(args.interval)
    except KeyboardInterrupt:
        print("[gen] interrupcao pelo utilizador", flush=True)
    finally:
        if log_f is not None:
            log_f.close()
        sp.close()

    return 0


if __name__ == "__main__":
    sys.exit(main())
