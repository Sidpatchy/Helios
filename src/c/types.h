#pragma once
#include <pebble.h>

typedef struct {
  bool valid;
  int32_t offset;
  char date[20];    // e.g., "Wed Sep 03"
  char dawn[6];     // "HH:MM"
  char sunrise[6];
  char sunset[6];
  char dusk[6];
} DayTimes;
