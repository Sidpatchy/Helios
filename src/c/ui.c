#include <pebble.h>
#include "ui.h"
#include "types.h"

// Window and layers
static Window *s_main_window;

static TextLayer *s_text_layer;       // status overlay
static TextLayer *s_date_layer;       // date line
static TextLayer *s_times_layer;      // LECO numbers (times)
static Layer     *s_labels_layer;     // custom-drawn labels (baseline-aligned)

// Fonts
static GFont s_font_date;
static GFont s_font_labels;
static GFont s_font_times;

// Metrics for baseline alignment
static int16_t s_leco_line_h = 0;     // measured row height from LECO
static int16_t s_label_h[4] = {0};
static int8_t  s_label_baseline_nudge = 0;

// Labels we draw
static const char *LABELS[4] = {"Dawn", "Sunrise", "Sunset", "Dusk"};

// Percent-based layout (per platform tuned)
typedef struct {
  uint8_t margin_h_pct;    // left/right
  uint8_t margin_top_pct;  // top
  uint8_t margin_bot_pct;  // bottom
  uint8_t label_col_pct;   // of content width
  uint8_t col_gap_pct;     // of content width
  uint8_t row_gap_pct;     // of full height
  int8_t  times_nudge_pct; // of content width (left negative)
  int8_t  times_nudge_px;  // absolute pixel nudge
  const char *font_key_date;
  const char *font_key_labels;
  const char *font_key_times;
} LayoutParams;

static LayoutParams s_lp;

static int16_t prv_measure_text_h(const char *text, GFont font, int16_t width, GTextOverflowMode overflow) {
  GRect box = GRect(0, 0, width, 2000);
  GSize sz = graphics_text_layout_get_content_size(text, font, box, overflow, GTextAlignmentLeft);
  return sz.h;
}

static void prv_compute_platform_params(void) {
  // Defaults for Aplite/Basalt/Diorite (144x168 baseline)
  s_lp.margin_h_pct   = 6;
  s_lp.margin_top_pct = 7;
  s_lp.margin_bot_pct = 6;
  s_lp.label_col_pct  = 42; // ~54 px of ~128 content width
  s_lp.col_gap_pct    = 5;  // ~6 px
  s_lp.row_gap_pct    = 3;
  s_lp.times_nudge_pct= 0;
  s_lp.times_nudge_px = -6;
  s_lp.font_key_date   = FONT_KEY_GOTHIC_24_BOLD;
  s_lp.font_key_labels = FONT_KEY_GOTHIC_24;
  s_lp.font_key_times  = FONT_KEY_LECO_26_BOLD_NUMBERS_AM_PM;

  // Chalk (round)
  #if defined(PBL_PLATFORM_CHALK)
    s_lp.margin_h_pct   = 9;
    s_lp.margin_top_pct = 8;
    s_lp.margin_bot_pct = 8;
    s_lp.label_col_pct  = 44;
    s_lp.col_gap_pct    = 5;
    s_lp.row_gap_pct    = 3;
    s_lp.times_nudge_pct= -2;
    s_lp.times_nudge_px = 0;
    s_lp.font_key_date   = FONT_KEY_GOTHIC_24_BOLD;
    s_lp.font_key_labels = FONT_KEY_GOTHIC_18;
    s_lp.font_key_times  = FONT_KEY_LECO_20_BOLD_NUMBERS;
  #endif

  // Emery (200x228)
  #if defined(PBL_PLATFORM_EMERY)
    s_lp.margin_h_pct   = 6;
    s_lp.margin_top_pct = 6;
    s_lp.margin_bot_pct = 6;
    s_lp.label_col_pct  = 38;
    s_lp.col_gap_pct    = 4;
    s_lp.row_gap_pct    = 3;
    s_lp.times_nudge_pct= -2;
    s_lp.times_nudge_px = -2;
    s_lp.font_key_date   = FONT_KEY_GOTHIC_28_BOLD;
    s_lp.font_key_labels = FONT_KEY_GOTHIC_24;
    s_lp.font_key_times  = FONT_KEY_LECO_32_BOLD_NUMBERS;
  #endif
}

