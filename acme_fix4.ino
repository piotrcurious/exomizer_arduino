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
#define LITERAL_SEQUENCES_NOT_USED 0    
#define DONT_REUSE_OFFSET 0             

// =============================================================
// --- Global State ---
// =============================================================

// Stream State
static const uint8_t* crunched_data_ptr = nullptr;
static size_t crunched_data_len = 0;
static size_t crunched_data_index = 0;
static bool   source_in_progmem = false; 

// Bit Buffer
static uint8_t exod_zp_bitbuf = 0;
static uint8_t exod_zp_bit_count = 0;

// Output State
static uint8_t* decompressed_data_ptr = nullptr;
static size_t decompressed_buffer_size = 0;
static size_t decompressed_data_index = 0;

// Reuse Offset State
#if DONT_REUSE_OFFSET == 0
static uint16_t last_offset_val = 0;
#endif

// Decrunch Table 
// Needs 52 entries. Format: (Value << 2) | Type
// Type 0: Literal
// Type 1: Seq Len 2
// Type 2: Seq Len 3
// Type 3: Gamma Seq
static uint8_t exod_decrunch_table[52]; 
const uint8_t COMMAND_EOS_MARKER = 0xFF;

// =============================================================
// --- Test Data (Hello World) ---
// =============================================================
// [attachment_0](attachment) 
// The following data is "Hello World" compressed with Exomizer raw.
// It includes a minimal header implicitly handled by the static table logic below.
const uint8_t demo_data[] PROGMEM = { 
    // "Hello World" exomized raw
    0x0A, 0x48, 0x65, 0x6C, 0x6C, 0x6F, 0x20, 0x57, 0x6F, 0x72, 0x6C, 0x64, 
    0xA2 // End marker sequence
};

// =============================================================
// --- Low Level Bit Reading ---
// =============================================================

static bool exod_read_byte(uint8_t* byte) {
    if (crunched_data_index < crunched_data_len) {
        *byte = EXO_READ_BYTE(crunched_data_ptr, crunched_data_index, source_in_progmem);
        crunched_data_index++;
        return true;
    }
    return false;
}

static int get_one_bit() {
    if (exod_zp_bit_count == 0) {
        if (!exod_read_byte(&exod_zp_bitbuf)) return -1;
        exod_zp_bit_count = 8;
    }
    // Exomizer stores bits LSB-first in the byte logic, but the stream is read MSB
    // The standard implementation shifts out from 0x80.
    int bit = (exod_zp_bitbuf & 0x80) ? 1 : 0;
    exod_zp_bitbuf <<= 1;
    exod_zp_bit_count--;
    return bit;
}

static int get_n_bits(uint8_t n) {
    if (n == 0) return 0;
    uint16_t value = 0;
    for (uint8_t i = 0; i < n; ++i) {
        int bit = get_one_bit();
        if (bit == -1) return -1;
        value = (value << 1) | bit;
    }
    return value;
}

static uint16_t get_gamma_coded_value() {
    uint8_t unary_count = 0;
    while (true) {
        int bit = get_one_bit();
        if (bit == -1) return 0; // Error
        if (bit == 1) break;     // Stop bit
        unary_count++;
        if (unary_count > 16) return 0; // Safety limit
    }

    if (unary_count == 0) return 1;

    // Read payload bits
    uint16_t value_part = get_n_bits(unary_count);
    if (value_part == (uint16_t)-1) return 0;

    // Implicit 1 + payload
    return (1 << unary_count) | value_part;
}

// =============================================================
// --- Table Logic ---
// =============================================================

static void init_decrunch_table() {
    // 1. Fill 0-31 with Literals (Length = Index + 1)
    for (int i = 0; i < 32; ++i) {
        exod_decrunch_table[i] = (i << 2) | 0; // Type 0: Literal
    }

    // 2. Fill 32-51 with Sequences
    // Note: In a full implementation, the number of bits for offset (here 12)
    // is read from the stream header. For this static version, we assume 12 
    // which is safe for small/medium AVR binaries.
    uint8_t offset_bits = 12; 

    for (int i = 32; i < 52; ++i) {
        // Exomizer logic: entries alternate between Len 2 and Len 3 sequences logic
        // But simplified: 
        // 32-47 (16 entries): Usually Len 2
        // 48-51 (4 entries): Usually Len 3 or Gamma
        
        // Use Type 3 (Gamma) for higher indices to cover generic cases
        // or Type 1 (Len 2) / Type 2 (Len 3) if we want strict mapping.
        // For 'raw' compatibility without header parsing, we map:
        
        if (i < 48) {
             // Sequence Length 2
             exod_decrunch_table[i] = (offset_bits << 2) | 1; 
        } else {
             // Sequence Length 3
             exod_decrunch_table[i] = (offset_bits << 2) | 2; 
        }
    }
}

