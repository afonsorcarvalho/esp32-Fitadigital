#!/usr/bin/env python3
"""WiFi self-healing stress orchestrator — drives POST /api/wifi/stress.

Sequence per --cycles N: [(35, "SOFT"), (305, "HARD")] * N
  SOFT: down_s=35  -> wifi_keepalive_tick T1 (30s) fires soft reconnect
  HARD: down_s=305 -> T1 fires, then T2 (5min) fires hard esp_wifi_stop/start

Per cycle:
  1. GET /api/health -> uptime_before
  2. POST /api/wifi/stress?down_s=N (Basic admin:<pin>)
  3. Sleep N + 5s
  4. Poll /api/health every 5s up to 300s extra; first 200 -> recovery_s
  5. Sleep 60s gap

PASS:
  - 0 reboots (uptime_after >= uptime_before + N)
  - recovery_s < (N + 60s)   (full window + 60s recovery slack)
FAIL: any reboot or recovery missed.

CSV columns: cycle,label,down_s,disconnect_at_iso,recovery_s,
             uptime_before,uptime_after,reboot_detected,pass
"""
from __future__ import annotations

import argparse
import csv
import datetime as _dt
import sys
import time

import requests
from requests.auth import HTTPBasicAuth


DEFAULT_IP = "192.168.0.197"
DEFAULT_USER = "admin"
DEFAULT_PIN = "0000"


