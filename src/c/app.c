#include <pebble.h>

static Window *s_main_window;
static TextLayer *s_time_layer, *s_station_layer, *s_type_layer, *s_dest_layer, *s_countdown_layer, *s_depart_layer, *s_note1_layer;
static BitmapLayer *s_icon_layer;
static GBitmap *s_icon_bitmap = NULL;

static TextLayer *s_toast_layer = NULL;
static AppTimer *s_toast_timer = NULL;

static GRect s_countdown_frame;
static GColor s_highlight_bg_color;
static GColor s_highlight_text_color;

static int s_target_hour = 0;
static int s_target_min = 0;
static bool s_data_received = false;

static bool s_vibrated_3min = false;
static bool s_vibrated_1min = false;
static bool s_vibrated_0min = false;

static bool is_dark_color(GColor color) {
#if defined(PBL_BW)
  return gcolor_equal(color, GColorBlack);
#else
  return (color.r * 3 + color.g * 10 + color.b * 1) < 7;
#endif
}

static const uint32_t S_ICON_IDS[] = {
  RESOURCE_ID_IMAGE_ICON_0,
  RESOURCE_ID_IMAGE_ICON_1,
  RESOURCE_ID_IMAGE_ICON_2,
  RESOURCE_ID_IMAGE_ICON_3,
  RESOURCE_ID_IMAGE_ICON_4,
  RESOURCE_ID_IMAGE_ICON_5,
  RESOURCE_ID_IMAGE_ICON_6,
  RESOURCE_ID_IMAGE_ICON_7,
  RESOURCE_ID_IMAGE_ICON_8,
  RESOURCE_ID_IMAGE_ICON_9,
  RESOURCE_ID_IMAGE_ICON_10,
  RESOURCE_ID_IMAGE_ICON_11,
  RESOURCE_ID_IMAGE_ICON_12,
  RESOURCE_ID_IMAGE_ICON_13,
  RESOURCE_ID_IMAGE_ICON_14,
  RESOURCE_ID_IMAGE_ICON_15
};

static void replace_newline(char *text) {
  for (int i = 0; text[i] != '\0'; i++) {
    if (text[i] == '\\' && text[i+1] == 'n') {
      text[i] = ' '; text[i+1] = '\n';
    }
  }
}

static void request_train(int key) {
  DictionaryIterator *iter;
  if (app_message_outbox_begin(&iter) == APP_MSG_OK) {
    dict_write_uint8(iter, key, 1);
    app_message_outbox_send();
  }
}

static void toast_timer_callback(void *data) {
  s_toast_timer = NULL;
  if (s_toast_layer) {
    layer_set_hidden(text_layer_get_layer(s_toast_layer), true);
  }
}

static void show_toast(const char *text) {
  if (!s_toast_layer) return;
  text_layer_set_text(s_toast_layer, text);
  layer_set_hidden(text_layer_get_layer(s_toast_layer), false);
  if (s_toast_timer) {
    app_timer_cancel(s_toast_timer);
  }
  s_toast_timer = app_timer_register(1000, toast_timer_callback, NULL);
}

static void update_time() {
  time_t temp = time(NULL);
  struct tm *tick_time = localtime(&temp);
  static char s_buffer[8];
  strftime(s_buffer, sizeof(s_buffer), "%H:%M", tick_time);
  text_layer_set_text(s_time_layer, s_buffer);
}

static void update_countdown() {
  if (!s_data_received) {
    text_layer_set_text(s_countdown_layer, "Loading");
    return;
  }
  time_t now = time(NULL);
  struct tm *t = localtime(&now);
  int hour = t->tm_hour; if (hour < 4) hour += 24;
  int now_sec = hour * 3600 + t->tm_min * 60 + t->tm_sec;
  int train_sec = s_target_hour * 3600 + s_target_min * 60;
  int diff = now_sec - train_sec;
  static char s_count_buf[16];

  text_layer_set_text_color(s_countdown_layer, GColorBlack);
  text_layer_set_background_color(s_countdown_layer, GColorClear);

  if (diff < 0) {
    int d = -diff;
    snprintf(s_count_buf, sizeof(s_count_buf), "%02d:%02d", d / 60, d % 60);

    if (d <= 180 && !s_vibrated_3min) {
      vibes_short_pulse();
      s_vibrated_3min = true;
    }
    if (d <= 60 && !s_vibrated_1min) {
      vibes_short_pulse();
      s_vibrated_1min = true;
    }
  } else if (diff <= 180) {
    if (diff <= 2 && !s_vibrated_0min) {
      vibes_double_pulse();
      s_vibrated_0min = true;
    }

    snprintf(s_count_buf, sizeof(s_count_buf), "%02d:%02d", diff / 60, diff % 60);
    if (t->tm_sec % 2 == 0) {
      text_layer_set_text_color(s_countdown_layer, s_highlight_text_color);
      text_layer_set_background_color(s_countdown_layer, s_highlight_bg_color);
    }
  } else {
    snprintf(s_count_buf, sizeof(s_count_buf), "Departed");
  }
  text_layer_set_text(s_countdown_layer, s_count_buf);
}

