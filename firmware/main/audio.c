#include "audio.h"

#include <stdlib.h>
#include <string.h>

#include "board.h"
#include "core/wav.h"
#include "driver/gpio.h"
#include "driver/i2s_std.h"
#include "es7210_adc.h"
#include "esp_codec_dev.h"
#include "esp_codec_dev_defaults.h"
#include "esp_attr.h"
#include "esp_log.h"
#include "esp_partition.h"
#include "esp_timer.h"

static const char *TAG = "audio";

enum {
    FRAMES = 512, /* per read: 32 ms at 16 kHz */
};

/* The chain as the spike built it (spike-mic main.c mic_start) */
typedef struct {
    i2s_chan_handle_t rx;
    const audio_codec_ctrl_if_t *ctrl;
    const audio_codec_data_if_t *data;
    const audio_codec_if_t *codec;
    esp_codec_dev_handle_t dev;
} mic_t;

/* Closes what was opened (the ES7210 powers down on close), frees the I²S
 * channel and drives its clock lines low again, as board_init left them */
static void mic_stop(mic_t *m)
{
    if (m->dev) {
        esp_codec_dev_close(m->dev);
        esp_codec_dev_delete(m->dev);
    }
    if (m->codec) audio_codec_delete_codec_if(m->codec);
    if (m->ctrl) audio_codec_delete_ctrl_if(m->ctrl);
    if (m->data) audio_codec_delete_data_if(m->data);
    if (m->rx) i2s_del_channel(m->rx);
    memset(m, 0, sizeof(*m));
    static const int pins[] = {PIN_AUDIO_MCLK, PIN_AUDIO_BCLK, PIN_AUDIO_LRCLK};
    for (size_t i = 0; i < sizeof(pins) / sizeof(pins[0]); i++) {
        gpio_set_level(pins[i], 0);
        gpio_config_t cfg = {.pin_bit_mask = 1ULL << pins[i], .mode = GPIO_MODE_INPUT_OUTPUT};
        gpio_config(&cfg);
    }
    /* esp_codec_dev_close ran es7210_stop, but nothing checked it: power the
     * ADC down again with read-back so it never stays on through deep sleep */
    if (board_mic_adc_off()) ESP_LOGI(TAG, "ES7210 off after recording, verified");
    else ESP_LOGE(TAG, "ES7210 power-down after recording FAILED (read-back did not match)");
}

/* DMA buffers the reader did not take in time: lost samples */
static volatile unsigned overflows;

static bool IRAM_ATTR on_overflow(i2s_chan_handle_t h, i2s_event_data_t *e, void *ctx)
{
    overflows++;
    return false;
}

static bool mic_start(mic_t *m)
{
    memset(m, 0, sizeof(*m));
    i2s_chan_config_t chan = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    /* 8 x 480 frames = 240 ms of DMA buffer (15 KB): room for a flash write
     * (which stalls the CPU) between reads */
    chan.dma_desc_num = 8;
    chan.dma_frame_num = 480;
    if (i2s_new_channel(&chan, NULL, &m->rx) != ESP_OK) {
        ESP_LOGE(TAG, "I2S channel failed");
        return false;
    }
    i2s_std_config_t std = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(AUDIO_RATE),
        .slot_cfg =
            I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {.mclk = PIN_AUDIO_MCLK, .bclk = PIN_AUDIO_BCLK, .ws = PIN_AUDIO_LRCLK,
                     .dout = I2S_GPIO_UNUSED, .din = PIN_AUDIO_DIN},
    };
    if (i2s_channel_init_std_mode(m->rx, &std) != ESP_OK) {
        ESP_LOGE(TAG, "I2S init failed");
        mic_stop(m);
        return false;
    }
    i2s_event_callbacks_t cbs = {.on_recv_q_ovf = on_overflow};
    i2s_channel_register_event_callback(m->rx, &cbs, NULL);
    audio_codec_i2c_cfg_t i2c_cfg = {
        .port = 0, .addr = ES7210_CODEC_DEFAULT_ADDR, .bus_handle = board_i2c()};
    m->ctrl = audio_codec_new_i2c_ctrl(&i2c_cfg);
    audio_codec_i2s_cfg_t i2s_cfg = {.port = 0, .rx_handle = m->rx, .tx_handle = NULL};
    m->data = audio_codec_new_i2s_data(&i2s_cfg);
    if (m->ctrl) {
        es7210_codec_cfg_t es = {.ctrl_if = m->ctrl,
                                 .mic_selected = ES7210_SEL_MIC1 | ES7210_SEL_MIC2};
        m->codec = es7210_codec_new(&es);
    }
    if (!m->ctrl || !m->data || !m->codec) {
        ESP_LOGE(TAG, "ES7210 not set up (I2C %s)", m->ctrl ? "ok" : "failed");
        mic_stop(m);
        return false;
    }
    esp_codec_dev_cfg_t dev = {
        .dev_type = ESP_CODEC_DEV_TYPE_IN, .codec_if = m->codec, .data_if = m->data};
    m->dev = esp_codec_dev_new(&dev);
    esp_codec_dev_sample_info_t fs = {.bits_per_sample = 16, .channel = 2,
                                      .sample_rate = AUDIO_RATE};
    if (!m->dev || esp_codec_dev_open(m->dev, &fs) != ESP_CODEC_DEV_OK ||
        esp_codec_dev_set_in_gain(m->dev, AUDIO_PGA_DB) != ESP_CODEC_DEV_OK) {
        ESP_LOGE(TAG, "ES7210 open failed");
        mic_stop(m);
        return false;
    }
    return true;
}

