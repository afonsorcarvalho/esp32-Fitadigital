"""Captura continua de porta serie para ficheiro ate Ctrl-C (ou kill externo).

Uso: python com3_log.py <saida.log> [baud] [port]
  port default = COM3 (nome preservado por compatibilidade)

Nota: RTS/DTR explicitamente desassertados. Se ficassem assertados o
ESP32-S3 poderia ficar preso em reset (EN=LOW) e nada sairia pela UART.
"""
import sys, time, serial

out_path = sys.argv[1] if len(sys.argv) > 1 else 'com3.log'
baud = int(sys.argv[2]) if len(sys.argv) > 2 else 115200
port = sys.argv[3] if len(sys.argv) > 3 else 'COM3'

s = serial.Serial()
s.port = port
s.baudrate = baud
s.timeout = 0.5
s.rts = False
s.dtr = False
s.open()
# Pulse RTS para forcar reset limpo (espelha esptool Hard reset). Sem isto
# o ESP32-S3 pode ficar preso em reset quando o driver USB-serial reassenta
# linhas no open (Windows). Sequencia: RTS=HIGH (EN=LOW, reset), 100ms,
# RTS=LOW (EN=HIGH, boot).
try:
    s.rts = True
    s.dtr = False
    time.sleep(0.1)
    s.rts = False
    time.sleep(0.05)
except Exception:
    pass

print(f"Capturando {port} @ {baud} -> {out_path}", flush=True)

with open(out_path, 'wb', buffering=0) as f:
    while True:
        try:
            data = s.read(4096)
        except Exception as e:
            sys.stderr.write(f"[read error] {e}\n")
            sys.stderr.flush()
            time.sleep(1)
            continue
        if data:
            f.write(data)
            try:
                sys.stdout.buffer.write(data)
                sys.stdout.buffer.flush()
            except Exception:
                pass
