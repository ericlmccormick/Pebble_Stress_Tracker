#include <pebble_worker.h>

#define PERSIST_KEY_PIN_TYPE 200 
#define PERSIST_KEY_SCORE 201
#define PERSIST_KEY_RHR 202

static int s_worker_bpm_samples[120];
static int s_worker_sample_count = 0;
static int s_worker_target_samples = 0;
static AppTimer *s_worker_scan_timer = NULL;
static bool s_was_sleeping = false;

// Triggered when a background scan (either 60s or 120s) finishes
static void worker_scan_complete() {
  health_service_set_heart_rate_sample_period(0); // Turn sensor off
  
  int sum = 0, count = 0;
  int min = 999, max = 0;
  
  for(int i = 0; i < s_worker_sample_count; i++) {
    if(s_worker_bpm_samples[i] > 0) { 
      if(s_worker_bpm_samples[i] < min) min = s_worker_bpm_samples[i];
      if(s_worker_bpm_samples[i] > max) max = s_worker_bpm_samples[i];
      sum += s_worker_bpm_samples[i];
      count++;
    }
  }
  
  int range = (max == 0) ? 0 : (max - min);
  int score = 100 - (range * 8); 
  if (score < 10) score = 10;
  if (score > 100) score = 100;
  
  // Calculate average HR from the 120s morning scan to use as the baseline
  int morning_rhr = (count > 0) ? (sum / count) : 0;

  // Save the calculated data for the main app to pick up
  if (s_worker_target_samples == 120) {
    persist_write_int(PERSIST_KEY_PIN_TYPE, 1); // Morning Readiness Pin
  } else {
    // Only push a stress pin if the score is actually bad (< 50)
    if (score >= 50) return; 
    persist_write_int(PERSIST_KEY_PIN_TYPE, 2); // High Stress Pin
  }
  
  persist_write_int(PERSIST_KEY_SCORE, score);
  persist_write_int(PERSIST_KEY_RHR, morning_rhr);
  
  // Silently wake the main app to bridge the data to the phone
  worker_launch_app(); 
}

// Collects 1 sample per second
static void worker_sample_tick(void *data) {
  if (s_worker_sample_count < s_worker_target_samples) {
    HealthValue bpm = health_service_peek_current_value(HealthMetricHeartRateRawBPM);
    if (bpm > 0) {
      s_worker_bpm_samples[s_worker_sample_count] = bpm;
      s_worker_sample_count++;
    }
    s_worker_scan_timer = app_timer_register(1000, worker_sample_tick, NULL);
  } else {
    s_worker_scan_timer = NULL;
    worker_scan_complete();
  }
}

// Starts a high-frequency background HR read
static void start_background_scan(int seconds) {
  if (s_worker_scan_timer) return; // Prevent overlapping scans
  s_worker_sample_count = 0;
  s_worker_target_samples = seconds;
  memset(s_worker_bpm_samples, 0, sizeof(s_worker_bpm_samples));
  health_service_set_heart_rate_sample_period(1); // Force 1Hz mode
  worker_sample_tick(NULL);
}

// Polled every 1 minute
static void tick_handler(struct tm *tick_time, TimeUnits units_changed) {
  // 1. Check Sleep State for Morning Readiness
  HealthActivityMask activities = health_service_peek_current_activities();
  
  // Check against the correct SDK constants
  bool is_sleeping = (activities & HealthActivitySleep) || (activities & HealthActivityRestfulSleep);
  
  if (s_was_sleeping && !is_sleeping) {
    // User just woke up. Trigger 120-second deep scan.
    start_background_scan(120);
  }
  s_was_sleeping = is_sleeping;

  // 2. Check general HR for high stress (if awake)
  if (!is_sleeping && !s_worker_scan_timer) {
    HealthValue current_bpm = health_service_peek_current_value(HealthMetricHeartRateBPM);
    if (current_bpm > 100) {
      // Elevated baseline detected. Trigger 60-second stress verification scan.
      start_background_scan(60);
    }
  }
}

static void worker_init() {
  tick_timer_service_subscribe(MINUTE_UNIT, tick_handler);
}

static void worker_deinit() {
  tick_timer_service_unsubscribe();
}

int main(void) {
  worker_init();
  worker_event_loop();
  worker_deinit();
}