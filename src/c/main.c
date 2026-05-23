#include <pebble.h>
#include <math.h>

#define DATA_COUNT 48 
#define SAMPLE_COUNT 60
#define BREATHE_TOTAL_SECONDS 300 
#define PERSIST_KEY_HISTORY 100
#define PERSIST_KEY_VOLUME 101 

static Window *s_window_history, *s_window_manual, *s_window_breathe;
static Layer *s_graph_layer, *s_circle_layer, *s_pulse_layer;

static TextLayer *s_manual_text_layer, *s_breathe_text_layer, *s_breathe_timer_layer, *s_breathe_session_timer;
static TextLayer *s_hist_up_label, *s_hist_down_label, *s_man_up_label, *s_man_down_label, *s_breathe_up_label, *s_breathe_down_label;

static int s_graph_data[DATA_COUNT];
static int s_bpm_samples[SAMPLE_COUNT];

static int s_manual_time_left = 60;
static int s_breathe_time = 0;
static int s_session_time_left = BREATHE_TOTAL_SECONDS;
static int s_breathe_phase = 0; 
static int s_volume = 100; 
static bool s_breathe_active = false;
static bool s_launched_by_worker = false;
static AppTimer *s_timer_handle = NULL;
static AppTimer *s_ekg_timer = NULL;
static uint8_t s_stream_chunk[4000];

// Animation Variables
static Animation *s_radius_animation = NULL;
static int s_radius = 0; // Starts invisible
static int s_radius_from = 0;
static int s_radius_to = 0;
static int s_current_bpm = 0;
static int s_ekg_offset = 0; // Tracks the scrolling distance of the EKG

// --- 1. UTILITIES ---
static TextLayer* create_text(GRect b, const char* t, GTextAlignment align, const char* font) {
  TextLayer *tl = text_layer_create(b);
  text_layer_set_text(tl, t);
  text_layer_set_text_alignment(tl, align);
  text_layer_set_font(tl, fonts_get_system_font(font));
  text_layer_set_background_color(tl, GColorClear);
  return tl;
}

static void play_voice_timed(uint32_t res_id) {
  if (s_volume == 0) { return; }

  ResHandle handle = resource_get_handle(res_id);
  if (!handle) { return; }
  size_t size = resource_size(handle), read = 0;
  
  speaker_stream_open(SpeakerPcmFormat_16kHz_16bit, s_volume);
  
  while (read < size && s_breathe_active) {
    size_t chunk = (size - read > sizeof(s_stream_chunk)) ? sizeof(s_stream_chunk) : size - read;
    resource_load_byte_range(handle, read, s_stream_chunk, chunk);
    
    int16_t *samples = (int16_t *)s_stream_chunk;
    size_t num_samples = chunk / 2;
    for(size_t i = 0; i < num_samples; i++) {
        samples[i] = (int16_t)(((int32_t)samples[i] * s_volume) / 100);
    }

    speaker_stream_write(s_stream_chunk, chunk);
    read += chunk;
    psleep(125); 
  }
  speaker_stream_close();
}

static int calculate_bpm_range(int *samples, int count) {
  int min = 999, max = 0;
  for(int i = 0; i < count; i++) {
    if(samples[i] > 0) { 
      if(samples[i] < min) { min = samples[i]; }
      if(samples[i] > max) { max = samples[i]; }
    }
  }
  return (max == 0) ? 0 : (max - min);
}

// --- 2. SMOOTH NATIVE ANIMATION ENGINE ---
static void radius_anim_update(Animation *anim, const AnimationProgress progress) {
  s_radius = s_radius_from + ((s_radius_to - s_radius_from) * (int)progress) / ANIMATION_NORMALIZED_MAX;
  layer_mark_dirty(s_circle_layer);
}

static const AnimationImplementation s_radius_anim_impl = {
  .update = radius_anim_update
};

static void anim_stopped_handler(Animation *animation, bool finished, void *context) {
  s_radius_animation = NULL; 
}

