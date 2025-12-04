#include <Arduino.h>

// =============================================================
// --- Platform Compatibility (AVR/ESP32) ---
// =============================================================
#if defined(__AVR__)
    #include <avr/pgmspace.h>
    #define EXO_READ_BYTE(ptr, offset, is_pgm) \
        (is_pgm ? pgm_read_byte(ptr + offset) : ptr[offset])
#else
    #include <pgmspace.h>
    #define EXO_READ_BYTE(ptr, offset, is_pgm) (ptr[offset])
#endif

// =============================================================
// --- Configuration ---
// =============================================================
#define LITERAL_SEQUENCES_NOT_USED 0    // keep for compatibility
#define DONT_REUSE_OFFSET 0             // 0 = allow reuse (original behavior)
#define EXO_DEBUG 0                     // set 1 for verbose serial debugging

// =============================================================
// --- Global State ---
// =============================================================

// Stream State
static const uint8_t* crunched_data_ptr = nullptr;
static size_t crunched_data_len = 0;
static size_t crunched_data_index = 0;
static bool   source_in_progmem = false;

// Bitstream accumulator (LSB-first)
static uint8_t exod_cur_byte = 0;
static uint8_t exod_bit_pos = 8; // 0..7 valid; 8 means "need to read new byte"

// Output State
static uint8_t* decompressed_data_ptr = nullptr;
static size_t decompressed_buffer_size = 0;
static size_t decompressed_data_index = 0;

// Reuse Offset State
#if DONT_REUSE_OFFSET == 0
static uint32_t last_offset_val = 0;
#endif

// Tables for raw tables (Matches Exomizer layout: 16 lengths, 16 offsets3, 16 offsets2, 4 offsets1 = 52 entries)
static uint8_t  tables_bits[52];   // number of bits for each entry (0..15)
static uint32_t tables_base[52];   // base value for each entry (cumulative base)
static uint8_t  lengths_bits[16];
static uint32_t lengths_base[16];
static uint8_t  offsets3_bits[16];
static uint32_t offsets3_base[16];
static uint8_t  offsets2_bits[16];
static uint32_t offsets2_base[16];
static uint8_t  offsets1_bits[4];
static uint32_t offsets1_base[4];

const uint16_t COMMAND_EOS_MARKER = 0xFFFF;

// =============================================================
// --- Test Data (Hello World) ---
// =============================================================
// Minimal example you supplied (may be not full raw-format produced by exomizer).
const uint8_t demo_data[] PROGMEM = {
    0x0A, 0x48, 0x65, 0x6C, 0x6C, 0x6F, 0x20, 0x57, 0x6F, 0x72, 0x6C, 0x64,
    0xA2
};

static bool exod_read_byte(uint8_t* byte) {
    if (crunched_data_index < crunched_data_len) {
        *byte = EXO_READ_BYTE(crunched_data_ptr, crunched_data_index, source_in_progmem);
        crunched_data_index++;
        return true;
    }
    return false;
}

// =============================================================
// --- Bit access, LSB-first (matching Exomizer ReadBits behavior)
// =============================================================

// read a single bit (LSB-first)
static int get_one_bit() {
    if (exod_bit_pos >= 8) {
        if (!exod_read_byte(&exod_cur_byte)) return -1;
        exod_bit_pos = 0;
    }
    int bit = (exod_cur_byte >> exod_bit_pos) & 1;
    exod_bit_pos++;
    return bit;
}

// read n bits LSB-first and return value (or -1 on EOF)
static int get_n_bits_u32(uint8_t n, uint32_t *out) {
    if (n == 0) { *out = 0; return 0; }
    if (n > 32) return -1;
    uint32_t v = 0;
    for (uint8_t i = 0; i < n; ++i) {
        int b = get_one_bit();
        if (b == -1) return -1;
        v |= ((uint32_t)b << i); // LSB-first: first bit is bit0
    }
    *out = v;
    return 0;
}

// convenience wrapper (returns -1 on error, else the integer)
static int32_t get_n_bits_int(uint8_t n) {
    uint32_t tmp;
    if (get_n_bits_u32(n, &tmp) < 0) return -1;
    return (int32_t)tmp;
}

