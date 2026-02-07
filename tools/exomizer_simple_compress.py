import sys
import argparse
import os
from collections import deque
import math

class BitWriter:
    def __init__(self):
        self.data = bytearray(); self.curr = 0; self.bits = 0
    def write_bit(self, b):
        if b: self.curr |= (1 << self.bits)
        self.bits += 1
        if self.bits == 8: self.data.append(self.curr); self.curr = 0; self.bits = 0
    def write_bits(self, v, n):
        for i in range(n): self.write_bit((v >> i) & 1)
    def write_unary(self, n):
        for _ in range(n): self.write_bit(0)
        self.write_bit(1)
    def flush(self):
        if self.bits > 0: self.data.append(self.curr)
        return self.data

def get_base(bits):
    base = [0] * len(bits); curr = 0
    for i in range(len(bits)):
        base[i] = curr
        curr += (1 if bits[i] == 0 else (1 << bits[i]))
    return base

def compress(data, window_size=32767, min_match=4, hash_len=32):
    # Tables optimized for different scenarios
    # For simplicity, we use a single robust table but adjust search parameters
    l_bits = [0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15]
    o_bits = [0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15]
    l_base = get_base(l_bits); o_base = get_base(o_bits)

    bw = BitWriter()
    for b in l_bits: bw.write_bits(b, 4)
    for b in o_bits: bw.write_bits(b, 4)
    for b in o_bits: bw.write_bits(b, 4)
    for _ in range(4): bw.write_bits(0, 4)

    hash_table = {}
    pos = 0; last_o = 0
    while pos < len(data):
        best_l, best_o, best_idx = 0, 0, -1
        if pos + 3 <= len(data):
            h = (data[pos], data[pos+1], data[pos+2])
            if h in hash_table:
                for prev_p in reversed(hash_table[h]):
                    off = pos - prev_p
                    if off > window_size: break
                    l = 3
                    while pos + l < len(data) and data[pos + l] == data[prev_p + l]:
                        l += 1
                        if l >= 32767: break
                    ov = off if off != last_o else 0
                    val = max(l, ov)
                    i = max(0, math.ceil(math.log2(val + 1)) - 1) if val > 0 else 0
                    while i < 16:
                        if l_base[i] <= l < l_base[i] + (1 << l_bits[i]) and \
                           o_base[i] <= ov < o_base[i] + (1 << o_bits[i]):
                            cost = 1 + (i + 1) + l_bits[i] + o_bits[i]
                            if l >= min_match and cost < l * 9 and l > best_l:
                                best_l, best_o, best_idx = l, off, i
                            break
                        i += 1
        if best_idx != -1:
            bw.write_bit(0); bw.write_unary(best_idx)
            bw.write_bits(best_l - l_base[best_idx], l_bits[best_idx])
            ov = best_o if best_o != last_o else 0
            bw.write_bits(ov - o_base[best_idx], o_bits[best_idx])
            if best_o != 0: last_o = best_o
            for i in range(min(best_l, 16)):
                if pos + i + 2 < len(data):
                    h = (data[pos+i], data[pos+i+1], data[pos+i+2])
                    if h not in hash_table: hash_table[h] = deque(maxlen=hash_len)
                    hash_table[h].append(pos+i)
            pos += best_l
        else:
            bw.write_bit(1); bw.write_bits(data[pos], 8)
            if pos + 2 < len(data):
                h = (data[pos], data[pos+1], data[pos+2])
                if h not in hash_table: hash_table[h] = deque(maxlen=hash_len)
                hash_table[h].append(pos)
            pos += 1
    bw.write_bit(0); bw.write_unary(16)
    return bw.flush()

def to_header(data, orig_len, var_name):
    out = ["#include <stdint.h>", "#if defined(__AVR__)", "  #include <avr/pgmspace.h>", "  #define EXO_PROGMEM PROGMEM", "#else", "  #define EXO_PROGMEM", "#endif\n"]
    out.append(f"const uint8_t {var_name}[] EXO_PROGMEM = {{")
    for i in range(0, len(data), 12):
        chunk = data[i:i+12]
        hex_str = ", ".join([f"0x{b:02x}" for b in chunk])
        out.append(f"    {hex_str}" + ("," if i + 12 < len(data) else ""))
    out.append("};")
    out.append(f"const uint32_t {var_name}_len = {len(data)};")
    out.append(f"const uint32_t {var_name}_orig_len = {orig_len};")
    return "\n".join(out)

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("input"); parser.add_argument("output")
    parser.add_argument("--preset", choices=["balanced", "speed", "ratio"], default="balanced")
    parser.add_argument("--name")
    args = parser.parse_args()

    presets = {
        "balanced": {"window": 32767, "min_match": 4, "hash_len": 64},
        "speed":    {"window": 4096,  "min_match": 6, "hash_len": 16},
        "ratio":    {"window": 32767, "min_match": 3, "hash_len": 256},
    }
    config = presets[args.preset]

    with open(args.input, "rb") as f: d = f.read()
    c = compress(d, window_size=config["window"], min_match=config["min_match"], hash_len=config["hash_len"])
    if args.output.endswith(".h"):
        name = args.name if args.name else os.path.basename(args.input).replace(".","_")
        with open(args.output, "w") as f: f.write(to_header(c, len(d), name))
    else:
        with open(args.output, "wb") as f: f.write(c)
    print(f"Compressed {len(d)} -> {len(c)} bytes ({len(c)/len(d)*100:.2f}%)")

if __name__ == "__main__": main()