static void start_radius_animation(int from, int to, int duration) {
  if (s_radius_animation) {
    animation_unschedule(s_radius_animation); 
  }
  s_radius_from = from;
  s_radius_to = to;
  s_radius_animation = animation_create();
  animation_set_duration(s_radius_animation, duration);
  animation_set_curve(s_radius_animation, AnimationCurveEaseInOut);
  animation_set_implementation(s_radius_animation, &s_radius_anim_impl);
  animation_set_handlers(s_radius_animation, (AnimationHandlers) {
    .stopped = anim_stopped_handler
  }, NULL);
  animation_schedule(s_radius_animation);
}

// --- 3. SCANNING & LOGIC ---
static void ekg_timer_callback(void *data) {
  // Drives the 20FPS smooth scrolling of the EKG
  if (s_current_bpm > 0 && s_manual_time_left > 0) {
    s_ekg_offset += 6; 
    layer_mark_dirty(s_pulse_layer);
    s_ekg_timer = app_timer_register(50, ekg_timer_callback, NULL);
  } else {
    s_ekg_timer = NULL;
  }
}

static void manual_timer_callback(void *data) {
  s_timer_handle = NULL; 

  if (s_manual_time_left > 0) {
    HealthValue bpm = health_service_peek_current_value(HealthMetricHeartRateRawBPM);
    if (bpm > 0) {
      s_bpm_samples[60 - s_manual_time_left] = bpm;
      s_current_bpm = bpm;
      
      // Start the scrolling animation if it isn't already running
      if (!s_ekg_timer) {
        s_ekg_timer = app_timer_register(50, ekg_timer_callback, NULL);
      }
    }
    s_manual_time_left--;
    static char buf[64];
    snprintf(buf, sizeof(buf), "Scanning...\n%ds left", s_manual_time_left);
    text_layer_set_text(s_manual_text_layer, buf);
    s_timer_handle = app_timer_register(1000, manual_timer_callback, NULL);
  } else {
    health_service_set_heart_rate_sample_period(0); 
    HealthValue bpm = health_service_peek_current_value(HealthMetricHeartRateBPM);
    int hr_range = calculate_bpm_range(s_bpm_samples, SAMPLE_COUNT);
    if (bpm == 0 && hr_range > 0) { bpm = s_bpm_samples[59]; }
    
    // Stop the scrolling animation
    if (s_ekg_timer) { app_timer_cancel(s_ekg_timer); s_ekg_timer = NULL; }
    s_current_bpm = 0;
    s_ekg_offset = 0;
    layer_mark_dirty(s_pulse_layer);

    static char res[128];
    if (bpm > 0) {
      int stress = 100 - (hr_range * 8);
      if (stress < 10) { stress = 10; }
      if (stress > 100) { stress = 100; }
      
      if (s_launched_by_worker && stress <= 75) {
        window_stack_pop_all(true);
        return;
      }

      const char *insight = (stress < 40) ? "Relaxed" : (stress <= 75) ? "Balanced" : "STRESSED\nBreathe [UP]";
      snprintf(res, sizeof(res), "%ld BPM\nStress: %d/100\n%s", (long)bpm, stress, insight);
    } else {
      snprintf(res, sizeof(res), "No Sensor Lock");
    }
    text_layer_set_text(s_manual_text_layer, res);
    vibes_double_pulse();
    s_manual_time_left = 60; 
  }
}

static void auto_start_scan() {
  if (s_manual_time_left == 60) {
    vibes_long_pulse(); 
    memset(s_bpm_samples, 0, sizeof(s_bpm_samples));
    health_service_set_heart_rate_sample_period(1); 
    manual_timer_callback(NULL);
  }
}

