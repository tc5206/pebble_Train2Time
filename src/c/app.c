#include <pebble.h>

static Window *s_main_window;
static TextLayer *s_time_layer, *s_station_layer, *s_dest_layer, *s_countdown_layer, *s_depart_layer, *s_note1_layer;
static BitmapLayer *s_icon_layer;
static GBitmap *s_icon_bitmap = NULL;

// 座標とカラーの管理
static GRect s_countdown_frame;
static GColor s_highlight_bg_color;
static GColor s_highlight_text_color;

static int s_target_hour = 0;
static int s_target_min = 0;
static bool s_data_received = false;
static bool s_vibrated_1min = false;
static bool s_vibrated_0min = false;
static bool is_dark_color(GColor color) {
#if defined(PBL_BW)
  return gcolor_equal(color, GColorBlack);
#else
  // 簡易的な輝度判定
  return (color.r * 3 + color.g * 10 + color.b * 1) < 7;
#endif
}

static void replace_newline(char *text) {
  for (int i = 0; text[i] != '\0'; i++) {
    if (text[i] == '\\' && text[i+1] == 'n') {
      text[i] = ' '; text[i+1] = '\n'; 
    }
  }
}

static void request_train(int key) {
  DictionaryIterator *iter;
  app_message_outbox_begin(&iter);
  if (iter) {
    dict_write_uint8(iter, key, 1);
    app_message_outbox_send();
  }
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

  if (diff < 0) {
    int d = -diff;
    snprintf(s_count_buf, sizeof(s_count_buf), "%02d:%02d", d / 60, d % 60);
    text_layer_set_text_color(s_countdown_layer, GColorBlack);
    text_layer_set_background_color(s_countdown_layer, GColorClear);
    if (d == 60 && !s_vibrated_1min) { vibes_short_pulse(); s_vibrated_1min = true; }
  } else if (diff <= 180) {
    snprintf(s_count_buf, sizeof(s_count_buf), "%02d:%02d", diff / 60, diff % 60);
    if (diff == 0 && !s_vibrated_0min) { vibes_double_pulse(); s_vibrated_0min = true; }

    if (t->tm_sec % 2 == 0) {
      // 偶数秒（ハイライト時）
      text_layer_set_text_color(s_countdown_layer, s_highlight_text_color);
      text_layer_set_background_color(s_countdown_layer, s_highlight_bg_color);
    } else {
      // 奇数秒（通常表示）: 常に背景透明・文字黒
      text_layer_set_text_color(s_countdown_layer, GColorBlack);
      text_layer_set_background_color(s_countdown_layer, GColorClear);
    }
  } else {
    snprintf(s_count_buf, sizeof(s_count_buf), "Departed");
    text_layer_set_text_color(s_countdown_layer, GColorBlack);
    text_layer_set_background_color(s_countdown_layer, GColorClear);
  }
  text_layer_set_text(s_countdown_layer, s_count_buf);
}

static void tick_handler(struct tm *t, TimeUnits u) { update_time(); update_countdown(); }

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

  if(st_t) text_layer_set_text(s_station_layer, st_t->value->cstring);
  if(de_t) text_layer_set_text(s_dest_layer, de_t->value->cstring);
  if(n1_t) {
    static char s_note_buf[128];
    strncpy(s_note_buf, n1_t->value->cstring, sizeof(s_note_buf));
    replace_newline(s_note_buf);
    text_layer_set_text(s_note1_layer, s_note_buf);
  }
  
  if(cl_t) {
#if defined(PBL_BW)
    // モノクロ機は、設定値が何であっても強制的に固定
    s_highlight_bg_color = GColorBlack;
    s_highlight_text_color = GColorWhite;
#else
    // カラー機は、設定値を正しく反映
    GColor raw_color = GColorFromHEX(cl_t->value->int32);
    s_highlight_bg_color = raw_color;
    s_highlight_text_color = is_dark_color(s_highlight_bg_color) ? GColorWhite : GColorBlack;
    
    text_layer_set_background_color(s_time_layer, s_highlight_bg_color);
    text_layer_set_text_color(s_time_layer, s_highlight_text_color);
#endif
  }

  bool has_train = true;
  if(hr_t) { s_target_hour = (int)hr_t->value->int32; if (s_target_hour == -1) has_train = false; }
  if(mn_t) s_target_min = (int)mn_t->value->int32;

  if(ic_t) {
    int icon_id = (int)ic_t->value->int32;
    if(s_icon_bitmap) gbitmap_destroy(s_icon_bitmap);
    uint32_t res_id;
    switch(icon_id) {
      case 1: res_id = RESOURCE_ID_IMAGE_ICON_1; break;
      case 2: res_id = RESOURCE_ID_IMAGE_ICON_2; break;
      case 3: res_id = RESOURCE_ID_IMAGE_ICON_3; break;
      case 4: res_id = RESOURCE_ID_IMAGE_ICON_4; break;
      case 5: res_id = RESOURCE_ID_IMAGE_ICON_5; break;
      default: res_id = RESOURCE_ID_IMAGE_ICON_1;
    }
    s_icon_bitmap = gbitmap_create_with_resource(res_id);
    bitmap_layer_set_bitmap(s_icon_layer, s_icon_bitmap);
  }

  s_vibrated_1min = s_vibrated_0min = false;
  s_data_received = true;
  
  static char s_dep_buf[16];
  if (has_train) {
    snprintf(s_dep_buf, sizeof(s_dep_buf), "Dep: %02d:%02d", s_target_hour % 24, s_target_min);
    text_layer_set_text(s_depart_layer, s_dep_buf);
  } else {
    text_layer_set_text(s_depart_layer, ""); 
    text_layer_set_text(s_countdown_layer, "Departed");
  }

  Layer *root = window_get_root_layer(s_main_window);
  GRect b = layer_get_bounds(root);
  int w = b.size.w; int h = b.size.h;

  animate_layer(text_layer_get_layer(s_countdown_layer), 
                GRect(0, (int)(h * 0.45), w, (int)(h * 0.19)), 
                s_countdown_frame, 300, 0);
  if (has_train) {
    animate_layer(text_layer_get_layer(s_dest_layer), 
                  GRect(0, (int)(h * 0.28), w, (int)(h * 0.13)), 
                  GRect(0, (int)(h * 0.26), w, (int)(h * 0.13)), 300, 100);
  }
  animate_layer(text_layer_get_layer(s_note1_layer), 
                GRect(0, (int)(h * 0.72), w, (int)(h * 0.24)), 
                GRect(0, (int)(h * 0.70), w, (int)(h * 0.24)), 300, 200);

  update_countdown();
}

