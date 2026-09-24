#!/usr/bin/env python3
"""Decodifica ou reescreve o IP do serverlist.bin do WYD.

Formato (7.48, BASE_InitializeServerList): 0x6E registros de 64 bytes; cada byte
do registro i foi somado a KEY[63 - i]. Registro 0 de cada grupo = nome do grupo,
os seguintes = IPs dos servidores.

Uso:
  serverlist.py serverlist.bin                      # lista entradas
  serverlist.py serverlist.bin saida.bin 10.0.0.5  # troca todo IPv4 pelo novo
"""
import sys

KEY = bytes([
    0xA4, 0xA1, 0xA4, 0xA4, 0xA4, 0xA7, 0xA4, 0xA9, 0xA4, 0xB1, 0xA4, 0xB2, 0xA4, 0xB5, 0xA4, 0xB7,
    0xA4, 0xB8, 0xA4, 0xBA, 0xA4, 0xBB, 0xA4, 0xBC, 0xA4, 0xBD, 0xA4, 0xBE, 0xA4, 0xBF, 0xA4, 0xC1,
    0xA4, 0xC3, 0xA4, 0xC5, 0xA4, 0xC7, 0xA4, 0xCB, 0xA4, 0xCC, 0xA4, 0xD0, 0xA4, 0xD1, 0xA4, 0xD3,
    0xA4, 0xBF, 0xA4, 0xC4, 0xA4, 0xD3, 0xA4, 0xC7, 0xA4, 0xCC, 0xB0, 0xA1, 0xB3, 0xAA, 0xB4, 0xD9,
])
RECORDS = 0x6E


def decode(data):
    out = []
    for r in range(RECORDS):
        rec = bytes((data[r * 64 + i] - KEY[63 - i]) & 0xFF for i in range(64))
        out.append(rec.split(b"\0", 1)[0])
    return out


def encode(entries):
    buf = bytearray()
    for s in entries:
        rec = s[:63].ljust(64, b"\0")
        buf += bytes((rec[i] + KEY[63 - i]) & 0xFF for i in range(64))
    return bytes(buf)


def is_ipv4(s):
    parts = s.split(b".")
    return len(parts) == 4 and all(p.isdigit() and int(p) < 256 for p in parts)


def main():
    if len(sys.argv) not in (2, 4):
        print(__doc__)
        return 1
    data = open(sys.argv[1], "rb").read().ljust(RECORDS * 64, b"\0")
    entries = decode(data)
    if len(sys.argv) == 2:
        for i, s in enumerate(entries):
            if s:
                print(i, s.decode("latin-1"))
        return 0
    new_ip = sys.argv[3].encode()
    for i, s in enumerate(entries):
        if is_ipv4(s):
            print(f"{i}: {s.decode()} -> {new_ip.decode()}")
            entries[i] = new_ip
    open(sys.argv[2], "wb").write(encode(entries))
    return 0


if __name__ == "__main__":
    sys.exit(main())
