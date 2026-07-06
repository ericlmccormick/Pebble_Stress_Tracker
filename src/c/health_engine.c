#include "vitalgauge.h"

#define CHUNK_SIZE 240

// ============================================================================
// --- SECTION 1: GLOBAL STATE INSTANTIATION ---
// ============================================================================
int g_live_energy_score = 0;
int g_sleep_comp = 0;
int g_readiness_comp = 0;
int g_card_eff_comp = 0;
int g_exertion_comp = 0;
int g_stress_comp = 0;
int g_morning_hr_comp = 0;
int g_volume_level = 100;
int g_target_hr = 60;

uint16_t g_act_map[144] = {0};
uint8_t g_stress_map[144] = {0};
uint8_t g_sleep_map[1440] = {0};

time_t g_24h_start = 0;
time_t g_sleep_start = 0;
time_t g_sleep_end = 0;
int g_sleep_deep_sec = 0;
int g_sleep_light_sec = 0;
int g_longest_sleep_duration = 0;

int g_hist_energy[7] = {0};
int g_hist_sleep[7] = {0};
int g_hist_stress[7] = {0};
int g_hist_activity[7] = {0};

static bool s_native_restful_mask[1440] = {0};
static DataLoggingSessionRef s_log_session;

// ============================================================================
// --- SECTION 2: CALCULATION ENGINE ---
// ============================================================================
void health_engine_calculate_energy() {
    g_sleep_comp = persist_exists(PERSIST_KEY_SLEEP_SCORE) ? persist_read_int(PERSIST_KEY_SLEEP_SCORE) : 0;
    g_readiness_comp = persist_exists(PERSIST_KEY_READINESS) ? persist_read_int(PERSIST_KEY_READINESS) : 0;
    int stress_score = persist_exists(PERSIST_KEY_SCORE) ? persist_read_int(PERSIST_KEY_SCORE) : 0;

    time_t now = time(NULL);
    time_t start_today = time_start_of_today();
    
    int min_hr = (int)health_service_aggregate_averaged(HealthMetricHeartRateBPM, start_today, now, HealthAggregationMin, HealthServiceTimeScopeOnce);
    int max_hr = (int)health_service_aggregate_averaged(HealthMetricHeartRateBPM, start_today, now, HealthAggregationMax, HealthServiceTimeScopeOnce);
    
    if (min_hr <= 0) min_hr = 60;
    g_card_eff_comp = (min_hr > 0) ? (153 * (max_hr > 0 ? max_hr : 120)) / (min_hr * 10) : 0;
    
    int active_kcal = (int)health_service_sum_today(HealthMetricActiveKCalories);
    g_exertion_comp = active_kcal / 15;
    g_stress_comp = stress_score / 5;

    int base_charge = (g_sleep_comp + g_readiness_comp) / 2;
    int final_score = base_charge + (g_card_eff_comp / 10) - g_exertion_comp - g_stress_comp;

    if (final_score > 100) final_score = 100;
    if (final_score < 0) final_score = 0;

    g_live_energy_score = final_score;
    persist_write_int(PERSIST_KEY_ENERGY_RESERVE, final_score);
}

