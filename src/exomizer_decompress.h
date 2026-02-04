#ifndef EXOMIZER_DECOMPRESS_H
#define EXOMIZER_DECOMPRESS_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    // Stream State
    const uint8_t* crunched_data_ptr;
    size_t crunched_data_len;
    size_t crunched_data_index;
    bool   source_in_progmem;

    // Bitstream accumulator
    uint8_t bitbuf;
    uint8_t bit_count;

    // Output State
    uint8_t* decompressed_data_ptr;
    size_t decompressed_buffer_size;
    size_t decompressed_data_index;

    // Reuse Offset State
    uint32_t last_offset_val;

    // Tables
    uint8_t  lengths_bits[16];
    uint32_t lengths_base[16];
    uint8_t  offsets3_bits[16];
    uint32_t offsets3_base[16];
    uint8_t  offsets2_bits[16];
    uint32_t offsets2_base[16];
    uint8_t  offsets1_bits[4];
    uint32_t offsets1_base[4];
} exod_state_t;

/**
 * @brief Decrunch Exomizer raw data.
 *
 * @param in_data Pointer to crunched data.
 * @param in_len Length of crunched data.
 * @param out_buffer Pointer to output buffer.
 * @param out_max_len Max size of output buffer.
 * @param is_progmem Set true if in_data is stored in PROGMEM (Flash). Only relevant on AVR.
 * @return size_t Number of bytes decompressed, or 0 on error.
 */
size_t exod_decrunch(const uint8_t* in_data, size_t in_len, uint8_t* out_buffer, size_t out_max_len, bool is_progmem);

#ifdef __cplusplus
}
#endif

#endif // EXOMIZER_DECOMPRESS_H
