#include "exomizer_decompress.h"
#include <string.h>

#if defined(ARDUINO) && defined(__AVR__)
    #include <avr/pgmspace.h>
    #define EXO_READ_BYTE(ctx, offset) \
        (ctx->source_in_progmem ? pgm_read_byte(ctx->crunched_data_ptr + offset) : ctx->crunched_data_ptr[offset])
#else
    #define EXO_READ_BYTE(ctx, offset) (ctx->crunched_data_ptr[offset])
#endif

static int exod_read_byte(exod_state_t* ctx) {
    if (ctx->read_cb) {
        int b = ctx->read_cb(ctx->userdata);
        if (b != -1) ctx->crunched_data_index++;
        return b;
    }
    if (ctx->crunched_data_index < ctx->crunched_data_len) {
        uint8_t b = EXO_READ_BYTE(ctx, ctx->crunched_data_index);
        ctx->crunched_data_index++;
        return b;
    }
    return -1;
}

static void exod_write_byte(exod_state_t* ctx, uint8_t byte) {
    if (ctx->write_cb) {
        ctx->write_cb(ctx->userdata, byte);
    }

    if (ctx->decompressed_data_ptr) {
        if (ctx->read_cb && ctx->write_cb) { // standard streaming with window
            ctx->decompressed_data_ptr[ctx->decompressed_data_index % ctx->decompressed_buffer_size] = byte;
        } else if (ctx->decompressed_data_index < ctx->decompressed_buffer_size) {
            ctx->decompressed_data_ptr[ctx->decompressed_data_index] = byte;
        }
    }
    ctx->decompressed_data_index++;
}

static int get_one_bit(exod_state_t* ctx) {
    if (ctx->bit_count == 0) {
        int b = exod_read_byte(ctx);
        if (b == -1) return -1;
        ctx->bitbuf = (uint8_t)b;
        ctx->bit_count = 8;
    }
    int bit = ctx->bitbuf & 1;
    ctx->bitbuf >>= 1;
    ctx->bit_count--;
    return bit;
}

static int get_n_bits(exod_state_t* ctx, uint8_t n, uint32_t *out) {
    uint32_t v = 0;
    for (uint8_t i = 0; i < n; ++i) {
        int b = get_one_bit(ctx);
        if (b == -1) return -1;
        v |= ((uint32_t)b << i);
    }
    *out = v;
    return 0;
}

static bool generate_table(exod_state_t* ctx, uint8_t *bits, uint32_t *base, int entries) {
    uint32_t running_base = 0;
    for (int i = 0; i < entries; ++i) {
        uint32_t v;
        if (get_n_bits(ctx, 4, &v) < 0) return false;
        bits[i] = (uint8_t)v;
        base[i] = running_base;
        if (bits[i] == 0) running_base += 1u;
        else running_base += (1u << bits[i]);
    }
    return true;
}

static int exod_decrunch_internal(exod_state_t* ctx, size_t limit_idx, uint8_t* out_byte, size_t start_offset, size_t end_offset);

static uint8_t exod_get_history(exod_state_t* ctx, uint32_t offset) {
    if (ctx->decompressed_data_ptr) {
        uint32_t pos = (uint32_t)(ctx->decompressed_data_index - offset);
        if (ctx->read_cb && ctx->write_cb) {
            return ctx->decompressed_data_ptr[pos % ctx->decompressed_buffer_size];
        } else {
            return ctx->decompressed_data_ptr[pos];
        }
    } else {
        exod_state_t sub_state = *ctx;
        if (sub_state.seek_cb) {
            sub_state.seek_cb(sub_state.userdata, ctx->bitstream_data_index);
        }
        sub_state.crunched_data_index = ctx->bitstream_data_index;
        sub_state.bitbuf = ctx->bitstream_data_bitbuf;
        sub_state.bit_count = ctx->bitstream_data_bit_count;
        sub_state.decompressed_data_index = 0;
        sub_state.write_cb = NULL;
        sub_state.last_offset_val = 0;

        uint8_t result_byte = 0;
        exod_decrunch_internal(&sub_state, ctx->decompressed_data_index - offset, &result_byte, 0, (size_t)-1);

        if (ctx->seek_cb) {
            ctx->seek_cb(ctx->userdata, ctx->crunched_data_index);
        }
        return result_byte;
    }
}