static void prv_apply_fonts(void) {
  s_font_date   = fonts_get_system_font(s_lp.font_key_date);
  s_font_labels = fonts_get_system_font(s_lp.font_key_labels);
  s_font_times  = fonts_get_system_font(s_lp.font_key_times);
  if (s_date_layer)  text_layer_set_font(s_date_layer, s_font_date);
  if (s_times_layer) text_layer_set_font(s_times_layer, s_font_times);
}

static void prv_labels_update_proc(Layer *layer, GContext *ctx) {
  graphics_context_set_text_color(ctx, GColorWhite);
  GRect bounds = layer_get_bounds(layer);
  int16_t row_h = s_leco_line_h > 0 ? s_leco_line_h : 24;

  for (int i = 0; i < 4; i++) {
    int16_t label_h = s_label_h[i] > 0 ? s_label_h[i] : 18;
    int16_t row_top = i * row_h;
    int16_t row_bottom = row_top + row_h;

    GRect r = GRect(0, row_bottom - label_h + s_label_baseline_nudge,
                    bounds.size.w, label_h);
    graphics_draw_text(ctx, LABELS[i], s_font_labels, r,
                       GTextOverflowModeWordWrap, GTextAlignmentLeft, NULL);
  }
}

static void prv_recalc_metrics_and_redraw(void) {
  if (!s_main_window || !s_times_layer || !s_labels_layer) return;

  GRect times_frame = layer_get_frame(text_layer_get_layer(s_times_layer));
  s_leco_line_h = prv_measure_text_h("88:88", s_font_times, times_frame.size.w, GTextOverflowModeFill);

  GRect labels_frame = layer_get_frame(s_labels_layer);
  s_label_h[0] = prv_measure_text_h("Dawn",    s_font_labels, labels_frame.size.w, GTextOverflowModeWordWrap);
  s_label_h[1] = prv_measure_text_h("Sunrise", s_font_labels, labels_frame.size.w, GTextOverflowModeWordWrap);
  s_label_h[2] = prv_measure_text_h("Sunset",  s_font_labels, labels_frame.size.w, GTextOverflowModeWordWrap);
  s_label_h[3] = prv_measure_text_h("Dusk",    s_font_labels, labels_frame.size.w, GTextOverflowModeWordWrap);

  layer_mark_dirty(s_labels_layer);
}

static void prv_layout_layers(void) {
  if (!s_main_window) return;

  Layer *window_layer = window_get_root_layer(s_main_window);
  GRect bounds = layer_get_bounds(window_layer);

  prv_compute_platform_params();
  prv_apply_fonts();

  int16_t margin_x   = (bounds.size.w * s_lp.margin_h_pct) / 100;
  int16_t margin_top = (bounds.size.h * s_lp.margin_top_pct) / 100;
  int16_t margin_bot = (bounds.size.h * s_lp.margin_bot_pct) / 100;

  int16_t content_x = margin_x;
  int16_t content_y = margin_top;
  int16_t content_w = bounds.size.w - margin_x - margin_x;
  int16_t content_h = bounds.size.h - margin_top - margin_bot;

  int16_t date_w = content_w;
  int16_t row_gap_px = (bounds.size.h * s_lp.row_gap_pct) / 100;
  if (row_gap_px < 1) row_gap_px = 1;

  int16_t date_h = prv_measure_text_h("Wed Sep 30", s_font_date, date_w, GTextOverflowModeWordWrap);
  GRect date_frame = GRect(content_x, content_y, date_w, date_h);

  int16_t labels_w = (content_w * s_lp.label_col_pct) / 100;
  int16_t gap_w    = (content_w * s_lp.col_gap_pct) / 100;

  int16_t rows_y = content_y + date_h + row_gap_px;
  int16_t rows_h = content_h - date_h - row_gap_px;

  GRect labels_frame = GRect(content_x, rows_y, labels_w, rows_h);

  int16_t times_nudge = (content_w * s_lp.times_nudge_pct) / 100 + s_lp.times_nudge_px;
  int16_t times_x = content_x + labels_w + gap_w + times_nudge;
  int16_t times_w = (content_x + content_w) - times_x;
  if (times_w < 10) times_w = 10;
  GRect times_frame = GRect(times_x, rows_y, times_w, rows_h);

  if (s_date_layer)    layer_set_frame(text_layer_get_layer(s_date_layer), date_frame);
  if (s_labels_layer)  layer_set_frame(s_labels_layer, labels_frame);
  if (s_times_layer)   layer_set_frame(text_layer_get_layer(s_times_layer), times_frame);
  if (s_text_layer)    layer_set_frame(text_layer_get_layer(s_text_layer), bounds);

  prv_recalc_metrics_and_redraw();
}

