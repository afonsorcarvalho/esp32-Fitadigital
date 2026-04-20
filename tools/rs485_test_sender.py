"""Gera stream de teste para o FitaDigital via RS232->RS485.

Envia linhas no formato:
    LINE <ctr:06d> | <ISO timestamp> | <payload repetitivo>

E permite verificar um .txt descarregado do dispositivo contra o esperado:
conta linhas, checa sequencialidade do contador e reporta lacunas.

Uso:
    python rs485_test_sender.py send   --port COM7 --count 100 --interval-ms 100
    python rs485_test_sender.py verify --file 20260419.txt --expect 100
"""

from __future__ import annotations

import argparse
import re
import sys
import time
from datetime import datetime
from pathlib import Path

try:
    import serial  # type: ignore
except ImportError:
    serial = None  # verify-only nao precisa de pyserial

LINE_RE = re.compile(r"^LINE (\d{6}) \| ([0-9T:\-\.]+) \| ([A-Z]+)\s*$")


def build_line(ctr: int, payload_len: int) -> str:
    ts = datetime.now().isoformat(timespec="milliseconds")
    pad_chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
    payload = (pad_chars * ((payload_len // len(pad_chars)) + 1))[:payload_len]
    return f"LINE {ctr:06d} | {ts} | {payload}"


def cmd_send(args: argparse.Namespace) -> int:
    if serial is None:
        print("ERRO: pyserial nao esta instalado. Rode: pip install pyserial", file=sys.stderr)
        return 2
    try:
        ser = serial.Serial(args.port, args.baud, bytesize=8, parity="N", stopbits=1, timeout=0)
    except serial.SerialException as exc:  # type: ignore[attr-defined]
        print(f"ERRO abrindo {args.port}@{args.baud}: {exc}", file=sys.stderr)
        return 2

    terminator = {"lf": b"\n", "cr": b"\r", "crlf": b"\r\n"}[args.terminator]
    bytes_total = 0
    t0 = time.monotonic()
    try:
        for i in range(1, args.count + 1):
            line = build_line(i, args.length)
            data = line.encode("ascii", errors="replace") + terminator
            ser.write(data)
            bytes_total += len(data)
            if args.echo:
                print(line)
            if args.interval_ms > 0 and i < args.count:
                time.sleep(args.interval_ms / 1000.0)
        ser.flush()
    finally:
        ser.close()

    dt = time.monotonic() - t0
    rate = (bytes_total * 8) / dt if dt > 0 else 0
    print(
        f"OK: enviadas {args.count} linhas, {bytes_total} bytes em {dt:.2f}s "
        f"(~{rate:.0f} bps equivalentes, baud do port = {args.baud})"
    )
    return 0


def cmd_verify(args: argparse.Namespace) -> int:
    path = Path(args.file)
    if not path.is_file():
        print(f"ERRO: ficheiro nao encontrado: {path}", file=sys.stderr)
        return 2

    raw = path.read_bytes()
    # Normaliza terminadores para contar linhas de forma tolerante
    text = raw.decode("ascii", errors="replace")
    lines = [ln for ln in text.splitlines() if ln.strip()]

    seen: list[int] = []
    malformed = 0
    for ln in lines:
        m = LINE_RE.match(ln)
        if not m:
            malformed += 1
            continue
        seen.append(int(m.group(1)))

    print(f"Ficheiro: {path}")
    print(f"Total de linhas nao vazias: {len(lines)}")
    print(f"Linhas no formato esperado: {len(seen)}")
    if malformed:
        print(f"Linhas fora do padrao: {malformed}")

    if not seen:
        print("AVISO: nenhuma linha no padrao LINE ###### | ...")
        return 1

    seen_sorted = sorted(seen)
    lo, hi = seen_sorted[0], seen_sorted[-1]
    expected_range = set(range(lo, hi + 1))
    missing = sorted(expected_range - set(seen))
    duplicates = len(seen) - len(set(seen))

    print(f"Contador: min={lo} max={hi} (esperado contiguo de {lo} ate {hi})")
    if duplicates:
        print(f"AVISO: {duplicates} linhas duplicadas")
    if missing:
        preview = ", ".join(str(n) for n in missing[:20])
        more = "" if len(missing) <= 20 else f" (+{len(missing) - 20} mais)"
        print(f"FALHA: {len(missing)} linhas em falta: {preview}{more}")

    if args.expect is not None:
        if len(seen) != args.expect:
            print(f"FALHA: esperado {args.expect} linhas validas, obtido {len(seen)}")
            return 1

    if not missing and not duplicates and malformed == 0:
        print("OK: sem perdas, sem duplicados, sem malformadas.")
        return 0
    return 1


def main(argv: list[str] | None = None) -> int:
    p = argparse.ArgumentParser(description="Teste RS232->RS485 para FitaDigital.")
    sub = p.add_subparsers(dest="cmd", required=True)

    s = sub.add_parser("send", help="Envia linhas de teste pela porta serie.")
    s.add_argument("--port", default="COM7")
    s.add_argument("--baud", type=int, default=9600)
    s.add_argument("--count", type=int, default=100)
    s.add_argument("--interval-ms", type=int, default=100)
    s.add_argument("--length", type=int, default=40, help="Tamanho do payload alfabetico.")
    s.add_argument("--terminator", choices=["lf", "cr", "crlf"], default="crlf")
    s.add_argument("--echo", action="store_true", help="Imprime cada linha na consola.")
    s.set_defaults(func=cmd_send)

    v = sub.add_parser("verify", help="Verifica um .txt gravado pelo dispositivo.")
    v.add_argument("--file", required=True)
    v.add_argument("--expect", type=int, default=None, help="Numero esperado de linhas.")
    v.set_defaults(func=cmd_verify)

    args = p.parse_args(argv)
    return int(args.func(args) or 0)


if __name__ == "__main__":
    sys.exit(main())