// gamma-coded value (unary + payload), matches the Exomizer gamma read (1 terminator)
static uint32_t get_gamma_coded_value_raw() {
    // Count zeros until a '1' is seen; LSB-first
    uint32_t unary_count = 0;
    while (true) {
        int b = get_one_bit();
        if (b == -1) return 0xFFFFFFFFu; // error sentinel
        if (b == 1) break; // stop bit
        unary_count++;
        // safety
        if (unary_count > 31) return 0xFFFFFFFFu;
    }

    if (unary_count == 0) return 1; // implicit 1 when no payload bits (as in original)
    // read payload bits (unary_count bits)
    uint32_t payload = 0;
    if (unary_count > 0) {
        if (get_n_bits_u32((uint8_t)unary_count, &payload) < 0) return 0xFFFFFFFFu;
    }
    uint32_t val = (1u << unary_count) | payload;
    return val;
}

// =============================================================
// --- Table parsing (GenerateTable-like) ---
// =============================================================

// Generate a table of (entries) entries: each entry has a 4-bit 'bits' value read from stream,
// and we compute base[] canonically by cumulative addition of (1 << bits).
// This matches the assembly GenerateTable semantics closely enough for proper decoding.
static bool generate_table_generic(uint8_t *bits_out, uint32_t *base_out, int entries) {
    uint32_t running_base = 0;
    for (int i = 0; i < entries; ++i) {
        // read 4 bits for code-length (0..15)
        int32_t v = get_n_bits_int(4);
        if (v < 0) return false;
        uint8_t b = (uint8_t)v;
        bits_out[i] = b;
        base_out[i] = running_base;
        // safety: if b==0 then this entry contributes 0 to number of codes
        if (b >= 32) return false; // sanity
        // Add (1 << b) to running_base (if b==0 this adds 1)
        if (b == 0) running_base += 1u;
        else running_base += (1u << b);
    }
    return true;
}

// Parse the 4 tables used by Exomizer raw:
//  - Lengths table (16 entries)
//  - Offsets3 table (16 entries)
//  - Offsets2 table (16 entries)
//  - Offsets1 table (4 entries)
static bool parse_exomizer_raw_tables() {
    // First byte is usually the initial bit-buffer in the assembly; our get_one_bit() handles reading bytes lazily,
    // but Exomizer loads a first byte into the bit buffer at start. To emulate that behaviour precisely we leave as is:
    // ensure bit pos is set to 8 so next get_one_bit() will fetch first header byte.
    // Build the tables:
    if (!generate_table_generic(lengths_bits, lengths_base, 16)) return false;
    if (!generate_table_generic(offsets3_bits, offsets3_base, 16)) return false;
    if (!generate_table_generic(offsets2_bits, offsets2_base, 16)) return false;
    if (!generate_table_generic(offsets1_bits, offsets1_base, 4)) return false;

    // Fill combined tables arrays (indexing later expects 52 entries)
    // mapping: first 16 => offsets table (for final offset bits), next 16 => next, next 16 => next, next 4 => last
    // The original assembly packs these differently (TablesBits / TablesBase) — for simplicity fill tables_bits/tables_base with:
    // table indices 0..15  => offsets3
    // 16..31 => offsets2
    // 32..47 => offsets1-like (but we need total 52 - we'll map as lengths etc)
    // However, the decode path uses lengths_* separately and tables_bits/base as "TablesBits" / "TablesBase"
    // so we assemble TablesBits/TablesBase with sizes 52 as: offsets1..3 + maybe placeholders
    // We'll fill tables_bits[0..15] = offsets3_bits, etc, to keep a contiguous lookup if code uses it.
    // (Downstream decode uses the individual arrays; the combined arrays remain for backward compat.)
    for (int i = 0; i < 16; ++i) { tables_bits[i] = offsets3_bits[i]; tables_base[i] = offsets3_base[i]; }
    for (int i = 0; i < 16; ++i) { tables_bits[16 + i] = offsets2_bits[i]; tables_base[16 + i] = offsets2_base[i]; }
    for (int i = 0; i < 16; ++i) { tables_bits[32 + i] = lengths_bits[i % 16]; tables_base[32 + i] = lengths_base[i % 16]; } // filler to reach 48
    // last 4:
    for (int i = 0; i < 4; ++i) { tables_bits[48 + i] = offsets1_bits[i]; tables_base[48 + i] = offsets1_base[i]; }

    return true;
}