static void update_history_arrays() {
    time_t now = time(NULL);
    struct tm *t = localtime(&now);
    int current_day = t->tm_yday;
    int last_day = persist_exists(PERSIST_KEY_HIST_DAY) ? persist_read_int(PERSIST_KEY_HIST_DAY) : -1;

    if (persist_exists(PERSIST_KEY_HIST_ENERGY)) persist_read_data(PERSIST_KEY_HIST_ENERGY, g_hist_energy, sizeof(g_hist_energy));
    if (persist_exists(PERSIST_KEY_HIST_SLEEP)) persist_read_data(PERSIST_KEY_HIST_SLEEP, g_hist_sleep, sizeof(g_hist_sleep));
    if (persist_exists(PERSIST_KEY_HIST_STRESS)) persist_read_data(PERSIST_KEY_HIST_STRESS, g_hist_stress, sizeof(g_hist_stress));
    if (persist_exists(PERSIST_KEY_HIST_ACTIVITY)) persist_read_data(PERSIST_KEY_HIST_ACTIVITY, g_hist_activity, sizeof(g_hist_activity));

    if (last_day != current_day && last_day != -1) {
        for(int i = 0; i < 6; i++) {
            g_hist_energy[i] = g_hist_energy[i+1];
            g_hist_sleep[i] = g_hist_sleep[i+1];
            g_hist_stress[i] = g_hist_stress[i+1];
            g_hist_activity[i] = g_hist_activity[i+1];
        }
        g_hist_energy[6] = 0; g_hist_sleep[6] = 0; g_hist_stress[6] = 0; g_hist_activity[6] = 0;
        persist_write_int(PERSIST_KEY_HIST_DAY, current_day);
    } else if (last_day == -1) {
        persist_write_int(PERSIST_KEY_HIST_DAY, current_day);
    }

    g_hist_energy[6] = g_live_energy_score;
    g_hist_sleep[6] = g_sleep_comp;
    g_hist_stress[6] = persist_exists(PERSIST_KEY_SCORE) ? persist_read_int(PERSIST_KEY_SCORE) : 0;
    
    int steps = (int)health_service_sum_today(HealthMetricStepCount);
    int act_score = (steps * 100) / 10000;
    g_hist_activity[6] = act_score > 100 ? 100 : act_score;

    persist_write_data(PERSIST_KEY_HIST_ENERGY, g_hist_energy, sizeof(g_hist_energy));
    persist_write_data(PERSIST_KEY_HIST_SLEEP, g_hist_sleep, sizeof(g_hist_sleep));
    persist_write_data(PERSIST_KEY_HIST_STRESS, g_hist_stress, sizeof(g_hist_stress));
    persist_write_data(PERSIST_KEY_HIST_ACTIVITY, g_hist_activity, sizeof(g_hist_activity));
}

// ============================================================================
// --- SECTION: DATA FETCHING AND PARSING (health_engine.c) ---
// ============================================================================
static bool sleep_session_cb(HealthActivity activity, time_t time_start, time_t time_end, void *context) {
    if (activity == HealthActivitySleep) {
        if (g_sleep_start == 0 || time_start < g_sleep_start) g_sleep_start = time_start;
        if (g_sleep_end == 0 || time_end > g_sleep_end) g_sleep_end = time_end;
        g_longest_sleep_duration = g_sleep_end - g_sleep_start;
    } else if (activity == HealthActivityRestfulSleep) {
        int s = (time_start - g_24h_start) / 60, e = (time_end - g_24h_start) / 60;
        for (int i = (s < 0 ? 0 : s); i <= (e > 1439 ? 1439 : e); i++) s_native_restful_mask[i] = true;
    }
    return true;
}

