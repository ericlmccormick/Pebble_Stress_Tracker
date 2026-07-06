#include "vitalgauge.h"

// ============================================================================
// --- SECTION 1: AUDIO ENGINE STATE ---
// ============================================================================
static bool s_amp_powered = false;
static uint8_t s_stream_chunk[1024];
static ResHandle s_audio_handle = NULL;
static size_t s_audio_size = 0;
static size_t s_audio_read = 0;
static AppTimer *s_audio_timer = NULL;

// ============================================================================
// --- SECTION 2: AUDIO PLAYBACK STREAM ---
// ============================================================================
static void audio_stream_callback(void *data) {
    if (s_audio_read >= s_audio_size) {
        memset(s_stream_chunk, 0, sizeof(s_stream_chunk));
        speaker_stream_write(s_stream_chunk, sizeof(s_stream_chunk));
        s_audio_handle = NULL;
        s_audio_timer = NULL;
        return;
    }

    size_t chunk = (s_audio_size - s_audio_read > sizeof(s_stream_chunk)) ? sizeof(s_stream_chunk) : s_audio_size - s_audio_read;
    chunk &= ~1; 

    if (chunk > 0) {
        resource_load_byte_range(s_audio_handle, s_audio_read, s_stream_chunk, chunk);
        
        int16_t *samples = (int16_t *)s_stream_chunk;
        size_t num_samples = chunk / 2;
        size_t current_overall_sample = s_audio_read / 2;
        size_t total_samples = s_audio_size / 2;
        size_t fade_samples = 1600;

        for(size_t i = 0; i < num_samples; i++) {
            int32_t sample_val = samples[i];
            size_t global_i = current_overall_sample + i;
            size_t samples_left = total_samples - global_i;
            int current_vol = g_volume_level;

            if (samples_left < fade_samples) {
                current_vol = (g_volume_level * samples_left) / fade_samples;
            }
            samples[i] = (int16_t)((sample_val * current_vol) / 100);
        }

        speaker_stream_write(s_stream_chunk, chunk);
        s_audio_read += chunk;
        s_audio_timer = app_timer_register(30, audio_stream_callback, NULL);
    }
}

// ============================================================================
// --- SECTION 3: AUDIO ENGINE CONTROL ---
// ============================================================================
void audio_engine_play(uint32_t res_id) {
    if (g_volume_level == 0) return;
    if (s_audio_timer) {
        app_timer_cancel(s_audio_timer);
        s_audio_timer = NULL;
    }

    s_audio_handle = resource_get_handle(res_id);
    if (!s_audio_handle) return;

    s_audio_size = resource_size(s_audio_handle);
    s_audio_read = 0;

    if (!s_amp_powered) {
        speaker_stream_open(SpeakerPcmFormat_16kHz_16bit, g_volume_level);
        memset(s_stream_chunk, 0, 100);
        speaker_stream_write(s_stream_chunk, 100);
        s_amp_powered = true;
    }
    s_audio_timer = app_timer_register(1, audio_stream_callback, NULL);
}

void audio_engine_stop() {
    if (s_audio_timer) {
        app_timer_cancel(s_audio_timer);
        s_audio_timer = NULL;
    }
    s_audio_handle = NULL;
    if (s_amp_powered) {
        memset(s_stream_chunk, 0, sizeof(s_stream_chunk));
        speaker_stream_write(s_stream_chunk, sizeof(s_stream_chunk));
        psleep(50); 
        speaker_stream_close();
        s_amp_powered = false;
    }
}