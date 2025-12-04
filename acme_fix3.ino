#include <Arduino.h>

// --- Platform Compatibility ---
#if defined(__AVR__)
    #include <avr/pgmspace.h>
    // AVR specific: Read from Flash if flag is set, otherwise RAM
    #define EXO_READ_BYTE(ptr, offset, is_pgm) \
        (is_pgm ? pgm_read_byte(ptr + offset) : ptr[offset])
#else
    #include <pgmspace.h>
    // ESP32/ARM: Unified memory map, read normally. 
    // PROGMEM is defined by the core but 'pgm_read_byte' isn't strictly needed.
    #define EXO_READ_BYTE(ptr, offset, is_pgm) (ptr[offset])
#endif

// --- Configuration Flags ---
#define INLINE_GET_BITS 0 
#define LITERAL_SEQUENCES_NOT_USED 0    
#define MAX_SEQUENCE_LENGTH_256 0       
#define EXTRA_TABLE_ENTRY_FOR_LENGTH_THREE 0 
#define DONT_REUSE_OFFSET 0             
#define DECRUNCH_FORWARDS 1             

// =============================================================
// --- Global State ---
// =============================================================

// Bitstream reading state
static uint8_t exod_zp_bitbuf = 0;
static uint8_t exod_zp_bit_count = 0;

// Data pointers
static const uint8_t* crunched_data_ptr = nullptr;
static size_t crunched_data_len = 0;
static size_t crunched_data_index = 0;
static bool   source_in_progmem = false; // New flag for memory type

static uint8_t* decompressed_data_ptr = nullptr;
static size_t decompressed_buffer_size = 0;
static size_t decompressed_data_index = 0;

// Offset reuse
#if DONT_REUSE_OFFSET == 0
static uint16_t last_offset_val = 0;
#endif

// Decrunch Table
static uint8_t exod_decrunch_table_data[145];
const uint8_t COMMAND_EOS_MARKER = 0xFF;

// Output Buffer
// On Uno: 256 bytes is safe. 
// On ESP32: You can increase this significantly (e.g., 10240).
uint8_t decompressed_data[256]; 

// =============================================================
// --- Test Data (PROGMEM) ---
// =============================================================
// "Hello" compressed. 
// stored in PROGMEM to save RAM on AVR.
const uint8_t demo_crunched_data[] PROGMEM = { 
    0x11, 0x21, 0x95, 0xB1, 0xB1, 0xBF, 0xC0 
};

// =============================================================
// --- Helper Functions ---
// =============================================================

static bool exod_read_byte_from_stream(uint8_t* byte) {
    if (crunched_data_index < crunched_data_len) {
        // Use the macro to read from either RAM or PROGMEM based on arch/flag
        *byte = EXO_READ_BYTE(crunched_data_ptr, crunched_data_index, source_in_progmem);
        crunched_data_index++;
        return true;
    }
    return false;
}

static int get_one_bit() {
    if (exod_zp_bit_count == 0) {
        if (!exod_read_byte_from_stream(&exod_zp_bitbuf)) {
            return -1; // EOS
        }
        exod_zp_bit_count = 8;
    }
    int bit = (exod_zp_bitbuf & 0x80) ? 1 : 0;
    exod_zp_bitbuf <<= 1;
    exod_zp_bit_count--;
    return bit;
}

static int get_n_bits(uint8_t n) {
    if (n == 0) return 0;
    if (n > 16) return -1; 
    
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
        if (bit == -1) return 0;
        if (bit == 1) break; 
        unary_count++;
        if (unary_count > 16) return 0; 
    }

    if (unary_count == 0) return 1;

    uint16_t value_part = get_n_bits(unary_count);
    if (value_part == (uint16_t)-1) return 0;

    return (1 << unary_count) | value_part;
}

#if LITERAL_SEQUENCES_NOT_USED == 0
static void init_decrunch_table() {
    int k = 0;
    for (int j = 0; j < 4; ++j) {
        for (int i = 0; i < 32; ++i) { 
            exod_decrunch_table_data[k++] = (i << 2) | 0; 
        }
        exod_decrunch_table_data[k++] = (10 << 2) | 1; 
        exod_decrunch_table_data[k++] = (10 << 2) | 1; 
        exod_decrunch_table_data[k++] = (10 << 2) | 2; 
        exod_decrunch_table_data[k++] = (10 << 2) | 2; 
    }
    exod_decrunch_table_data[k++] = (0 << 2) | 3; 
}