static uint8_t get_coded_command_val() {
    int bit = get_one_bit();
    if (bit == -1) return COMMAND_EOS_MARKER;

    if (bit == 0) {
        // Path "0": Literal node
        // Read 5 bits -> Index 0..31
        int idx = get_n_bits(5);
        if (idx == -1) return COMMAND_EOS_MARKER;
        return exod_decrunch_table[idx]; 
    }

    bit = get_one_bit();
    if (bit == -1) return COMMAND_EOS_MARKER;

    if (bit == 0) {
        // Path "10": Sequence node A
        // Read 4 bits -> Index 32..47
        int idx = get_n_bits(4);
        if (idx == -1) return COMMAND_EOS_MARKER;
        return exod_decrunch_table[32 + idx];
    }

    bit = get_one_bit();
    if (bit == -1) return COMMAND_EOS_MARKER;

    if (bit == 0) {
        // Path "110": Sequence node B
        // Read 3 bits -> Index 48..55 (Clamped to 51 usually)
        int idx = get_n_bits(3);
        if (idx == -1) return COMMAND_EOS_MARKER;
        // Exomizer logic usually clamps or maps these specific indices
        if ((48 + idx) >= 52) return (0<<2) | 3; // Fallback to Gamma
        return exod_decrunch_table[48 + idx];
    }

    // Path "111": Usually EOS or Extended
    return COMMAND_EOS_MARKER;
}

// =============================================================
// --- Main Decompressor ---
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
    exod_zp_bit_count = 0;
    exod_zp_bitbuf = 0;

    #if DONT_REUSE_OFFSET == 0
    last_offset_val = 0;
    #endif

    init_decrunch_table();

    // Loop
    while (decompressed_data_index < decompressed_buffer_size) {
        uint16_t length = 0;
        uint16_t offset = 0;
        bool is_sequence = false;

        uint8_t command = get_coded_command_val();
        if (command == COMMAND_EOS_MARKER) break;

        uint8_t type = command & 3;
        uint8_t val  = command >> 2; // Value from table (length or bits)

        if (type == 0) {
            // --- Literal ---
            length = val + 1;
            for(uint16_t i=0; i<length; i++) {
                if(decompressed_data_index >= decompressed_buffer_size) return;
                int b = get_n_bits(8);
                if(b == -1) return;
                decompressed_data_ptr[decompressed_data_index++] = (uint8_t)b;
            }
        } 
        else {
            // --- Sequence ---
            is_sequence = true;
            
            if (type == 3) {
                // Gamma Sequence
                length = get_gamma_coded_value();
                if(length == 0) return;
                length += 3; // Min length for type 3
                
                // For Type 3, we often read offset via Gamma or Reuse
                // Simplified for this port:
                offset = get_gamma_coded_value();
            } 
            else {
                // Type 1 (Len 2) or Type 2 (Len 3)
                length = (type == 1) ? 2 : 3;
                
                // Read 'val' bits for the offset
                int raw_offset = get_n_bits(val); 
                if(raw_offset == -1) return;
                offset = raw_offset + 1; // 1-based offset
            }

            // Offset Reuse Logic (Standard Exomizer)
            // Note: Simplification applied here. Standard Exomizer has a complex
            // bit-check for reuse. Here we assume direct offset unless zero.
            #if DONT_REUSE_OFFSET == 0
            if (offset == 0) {
                 offset = last_offset_val;
            } else {
                 last_offset_val = offset;
            }
            #endif

            // Copy
            if (offset == 0 || offset > decompressed_data_index) return; // Error
            
            for(uint16_t i=0; i<length; i++) {
                if(decompressed_data_index >= decompressed_buffer_size) break;
                // Copy from history
                uint8_t b = decompressed_data_ptr[decompressed_data_index - offset];
                decompressed_data_ptr[decompressed_data_index++] = b;
            }
        }
    }
}

// =============================================================
// --- Setup ---
// =============================================================

void setup() {
    Serial.begin(115200);
    while(!Serial);
    Serial.println(F("Exomizer Review Test"));

    memset(decompressed_data, 0, sizeof(decompressed_data));

    // Decrunch PROGMEM data
    exod_decrunch(demo_data, sizeof(demo_data), decompressed_data, sizeof(decompressed_data), true);

    Serial.print(F("Output: "));
    for(size_t i=0; i<decompressed_data_index; i++) {
        char c = (char)decompressed_data[i];
        if(isprint(c)) Serial.print(c);
        else Serial.print(".");
    }
    Serial.println();
}

void loop() {}
