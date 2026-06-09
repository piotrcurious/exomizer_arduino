#include "src/exomizer_decompress.h"
#include <iostream>
#include <fstream>
#include <vector>
#include <string>

struct StreamCtx {
    const uint8_t* data;
    size_t len;
    size_t pos;
    std::vector<uint8_t> out;
};

int read_cb(void* userdata) {
    StreamCtx* ctx = (StreamCtx*)userdata;
    if (ctx->pos < ctx->len) return ctx->data[ctx->pos++];
    return -1;
}

int seek_cb(void* userdata, size_t offset) {
    StreamCtx* ctx = (StreamCtx*)userdata;
    if (offset > ctx->len) return -1;
    ctx->pos = offset;
    return 0;
}

void write_cb(void* userdata, uint8_t b) {
    StreamCtx* ctx = (StreamCtx*)userdata;
    ctx->out.push_back(b);
}

int main(int argc, char** argv) {
    if (argc < 3) return 1;

    std::ifstream ifs(argv[1], std::ios::binary);
    std::vector<uint8_t> in((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());

    StreamCtx ctx;
    ctx.data = in.data();
    ctx.len = in.size();
    ctx.pos = 0;

    size_t total = exod_decrunch_memoryless_streaming(read_cb, seek_cb, write_cb, &ctx);

    std::ofstream ofs(argv[2], std::ios::binary);
    ofs.write((char*)ctx.out.data(), ctx.out.size());

    std::cout << "Memoryless streaming decompressed " << total << " bytes." << std::endl;
    return 0;
}
