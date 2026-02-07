# Exomizer-compatible Arduino Port

This repository contains a C++ port of a decompression algorithm compatible with the "raw" stream format used by Exomizer, optimized for Arduino and other embedded systems. It includes a consolidated decompressor library, a versatile Python-based compressor, and examples for various platforms.

**Note:** The compressor and decompressor in this repository implement a simplified LZ77 algorithm that uses the same bitstream structure and dynamic tables as Exomizer's raw mode. It is not a full port of the official Exomizer tool but is designed to be highly compatible and efficient for embedded use.

## Project Structure

- `src/`: Core decompressor library (`exomizer_decompress.h/cpp`).
- `tools/`: Python-based compression tool (`exomizer_simple_compress.py`).
- `examples/`: Arduino examples for ESP32 and Arduino Uno.
- `tests/`: C++ test runners for host-side verification.
- `test_harness.py`: Orchestration script for comprehensive testing.

## Decompressor Library

The core decompressor is designed to be reentrant, efficient, and safe. It supports both block-based and streaming decompression.

### Usage (Block)

```cpp
#include "exomizer_decompress.h"

uint8_t out_buffer[1024];
// is_progmem should be true on AVR if data is in Flash
size_t size = exod_decrunch(crunched_data, crunched_len, out_buffer, sizeof(out_buffer), is_progmem);
```

### Usage (Streaming)

Streaming decompression allows you to process data without having the entire input or output in RAM at once. You only need enough RAM for the sliding window (typically up to 32KB).

```cpp
#include "exomizer_decompress.h"

int my_read_cb(void* userdata) {
    // Return next byte or -1 on EOF
    return my_source.read();
}

void my_write_cb(void* userdata, uint8_t byte) {
    // Process decompressed byte
    Serial.write(byte);
}

uint8_t window[2048]; // Sliding window
size_t total = exod_decrunch_streaming(my_read_cb, my_write_cb, &my_ctx, window, sizeof(window));
```

## Compression Tool

The provided Python tool can compress files into the compatible 'raw' format or generate C++ headers.

### Presets

- `balanced` (default): Good balance between speed and ratio.
- `speed`: Faster compression/decompression by using fewer sequences.
- `ratio`: Maximizes compression ratio.

### Generating Headers

To generate a header file for your Arduino project:

```bash
python3 tools/exomizer_simple_compress.py input.bin output.h --name my_data --preset ratio
```

## Examples

Check the `examples/` directory for standalone Arduino sketches:
- **BasicDecompression**: Simple string decompression from a header.
- **MemoryOptimized**: Demonstrates handling larger data sets and cross-platform compatibility.
- **StreamingDecompression**: Shows how to decompress using callbacks for input and output, saving RAM.

## Testing

Run the comprehensive test suite to verify both block and streaming modes, including circular window logic:

```bash
python3 test_harness.py
```

## License

This project is a port based on the bitstream format of the Exomizer algorithm by Magnus Lind.