static void up_click_handler(ClickRecognizerRef r, void *c) { request_train(MESSAGE_KEY_KEY_REQUEST_PREV); }
static void down_click_handler(ClickRecognizerRef r, void *c) { request_train(MESSAGE_KEY_KEY_REQUEST_NEXT); }
static void center_click_handler(ClickRecognizerRef r, void *c) { request_train(MESSAGE_KEY_KEY_REQUEST_SWITCH); }

static void click_config_provider(void *c) {
  window_single_click_subscribe(BUTTON_ID_UP, up_click_handler);
  window_single_click_subscribe(BUTTON_ID_DOWN, down_click_handler);
  window_single_click_subscribe(BUTTON_ID_SELECT, center_click_handler);
}

// Layout（初期配置と定数定義）を担当
static void main_window_load(Window *window) {
  Layer *w_layer = window_get_root_layer(window);
  GRect bounds = layer_get_bounds(w_layer);
  int w = bounds.size.w; int h = bounds.size.h;

  s_highlight_bg_color = GColorBlack;
  s_highlight_text_color = GColorWhite;

  // 各レイヤーの基本フレームを確定
  s_countdown_frame = GRect(0, (int)(h * 0.40), w, (int)(h * 0.19));

  // --- プラットフォーム別レイアウト設定 ---
#if defined(PBL_ROUND)
  // Chalk (Round) 用: 円形のカーブを避けるため内側に寄せる
  GRect icon_rect    = GRect(28, (int)(h * 0.12), 23, 23);
  GRect station_rect = GRect(53, (int)(h * 0.13), w - 81, (int)(h * 0.12));
#else
  // 矩形 (Basalt/Diorite/Emery等) 用: 従来の左詰め
  GRect icon_rect    = GRect(10, (int)(h * 0.12), 23, 23);
  GRect station_rect = GRect(35, (int)(h * 0.13), w - 35, (int)(h * 0.12));
#endif
  // ---------------------------------------

  s_time_layer = text_layer_create(GRect(0, 0, w, (int)(h * 0.12)));
  s_icon_layer = bitmap_layer_create(icon_rect);
  s_station_layer = text_layer_create(station_rect);
  
  s_dest_layer = text_layer_create(GRect(0, (int)(h * 0.26), w, (int)(h * 0.13)));
  s_countdown_layer = text_layer_create(s_countdown_frame);
  s_depart_layer = text_layer_create(GRect(0, (int)(h * 0.58), w, (int)(h * 0.13)));
  s_note1_layer = text_layer_create(GRect(0, (int)(h * 0.72), w, (int)(h * 0.22)));

  text_layer_set_background_color(s_time_layer, GColorBlack);
  text_layer_set_text_color(s_time_layer, GColorWhite);
  bitmap_layer_set_compositing_mode(s_icon_layer, GCompOpSet);
  layer_add_child(w_layer, bitmap_layer_get_layer(s_icon_layer));

  TextLayer *layers[] = {s_time_layer, s_station_layer, s_dest_layer, s_countdown_layer, s_depart_layer, s_note1_layer};
  for(int i = 0; i < 6; i++) {
    text_layer_set_text_alignment(layers[i], (i == 1) ? GTextAlignmentLeft : GTextAlignmentCenter);
    text_layer_set_font(layers[i], fonts_get_system_font((i == 3) ? FONT_KEY_GOTHIC_28_BOLD : FONT_KEY_GOTHIC_18_BOLD));
    layer_add_child(w_layer, text_layer_get_layer(layers[i]));
  }
}

static void main_window_unload(Window *window) {
  text_layer_destroy(s_time_layer); text_layer_destroy(s_station_layer);
  text_layer_destroy(s_dest_layer); text_layer_destroy(s_countdown_layer);
  text_layer_destroy(s_depart_layer); text_layer_destroy(s_note1_layer);
  bitmap_layer_destroy(s_icon_layer);
  if(s_icon_bitmap) gbitmap_destroy(s_icon_bitmap);
}

static void init() {
  s_main_window = window_create();
  window_set_window_handlers(s_main_window, (WindowHandlers) {.load = main_window_load, .unload = main_window_unload});
  window_stack_push(s_main_window, true);
  window_set_click_config_provider(s_main_window, click_config_provider);
  app_message_register_inbox_received(inbox_received_callback);
  app_message_open(256, 128); 
  tick_timer_service_subscribe(SECOND_UNIT, tick_handler);
}

static void deinit() { tick_timer_service_unsubscribe(); window_destroy(s_main_window); }
int main() { init(); app_event_loop(); deinit(); }