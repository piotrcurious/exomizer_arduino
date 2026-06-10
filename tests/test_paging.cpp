#include "src/exomizer_decompress.h"
#include <iostream>
#include <fstream>
#include <vector>
#include <string>

struct PageCtx {
    std::vector<uint8_t> data;
};

void write_cb(void* userdata, uint8_t b) {
    PageCtx* ctx = (PageCtx*)userdata;
    ctx->data.push_back(b);
}

int main(int argc, char** argv) {
    if (argc < 2) return 1;

    std::ifstream ifs(argv[1], std::ios::binary);
    std::vector<uint8_t> in((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());

    // First, get full decompression for reference
    PageCtx full_ctx;
    exod_decrunch(in.data(), in.size(), NULL, 0, false); // Warm up
    size_t full_len = exod_decrunch_memoryless(in.data(), in.size(), write_cb, &full_ctx, false);

    std::cout << "Full length: " << full_len << std::endl;

    // Test various ranges
    size_t offsets[] = {0, 100, 500, 1000, 5000, 10000};
    size_t lengths[] = {10, 50, 200, 500};

    bool all_pass = true;
    for (size_t o : offsets) {
        for (size_t l : lengths) {
            if (o + l > full_len) continue;

            PageCtx range_ctx;
            size_t got = exod_decrunch_memoryless_range(in.data(), in.size(), o, l, write_cb, &range_ctx, false);

            if (got != l) {
                std::cout << "FAIL: Offset " << o << ", Len " << l << " -> Got " << got << std::endl;
                all_pass = false;
                continue;
            }

            for (size_t i = 0; i < l; ++i) {
                if (range_ctx.data[i] != full_ctx.data[o + i]) {
                    std::cout << "FAIL: Mismatch at Offset " << o << "+" << i << std::endl;
                    all_pass = false;
                    break;
                }
            }
        }
    }

    if (all_pass) {
        std::cout << "Paging verification SUCCESS!" << std::endl;
        return 0;
    } else {
        std::cout << "Paging verification FAILURE!" << std::endl;
        return 1;
    }
}
