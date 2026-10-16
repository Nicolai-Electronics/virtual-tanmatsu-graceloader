#include "bsp/audio.h"

#include <SDL2/SDL.h>
#include <stdint.h>
#include <unistd.h>

#include "esp_err.h"
#include "esp_log.h"
#include "esp_timer.h"

static char const TAG[] = "bsp_audio";

static float s_volume_percentage = 50.0f;
static bool  s_amplifier_enabled = false;
static bool  s_amplifier_forced  = false;

// The real I2S peripheral this stands in for. It is a fixed pipeline rather
// than a general driver -- signed-16-bit PCM, stereo interleaved, at whatever
// rate the app asked for: synthengine3D's audio_mixer.c (see se_audio.h) runs
// it at 22050 Hz, and the apps that don't use synthengine3D (Ballz, Pogn,
// Floppy Bird, Musical Keyboard) run it at 44100 Hz. Apps determine their own
// pipeline rate, not this file; the device is (re)opened in
// bsp_audio_set_rate().
static SDL_AudioDeviceID s_dev     = 0;
static uint32_t          s_rate    = 0;

// The BSP has the I2S channel running before an app gets control. That's what
// synthengine3D's audio_mixer_init() documents: "The BSP enables the I2S
// channel inside bsp_device_initialize() at whatever sample rate the boot
// config picked (typically 16 kHz)". So an app is not required to enable the
// channel -- and the apps that don't go through synthengine3D never do: they
// call bsp_audio_initialize(), bsp_audio_get_i2s_handle() and then write
// straight to i2s_channel_write(), with no i2s_channel_enable() anywhere.
// Starting this false made every one of those writes fail with
// ESP_ERR_INVALID_STATE, which those apps ignore, so they ran silently while
// the synthengine3D apps -- which do call i2s_channel_enable() only to work
// around the driver rejecting a rate change on a live channel -- had sound.
static bool     s_enabled   = true;
static uint32_t s_boot_rate = 16000;  // where bsp_device_initialize() leaves it

// --- The stream clock -----------------------------------------------------
//
// Real I2S DMA transmits at the sample rate, and i2s_channel_write() blocks
// until a DMA descriptor frees. That blocking write is the *only* thing that
// paces an app's mixer task: none of the apps this emulator runs has a
// vTaskDelay(), a timer or any other sleep in its audio loop -- synthengine3D's
// audio_mixer.c and Ballz's audio_task() both just write as fast as the
// channel lets them -- so the emulator has to model the transmission, not the
// buffer.
//
// s_bytes_streamed is everything handed to the channel since it started
// transmitting; by now the DMA would have finished
// (now - s_stream_us) * rate * 4/1e6 bytes of it. A write may run at most
// STREAM_LEAD_MS ahead of the stream, which is how much audio the DMA queue
// holds.
//
// Pacing off the time the stream needs -- rather than a queue depth measured
// in multiples of the caller's own chunk -- is the whole point of this. SDL
// drains a queued-mode device in bursts of `obtained.samples` frames, so a
// target smaller than one burst gates the writer to one burst per burst no
// matter how fast it wants to go. Ballz writes 256 bytes at a time (64 stereo
// frames, see its main.c's FRAMES_PER_WRITE), so a "4 chunks" target was 1024
// bytes against a 4096-byte burst, and its mixer task ran at 215 writes/s
// instead of the 689 that 44100 Hz / 64 frames needs.
#define STREAM_LEAD_MS 40

static int64_t  s_stream_us      = 0;  // when the channel started transmitting
static uint64_t s_bytes_streamed = 0;  // bytes handed to it since then

// (Re)start the stream from now, dropping anything still queued from before:
// a channel that was closed, reopened at another rate, or disabled and
// enabled again has not transmitted any of it.
static void stream_restart(void) {
    s_stream_us      = esp_timer_get_time();
    s_bytes_streamed = 0;
    if (s_dev != 0) {
        SDL_ClearQueuedAudio(s_dev);
    }
}

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
    // The device's drain granularity: a queued-mode SDL device is emptied in
    // bursts of this many frames, and the stream clock above is what the
    // writer is paced against. A smaller period means finer bursts and less
    // latency; 256 frames is 5.8 ms at 44100 Hz, 11.6 ms at 22050 Hz.
    want.samples  = 256;
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
    stream_restart();
    ESP_LOGI(TAG, "I2S channel open: %u Hz, %u byte bursts, %d ms of DMA queue",
             (unsigned)rate, (unsigned)(have.samples * 4), STREAM_LEAD_MS);
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
        // Nothing queued while the channel was disabled was ever transmitted,
        // so the stream starts from now rather than carrying that gap over.
        stream_restart();
    }
    return ESP_OK;
}

esp_err_t bsp_audio_internal_channel_disable(void) {
    s_enabled = false;
    if (s_dev != 0) {
        SDL_PauseAudioDevice(s_dev, 1);
    }
    s_stream_us = 0;  // stopped: the next enable re-anchors the stream clock
    return ESP_OK;
}

