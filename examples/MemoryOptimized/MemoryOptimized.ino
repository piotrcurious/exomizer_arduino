/*
  MemoryOptimized
  Shows how to handle larger data sets efficiently.

  This example demonstrates decompression of a medium-sized buffer.
  On Arduino Uno, it uses a large portion of available RAM, while
  on ESP32 it is negligible.
*/

#include "exomizer_decompress.h"
#include "large_data.h"

void setup() {
  Serial.begin(115200);
  while (!Serial) delay(10);

  Serial.println("\n--- Exomizer Memory Optimized Example ---");

  Serial.print("Compressed size: ");
  Serial.println(large_data_len);
  Serial.print("Original size: ");
  Serial.println(large_data_orig_len);

  // Allocate buffer for decompressed data
  uint8_t* out_buffer = (uint8_t*)malloc(large_data_orig_len);

  if (!out_buffer) {
    Serial.println("Error: Not enough RAM to decompress!");
    return;
  }

#if defined(__AVR__)
  // On AVR, we MUST read from PROGMEM if the data was stored there
  bool is_progmem = true;
#else
  // On ESP32/ARM, PROGMEM is just a macro and data is in the same address space
  bool is_progmem = false;
#endif

  size_t result_size = exod_decrunch(large_data, large_data_len, out_buffer, large_data_orig_len, is_progmem);

  if (result_size == large_data_orig_len) {
    Serial.println("Decompression successful!");
    // Print first 50 bytes
    Serial.print("Preview: ");
    for(int i = 0; i < 50 && i < result_size; i++) {
      Serial.print((char)out_buffer[i]);
    }
    Serial.println("...");
  } else {
    Serial.println("Decompression failed!");
  }

  free(out_buffer);
}

void loop() {}
