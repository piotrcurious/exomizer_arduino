#include <Arduino.h>

// =============================================================
// --- Configuration Flags ---
// =============================================================
#define INLINE_GET_BITS 0 
#define LITERAL_SEQUENCES_NOT_USED 0    // 0 = Standard raw mode (complex table)
#define MAX_SEQUENCE_LENGTH_256 0       // 0 = Max 255, 1 = Max 256
#define EXTRA_TABLE_ENTRY_FOR_LENGTH_THREE 0 
#define DONT_REUSE_OFFSET 0             // 0 = Enable offset reuse (standard)
#define DECRUNCH_FORWARDS 1             // 1 = Forward decrunching

// =============================================================
// --- Global State & Buffers ---
// =============================================================

// Bitstream reading state
static uint8_t exod_zp_bitbuf = 0;
static uint8_t exod_zp_bit_count = 0;

// Data pointers
// NOTE: For large data, crunched_data should be in PROGMEM (requires code modification)
static const uint8_t* crunched_data_ptr = nullptr;
static size_t crunched_data_len = 0;
static size_t crunched_data_index = 0;

static uint8_t* decompressed_data_ptr = nullptr;
static size_t decompressed_buffer_size = 0;
static size_t decompressed_data_index = 0;

// Offset reuse
#if DONT_REUSE_OFFSET == 0
static uint16_t last_offset_val = 0;
#endif

// Decrunch Table
// Size: 145 bytes. Format: (value << 2) | type
static uint8_t exod_decrunch_table_data[145];
const uint8_t COMMAND_EOS_MARKER = 0xFF;

// Output Buffer
uint8_t decompressed_data[256]; 

// =============================================================
// --- Test Data (Manually Constructed for "Hello") ---
// =============================================================
/* Explanation of this stream for "Hello":
   1. Command: Literal, length 5 (val 4). Table Entry [108+4].
      Prefix "0" + Index "00100" = Bits "000100"
   2. Literal Bytes: "Hello" (8 bits each)
   3. Command: EOS (End of Stream). Table Entry [144].
      Prefix "1111"
   
   Stream (Binary): 000100 01001000 01100101 01101100 01101100 01101111 1111
   Packed into bytes (MSB first):
   0x11, 0x21, 0x95, 0xB1, 0xB1, 0xBF, 0xC0
*/
const uint8_t demo_crunched_data[] = { 
    0x11, 0x21, 0x95, 0xB1, 0xB1, 0xBF, 0xC0 
};

// =============================================================
// --- Helper Functions ---
// =============================================================

static bool exod_read_byte_from_stream(uint8_t* byte) {
    if (crunched_data_index < crunched_data_len) {
        *byte = crunched_data_ptr[crunched_data_index++];
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
    // Extract MSB
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
        if (bit == 1) break; // Ends with 1
        unary_count++;
        if (unary_count > 16) return 0; // Safety
    }

    if (unary_count == 0) return 1;

    uint16_t value_part = get_n_bits(unary_count);
    if (value_part == (uint16_t)-1) return 0;

    return (1 << unary_count) | value_part;
}

#if LITERAL_SEQUENCES_NOT_USED == 0
static void init_decrunch_table() {
    int k = 0;
    // The table is generated in 4 passes for different prefix hierarchies
    for (int j = 0; j < 4; ++j) {
        // 32 Literal entries (Type 0)
        for (int i = 0; i < 32; ++i) { 
            exod_decrunch_table_data[k++] = (i << 2) | 0; 
        }
        // 4 Sequence entries (Type 1 or 2)
        // val = 10 bits for offset (standard raw preset)
        exod_decrunch_table_data[k++] = (10 << 2) | 1; // len 2
        exod_decrunch_table_data[k++] = (10 << 2) | 1; // len 2
        exod_decrunch_table_data[k++] = (10 << 2) | 2; // len 3
        exod_decrunch_table_data[k++] = (10 << 2) | 2; // len 3
    }
    // Final entry: Type 3 (gamma sequence)
    exod_decrunch_table_data[k++] = (0 << 2) | 3; 
}

