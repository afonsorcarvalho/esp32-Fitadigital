"""Soak runner — captura serial + analise silenciosa + JSON summary.

Reduz token usage vs Monitor streaming events. 1 unica notificacao no fim.

Uso:
  python soak_run.py --label stage1 --duration 1200
  python soak_run.py --label stage3 --duration 1200 --ftp-probe --probe-interval 60
  python soak_run.py --label stage4 --duration 1200 --health-probe --probe-interval 30
  python soak_run.py --label stress --duration 28800 --health-probe --ftp-probe --warmup 30

Output:
  logs/soak-<label>.log         (raw serial capture)
  logs/soak-<label>-summary.json (parsed analysis)
  stdout                         (summary impresso)

Exit code:
  0 = PASS (zero crash, zero reboot, heap drift < 4KB)
  1 = FAIL (crash/reboot/heap drift critical, OR probe fail count > 0)
  2 = config error

Nota --warmup (default 30s): aguarda antes do 1o probe HTTP/FTP para evitar
false-fail no startup race (device ainda em boot WiFi+HTTP, retorna
WinError 10061 connection refused). Capture serial nao espera — comeca
imediatamente para captar boot completo.
"""
import argparse
import datetime as dt
import json
import os
import re
import sys
import threading
import time
import urllib.request
import urllib.error
from urllib.request import HTTPDigestAuthHandler, HTTPPasswordMgrWithDefaultRealm, build_opener
from ftplib import FTP

import serial


HEAP_RE = re.compile(r"\[HEAP\] t=(\d+) int=(\d+) min=(\d+) psram=(\d+)")
BOOT_RE = re.compile(r"BOOT START \| ms=(\d+) \| prev_reset=(\w+)\((\d+)\)")
CRASH_RE = re.compile(r"Guru Meditation|abort\(\)|assert failed|Stack canary|HEAP_GUARD|panic|backtrace|Task watchdog")
WG_HS_RE = re.compile(r"start handshake")
WG_OK_RE = re.compile(r"good handshake")


def capture_serial(port: str, baud: int, out_path: str, stop_evt: threading.Event):
    """Background thread: read serial and append to file. Pulses RTS once to reset device."""
    s = serial.Serial()
    s.port = port
    s.baudrate = baud
    s.timeout = 0.5
    s.rts = False
    s.dtr = False
    s.open()
    try:
        s.rts = True
        s.dtr = False
        time.sleep(0.1)
        s.rts = False
        time.sleep(0.05)
    except Exception:
        pass
    with open(out_path, "wb", buffering=0) as f:
        while not stop_evt.is_set():
            try:
                data = s.read(4096)
            except Exception:
                time.sleep(0.5)
                continue
            if data:
                f.write(data)
    s.close()


def probe_ftp(ip: str, user: str, pwd: str, results: list):
    """FTP probe: connect + LIST + disconnect. Append (ts, ok, msg) to results."""
    ts = time.monotonic()
    try:
        ftp = FTP()
        ftp.connect(ip, timeout=10)
        ftp.login(user, pwd)
        items = ftp.nlst()
        ftp.quit()
        results.append((ts, True, f"items={len(items)}"))
    except Exception as e:
        results.append((ts, False, str(e)[:80]))


def probe_health(ip: str, user: str, pwd: str, results: list):
    """HTTP /api/health probe with Digest auth."""
    ts = time.monotonic()
    url = f"http://{ip}/api/health"
    try:
        mgr = HTTPPasswordMgrWithDefaultRealm()
        mgr.add_password(None, url, user, pwd)
        opener = build_opener(HTTPDigestAuthHandler(mgr))
        req = urllib.request.Request(url)
        r = opener.open(req, timeout=5)
        body = r.read().decode("utf-8")
        results.append((ts, True, body))
    except Exception as e:
        results.append((ts, False, str(e)[:80]))


