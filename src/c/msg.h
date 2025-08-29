#pragma once
#include <pebble.h>

// Initialize/destroy messaging and cache
void msg_init(void);
void msg_deinit(void);

// Navigation and actions
void msg_navigate_to_offset(int32_t new_offset);
void msg_request_times(void);

// Connection change
void msg_on_phone_conn_changed(bool connected);

// Accessors
int32_t msg_get_day_offset(void);
