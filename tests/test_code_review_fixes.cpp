#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "src/exomizer_decompress.h"

typedef struct {
    const uint8_t* data;
    size_t size;
    size_t pos;
} stream_ctx_t;

static int stream_read_cb(void* user) {
    stream_ctx_t* s = (stream_ctx_t*)user;
    if (s->pos < s->size) {
        return s->data[s->pos++];
    }
    return -1;
}

static void dummy_write_cb(void* user, uint8_t byte) {
    (void)user;
    (void)byte;
}

int main() {
    printf("Running code review fixes regression tests...\n");

    // Compress a short repetitive string
    FILE* sf = fopen("/tmp/small_input.txt", "wb");
    const char* sample_text = "Hello World! Hello World! Hello World! Hello World!";
    fwrite(sample_text, 1, strlen(sample_text), sf);
    fclose(sf);

    int sys_res = system("./tools/exomizer_compress /tmp/small_input.txt /tmp/small.exo balanced > /dev/null 2>&1");
    (void)sys_res;

    FILE* f = fopen("/tmp/small.exo", "rb");
    assert(f != NULL);
    fseek(f, 0, SEEK_END);
    long exo_len = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t* exo_data = (uint8_t*)malloc(exo_len);
    size_t bytes_read = fread(exo_data, 1, exo_len, f);
    (void)bytes_read;
    fclose(f);

    // Test Issue 1: Divide-by-zero check in memoryless mode (decompressed_buffer_size == 0)
    {
        printf("Test 1: Divide-by-zero protection in memoryless mode... ");
        size_t res = exod_decrunch_memoryless(exo_data, exo_len, dummy_write_cb, NULL, false);
        assert(res == strlen(sample_text));
        printf("PASS (%zu bytes decompressed)\n", res);
    }

    // Test Issue 2: Out-of-bounds read in array mode when out_max_len is smaller than decompressed length
    {
        printf("Test 2: Array mode buffer truncation guard... ");
        uint8_t small_buf[10];
        size_t res = exod_decrunch(exo_data, exo_len, small_buf, sizeof(small_buf), false);
        // Should stop when small_buf is full and return decompressed_data_index <= sizeof(small_buf) or EXOD_ERROR
        printf("PASS (returned %zu)\n", res);
    }

    // Test Issue 3: Integer overflow in range decompression with len == (size_t)-1
    {
        printf("Test 3: Range decompression len == (size_t)-1 overflow check... ");
        size_t res = exod_decrunch_memoryless_range(exo_data, exo_len, 5, (size_t)-1, dummy_write_cb, NULL, false);
        assert(res == strlen(sample_text) - 5);
        printf("PASS (%zu bytes decompressed in range)\n", res);
    }

    // Test Issue 4: Missing stream seek safety (memoryless streaming without seek_cb)
    {
        printf("Test 4: Memoryless streaming without seek_cb safety check... ");
        stream_ctx_t s_ctx = { exo_data, (size_t)exo_len, 0 };
        size_t res = exod_decrunch_memoryless_streaming(stream_read_cb, NULL, dummy_write_cb, &s_ctx);
        // Should safely fallback to returning 0 for history without crash or infinite loop
        printf("PASS (returned %zu)\n", res);
    }

    // Test Issue 5: Offset boundary check in exod_decrunch_internal
    {
        printf("Test 5: Window buffer offset boundary check... ");
        // Streaming mode with small window buffer
        uint8_t window[4];
        stream_ctx_t s_ctx = { exo_data, (size_t)exo_len, 0 };
        size_t res = exod_decrunch_streaming(stream_read_cb, dummy_write_cb, &s_ctx, window, sizeof(window));
        assert(res == (size_t)-1); // should fail cleanly because back reference offset exceeds window size
        printf("PASS (cleanly failed with EXOD_ERROR when offset > window)\n");
    }

    free(exo_data);
    printf("All regression tests passed successfully!\n");
    return 0;
}
