import sys
import argparse
import os

class BitWriter:
    def __init__(self):
        self.data = bytearray()
        self.current_byte = 0
        self.bit_count = 0

    def write_bit(self, bit):
        if bit:
            self.current_byte |= (1 << self.bit_count)
        self.bit_count += 1
        if self.bit_count == 8:
            self.data.append(self.current_byte)
            self.current_byte = 0
            self.bit_count = 0

    def write_bits(self, val, count):
        for i in range(count):
            self.write_bit((val >> i) & 1)

    def write_unary(self, count):
        for _ in range(count):
            self.write_bit(0)
        self.write_bit(1)

    def flush(self):
        if self.bit_count > 0:
            self.data.append(self.current_byte)
        return self.data

def compress(data, window_size=32767, min_match=4):
    bw = BitWriter()

    # Universal Tables: 1st entry covers almost everything
    # We use 15 bits which allows up to 32767 length/offset in the first table entry.
    l_bits = [15] + [0]*15
    o3_bits = [15] + [0]*15
    o2_bits = [15] + [0]*15
    o1_bits = [15] + [0]*3

    for b in l_bits: bw.write_bits(b, 4)
    for b in o3_bits: bw.write_bits(b, 4)
    for b in o2_bits: bw.write_bits(b, 4)
    for b in o1_bits: bw.write_bits(b, 4)

    pos = 0
    last_offset = 0

    while pos < len(data):
        best_len = 0
        best_off = 0

        # LZ77 search
        if pos > 0:
            max_off = min(pos, window_size)
            # Optimization: check matches at current position
            for off in range(1, max_off + 1):
                l = 0
                while pos + l < len(data) and data[pos + l] == data[pos - off + l]:
                    l += 1
                    if l >= 32767: break
                if l > best_len:
                    best_len = l
                    best_off = off

        if best_len >= min_match:
            bw.write_bit(0) # Sequence
            bw.write_bit(1) # len_idx = 0 (base[0]=0)
            bw.write_bits(best_len, 15)

            if best_off == last_offset:
                bw.write_bits(0, 15) # Trigger reuse
            else:
                bw.write_bits(best_off, 15)
                last_offset = best_off
            pos += best_len
        else:
            bw.write_bit(1) # Literal
            bw.write_bits(data[pos], 8)
            pos += 1

    # EOS
    bw.write_bit(0)
    bw.write_unary(16)

    return bw.flush()

def to_header(data, orig_len, var_name):
    out = []
    out.append("#include <stdint.h>")
    out.append("#if defined(__AVR__)")
    out.append("  #include <avr/pgmspace.h>")
    out.append("  #define EXO_PROGMEM PROGMEM")
    out.append("#else")
    out.append("  #define EXO_PROGMEM")
    out.append("#endif\n")

    out.append(f"const uint8_t {var_name}[] EXO_PROGMEM = {{")
    for i in range(0, len(data), 12):
        chunk = data[i:i+12]
        hex_chunk = ", ".join([f"0x{b:02x}" for b in chunk])
        comma = "," if i + 12 < len(data) else ""
        out.append(f"    {hex_chunk}{comma}")
    out.append("};\n")

    out.append(f"const uint32_t {var_name}_len = {len(data)};")
    out.append(f"const uint32_t {var_name}_orig_len = {orig_len};")

    return "\n".join(out)

def main():
    parser = argparse.ArgumentParser(description="Exomizer Simple Compressor for Arduino")
    parser.add_argument("input", help="Input file")
    parser.add_argument("output", help="Output file (.exo or .h)")
    parser.add_argument("--preset", choices=["balanced", "speed", "ratio"], default="balanced", help="Compression preset")
    parser.add_argument("--name", help="Variable name for header output (default: derived from filename)")

    args = parser.parse_args()

    presets = {
        "balanced": {"window": 32767, "min_match": 4},
        "speed":    {"window": 4096,  "min_match": 6},
        "ratio":    {"window": 65535, "min_match": 3}, # Note: decompressor currently caps at 15 bits (32767) if using universal table with 15 bits.
    }

    # Actually, let's keep it to 32767 for now as the decompressor get_n_bits might be limited if not careful
    # Re-checking src/exomizer_decompress.cpp: get_n_bits uses uint16_t for return if it was there, but wait.
    # size_t exod_decrunch returns size_t.
    # get_n_bits(ctx, n, &extra) where extra is uint32_t. So 15 bits is fine. 16 bits also fine.

    config = presets[args.preset]
    if args.preset == "ratio":
        # Check if we can safely go higher. 15 bits in table entry is the max in our current scheme.
        config["window"] = 32767

    with open(args.input, "rb") as f:
        data = f.read()

    compressed = compress(data, window_size=config["window"], min_match=config["min_match"])

    if args.output.endswith(".h"):
        var_name = args.name if args.name else os.path.basename(args.input).replace(".", "_")
        header_content = to_header(compressed, len(data), var_name)
        with open(args.output, "w") as f:
            f.write(header_content)
    else:
        with open(args.output, "wb") as f:
            f.write(compressed)

    print(f"Compressed {len(data)} -> {len(compressed)} bytes ({len(compressed)/len(data)*100:.2f}%)")

if __name__ == "__main__":
    main()
