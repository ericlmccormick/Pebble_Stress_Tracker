// --- SECTION 1: INCLUDES & MACROS ---
// Include the standard Pebble Worker SDK library
#include <pebble_worker.h>

// Define the storage key used to pass the pin type to the main app
#define PERSIST_KEY_PIN_TYPE 200 
// Define the storage key used to pass the calculated stress score
#define PERSIST_KEY_SCORE 201
// Define the storage key used to pass the calculated resting heart rate
#define PERSIST_KEY_RHR 202

// --- SECTION 2: GLOBAL VARIABLES ---
// Array to hold exactly 60 seconds of heart rate data
static int s_worker_bpm_samples[60];
// Counter to track how many samples we have currently collected
static int s_worker_sample_count = 0;
// Pointer to hold the memory reference for our background 1-second timer
static AppTimer *s_worker_scan_timer = NULL;
// Variable to hold the timestamp of our last deep scan to prevent looping
static time_t s_last_scan_time = 0;

// --- SECTION 3: SCAN COMPLETION MATH & HANDOFF ---
// Function called when the 60-second deep scan reaches its target
static void worker_scan_complete() {
  // Turn off the forced 1Hz sensor mode to save battery
  health_service_set_heart_rate_sample_period(0); 
  
  // Initialize variables for our mathematical calculations
  int sum = 0;
  int count = 0;
  int min = 999;
  int max = 0;
  
  // Loop through the 60-second array to find the min, max, and sum
  for(int i = 0; i < s_worker_sample_count; i++) {
    // Only process valid readings (greater than 0)
    if(s_worker_bpm_samples[i] > 0) { 
      // Update the minimum value if the current reading is lower
      if(s_worker_bpm_samples[i] < min) min = s_worker_bpm_samples[i];
      // Update the maximum value if the current reading is higher
      if(s_worker_bpm_samples[i] > max) max = s_worker_bpm_samples[i];
      // Add the current reading to the total sum
      sum += s_worker_bpm_samples[i];
      // Increment the count of valid samples
      count++;
    }
  }
  
  // Calculate the difference between the highest and lowest heartbeats
  int range = (max == 0) ? 0 : (max - min);
  // Calculate a basic stress score out of 100 based on the variance
  int score = 100 - (range * 8); 
  
  // Clamp the lowest possible stress score to 10
  if (score < 10) score = 10;
  // Clamp the highest possible stress score to 100
  if (score > 100) score = 100;
  
  // Calculate the average heart rate over the 60 seconds
  int avg_hr = (count > 0) ? (sum / count) : 0;

  // Write the pin type (2 = Stress Event) to persistent memory
  persist_write_int(PERSIST_KEY_PIN_TYPE, 2); 
  // Write the calculated stress score to persistent memory
  persist_write_int(PERSIST_KEY_SCORE, score);
  // Write the calculated average heart rate to persistent memory
  persist_write_int(PERSIST_KEY_RHR, avg_hr);
  
  // Silently wake the main app so it can beam the data to the phone
  worker_launch_app(); 
}

// --- SECTION 4: DATA COLLECTION TICKER ---
// Function called every 1 second during an active deep scan
static void worker_sample_tick(void *data) {
  // Check if we still need to collect more samples (less than 60)
  if (s_worker_sample_count < 60) {
    // Peek at the raw optical sensor to bypass the OS smoothing
    HealthValue bpm = health_service_peek_current_value(HealthMetricHeartRateRawBPM);
    
    // Check if the sensor got a valid lock on the user's pulse
    if (bpm > 0) {
      // Save the heartbeat into our array at the current index
      s_worker_bpm_samples[s_worker_sample_count] = bpm;
      // Increment the counter for the next tick
      s_worker_sample_count++;
    }
    
    // Register the timer to call this exact function again in 1000ms (1 second)
    s_worker_scan_timer = app_timer_register(1000, worker_sample_tick, NULL);
  } else {
    // We have reached 60 samples; clear the timer pointer
    s_worker_scan_timer = NULL;
    // Trigger the math and handoff function
    worker_scan_complete();
  }
}

// --- SECTION 5: DEEP SCAN INITIALIZATION ---
// Function to prepare memory and hardware for a high-frequency scan
static void start_background_scan(int seconds) {
  // Prevent a new scan from starting if one is already running
  if (s_worker_scan_timer) return; 
  
  // Reset our sample counter back to zero
  s_worker_sample_count = 0;
  // Clear any old data out of the array memory block
  memset(s_worker_bpm_samples, 0, sizeof(s_worker_bpm_samples));
  
  // Force the Pebble optical sensor into continuous 1Hz mode
  health_service_set_heart_rate_sample_period(1); 
  // Start the 1-second collection loop immediately
  worker_sample_tick(NULL);
}

// --- SECTION 6: SYSTEM EVENT LISTENER ---
// Function triggered natively by the OS whenever it updates health data
static void health_handler(HealthEventType event, void *context) {
  // Check if the event that fired is specifically a heart rate update
  if (event == HealthEventHeartRateUpdate) {
    // Ensure we aren't already running a custom 60-second deep scan
    if (!s_worker_scan_timer) {
      // Get the current system time in seconds
      time_t now = time(NULL);
      
      // Calculate how long it has been since our last deep scan
      // If it has been less than 300 seconds (5 minutes), ignore the event
      if (now - s_last_scan_time < 300) return;

      // Peek at the smoothed system BPM value that triggered the event
      HealthValue current_bpm = health_service_peek_current_value(HealthMetricHeartRateBPM);
      
      // ** TEST THRESHOLD ** Check if it is over 40 (Change to 100 when testing is done)
      if (current_bpm > 40) {
        // Record this exact moment as the time of our latest scan
        s_last_scan_time = now;
        // Trigger the 60-second deep scan sequence
        start_background_scan(60);
      }
    }
  }
}

// --- SECTION 7: LIFECYCLE MANAGEMENT ---
// Initialization function run when the worker first boots
static void worker_init() { 
  // Subscribe to system health events instead of using a minute timer
  health_service_events_subscribe(health_handler, NULL); 
}

// De-initialization function run when the worker is killed
static void worker_deinit() { 
  // Unsubscribe from system health events to free up OS memory
  health_service_events_unsubscribe(); 
}

// Main function that serves as the entry point for the C program
int main(void) { 
  // Run the initialization routine
  worker_init(); 
  // Enter the endless event loop to keep the background process alive
  worker_event_loop(); 
  // Run the cleanup routine when the loop is finally broken
  worker_deinit(); 
}