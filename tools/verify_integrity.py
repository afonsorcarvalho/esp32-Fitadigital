#!/usr/bin/env python3
"""
verify_integrity.py — verifica a cadeia HMAC-SHA256/32 inline dos .txt do FitaDigital.

Formato (ver src/cycle_integrity.cpp):
  Cabecalho: "#FDIGI-INT v1 alg=HMAC-SHA256-32 dev=<MAC> sep=TAB\t0\t<8hex>"
  Linhas:    "<conteudo>\t<seq>\t<8hex>"
  seed   = HMAC(key, path_do_ficheiro)[:4]      # nao usado p/ verificar (ver nota)
  mac_i  = HMAC(key, mac_{i-1} || ":" || seq || ":" || raw)[:4]

A password (FDIGI_INT_SECRET) NAO esta' hardcoded aqui: lida de --secret ou da
variavel de ambiente FDIGI_INT_SECRET (mesmo cuidado de fuga que o firmware).

NOTA seed: o firmware semeia a cadeia com HMAC(key, path). Como nao sabemos o path
original a partir do ficheiro local, o cabecalho ancora a cadeia: a 1a linha de dados
encadeia no mac do cabecalho, que recalculamos a partir do conteudo do cabecalho + seed.
O seed depende do path. Por defeito derivamos o path do nome do ficheiro
(/CICLOS/AAAA/MM/AAAAMMDD.txt); usa --path para forcar outro.

Limites: cadeia deteta editar/inserir/remover-no-meio/reordenar. NAO deteta cortar as
ultimas N linhas (truncamento da cauda) — ver plano. Verificacao offline, simetrica.

Uso:
  set FDIGI_INT_SECRET=...        (Windows)  /  export FDIGI_INT_SECRET=...  (Linux)
  python verify_integrity.py CAMINHO\20260626.txt
  python verify_integrity.py 20260626.txt --secret "..." --path /CICLOS/2026/06/20260626.txt
  python verify_integrity.py 20260626.txt --clean saida_legivel.txt
"""
import argparse
import hashlib
import hmac
import os
import re
import sys

MAC_BYTES = 4
HEADER_PREFIX = "#FDIGI-INT"


def mac_trunc(key: bytes, msg: bytes) -> bytes:
    return hmac.new(key, msg, hashlib.sha256).digest()[:MAC_BYTES]


def mac_line(key: bytes, prev: bytes, seq: int, raw: str) -> bytes:
    msg = prev + b":" + str(seq).encode() + b":" + raw.encode("utf-8", "surrogateescape")
    return mac_trunc(key, msg)


def derive_path_from_name(fname: str) -> str:
    """20260626.txt -> /CICLOS/2026/06/20260626.txt (convencao do firmware)."""
    base = os.path.basename(fname)
    m = re.match(r"^(\d{4})(\d{2})(\d{2})\.txt$", base)
    if m:
        y, mo, _d = m.groups()
        return f"/CICLOS/{y}/{mo}/{base}"
    return base


def split_signed(line: str):
    """Devolve (raw, seq, mac_hex) a partir dos 2 ultimos TABs, ou None."""
    i2 = line.rfind("\t")
    if i2 < 0:
        return None
    i1 = line.rfind("\t", 0, i2)
    if i1 < 0:
        return None
    raw = line[:i1]
    seq_s = line[i1 + 1:i2]
    mac_s = line[i2 + 1:]
    if not seq_s.isdigit():
        return None
    if len(mac_s) != MAC_BYTES * 2 or not re.fullmatch(r"[0-9a-fA-F]+", mac_s):
        return None
    return raw, int(seq_s), mac_s.lower()


def main() -> int:
    ap = argparse.ArgumentParser(description="Verifica cadeia HMAC inline dos .txt FitaDigital.")
    ap.add_argument("file", help="ficheiro .txt assinado")
    ap.add_argument("--secret", help="password (senao usa env FDIGI_INT_SECRET)")
    ap.add_argument("--path", help="path original do ficheiro no device (seed da cadeia)")
    ap.add_argument("--clean", metavar="OUT", help="exporta versao legivel (sem seq/mac)")
    args = ap.parse_args()

    secret = args.secret or os.environ.get("FDIGI_INT_SECRET")
    if not secret:
        print("ERRO: password ausente. Use --secret ou defina FDIGI_INT_SECRET.", file=sys.stderr)
        return 2
    key = secret.encode("utf-8")

    try:
        with open(args.file, "r", encoding="utf-8", errors="surrogateescape", newline="") as fh:
            raw_text = fh.read()
    except OSError as e:
        print(f"ERRO: nao foi possivel abrir {args.file}: {e}", file=sys.stderr)
        return 2

    lines = raw_text.split("\n")
    if lines and lines[-1] == "":
        lines.pop()  # ultima linha vazia apos o \n final

    if not lines or not lines[0].startswith(HEADER_PREFIX):
        print("NAO ASSINADO: ficheiro sem cabecalho #FDIGI-INT (legado).")
        return 1

    path = args.path or derive_path_from_name(args.file)
    seed = mac_trunc(key, path.encode("utf-8"))

    # Linha 0 = cabecalho. Recalcula mac0 (prev=seed, seq=0, raw=conteudo do cabecalho).
    h = split_signed(lines[0])
    if h is None:
        print("ADULTERADO: cabecalho mal formado (linha 1).")
        return 1
    hdr_raw, hdr_seq, hdr_mac = h
    if hdr_seq != 0:
        print(f"ADULTERADO: cabecalho com seq={hdr_seq} (esperado 0).")
        return 1
    calc0 = mac_line(key, seed, 0, hdr_raw).hex()
    if calc0 != hdr_mac:
        print("ADULTERADO: cabecalho falha verificacao (mac0).")
        print(f"  nota: confirme --path (usado: {path}).")
        return 1

    prev = bytes.fromhex(hdr_mac)
    expected_seq = 1
    clean_out = [] if args.clean else None  # so' linhas de dados, sem cabecalho

    for idx in range(1, len(lines)):
        parsed = split_signed(lines[idx])
        if parsed is None:
            print(f"ADULTERADO na linha {idx + 1}: formato invalido (sem seq/mac).")
            print(f"  conteudo: {lines[idx][:80]!r}")
            return 1
        raw, seq, mac = parsed
        if seq != expected_seq:
            print(f"ADULTERADO na linha {idx + 1}: seq={seq} (esperado {expected_seq}) "
                  f"— remocao/insercao/reordenacao.")
            return 1
        calc = mac_line(key, prev, seq, raw).hex()
        if calc != mac:
            print(f"ADULTERADO na linha {idx + 1} (seq {seq}): mac nao bate.")
            print(f"  conteudo: {raw[:80]!r}")
            return 1
        prev = bytes.fromhex(mac)
        expected_seq += 1
        if clean_out is not None:
            clean_out.append(raw)

    n_data = len(lines) - 1
    print(f"OK ({n_data} linhas assinadas, dev={hdr_raw.split('dev=')[-1].split(' ')[0]}).")
    print("  nota: truncamento da cauda (cortar linhas finais) NAO e' detetavel offline.")

    if clean_out is not None:
        try:
            with open(args.clean, "w", encoding="utf-8", errors="surrogateescape", newline="\n") as out:
                out.write("\n".join(clean_out) + "\n")
            print(f"  export legivel: {args.clean}")
        except OSError as e:
            print(f"ERRO ao escrever {args.clean}: {e}", file=sys.stderr)
            return 2
    return 0


if __name__ == "__main__":
    sys.exit(main())