static void breathe_timer_callback(void *data) {
  s_timer_handle = NULL; 
  if (!s_breathe_active) { return; }
  
  if (s_session_time_left > 0) {
    if (s_breathe_time % 4 == 0) {
      s_breathe_phase = (s_breathe_phase + 1) % 4;
      
      if (s_volume == 0) {
        vibes_short_pulse(); 
      }

      switch(s_breathe_phase) {
        case 0: 
          text_layer_set_text(s_breathe_text_layer, "INHALE"); 
          start_radius_animation(10, 50, 4000); 
          play_voice_timed(RESOURCE_ID_inhale); 
          break;
        case 1: 
          text_layer_set_text(s_breathe_text_layer, "HOLD"); 
          play_voice_timed(RESOURCE_ID_hold); 
          break;
        case 2: 
          text_layer_set_text(s_breathe_text_layer, "EXHALE"); 
          start_radius_animation(50, 10, 4000); 
          play_voice_timed(RESOURCE_ID_exhale); 
          break;
        case 3: 
          text_layer_set_text(s_breathe_text_layer, "HOLD"); 
          play_voice_timed(RESOURCE_ID_hold); 
          break;
      }
    }
    
    static char main_buf[4], sess_buf[32];
    snprintf(main_buf, sizeof(main_buf), "%d", 4 - (s_breathe_time % 4));
    text_layer_set_text(s_breathe_timer_layer, main_buf);
    
    snprintf(sess_buf, sizeof(sess_buf), "Session: %dm %ds", s_session_time_left / 60, s_session_time_left % 60);
    text_layer_set_text(s_breathe_session_timer, sess_buf);

    s_breathe_time++;
    s_session_time_left--;
    s_timer_handle = app_timer_register(1000, breathe_timer_callback, NULL);
  } else {
    // Reset layout on complete
    layer_set_frame(text_layer_get_layer(s_breathe_text_layer), GRect(0, 60, 200, 35));
    s_radius = 0;
    layer_mark_dirty(s_circle_layer);
    text_layer_set_text(s_breathe_text_layer, "COMPLETE");
    text_layer_set_text(s_breathe_up_label, "Graph ->");
    text_layer_set_text(s_breathe_down_label, "Measure ->");
    vibes_long_pulse();
    s_breathe_active = false;
  }
}

// --- 4. UI DRAWING PROCEDURES ---
static void graph_update_proc(Layer *l, GContext *ctx) {
  int graph_h = 130, margin = 4;
  time_t now = time(NULL);
  struct tm *tm = localtime(&now);
  graphics_context_set_text_color(ctx, GColorBlack); 
  for(int i = 0; i < DATA_COUNT; i++) {
    int mins_ago = (47 - i) * 5;
    int total_m = (tm->tm_hour * 60 + tm->tm_min) - mins_ago;
    if (total_m < 0) { total_m += 1440; }
    if ((total_m % 60) < 5) {
      int x = margin + (i * 4);
      graphics_context_set_stroke_color(ctx, GColorDarkGray);
      graphics_draw_line(ctx, GPoint(x, 0), GPoint(x, graph_h));
      int h = (total_m / 60) % 12; 
      if (h == 0) { h = 12; }
      char lbl[8]; snprintf(lbl, sizeof(lbl), "%d%s", h, (total_m/60 >= 12) ? "p" : "a");
      graphics_draw_text(ctx, lbl, fonts_get_system_font(FONT_KEY_GOTHIC_14), GRect(x-15, graph_h, 30, 20), GTextOverflowModeFill, GTextAlignmentCenter, NULL);
    }
  }
  for (int i = 0; i < DATA_COUNT; i++) {
    int bpm = s_graph_data[i]; 
    if (bpm <= 0) { continue; }
    int h = (bpm * graph_h) / 180; 
    if (h > graph_h) { h = graph_h; }
    GColor c = (bpm > 105) ? GColorRed : (bpm > 85) ? GColorChromeYellow : (bpm >= 60) ? GColorJaegerGreen : GColorPictonBlue;
    graphics_context_set_fill_color(ctx, c);
    graphics_fill_rect(ctx, GRect(margin + (i*4), graph_h - h, 4, h), 0, GCornerNone);
  }
}

static void circle_update_proc(Layer *l, GContext *ctx) {
  if (s_radius > 0) {
    graphics_context_set_fill_color(ctx, GColorPictonBlue);
    graphics_fill_circle(ctx, GPoint(100, 50), s_radius);
  }
}

