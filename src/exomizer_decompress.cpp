#include "exomizer_decompress.h"
#include <string.h>

#if defined(ARDUINO) && defined(__AVR__)
    #include <avr/pgmspace.h>
    #define EXO_READ_BYTE(ctx, offset) \
        (ctx->source_in_progmem ? pgm_read_byte(ctx->crunched_data_ptr + offset) : ctx->crunched_data_ptr[offset])
#else
    #define EXO_READ_BYTE(ctx, offset) (ctx->crunched_data_ptr[offset])
#endif

static bool exod_read_byte(exod_state_t* ctx, uint8_t* byte) {
    if (ctx->crunched_data_index < ctx->crunched_data_len) {
        *byte = EXO_READ_BYTE(ctx, ctx->crunched_data_index);
        ctx->crunched_data_index++;
        return true;
    }
    return false;
}

static int get_one_bit(exod_state_t* ctx) {
    if (ctx->bit_count == 0) {
        if (!exod_read_byte(ctx, &ctx->bitbuf)) return -1;
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

size_t exod_decrunch(const uint8_t* in_data, size_t in_len, uint8_t* out_buffer, size_t out_max_len, bool is_progmem) {
    exod_state_t state;
    memset(&state, 0, sizeof(exod_state_t));
    exod_state_t* ctx = &state;

    ctx->crunched_data_ptr = in_data;
    ctx->crunched_data_len = in_len;
    ctx->source_in_progmem = is_progmem;
    ctx->decompressed_data_ptr = out_buffer;
    ctx->decompressed_buffer_size = out_max_len;

    if (!generate_table(ctx, ctx->lengths_bits, ctx->lengths_base, 16)) return 0;
    if (!generate_table(ctx, ctx->offsets3_bits, ctx->offsets3_base, 16)) return 0;
    if (!generate_table(ctx, ctx->offsets2_bits, ctx->offsets2_base, 16)) return 0;
    if (!generate_table(ctx, ctx->offsets1_bits, ctx->offsets1_base, 4)) return 0;

    while (ctx->decompressed_data_index < ctx->decompressed_buffer_size) {
        int bit = get_one_bit(ctx);
        if (bit == -1) break;
        if (bit == 1) {
            uint32_t b;
            if (get_n_bits(ctx, 8, &b) < 0) break;
            ctx->decompressed_data_ptr[ctx->decompressed_data_index++] = (uint8_t)b;
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
        if (len_idx == 16) break; // EOS

        // Safety check for OOB read
        if (len_idx > 15) break;

        uint32_t seq_len = ctx->lengths_base[len_idx];
        if (ctx->lengths_bits[len_idx] > 0) {
            uint32_t extra;
            if (get_n_bits(ctx, ctx->lengths_bits[len_idx], &extra) < 0) break;
            seq_len += extra;
        }

        uint32_t off_val = 0;
        if (seq_len == 1) {
            uint32_t extra;
            uint8_t tab_idx = len_idx % 4;
            if (get_n_bits(ctx, ctx->offsets1_bits[tab_idx], &extra) < 0) break;
            off_val = ctx->offsets1_base[tab_idx] + extra;
        } else if (seq_len == 2) {
            uint32_t extra;
            uint8_t tab_idx = len_idx % 16;
            if (get_n_bits(ctx, ctx->offsets2_bits[tab_idx], &extra) < 0) break;
            off_val = ctx->offsets2_base[tab_idx] + extra;
        } else {
            uint32_t extra;
            uint8_t tab_idx = len_idx % 16;
            if (get_n_bits(ctx, ctx->offsets3_bits[tab_idx], &extra) < 0) break;
            off_val = ctx->offsets3_base[tab_idx] + extra;
        }

        if (off_val == 0) off_val = ctx->last_offset_val;
        else ctx->last_offset_val = off_val;

        if (off_val == 0 || off_val > ctx->decompressed_data_index) break;
        for (uint32_t i = 0; i < seq_len; ++i) {
            if (ctx->decompressed_data_index >= ctx->decompressed_buffer_size) break;
            ctx->decompressed_data_ptr[ctx->decompressed_data_index] = ctx->decompressed_data_ptr[ctx->decompressed_data_index - off_val];
            ctx->decompressed_data_index++;
        }
    }
    return ctx->decompressed_data_index;
}
