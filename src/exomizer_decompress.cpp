#include "exomizer_decompress.h"
#include <string.h>

#if defined(ARDUINO) && defined(__AVR__)
    #include <avr/pgmspace.h>
    #define EXO_READ_BYTE(ctx, offset) \
        (ctx->source_in_progmem ? pgm_read_byte(ctx->crunched_data_ptr + offset) : ctx->crunched_data_ptr[offset])
#else
    #define EXO_READ_BYTE(ctx, offset) (ctx->crunched_data_ptr[offset])
#endif

#define EXOD_ERROR ((size_t)-1)
#define MAX_RECURSION_DEPTH 8

#ifdef INSTRUMENT_STACK
extern "C" void instrument_stack();
#else
#define instrument_stack()
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
    if (ctx->write_cb) ctx->write_cb(ctx->userdata, byte);
    if (ctx->decompressed_data_ptr) {
        if (ctx->read_cb && ctx->write_cb) {
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

static int exod_decrunch_internal(exod_state_t* ctx, size_t limit_idx, uint8_t* out_byte, size_t start_offset, size_t end_offset, int depth);

static uint8_t exod_get_history(exod_state_t* ctx, uint32_t offset, int depth) {
    if (ctx->decompressed_data_ptr) {
        uint32_t pos = (uint32_t)(ctx->decompressed_data_index - offset);
        if (ctx->read_cb && ctx->write_cb) {
            return ctx->decompressed_data_ptr[pos % ctx->decompressed_buffer_size];
        } else {
            return ctx->decompressed_data_ptr[pos];
        }
    } else {
        if (depth >= MAX_RECURSION_DEPTH) return 0;
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
        sub_state.stop_decompression = false;
        uint8_t result_byte = 0;
        exod_decrunch_internal(&sub_state, ctx->decompressed_data_index - offset, &result_byte, 0, (size_t)-1, depth + 1);
        if (ctx->seek_cb) {
            ctx->seek_cb(ctx->userdata, (size_t)ctx->crunched_data_index);
        }
        return result_byte;
    }
}

static int exod_decrunch_internal(exod_state_t* ctx, size_t limit_idx, uint8_t* out_byte, size_t start_offset, size_t end_offset, int depth) {
    while (!ctx->stop_decompression) {
        instrument_stack();
        if (limit_idx != (size_t)-1 && ctx->decompressed_data_index > limit_idx) break;
        if (end_offset != (size_t)-1 && ctx->decompressed_data_index >= end_offset) {
            ctx->stop_decompression = true; break;
        }
        int bit = get_one_bit(ctx);
        if (bit == -1) break;
        if (bit == 1) {
            uint32_t b;
            if (get_n_bits(ctx, 8, &b) < 0) break;
            if (ctx->decompressed_data_index == limit_idx) {
                if (out_byte) *out_byte = (uint8_t)b;
                ctx->decompressed_data_index++; return 1;
            }
            if (ctx->decompressed_data_index >= start_offset) exod_write_byte(ctx, (uint8_t)b);
            else ctx->decompressed_data_index++;
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
        if (len_idx == 16) { ctx->eos_reached = true; ctx->stop_decompression = true; break; }
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
                        ctx->decompressed_data_index++; return 1;
                    }
                    if (ctx->decompressed_data_index >= start_offset) {
                        exod_write_byte(ctx, (uint8_t)val);
                        if (end_offset != (size_t)-1 && ctx->decompressed_data_index >= end_offset) {
                            ctx->stop_decompression = true; break;
                        }
                    } else ctx->decompressed_data_index++;
                }
            }
            continue;
        }
        if (len_idx > 15) break;
        uint32_t seq_len = ctx->tables->lengths_base[len_idx];
        if (ctx->tables->lengths_bits[len_idx] > 0) {
            uint32_t extra;
            if (get_n_bits(ctx, ctx->tables->lengths_bits[len_idx], &extra) < 0) break;
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
            if (get_n_bits(ctx, ctx->tables->offsets1_bits[off_idx], &extra) < 0) break;
            off_val = ctx->tables->offsets1_base[off_idx] + extra;
        } else if (seq_len == 2) {
            if (off_idx >= 16) break;
            if (get_n_bits(ctx, ctx->tables->offsets2_bits[off_idx], &extra) < 0) break;
            off_val = ctx->tables->offsets2_base[off_idx] + extra;
        } else {
            if (off_idx >= 16) break;
            if (get_n_bits(ctx, ctx->tables->offsets3_bits[off_idx], &extra) < 0) break;
            off_val = ctx->tables->offsets3_base[off_idx] + extra;
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
                uint8_t b = exod_get_history(ctx, off_val, depth);
                if (ctx->decompressed_data_index == limit_idx) {
                    if (out_byte) *out_byte = b;
                    ctx->decompressed_data_index++; return 1;
                }
                if (ctx->decompressed_data_index >= start_offset) {
                    exod_write_byte(ctx, b);
                    if (end_offset != (size_t)-1 && ctx->decompressed_data_index >= end_offset) {
                        ctx->stop_decompression = true; break;
                    }
                } else ctx->decompressed_data_index++;
            }
        }
    }
    return 0;
}

