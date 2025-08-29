#include <pebble.h>

// ===== UI/state =====
static Window *s_main_window;
static TextLayer *s_text_layer;
static AppTimer *s_retry_timer;

static int32_t s_day_offset = 0;      // currently displayed offset (0=today)
static int32_t s_cache_center = 9999; // center offset of the cached bundle

typedef struct {
  bool valid;
  int32_t offset;
  char date[20];   // e.g., "Wed Sep 03"
  char dawn[6];    // "HH:MM"
  char sunrise[6];
  char sunset[6];
  char dusk[6];
} DayTimes;

static DayTimes s_cache[3]; // 0: center-1, 1: center, 2: center+1

// ===== Utils =====
static const char *app_msg_reason_str(AppMessageResult r) {
  switch (r) {
    case APP_MSG_OK: return "OK";
    case APP_MSG_SEND_TIMEOUT: return "TIMEOUT";
    case APP_MSG_SEND_REJECTED: return "REJECTED";
    case APP_MSG_NOT_CONNECTED: return "NOT_CONNECTED";
    case APP_MSG_APP_NOT_RUNNING: return "APP_NOT_RUNNING";
    case APP_MSG_INVALID_ARGS: return "INVALID_ARGS";
    case APP_MSG_BUSY: return "BUSY";
    case APP_MSG_BUFFER_OVERFLOW: return "BUF_OVERFLOW";
    case APP_MSG_ALREADY_RELEASED: return "ALREADY_RELEASED";
    case APP_MSG_CALLBACK_ALREADY_REGISTERED: return "CB_ALREADY";
    case APP_MSG_CALLBACK_NOT_REGISTERED: return "CB_NOT_REG";
    case APP_MSG_OUT_OF_MEMORY: return "OOM";
    case APP_MSG_CLOSED: return "CLOSED";
    case APP_MSG_INTERNAL_ERROR: return "INTERNAL";
    default: return "UNKNOWN";
  }
  return "UNKNOWN";
}

static void clear_cache(void) {
  for (int i = 0; i < 3; i++) {
    s_cache[i].valid = false;
    s_cache[i].offset = 0;
    s_cache[i].date[0] = 0;
    s_cache[i].dawn[0] = 0;
    s_cache[i].sunrise[0] = 0;
    s_cache[i].sunset[0] = 0;
    s_cache[i].dusk[0] = 0;
  }
  s_cache_center = 9999;
}

static DayTimes *cache_get_for_offset(int32_t offset) {
  if (s_cache_center == 9999) return NULL;
  if (offset == s_cache_center - 1) return &s_cache[0];
  if (offset == s_cache_center)     return &s_cache[1];
  if (offset == s_cache_center + 1) return &s_cache[2];
  return NULL;
}

static void show_daytimes(const DayTimes *dt) {
  if (!dt || !dt->valid) {
    text_layer_set_text(s_text_layer, "Fetching…");
    return;
  }
  static char s_buffer[160];
  snprintf(s_buffer, sizeof(s_buffer),
           "%s\nDawn   %s\nSunrise %s\nSunset  %s\nDusk    %s",
           dt->date[0] ? dt->date : "",
           dt->dawn[0]    ? dt->dawn    : "--:--",
           dt->sunrise[0] ? dt->sunrise : "--:--",
           dt->sunset[0]  ? dt->sunset  : "--:--",
           dt->dusk[0]    ? dt->dusk    : "--:--");
  text_layer_set_text(s_text_layer, s_buffer);
}

// ===== Messaging =====
static void request_times(void);

static void retry_cb(void *data) {
  request_times();
}

static void request_times(void) {
  DictionaryIterator *iter;
  AppMessageResult r = app_message_outbox_begin(&iter);
  if (r != APP_MSG_OK) {
    static char s_buf[64];
    snprintf(s_buf, sizeof(s_buf), "Outbox: %s", app_msg_reason_str(r));
    text_layer_set_text(s_text_layer, s_buf);
    s_retry_timer = app_timer_register(2000, retry_cb, NULL);
    return;
  }
  int32_t one = 1;
  dict_write_int(iter, MESSAGE_KEY_REQ, &one, sizeof(one), true);
  dict_write_int(iter, MESSAGE_KEY_OFFSET, &s_day_offset, sizeof(s_day_offset), true);
  dict_write_end(iter);
  app_message_outbox_send();

  // If we don’t already have this offset cached, hint loading
  if (!cache_get_for_offset(s_day_offset)) {
    text_layer_set_text(s_text_layer, "Fetching…");
  }
}

