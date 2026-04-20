"""Captura COM3 (ESP32) durante N segundos e imprime as linhas recebidas."""
import sys, time, serial
sec = float(sys.argv[1]) if len(sys.argv) > 1 else 15
s = serial.Serial('COM3', 115200, timeout=0.2)
t0 = time.monotonic()
buf = b''
while time.monotonic() - t0 < sec:
    data = s.read(4096)
    if data:
        buf += data
        sys.stdout.buffer.write(data)
        sys.stdout.buffer.flush()
s.close()
print(f"\n--- fim ({len(buf)} bytes em {sec:.0f}s) ---")
