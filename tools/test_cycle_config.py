#!/usr/bin/env python3
"""Integration test /api/cycles/config GET/POST + live apply.

Pre-cond: device em 192.168.0.197, admin Digest "0000".
Pos: defaults repostos (OPERACAO/FIM CICLO/900).

Uso: python tools/test_cycle_config.py
"""
import json
import sys

import requests
from requests.auth import HTTPDigestAuth

HOST = "http://192.168.0.197"
AUTH = HTTPDigestAuth("admin", "0000")
CFG_URL = HOST + "/api/cycles/config"
STA_URL = HOST + "/api/cycles/status"
TIMEOUT = 8


def assert_eq(label, got, exp):
    if got != exp:
        print(f"FAIL [{label}]: got={got!r} expected={exp!r}", file=sys.stderr)
        sys.exit(1)
    print(f"  OK [{label}]: {got!r}")


def get_cfg():
    r = requests.get(CFG_URL, auth=AUTH, timeout=TIMEOUT)
    r.raise_for_status()
    return r.json()


def get_status():
    r = requests.get(STA_URL, auth=AUTH, timeout=TIMEOUT)
    r.raise_for_status()
    return r.json()


def post_cfg(start, end, idle):
    body = {"startPattern": start, "endPattern": end, "idleTimeoutS": idle}
    r = requests.post(CFG_URL, json=body, auth=AUTH, timeout=TIMEOUT)
    r.raise_for_status()
    return r.json()


def main():
    print("=== Test 1: GET retorna config shape OK ===")
    cfg = get_cfg()
    print(json.dumps(cfg, indent=2))
    for k in ("startPattern", "endPattern", "idleTimeoutS", "enabled"):
        assert k in cfg, f"missing key: {k}"
    print("  OK shape")

    print("\n=== Test 2: POST custom values + live apply ===")
    resp = post_cfg("TEST_INI", "TEST_FIM", 60)
    print(json.dumps(resp, indent=2))
    assert_eq("ok", resp.get("ok"), True)
    ap = resp.get("applied", {})
    assert_eq("applied.startPattern", ap.get("startPattern"), "TEST_INI")
    assert_eq("applied.endPattern", ap.get("endPattern"), "TEST_FIM")
    assert_eq("applied.idleTimeoutS", ap.get("idleTimeoutS"), 60)
    assert_eq("applied.enabled", ap.get("enabled"), True)

    print("\n=== Test 3: status reflete novo config ===")
    sta = get_status()
    print(json.dumps(sta, indent=2))
    assert_eq("status.start_pattern", sta.get("start_pattern"), "TEST_INI")
    assert_eq("status.end_pattern", sta.get("end_pattern"), "TEST_FIM")
    assert_eq("status.idle_timeout_s", sta.get("idle_timeout_s"), 60)
    assert_eq("status.enabled", sta.get("enabled"), True)

    print("\n=== Test 4: POST start vazio desactiva detector ===")
    resp = post_cfg("", "FIM CICLO", 900)
    assert_eq("disabled.applied.enabled", resp["applied"]["enabled"], False)
    sta = get_status()
    assert_eq("status.enabled disabled", sta.get("enabled"), False)

    print("\n=== Test 5: POST idle clamp [0,86400] ===")
    resp = post_cfg("OPERACAO", "FIM CICLO", 999999)
    assert_eq("idle clamped", resp["applied"]["idleTimeoutS"], 86400)

    print("\n=== Test 6: POST pattern truncate >47 chars ===")
    long_str = "X" * 60
    resp = post_cfg(long_str, "FIM CICLO", 900)
    truncated = resp["applied"]["startPattern"]
    assert_eq("truncate len", len(truncated), 47)

    print("\n=== Reset to defaults ===")
    post_cfg("OPERACAO", "FIM CICLO", 900)
    final = get_cfg()
    assert_eq("reset.startPattern", final["startPattern"], "OPERACAO")
    assert_eq("reset.endPattern", final["endPattern"], "FIM CICLO")
    assert_eq("reset.idleTimeoutS", final["idleTimeoutS"], 900)

    print("\nALL TESTS PASSED")
    return 0


if __name__ == "__main__":
    sys.exit(main())
