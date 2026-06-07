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

# Optimized default tables
DEFAULT_L_BITS = [0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 7, 8, 9, 10]
DEFAULT_O_BITS_3 = [4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 10, 11, 12, 13, 14]
DEFAULT_O_BITS_2 = [3, 3, 4, 4, 5, 5, 6, 6, 7, 8, 9, 10, 11, 12, 13, 14]
DEFAULT_O_BITS_1 = [2, 3, 4, 5]

class Compressor:
    def __init__(self, data, window_size=32767, hash_len=32):
        self.data = data
        self.n = len(data)
        self.window_size = window_size
        self.hash_len = hash_len
        self.l_bits = list(DEFAULT_L_BITS)
        self.o_bits3 = list(DEFAULT_O_BITS_3)
        self.o_bits2 = list(DEFAULT_O_BITS_2)
        self.o_bits1 = list(DEFAULT_O_BITS_1)
        self.update_bases()

    def update_bases(self):
        self.l_base = get_base(self.l_bits)
        self.o_base3 = get_base(self.o_bits3)
        self.o_base2 = get_base(self.o_bits2)
        self.o_base1 = get_base(self.o_bits1)

    def get_idx_and_extra(self, val, bits, base):
        for i in range(len(bits)):
            limit = base[i] + (1 if bits[i] == 0 else (1 << bits[i]))
            if base[i] <= val < limit:
                return i, val - base[i]
        return -1, 0

    def find_matches(self, pos, last_o, hash_table):
        matches = []
        if last_o > 0 and pos - last_o >= 0:
            l = 0
            while pos + l < self.n and self.data[pos + l] == self.data[pos - last_o + l]:
                l += 1
                if l >= 32767: break
            if l >= 1: matches.append((l, last_o))

        if pos + 2 <= self.n:
            h = (self.data[pos], self.data[pos+1])
            if h in hash_table:
                seen_offsets = {last_o} if last_o > 0 else set()
                count = 0
                for prev_p in reversed(hash_table[h]):
                    off = pos - prev_p
                    if off > self.window_size: break
                    if off in seen_offsets: continue
                    seen_offsets.add(off)
                    l = 2
                    while pos + l < self.n and self.data[pos + l] == self.data[prev_p + l]:
                        l += 1
                        if l >= 32767: break
                    matches.append((l, off))
                    count += 1
                    if count >= self.hash_len: break
        return matches

    def solve_dp(self):
        n = self.n
        cost = [1e15] * (n + 1)
        from_info = [None] * (n + 1)
        cost[0] = 0
        last_o_at = [0] * (n + 1)
        hash_table = {}

        for i in range(n):
            if i % 10000 == 0:
                print(f"DP Progress: {i}/{n}", end='\r', flush=True)

            ci = cost[i]
            loi = last_o_at[i]

            # 1. Literal
            if ci + 9 < cost[i+1]:
                cost[i+1] = ci + 9
                from_info[i+1] = (i, 'lit', 0, 0, 0, 0)
                last_o_at[i+1] = loi

            # 2. Matches
            matches = self.find_matches(i, loi, hash_table)
            for l, off in matches:
                ov = off if off != loi else 0

                # Check all possible length buckets
                for l_idx in range(16):
                    if self.l_base[l_idx] > l: break
                    l_limit = self.l_base[l_idx] + (1 if self.l_bits[l_idx] == 0 else (1 << self.l_bits[l_idx]))
                    use_l = min(l, l_limit - 1)
                    if use_l < self.l_base[l_idx]: continue

                    if use_l == 1: obits_tab = self.o_bits1; obase_tab = self.l_base; o_count = 4
                    elif use_l == 2: obits_tab = self.o_bits2; obase_tab = self.o_base2; o_count = 16
                    else: obits_tab = self.o_bits3; obase_tab = self.o_base3; o_count = 16

                    o_idx, _ = self.get_idx_and_extra(ov, obits_tab, obase_tab)
                    if o_idx != -1:
                        c_match = ci + 1 + (l_idx + 1) + self.l_bits[l_idx] + (o_idx + 1) + obits_tab[o_idx]
                        if c_match < cost[i+use_l]:
                            cost[i+use_l] = c_match
                            from_info[i+use_l] = (i, 'match', use_l, off, l_idx, o_idx)
                            last_o_at[i+use_l] = off

            # 3. Literal Run
            if i + 35 <= n:
                for rl in [64, 512, 4096, 65535]:
                    actual_rl = min(n - i, rl)
                    c_run = ci + 1 + 18 + 16 + actual_rl * 8
                    if c_run < cost[i+actual_rl]:
                        cost[i+actual_rl] = c_run
                        from_info[i+actual_rl] = (i, 'run', actual_rl, 0, 0, 0)
                        last_o_at[i+actual_rl] = loi

            if i + 2 <= n:
                h = (self.data[i], self.data[i+1])
                if h not in hash_table: hash_table[h] = deque(maxlen=self.hash_len)
                hash_table[h].append(i)

        path = []
        curr = n
        while curr > 0:
            info = from_info[curr]
            if not info: break
            path.append(info)
            curr = info[0]
        path.reverse()
        return path

    def optimize_tables(self, path):
        l_vals = []
        o1_vals = []; o2_vals = []; o3_vals = []
        last_o = 0
        for p in path:
            if p[1] == 'match':
                l_vals.append(p[2])
                ov = p[3] if p[3] != last_o else 0
                if p[2] == 1: o1_vals.append(ov)
                elif p[2] == 2: o2_vals.append(ov)
                else: o3_vals.append(ov)
                last_o = p[3]

        def fit(vals, bits, base, count):
            if not vals: return
            freq = {}
            for v in vals: freq[v] = freq.get(v, 0) + 1
            curr_base = 0
            for i in range(count):
                best_b = 0; min_added = 1e18
                for b in range(16):
                    limit = curr_base + (1 if b == 0 else (1 << b))
                    cost = 0; items = 0
                    for v, f in freq.items():
                        if curr_base <= v < limit:
                            cost += f * (i + 1 + b)
                            items += f
                    if items > 0:
                        score = (cost / items) - 0.1 * items
                        if score < min_added: min_added = score; best_b = b
                bits[i] = best_b
                base[i] = curr_base
                curr_base += (1 if best_b == 0 else (1 << best_b))
                if not [v for v in freq if v >= curr_base]: break

        fit(l_vals, self.l_bits, self.l_base, 16)
        fit(o1_vals, self.o_bits1, self.o_base1, 4)
        fit(o2_vals, self.o_bits2, self.o_base2, 16)
        fit(o3_vals, self.o_bits3, self.o_base3, 16)
        self.update_bases()

    def compress(self):
        path = self.solve_dp()
        for _ in range(2):
            self.optimize_tables(path)
            path = self.solve_dp()

        bw = BitWriter()
        for b in self.l_bits: bw.write_bits(b, 4)
        for b in self.o_bits3: bw.write_bits(b, 4)
        for b in self.o_bits2: bw.write_bits(b, 4)
        for b in self.o_bits1: bw.write_bits(b, 4)

        last_o = 0
        for p in path:
            ptype = p[1]
            if ptype == 'lit':
                bw.write_bit(1)
                bw.write_bits(self.data[p[0]], 8)
            elif ptype == 'run':
                bw.write_bit(0)
                bw.write_unary(17)
                bw.write_bits(p[2], 16)
                for j in range(p[2]):
                    bw.write_bits(self.data[p[0]+j], 8)
            else: # match
                l, off, l_idx, o_idx = p[2], p[3], p[4], p[5]
                bw.write_bit(0)
                bw.write_unary(l_idx)
                _, l_extra = self.get_idx_and_extra(l, self.l_bits, self.l_base)
                bw.write_bits(l_extra, self.l_bits[l_idx])

                bw.write_unary(o_idx)
                ov = off if off != last_o else 0
                if l == 1: obits_tab = self.o_bits1; obase_tab = self.o_base1
                elif l == 2: obits_tab = self.o_bits2; obase_tab = self.o_base2
                else: obits_tab = self.o_bits3; obase_tab = self.o_base3

                o_extra = ov - obase_tab[o_idx]
                bw.write_bits(o_extra, obits_tab[o_idx])
                last_o = off

        bw.write_bit(0); bw.write_unary(16) # EOS
        return bw.flush()

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("input"); parser.add_argument("output")
    parser.add_argument("--preset", choices=["balanced", "speed", "ratio"], default="balanced")
    args = parser.parse_args()
    if not os.path.exists(args.input): return
    with open(args.input, "rb") as f: d = f.read()

    hash_len = 16
    if args.preset == "balanced": hash_len = 32
    if args.preset == "ratio": hash_len = 64

    comp = Compressor(d, hash_len=hash_len)
    c = comp.compress()
    with open(args.output, "wb") as f: f.write(c)
    ratio = (len(c)/len(d)*100) if len(d) > 0 else 0
    print(f"Compressed {len(d)} -> {len(c)} bytes ({ratio:.2f}%)")

if __name__ == "__main__": main()