static void inbox_received_callback(DictionaryIterator *iter, void *context) {
  Tuple *hello_t = dict_find(iter, MESSAGE_KEY_HELLO);
  Tuple *error_t = dict_find(iter, MESSAGE_KEY_ERROR);

  if (hello_t) {
    text_layer_set_text(s_text_layer, "Connected. Fetching…");
    clear_cache();
    request_times();
    return;
  }

  if (error_t) {
    static char s_buf[64];
    snprintf(s_buf, sizeof(s_buf), "Error: %s", error_t->value->cstring);
    text_layer_set_text(s_text_layer, s_buf);
    return;
  }

  // We expect a bundle: CENTER plus M1, 0, P1 fields
  Tuple *center_t = dict_find(iter, MESSAGE_KEY_CENTER);
  if (!center_t) {
    // Fallback: support legacy single-day payloads (DATE/DAWN/SUNRISE/SUNSET/DUSK)
    Tuple *date_t    = dict_find(iter, MESSAGE_KEY_DATE);
    Tuple *dawn_t    = dict_find(iter, MESSAGE_KEY_DAWN);
    Tuple *sunrise_t = dict_find(iter, MESSAGE_KEY_SUNRISE);
    Tuple *sunset_t  = dict_find(iter, MESSAGE_KEY_SUNSET);
    Tuple *dusk_t    = dict_find(iter, MESSAGE_KEY_DUSK);
    if (date_t || dawn_t || sunrise_t || sunset_t || dusk_t) {
      DayTimes tmp = {
        .valid = true,
        .offset = s_day_offset
      };
      snprintf(tmp.date, sizeof(tmp.date), "%s", date_t ? date_t->value->cstring : "");
      snprintf(tmp.dawn, sizeof(tmp.dawn), "%s", dawn_t ? dawn_t->value->cstring : "");
      snprintf(tmp.sunrise, sizeof(tmp.sunrise), "%s", sunrise_t ? sunrise_t->value->cstring : "");
      snprintf(tmp.sunset, sizeof(tmp.sunset), "%s", sunset_t ? sunset_t->value->cstring : "");
      snprintf(tmp.dusk, sizeof(tmp.dusk), "%s", dusk_t ? dusk_t->value->cstring : "");
      show_daytimes(&tmp);
    }
    return;
  }

  int32_t center = center_t->value->int32;
  s_cache_center = center;

  // Fill the three cache slots
  // Center-1
  Tuple *date_m1    = dict_find(iter, MESSAGE_KEY_DATE_M1);
  Tuple *dawn_m1    = dict_find(iter, MESSAGE_KEY_DAWN_M1);
  Tuple *sunrise_m1 = dict_find(iter, MESSAGE_KEY_SUNRISE_M1);
  Tuple *sunset_m1  = dict_find(iter, MESSAGE_KEY_SUNSET_M1);
  Tuple *dusk_m1    = dict_find(iter, MESSAGE_KEY_DUSK_M1);

  // Center
  Tuple *date_0    = dict_find(iter, MESSAGE_KEY_DATE_0);
  Tuple *dawn_0    = dict_find(iter, MESSAGE_KEY_DAWN_0);
  Tuple *sunrise_0 = dict_find(iter, MESSAGE_KEY_SUNRISE_0);
  Tuple *sunset_0  = dict_find(iter, MESSAGE_KEY_SUNSET_0);
  Tuple *dusk_0    = dict_find(iter, MESSAGE_KEY_DUSK_0);

  // Center+1
  Tuple *date_p1    = dict_find(iter, MESSAGE_KEY_DATE_P1);
  Tuple *dawn_p1    = dict_find(iter, MESSAGE_KEY_DAWN_P1);
  Tuple *sunrise_p1 = dict_find(iter, MESSAGE_KEY_SUNRISE_P1);
  Tuple *sunset_p1  = dict_find(iter, MESSAGE_KEY_SUNSET_P1);
  Tuple *dusk_p1    = dict_find(iter, MESSAGE_KEY_DUSK_P1);

  // Helper to populate one slot
  #define FILL_SLOT(slot, off, t_date, t_dawn, t_sunrise, t_sunset, t_dusk) \
    do { \
      DayTimes *dt = &s_cache[slot]; \
      dt->valid = (t_dawn || t_sunrise || t_sunset || t_dusk || t_date); \
      dt->offset = (off); \
      snprintf(dt->date, sizeof(dt->date), "%s", (t_date && t_date->value) ? t_date->value->cstring : ""); \
      snprintf(dt->dawn, sizeof(dt->dawn), "%s", (t_dawn && t_dawn->value) ? t_dawn->value->cstring : ""); \
      snprintf(dt->sunrise, sizeof(dt->sunrise), "%s", (t_sunrise && t_sunrise->value) ? t_sunrise->value->cstring : ""); \
      snprintf(dt->sunset, sizeof(dt->sunset), "%s", (t_sunset && t_sunset->value) ? t_sunset->value->cstring : ""); \
      snprintf(dt->dusk, sizeof(dt->dusk), "%s", (t_dusk && t_dusk->value) ? t_dusk->value->cstring : ""); \
    } while (0)

  FILL_SLOT(0, center - 1, date_m1, dawn_m1, sunrise_m1, sunset_m1, dusk_m1);
  FILL_SLOT(1, center,     date_0,  dawn_0,  sunrise_0,  sunset_0,  dusk_0);
  FILL_SLOT(2, center + 1, date_p1, dawn_p1, sunrise_p1, sunset_p1, dusk_p1);
  #undef FILL_SLOT

  // If the currently displayed offset is now cached, update immediately
  DayTimes *dt = cache_get_for_offset(s_day_offset);
  if (dt && dt->valid) {
    show_daytimes(dt);
  }
}

