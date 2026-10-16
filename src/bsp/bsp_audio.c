#include "bsp/audio.h"

#include <SDL2/SDL.h>
#include <stdint.h>
#include <unistd.h>

#include "esp_err.h"
#include "esp_log.h"

static char const TAG[] = "bsp_audio";

static float s_volume_percentage = 50.0f;
static bool  s_amplifier_enabled = false;
static bool  s_amplifier_forced  = false;

// The real I2S peripheral this stands in for: synthengine3D's audio_mixer.c
// (see se_audio.h) always drives it as 22050 Hz, signed-16-bit PCM, stereo
// interleaved -- a fixed pipeline, not a general driver -- so that's all
// this needs to support. The device is (re)opened in bsp_audio_set_rate(),
// since that's always called (per audio_mixer_init()'s documented sequence:
// get_i2s_handle, disable, set_rate, enable) before the channel is enabled,
// and apps determine their own pipeline rate, not this file.
static SDL_AudioDeviceID s_dev     = 0;
static uint32_t          s_rate    = 0;
static bool               s_enabled = false;

// A real (non-NULL) value is enough: apps only ever hand this back to
// i2s_channel_*() calls (also emulated, see i2c_i2s_shim.c), never
// dereference it -- i2s_chan_handle_t is an opaque incomplete struct
// pointer. Same pattern as bsp_display_get_panel()'s s_dummy_panel.
static int s_dummy_i2s_handle;

esp_err_t bsp_audio_set_rate(uint32_t rate) {
    if (rate == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_dev != 0 && s_rate == rate) {
        return ESP_OK;
    }
    if (s_dev != 0) {
        SDL_CloseAudioDevice(s_dev);
        s_dev = 0;
    }
    if (SDL_WasInit(SDL_INIT_AUDIO) == 0 && SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) {
        ESP_LOGE(TAG, "SDL_InitSubSystem(AUDIO) failed: %s", SDL_GetError());
        return ESP_FAIL;
    }

    SDL_AudioSpec want;
    SDL_zero(want);
    want.freq     = (int)rate;
    want.format   = AUDIO_S16LSB;
    want.channels = 2;
    want.samples  = 1024;
    want.callback = NULL;  // queued (push) mode: see bsp_audio_internal_channel_write()

    SDL_AudioSpec have;
    // No `changes allowed` flags: the pipeline format above is fixed, not
    // negotiated, so a device that can't open at exactly this spec should
    // fail loudly rather than silently resample/reformat.
    s_dev = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
    if (s_dev == 0) {
        ESP_LOGE(TAG, "SDL_OpenAudioDevice(%u Hz) failed: %s", (unsigned)rate, SDL_GetError());
        return ESP_FAIL;
    }
    s_rate = rate;
    if (s_enabled) {
        SDL_PauseAudioDevice(s_dev, 0);
    }
    return ESP_OK;
}

esp_err_t bsp_audio_get_volume(float* out_percentage) {
    if (out_percentage == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    *out_percentage = s_volume_percentage;
    return ESP_OK;
}

esp_err_t bsp_audio_set_volume(float percentage) {
    s_volume_percentage = percentage;
    return ESP_OK;
}

esp_err_t bsp_audio_set_amplifier(bool enable) {
    s_amplifier_enabled = enable;
    return ESP_OK;
}

esp_err_t bsp_audio_set_amplifier_force(bool force) {
    s_amplifier_forced = force;
    return ESP_OK;
}

esp_err_t bsp_audio_get_i2s_handle(i2s_chan_handle_t* out_handle) {
    if (out_handle == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    *out_handle = (i2s_chan_handle_t)&s_dummy_i2s_handle;
    return ESP_OK;
}

// --- Internal, for i2c_i2s_shim.c's i2s_channel_*() traps --------------

esp_err_t bsp_audio_internal_channel_enable(void) {
    s_enabled = true;
    if (s_dev != 0) {
        SDL_PauseAudioDevice(s_dev, 0);
    }
    return ESP_OK;
}

esp_err_t bsp_audio_internal_channel_disable(void) {
    s_enabled = false;
    if (s_dev != 0) {
        SDL_PauseAudioDevice(s_dev, 1);
    }
    return ESP_OK;
}

esp_err_t bsp_audio_internal_channel_write(void const* data, size_t size, size_t* out_written) {
    if (s_dev == 0 || !s_enabled || data == NULL) {
        if (out_written != NULL) {
            *out_written = 0;
        }
        return ESP_ERR_INVALID_STATE;
    }

    // Apply the software volume knob (amplifier gain stands in for the
    // real device's analog gain, which has nothing to scale in software):
    // mix a silence buffer up to `percentage`, rather than queuing the
    // caller's buffer directly, so bsp_audio_set_volume() actually affects
    // output. SDL_MixAudioFormat clips safely; a raw multiply would not.
    uint8_t mixed[4096];  // 4x synthengine3D's actual 1024-byte chunk (see audio_mixer.c's MIXER_CHUNK_BYTES)
    if (size > sizeof(mixed)) {
        ESP_LOGW(TAG, "i2s write %u bytes > %u byte scratch buffer; truncating", (unsigned)size, (unsigned)sizeof(mixed));
    }
    size_t mix_len = size < sizeof(mixed) ? size : sizeof(mixed);
    SDL_memset(mixed, 0, mix_len);
    int const vol = (int)((s_volume_percentage / 100.0f) * SDL_MIX_MAXVOLUME);
    SDL_MixAudioFormat(mixed, (uint8_t const*)data, AUDIO_S16LSB, (Uint32)mix_len, vol);

    if (SDL_QueueAudio(s_dev, mixed, (Uint32)mix_len) != 0) {
        ESP_LOGW(TAG, "SDL_QueueAudio failed: %s", SDL_GetError());
        if (out_written != NULL) {
            *out_written = 0;
        }
        return ESP_FAIL;
    }
    if (out_written != NULL) {
        *out_written = mix_len;
    }

    // Real I2S DMA blocks the writer until there's buffer room, which is
    // what paces the app's mixer task to real playback speed rather than
    // it producing audio as fast as this emulator's interpreter can run
    // (synthengine3D's audio_mixer.c always calls this with
    // timeout_ms=portMAX_DELAY, i.e. it expects to block here). Block
    // until the SDL queue has drained to a few chunks' depth -- genuine
    // backpressure, self-correcting against any per-call overhead, unlike
    // sleeping a fixed "nominal chunk duration" per call (which drifts
    // behind real time and the speaker underruns).
    Uint32 const target_queued = (Uint32)mix_len * 4;
    while (SDL_GetQueuedAudioSize(s_dev) > target_queued) {
        usleep(1000);
    }
    return ESP_OK;
}
