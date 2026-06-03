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

# Optimized robust tables
L_BITS = [0, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14]
O_BITS_3 = [0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15]
O_BITS_2 = [0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15]
O_BITS_1 = [0, 1, 2, 3]

L_BASE = get_base(L_BITS)
O_BASE_3 = get_base(O_BITS_3)
O_BASE_2 = get_base(O_BITS_2)
O_BASE_1 = get_base(O_BITS_1)

def get_idx_and_extra(val, bits, base):
    for i in range(len(bits)):
        limit = base[i] + (1 if bits[i] == 0 else (1 << bits[i]))
        if base[i] <= val < limit:
            return i, val - base[i]
    return -1, 0

def compress(data, window_size=32767, min_match=1, hash_len=256, lazy=True):
    bw = BitWriter()
    for b in L_BITS: bw.write_bits(b, 4)
    for b in O_BITS_3: bw.write_bits(b, 4)
    for b in O_BITS_2: bw.write_bits(b, 4)
    for b in O_BITS_1: bw.write_bits(b, 4)

    hash_table = {}
    pos = 0; last_o = 0

    def get_hash(p):
        if p + 3 > len(data): return None
        return (data[p], data[p+1], data[p+2])

    def find_best_match(p, current_last_o):
        best_match = None
        best_score = 0
        h = get_hash(p)

        # Also check for 2-byte matches if length 3+ not found or as alternatives
        # but the hash is 3 bytes. Let's just use the 3-byte hash for now
        # and maybe a 2-byte hash if needed.
        # Actually, Exomizer often finds many matches.

        match_candidates = []
        if h and h in hash_table:
            match_candidates = list(hash_table[h])

        # Check last_o reuse even if no hash match (for length 1 or 2)
        if current_last_o > 0 and current_last_o <= p:
            prev_p = p - current_last_o
            if prev_p >= 0 and data[p] == data[prev_p]:
                match_candidates.append(prev_p)

        seen_offsets = set()
        for prev_p in reversed(match_candidates):
            off = p - prev_p
            if off <= 0 or off > window_size or off in seen_offsets: continue
            seen_offsets.add(off)

            l = 0
            while p + l < len(data) and p - off + l >= 0 and data[p + l] == data[p - off + l]:
                l += 1
                if l >= 32767: break

            if l < min_match: continue

            # Find best encoding for this (l, off)
            # We can try different l values down to min_match
            for cur_l in range(l, min_match - 1, -1):
                l_idx, l_extra = get_idx_and_extra(cur_l, L_BITS, L_BASE)
                if l_idx == -1 or l_idx >= 16: continue

                ov = off if off != current_last_o else 0
                if cur_l == 1:
                    t_idx = l_idx % 4; obits = O_BITS_1[t_idx]; obase = O_BASE_1[t_idx]
                elif cur_l == 2:
                    t_idx = l_idx % 16; obits = O_BITS_2[t_idx]; obase = O_BASE_2[t_idx]
                else:
                    t_idx = l_idx % 16; obits = O_BITS_3[t_idx]; obase = O_BASE_3[t_idx]

                if obase <= ov < obase + (1 if obits == 0 else (1 << obits)):
                    cost = 1 + (l_idx + 1) + L_BITS[l_idx] + obits
                    score = cur_l / cost
                    if score > best_score:
                        best_score = score
                        best_match = (cur_l, off, l_idx, obits, obase, cost)
                    # For a fixed offset, a longer length is usually better score-wise
                    # so we don't need to check smaller cur_l unless we have a specific reason.
                    break
        return best_match

    while pos < len(data):
        match = find_best_match(pos, last_o)

        if lazy and match and pos + 1 < len(data):
            # Cost of current match per byte
            current_cost_per_byte = match[5] / match[0]

            # Literal + next match cost per byte
            next_match = find_best_match(pos + 1, last_o)
            if next_match:
                next_cost_per_byte = (9 + next_match[5]) / (next_match[0] + 1)
                if next_cost_per_byte < current_cost_per_byte:
                    match = None
            else:
                # If no next match, literal + literal might still be worse than match
                # but if current_cost_per_byte > 9, literal is better
                if current_cost_per_byte > 9:
                    match = None

        if match:
            l, off, l_idx, obits, obase, _ = match
            bw.write_bit(0); bw.write_unary(l_idx)
            _, l_extra = get_idx_and_extra(l, L_BITS, L_BASE)
            bw.write_bits(l_extra, L_BITS[l_idx])
            ov = off if off != last_o else 0
            bw.write_bits(ov - obase, obits)
            if off != 0: last_o = off
            for i in range(l):
                hu = get_hash(pos + i)
                if hu:
                    if hu not in hash_table: hash_table[hu] = deque(maxlen=hash_len)
                    hash_table[hu].append(pos + i)
            pos += l
        else:
            bw.write_bit(1); bw.write_bits(data[pos], 8)
            hu = get_hash(pos)
            if hu:
                if hu not in hash_table: hash_table[hu] = deque(maxlen=hash_len)
                hash_table[hu].append(pos)
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
        "balanced": {"window": 32767, "min_match": 1, "hash_len": 128, "lazy": True},
        "speed":    {"window": 8192,  "min_match": 2, "hash_len": 32, "lazy": True},
        "ratio":    {"window": 65535, "min_match": 1, "hash_len": 512, "lazy": True},
    }
    config = presets[args.preset]
    with open(args.input, "rb") as f: d = f.read()
    c = compress(d, window_size=config["window"], min_match=config["min_match"], hash_len=config["hash_len"], lazy=config["lazy"])
    if args.output.endswith(".h"):
        name = args.name if args.name else os.path.basename(args.input).replace(".","_").replace("-","_")
        with open(args.output, "w") as f: f.write(to_header(c, len(d), name))
    else:
        with open(args.output, "wb") as f: f.write(c)
    ratio = (len(c)/len(d)*100) if len(d) > 0 else 0
    print(f"Compressed {len(d)} -> {len(c)} bytes ({ratio:.2f}%)")

if __name__ == "__main__": main()