void health_engine_update_buffers() { 
    time_t now = time(NULL); g_24h_start = now - (24 * 3600);
    g_sleep_start = 0; g_sleep_end = 0; g_sleep_deep_sec = 0; g_sleep_light_sec = 0; g_longest_sleep_duration = 0;
    memset(g_act_map, 0, sizeof(g_act_map)); memset(g_stress_map, 0, sizeof(g_stress_map));
    memset(g_sleep_map, 0, sizeof(g_sleep_map)); memset(s_native_restful_mask, 0, sizeof(s_native_restful_mask));
    
    health_service_activities_iterate(HealthActivitySleep | HealthActivityRestfulSleep, time_start_of_today() - (12 * 3600), now, HealthIterationDirectionPast, sleep_session_cb, NULL);
    
    time_t rhr_window_end = (g_sleep_end != 0) ? g_sleep_end : time_start_of_today() + (6 * 3600);
    int m_hr = 999;
    HealthMinuteData chunk[CHUNK_SIZE];
    for (int c = 0; c < 6; c++) {
        time_t chunk_start = g_24h_start + (c * CHUNK_SIZE * 60), chunk_end = chunk_start + (CHUNK_SIZE * 60);
        uint32_t fetched = health_service_get_minute_history(chunk, CHUNK_SIZE, &chunk_start, &chunk_end);
        for (uint32_t i = 0; i < fetched; i++) {
            if (chunk[i].is_invalid) continue;
            int min_of_day = (chunk_start + (i * 60) - g_24h_start) / 60, b = min_of_day / 10;
            if (b >= 0 && b < 144) { g_act_map[b] += chunk[i].steps; if (chunk[i].heart_rate_bpm > g_stress_map[b]) g_stress_map[b] = chunk[i].heart_rate_bpm; }
            if (chunk_start + ((time_t)i * 60) >= g_sleep_start && chunk_start + ((time_t)i * 60) <= g_sleep_end) {
                if (min_of_day >= 0 && min_of_day < 1440) {
                    if (chunk[i].steps > 15 || chunk[i].vmc > 350) g_sleep_map[min_of_day] = 0;
                    else if (s_native_restful_mask[min_of_day]) { g_sleep_deep_sec += 60; g_sleep_map[min_of_day] = 2; }
                    else { g_sleep_light_sec += 60; g_sleep_map[min_of_day] = 1; }
                }
            }
            if (chunk_start + ((time_t)i * 60) >= rhr_window_end - (90 * 60) && chunk_start + ((time_t)i * 60) <= rhr_window_end) if (chunk[i].heart_rate_bpm > 0 && chunk[i].vmc == 0 && chunk[i].heart_rate_bpm < m_hr) m_hr = chunk[i].heart_rate_bpm;
        }
    }
    g_morning_hr_comp = (m_hr == 999) ? 60 : m_hr;
    persist_write_int(PERSIST_KEY_RHR, g_morning_hr_comp);
    int net = g_sleep_deep_sec + g_sleep_light_sec;
    g_sleep_comp = (net > 0) ? (((((net * 100) / (8 * 3600)) > 100 ? 100 : (net * 100) / (8 * 3600)) * 70) + (((((g_sleep_deep_sec * 100) / net) * 100) / 25) > 100 ? 100 : (((g_sleep_deep_sec * 100) / net) * 100) / 25) * 30) / 100 : 0;
    persist_write_int(PERSIST_KEY_SLEEP_SCORE, g_sleep_comp);
    g_readiness_comp = (g_sleep_comp - (g_morning_hr_comp - 55 < 0 ? 0 : g_morning_hr_comp - 55) > 100) ? 100 : (g_sleep_comp - (g_morning_hr_comp - 55 < 0 ? 0 : g_morning_hr_comp - 55) < 10 ? 10 : g_sleep_comp - (g_morning_hr_comp - 55 < 0 ? 0 : g_morning_hr_comp - 55));
    persist_write_int(PERSIST_KEY_READINESS, g_readiness_comp);
    health_engine_calculate_energy(); update_history_arrays();
}

// ============================================================================
// --- SECTION 4: DATALOGGING & LIFECYCLE ---
// ============================================================================
void health_engine_init() { 
    s_log_session = data_logging_create(0xDEADBEEF, DATA_LOGGING_INT, 4, false); 
    if (persist_exists(PERSIST_KEY_ENERGY_RESERVE)) g_live_energy_score = persist_read_int(PERSIST_KEY_ENERGY_RESERVE);
    if (persist_exists(PERSIST_KEY_SLEEP_SCORE)) g_sleep_comp = persist_read_int(PERSIST_KEY_SLEEP_SCORE);
    if (persist_exists(PERSIST_KEY_READINESS)) g_readiness_comp = persist_read_int(PERSIST_KEY_READINESS);
}

void health_engine_deinit() {
    data_logging_finish(s_log_session);
}

void health_engine_log_metrics() {
    int data[1] = { g_live_energy_score };
    data_logging_log(s_log_session, data, 1);
}