static void tick_handler(struct tm *t, TimeUnits u) { update_time(); update_countdown(); }

#if defined(PBL_TOUCH)
static void tap_handler(AccelAxisType axis, int32_t direction) {
  light_enable_interaction();
}
#endif

static void animate_layer(Layer *layer, GRect start, GRect end, int duration, int delay) {
  PropertyAnimation *prop_anim = property_animation_create_layer_frame(layer, &start, &end);
  Animation *anim = property_animation_get_animation(prop_anim);
  animation_set_duration(anim, duration);
  animation_set_delay(anim, delay);
  animation_set_curve(anim, AnimationCurveEaseIn);
  animation_schedule(anim);
}

static void inbox_received_callback(DictionaryIterator *iterator, void *context) {
  Tuple *st_t = dict_find(iterator, MESSAGE_KEY_KEY_STATION);
  Tuple *ic_t = dict_find(iterator, MESSAGE_KEY_KEY_ICON);
  Tuple *de_t = dict_find(iterator, MESSAGE_KEY_KEY_DEST);
  Tuple *n1_t = dict_find(iterator, MESSAGE_KEY_KEY_NOTE1);
  Tuple *hr_t = dict_find(iterator, MESSAGE_KEY_KEY_HOUR);
  Tuple *mn_t = dict_find(iterator, MESSAGE_KEY_KEY_MIN);
  Tuple *cl_t = dict_find(iterator, MESSAGE_KEY_KEY_HIGHLIGHT_COLOR);
  Tuple *ty_t = dict_find(iterator, MESSAGE_KEY_KEY_TYPE_TEXT);
  Tuple *tl_res_t = dict_find(iterator, MESSAGE_KEY_KEY_TIMELINE_RESULT);

  if (tl_res_t) {
    if (tl_res_t->value->int32 == 1) {
      show_toast("Added Pin!");
    } else {
      show_toast("Failed Pin");
    }
    return;
  }

#if defined(PBL_PLATFORM_EMERY) || defined(PBL_PLATFORM_GABBRO)
  Tuple *tc_t = dict_find(iterator, MESSAGE_KEY_KEY_TYPE_COLOR);
  Tuple *tb_t = dict_find(iterator, MESSAGE_KEY_KEY_TYPE_BG_COLOR);
#endif

  static char s_st_buf[64], s_ty_buf[64], s_de_buf[128], s_n1_buf[128];

  if (st_t) {
    strncpy(s_st_buf, st_t->value->cstring, sizeof(s_st_buf) - 1);
    s_st_buf[sizeof(s_st_buf)-1] = '\0';
    text_layer_set_text(s_station_layer, s_st_buf);
  }

  if (ty_t) {
    strncpy(s_ty_buf, ty_t->value->cstring, sizeof(s_ty_buf) - 1);
    s_ty_buf[sizeof(s_ty_buf)-1] = '\0';
  } else {
    s_ty_buf[0] = '\0';
  }
  text_layer_set_text(s_type_layer, s_ty_buf);

  if (de_t) {
    strncpy(s_de_buf, de_t->value->cstring, sizeof(s_de_buf) - 1);
    s_de_buf[sizeof(s_de_buf)-1] = '\0';
    text_layer_set_text(s_dest_layer, s_de_buf);
  }

#if defined(PBL_PLATFORM_EMERY) || defined(PBL_PLATFORM_GABBRO)
  if (tc_t && tb_t) {
    text_layer_set_text_color(s_type_layer, GColorFromHEX(tc_t->value->int32));
    text_layer_set_background_color(s_type_layer, GColorFromHEX(tb_t->value->int32));
  } else {
    text_layer_set_text_color(s_type_layer, GColorBlack);
    text_layer_set_background_color(s_type_layer, GColorClear);
  }
#endif

  if (n1_t) {
    strncpy(s_n1_buf, n1_t->value->cstring, sizeof(s_n1_buf) - 1);
    s_n1_buf[sizeof(s_n1_buf)-1] = '\0';
    replace_newline(s_n1_buf);
    text_layer_set_text(s_note1_layer, s_n1_buf);
  }

  if (cl_t) {
#if !defined(PBL_BW)
    GColor raw_color = GColorFromHEX(cl_t->value->int32);
    s_highlight_bg_color = raw_color;
    s_highlight_text_color = is_dark_color(s_highlight_bg_color) ? GColorWhite : GColorBlack;
    text_layer_set_background_color(s_time_layer, s_highlight_bg_color);
    text_layer_set_text_color(s_time_layer, s_highlight_text_color);
#endif
  }

  bool has_train = true;
  if (hr_t) {
    s_target_hour = (int)hr_t->value->int32;
    if (s_target_hour == -1) has_train = false;
    else if (s_target_hour >= 0 && s_target_hour < 4) s_target_hour += 24;
  }
  if (mn_t) s_target_min = (int)mn_t->value->int32;

  s_vibrated_3min = false;
  s_vibrated_1min = false;
  s_vibrated_0min = false;

  if (ic_t) {
    int icon_id = (int)ic_t->value->int32;
    if (s_icon_bitmap) gbitmap_destroy(s_icon_bitmap);
    uint32_t res_id = (icon_id >= 0 && icon_id < (int)ARRAY_LENGTH(S_ICON_IDS)) ? S_ICON_IDS[icon_id] : RESOURCE_ID_IMAGE_ICON_15;
    s_icon_bitmap = gbitmap_create_with_resource(res_id);
    bitmap_layer_set_bitmap(s_icon_layer, s_icon_bitmap);
  }

#if defined(PBL_PLATFORM_EMERY) || defined(PBL_PLATFORM_GABBRO)
  text_layer_set_font(s_countdown_layer, fonts_get_system_font(FONT_KEY_GOTHIC_28_BOLD));
#endif

  s_data_received = true;

  if (has_train) {
    static char s_dep_buf[16];
    snprintf(s_dep_buf, sizeof(s_dep_buf), "Dep: %02d:%02d", s_target_hour % 24, s_target_min);
    text_layer_set_text(s_depart_layer, s_dep_buf);
  } else {
    text_layer_set_text(s_depart_layer, "");
    text_layer_set_text(s_countdown_layer, "Departed");
  }

  Layer *root = window_get_root_layer(s_main_window);
  int w = layer_get_bounds(root).size.w;

#if defined(PBL_PLATFORM_GABBRO)
  if (has_train) {
    animate_layer(text_layer_get_layer(s_type_layer),   GRect(0,  67, w, 24), GRect(0,  65, w, 24), 300, 100);
    animate_layer(text_layer_get_layer(s_dest_layer),   GRect(0,  91, w, 32), GRect(0,  89, w, 32), 300, 150);
    animate_layer(text_layer_get_layer(s_depart_layer), GRect(0, 155, w, 28), GRect(0, 153, w, 28), 300, 180);
  }
  animate_layer(text_layer_get_layer(s_countdown_layer), GRect(0, 123, w, 32), GRect(0, 121, w, 32), 300,   0);
  animate_layer(text_layer_get_layer(s_note1_layer),     GRect(0, 183, w, 48), GRect(0, 181, w, 48), 300, 200);
#elif defined(PBL_PLATFORM_EMERY)
  if (has_train) {
    animate_layer(text_layer_get_layer(s_type_layer),   GRect(0,  52, w, 24), GRect(0,  49, w, 24), 300, 100);
    animate_layer(text_layer_get_layer(s_dest_layer),   GRect(0,  76, w, 32), GRect(0,  73, w, 32), 300, 150);
    animate_layer(text_layer_get_layer(s_depart_layer), GRect(0, 143, w, 28), GRect(0, 141, w, 28), 300, 180);
  }
  animate_layer(text_layer_get_layer(s_countdown_layer), GRect(0, 108, w, 32), GRect(0, 105, w, 32), 300,   0);
  animate_layer(text_layer_get_layer(s_note1_layer),     GRect(0, 171, w, 48), GRect(0, 169, w, 48), 300, 200);
#else
  int h = layer_get_bounds(root).size.h;
  animate_layer(text_layer_get_layer(s_countdown_layer), GRect(0, (int)(h * 0.45), w, (int)(h * 0.19)), s_countdown_frame, 300, 0);
  if (has_train) animate_layer(text_layer_get_layer(s_dest_layer), GRect(0, (int)(h * 0.28), w, (int)(h * 0.13)), GRect(0, (int)(h * 0.26), w, (int)(h * 0.13)), 300, 100);
  animate_layer(text_layer_get_layer(s_note1_layer), GRect(0, (int)(h * 0.72), w, (int)(h * 0.24)), GRect(0, (int)(h * 0.70), w, (int)(h * 0.24)), 300, 200);
#endif
  update_countdown();
}

