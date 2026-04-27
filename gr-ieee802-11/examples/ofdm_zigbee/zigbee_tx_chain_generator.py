#!/usr/bin/env python3
"""
Reproduce the ZigBee OQPSK TX chain in ieee802_15_4_OQPSK_PHY.grc.

Chain mirrored from GRC:
1) access_code_prefixer: [pad][4-byte preamble][len][payload]
2) packed_to_unpacked (4 bits, LSB first): byte -> low nibble, high nibble
3) chunks_to_symbols (dimension=16): nibble -> 16 complex chips
4) repeat interp=4
5) multiply by half-sine vector [0, sin(pi/4), 1, sin(3*pi/4)] (repeating)
6) packet_pad2 tail=8 complex zeros
7) OQPSK branch delay: Q delayed by 2 samples, then I + jQ_delayed
"""

import argparse
import ast
import re
from math import pi, sin

import numpy as np


def access_code_prefix(payload: bytes, pad: int = 0x00, preamble: int = 0x000000A7) -> bytes:
    if len(payload) > 255 - 5:
        raise ValueError("Payload too long for this block (must satisfy len < 251).")
    buf = bytearray(6 + len(payload))
    buf[0] = pad & 0xFF
    d = preamble
    for i in range(4, 0, -1):
        buf[i] = d & 0xFF
        d >>= 8
    buf[5] = len(payload) & 0xFF
    buf[6:] = payload
    return bytes(buf)


def bytes_to_nibbles_lsb(data: bytes) -> np.ndarray:
    nibbles = []
    for b in data:
        nibbles.append(b & 0x0F)
        nibbles.append((b >> 4) & 0x0F)
    return np.asarray(nibbles, dtype=np.uint8)


def load_symbol_table_from_grc(grc_path: str) -> np.ndarray:
    text = open(grc_path, "r", encoding="utf-8").read()
    m = re.search(r"symbol_table:\s*'(\[.*?\])'", text, re.S)
    if not m:
        raise RuntimeError("Could not find symbol_table in GRC.")
    raw = m.group(1)
    table = np.asarray(ast.literal_eval(raw), dtype=complex)
    if table.size != 256:
        raise RuntimeError(f"Expected symbol table of size 256, got {table.size}.")
    return table.reshape(16, 16)


def map_nibbles_to_chips(nibbles: np.ndarray, table_16x16: np.ndarray) -> np.ndarray:
    chips = [table_16x16[int(n)] for n in nibbles]
    return np.concatenate(chips).astype(complex)


def apply_repeat_and_half_sine(chips: np.ndarray, interp: int = 4) -> np.ndarray:
    repeated = np.repeat(chips, interp)
    pulse = np.asarray([0.0, sin(pi / 4), 1.0, sin(3 * pi / 4)], dtype=float)
    pulse_rep = np.resize(pulse, repeated.size)
    return repeated * pulse_rep


def apply_oqpsk_q_delay(x: np.ndarray, q_delay: int = 2) -> np.ndarray:
    i = np.real(x)
    q = np.imag(x)
    q_delayed = np.concatenate([np.zeros(q_delay, dtype=float), q[:-q_delay]])
    return i + 1j * q_delayed


def generate_tx(payload_text: str, grc_path: str, pad_tail: int = 8):
    payload = payload_text.encode("utf-8")
    framed = access_code_prefix(payload)
    nibbles = bytes_to_nibbles_lsb(framed)
    table = load_symbol_table_from_grc(grc_path)
    chips = map_nibbles_to_chips(nibbles, table)
    shaped = apply_repeat_and_half_sine(chips, interp=4)
    with_tail = np.concatenate([shaped, np.zeros(pad_tail, dtype=complex)])
    tx = apply_oqpsk_q_delay(with_tail, q_delay=2)
    return {
        "payload": payload,
        "framed_bytes": framed,
        "nibbles": nibbles,
        "chips": chips,
        "shaped": shaped,
        "tx": tx,
    }


def to_hex(data: bytes) -> str:
    return " ".join(f"{b:02X}" for b in data)


def main():
    p = argparse.ArgumentParser(description="Generate final ZigBee OQPSK TX output samples.")
    p.add_argument("--payload", type=str, default="001 HELLO WORLD")
    p.add_argument("--seq", type=int, default=None, help="If set, payload becomes f'{seq:06d} HELLO WORLD'")
    p.add_argument("--grc", type=str, default="ieee802_15_4_OQPSK_PHY.grc")
    p.add_argument("--save-npy", type=str, default="zigbee_tx_final.npy")
    p.add_argument("--save-csv", type=str, default="zigbee_tx_final.csv")
    p.add_argument("--save-framed-hex", type=str, default="")
    args = p.parse_args()

    if args.seq is not None:
        payload_text = f"{args.seq:06d} HELLO WORLD"
    else:
        payload_text = args.payload

    out = generate_tx(payload_text=payload_text, grc_path=args.grc, pad_tail=8)
    np.save(args.save_npy, out["tx"])
    tx_ri = np.column_stack((np.real(out["tx"]), np.imag(out["tx"])))
    np.savetxt(args.save_csv, tx_ri, delimiter=",", header="real,imag", comments="")

    print("Payload text:", payload_text)
    print("Payload bytes length:", len(out["payload"]))
    print("Framed bytes length:", len(out["framed_bytes"]))
    print("Framed bytes (hex):", to_hex(out["framed_bytes"]))
    print("Header [pad preamble len] (hex):", to_hex(out["framed_bytes"][:6]))
    print("Nibbles:", len(out["nibbles"]))
    print("Chips:", len(out["chips"]))
    print("After repeat*4:", len(out["shaped"]))
    print("Final TX samples:", len(out["tx"]))
    print("Saved final TX to:", args.save_npy)
    print("Saved TX CSV to:", args.save_csv)

    if args.save_framed_hex:
        with open(args.save_framed_hex, "w", encoding="utf-8") as f:
            f.write(to_hex(out["framed_bytes"]) + "\n")
        print("Saved framed hex to:", args.save_framed_hex)


if __name__ == "__main__":
    main()


# python3 zigbee_tx_chain_generator.py \
#   --payload "000001 HELLO WORLD" \
#   --save-npy zigbee_tx_final.npy \
#   --save-csv zigbee_tx_final.csv
