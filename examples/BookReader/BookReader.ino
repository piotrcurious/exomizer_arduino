/**
 * @file BookReader.ino
 * @brief Interactive Serial Book Reader using memoryless range decompression.
 *
 * This example demonstrates how to browse a large text file (Prometheus manual)
 * on a memory-constrained device like an Arduino Uno (2KB RAM).
 *
 * Commands (via Serial):
 *   'n' : Next Page
 *   'p' : Previous Page
 *   'g' : Go to Page (e.g. g10)
 *   'r' : Redisplay current page
 *   'h' : Help
 */

#include <exomizer_decompress.h>
#include "prometheus_data.h"

#define PAGE_SIZE 512
uint16_t currentPage = 0;

void serial_write_cb(void* userdata, uint8_t byte) {
    Serial.write(byte);
}

void displayPage(uint16_t page) {
    size_t startOffset = (size_t)page * PAGE_SIZE;

    Serial.print(F("\n--- Page "));
    Serial.print(page);
    Serial.println(F(" ---"));

    bool is_progmem = false;
#if defined(__AVR__)
    is_progmem = true;
#endif

    size_t count = exod_decrunch_memoryless_range(
        prometheus_compressed,
        prometheus_compressed_len,
        startOffset,
        PAGE_SIZE,
        serial_write_cb,
        NULL,
        is_progmem
    );

    if (count == 0 && startOffset > 0) {
        Serial.println(F("\n[End of Book]"));
    } else {
        Serial.println(F("\n---"));
    }
}

void showHelp() {
    Serial.println(F("\nCommands:"));
    Serial.println(F("  n: Next Page"));
    Serial.println(F("  p: Previous Page"));
    Serial.println(F("  g#: Go to Page # (e.g. g5)"));
    Serial.println(F("  r: Refresh Current Page"));
    Serial.println(F("  h: Show this help"));
}

void setup() {
    Serial.begin(115200);
    while (!Serial);

    Serial.println(F("\nInteractive Exomizer Book Reader"));
    showHelp();
    displayPage(currentPage);
}

void loop() {
    if (Serial.available()) {
        char cmd = Serial.read();
        if (cmd == 'n') {
            currentPage++;
            displayPage(currentPage);
        } else if (cmd == 'p') {
            if (currentPage > 0) currentPage--;
            displayPage(currentPage);
        } else if (cmd == 'r') {
            displayPage(currentPage);
        } else if (cmd == 'h') {
            showHelp();
        } else if (cmd == 'g') {
            int target = Serial.parseInt();
            if (target >= 0) {
                currentPage = target;
                displayPage(currentPage);
            }
        }
    }
}