static uint8_t get_coded_command_val() {
    int bit_val = get_one_bit();
    if (bit_val == -1) return COMMAND_EOS_MARKER;

    if (bit_val == 0) { 
        int sub_idx = get_n_bits(5);
        if (sub_idx == -1) return COMMAND_EOS_MARKER;
        return exod_decrunch_table_data[108 + sub_idx];
    }

    bit_val = get_one_bit();
    if (bit_val == -1) return COMMAND_EOS_MARKER;
    if (bit_val == 0) { 
        int sub_idx = get_n_bits(4);
        if (sub_idx == -1) return COMMAND_EOS_MARKER;
        return exod_decrunch_table_data[36 + sub_idx];
    }

    bit_val = get_one_bit();
    if (bit_val == -1) return COMMAND_EOS_MARKER;
    if (bit_val == 0) { 
        int sub_idx = get_n_bits(3);
        if (sub_idx == -1) return COMMAND_EOS_MARKER;
        return exod_decrunch_table_data[72 + sub_idx];
    }

    bit_val = get_one_bit();
    if (bit_val == -1) return COMMAND_EOS_MARKER;
    if (bit_val == 0) { 
        int sub_idx = get_n_bits(2);
        if (sub_idx == -1) return COMMAND_EOS_MARKER;
        return exod_decrunch_table_data[0 + sub_idx];
    }

    return exod_decrunch_table_data[144];
}
#endif

// =============================================================
// --- Main Decrunch Logic ---
// =============================================================

/**
 * @param in_data Pointer to crunched data
 * @param in_len Length of crunched data
 * @param out_buffer Pointer to output buffer
 * @param out_max_len Max size of output buffer
 * @param is_progmem Set true if in_data is stored in PROGMEM (Flash)
 */
void exod_decrunch(const uint8_t* in_data, size_t in_len, uint8_t* out_buffer, size_t out_max_len, bool is_progmem) {
    crunched_data_ptr = in_data;
    crunched_data_len = in_len;
    crunched_data_index = 0;
    source_in_progmem = is_progmem; // Store memory type flag

    decompressed_data_ptr = out_buffer;
    decompressed_buffer_size = out_max_len;
    decompressed_data_index = 0;
    exod_zp_bit_count = 0;
    exod_zp_bitbuf = 0;
    
    #if DONT_REUSE_OFFSET == 0
    last_offset_val = 0;
    #endif

    #if LITERAL_SEQUENCES_NOT_USED == 0
    init_decrunch_table();
    #endif

    while (decompressed_data_index < decompressed_buffer_size) {
        uint16_t length = 0;
        uint16_t offset = 0;
        bool is_sequence = false;

        #if LITERAL_SEQUENCES_NOT_USED == 0
        uint8_t command_byte = get_coded_command_val();
        if (command_byte == COMMAND_EOS_MARKER) break; 

        uint8_t type = command_byte & 3;
        uint8_t val_from_table = command_byte >> 2;

        if (type == 0) { 
            length = val_from_table + 1;
            for (uint16_t i = 0; i < length; ++i) {
                if (decompressed_data_index >= decompressed_buffer_size) break;
                int literal_byte = get_n_bits(8);
                if (literal_byte == -1) return;
                decompressed_data_ptr[decompressed_data_index++] = (uint8_t)literal_byte;
            }
        } else {
            is_sequence = true;
            if (type == 1) { 
                length = 2; 
                int raw = get_n_bits(val_from_table);
                if(raw == -1) return;
                offset = raw + 1;
            } else if (type == 2) { 
                length = 3; 
                int raw = get_n_bits(val_from_table);
                if(raw == -1) return;
                offset = raw + 1;
            } else { 
                length = get_gamma_coded_value();
                if (length == 0) return;
                length += 3; 

                #if DONT_REUSE_OFFSET == 0
                int bit = get_one_bit();
                if (bit == -1) return;
                if (bit == 1) {
                    offset = last_offset_val;
                } else {
                    offset = get_gamma_coded_value();
                    if (offset == 0) return;
                    last_offset_val = offset;
                }
                #else
                offset = get_gamma_coded_value();
                if (offset == 0) return;
                #endif
            }
        }
        #endif

        if (is_sequence) {
            if (offset == 0 || offset > decompressed_data_index) {
                Serial.println(F("Err: Offset"));
                return;
            }
            for (uint16_t i = 0; i < length; ++i) {
                if (decompressed_data_index >= decompressed_buffer_size) break;
                decompressed_data_ptr[decompressed_data_index] = decompressed_data_ptr[decompressed_data_index - offset];
                decompressed_data_index++;
            }
        }
    }
}

// =============================================================
// --- Setup & Loop ---
// =============================================================

void setup() {
    Serial.begin(115200);
    while (!Serial);
    Serial.println(F("\n--- Exomizer PROGMEM Test (Uno/ESP32) ---"));

    // Clear output buffer
    memset(decompressed_data, 0, sizeof(decompressed_data));

    // Call decrunch
    // Notice the last argument: 'true' indicates data is in PROGMEM
    exod_decrunch(demo_crunched_data, sizeof(demo_crunched_data), decompressed_data, sizeof(decompressed_data), true);

    // Output results
    Serial.print(F("Decoded: ["));
    for (size_t i = 0; i < decompressed_data_index; i++) {
        char c = (char)decompressed_data[i];
        Serial.print(isprint(c) ? c : '.');
    }
    Serial.println(F("]"));
}

void loop() {
    // Idle
}
