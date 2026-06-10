CC = g++
CFLAGS = -O3 -Wall -I.
SRC_DIR = src
TOOLS_DIR = tools
TESTS_DIR = tests

# Targets
ALL = $(TOOLS_DIR)/exomizer_compress $(TESTS_DIR)/test_runner $(TESTS_DIR)/test_streaming $(TESTS_DIR)/test_memoryless_streaming $(TESTS_DIR)/test_paging

all: $(ALL)

$(TOOLS_DIR)/exomizer_compress: $(TOOLS_DIR)/exomizer_compress.cpp
	$(CC) $(CFLAGS) $< -o $@

$(TESTS_DIR)/test_runner: $(TESTS_DIR)/test_runner.cpp $(SRC_DIR)/exomizer_decompress.cpp
	$(CC) $(CFLAGS) $^ -o $@

$(TESTS_DIR)/test_streaming: $(TESTS_DIR)/test_streaming.cpp $(SRC_DIR)/exomizer_decompress.cpp
	$(CC) $(CFLAGS) $^ -o $@

$(TESTS_DIR)/test_memoryless_streaming: $(TESTS_DIR)/test_memoryless_streaming.cpp $(SRC_DIR)/exomizer_decompress.cpp
	$(CC) $(CFLAGS) $^ -o $@

$(TESTS_DIR)/test_paging: $(TESTS_DIR)/test_paging.cpp $(SRC_DIR)/exomizer_decompress.cpp
	$(CC) $(CFLAGS) $^ -o $@

clean:
	rm -f $(ALL)

.PHONY: all clean
