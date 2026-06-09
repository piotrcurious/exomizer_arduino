#include "src/exomizer_decompress.h"
#include <iostream>
#include <fstream>
#include <vector>
#include <string>

struct Ctx {
    std::vector<uint8_t> out;
};

void write_cb(void* userdata, uint8_t b) {
    Ctx* ctx = (Ctx*)userdata;
    ctx->out.push_back(b);
}

int main(int argc, char** argv) {
    if (argc < 3) return 1;

    std::ifstream ifs(argv[1], std::ios::binary);
    std::vector<uint8_t> in((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());

    Ctx ctx;
    size_t total = exod_decrunch_memoryless(in.data(), in.size(), write_cb, &ctx, false);

    std::ofstream ofs(argv[2], std::ios::binary);
    ofs.write((char*)ctx.out.data(), ctx.out.size());

    std::cout << "Memoryless decompressed " << total << " bytes." << std::endl;
    return 0;
}
