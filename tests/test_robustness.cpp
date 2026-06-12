#include "src/exomizer_decompress.h"
#include <iostream>
#include <vector>
#include <cstring>

void null_write_cb(void* userdata, uint8_t byte) {
    (void)userdata; (void)byte;
}

int main() {
    std::cout << "Running robustness tests..." << std::endl;

    // Test 1: Random data (should fail gracefully)
    uint8_t random_data[100];
    for (int i = 0; i < 100; ++i) random_data[i] = (uint8_t)rand();
    size_t res = exod_decrunch(random_data, 100, NULL, 0, false);
    std::cout << "Random data test: " << (res == (size_t)-1 ? "PASS (failed as expected)" : "FAIL (didn't report error)") << std::endl;

    // Test 2: Truncated valid stream
    // We need a valid header first
    uint8_t valid_header[26];
    memset(valid_header, 0, 26); // All 0 bits for tables
    res = exod_decrunch(valid_header, 10, NULL, 0, false);
    std::cout << "Truncated header test: " << (res == (size_t)-1 ? "PASS (failed as expected)" : "FAIL (didn't report error)") << std::endl;

    // Test 3: Null input
    res = exod_decrunch(NULL, 0, NULL, 0, false);
    std::cout << "Null input test: " << (res == (size_t)-1 ? "PASS (failed as expected)" : "FAIL (didn't report error)") << std::endl;

    // Test 4: Invalid match (offset > history)
    // 26 bytes of 0s for header, then a match command 0 (unary 0) -> len 1, then offset 1 (unary 0) -> extra 0 -> off 1.
    // Index 0, so off 1 > 0 is invalid.
    uint8_t invalid_match[] = {
        0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, // tables (4 bits each, 52 total -> 26 bytes)
        0x00, 0x00 // Command bits
    };
    res = exod_decrunch(invalid_match, sizeof(invalid_match), NULL, 0, false);
    std::cout << "Invalid offset test: " << (res == (size_t)-1 ? "PASS (failed as expected)" : "FAIL (didn't report error)") << std::endl;

    std::cout << "Robustness tests complete." << std::endl;
    return 0;
}
