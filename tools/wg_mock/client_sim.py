"""
WireGuard enrollment client simulator.
Mimics exactly what the ESP32 firmware will do:
  1. Generate Curve25519 keypair
  2. POST /api/enroll
  3. Print QR + activation_code
  4. Poll /api/enroll/status with exponential backoff
  5. Print received WireGuard config
"""

import base64
import io
import os
import sys
import time
import uuid

# Force UTF-8 output so QR block chars don't crash on Windows cp1252
if hasattr(sys.stdout, "buffer"):
    sys.stdout = io.TextIOWrapper(sys.stdout.buffer, encoding="utf-8", errors="replace")

import qrcode
import requests
from cryptography.hazmat.primitives.asymmetric.x25519 import X25519PrivateKey

SERVER = os.environ.get("WG_SERVER", "http://localhost:5000")

# Backoff sequence in seconds (matches ESP32 firmware plan)
BACKOFF_SEQ = [3, 6, 12, 30]


def gen_keypair() -> tuple[str, str]:
    """Generate Curve25519 keypair, return (private_b64, public_b64)."""
    private_key = X25519PrivateKey.generate()
    private_bytes = private_key.private_bytes_raw()
    public_bytes = private_key.public_key().public_bytes_raw()
    priv_b64 = base64.b64encode(private_bytes).decode()
    pub_b64 = base64.b64encode(public_bytes).decode()
    return priv_b64, pub_b64


def fake_device_id() -> str:
    """Simulate eFuse MAC — stable per run via UUID seeded from hostname."""
    seed = uuid.uuid5(uuid.NAMESPACE_DNS, os.environ.get("COMPUTERNAME", "esp32-sim"))
    return seed.hex[:12]


def print_qr(url: str) -> None:
    qr = qrcode.QRCode(border=1)
    qr.add_data(url)
    qr.make(fit=True)
    qr.print_ascii(invert=True)


def poll(poll_url: str, activation_code: str, expires_in: int = 600) -> dict | None:
    deadline = time.time() + expires_in
    backoff_idx = 0

    print(f"\n[poll] Waiting for admin to activate code: {activation_code}")
    print(f"[poll] Run: python admin_cli.py activate {activation_code}\n")

    while time.time() < deadline:
        try:
            resp = requests.get(poll_url, timeout=15)
        except requests.RequestException as e:
            print(f"[poll] Request error: {e}")
            delay = BACKOFF_SEQ[min(backoff_idx, len(BACKOFF_SEQ) - 1)]
            backoff_idx += 1
            time.sleep(delay)
            continue

        if resp.status_code == 204:
            remaining = int(deadline - time.time())
            print(f"[poll] Pending... expires in {remaining}s", end="\r")
            delay = BACKOFF_SEQ[min(backoff_idx, len(BACKOFF_SEQ) - 1)]
            backoff_idx = min(backoff_idx + 1, len(BACKOFF_SEQ) - 1)
            time.sleep(delay)

        elif resp.status_code == 200:
            return resp.json()

        elif resp.status_code == 410:
            print("\n[poll] Code expired — restart enrollment.")
            return None

        else:
            print(f"\n[poll] Unexpected status {resp.status_code}: {resp.text}")
            return None

    print("\n[poll] Timed out locally.")
    return None


def main() -> None:
    device_id = fake_device_id()
    print(f"[keygen] Device ID: {device_id}")

    priv_b64, pub_b64 = gen_keypair()
    print(f"[keygen] Public key:  {pub_b64}")
    print(f"[keygen] Private key: {priv_b64}  (never leaves device)")

    # Enroll
    print(f"\n[enroll] POST {SERVER}/api/enroll")
    try:
        resp = requests.post(
            f"{SERVER}/api/enroll",
            json={"device_id": device_id, "public_key": pub_b64},
            timeout=10,
        )
        resp.raise_for_status()
    except requests.RequestException as e:
        print(f"[enroll] Failed: {e}")
        sys.exit(1)

    enroll_data = resp.json()
    code = enroll_data["activation_code"]
    activation_url = enroll_data["activation_url"]
    poll_url = enroll_data["poll_url"]

    print(f"\n[enroll] Activation code: {code}")
    print(f"[enroll] Activation URL:  {activation_url}")
    print("\n[qr] Scan to activate:\n")
    print_qr(activation_url)

    # Poll
    config = poll(poll_url, code)
    if config is None:
        sys.exit(1)

    print("\n[config] WireGuard configuration received:")
    print(f"  Address:           {config['address']}")
    print(f"  Server public key: {config['server_public_key']}")
    print(f"  Server endpoint:   {config['server_endpoint']}")
    print(f"  Allowed IPs:       {config['allowed_ips']}")
    print(f"  DNS:               {config['dns']}")
    print("\n[done] Tunnel config ready. Device would write this to NVS and start WireGuard.")


if __name__ == "__main__":
    main()