// Public API
void ui_init(Window *window) {
  s_main_window = window;

  Layer *window_layer = window_get_root_layer(window);
  GRect bounds = layer_get_bounds(window_layer);
  window_set_background_color(window, GColorBlack);

  // Date
  s_date_layer = text_layer_create(bounds);
  text_layer_set_background_color(s_date_layer, GColorBlack);
  text_layer_set_text_color(s_date_layer, GColorWhite);
  text_layer_set_text_alignment(s_date_layer,
    #if defined(PBL_ROUND)
      GTextAlignmentCenter
    #else
      GTextAlignmentLeft
    #endif
  );
  text_layer_set_overflow_mode(s_date_layer, GTextOverflowModeWordWrap);
  layer_add_child(window_layer, text_layer_get_layer(s_date_layer));

  // Labels (custom draw for baseline alignment)
  s_labels_layer = layer_create(bounds);
  layer_set_update_proc(s_labels_layer, prv_labels_update_proc);
  layer_add_child(window_layer, s_labels_layer);

  // Times (LECO)
  s_times_layer = text_layer_create(bounds);
  text_layer_set_background_color(s_times_layer, GColorBlack);
  text_layer_set_text_color(s_times_layer, GColorWhite);
  text_layer_set_text_alignment(s_times_layer, GTextAlignmentLeft);
  text_layer_set_overflow_mode(s_times_layer, GTextOverflowModeFill);
  layer_add_child(window_layer, text_layer_get_layer(s_times_layer));

  // Status overlay
  s_text_layer = text_layer_create(bounds);
  text_layer_set_background_color(s_text_layer, GColorClear);
  text_layer_set_text_color(s_text_layer, GColorWhite);
  text_layer_set_text(s_text_layer, "Loading…");
  text_layer_set_text_alignment(s_text_layer,
    #if defined(PBL_ROUND)
      GTextAlignmentCenter
    #else
      GTextAlignmentLeft
    #endif
  );
  text_layer_set_overflow_mode(s_text_layer, GTextOverflowModeWordWrap);
  text_layer_set_font(s_text_layer, fonts_get_system_font(FONT_KEY_GOTHIC_24));
  layer_add_child(window_layer, text_layer_get_layer(s_text_layer));

  prv_layout_layers();
}

void ui_deinit(void) {
  if (s_text_layer)   { text_layer_destroy(s_text_layer);   s_text_layer = NULL; }
  if (s_times_layer)  { text_layer_destroy(s_times_layer);  s_times_layer = NULL; }
  if (s_labels_layer) { layer_destroy(s_labels_layer);      s_labels_layer = NULL; }
  if (s_date_layer)   { text_layer_destroy(s_date_layer);   s_date_layer = NULL; }
  s_main_window = NULL;
}

void ui_show_status(const char *text) {
  if (s_text_layer) text_layer_set_text(s_text_layer, text ? text : "");
}

void ui_show_daytimes(const DayTimes *dt) {
  if (!dt || !dt->valid) {
    ui_show_status("Fetching…");
    return;
  }
  ui_show_status("");

  // Date
  if (s_date_layer) text_layer_set_text(s_date_layer, dt->date[0] ? dt->date : "");

  // Times
  static char s_times_buf[32];
  snprintf(s_times_buf, sizeof(s_times_buf),
           "%s\n%s\n%s\n%s",
           dt->dawn[0]    ? dt->dawn    : "--:--",
           dt->sunrise[0] ? dt->sunrise : "--:--",
           dt->sunset[0]  ? dt->sunset  : "--:--",
           dt->dusk[0]    ? dt->dusk    : "--:--");
  if (s_times_layer) text_layer_set_text(s_times_layer, s_times_buf);

  if (s_labels_layer) layer_mark_dirty(s_labels_layer);
}

void ui_relayout(void) {
  prv_layout_layers();
}
