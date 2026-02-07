#include "src/exomizer_decompress.h"
#include <iostream>
#include <vector>
#include <cstring>
#include <string>

struct StreamCtx {
    const uint8_t* in_data;
    size_t in_len;
    size_t in_pos;
    std::vector<uint8_t> out_data;
};

int my_read_cb(void* userdata) {
    StreamCtx* ctx = (StreamCtx*)userdata;
    if (ctx->in_pos < ctx->in_len) {
        return ctx->in_data[ctx->in_pos++];
    }
    return -1;
}

void my_write_cb(void* userdata, uint8_t byte) {
    StreamCtx* ctx = (StreamCtx*)userdata;
    ctx->out_data.push_back(byte);
}

int main(int argc, char** argv) {
    if (argc < 3) {
        std::cerr << "Usage: " << argv[0] << " <input_crunched> <output_decompressed> [window_size]" << std::endl;
        return 1;
    }

    FILE* f = fopen(argv[1], "rb");
    if (!f) return 1;
    fseek(f, 0, SEEK_END);
    size_t in_len = ftell(f);
    fseek(f, 0, SEEK_SET);
    std::vector<uint8_t> in_data(in_len);
    if (fread(in_data.data(), 1, in_len, f) != in_len) {
        fclose(f);
        return 1;
    }
    fclose(f);

    StreamCtx ctx;
    ctx.in_data = in_data.data();
    ctx.in_len = in_len;
    ctx.in_pos = 0;

    size_t window_size = 32768;
    if (argc >= 4) {
        window_size = std::stoul(argv[3]);
    }

    std::vector<uint8_t> window(window_size);

    size_t total = exod_decrunch_streaming(my_read_cb, my_write_cb, &ctx, window.data(), window.size());

    FILE* out_f = fopen(argv[2], "wb");
    if (!out_f) return 1;
    fwrite(ctx.out_data.data(), 1, ctx.out_data.size(), out_f);
    fclose(out_f);

    std::cout << "Decompressed " << total << " bytes via streaming (window size: " << window_size << ")." << std::endl;

    return 0;
}