def now_iso() -> str:
    return _dt.datetime.now(_dt.timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")


def get_health(ip: str, timeout: float = 5.0) -> dict | None:
    """Return parsed /api/health JSON, or None if unreachable / non-200."""
    try:
        r = requests.get(f"http://{ip}/api/health", timeout=timeout)
        if r.status_code == 200:
            return r.json()
    except (requests.RequestException, ValueError):
        pass
    return None


def post_stress(ip: str, down_s: int, auth: HTTPBasicAuth) -> dict | None:
    """POST stress endpoint. Returns response JSON or None on failure."""
    try:
        r = requests.post(
            f"http://{ip}/api/wifi/stress",
            params={"down_s": down_s},
            auth=auth,
            timeout=8.0,
        )
        if r.status_code == 200:
            return r.json()
        print(f"[stress] POST returned {r.status_code}: {r.text[:200]}",
              file=sys.stderr, flush=True)
    except requests.RequestException as e:
        print(f"[stress] POST exception: {e}", file=sys.stderr, flush=True)
    return None


def run_cycle(cycle_idx: int, label: str, down_s: int, ip: str,
              auth: HTTPBasicAuth, max_recover_s: int) -> dict:
    """Execute one stress cycle and return result dict."""
    print(f"\n=== cycle {cycle_idx} {label} down_s={down_s} ===", flush=True)

    # 1. uptime before
    h0 = get_health(ip)
    if h0 is None:
        print(f"[cycle {cycle_idx}] device unreachable BEFORE stress — aborting",
              file=sys.stderr, flush=True)
        return {
            "cycle": cycle_idx, "label": label, "down_s": down_s,
            "disconnect_at_iso": now_iso(), "recovery_s": -1,
            "uptime_before": -1, "uptime_after": -1,
            "reboot_detected": True, "pass": False,
        }
    uptime_before = int(h0.get("uptime_s", 0))
    print(f"[cycle {cycle_idx}] uptime_before={uptime_before}", flush=True)

    # 2. POST stress
    disconnect_at_iso = now_iso()
    disconnect_at = time.time()
    resp = post_stress(ip, down_s, auth)
    if resp is None:
        print(f"[cycle {cycle_idx}] POST failed — aborting cycle",
              file=sys.stderr, flush=True)
        return {
            "cycle": cycle_idx, "label": label, "down_s": down_s,
            "disconnect_at_iso": disconnect_at_iso, "recovery_s": -1,
            "uptime_before": uptime_before, "uptime_after": -1,
            "reboot_detected": False, "pass": False,
        }
    print(f"[cycle {cycle_idx}] POST OK: {resp}", flush=True)

    # 3. sleep window + small slack
    sleep_s = down_s + 5
    print(f"[cycle {cycle_idx}] sleeping {sleep_s}s through window", flush=True)
    time.sleep(sleep_s)

    # 4. poll /api/health up to max_recover_s extra
    recovery_s = -1
    uptime_after = -1
    elapsed_extra = 0
    while elapsed_extra <= max_recover_s:
        h = get_health(ip, timeout=4.0)
        if h is not None:
            uptime_after = int(h.get("uptime_s", 0))
            recovery_s = int(time.time() - disconnect_at)
            print(f"[cycle {cycle_idx}] recovered: recovery_s={recovery_s} "
                  f"uptime_after={uptime_after}", flush=True)
            break
        time.sleep(5)
        elapsed_extra += 5

    # 5. analysis
    if recovery_s < 0:
        print(f"[cycle {cycle_idx}] TIMEOUT — no recovery within "
              f"{sleep_s + max_recover_s}s", flush=True)
        reboot = True
        passed = False
    else:
        expected_min_uptime = uptime_before + recovery_s - 10  # -10s slack
        reboot = uptime_after < expected_min_uptime
        pass_recovery = recovery_s < (down_s + 60)
        passed = (not reboot) and pass_recovery
        print(f"[cycle {cycle_idx}] reboot={reboot} pass_recovery="
              f"{pass_recovery} -> {'PASS' if passed else 'FAIL'}", flush=True)

    return {
        "cycle": cycle_idx, "label": label, "down_s": down_s,
        "disconnect_at_iso": disconnect_at_iso,
        "recovery_s": recovery_s, "uptime_before": uptime_before,
        "uptime_after": uptime_after, "reboot_detected": reboot,
        "pass": passed,
    }


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--ip", default=DEFAULT_IP)
    ap.add_argument("--user", default=DEFAULT_USER)
    ap.add_argument("--pin", default=DEFAULT_PIN, help="device settings PIN")
    ap.add_argument("--cycles", type=int, default=3,
                    help="num (SOFT,HARD) pairs (default 3 -> 6 cycles total)")
    ap.add_argument("--gap-s", type=int, default=60,
                    help="sleep between cycles (default 60s)")
    ap.add_argument("--max-recover-s", type=int, default=300,
                    help="extra wait after window for recovery (default 300s)")
    ap.add_argument("--out", default=None,
                    help="CSV output path (default logs/wifi_stress_<ts>.csv)")
    args = ap.parse_args()

    if args.out is None:
        ts = _dt.datetime.now().strftime("%Y%m%d-%H%M%S")
        args.out = f"logs/wifi_stress_{ts}.csv"

    auth = HTTPBasicAuth(args.user, args.pin)

    print(f"[run] target={args.ip} cycles={args.cycles} gap={args.gap_s}s "
          f"max_recover={args.max_recover_s}s out={args.out}", flush=True)

    # connectivity sanity
    h0 = get_health(args.ip)
    if h0 is None:
        print(f"[run] FATAL: device {args.ip} unreachable at start",
              file=sys.stderr, flush=True)
        return 2
    print(f"[run] initial uptime_s={h0.get('uptime_s')}", flush=True)

    schedule = []
    for _ in range(args.cycles):
        schedule.append((35, "SOFT"))
        schedule.append((305, "HARD"))

    results = []
    fields = ["cycle", "label", "down_s", "disconnect_at_iso",
              "recovery_s", "uptime_before", "uptime_after",
              "reboot_detected", "pass"]
    with open(args.out, "w", newline="", encoding="utf-8") as f:
        w = csv.DictWriter(f, fieldnames=fields)
        w.writeheader()
        for i, (down_s, label) in enumerate(schedule, 1):
            res = run_cycle(i, label, down_s, args.ip, auth,
                            args.max_recover_s)
            results.append(res)
            w.writerow(res)
            f.flush()
            if i < len(schedule):
                print(f"[run] gap {args.gap_s}s before next cycle", flush=True)
                time.sleep(args.gap_s)

    # summary
    total = len(results)
    passed = sum(1 for r in results if r["pass"])
    failed = total - passed
    reboots = sum(1 for r in results if r["reboot_detected"])
    max_rec = max((r["recovery_s"] for r in results
                   if r["recovery_s"] >= 0), default=-1)

    print("\n" + "=" * 60, flush=True)
    print(f"SUMMARY: {passed}/{total} PASS ({failed} FAIL)", flush=True)
    print(f"  reboots: {reboots}", flush=True)
    print(f"  max recovery_s: {max_rec}", flush=True)
    print(f"  csv: {args.out}", flush=True)
    print("=" * 60, flush=True)

    return 0 if failed == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