esp_err_t bsp_audio_internal_channel_write(void const* data, size_t size, size_t* out_written) {
    if (data == NULL) {
        if (out_written != NULL) {
            *out_written = 0;
        }
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_enabled) {
        // Only reachable if the app disabled the channel itself. Worth saying
        // out loud once: on the device the channel is live from boot, so an
        // app that calls i2s_channel_disable() has to enable it again to hear
        // anything back.
        static bool warned = false;
        if (!warned) {
            warned = true;
            ESP_LOGW(TAG, "i2s_channel_write on a channel this app disabled: enabling it again is what makes it audible");
        }
        if (out_written != NULL) {
            *out_written = 0;
        }
        return ESP_ERR_INVALID_STATE;
    }
    if (s_dev == 0) {
        // Nothing named a rate, so the channel is still at the one the BSP
        // booted it at -- which is what an app that never calls
        // bsp_audio_set_rate()/bsp_audio_initialize() gets on the device too.
        ESP_LOGI(TAG, "no app set a sample rate, opening the channel at the boot rate %u Hz", (unsigned)s_boot_rate);
        if (bsp_audio_set_rate(s_boot_rate) != ESP_OK) {
            if (out_written != NULL) {
                *out_written = 0;
            }
            return ESP_FAIL;
        }
    }

    // Apply the software volume knob (amplifier gain stands in for the
    // real device's analog gain, which has nothing to scale in software):
    // mix a silence buffer up to `percentage`, rather than queuing the
    // caller's buffer directly, so bsp_audio_set_volume() actually affects
    // output. SDL_MixAudioFormat clips safely; a raw multiply would not.
    // Chunked through a scratch buffer because a write is as big as the app's
    // mixer task decided, not as big as this buffer: synthengine3D's
    // audio_mixer.c uses 1024 bytes (MIXER_CHUNK_BYTES), and dropping or
    // truncating the rest of a larger one would silently eat audio.
    int     const vol    = (int)((s_volume_percentage / 100.0f) * SDL_MIX_MAXVOLUME);
    uint8_t mixed[4096];
    size_t  queued       = 0;
    while (queued < size) {
        size_t const chunk = size - queued < sizeof(mixed) ? size - queued : sizeof(mixed);
        SDL_memset(mixed, 0, chunk);
        SDL_MixAudioFormat(mixed, (uint8_t const*)data + queued, AUDIO_S16LSB, (Uint32)chunk, vol);
        if (SDL_QueueAudio(s_dev, mixed, (Uint32)chunk) != 0) {
            ESP_LOGW(TAG, "SDL_QueueAudio failed: %s", SDL_GetError());
            if (out_written != NULL) {
                *out_written = queued;
            }
            return queued > 0 ? ESP_OK : ESP_FAIL;
        }
        queued += chunk;
    }
    if (out_written != NULL) {
        *out_written = queued;
    }

    // Real I2S DMA blocks the writer until a descriptor frees, which is what
    // paces the app's mixer task to real playback speed rather than it
    // producing audio as fast as this emulator's interpreter can run (every
    // app here calls this with timeout_ms=portMAX_DELAY, i.e. it expects to
    // block). Wait until the stream clock has caught up with what was just
    // handed to it, sleeping exactly as long as the stream needs so the writer
    // lands on the stream instead of overshooting it a millisecond at a time.
    //
    // Self-correcting in both directions: an app that runs ahead waits, an app
    // the interpreter fell behind catches up for free, up to the lead.
    s_bytes_streamed += queued;
    if (s_stream_us == 0) {
        stream_restart();
    }
    int64_t const lead_bytes   = ((int64_t)s_rate * 4 * STREAM_LEAD_MS) / 1000;
    for (;;) {
        int64_t const now         = esp_timer_get_time();
        int64_t const transmitted = (now - s_stream_us) * (int64_t)s_rate * 4 / 1000000;
        int64_t const ahead       = (int64_t)s_bytes_streamed - transmitted;
        if (ahead <= lead_bytes) {
            break;
        }
        int64_t const wait_us = (ahead - lead_bytes) * 1000000 / ((int64_t)s_rate * 4);
        usleep((useconds_t)(wait_us > 0 ? wait_us : 200));
    }

    // The other direction is worth reporting once: the interpreter could not
    // produce audio as fast as the stream consumed it, so the speaker ran dry.
    // Nothing here can fix that from the emulator side -- on the device the
    // same app would simply have been starved of CPU.
    {
        int64_t const transmitted = (esp_timer_get_time() - s_stream_us) * (int64_t)s_rate * 4 / 1000000;
        static bool   warned      = false;
        if (!warned && transmitted > (int64_t)s_bytes_streamed + lead_bytes * 4) {
            warned = true;
            ESP_LOGW(TAG, "this app is producing audio %.1f ms behind the stream: the interpreter is slower than its mixer task",
                     (float)(transmitted - (int64_t)s_bytes_streamed) / ((float)s_rate * 4.0f) * 1000.0f);
        }
    }
    return ESP_OK;
}