static size_t exod_decrunch_all_modes(exod_state_t* state, size_t start, size_t len) {
    exod_tables_t tables;
    state->tables = &tables;
    if (!generate_table(state, state->tables->lengths_bits, state->tables->lengths_base, 16)) return EXOD_ERROR;
    if (!generate_table(state, state->tables->offsets3_bits, state->tables->offsets3_base, 16)) return EXOD_ERROR;
    if (!generate_table(state, state->tables->offsets2_bits, state->tables->offsets2_base, 16)) return EXOD_ERROR;
    if (!generate_table(state, state->tables->offsets1_bits, state->tables->offsets1_base, 4)) return EXOD_ERROR;
    state->bitstream_data_index = (uint32_t)state->crunched_data_index;
    state->bitstream_data_bitbuf = state->bitbuf;
    state->bitstream_data_bit_count = state->bit_count;
    exod_decrunch_internal(state, (size_t)-1, NULL, start, start + len, 0);
    return state->decompressed_data_index;
}

size_t exod_decrunch(const uint8_t* in_data, size_t in_len, uint8_t* out_buffer, size_t out_max_len, bool is_progmem) {
    exod_state_t s; memset(&s, 0, sizeof(s));
    s.crunched_data_ptr = in_data; s.crunched_data_len = in_len; s.source_in_progmem = is_progmem;
    s.decompressed_data_ptr = out_buffer; s.decompressed_buffer_size = out_max_len;
    size_t res = exod_decrunch_all_modes(&s, 0, (size_t)-1);
    if (res == 0 && s.eos_reached) return 0;
    if (!s.eos_reached) return EXOD_ERROR;
    return res;
}

size_t exod_decrunch_streaming(exod_read_cb r, exod_write_cb w, void* u, uint8_t* wb, size_t ws) {
    exod_state_t s; memset(&s, 0, sizeof(s));
    s.read_cb = r; s.write_cb = w; s.userdata = u; s.decompressed_data_ptr = wb; s.decompressed_buffer_size = ws;
    size_t res = exod_decrunch_all_modes(&s, 0, (size_t)-1);
    return s.eos_reached ? res : EXOD_ERROR;
}

size_t exod_decrunch_memoryless(const uint8_t* in, size_t in_len, exod_write_cb w, void* u, bool p) {
    exod_state_t s; memset(&s, 0, sizeof(s));
    s.crunched_data_ptr = in; s.crunched_data_len = in_len; s.source_in_progmem = p; s.write_cb = w; s.userdata = u;
    size_t res = exod_decrunch_all_modes(&s, 0, (size_t)-1);
    return s.eos_reached ? res : EXOD_ERROR;
}

size_t exod_decrunch_memoryless_streaming(exod_read_cb r, exod_seek_cb sk, exod_write_cb w, void* u) {
    exod_state_t s; memset(&s, 0, sizeof(s));
    s.read_cb = r; s.seek_cb = sk; s.write_cb = w; s.userdata = u;
    size_t res = exod_decrunch_all_modes(&s, 0, (size_t)-1);
    return s.eos_reached ? res : EXOD_ERROR;
}

size_t exod_decrunch_memoryless_range(const uint8_t* in, size_t in_len, size_t start, size_t len, exod_write_cb w, void* u, bool p) {
    exod_state_t s; memset(&s, 0, sizeof(s));
    s.crunched_data_ptr = in; s.crunched_data_len = in_len; s.source_in_progmem = p; s.write_cb = w; s.userdata = u;
    size_t res = exod_decrunch_all_modes(&s, start, len);
    if (res <= start) return 0;
    return res - start;
}
