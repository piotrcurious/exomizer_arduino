/*
  BasicDecompression
  Demonstrates how to use the Exomizer decompressor with a compressed header file.

  This example works on both ESP32 and Arduino Uno.
*/

#include "exomizer_decompress.h"
#include "compressed_data.h"

void setup() {
  Serial.begin(115200);
  while (!Serial) delay(10);

  Serial.println("\n--- Exomizer Basic Decompression Example ---");

  // Allocate buffer for decompressed data
  uint8_t* out_buffer = (uint8_t*)malloc(sample_data_orig_len + 1);
  if (!out_buffer) {
    Serial.println("Failed to allocate memory!");
    return;
  }

  Serial.print("Compressed size: ");
  Serial.println(sample_data_len);
  Serial.print("Original size: ");
  Serial.println(sample_data_orig_len);

  // Decompress
  // Note: the last argument is 'true' if we are on AVR to read from PROGMEM
#if defined(__AVR__)
  bool is_progmem = true;
#else
  bool is_progmem = false;
#endif

  uint32_t start_time = micros();
  size_t result_size = exod_decrunch(sample_data, sample_data_len, out_buffer, sample_data_orig_len, is_progmem);
  uint32_t end_time = micros();

  if (result_size == sample_data_orig_len) {
    out_buffer[result_size] = '\0'; // Null-terminate if it's a string
    Serial.println("Decompression successful!");
    Serial.print("Time taken: ");
    Serial.print(end_time - start_time);
    Serial.println(" us");
    Serial.println("Result:");
    Serial.println((char*)out_buffer);
  } else {
    Serial.print("Decompression failed! Result size: ");
    Serial.println(result_size);
  }

  free(out_buffer);
}

void loop() {
  // Nothing to do
}