static void inbox_dropped_callback(AppMessageResult reason, void *context) {
  static char s_buf[40];
  snprintf(s_buf, sizeof(s_buf), "Inbox dropped: %s", app_msg_reason_str(reason));
  text_layer_set_text(s_text_layer, s_buf);
}

static void outbox_failed_callback(DictionaryIterator *iter, AppMessageResult reason, void *context) {
  static char s_buf[48];
  snprintf(s_buf, sizeof(s_buf), "Send failed: %s", app_msg_reason_str(reason));
  text_layer_set_text(s_text_layer, s_buf);
  s_retry_timer = app_timer_register(1200, retry_cb, NULL);
}

static void outbox_sent_callback(DictionaryIterator *iter, void *context) {
  // No-op
}

static void phone_conn_handler(bool connected) {
  if (connected) {
    text_layer_set_text(s_text_layer, "Connecting…");
    s_retry_timer = app_timer_register(400, retry_cb, NULL);
  } else {
    text_layer_set_text(s_text_layer, "Waiting for phone…");
  }
}

// ===== Buttons: instant show from cache, then prefetch neighbors =====
static void navigate_to_offset(int32_t new_offset) {
  s_day_offset = new_offset;

  DayTimes *dt = cache_get_for_offset(s_day_offset);
  if (dt && dt->valid) {
    show_daytimes(dt);  // instant
  } else {
    text_layer_set_text(s_text_layer, "Fetching…");
  }

  // Request a new bundle centered on the selected day
  request_times();
}

static void up_click_handler(ClickRecognizerRef recognizer, void *ctx) {
  navigate_to_offset(s_day_offset - 1);
}
static void down_click_handler(ClickRecognizerRef recognizer, void *ctx) {
  navigate_to_offset(s_day_offset + 1);
}
static void select_click_handler(ClickRecognizerRef recognizer, void *ctx) {
  navigate_to_offset(0);
}

static void click_config_provider(void *ctx) {
  window_single_click_subscribe(BUTTON_ID_UP, up_click_handler);
  window_single_click_subscribe(BUTTON_ID_DOWN, down_click_handler);
  window_single_click_subscribe(BUTTON_ID_SELECT, select_click_handler);

  // Optional: hold to scroll faster
  window_single_repeating_click_subscribe(BUTTON_ID_UP, 180, up_click_handler);
  window_single_repeating_click_subscribe(BUTTON_ID_DOWN, 180, down_click_handler);
}

// ===== Window lifecycle =====
static void main_window_load(Window *window) {
  Layer *window_layer = window_get_root_layer(window);
  GRect bounds = layer_get_bounds(window_layer);

  s_text_layer = text_layer_create(bounds);
  text_layer_set_text(s_text_layer, "Loading…");
  text_layer_set_text_alignment(s_text_layer,
    PBL_IF_ROUND_ELSE(GTextAlignmentCenter, GTextAlignmentLeft));
  text_layer_set_overflow_mode(s_text_layer, GTextOverflowModeWordWrap);
  text_layer_set_font(s_text_layer, fonts_get_system_font(FONT_KEY_GOTHIC_24));
  layer_add_child(window_layer, text_layer_get_layer(s_text_layer));
}

static void main_window_unload(Window *window) {
  if (s_retry_timer) {
    app_timer_cancel(s_retry_timer);
    s_retry_timer = NULL;
  }
  text_layer_destroy(s_text_layer);
  s_text_layer = NULL;
}

static void init(void) {
  clear_cache();

  s_main_window = window_create();
  window_set_window_handlers(s_main_window, (WindowHandlers) {
    .load = main_window_load,
    .unload = main_window_unload
  });
  window_set_click_config_provider(s_main_window, click_config_provider);
  window_stack_push(s_main_window, true);

  app_message_register_inbox_received(inbox_received_callback);
  app_message_register_inbox_dropped(inbox_dropped_callback);
  app_message_register_outbox_failed(outbox_failed_callback);
  app_message_register_outbox_sent(outbox_sent_callback);

  app_message_open(app_message_inbox_size_maximum(),
                   app_message_outbox_size_maximum());

  connection_service_subscribe((ConnectionHandlers) {
    .pebble_app_connection_handler = phone_conn_handler
  });
  phone_conn_handler(connection_service_peek_pebble_app_connection());
}

static void deinit(void) {
  connection_service_unsubscribe();
  window_destroy(s_main_window);
  s_main_window = NULL;
}

int main(void) {
  init();
  app_event_loop();
  deinit();
}