static int exod_decrunch_internal(exod_state_t* ctx, size_t limit_idx, uint8_t* out_byte, size_t start_offset, size_t end_offset) {
    while (!ctx->stop_decompression && (ctx->write_cb || ctx->decompressed_data_ptr || limit_idx != (size_t)-1)) {
        if (limit_idx != (size_t)-1 && ctx->decompressed_data_index > limit_idx) break;
        if (end_offset != (size_t)-1 && ctx->decompressed_data_index >= end_offset) {
            ctx->stop_decompression = true;
            break;
        }

        int bit = get_one_bit(ctx);
        if (bit == -1) break;
        if (bit == 1) {
            uint32_t b;
            if (get_n_bits(ctx, 8, &b) < 0) break;
            if (ctx->decompressed_data_index == limit_idx) {
                if (out_byte) *out_byte = (uint8_t)b;
                ctx->decompressed_data_index++;
                return 1;
            }
            if (ctx->decompressed_data_index >= start_offset) {
                exod_write_byte(ctx, (uint8_t)b);
            } else {
                ctx->decompressed_data_index++;
            }
            continue;
        }

        uint32_t len_idx = 0;
        while (true) {
            int b = get_one_bit(ctx);
            if (b == 1) break;
            if (b == -1) { len_idx = 0xFFFFFFFFu; break; }
            len_idx++;
        }
        if (len_idx == 0xFFFFFFFFu) break;
        if (len_idx == 16) { ctx->eos_reached = true; break; }

        if (len_idx == 17) {
            uint32_t run_len;
            if (get_n_bits(ctx, 16, &run_len) < 0) break;
            uint32_t start = (uint32_t)ctx->decompressed_data_index;
            if (limit_idx != (size_t)-1 && (limit_idx < start || limit_idx >= start + run_len)) {
                for (uint32_t i = 0; i < run_len; ++i) { uint32_t dummy; get_n_bits(ctx, 8, &dummy); }
                ctx->decompressed_data_index += run_len;
            } else if (start + run_len <= start_offset) {
                for (uint32_t i = 0; i < run_len; ++i) { uint32_t dummy; get_n_bits(ctx, 8, &dummy); }
                ctx->decompressed_data_index += run_len;
            } else {
                for (uint32_t i = 0; i < run_len; ++i) {
                    uint32_t val;
                    if (get_n_bits(ctx, 8, &val) < 0) break;
                    if (ctx->decompressed_data_index == limit_idx) {
                        if (out_byte) *out_byte = (uint8_t)val;
                        ctx->decompressed_data_index++;
                        return 1;
                    }
                    if (ctx->decompressed_data_index >= start_offset) {
                        exod_write_byte(ctx, (uint8_t)val);
                        if (end_offset != (size_t)-1 && ctx->decompressed_data_index >= end_offset) {
                            ctx->stop_decompression = true; break;
                        }
                    } else {
                        ctx->decompressed_data_index++;
                    }
                }
            }
            continue;
        }

        if (len_idx > 15) break;

        uint32_t seq_len = ctx->lengths_base[len_idx];
        if (ctx->lengths_bits[len_idx] > 0) {
            uint32_t extra;
            if (get_n_bits(ctx, ctx->lengths_bits[len_idx], &extra) < 0) break;
            seq_len += extra;
        }

        uint32_t off_idx = 0;
        while (true) {
            int b = get_one_bit(ctx);
            if (b == 1) break;
            if (b == -1) { off_idx = 0xFFFFFFFFu; break; }
            off_idx++;
        }
        if (off_idx == 0xFFFFFFFFu) break;

        uint32_t off_val = 0;
        uint32_t extra;
        if (seq_len == 1) {
            if (off_idx >= 4) break;
            if (get_n_bits(ctx, ctx->offsets1_bits[off_idx], &extra) < 0) break;
            off_val = ctx->offsets1_base[off_idx] + extra;
        } else if (seq_len == 2) {
            if (off_idx >= 16) break;
            if (get_n_bits(ctx, ctx->offsets2_bits[off_idx], &extra) < 0) break;
            off_val = ctx->offsets2_base[off_idx] + extra;
        } else {
            if (off_idx >= 16) break;
            if (get_n_bits(ctx, ctx->offsets3_bits[off_idx], &extra) < 0) break;
            off_val = ctx->offsets3_base[off_idx] + extra;
        }

        if (off_val == 0) off_val = ctx->last_offset_val;
        else ctx->last_offset_val = off_val;

        if (off_val == 0 || off_val > ctx->decompressed_data_index) break;
        if (ctx->write_cb && ctx->decompressed_data_ptr && off_val > ctx->decompressed_buffer_size) break;

        uint32_t start = (uint32_t)ctx->decompressed_data_index;
        if (limit_idx != (size_t)-1 && (limit_idx < start || limit_idx >= start + seq_len)) {
            ctx->decompressed_data_index += seq_len;
        } else if (start + seq_len <= start_offset) {
            ctx->decompressed_data_index += seq_len;
        } else {
            for (uint32_t i = 0; i < seq_len; ++i) {
                uint8_t b = exod_get_history(ctx, off_val);
                if (ctx->decompressed_data_index == limit_idx) {
                    if (out_byte) *out_byte = b;
                    ctx->decompressed_data_index++;
                    return 1;
                }
                if (ctx->decompressed_data_index >= start_offset) {
                    exod_write_byte(ctx, b);
                    if (end_offset != (size_t)-1 && ctx->decompressed_data_index >= end_offset) {
                        ctx->stop_decompression = true; break;
                    }
                } else {
                    ctx->decompressed_data_index++;
                }
            }
        }
    }
    return 0;
}

