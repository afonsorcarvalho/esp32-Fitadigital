"""Captura continua de porta serie para ficheiro ate Ctrl-C (ou kill externo).

Uso: python com3_log.py <saida.log> [baud] [port]
  port default = COM3 (nome preservado por compatibilidade)
"""
import sys, time, serial

out_path = sys.argv[1] if len(sys.argv) > 1 else 'com3.log'
baud = int(sys.argv[2]) if len(sys.argv) > 2 else 115200
port = sys.argv[3] if len(sys.argv) > 3 else 'COM3'

s = serial.Serial(port, baud, timeout=0.5)
with open(out_path, 'wb', buffering=0) as f:
    print(f"Capturando {port} @ {baud} -> {out_path}")
    while True:
        try:
            data = s.read(4096)
        except Exception as e:
            sys.stderr.write(f"[read error] {e}\n")
            time.sleep(1)
            continue
        if data:
            f.write(data)
            sys.stdout.buffer.write(data)
            sys.stdout.buffer.flush()