static void pulse_update_proc(Layer *l, GContext *ctx) {
  if (s_current_bpm > 0) {
    graphics_context_set_stroke_color(ctx, GColorRed);
    graphics_context_set_stroke_width(ctx, 3);
    
    int y_base = 35; 
    
    int amp = 10 + ((s_current_bpm - 50) * 20) / 100;
    if (amp < 10) amp = 10;
    if (amp > 30) amp = 30; 
    
    int space = 7200 / s_current_bpm; 
    if (space < 35) { space = 35; } 
    
    // Shift starting coordinate leftwards based on the animation offset
    int start_x = -(s_ekg_offset % space); 
    int x = start_x;
    
    while (x < 200) {
        int next_spike = x + (space - 30);
        
        if (next_spike > x) {
            graphics_draw_line(ctx, GPoint(x, y_base), GPoint(next_spike, y_base));
        }
        
        if (next_spike < 200) {
            graphics_draw_line(ctx, GPoint(next_spike, y_base), GPoint(next_spike+5, y_base + (amp/3))); 
            graphics_draw_line(ctx, GPoint(next_spike+5, y_base + (amp/3)), GPoint(next_spike+15, y_base - amp)); 
            graphics_draw_line(ctx, GPoint(next_spike+15, y_base - amp), GPoint(next_spike+25, y_base + (amp/2))); 
            graphics_draw_line(ctx, GPoint(next_spike+25, y_base + (amp/2)), GPoint(next_spike+30, y_base)); 
        }
        x += space;
    }
  }
}

// --- 5. LOADERS & UNLOADERS ---
static void sync_history_from_os() {
  time_t end = time(NULL), start = end - (240 * 60); 
  HealthMinuteData *md = malloc(240 * sizeof(HealthMinuteData));
  if (!md) { return; }
  uint32_t r = health_service_get_minute_history(md, 240, &start, &end);
  for(int i=0; i<DATA_COUNT; i++) {
    int sum=0, cnt=0;
    for(int j=0; j<5; j++) {
      int idx = (i*5)+j;
      if(idx < (int)r && !md[idx].is_invalid && md[idx].heart_rate_bpm > 0) { 
        sum += md[idx].heart_rate_bpm; 
        cnt++; 
      }
    }
    s_graph_data[i] = (cnt > 0) ? (sum / cnt) : 0;
  }
  free(md);
}

static void history_load(Window *w) {
  Layer *root = window_get_root_layer(w);
  s_hist_up_label = create_text(GRect(0, 10, 195, 20), "Measure ->", GTextAlignmentRight, FONT_KEY_GOTHIC_18_BOLD);
  s_hist_down_label = create_text(GRect(0, 200, 195, 20), "Breathe ->", GTextAlignmentRight, FONT_KEY_GOTHIC_18_BOLD);
  s_graph_layer = layer_create(GRect(0, 40, 200, 150));
  layer_set_update_proc(s_graph_layer, graph_update_proc);
  layer_add_child(root, text_layer_get_layer(s_hist_up_label));
  layer_add_child(root, s_graph_layer);
  layer_add_child(root, text_layer_get_layer(s_hist_down_label));
  sync_history_from_os();
}

static void history_unload(Window *w) {
  text_layer_destroy(s_hist_up_label);
  text_layer_destroy(s_hist_down_label);
  layer_destroy(s_graph_layer);
}

static void manual_load(Window *w) {
  Layer *root = window_get_root_layer(w);
  s_manual_text_layer = create_text(GRect(0, 30, 200, 100), "Press SELECT\nto Scan", GTextAlignmentCenter, FONT_KEY_GOTHIC_28_BOLD);
  s_man_up_label = create_text(GRect(0, 10, 195, 20), "Breathe ->", GTextAlignmentRight, FONT_KEY_GOTHIC_18_BOLD);
  s_man_down_label = create_text(GRect(0, 200, 195, 20), "Graph ->", GTextAlignmentRight, FONT_KEY_GOTHIC_18_BOLD);
  
  s_pulse_layer = layer_create(GRect(0, 140, 200, 60));
  layer_set_update_proc(s_pulse_layer, pulse_update_proc);

  layer_add_child(root, text_layer_get_layer(s_manual_text_layer));
  layer_add_child(root, text_layer_get_layer(s_man_up_label));
  layer_add_child(root, text_layer_get_layer(s_man_down_label));
  layer_add_child(root, s_pulse_layer);
}

static void manual_unload(Window *w) {
  if (s_timer_handle) { app_timer_cancel(s_timer_handle); s_timer_handle = NULL; }
  if (s_ekg_timer) { app_timer_cancel(s_ekg_timer); s_ekg_timer = NULL; }
  health_service_set_heart_rate_sample_period(0); 
  s_manual_time_left = 60;
  s_ekg_offset = 0;
  text_layer_destroy(s_manual_text_layer);
  text_layer_destroy(s_man_up_label);
  text_layer_destroy(s_man_down_label);
  layer_destroy(s_pulse_layer);
}

