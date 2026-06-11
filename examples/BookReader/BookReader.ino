/**
 * @file BookReader.ino
 * @brief Interactive Serial Book Reader using memoryless range decompression.
 *
 * This example demonstrates how to browse a large text file (Prometheus manual)
 * on a memory-constrained device like an Arduino Uno (2KB RAM).
 *
 * Features:
 * - O(1) extra RAM usage for decompression.
 * - Interactive Serial navigation.
 * - Support for files larger than available RAM.
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

#define PAGE_SIZE 1024
uint16_t currentPage = 0;
uint32_t totalEstimatedSize = 20693; // We know this from compression time

void serial_write_cb(void* userdata, uint8_t byte) {
    Serial.write(byte);
}

void displayPage(uint16_t page) {
    size_t startOffset = (size_t)page * PAGE_SIZE;

    Serial.println(F("\n========================================"));
    Serial.print(F("  Exomizer Book Reader - Page "));
    Serial.println(page);
    Serial.println(F("========================================"));

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

    if (count == 0 && startOffset >= totalEstimatedSize) {
        Serial.println(F("\n[End of Book]"));
        if (currentPage > 0) currentPage--;
    } else {
        Serial.println(F("\n----------------------------------------"));
        Serial.print(F(" (n)ext, (p)rev, (g)#age, (r)efresh, (h)elp"));
    }
}

void showHelp() {
    Serial.println(F("\nExomizer Book Reader Commands:"));
    Serial.println(F("  n      : Go to the next page"));
    Serial.println(F("  p      : Go to the previous page"));
    Serial.println(F("  g<num> : Jump to specific page number (e.g., g5)"));
    Serial.println(F("  r      : Redraw the current page"));
    Serial.println(F("  h      : Show this help menu"));
    Serial.print(F("Current Page Size: "));
    Serial.print(PAGE_SIZE);
    Serial.println(F(" bytes"));
}

void setup() {
    Serial.begin(115200);
    while (!Serial);

    Serial.println(F("\nWelcome to the Exomizer Interactive Book Reader!"));
    Serial.println(F("This demo uses 'Memoryless Mode' to decompress data on-the-fly."));

    showHelp();
    displayPage(currentPage);
}

void loop() {
    if (Serial.available()) {
        char cmd = Serial.read();

        // Handle basic navigation
        if (cmd == 'n' || cmd == 'N') {
            currentPage++;
            displayPage(currentPage);
        }
        else if (cmd == 'p' || cmd == 'P') {
            if (currentPage > 0) {
                currentPage--;
                displayPage(currentPage);
            } else {
                Serial.println(F("\n[Already at first page]"));
            }
        }
        else if (cmd == 'r' || cmd == 'R') {
            displayPage(currentPage);
        }
        else if (cmd == 'h' || cmd == 'H') {
            showHelp();
        }
        else if (cmd == 'g' || cmd == 'G') {
            // Wait a bit for number to arrive
            delay(10);
            int target = Serial.parseInt();
            if (target >= 0) {
                currentPage = target;
                displayPage(currentPage);
            }
        }
    }
}
