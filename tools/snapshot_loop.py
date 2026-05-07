"""Snapshot loop: POST /api/screenshot/take cada N segundos.
Usado p/soak test. Log file por linha JSON com timestamp + http status + file."""
import argparse
import base64
import json
import sys
import time
import urllib.request
import urllib.error


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--host", default="192.168.0.197")
    ap.add_argument("--user", default="admin")
    ap.add_argument("--pass", dest="passwd", default="0000")
    ap.add_argument("--interval", type=float, default=60.0)
    ap.add_argument("--minutes", type=float, default=30.0)
    ap.add_argument("--log", default="logs/snapshot_loop.log")
    args = ap.parse_args()

    auth = base64.b64encode(f"{args.user}:{args.passwd}".encode()).decode()
    url = f"http://{args.host}/api/screenshot/take"
    headers = {"Authorization": f"Basic {auth}", "Content-Type": "application/json"}

    deadline = time.time() + args.minutes * 60.0
    n = 0
    with open(args.log, "w", encoding="utf-8") as f:
        while time.time() < deadline:
            n += 1
            ts = time.strftime("%Y-%m-%dT%H:%M:%S")
            entry = {"ts": ts, "n": n}
            try:
                req = urllib.request.Request(url, data=b"{}", headers=headers, method="POST")
                with urllib.request.urlopen(req, timeout=10) as resp:
                    body = resp.read().decode("utf-8", errors="replace")
                    entry["status"] = resp.status
                    try:
                        entry["body"] = json.loads(body)
                    except Exception:
                        entry["body"] = body
            except urllib.error.HTTPError as e:
                entry["status"] = e.code
                entry["error"] = str(e)
            except Exception as e:
                entry["status"] = -1
                entry["error"] = repr(e)
            f.write(json.dumps(entry) + "\n")
            f.flush()
            print(f"[{ts}] #{n} status={entry.get('status')} {entry.get('body', entry.get('error', ''))}")
            sys.stdout.flush()
            # sleep until next interval
            time.sleep(max(0.1, args.interval))


if __name__ == "__main__":
    main()