static void breathe_load(Window *w) {
  Layer *root = window_get_root_layer(w);
  
  // Starts centered
  s_breathe_text_layer = create_text(GRect(0, 60, 200, 35), "Press SELECT", GTextAlignmentCenter, FONT_KEY_GOTHIC_28_BOLD);
  
  s_breathe_timer_layer = create_text(GRect(0, 140, 200, 45), "", GTextAlignmentCenter, FONT_KEY_LECO_42_NUMBERS);
  s_breathe_session_timer = create_text(GRect(0, 185, 200, 20), "Goal: 5 Minutes", GTextAlignmentCenter, FONT_KEY_GOTHIC_18);
  s_breathe_up_label = create_text(GRect(0, 10, 195, 20), "Graph ->", GTextAlignmentRight, FONT_KEY_GOTHIC_18_BOLD);
  s_breathe_down_label = create_text(GRect(0, 200, 195, 20), "Measure ->", GTextAlignmentRight, FONT_KEY_GOTHIC_18_BOLD);
  
  s_circle_layer = layer_create(GRect(0, 40, 200, 100));
  s_radius = 0; // Ensures it's invisible before SELECT
  layer_set_update_proc(s_circle_layer, circle_update_proc);
  
  layer_add_child(root, text_layer_get_layer(s_breathe_text_layer));
  layer_add_child(root, text_layer_get_layer(s_breathe_timer_layer));
  layer_add_child(root, text_layer_get_layer(s_breathe_session_timer));
  layer_add_child(root, text_layer_get_layer(s_breathe_up_label));
  layer_add_child(root, text_layer_get_layer(s_breathe_down_label));
  layer_add_child(root, s_circle_layer);
}

static void breathe_unload(Window *w) {
  s_breathe_active = false; 
  if (s_timer_handle) { app_timer_cancel(s_timer_handle); s_timer_handle = NULL; }
  if (s_radius_animation) { animation_unschedule(s_radius_animation); }
  s_breathe_time = 0; s_session_time_left = BREATHE_TOTAL_SECONDS; s_radius = 0;
  text_layer_destroy(s_breathe_text_layer);
  text_layer_destroy(s_breathe_timer_layer);
  text_layer_destroy(s_breathe_session_timer);
  text_layer_destroy(s_breathe_up_label);
  text_layer_destroy(s_breathe_down_label);
  layer_destroy(s_circle_layer);
}

// --- 6. CLEAN CAROUSEL NAVIGATION ---
static void history_up_click(ClickRecognizerRef r, void *c) { 
  window_stack_push(s_window_manual, true); 
}

static void history_down_click(ClickRecognizerRef r, void *c) { 
  window_stack_push(s_window_breathe, true); 
}

static void manual_up_click(ClickRecognizerRef r, void *c) { 
  window_stack_pop(false); 
  window_stack_push(s_window_breathe, true); 
}

static void manual_down_click(ClickRecognizerRef r, void *c) { 
  window_stack_pop(true); 
}

static void breathe_up_click(ClickRecognizerRef r, void *c) {
  if (s_breathe_active) {
    s_volume += 10; if (s_volume > 100) { s_volume = 100; }
    persist_write_int(PERSIST_KEY_VOLUME, s_volume); 
    static char vbuf[16]; snprintf(vbuf, sizeof(vbuf), "Vol: %d%%", s_volume);
    text_layer_set_text(s_breathe_session_timer, vbuf); 
    vibes_short_pulse(); 
  } else {
    window_stack_pop(true); 
  }
}

static void breathe_down_click(ClickRecognizerRef r, void *c) {
  if (s_breathe_active) {
    s_volume -= 10; if (s_volume < 0) { s_volume = 0; }
    persist_write_int(PERSIST_KEY_VOLUME, s_volume); 
    static char vbuf[16]; snprintf(vbuf, sizeof(vbuf), "Vol: %d%%", s_volume);
    text_layer_set_text(s_breathe_session_timer, vbuf); 
    vibes_short_pulse(); 
  } else {
    window_stack_pop(false); 
    window_stack_push(s_window_manual, true); 
  }
}

