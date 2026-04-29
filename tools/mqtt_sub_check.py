#!/usr/bin/env python3
"""Subscribe MQTT broker e regista mensagens durante N segundos.

Uso: python mqtt_sub_check.py --host 192.168.0.188 --port 1883 \
                              --topic 'fitadigital/#' --duration 90 --log out.log
"""
import argparse
import datetime
import sys
import time

import paho.mqtt.client as mqtt


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--host", default="192.168.0.188")
    ap.add_argument("--port", type=int, default=1883)
    ap.add_argument("--topic", default="fitadigital/#")
    ap.add_argument("--duration", type=int, default=90)
    ap.add_argument("--log", default=None)
    args = ap.parse_args()

    log_fh = open(args.log, "a", encoding="utf-8") if args.log else None
    msg_count = {"n": 0}
    topics = {}

    def on_connect(client, userdata, flags, rc, props=None):
        line = f"[connect] rc={rc}"
        print(line, flush=True)
        if log_fh:
            log_fh.write(line + "\n")
        client.subscribe(args.topic)

    def on_message(client, userdata, msg):
        msg_count["n"] += 1
        topics[msg.topic] = topics.get(msg.topic, 0) + 1
        ts = datetime.datetime.now().isoformat(timespec="seconds")
        try:
            payload = msg.payload.decode("utf-8", errors="replace")
        except Exception:
            payload = repr(msg.payload)
        line = f"[{ts}] {msg.topic} | {payload[:200]}"
        print(line, flush=True)
        if log_fh:
            log_fh.write(line + "\n")
            log_fh.flush()

    client = mqtt.Client(callback_api_version=mqtt.CallbackAPIVersion.VERSION2,
                         client_id=f"soak-sub-{int(time.time())}")
    client.on_connect = on_connect
    client.on_message = on_message
    client.connect(args.host, args.port, keepalive=30)
    client.loop_start()

    t_end = time.monotonic() + args.duration
    while time.monotonic() < t_end:
        time.sleep(1.0)

    client.loop_stop()
    client.disconnect()

    summary = f"\n=== summary: {msg_count['n']} mensagens em {args.duration}s ==="
    print(summary, flush=True)
    if log_fh:
        log_fh.write(summary + "\n")
    for t, n in sorted(topics.items()):
        line = f"  {t}: {n}"
        print(line, flush=True)
        if log_fh:
            log_fh.write(line + "\n")

    if log_fh:
        log_fh.close()
    return 0 if msg_count["n"] > 0 else 1


if __name__ == "__main__":
    sys.exit(main())