def probe_wg_status(ip: str, user: str, pwd: str, results: list):
    """HTTP /api/wg/status probe with Digest auth (v1.91+ silent-death telemetry)."""
    ts = time.monotonic()
    url = f"http://{ip}/api/wg/status"
    try:
        mgr = HTTPPasswordMgrWithDefaultRealm()
        mgr.add_password(None, url, user, pwd)
        opener = build_opener(HTTPDigestAuthHandler(mgr))
        req = urllib.request.Request(url)
        r = opener.open(req, timeout=5)
        body = r.read().decode("utf-8")
        try:
            parsed = json.loads(body)
        except Exception:
            parsed = None
        results.append((ts, True, parsed if parsed is not None else body))
    except Exception as e:
        results.append((ts, False, str(e)[:80]))


def probe_loop(probe_fn, kwargs: dict, interval_s: int, stop_evt: threading.Event,
               results: list, warmup_s: int = 0):
    """warmup_s: aguardar antes do 1o probe (skip startup race onde device ainda
    nao terminou WiFi+HTTP boot — typical 10-15s no FitaDigital v2.x).
    Verdict FAIL falso por WinError 10061 connection refused = sintoma classico."""
    if warmup_s > 0:
        for _ in range(warmup_s * 10):
            if stop_evt.is_set():
                return
            time.sleep(0.1)
    while not stop_evt.is_set():
        probe_fn(**kwargs, results=results)
        # Wait interval but allow early exit
        for _ in range(interval_s * 10):
            if stop_evt.is_set():
                return
            time.sleep(0.1)