static uint8_t get_coded_command_val() {
    int bit_val = get_one_bit();
    if (bit_val == -1) return COMMAND_EOS_MARKER;

    if (bit_val == 0) { 
        // Prefix "0", 5 bits index -> Table 108..139
        int sub_idx = get_n_bits(5);
        if (sub_idx == -1) return COMMAND_EOS_MARKER;
        return exod_decrunch_table_data[108 + sub_idx];
    }

    bit_val = get_one_bit();
    if (bit_val == -1) return COMMAND_EOS_MARKER;
    if (bit_val == 0) { 
        // Prefix "10", 4 bits index -> Table 36..51
        int sub_idx = get_n_bits(4);
        if (sub_idx == -1) return COMMAND_EOS_MARKER;
        return exod_decrunch_table_data[36 + sub_idx];
    }

    bit_val = get_one_bit();
    if (bit_val == -1) return COMMAND_EOS_MARKER;
    if (bit_val == 0) { 
        // Prefix "110", 3 bits index -> Table 72..79
        int sub_idx = get_n_bits(3);
        if (sub_idx == -1) return COMMAND_EOS_MARKER;
        return exod_decrunch_table_data[72 + sub_idx];
    }

    bit_val = get_one_bit();
    if (bit_val == -1) return COMMAND_EOS_MARKER;
    if (bit_val == 0) { 
        // Prefix "1110", 2 bits index -> Table 0..3
        int sub_idx = get_n_bits(2);
        if (sub_idx == -1) return COMMAND_EOS_MARKER;
        return exod_decrunch_table_data[0 + sub_idx];
    }

    // Prefix "1111" -> EOS
    return exod_decrunch_table_data[144];
}
#endif

// =============================================================
// --- Main Decrunch Logic ---
// =============================================================

void exod_decrunch(const uint8_t* in_data, size_t in_len, uint8_t* out_buffer, size_t out_max_len) {
    crunched_data_ptr = in_data;
    crunched_data_len = in_len;
    crunched_data_index = 0;
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
        if (command_byte == COMMAND_EOS_MARKER) break; // Successfully reached EOS

        uint8_t type = command_byte & 3;
        uint8_t val_from_table = command_byte >> 2;

        if (type == 0) { 
            // --- Literal Run ---
            length = val_from_table + 1;
            for (uint16_t i = 0; i < length; ++i) {
                if (decompressed_data_index >= decompressed_buffer_size) break;
                
                int literal_byte = get_n_bits(8);
                if (literal_byte == -1) return; // Unexpected EOS
                
                decompressed_data_ptr[decompressed_data_index++] = (uint8_t)literal_byte;
            }
        } else {
            // --- Sequence ---
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
            } else { // type == 3 (Gamma)
                length = get_gamma_coded_value();
                if (length == 0) return;
                length += 3; // Adjust for types 1 and 2

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
        #else
        // If LITERAL_SEQUENCES_NOT_USED == 1, implementation requires different first-byte logic
        // (Not implemented in this snippet to keep it clean, as user flag is 0)
        #endif

        if (is_sequence) {
            if (offset == 0 || offset > decompressed_data_index) {
                Serial.println(F("Error: Invalid offset"));
                return;
            }
            // Copy memory from previous output
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
    Serial.println(F("\n--- Exomizer Decruncher Test ---"));

    // 1. Fill buffer with sentinel to prove decompression worked
    memset(decompressed_data, 0, sizeof(decompressed_data));

    // 2. Perform Decrunch
    // Uses the "Hello" demo data defined above.
    // To use your own data, generate it with: `exomizer raw input.bin -o output.exo`
    // Convert output.exo to a hex array.
    exod_decrunch(demo_crunched_data, sizeof(demo_crunched_data), decompressed_data, sizeof(decompressed_data));

    // 3. Print Results
    Serial.print(F("Decompressed Bytes: "));
    Serial.println(decompressed_data_index);

    Serial.print(F("Result (String): ["));
    for (size_t i = 0; i < decompressed_data_index; i++) {
        char c = (char)decompressed_data[i];
        if (isprint(c)) Serial.print(c);
        else Serial.print('.');
    }
    Serial.println(F("]"));

    Serial.print(F("Result (Hex): "));
    for (size_t i = 0; i < decompressed_data_index; i++) {
        if (decompressed_data[i] < 0x10) Serial.print('0');
        Serial.print(decompressed_data[i], HEX);
        Serial.print(' ');
    }
    Serial.println();
}

void loop() {
    delay(1000);
}
