#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Recording from the on-board microphones (ES7210 on the shared I²C bus,
 * I²S MCLK 19, BCLK 20, WS 22, DIN 21): 16 kHz mono 16-bit, mic 1 only
 * (mic 2 shows a full-scale glitch at the start of some recordings), at
 * AUDIO_PGA_DB in the ES7210 times AUDIO_SOFT_GAIN in software. The ADC is powered up for the recording and shut down after it. */

/* 8 s: a long title plus a page did not fit in 5 s (owner-approved
 * 2026-11-12). The clip partition holds 44 + 8 s x 32000 bytes. */
enum { AUDIO_RATE = 16000, AUDIO_MAX_MS = 8000 };

/* The driver's maximum PGA gain, and a saturating software gain on top:
 * at 30 dB alone a normal voice 1-2 ft away peaked only about 330
 * (measured with the owner, 2026-11-12). */
#define AUDIO_PGA_DB 37.5f
#define AUDIO_SOFT_GAIN 6

/* A clip whose peak, after the software gain, is below this is silence or
 * too far away: do not send it. Measured 2026-11-12 at 37.5 dB x 6: a
 * quiet room floor 580-900, a normal voice 1-2 ft away peaked 5,916 (its
 * speech half-seconds 2,000-3,900). */
#define CLIP_TOO_QUIET 2000

/* The clip lives in the "clip" data partition (subtype AUDIO_CLIP_SUBTYPE,
 * partitions.csv), not in RAM: PCM is written there as it is recorded and
 * the WAV is read back through a flash mapping. One clip exists at a time. */
#define AUDIO_CLIP_SUBTYPE 0x40

typedef struct {
    uint8_t *wav; /* 44-byte header, then the PCM; mapped flash, read-only */
    size_t len;   /* header included */
    int peak;     /* largest |sample|, 0-32768 */
} clip_t;

/* Records for max_ms (at most AUDIO_MAX_MS), or less once *stop_early
 * turns true (checked every 32 ms; may be NULL). Erases the clip partition
 * first (about 0.4 s, before the mic starts), then logs "recording now"
 * and sets *started (may be NULL) when the first sample is kept, so a
 * screen can say "Listening" only once the mic really runs. A new recording replaces the last clip,
 * so release any earlier clip first. Blocks; one task at a time. On false
 * out->wav is NULL and nothing is held. Release a clip with
 * audio_clip_free(). */
bool audio_record(clip_t *out, int max_ms, volatile bool *stop_early, volatile bool *started);

/* Unmaps the clip and zeroes *clip. Safe on a zeroed clip and when called
 * twice. */
void audio_clip_free(clip_t *clip);

/* Privacy: unmaps the clip (if still mapped) and erases the part of the
 * "clip" partition the last recording wrote (about 0.3 s for 8 s of
 * audio), so no bedroom audio stays readable in flash. Call once the clip
 * is no longer needed; nothing to do when already erased. Blocks; on the
 * recording task. */
void audio_clip_erase(void);
