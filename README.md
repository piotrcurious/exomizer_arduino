# Exomizer-compatible Arduino Library (Production Ready)

This repository contains a high-performance C++ port of an Exomizer-compatible decompression algorithm, optimized for Arduino and other memory-constrained embedded systems. It supports the Exomizer "raw" bitstream format with advanced features like optimal parsing and decoupled match/offset indices.

## Key Features

- **Optimal Compression:** Includes a high-performance C++ compressor (`tools/exomizer_compress`) using Dynamic Programming for optimal bit-cost parsing.
- **Entropy Coding:** Implements dynamic bit-length table optimization for superior compression ratios (e.g., text ~39%, repetitive data ~1.2%).
- **Streaming Support:** Standard LZ77 streaming with a circular sliding window buffer.
- **Memoryless Mode:** O(1) RAM decompression mode that re-computes references on the fly—ideal for 8-bit AVRs like the Arduino Uno.
- **Range Retrieval:** Random-access-like retrieval of byte ranges from compressed data without full decompression.
- **Reentrant & Safe:** Thread-safe state management with rigorous bounds checking and error propagation.
- **Cross-Platform:** Verified on AVR, ESP32, ESP8266, and standard Linux/macOS/Windows environments.

## Feature Matrix

| Feature | Block Mode | Streaming (Window) | Memoryless |
| --- | --- | --- | --- |
| **RAM Usage** | Output Buffer | Window Size (e.g. 2KB) | **O(1) (Stack only)** |
| **CPU Usage** | Low | Low | High (O(N^2)) |
| **Input Source** | Buffer | Callback | Seekable Callback / Buffer |
| **Best For** | General use | ESP32 / Large files | Arduino Uno / Browsing |

## Project Structure

- `src/`: Core decompressor library (`exomizer_decompress.h/cpp`).
- `tools/`: High-performance C++ and Python compression tools.
- `examples/`: Arduino examples including a paging **Book Reader**.
- `tests/`: Robustness and functional test suites.
- `Makefile`: Unified build system for tools and tests.

## Library Usage

### Memoryless Range (Arduino Uno Example)

```cpp
#include <exomizer_decompress.h>

// Fetch 512 bytes starting at offset 1024 with ZERO window RAM
size_t bytes = exod_decrunch_memoryless_range(
    compressed_data, compressed_len,
    1024, 512,
    write_callback, NULL, true
);
```

## Compression Tools

### C++ Compressor (Recommended)
```bash
make
./tools/exomizer_compress input.bin output.exo ratio
```

### Python Compressor (Scripting)
```bash
python3 tools/exomizer_simple_compress.py input.bin output.exo --preset balanced
```

## Testing

Run the full verification suite:
```bash
python3 test_harness.py
```

## License
Based on the bitstream format of the Exomizer algorithm by Magnus Lind.