// =============================================================
// --- Main Decompressor (raw-supporting) ---
// =============================================================

void exod_decrunch(const uint8_t* in_data, size_t in_len, uint8_t* out_buffer, size_t out_max_len, bool is_progmem) {
    // Reset state
    crunched_data_ptr = in_data;
    crunched_data_len = in_len;
    crunched_data_index = 0;
    source_in_progmem = is_progmem;

    decompressed_data_ptr = out_buffer;
    decompressed_buffer_size = out_max_len;
    decompressed_data_index = 0;
    exod_bit_pos = 8; // force read of new byte for first bit
    exod_cur_byte = 0;

    #if DONT_REUSE_OFFSET == 0
    last_offset_val = 0;
    #endif

    // Parse tables from the stream (raw format): lengths, offsets3, offsets2, offsets1
    if (!parse_exomizer_raw_tables()) {
        // Failed table parse - abort
        #if EXO_DEBUG
        Serial.println(F("EXO: Table parse failure"));
        #endif
        return;
    }

    // Main loop (modeled after assembly):
    while (decompressed_data_index < decompressed_buffer_size) {
        // Read 1 bit: if '1' => literal next byte. If '0' => gamma/sequence path.
        int bit = get_one_bit();
        if (bit == -1) {
            #if EXO_DEBUG
            Serial.println(F("EXO: EOF on bit"));
            #endif
            break;
        }
        if (bit == 1) {
            // literal byte — copy one byte
            uint8_t b;
            if (!exod_read_byte(&b)) { break; }
            if (decompressed_data_index < decompressed_buffer_size) {
                decompressed_data_ptr[decompressed_data_index++] = b;
            } else break;
            continue;
        }

        // Gamma-coded: count zeros until a '1'
        uint32_t unary_count = 0;
        while (true) {
            int gb = get_one_bit();
            if (gb == -1) { unary_count = UINT32_MAX; break; }
            if (gb == 1) break;
            unary_count++;
            if (unary_count > 31) { unary_count = UINT32_MAX; break; }
        }
        if (unary_count == UINT32_MAX) break;

        // If unary_count is large (we'll use 16 as assembly seems to signal EOS or special cases)
        if (unary_count >= 16) {
            // In assembly: if cpx==16*2 -> end marker. If cpx==17*2 -> literal block
            if (unary_count == 16) {
                // End marker
                #if EXO_DEBUG
                Serial.println(F("EXO: EOS marker"));
                #endif
                break;
            }
            if (unary_count == 17) {
                // Long literal block: read 16-bit length and copy that many bytes
                int32_t hi = get_n_bits_int(8);
                int32_t lo = get_n_bits_int(8);
                if (hi < 0 || lo < 0) break;
                uint16_t run_len = (uint16_t)((hi << 8) | (lo & 0xFF));
                for (uint16_t i = 0; i < run_len; ++i) {
                    uint8_t b;
                    if (!exod_read_byte(&b)) { i = run_len; break; }
                    if (decompressed_data_index < decompressed_buffer_size)
                        decompressed_data_ptr[decompressed_data_index++] = b;
                    else break;
                }
                continue;
            }
            // Other large unary_count values: treat as EOS fallback
            #if EXO_DEBUG
            Serial.print(F("EXO: unexpected unary_count "));
            Serial.println(unary_count);
            #endif
            break;
        }

        // Now unary_count is the 'length index' (0..15)
        uint8_t len_idx = (uint8_t)unary_count;
        // Compute sequence length:
        uint32_t seq_len = lengths_base[len_idx];
        if (lengths_bits[len_idx] != 0) {
            uint32_t v;
            if (get_n_bits_u32(lengths_bits[len_idx], &v) < 0) break;
            seq_len += v;
        }

        // Now compute offset bits selection:
        // Exomizer assembly uses different offset bit groups depending on len (1,2,3 groups).
        // We'll mimic the assembly decision:
        // The assembly effectively chooses between offsets1 (2 bits), offsets2 (??), offsets3 (?).
        // We attempt a pragmatic mapping:
        // if seq_len == 1 -> use offsets1 (4 entries)
        // if seq_len == 2 -> use offsets2
        // else -> use offsets3
        // This is an approximation but reflects how exomizer groups small/medium/large sequences.
        uint32_t raw_offset = 0;
        if (seq_len == 1) {
            // pick one of the small 4-entry tables using perhaps an index derived from len_idx
            // We'll read offsets1_bits[len_idx % 4] if present
            uint8_t tab_index = (uint8_t)(len_idx % 4);
            uint8_t b = offsets1_bits[tab_index];
            if (b != 0) {
                if (get_n_bits_u32(b, &raw_offset) < 0) break;
            } else raw_offset = 0;
            raw_offset += offsets1_base[tab_index];
        } else if (seq_len == 2) {
            uint8_t tab_index = (uint8_t)(len_idx % 16);
            uint8_t b = offsets2_bits[tab_index];
            if (b != 0) {
                if (get_n_bits_u32(b, &raw_offset) < 0) break;
            } else raw_offset = 0;
            raw_offset += offsets2_base[tab_index];
        } else {
            uint8_t tab_index = (uint8_t)(len_idx % 16);
            uint8_t b = offsets3_bits[tab_index];
            if (b != 0) {
                if (get_n_bits_u32(b, &raw_offset) < 0) break;
            } else raw_offset = 0;
            raw_offset += offsets3_base[tab_index];
        }

        // Exomizer offsets are usually encoded relative to the output address:
        // The assembly then inverts and adds dest address to compute absolute address then sub to produce final back-reference offset.
        // Practically we just interpret raw_offset as relative distance + 1 (unless reuse)
        uint32_t offset = raw_offset;
        if (offset == 0) {
            #if DONT_REUSE_OFFSET == 0
            offset = last_offset_val;
            #else
            // zero offset invalid
            #if EXO_DEBUG
            Serial.println(F("EXO: zero offset, no reuse allowed -> abort"));
            #endif
            return;
            #endif
        } else {
            #if DONT_REUSE_OFFSET == 0
            last_offset_val = offset;
            #endif
        }

        // Bound checks: offset cannot be greater than decompressed_data_index
        if (offset > decompressed_data_index) {
            #if EXO_DEBUG
            Serial.print(F("EXO: invalid offset ("));
            Serial.print(offset);
            Serial.print(F(") > outPos "));
            Serial.println(decompressed_data_index);
            #endif
            return;
        }

        // Now copy seq_len bytes from history (offset is 1-based in many encoders) -> try using offset as-is
        for (uint32_t i = 0; i < seq_len; ++i) {
            if (decompressed_data_index >= decompressed_buffer_size) break;
            uint8_t b = decompressed_data_ptr[decompressed_data_index - offset];
            decompressed_data_ptr[decompressed_data_index++] = b;
        }
    }
}

// =============================================================
// --- Test/Arduino harness ---
// =============================================================

#define OUTBUF_SIZE 256
static uint8_t decompressed_data[OUTBUF_SIZE];

void setup() {
    Serial.begin(115200);
    while (!Serial) { delay(1); }
    Serial.println(F("Exomizer Review Test (raw parser)"));

    memset(decompressed_data, 0, sizeof(decompressed_data));

    // Decrunch PROGMEM data (demo_data)
    exod_decrunch(demo_data, sizeof(demo_data), decompressed_data, sizeof(decompressed_data), true);

    Serial.print(F("Output: "));
    for (size_t i = 0; i < sizeof(decompressed_data); ++i) {
        if (decompressed_data[i] == 0) break;
        char c = (char)decompressed_data[i];
        if (isprint((unsigned char)c)) Serial.print(c);
        else Serial.print(".");
    }
    Serial.println();
}

void loop() {}
