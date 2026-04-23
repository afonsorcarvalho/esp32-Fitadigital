"""
WireGuard enrollment mock server.
Simulates the backend the ESP32 will talk to.

Endpoints:
  POST /api/enroll              — device registers pubkey, gets activation_code
  GET  /api/enroll/status/<code> — device polls; 204=pending, 200=config, 410=expired
  POST /api/activate/<code>     — admin approves device (simulates QR scan)
  GET  /api/pending             — list pending enrollments (debug)
"""

import secrets
import string
import time
from flask import Flask, request, jsonify

app = Flask(__name__)

# In-memory store: code -> enrollment record
enrollments: dict[str, dict] = {}

ACTIVATION_TIMEOUT_SEC = 600  # 10 minutes

MOCK_WG_CONFIG = {
    "address": "10.8.0.42/24",
    "server_public_key": "mocked+server+pubkey+base64+AAAAAAAAAAAAAAAAAAA=",
    "server_endpoint": "vpn.fitadigital.local:51820",
    "allowed_ips": "10.8.0.0/24",
    "dns": "10.8.0.1",
}


def _gen_code(length: int = 7) -> str:
    chars = string.ascii_letters + string.digits
    raw = "".join(secrets.choice(chars) for _ in range(length))
    return f"{raw[:3]}-{raw[3:]}"


def _is_expired(record: dict) -> bool:
    return time.time() > record["expires_at"]


@app.post("/api/enroll")
def enroll():
    data = request.get_json(silent=True) or {}
    device_id = data.get("device_id", "").strip()
    public_key = data.get("public_key", "").strip()

    if not device_id or not public_key:
        return jsonify({"error": "device_id and public_key required"}), 400

    code = _gen_code()
    enrollments[code] = {
        "device_id": device_id,
        "public_key": public_key,
        "state": "pending",  # pending | activated | expired
        "created_at": time.time(),
        "expires_at": time.time() + ACTIVATION_TIMEOUT_SEC,
        "config": None,
    }

    activation_url = f"http://localhost:5000/api/activate/{code}"
    poll_url = f"http://localhost:5000/api/enroll/status/{code}"

    app.logger.info("ENROLL device=%s code=%s", device_id, code)

    return jsonify(
        {
            "activation_code": code,
            "activation_url": activation_url,
            "poll_url": poll_url,
        }
    )


@app.get("/api/enroll/status/<code>")
def enroll_status(code: str):
    record = enrollments.get(code)
    if record is None:
        return jsonify({"error": "unknown code"}), 404

    if _is_expired(record) and record["state"] == "pending":
        record["state"] = "expired"

    if record["state"] == "expired":
        app.logger.info("STATUS code=%s → 410 expired", code)
        return jsonify({"error": "code expired"}), 410

    if record["state"] == "pending":
        app.logger.info("STATUS code=%s → 204 pending", code)
        return "", 204

    # activated
    app.logger.info("STATUS code=%s → 200 config", code)
    return jsonify(record["config"])


@app.post("/api/activate/<code>")
def activate(code: str):
    record = enrollments.get(code)
    if record is None:
        return jsonify({"error": "unknown code"}), 404

    if _is_expired(record):
        record["state"] = "expired"
        return jsonify({"error": "code expired"}), 410

    if record["state"] == "activated":
        return jsonify({"error": "already activated"}), 409

    # Assign a unique IP per device (simple increment from pool)
    peer_index = sum(1 for r in enrollments.values() if r["state"] == "activated") + 42
    config = dict(MOCK_WG_CONFIG)
    config["address"] = f"10.8.0.{peer_index}/24"

    record["state"] = "activated"
    record["config"] = config

    app.logger.info("ACTIVATE code=%s device=%s ip=%s", code, record["device_id"], config["address"])
    return jsonify({"ok": True, "assigned_ip": config["address"]})


@app.get("/api/pending")
def list_pending():
    result = []
    for code, r in enrollments.items():
        if _is_expired(r) and r["state"] == "pending":
            r["state"] = "expired"
        result.append(
            {
                "code": code,
                "device_id": r["device_id"],
                "state": r["state"],
                "expires_in": max(0, int(r["expires_at"] - time.time())),
            }
        )
    return jsonify(result)


if __name__ == "__main__":
    app.run(host="0.0.0.0", port=5000, debug=True)
