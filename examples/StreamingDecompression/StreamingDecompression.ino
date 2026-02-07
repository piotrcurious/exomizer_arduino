/*
  StreamingDecompression
  Demonstrates how to use the streaming Exomizer decompressor.

  This is useful when you want to decompress data that is larger than
  available RAM, provided the compression window fits in RAM.
*/

#include "exomizer_decompress.h"
#include "streaming_data.h"

// Context for the stream reader
struct MyStream {
  const uint8_t* data;
  uint32_t len;
  uint32_t pos;
#if defined(__AVR__)
  bool is_progmem;
#endif
};

// Input callback: returns the next crunched byte
int read_from_buffer(void* userdata) {
  MyStream* s = (MyStream*)userdata;
  if (s->pos < s->len) {
    uint8_t b;
#if defined(__AVR__)
    if (s->is_progmem) {
      b = pgm_read_byte(s->data + s->pos);
    } else {
      b = s->data[s->pos];
    }
#else
    b = s->data[s->pos];
#endif
    s->pos++;
    return b;
  }
  return -1; // EOF
}

// Output callback: called for each decompressed byte
void write_to_serial(void* userdata, uint8_t byte) {
  Serial.write(byte);
}

// We need a window buffer for back-references.
// For this simple example, 256 bytes is enough.
// In real projects, it should match your compressor's window size (e.g., 32768).
uint8_t window[256];

void setup() {
  Serial.begin(115200);
  while (!Serial) delay(10);

  Serial.println("\n--- Exomizer Streaming Decompression Example ---");

  MyStream s;
  s.data = streaming_data;
  s.len = streaming_data_len;
  s.pos = 0;
#if defined(__AVR__)
  s.is_progmem = true;
#endif

  Serial.println("Decompressing:");

  size_t total = exod_decrunch_streaming(
    read_from_buffer,
    write_to_serial,
    &s,
    window,
    sizeof(window)
  );

  Serial.println("\n\nDone.");
  Serial.print("Decompressed ");
  Serial.print(total);
  Serial.println(" bytes.");
}

void loop() {}
