#!/usr/bin/env python3
"""Soak test cycle_detector + /api/cycles/list endpoint (v2.1.1 fix validation).

Single Python process (COM8 aberto uma vez para evitar adapter trava).
Cada ciclo: OPERACAO + N dummies + FIM CICLO via COM8, depois K stress hits
em /api/cycles/list. Stats finais.

Uso: python soak_cycle_endpoint.py --duration 1200 --cycle-interval 100 \
                                   --port COM8 --baud 9600 --host 192.168.0.197
"""
import argparse
import datetime
import json
import sys
import time
import urllib.parse

import requests
import serial
from requests.auth import HTTPDigestAuth


def now_iso() -> str:
    return datetime.datetime.now().isoformat(timespec="seconds")


def tx(sp, payload: str) -> None:
    line = f"[{now_iso()}] {payload}\n"
    sp.write(line.encode("ascii", errors="replace"))
    sp.flush()


def http_get(url: str, auth) -> tuple[int, bytes, float]:
    t0 = time.time()
    try:
        r = requests.get(url, auth=auth, timeout=8)
        return r.status_code, r.content, time.time() - t0
    except requests.RequestException as e:
        return 0, str(e).encode(), time.time() - t0


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--duration", type=int, default=1200, help="soak seconds")
    ap.add_argument("--cycle-interval", type=int, default=100, help="seconds between cycle starts")
    ap.add_argument("--port", default="COM8")
    ap.add_argument("--baud", type=int, default=9600)
    ap.add_argument("--host", default="192.168.0.197")
    ap.add_argument("--user", default="admin")
    ap.add_argument("--pin", default="0000")
    ap.add_argument("--dummy", type=int, default=3)
    ap.add_argument("--stress-hits", type=int, default=5, help="rapid /list hits between cycles")
    ap.add_argument("--log", default="logs/soak_v211.log")
    args = ap.parse_args()

    auth = HTTPDigestAuth(args.user, args.pin)
    base = f"http://{args.host}"
    list_url = f"{base}/api/cycles/list?year=2026&month=05"
    status_url = f"{base}/api/cycles/status"

    sp = serial.Serial(args.port, args.baud, timeout=0.5)
    print(f"[soak] open {args.port}@{args.baud} duration={args.duration}s interval={args.cycle_interval}s", flush=True)

    import os
    os.makedirs(os.path.dirname(args.log), exist_ok=True)
    log_fh = open(args.log, "a", encoding="utf-8")

    stats = {
        "cycles_sent": 0,
        "list_hits": 0,
        "list_200": 0,
        "list_err": 0,
        "list_max_t": 0.0,
        "list_total_t": 0.0,
        "status_hits": 0,
        "status_200": 0,
        "active_seen": 0,
        "idle_seen": 0,
        "ndjson_max_bytes": 0,
        "errors": [],
    }

    def log(msg: str) -> None:
        line = f"[{now_iso()}] {msg}"
        print(line, flush=True)
        log_fh.write(line + "\n")
        log_fh.flush()

    t_end = time.time() + args.duration
    cycle_idx = 0
    try:
        while time.time() < t_end:
            cycle_idx += 1
            cycle_t0 = time.time()
            log(f"=== cycle {cycle_idx} start ===")

            tx(sp, f"OPERACAO ciclo={cycle_idx:03d} status=START temperatura=121C")
            stats["cycles_sent"] += 1

            time.sleep(1.0)
            sc, _, _ = http_get(status_url, auth)
            stats["status_hits"] += 1
            if sc == 200:
                stats["status_200"] += 1

            for d in range(args.dummy):
                tx(sp, f"DADO_PROCESSO ciclo={cycle_idx:03d} seq={d+1} temp=121C press=2.1bar")
                time.sleep(0.8)

            sc, body, _ = http_get(status_url, auth)
            stats["status_hits"] += 1
            if sc == 200:
                stats["status_200"] += 1
                try:
                    if b'"state":"ACTIVE"' in body:
                        stats["active_seen"] += 1
                except Exception:
                    pass

            tx(sp, f"FIM CICLO ciclo={cycle_idx:03d} status=DONE")
            time.sleep(1.5)

            sc, body, _ = http_get(status_url, auth)
            stats["status_hits"] += 1
            if sc == 200:
                stats["status_200"] += 1
                if b'"state":"IDLE"' in body:
                    stats["idle_seen"] += 1

            for hit in range(args.stress_hits):
                sc, body, dt = http_get(list_url, auth)
                stats["list_hits"] += 1
                stats["list_total_t"] += dt
                if dt > stats["list_max_t"]:
                    stats["list_max_t"] = dt
                if sc == 200:
                    stats["list_200"] += 1
                    if len(body) > stats["ndjson_max_bytes"]:
                        stats["ndjson_max_bytes"] = len(body)
                else:
                    stats["list_err"] += 1
                    stats["errors"].append(f"cyc{cycle_idx} hit{hit+1} sc={sc}")
                time.sleep(0.2)

            log(f"cycle {cycle_idx} done: list_200={stats['list_200']}/{stats['list_hits']} "
                f"ndjson_max={stats['ndjson_max_bytes']}B "
                f"avg_t={stats['list_total_t']/max(1,stats['list_hits']):.3f}s")

            remain = args.cycle_interval - (time.time() - cycle_t0)
            if remain > 0 and time.time() + remain < t_end:
                time.sleep(remain)
    finally:
        sp.close()
        log("=== SOAK END ===")
        log(json.dumps(stats, indent=2))
        log_fh.close()

    return 0


if __name__ == "__main__":
    sys.exit(main())
