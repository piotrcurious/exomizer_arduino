#ifndef EXOMIZER_DECOMPRESS_H
#define EXOMIZER_DECOMPRESS_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Callback for reading crunched data.
 * @return The next byte of crunched data, or -1 on EOF/error.
 */
typedef int (*exod_read_cb)(void* userdata);

/**
 * @brief Callback for seeking in the input stream (required for memoryless streaming).
 * @param offset The absolute offset from the start of the crunched data.
 * @return 0 on success, -1 on failure.
 */
typedef int (*exod_seek_cb)(void* userdata, size_t offset);

/**
 * @brief Callback for writing decompressed data.
 */
typedef void (*exod_write_cb)(void* userdata, uint8_t byte);

/**
 * @brief Entropy tables used during decompression.
 * Moved to a separate struct to reduce stack usage during recursion.
 */
typedef struct {
    uint8_t  lengths_bits[16];
    uint32_t lengths_base[16];
    uint8_t  offsets3_bits[16];
    uint32_t offsets3_base[16];
    uint8_t  offsets2_bits[16];
    uint32_t offsets2_base[16];
    uint8_t  offsets1_bits[4];
    uint32_t offsets1_base[4];
} exod_tables_t;

typedef struct {
    // Stream State
    const uint8_t* crunched_data_ptr;
    size_t crunched_data_len;
    size_t crunched_data_index;
    bool   source_in_progmem;

    // Callbacks for streaming
    exod_read_cb  read_cb;
    exod_seek_cb  seek_cb;
    exod_write_cb write_cb;
    void*         userdata;

    // Bitstream accumulator
    uint8_t bitbuf;
    uint8_t bit_count;

    // Output State (for non-streaming or as window buffer for streaming)
    uint8_t* decompressed_data_ptr;
    size_t decompressed_buffer_size;
    size_t decompressed_data_index;

    // Reuse Offset State
    uint32_t last_offset_val;
    bool     eos_reached;
    bool     stop_decompression;

    // Bitstream restart point (after tables)
    uint32_t bitstream_data_index;
    uint8_t  bitstream_data_bitbuf;
    uint8_t  bitstream_data_bit_count;

    // Pointer to tables (shared across recursive calls)
    exod_tables_t* tables;
} exod_state_t;

/**
 * @brief Decrunch Exomizer raw data (Block version).
 */
size_t exod_decrunch(const uint8_t* in_data, size_t in_len, uint8_t* out_buffer, size_t out_max_len, bool is_progmem);

/**
 * @brief Decrunch Exomizer raw data (Streaming version).
 */
size_t exod_decrunch_streaming(
    exod_read_cb read_func,
    exod_write_cb write_func,
    void* userdata,
    uint8_t* window_buffer,
    size_t window_size
);

/**
 * @brief Decrunch Exomizer raw data using minimal memory.
 */
size_t exod_decrunch_memoryless(
    const uint8_t* in_data, size_t in_len,
    exod_write_cb write_func,
    void* userdata,
    bool is_progmem
);

/**
 * @brief Decrunch Exomizer raw data using memoryless streaming.
 */
size_t exod_decrunch_memoryless_streaming(
    exod_read_cb read_func,
    exod_seek_cb seek_func,
    exod_write_cb write_func,
    void* userdata
);

/**
 * @brief Decrunch a specific range of bytes from Exomizer raw data using minimal memory.
 */
size_t exod_decrunch_memoryless_range(
    const uint8_t* in_data, size_t in_len,
    size_t start_offset, size_t length,
    exod_write_cb write_func,
    void* userdata,
    bool is_progmem
);

#ifdef __cplusplus
}
#endif

#endif // EXOMIZER_DECOMPRESS_H
