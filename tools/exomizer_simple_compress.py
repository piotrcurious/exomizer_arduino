import sys

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

def compress(data):
    bw = BitWriter()

    # Universal Tables: 1st entry covers almost everything
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
        search_window = 32767

        # Simple LZ77 search
        if pos > 0:
            for off in range(1, min(pos, search_window) + 1):
                l = 0
                while pos + l < len(data) and data[pos + l] == data[pos - off + l]:
                    l += 1
                    if l >= 32767: break
                if l > best_len:
                    best_len = l
                    best_off = off

        # Threshold for sequence: 3 bytes (because sequence header is quite long in this universal mode)
        # Table (52*4 bits) + 1 bit + (unary 0) + 15 bits len + 15 bits off = a lot.
        # But once we are past the table, it's 1 + 1 + 15 + 15 = 32 bits = 4 bytes.
        # Actually, literal is 1 + 8 = 9 bits.
        # For 4 literals: 36 bits.
        # For sequence of 4: 1 + 1 + 15 + 15 = 32 bits.
        if best_len >= 4:
            bw.write_bit(0) # Sequence
            bw.write_bit(1) # len_idx = 0
            bw.write_bits(best_len, 15) # seq_len (base[0]=0)

            # Offset
            if best_off == last_offset:
                bw.write_bits(0, 15) # Should we use reuse? acme_anytable.ino does if off_val == 0
                # But wait, my table base[0] is 0, so if I write 0, off_val is 0.
                # Yes, that should trigger reuse.
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

if __name__ == "__main__":
    if len(sys.argv) < 3:
        print("Usage: python exomizer_simple_compress.py <input> <output>")
        sys.exit(1)
    with open(sys.argv[1], "rb") as f:
        d = f.read()
    c = compress(d)
    with open(sys.argv[2], "wb") as f:
        f.write(c)
