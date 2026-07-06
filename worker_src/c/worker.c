// ============================================================================
// --- SECTION 1: INCLUDES & MACROS ---
// ============================================================================
// Include the standard Pebble SDK worker library
#include <pebble_worker.h>

// ============================================================================
// --- SECTION 2: HEALTH EVENT PROCESSING ---
// ============================================================================
// Define background event handler for system health updates
static void health_handler(HealthEventType event, void *context) {
  
  // Isolate processing exclusively to heart rate update events
  if (event == HealthEventHeartRateUpdate) {
      // Query physical sensor for current BPM
      HealthValue current_bpm = health_service_peek_current_value(HealthMetricHeartRateBPM);
      
      // Execute anomaly logic if baseline threshold is exceeded
      if (current_bpm > 95) { 
        // Query system for physical movement categories
        HealthActivityMask activities = health_service_peek_current_activities();
        
        // Gatekeep alert to ensure heart rate spike is not exercise-induced
        if (!(activities & HealthActivityRun) && !(activities & HealthActivityWalk) && !(activities & HealthActivityOpenWorkout)) {
            // Command OS to bring primary application to the foreground
            worker_launch_app();
        }
      }
  }
}

// ============================================================================
// --- SECTION 3: LIFECYCLE ---
// ============================================================================
// Define worker startup routine
static void worker_init() { 
  // Bind health handler callback to OS event system
  health_service_events_subscribe(health_handler, NULL);
}

// Define worker teardown routine
static void worker_deinit() { 
  // Release OS subscription hook to free system memory
  health_service_events_unsubscribe(); 
}

// Define standard C entry point for background process
int main(void) { 
  // Initialize worker environment
  worker_init(); 
  // Block thread execution to maintain background state
  worker_event_loop(); 
  // Execute cleanup upon thread termination
  worker_deinit(); 
}