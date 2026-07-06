#include "vitalgauge.h"

// ============================================================================
// --- SECTION 1: APPSYNC KEYS AND STATE ---
// ============================================================================
static AppSync s_sync;
static uint8_t s_sync_buffer[128];

enum AppSyncKeys {
    SYNC_VOLUME_KEY = 0,
    SYNC_TARGET_HR_KEY = 1
};

// ============================================================================
// --- SECTION 2: SYNC CALLBACKS ---
// ============================================================================
static void sync_tuple_changed_callback(const uint32_t key, const Tuple* new_tuple, const Tuple* old_tuple, void* context) {
    switch (key) {
        case SYNC_VOLUME_KEY:
            g_volume_level = new_tuple->value->int32;
            persist_write_int(PERSIST_KEY_VOLUME, g_volume_level);
            ui_stress_mark_dirty();
            break;
        case SYNC_TARGET_HR_KEY:
            g_target_hr = new_tuple->value->int32;
            break;
    }
}

static void sync_error_callback(DictionaryResult dict_error, AppMessageResult app_message_error, void *context) {
    APP_LOG(APP_LOG_LEVEL_ERROR, "AppSync Error: %d", app_message_error);
}

// ============================================================================
// --- SECTION 3: SYNC INIT/DEINIT ---
// ============================================================================
void config_sync_init() {
    if (persist_exists(PERSIST_KEY_VOLUME)) { 
        g_volume_level = persist_read_int(PERSIST_KEY_VOLUME);
    }
    Tuplet initial_values[] = {
        TupletInteger(SYNC_VOLUME_KEY, g_volume_level),
        TupletInteger(SYNC_TARGET_HR_KEY, 60)
    };
    app_sync_init(&s_sync, s_sync_buffer, sizeof(s_sync_buffer), initial_values, ARRAY_LENGTH(initial_values), sync_tuple_changed_callback, sync_error_callback, NULL);
}

void config_sync_deinit() {
    app_sync_deinit(&s_sync);
}