static void center_long_click_handler(ClickRecognizerRef recognizer, void *context) {
  s_data_received = false;
  
  text_layer_set_text(s_station_layer, "");
  text_layer_set_text(s_type_layer, "");
  text_layer_set_text(s_dest_layer, "");
  text_layer_set_text(s_depart_layer, "");
  text_layer_set_text(s_note1_layer, "");
  
  if (s_icon_bitmap) {
    gbitmap_destroy(s_icon_bitmap);
    s_icon_bitmap = NULL;
  }
  bitmap_layer_set_bitmap(s_icon_layer, NULL);
  
  update_countdown();
  
  DictionaryIterator *iter;
  if (app_message_outbox_begin(&iter) == APP_MSG_OK) {
    dict_write_uint8(iter, MESSAGE_KEY_REQUEST_TOGGLE_URL, 1); 
    app_message_outbox_send();
  }
}

static void up_long_click_handler(ClickRecognizerRef recognizer, void *context) {
  request_train(MESSAGE_KEY_KEY_REQUEST_ADD_TIMELINE);
}

static void down_long_click_handler(ClickRecognizerRef recognizer, void *context) {
  request_train(MESSAGE_KEY_KEY_REQUEST_ADD_TIMELINE);
}

static void up_click_handler(ClickRecognizerRef r, void *c) { request_train(MESSAGE_KEY_KEY_REQUEST_PREV); }
static void down_click_handler(ClickRecognizerRef r, void *c) { request_train(MESSAGE_KEY_KEY_REQUEST_NEXT); }
static void center_click_handler(ClickRecognizerRef r, void *c) { request_train(MESSAGE_KEY_KEY_REQUEST_SWITCH); }

