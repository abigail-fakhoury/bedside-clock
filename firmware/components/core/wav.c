#include "core/wav.h"

#include <string.h>

static void le32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

static void le16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
}

void wav_header(uint8_t h[44], uint32_t pcm_bytes, uint32_t rate)
{
    memcpy(h, "RIFF", 4);
    le32(h + 4, 36 + pcm_bytes);
    memcpy(h + 8, "WAVEfmt ", 8);
    le32(h + 16, 16);       /* fmt chunk size */
    le16(h + 20, 1);        /* PCM */
    le16(h + 22, 1);        /* mono */
    le32(h + 24, rate);
    le32(h + 28, rate * 2); /* bytes per second */
    le16(h + 32, 2);        /* bytes per frame */
    le16(h + 34, 16);       /* bits per sample */
    memcpy(h + 36, "data", 4);
    le32(h + 40, pcm_bytes);
}

int16_t pcm_gain(int16_t s, int factor)
{
    int32_t v = (int32_t)s * factor;
    return v > INT16_MAX ? INT16_MAX : v < INT16_MIN ? INT16_MIN : (int16_t)v;
}