size_t exod_decrunch(const uint8_t* in_data, size_t in_len, uint8_t* out_buffer, size_t out_max_len, bool is_progmem) {
    exod_state_t state;
    memset(&state, 0, sizeof(exod_state_t));
    state.crunched_data_ptr = in_data;
    state.crunched_data_len = in_len;
    state.source_in_progmem = is_progmem;
    state.decompressed_data_ptr = out_buffer;
    state.decompressed_buffer_size = out_max_len;

    if (!generate_table(&state, state.lengths_bits, state.lengths_base, 16)) return (size_t)-1;
    if (!generate_table(&state, state.offsets3_bits, state.offsets3_base, 16)) return (size_t)-1;
    if (!generate_table(&state, state.offsets2_bits, state.offsets2_base, 16)) return (size_t)-1;
    if (!generate_table(&state, state.offsets1_bits, state.offsets1_base, 4)) return (size_t)-1;

    state.bitstream_data_index = (uint32_t)state.crunched_data_index;
    state.bitstream_data_bitbuf = state.bitbuf;
    state.bitstream_data_bit_count = state.bit_count;

    exod_decrunch_internal(&state, (size_t)-1, NULL, 0, (size_t)-1);
    if (state.decompressed_data_index == 0 && state.eos_reached) return 0;
    if (!state.eos_reached) return (size_t)-1;
    return state.decompressed_data_index;
}

size_t exod_decrunch_streaming(
    exod_read_cb read_func,
    exod_write_cb write_func,
    void* userdata,
    uint8_t* window_buffer,
    size_t window_size
) {
    exod_state_t state;
    memset(&state, 0, sizeof(exod_state_t));
    state.read_cb = read_func;
    state.write_cb = write_func;
    state.userdata = userdata;
    state.decompressed_data_ptr = window_buffer;
    state.decompressed_buffer_size = window_size;

    if (!generate_table(&state, state.lengths_bits, state.lengths_base, 16)) return (size_t)-1;
    if (!generate_table(&state, state.offsets3_bits, state.offsets3_base, 16)) return (size_t)-1;
    if (!generate_table(&state, state.offsets2_bits, state.offsets2_base, 16)) return (size_t)-1;
    if (!generate_table(&state, state.offsets1_bits, state.offsets1_base, 4)) return (size_t)-1;

    exod_decrunch_internal(&state, (size_t)-1, NULL, 0, (size_t)-1);
    return state.decompressed_data_index;
}

