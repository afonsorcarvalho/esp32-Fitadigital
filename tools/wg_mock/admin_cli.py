"""
Admin CLI — simulates admin scanning the QR code.

Usage:
  python admin_cli.py activate <code>
  python admin_cli.py pending
"""

import os
import sys
import requests

SERVER = os.environ.get("WG_SERVER", "http://localhost:5000")


def cmd_activate(code: str) -> None:
    resp = requests.post(f"{SERVER}/api/activate/{code}", timeout=10)
    if resp.status_code == 200:
        data = resp.json()
        print(f"[ok] Device activated. Assigned IP: {data['assigned_ip']}")
    elif resp.status_code == 410:
        print("[error] Code expired.")
    elif resp.status_code == 409:
        print("[error] Already activated.")
    elif resp.status_code == 404:
        print("[error] Unknown code.")
    else:
        print(f"[error] {resp.status_code}: {resp.text}")


def cmd_pending() -> None:
    resp = requests.get(f"{SERVER}/api/pending", timeout=10)
    resp.raise_for_status()
    items = resp.json()
    if not items:
        print("No enrollments.")
        return
    for item in items:
        print(f"  {item['code']}  device={item['device_id']}  state={item['state']}  expires_in={item['expires_in']}s")


def main() -> None:
    args = sys.argv[1:]
    if not args:
        print(__doc__)
        sys.exit(1)

    cmd = args[0]
    if cmd == "activate":
        if len(args) < 2:
            print("Usage: python admin_cli.py activate <code>")
            sys.exit(1)
        cmd_activate(args[1])
    elif cmd == "pending":
        cmd_pending()
    else:
        print(f"Unknown command: {cmd}")
        sys.exit(1)


if __name__ == "__main__":
    main()
