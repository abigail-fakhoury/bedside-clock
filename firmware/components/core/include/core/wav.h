#pragma once
#include <stdint.h>

/* The 44-byte RIFF/WAVE header for 16-bit mono PCM: "RIFF", 36 + pcm_bytes,
 * "WAVE", a 16-byte "fmt " chunk (PCM, 1 channel, rate, rate * 2 bytes/s,
 * block 2, 16 bits), then "data" and pcm_bytes. All fields little-endian. */
void wav_header(uint8_t h[44], uint32_t pcm_bytes, uint32_t rate);

/* s * factor, clamped to [-32768, 32767] (saturates, never wraps); factor
 * 0-65535. The software gain applied to each recorded sample. */
int16_t pcm_gain(int16_t s, int factor);
