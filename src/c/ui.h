#pragma once
#include <pebble.h>
#include "types.h"

// Initialize/destroy all UI layers within the given window
void ui_init(Window *window);
void ui_deinit(void);

// Display helpers
void ui_show_status(const char *text);         // status overlay
void ui_show_daytimes(const DayTimes *dt);     // main content

// Relayout (e.g., after config changes)
void ui_relayout(void);