static void click_config_provider(void *c) {
  window_single_click_subscribe(BUTTON_ID_UP, up_click_handler);
  window_single_click_subscribe(BUTTON_ID_DOWN, down_click_handler);
  window_single_click_subscribe(BUTTON_ID_SELECT, center_click_handler);
  window_long_click_subscribe(BUTTON_ID_SELECT, 500, center_long_click_handler, NULL);
  window_long_click_subscribe(BUTTON_ID_UP, 500, up_long_click_handler, NULL);
  window_long_click_subscribe(BUTTON_ID_DOWN, 500, down_long_click_handler, NULL);
}

static void main_window_load(Window *window) {
  Layer *w_layer = window_get_root_layer(window);
  GRect bounds = layer_get_bounds(w_layer);
  int w = bounds.size.w;
  int h = bounds.size.h;

  s_highlight_bg_color = GColorBlack; s_highlight_text_color = GColorWhite;

#if defined(PBL_PLATFORM_GABBRO)
  GRect rect_time = GRect(0, 16, w, 24), rect_icon = GRect(40, 41, 23, 23), rect_station = GRect(65, 40, w - 65, 24), rect_type = GRect(0, 64, w, 24), rect_dest = GRect(0, 88, w, 32), rect_countdown = GRect(0, 120, w, 32), rect_depart = GRect(0, 152, w, 28), rect_note1 = GRect(0, 180, w, 48);
  s_countdown_frame = rect_countdown;
#elif defined(PBL_PLATFORM_EMERY)
  GRect rect_time = GRect(0, 0, w, 24), rect_icon = GRect(10, 25, 23, 23), rect_station = GRect(35, 24, w - 35, 24), rect_type = GRect(0, 48, w, 24), rect_dest = GRect(0, 72, w, 32), rect_countdown = GRect(0, 104, w, 32), rect_depart = GRect(0, 140, w, 28), rect_note1 = GRect(0, 168, w, 48);
  s_countdown_frame = rect_countdown;
#else
  s_countdown_frame = GRect(0, (int)(h * 0.40), w, (int)(h * 0.19));
  #if defined(PBL_ROUND)
    GRect rect_icon = GRect(28, (int)(h * 0.12), 23, 23);
    GRect rect_station = GRect(53, (int)(h * 0.13), w - 81, (int)(h * 0.12));
  #else
    GRect rect_icon = GRect(10, (int)(h * 0.12), 23, 23);
    GRect rect_station = GRect(35, (int)(h * 0.13), w - 35, (int)(h * 0.12));
  #endif
  GRect rect_time = GRect(0, 0, w, (int)(h * 0.12)), rect_type = GRect(0, 0, 0, 0), rect_dest = GRect(0, (int)(h * 0.26), w, (int)(h * 0.13)), rect_countdown = s_countdown_frame, rect_depart = GRect(0, (int)(h * 0.58), w, (int)(h * 0.13)), rect_note1 = GRect(0, (int)(h * 0.72), w, (int)(h * 0.24));
#endif

  s_time_layer = text_layer_create(rect_time); s_station_layer = text_layer_create(rect_station); s_type_layer = text_layer_create(rect_type); s_dest_layer = text_layer_create(rect_dest); s_countdown_layer = text_layer_create(rect_countdown); s_depart_layer = text_layer_create(rect_depart); s_note1_layer = text_layer_create(rect_note1); s_icon_layer = bitmap_layer_create(rect_icon);

  text_layer_set_background_color(s_time_layer, GColorBlack); text_layer_set_text_color(s_time_layer, GColorWhite); bitmap_layer_set_compositing_mode(s_icon_layer, GCompOpSet);
  layer_add_child(w_layer, bitmap_layer_get_layer(s_icon_layer));

  TextLayer *layers[] = { s_time_layer, s_station_layer, s_type_layer, s_dest_layer, s_countdown_layer, s_depart_layer, s_note1_layer };
  for (int i = 0; i < 7; i++) {
    text_layer_set_text_alignment(layers[i], (i == 1) ? GTextAlignmentLeft : GTextAlignmentCenter);
#if defined(PBL_PLATFORM_EMERY) || defined(PBL_PLATFORM_GABBRO)
    text_layer_set_font(layers[i], fonts_get_system_font((i == 4) ? FONT_KEY_GOTHIC_24_BOLD : (i == 3) ? FONT_KEY_GOTHIC_28_BOLD : (i == 5) ? FONT_KEY_GOTHIC_24_BOLD : FONT_KEY_GOTHIC_18_BOLD));
#else
    text_layer_set_font(layers[i], fonts_get_system_font((i == 4) ? FONT_KEY_GOTHIC_28_BOLD : FONT_KEY_GOTHIC_18_BOLD));
#endif
    text_layer_set_overflow_mode(layers[i], (i == 6) ? GTextOverflowModeWordWrap : GTextOverflowModeTrailingEllipsis);
    layer_add_child(w_layer, text_layer_get_layer(layers[i]));
  }

  s_toast_layer = text_layer_create(GRect((w - 110) / 2, (h - 32) / 2, 110, 32));
  text_layer_set_background_color(s_toast_layer, GColorBlack);
  text_layer_set_text_color(s_toast_layer, GColorWhite);
  text_layer_set_text_alignment(s_toast_layer, GTextAlignmentCenter);
  text_layer_set_font(s_toast_layer, fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD));
  layer_set_hidden(text_layer_get_layer(s_toast_layer), true);
  layer_add_child(w_layer, text_layer_get_layer(s_toast_layer));
}

