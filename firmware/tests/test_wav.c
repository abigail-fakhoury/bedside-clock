#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "core/wav.h"

/* Python's wave module writing 64000 zero bytes, 1 channel, 2-byte
 * samples, 16000 Hz: the first 44 bytes of that file. */
static const uint8_t KNOWN[44] = {
    0x52, 0x49, 0x46, 0x46, 0x24, 0xfa, 0x00, 0x00, 0x57, 0x41, 0x56, 0x45, 0x66, 0x6d, 0x74,
    0x20, 0x10, 0x00, 0x00, 0x00, 0x01, 0x00, 0x01, 0x00, 0x80, 0x3e, 0x00, 0x00, 0x00, 0x7d,
    0x00, 0x00, 0x02, 0x00, 0x10, 0x00, 0x64, 0x61, 0x74, 0x61, 0x00, 0xfa, 0x00, 0x00,
};

static void test_known(void)
{
    uint8_t h[44];
    memset(h, 0xee, sizeof(h));
    wav_header(h, 64000, 16000);
    for (int i = 0; i < 44; i++) {
        if (h[i] != KNOWN[i]) {
            fprintf(stderr, "byte %d: got 0x%02x, want 0x%02x\n", i, h[i], KNOWN[i]);
            assert(0);
        }
    }
}

/* Five seconds at 16 kHz, and the largest sizes, little-endian */
static void test_sizes(void)
{
    uint8_t h[44];
    wav_header(h, 160000, 16000);
    assert(h[4] == 0x24 && h[5] == 0x71 && h[6] == 0x02 && h[7] == 0x00); /* 160036 */
    assert(h[40] == 0x00 && h[41] == 0x71 && h[42] == 0x02 && h[43] == 0x00);
    wav_header(h, 0, 8000);
    assert(h[4] == 36 && h[5] == 0 && h[40] == 0 && h[43] == 0);
    assert(h[24] == 0x40 && h[25] == 0x1f); /* 8000 */
    assert(h[28] == 0x80 && h[29] == 0x3e); /* 16000 bytes/s */
}

static void test_gain(void)
{
    assert(pcm_gain(0, 6) == 0);
    assert(pcm_gain(100, 6) == 600);
    assert(pcm_gain(-100, 6) == -600);
    assert(pcm_gain(330, 6) == 1980);
    assert(pcm_gain(5461, 6) == 32766);   /* largest that fits */
    assert(pcm_gain(5462, 6) == 32767);   /* 32772 clamps */
    assert(pcm_gain(-5461, 6) == -32766);
    assert(pcm_gain(-5462, 6) == -32768); /* -32772 clamps */
    assert(pcm_gain(32767, 6) == 32767);
    assert(pcm_gain(INT16_MIN, 6) == INT16_MIN);
    assert(pcm_gain(INT16_MIN, 1) == INT16_MIN);
    assert(pcm_gain(1234, 1) == 1234);
}

int main(void)
{
    test_known();
    test_sizes();
    test_gain();
    return 0;
}
