# Exomizer Arduino Port

This repository contains a C++ port of the Exomizer decompression algorithm, optimized for Arduino and other embedded systems. It includes a consolidated decompressor library, a simple Python-based compressor for testing, and a comprehensive test harness.

## Project Structure

- `src/`: Core decompressor library (`exomizer_decompress.h/cpp`).
- `tools/`: Python-based compression tool (`exomizer_simple_compress.py`).
- `tests/`: C++ test runner for host-side verification.
- `test_harness.py`: Orchestration script for comprehensive testing.

## Decompressor Library

The core decompressor is located in `src/`. It is designed to be reentrant and efficient.

### Usage

```cpp
#include "src/exomizer_decompress.h"

uint8_t out_buffer[1024];
size_t size = exod_decrunch(crunched_data, crunched_len, out_buffer, sizeof(out_buffer), false);
if (size > 0) {
    // Success
}
```

## Testing

### Test Harness

The `test_harness.py` script automates the generation of test data, compression, decompression, and verification. It ensures that the decompressor works correctly across various data types (text, binary, structured data).

To run the tests:

```bash
python3 test_harness.py
```

This will:
1. Compile the host-side test runner.
2. Generate test files in `test_data/`.
3. Compress them using the Python tool.
4. Decompress them using the C++ library.
5. Verify that the output matches the original and report compression ratios.

### Simple Compressor

You can use the provided Python tool to compress files into the 'raw' Exomizer format expected by the decompressor:

```bash
python3 tools/exomizer_simple_compress.py <input_file> <output_file.exo>
```

## Platform Compatibility

The decompressor is compatible with:
- AVR (Arduino Uno, Mega, etc.) - supports `PROGMEM` for crunched data.
- ESP32 / ESP8266.
- Standard C++ environments (Linux, macOS, Windows).

## License

This project is a port based on the Exomizer algorithm by Magnus Lind.
