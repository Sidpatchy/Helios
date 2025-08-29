#include <pebble.h>
#include "ui.h"
#include "msg.h"

static Window *s_main_window;

// Buttons
static void up_click_handler(ClickRecognizerRef recognizer, void *ctx) {
  msg_navigate_to_offset(msg_get_day_offset() - 1);
}
static void down_click_handler(ClickRecognizerRef recognizer, void *ctx) {
  msg_navigate_to_offset(msg_get_day_offset() + 1);
}
static void select_click_handler(ClickRecognizerRef recognizer, void *ctx) {
  msg_navigate_to_offset(0);
}

static void click_config_provider(void *ctx) {
  window_single_click_subscribe(BUTTON_ID_UP, up_click_handler);
  window_single_click_subscribe(BUTTON_ID_DOWN, down_click_handler);
  window_single_click_subscribe(BUTTON_ID_SELECT, select_click_handler);

  // Optional: hold to scroll faster
  window_single_repeating_click_subscribe(BUTTON_ID_UP, 180, up_click_handler);
  window_single_repeating_click_subscribe(BUTTON_ID_DOWN, 180, down_click_handler);
}

// Connection service
static void phone_conn_handler(bool connected) {
  msg_on_phone_conn_changed(connected);
}

static void init(void) {
  s_main_window = window_create();
  window_set_background_color(s_main_window, GColorBlack);
  window_set_click_config_provider(s_main_window, click_config_provider);

  ui_init(s_main_window);
  window_stack_push(s_main_window, true);

  msg_init();

  connection_service_subscribe((ConnectionHandlers) {
    .pebble_app_connection_handler = phone_conn_handler
  });
  phone_conn_handler(connection_service_peek_pebble_app_connection());
}

static void deinit(void) {
  connection_service_unsubscribe();

  msg_deinit();
  ui_deinit();

  window_destroy(s_main_window);
  s_main_window = NULL;
}

int main(void) {
  init();
  app_event_loop();
  deinit();
}