static void main_window_unload(Window *window) {
  if (s_toast_timer) {
    app_timer_cancel(s_toast_timer);
    s_toast_timer = NULL;
  }
  text_layer_destroy(s_toast_layer);
  text_layer_destroy(s_time_layer); text_layer_destroy(s_station_layer); text_layer_destroy(s_dest_layer); text_layer_destroy(s_countdown_layer); text_layer_destroy(s_depart_layer); text_layer_destroy(s_note1_layer); text_layer_destroy(s_type_layer); bitmap_layer_destroy(s_icon_layer);
  if (s_icon_bitmap) { gbitmap_destroy(s_icon_bitmap); s_icon_bitmap = NULL; }
}

static void init() {
  s_main_window = window_create();
  window_set_window_handlers(s_main_window, (WindowHandlers) {.load = main_window_load, .unload = main_window_unload});
  window_stack_push(s_main_window, true);
  window_set_click_config_provider(s_main_window, click_config_provider);
  app_message_register_inbox_received(inbox_received_callback);
  app_message_open(512, 128);
  tick_timer_service_subscribe(SECOND_UNIT, tick_handler);

#if defined(PBL_TOUCH)
  accel_tap_service_subscribe(tap_handler);
#endif
}

static void deinit() {
  tick_timer_service_unsubscribe();

#if defined(PBL_TOUCH)
  accel_tap_service_unsubscribe();
#endif
  window_destroy(s_main_window);
}

int main() { init(); app_event_loop(); deinit(); }