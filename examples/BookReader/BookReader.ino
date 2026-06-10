/**
 * @file BookReader.ino
 * @brief Simple Serial Book Reader using memoryless decompression.
 *
 * This example demonstrates how to read a large text file (Prometheus manual)
 * on a memory-constrained device like an Arduino Uno (2KB RAM) using the
 * memoryless decompression mode. This mode requires NO sliding window buffer,
 * allowing you to process files of any size as long as they fit in Flash.
 */

#include <exomizer_decompress.h>
#include "prometheus_data.h"

// Callback to write a byte to Serial
void serial_write_cb(void* userdata, uint8_t byte) {
    Serial.write(byte);
}

void setup() {
    Serial.begin(115200);
    while (!Serial);

    Serial.println(F("\n--- Exomizer Book Reader ---"));
    Serial.println(F("Decompressing 'Prometheus48+128 Manual' using Memoryless Mode..."));
    Serial.println(F("This uses O(1) RAM for history references."));
    Serial.println(F("Wait for decompression to start...\n"));

    // Set is_progmem to true for AVR targets
    bool is_progmem = false;
#if defined(__AVR__)
    is_progmem = true;
#endif

    // Memoryless decompression: O(1) RAM usage, O(N^2) time complexity.
    // Perfect for reading text one character at a time on low-end hardware.
    size_t total = exod_decrunch_memoryless(
        prometheus_compressed,
        prometheus_compressed_len,
        serial_write_cb,
        NULL,
        is_progmem
    );

    Serial.print(F("\n\n--- End of Book ---\nTotal decompressed bytes: "));
    Serial.println(total);
}

void loop() {
    // Nothing to do
}