size_t exod_decrunch_memoryless(
    const uint8_t* in_data, size_t in_len,
    exod_write_cb write_func,
    void* userdata,
    bool is_progmem
) {
    exod_state_t state;
    memset(&state, 0, sizeof(exod_state_t));
    state.crunched_data_ptr = in_data;
    state.crunched_data_len = in_len;
    state.source_in_progmem = is_progmem;
    state.write_cb = write_func;
    state.userdata = userdata;

    if (!generate_table(&state, state.lengths_bits, state.lengths_base, 16)) return (size_t)-1;
    if (!generate_table(&state, state.offsets3_bits, state.offsets3_base, 16)) return (size_t)-1;
    if (!generate_table(&state, state.offsets2_bits, state.offsets2_base, 16)) return (size_t)-1;
    if (!generate_table(&state, state.offsets1_bits, state.offsets1_base, 4)) return (size_t)-1;

    state.bitstream_data_index = (uint32_t)state.crunched_data_index;
    state.bitstream_data_bitbuf = state.bitbuf;
    state.bitstream_data_bit_count = state.bit_count;

    exod_decrunch_internal(&state, (size_t)-1, NULL, 0, (size_t)-1);
    return state.decompressed_data_index;
}

size_t exod_decrunch_memoryless_streaming(
    exod_read_cb read_func,
    exod_seek_cb seek_func,
    exod_write_cb write_func,
    void* userdata
) {
    exod_state_t state;
    memset(&state, 0, sizeof(exod_state_t));
    state.read_cb = read_func;
    state.seek_cb = seek_func;
    state.write_cb = write_func;
    state.userdata = userdata;

    if (!generate_table(&state, state.lengths_bits, state.lengths_base, 16)) return (size_t)-1;
    if (!generate_table(&state, state.offsets3_bits, state.offsets3_base, 16)) return (size_t)-1;
    if (!generate_table(&state, state.offsets2_bits, state.offsets2_base, 16)) return (size_t)-1;
    if (!generate_table(&state, state.offsets1_bits, state.offsets1_base, 4)) return (size_t)-1;

    state.bitstream_data_index = (uint32_t)state.crunched_data_index;
    state.bitstream_data_bitbuf = state.bitbuf;
    state.bitstream_data_bit_count = state.bit_count;

    exod_decrunch_internal(&state, (size_t)-1, NULL, 0, (size_t)-1);
    return state.decompressed_data_index;
}

size_t exod_decrunch_memoryless_range(
    const uint8_t* in_data, size_t in_len,
    size_t start_offset, size_t length,
    exod_write_cb write_func,
    void* userdata,
    bool is_progmem
) {
    exod_state_t state;
    memset(&state, 0, sizeof(exod_state_t));
    state.crunched_data_ptr = in_data;
    state.crunched_data_len = in_len;
    state.source_in_progmem = is_progmem;
    state.write_cb = write_func;
    state.userdata = userdata;

    if (!generate_table(&state, state.lengths_bits, state.lengths_base, 16)) return (size_t)-1;
    if (!generate_table(&state, state.offsets3_bits, state.offsets3_base, 16)) return (size_t)-1;
    if (!generate_table(&state, state.offsets2_bits, state.offsets2_base, 16)) return (size_t)-1;
    if (!generate_table(&state, state.offsets1_bits, state.offsets1_base, 4)) return (size_t)-1;

    state.bitstream_data_index = (uint32_t)state.crunched_data_index;
    state.bitstream_data_bitbuf = state.bitbuf;
    state.bitstream_data_bit_count = state.bit_count;

    size_t prev_index = 0;
    exod_decrunch_internal(&state, (size_t)-1, NULL, start_offset, start_offset + length);
    return state.decompressed_data_index - start_offset;
}