/* The one clip: written to the "clip" partition, mapped for reading */
static esp_partition_mmap_handle_t map;
static bool mapped;
static size_t dirty; /* bytes from the partition start that may hold audio */

void audio_clip_free(clip_t *clip)
{
    if (mapped) {
        esp_partition_munmap(map);
        mapped = false;
    }
    if (clip) memset(clip, 0, sizeof(*clip));
}

void audio_clip_erase(void)
{
    audio_clip_free(NULL);
    if (!dirty) return;
    const esp_partition_t *part =
        esp_partition_find_first(ESP_PARTITION_TYPE_DATA, AUDIO_CLIP_SUBTYPE, "clip");
    int64_t t0 = esp_timer_get_time();
    esp_err_t err = part ? esp_partition_erase_range(part, 0, dirty) : ESP_ERR_NOT_FOUND;
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "clip erased after use: %u bytes in %lld ms", (unsigned)dirty,
                 (esp_timer_get_time() - t0) / 1000);
        dirty = 0;
    } else {
        ESP_LOGE(TAG, "clip erase after use failed: %s", esp_err_to_name(err));
    }
}

bool audio_record(clip_t *out, int max_ms, volatile bool *stop_early, volatile bool *started)
{
    audio_clip_free(out); /* a new recording overwrites the last */
    if (max_ms <= 0) return false;
    if (max_ms > AUDIO_MAX_MS) max_ms = AUDIO_MAX_MS;
    size_t samples = (size_t)AUDIO_RATE * (size_t)max_ms / 1000;
    size_t need = 44 + samples * 2;
    const esp_partition_t *part =
        esp_partition_find_first(ESP_PARTITION_TYPE_DATA, AUDIO_CLIP_SUBTYPE, "clip");
    if (!part || part->size < need) {
        ESP_LOGE(TAG, "no \"clip\" partition of %u bytes", (unsigned)need);
        return false;
    }
    /* Erased before the mic starts, so it never eats the speaking window */
    size_t erase = (need + 4095) & ~(size_t)4095;
    int64_t t0 = esp_timer_get_time();
    if (esp_partition_erase_range(part, 0, erase) != ESP_OK) {
        ESP_LOGE(TAG, "clip partition erase failed");
        return false;
    }
    int64_t t1 = esp_timer_get_time();
    int16_t *chunk = malloc(FRAMES * 2 * sizeof(int16_t)); /* stereo frames */
    if (!chunk) {
        ESP_LOGE(TAG, "no memory for the I2S chunk");
        return false;
    }
    mic_t m;
    if (!mic_start(&m)) {
        free(chunk);
        return false;
    }
    /* Start-up samples are discarded, as in the spike */
    esp_codec_dev_read(m.dev, chunk, FRAMES * 2 * sizeof(int16_t));
    esp_codec_dev_read(m.dev, chunk, FRAMES * 2 * sizeof(int16_t));
    overflows = 0;

    ESP_LOGI(TAG, "recording now... speak (up to %d ms; clip erased in %lld ms first)", max_ms,
             (t1 - t0) / 1000);
    if (started) *started = true;
    int64_t t2 = esp_timer_get_time();
    size_t n = 0;
    int peak = 0;
    size_t peak_at = 0;
    bool ok = true;
    while (n < samples && !(stop_early && *stop_early)) {
        if (esp_codec_dev_read(m.dev, chunk, FRAMES * 2 * sizeof(int16_t)) != ESP_CODEC_DEV_OK) {
            ESP_LOGE(TAG, "I2S read failed after %u samples", (unsigned)n);
            ok = false;
            break;
        }
        /* Mic 1 packed into the front of the chunk, then written straight on */
        size_t k = 0;
        for (; k < FRAMES && n + k < samples; k++) {
            int16_t v = pcm_gain(chunk[2 * k], AUDIO_SOFT_GAIN); /* mic 1 */
            chunk[k] = v;
            int a = v < 0 ? -(int)v : v;
            if (a > peak) {
                peak = a;
                peak_at = n + k;
            }
        }
        if (esp_partition_write(part, 44 + n * 2, chunk, k * 2) != ESP_OK) {
            ESP_LOGE(TAG, "clip write failed at %u samples", (unsigned)n);
            ok = false;
            break;
        }
        n += k;
    }
    int64_t t3 = esp_timer_get_time();
    /* What audio_clip_erase() must clear: the samples written, the chunk a
     * failed write may have half-written, and the header */
    size_t used = (44 + (n + FRAMES) * 2 + 4095) & ~(size_t)4095;
    dirty = used < erase ? used : erase;
    mic_stop(&m);
    free(chunk);
    uint8_t h[44];
    wav_header(h, (uint32_t)(n * 2), AUDIO_RATE);
    const void *p = NULL;
    if (ok && esp_partition_write(part, 0, h, sizeof(h)) != ESP_OK) {
        ESP_LOGE(TAG, "clip header write failed");
        ok = false;
    }
    if (ok &&
        esp_partition_mmap(part, 0, 44 + n * 2, ESP_PARTITION_MMAP_DATA, &p, &map) != ESP_OK) {
        ESP_LOGE(TAG, "clip map failed");
        ok = false;
    }
    if (!ok) return false;
    mapped = true;
    out->wav = (uint8_t *)p;
    out->len = 44 + n * 2;
    out->peak = peak;
    ESP_LOGI(TAG, "recorded %u ms of audio in %lld ms%s, %u I2S overflow(s), "
             "peak %d of 32767 at %u ms",
             (unsigned)(n * 1000 / AUDIO_RATE), (t3 - t2) / 1000,
             stop_early && *stop_early ? " (stopped early)" : "", (unsigned)overflows, peak,
             (unsigned)(peak_at * 1000 / AUDIO_RATE));
    return true;
}
