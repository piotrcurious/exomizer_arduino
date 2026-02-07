# Exomizer-compatible Arduino Port

This repository contains a C++ port of a decompression algorithm compatible with the "raw" stream format used by Exomizer, optimized for Arduino and other embedded systems. It includes a consolidated decompressor library, a versatile Python-based compressor, and examples for various platforms.

## Key Features

- **Entropy Coding:** Implements Exomizer-style entropy coding using dynamic bit-length tables and variable-length prefix codes for indices.
- **Reentrant & Safe:** The decompressor is reentrant and includes bounds checks for safety.
- **Streaming Support:** Supports callback-driven streaming decompression, allowing processing of data larger than available RAM using a circular window buffer.
- **Cross-Platform:** Works on AVR (Uno/Mega), ESP32, ESP8266, and standard C++ environments.
- **Arduino Integration:** Python tool can generate C++ headers with `PROGMEM` support.

## Project Structure

- `src/`: Core decompressor library (`exomizer_decompress.h/cpp`).
- `tools/`: Python-based compression tool (`exomizer_simple_compress.py`).
- `examples/`: Arduino examples for ESP32 and Arduino Uno.
- `tests/`: C++ test runners for host-side verification.
- `test_harness.py`: Orchestration script for comprehensive testing.

## Decompressor Library

### Usage (Block)

```cpp
#include "exomizer_decompress.h"

uint8_t out_buffer[1024];
// is_progmem should be true on AVR if data is in Flash
size_t size = exod_decrunch(crunched_data, crunched_len, out_buffer, sizeof(out_buffer), is_progmem);
```

### Usage (Streaming)

```cpp
#include "exomizer_decompress.h"

int my_read_cb(void* userdata) { return my_source.read(); }
void my_write_cb(void* userdata, uint8_t byte) { Serial.write(byte); }

uint8_t window[2048]; // Sliding window
size_t total = exod_decrunch_streaming(my_read_cb, my_write_cb, &my_ctx, window, sizeof(window));
```

## Compression Tool

The Python tool compresses files into the compatible 'raw' format and can generate headers.

### Presets

- `balanced` (default): Balance between speed and ratio.
- `speed`: Fast compression/decompression.
- `ratio`: Maximizes compression ratio.

### Generating Headers

```bash
python3 tools/exomizer_simple_compress.py input.bin output.h --name my_data --preset ratio
```

## Testing

Run the comprehensive test suite:

```bash
python3 test_harness.py
```

## License

This project is a port based on the bitstream format of the Exomizer algorithm by Magnus Lind.
