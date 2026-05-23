#include <pebble_worker.h>

static void health_handler(HealthEventType event, void *context) {
  if (event != HealthEventHeartRateUpdate) return;

  // 1-Hour Cooldown so the watch doesn't spam you during a prolonged event
  static time_t s_last_alert = 0;
  time_t now = time(NULL);
  if (now - s_last_alert < 3600) return; 

  HealthValue bpm = health_service_peek_current_value(HealthMetricHeartRateBPM);
  
  if (bpm > 100) {
    // Cross-reference with activity: Check steps in the last 5 minutes
    time_t start = now - (5 * SECONDS_PER_MINUTE);
    HealthServiceAccessibilityMask mask = health_service_metric_accessible(HealthMetricStepCount, start, now);
    
    if (mask & HealthServiceAccessibilityMaskAvailable) {
      HealthValue steps = health_service_sum(HealthMetricStepCount, start, now);
      
      // SEDENTARY STRESS DETECTED: High HR, but fewer than 50 steps recently
      if (steps < 50) {
        s_last_alert = now;
        worker_launch_app(); // Wake up the main app!
      }
    }
  }
}

int main(void) {
  health_service_events_subscribe(health_handler, NULL);
  worker_event_loop();
}