static void manual_select(ClickRecognizerRef r, void *c) {
  if (s_manual_time_left == 60) {
    memset(s_bpm_samples, 0, sizeof(s_bpm_samples));
    health_service_set_heart_rate_sample_period(1); 
    manual_timer_callback(NULL);
  }
}

static void breathe_select(ClickRecognizerRef r, void *c) {
  if (s_breathe_time == 0) { 
    s_breathe_active = true; s_breathe_phase = -1; 
    // Pop text to the top to make room for animation
    layer_set_frame(text_layer_get_layer(s_breathe_text_layer), GRect(0, 5, 200, 35));
    text_layer_set_text(s_breathe_up_label, "Vol +");
    text_layer_set_text(s_breathe_down_label, "Vol -");
    breathe_timer_callback(NULL); 
  } else if (s_breathe_active) { 
    s_breathe_active = false;
    if (s_timer_handle) { app_timer_cancel(s_timer_handle); s_timer_handle = NULL; }
    if (s_radius_animation) { animation_unschedule(s_radius_animation); }
    // Return text to center and clear dot
    layer_set_frame(text_layer_get_layer(s_breathe_text_layer), GRect(0, 60, 200, 35));
    s_radius = 0;
    layer_mark_dirty(s_circle_layer);
    
    text_layer_set_text(s_breathe_text_layer, "PAUSED");
    text_layer_set_text(s_breathe_up_label, "Graph ->");
    text_layer_set_text(s_breathe_down_label, "Measure ->");
  } else { 
    s_breathe_active = true;
    // Pop text back to the top to resume
    layer_set_frame(text_layer_get_layer(s_breathe_text_layer), GRect(0, 5, 200, 35));
    text_layer_set_text(s_breathe_up_label, "Vol +");
    text_layer_set_text(s_breathe_down_label, "Vol -");
    breathe_timer_callback(NULL);
  }
}

// --- 7. CONFIGURATORS ---
static void history_config(void *c) {
  window_single_click_subscribe(BUTTON_ID_UP, history_up_click);
  window_single_click_subscribe(BUTTON_ID_DOWN, history_down_click);
}
static void manual_config(void *c) { 
  window_single_click_subscribe(BUTTON_ID_SELECT, manual_select); 
  window_single_click_subscribe(BUTTON_ID_UP, manual_up_click);
  window_single_click_subscribe(BUTTON_ID_DOWN, manual_down_click); 
}
static void breathe_config(void *c) { 
  window_single_click_subscribe(BUTTON_ID_SELECT, breathe_select); 
  window_single_click_subscribe(BUTTON_ID_UP, breathe_up_click); 
  window_single_click_subscribe(BUTTON_ID_DOWN, breathe_down_click);
}

// --- 8. INIT & DEINIT ---
static void init() {
  if (persist_exists(PERSIST_KEY_VOLUME)) {
    s_volume = persist_read_int(PERSIST_KEY_VOLUME);
  } else {
    s_volume = 100;
  }
  
  if (persist_get_size(PERSIST_KEY_HISTORY) != sizeof(s_graph_data)) {
    persist_delete(PERSIST_KEY_HISTORY);
  }

  s_window_history = window_create();
  window_set_click_config_provider(s_window_history, history_config);
  window_set_window_handlers(s_window_history, (WindowHandlers){ .load = history_load, .unload = history_unload });
  
  s_window_manual = window_create();
  window_set_click_config_provider(s_window_manual, manual_config);
  window_set_window_handlers(s_window_manual, (WindowHandlers){ .load = manual_load, .unload = manual_unload });
  
  s_window_breathe = window_create();
  window_set_click_config_provider(s_window_breathe, breathe_config);
  window_set_window_handlers(s_window_breathe, (WindowHandlers){ .load = breathe_load, .unload = breathe_unload });

  if (launch_reason() == APP_LAUNCH_WORKER) {
    s_launched_by_worker = true;
    window_stack_push(s_window_history, false); 
    window_stack_push(s_window_manual, true);   
    app_timer_register(500, (AppTimerCallback)auto_start_scan, NULL); 
  } else {
    window_stack_push(s_window_history, true);
  }
  
  app_worker_launch();
}

static void deinit() {
  window_destroy(s_window_history);
  window_destroy(s_window_manual);
  window_destroy(s_window_breathe);
}

int main(void) { init(); app_event_loop(); deinit(); }
