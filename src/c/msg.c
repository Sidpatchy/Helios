#include <pebble.h>
#include "msg.h"
#include "ui.h"
#include "types.h"

// Cache/state
static int32_t s_day_offset = 0;      // currently displayed offset (0=today)
static int32_t s_cache_center = 9999; // center offset of the cached bundle
static DayTimes s_cache[3];           // 0: center-1, 1: center, 2: center+1
static AppTimer *s_retry_timer = NULL;

// Utils
static const char *prv_reason(AppMessageResult r) {
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
}

static void prv_clear_cache(void) {
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

static DayTimes *prv_cache_get_for_offset(int32_t offset) {
  if (s_cache_center == 9999) return NULL;
  if (offset == s_cache_center - 1) return &s_cache[0];
  if (offset == s_cache_center)     return &s_cache[1];
  if (offset == s_cache_center + 1) return &s_cache[2];
  return NULL;
}

static void prv_retry_cb(void *data);

void msg_request_times(void) {
  DictionaryIterator *iter;
  AppMessageResult r = app_message_outbox_begin(&iter);
  if (r != APP_MSG_OK) {
    static char s_buf[64];
    snprintf(s_buf, sizeof(s_buf), "Outbox: %s", prv_reason(r));
    ui_show_status(s_buf);
    s_retry_timer = app_timer_register(2000, prv_retry_cb, NULL);
    return;
  }
  int32_t one = 1;
  dict_write_int(iter, MESSAGE_KEY_REQ, &one, sizeof(one), true);
  dict_write_int(iter, MESSAGE_KEY_OFFSET, &s_day_offset, sizeof(s_day_offset), true);
  dict_write_end(iter);
  app_message_outbox_send();

  if (!prv_cache_get_for_offset(s_day_offset)) {
    ui_show_status("Fetching…");
  }
}

static void prv_retry_cb(void *data) {
  msg_request_times();
}

// Inbox
static void prv_inbox_received(DictionaryIterator *iter, void *context) {
  Tuple *hello_t = dict_find(iter, MESSAGE_KEY_HELLO);
  Tuple *error_t = dict_find(iter, MESSAGE_KEY_ERROR);

  if (hello_t) {
    ui_show_status("Connected. Fetching…");
    prv_clear_cache();
    msg_request_times();
    return;
  }

  if (error_t) {
    static char s_buf[64];
    snprintf(s_buf, sizeof(s_buf), "Error: %s", error_t->value->cstring);
    ui_show_status(s_buf);
    return;
  }

  Tuple *center_t = dict_find(iter, MESSAGE_KEY_CENTER);
  if (!center_t) {
    // Legacy single-day payload
    Tuple *date_t    = dict_find(iter, MESSAGE_KEY_DATE);
    Tuple *dawn_t    = dict_find(iter, MESSAGE_KEY_DAWN);
    Tuple *sunrise_t = dict_find(iter, MESSAGE_KEY_SUNRISE);
    Tuple *sunset_t  = dict_find(iter, MESSAGE_KEY_SUNSET);
    Tuple *dusk_t    = dict_find(iter, MESSAGE_KEY_DUSK);
    if (date_t || dawn_t || sunrise_t || sunset_t || dusk_t) {
      DayTimes tmp = { .valid = true, .offset = s_day_offset };
      snprintf(tmp.date, sizeof(tmp.date), "%s", date_t ? date_t->value->cstring : "");
      snprintf(tmp.dawn, sizeof(tmp.dawn), "%s", dawn_t ? dawn_t->value->cstring : "");
      snprintf(tmp.sunrise, sizeof(tmp.sunrise), "%s", sunrise_t ? sunrise_t->value->cstring : "");
      snprintf(tmp.sunset, sizeof(tmp.sunset), "%s", sunset_t ? sunset_t->value->cstring : "");
      snprintf(tmp.dusk, sizeof(tmp.dusk), "%s", dusk_t ? dusk_t->value->cstring : "");
      ui_show_daytimes(&tmp);
    }
    return;
  }

  int32_t center = center_t->value->int32;
  s_cache_center = center;

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

  DayTimes *dt = prv_cache_get_for_offset(s_day_offset);
  if (dt && dt->valid) {
    ui_show_daytimes(dt);
  }
}

static void prv_inbox_dropped(AppMessageResult reason, void *context) {
  static char s_buf[40];
  snprintf(s_buf, sizeof(s_buf), "Inbox dropped: %s", prv_reason(reason));
  ui_show_status(s_buf);
}

static void prv_outbox_failed(DictionaryIterator *iter, AppMessageResult reason, void *context) {
  static char s_buf[48];
  snprintf(s_buf, sizeof(s_buf), "Send failed: %s", prv_reason(reason));
  ui_show_status(s_buf);
  s_retry_timer = app_timer_register(1200, prv_retry_cb, NULL);
}

static void prv_outbox_sent(DictionaryIterator *iter, void *context) {
  // No-op
}

// Public API
void msg_init(void) {
  prv_clear_cache();

  app_message_register_inbox_received(prv_inbox_received);
  app_message_register_inbox_dropped(prv_inbox_dropped);
  app_message_register_outbox_failed(prv_outbox_failed);
  app_message_register_outbox_sent(prv_outbox_sent);

  app_message_open(app_message_inbox_size_maximum(),
                   app_message_outbox_size_maximum());
}

void msg_deinit(void) {
  if (s_retry_timer) {
    app_timer_cancel(s_retry_timer);
    s_retry_timer = NULL;
  }
  app_message_deregister_callbacks();
}

void msg_on_phone_conn_changed(bool connected) {
  if (connected) {
    ui_show_status("Connecting…");
    s_retry_timer = app_timer_register(400, prv_retry_cb, NULL);
  } else {
    ui_show_status("Waiting for phone…");
  }
}

void msg_navigate_to_offset(int32_t new_offset) {
  s_day_offset = new_offset;

  DayTimes *dt = prv_cache_get_for_offset(s_day_offset);
  if (dt && dt->valid) {
    ui_show_daytimes(dt);  // instant
  } else {
    ui_show_status("Fetching…");
  }

  msg_request_times();
}

int32_t msg_get_day_offset(void) {
  return s_day_offset;
}