def analyze_log(log_path: str) -> dict:
    heap_samples = []
    boot_count = 0
    boot_reasons = []
    crash_count = 0
    wg_hs_count = 0
    wg_ok_count = 0
    with open(log_path, "rb") as f:
        for raw in f:
            try:
                line = raw.decode("utf-8", errors="ignore")
            except Exception:
                continue
            m = HEAP_RE.search(line)
            if m:
                heap_samples.append({
                    "t": int(m.group(1)),
                    "int": int(m.group(2)),
                    "min": int(m.group(3)),
                    "psram": int(m.group(4)),
                })
                continue
            m = BOOT_RE.search(line)
            if m:
                boot_count += 1
                boot_reasons.append(f"{m.group(2)}({m.group(3)})")
                continue
            if CRASH_RE.search(line):
                crash_count += 1
            if WG_HS_RE.search(line):
                wg_hs_count += 1
            if WG_OK_RE.search(line):
                wg_ok_count += 1
    heap_summary = None
    if heap_samples:
        first = heap_samples[0]
        last = heap_samples[-1]
        min_seen = min(s["min"] for s in heap_samples)
        heap_summary = {
            "samples": len(heap_samples),
            "int_first": first["int"],
            "int_last": last["int"],
            "int_delta": last["int"] - first["int"],
            "min_first": first["min"],
            "min_last": last["min"],
            "min_seen": min_seen,
            "psram_first": first["psram"],
            "psram_last": last["psram"],
            "psram_delta": last["psram"] - first["psram"],
            "duration_ms": last["t"] - first["t"],
        }
    return {
        "boot_count": boot_count,
        "boot_reasons": boot_reasons,
        "crash_count": crash_count,
        "wg_handshake_starts": wg_hs_count,
        "wg_good_handshakes": wg_ok_count,
        "heap": heap_summary,
    }


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--label", required=True, help="stage label, e.g. stage3")
    ap.add_argument("--duration", type=int, required=True, help="seconds")
    ap.add_argument("--port", default="COM3")
    ap.add_argument("--baud", type=int, default=115200)
    ap.add_argument("--ip", default="192.168.0.197")
    ap.add_argument("--user", default="admin", help="HTTP user (Digest)")
    ap.add_argument("--pwd", default="0000", help="HTTP password / PIN")
    ap.add_argument("--ftp-user", default="esp32")
    ap.add_argument("--ftp-pwd", default="esp32")
    ap.add_argument("--ftp-probe", action="store_true", help="Stage 3: probe FTP each N seconds")
    ap.add_argument("--health-probe", action="store_true", help="Stage 4: probe /api/health each N seconds")
    ap.add_argument("--wg-probe", action="store_true", help="v1.91+: probe /api/wg/status each --wg-interval")
    ap.add_argument("--wg-interval", type=int, default=30, help="WG status probe interval (s)")
    ap.add_argument("--probe-interval", type=int, default=60)
    ap.add_argument("--warmup", type=int, default=30,
                    help="segundos a aguardar antes do 1o probe (skip startup race WinError 10061)")
    args = ap.parse_args()

    os.makedirs("logs", exist_ok=True)
    log_path = f"logs/soak-{args.label}.log"
    summary_path = f"logs/soak-{args.label}-summary.json"

    print(f"[soak] label={args.label} duration={args.duration}s port={args.port}")
    print(f"[soak] log -> {log_path}")

    stop_evt = threading.Event()
    ftp_results = []
    health_results = []
    wg_results = []

    threads = []
    t_serial = threading.Thread(
        target=capture_serial,
        args=(args.port, args.baud, log_path, stop_evt),
        daemon=True,
    )
    t_serial.start()
    threads.append(("serial", t_serial))

    if args.ftp_probe:
        t = threading.Thread(
            target=probe_loop,
            args=(probe_ftp, {"ip": args.ip, "user": args.ftp_user, "pwd": args.ftp_pwd},
                  args.probe_interval, stop_evt, ftp_results, args.warmup),
            daemon=True,
        )
        t.start()
        threads.append(("ftp_probe", t))

    if args.health_probe:
        t = threading.Thread(
            target=probe_loop,
            args=(probe_health, {"ip": args.ip, "user": args.user, "pwd": args.pwd},
                  args.probe_interval, stop_evt, health_results, args.warmup),
            daemon=True,
        )
        t.start()
        threads.append(("health_probe", t))

    if args.wg_probe:
        t = threading.Thread(
            target=probe_loop,
            args=(probe_wg_status, {"ip": args.ip, "user": args.user, "pwd": args.pwd},
                  args.wg_interval, stop_evt, wg_results, args.warmup),
            daemon=True,
        )
        t.start()
        threads.append(("wg_probe", t))

    t0 = time.monotonic()
    try:
        while (time.monotonic() - t0) < args.duration:
            time.sleep(1)
    except KeyboardInterrupt:
        print("[soak] interrupted")
    stop_evt.set()

    # Give serial thread a sec to flush
    time.sleep(1)

    print("[soak] analyzing log ...")
    result = analyze_log(log_path)
    result["label"] = args.label
    result["duration_s"] = args.duration
    result["timestamp"] = dt.datetime.now().isoformat(timespec="seconds")

    if args.ftp_probe:
        ftp_ok = sum(1 for _, ok, _ in ftp_results if ok)
        result["ftp_probe"] = {
            "total": len(ftp_results),
            "ok": ftp_ok,
            "fail": len(ftp_results) - ftp_ok,
            "results": [{"t": round(t, 1), "ok": ok, "msg": m} for t, ok, m in ftp_results],
        }
    if args.health_probe:
        h_ok = sum(1 for _, ok, _ in health_results if ok)
        result["health_probe"] = {
            "total": len(health_results),
            "ok": h_ok,
            "fail": len(health_results) - h_ok,
            "results": [{"t": round(t, 1), "ok": ok, "msg": m[:80]} for t, ok, m in health_results],
        }
    if args.wg_probe:
        wg_ok = sum(1 for _, ok, _ in wg_results if ok)
        # Silent-death analysis: peer_up transitions, last_up_ms_ago growth, RX activity
        peer_up_samples = []
        peer_down_samples = []
        max_last_up_ago = 0
        ever_rx_count = 0
        reapply_first = None
        reapply_last = None
        wifi_hard_max = 0
        wifi_soft_max = 0
        compact = []
        for ts, ok, payload in wg_results:
            if not ok or not isinstance(payload, dict):
                compact.append({"t": round(ts, 1), "ok": False, "msg": str(payload)[:80]})
                continue
            up = bool(payload.get("peer_up"))
            last_up_ago = int(payload.get("last_up_ms_ago", 0))
            last_apply_ago = int(payload.get("last_apply_ms_ago", 0))
            reapply = int(payload.get("reapply_count", 0))
            ever_rx = bool(payload.get("ever_rx"))
            wifi_soft = int(payload.get("wifi_soft_count", 0))
            wifi_hard = int(payload.get("wifi_hard_count", 0))
            if up:
                peer_up_samples.append(ts)
            else:
                peer_down_samples.append(ts)
            if last_up_ago > max_last_up_ago:
                max_last_up_ago = last_up_ago
            if ever_rx:
                ever_rx_count += 1
            if reapply_first is None:
                reapply_first = reapply
            reapply_last = reapply
            if wifi_hard > wifi_hard_max:
                wifi_hard_max = wifi_hard
            if wifi_soft > wifi_soft_max:
                wifi_soft_max = wifi_soft
            compact.append({
                "t": round(ts, 1),
                "ok": True,
                "peer_up": up,
                "last_up_ms_ago": last_up_ago,
                "last_apply_ms_ago": last_apply_ago,
                "reapply": reapply,
                "ever_rx": ever_rx,
            })
        # Silent-death detection: peer_up=false for >= 3 consecutive samples
        # AND last_up_ms_ago > 180000 (REJECT_AFTER_TIME)
        silent_death_streaks = []
        cur_streak = 0
        cur_start = None
        for s in compact:
            if s.get("ok") and not s.get("peer_up"):
                if cur_streak == 0:
                    cur_start = s.get("t")
                cur_streak += 1
                if cur_streak >= 3:
                    pass
            else:
                if cur_streak >= 3:
                    silent_death_streaks.append({
                        "start_t": cur_start,
                        "end_t": s.get("t"),
                        "samples_down": cur_streak,
                    })
                cur_streak = 0
                cur_start = None
        if cur_streak >= 3:
            silent_death_streaks.append({
                "start_t": cur_start,
                "end_t": compact[-1].get("t") if compact else None,
                "samples_down": cur_streak,
                "still_down_at_end": True,
            })
        result["wg_probe"] = {
            "total": len(wg_results),
            "ok": wg_ok,
            "fail": len(wg_results) - wg_ok,
            "peer_up_samples": len(peer_up_samples),
            "peer_down_samples": len(peer_down_samples),
            "max_last_up_ms_ago": max_last_up_ago,
            "ever_rx_samples": ever_rx_count,
            "reapply_first": reapply_first,
            "reapply_last": reapply_last,
            "reapply_delta": (reapply_last - reapply_first) if (reapply_first is not None and reapply_last is not None) else 0,
            "wifi_soft_count_max": wifi_soft_max,
            "wifi_hard_count_max": wifi_hard_max,
            "silent_death_streaks": silent_death_streaks,
            "results": compact,
        }

    # PASS/FAIL verdict
    heap_drift_ok = True
    if result["heap"] and abs(result["heap"]["int_delta"]) > 4096:
        heap_drift_ok = False
    pass_ok = (
        result["boot_count"] <= 1  # initial boot only OK
        and result["crash_count"] == 0
        and heap_drift_ok
    )
    if args.ftp_probe and result.get("ftp_probe", {}).get("fail", 0) > 0:
        pass_ok = False
    if args.health_probe and result.get("health_probe", {}).get("fail", 0) > 0:
        pass_ok = False
    result["verdict"] = "PASS" if pass_ok else "FAIL"

    with open(summary_path, "w", encoding="utf-8") as f:
        json.dump(result, f, indent=2)

    print(json.dumps(result, indent=2))
    print(f"[soak] verdict={result['verdict']} summary={summary_path}")
    sys.exit(0 if pass_ok else 1)


if __name__ == "__main__":
    main()
