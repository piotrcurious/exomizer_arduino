#include "src/exomizer_decompress.h"
#include <iostream>
#include <fstream>
#include <vector>
#include <chrono>

int main(int argc, char** argv) {
    if (argc < 3) {
        std::cerr << "Usage: " << argv[0] << " <input_crunched> <output_decompressed> [max_size]" << std::endl;
        return 1;
    }

    std::ifstream ifs(argv[1], std::ios::binary);
    if (!ifs) {
        std::cerr << "Could not open input file: " << argv[1] << std::endl;
        return 1;
    }

    std::vector<uint8_t> in_data((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());

    size_t out_max_len = 1024 * 1024; // Default 1MB
    if (argc >= 4) {
        out_max_len = std::stoul(argv[3]);
    }

    std::vector<uint8_t> out_buf(out_max_len);

    auto start = std::chrono::high_resolution_clock::now();
    size_t decompressed_size = exod_decrunch(in_data.data(), in_data.size(), out_buf.data(), out_max_len, false);
    auto end = std::chrono::high_resolution_clock::now();

    if (decompressed_size == 0) {
        std::cerr << "Decompression failed!" << std::endl;
        return 1;
    }

    std::ofstream ofs(argv[2], std::ios::binary);
    ofs.write((char*)out_buf.data(), decompressed_size);

    std::chrono::duration<double, std::milli> duration = end - start;
    std::cout << "Decompressed " << in_data.size() << " bytes to " << decompressed_size << " bytes in " << duration.count() << " ms." << std::endl;

    return 0;
}
