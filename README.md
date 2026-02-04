# Exomizer Arduino Port

This repository contains a C++ port of the Exomizer decompression algorithm, optimized for Arduino and other embedded systems. It includes a consolidated decompressor library, a versatile Python-based compressor, and examples for various platforms.

## Project Structure

- `src/`: Core decompressor library (`exomizer_decompress.h/cpp`).
- `tools/`: Python-based compression tool (`exomizer_simple_compress.py`).
- `examples/`: Arduino examples for ESP32 and Arduino Uno.
- `tests/`: C++ test runner for host-side verification.
- `test_harness.py`: Orchestration script for comprehensive testing.

## Decompressor Library

The core decompressor is designed to be reentrant, efficient, and safe.

### Usage

```cpp
#include "exomizer_decompress.h"

uint8_t out_buffer[1024];
// is_progmem should be true on AVR if data is in Flash
size_t size = exod_decrunch(crunched_data, crunched_len, out_buffer, sizeof(out_buffer), is_progmem);
```

## Compression Tool

The provided Python tool can compress files into the 'raw' Exomizer format or generate C++ headers.

### Presets

- `balanced` (default): Good balance between speed and ratio.
- `speed`: Faster compression/decompression by using fewer sequences.
- `ratio`: Maximizes compression ratio.

### Generating Headers

To generate a header file for your Arduino project:

```bash
python3 tools/exomizer_simple_compress.py input.bin output.h --name my_data --preset ratio
```

This generates a `.h` file containing:
- `const uint8_t my_data[]` (with `PROGMEM` on AVR).
- `const uint32_t my_data_len` (compressed size).
- `const uint32_t my_data_orig_len` (original size).

## Examples

Check the `examples/` directory for standalone Arduino sketches:
- **BasicDecompression**: Simple string decompression from a header.
- **MemoryOptimized**: Demonstrates handling larger data sets and cross-platform compatibility.

## Testing

Run the comprehensive test suite to verify everything:

```bash
python3 test_harness.py
```

## Platform Compatibility

- **AVR** (Arduino Uno, Mega, etc.): Uses `PROGMEM` to store compressed data in Flash.
- **ESP32 / ESP8266**: Full support, data is handled in unified memory space.
- **Standard C++**: Can be used in any C++ project.

## License

This project is a port based on the Exomizer algorithm by Magnus Lind.
