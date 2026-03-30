#include <pebble.h>

static Window *s_main_window;
static TextLayer *s_time_layer, *s_station_layer, *s_type_layer, *s_dest_layer, *s_countdown_layer, *s_depart_layer, *s_note1_layer;
static BitmapLayer *s_icon_layer;
static GBitmap *s_icon_bitmap = NULL;

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

static void set_layer_layout(int index, TextLayer *layer, int width, const char *text) {
#if defined(PBL_PLATFORM_EMERY)
  return; 
#else
  Layer *l = text_layer_get_layer(layer);
  GRect frame;

  // 常に標準の座標（中央揃え用）を適用
  #if defined(PBL_ROUND)
    if (index == 1)      frame = GRect(53, 23, 180 - 81, 21); // 駅名
    else if (index == 3) frame = GRect(0, 46, 180, 23);        // 行先
    else                 frame = layer_get_frame(l);
  #else
    if (index == 1)      frame = GRect(41, 21, 144 - 41, 20);
    else if (index == 3) frame = GRect(0, 42, 144, 20);
    else                 frame = layer_get_frame(l);
  #endif

  // すべて中央揃えに統一（駅名 index 1 を左寄せにしたい場合はここを調整）
  text_layer_set_text_alignment(layer, (index == 1) ? GTextAlignmentLeft : GTextAlignmentCenter);

  // 枠とサイズを確定させ、溢れたら「...」を出す設定を維持
  layer_set_frame(l, frame);
  text_layer_set_size(layer, frame.size);
  layer_set_bounds(l, GRect(0, 0, frame.size.w, frame.size.h));
  text_layer_set_overflow_mode(layer, GTextOverflowModeTrailingEllipsis);
#endif
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
      text_layer_set_text_color(s_countdown_layer, s_highlight_text_color);
      text_layer_set_background_color(s_countdown_layer, s_highlight_bg_color);
    } else {
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
  Layer *root = window_get_root_layer(s_main_window);
  GRect b = layer_get_bounds(root);
  int w = b.size.w;
  Tuple *ic_t = dict_find(iterator, MESSAGE_KEY_KEY_ICON);
  Tuple *de_t = dict_find(iterator, MESSAGE_KEY_KEY_DEST);
  Tuple *n1_t = dict_find(iterator, MESSAGE_KEY_KEY_NOTE1);
  Tuple *hr_t = dict_find(iterator, MESSAGE_KEY_KEY_HOUR);
  Tuple *mn_t = dict_find(iterator, MESSAGE_KEY_KEY_MIN);
  Tuple *cl_t = dict_find(iterator, MESSAGE_KEY_KEY_HIGHLIGHT_COLOR);
  Tuple *ty_t = dict_find(iterator, MESSAGE_KEY_KEY_TYPE_TEXT);
	#if defined(PBL_PLATFORM_EMERY)
  Tuple *tc_t = dict_find(iterator, MESSAGE_KEY_KEY_TYPE_COLOR);
  Tuple *tb_t = dict_find(iterator, MESSAGE_KEY_KEY_TYPE_BG_COLOR);
	#endif

  static char s_st_buf[64];
  static char s_ty_buf[64];
  static char s_de_buf[128];
  static char s_n1_buf[128];

  // 1. 駅名
  if(st_t) {
    strncpy(s_st_buf, st_t->value->cstring, sizeof(s_st_buf) - 1);
    text_layer_set_text(s_station_layer, s_st_buf);
    // 引数にバッファを追加
    set_layer_layout(1, s_station_layer, w - 35, s_st_buf);
  }
  
  // 2. 列車種別
  if(ty_t) {
    strncpy(s_ty_buf, ty_t->value->cstring, sizeof(s_ty_buf) - 1);
    text_layer_set_text(s_type_layer, s_ty_buf);
    set_layer_layout(2, s_type_layer, w, s_ty_buf);
  } else {
    s_ty_buf[0] = '\0';
    text_layer_set_text(s_type_layer, s_ty_buf);
    // データがない場合も呼び出してレイヤーの状態をリセット
    set_layer_layout(2, s_type_layer, w, s_ty_buf);
  }

  // 3. 行先
  if(de_t) {
    strncpy(s_de_buf, de_t->value->cstring, sizeof(s_de_buf) - 1);
    text_layer_set_text(s_dest_layer, s_de_buf);
    // 引数にバッファを追加（非Emeryではアニメーションと競合するがアライメント設定に必要）
    set_layer_layout(3, s_dest_layer, w, s_de_buf);
  }

#if defined(PBL_PLATFORM_EMERY)
  if(tc_t && tb_t) {
    text_layer_set_text_color(s_type_layer, GColorFromHEX(tc_t->value->int32));
    text_layer_set_background_color(s_type_layer, GColorFromHEX(tb_t->value->int32));
  } else {
    text_layer_set_text_color(s_type_layer, GColorBlack);
    text_layer_set_background_color(s_type_layer, GColorClear);
  }
#endif

  // 4. 補足情報
  if(n1_t) {
    strncpy(s_n1_buf, n1_t->value->cstring, sizeof(s_n1_buf) - 1);
    replace_newline(s_n1_buf);
    text_layer_set_text(s_note1_layer, s_n1_buf);
  }
  
  // 5. ハイライトカラー
  if(cl_t) {
#if defined(PBL_BW)
    s_highlight_bg_color = GColorBlack;
    s_highlight_text_color = GColorWhite;
#else
    GColor raw_color = GColorFromHEX(cl_t->value->int32);
    s_highlight_bg_color = raw_color;
    s_highlight_text_color = is_dark_color(s_highlight_bg_color) ? GColorWhite : GColorBlack;
    text_layer_set_background_color(s_time_layer, s_highlight_bg_color);
    text_layer_set_text_color(s_time_layer, s_highlight_text_color);
#endif
  }

  // 6. 出発時刻判定
  bool has_train = true;
  if(hr_t) { 
    s_target_hour = (int)hr_t->value->int32; 
    if (s_target_hour == -1) has_train = false; 
  }
  if(mn_t) s_target_min = (int)mn_t->value->int32;

  // 7. アイコン
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
  
  // 8. Depart表示の更新
  static char s_dep_buf[16];
  if (has_train) {
    snprintf(s_dep_buf, sizeof(s_dep_buf), "Dep: %02d:%02d", s_target_hour % 24, s_target_min);
    text_layer_set_text(s_depart_layer, s_dep_buf);
  } else {
    text_layer_set_text(s_depart_layer, ""); 
    text_layer_set_text(s_countdown_layer, "Departed");
  }

#if !defined(PBL_PLATFORM_EMERY)
  int h = b.size.h;
#endif

  // 9. アニメーション実行
#if defined(PBL_PLATFORM_EMERY)
  if (has_train) {
    animate_layer(text_layer_get_layer(s_type_layer), GRect(0, 52, w, 24), GRect(0, 49, w, 24), 300, 100);
    animate_layer(text_layer_get_layer(s_dest_layer), GRect(0, 76, w, 24), GRect(0, 73, w, 24), 300, 150);
  }
  animate_layer(text_layer_get_layer(s_countdown_layer), GRect(0, 100, w, 32), GRect(0, 97, w, 32), 300, 0);
  animate_layer(text_layer_get_layer(s_note1_layer), GRect(0, 156, w, 75), GRect(0, 153, w, 75), 300, 200);
#else
  // 非Emeryではアニメーションがレイアウト設定（座標）を上書きする
  animate_layer(text_layer_get_layer(s_countdown_layer), GRect(0, (int)(h * 0.45), w, (int)(h * 0.19)), s_countdown_frame, 300, 0);
  if (has_train) {
    animate_layer(text_layer_get_layer(s_dest_layer), GRect(0, (int)(h * 0.28), w, (int)(h * 0.13)), GRect(0, (int)(h * 0.26), w, (int)(h * 0.13)), 300, 100);
  }
  animate_layer(text_layer_get_layer(s_note1_layer), GRect(0, (int)(h * 0.72), w, (int)(h * 0.24)), GRect(0, (int)(h * 0.70), w, (int)(h * 0.24)), 300, 200);
#endif

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

static void main_window_load(Window *window) {
  Layer *w_layer = window_get_root_layer(window);
  GRect bounds = layer_get_bounds(w_layer);
  int w = bounds.size.w;
#if !defined(PBL_PLATFORM_EMERY)
  int h = bounds.size.h;
#endif

  s_highlight_bg_color = GColorBlack;
  s_highlight_text_color = GColorWhite;

#if defined(PBL_PLATFORM_EMERY)
  GRect rect_time       = GRect(0, 0, w, 24);
  GRect rect_icon       = GRect(10, 25, 23, 23);
  GRect rect_station    = GRect(35, 24, w - 35, 25);
  GRect rect_type       = GRect(0, 49, w, 24);
  GRect rect_dest       = GRect(0, 73, w, 24);
  GRect rect_countdown  = GRect(0, 97, w, 32);
  GRect rect_depart     = GRect(0, 129, w, 24);
  GRect rect_note1      = GRect(0, 153, w, 75);
  s_countdown_frame = rect_countdown;
#else
  s_countdown_frame = GRect(0, (int)(h * 0.40), w, (int)(h * 0.19));
  #if defined(PBL_ROUND)
    GRect rect_icon    = GRect(28, (int)(h * 0.12), 23, 23);
    GRect rect_station = GRect(53, (int)(h * 0.13), w - 81, (int)(h * 0.12));
  #else
    GRect rect_icon    = GRect(10, (int)(h * 0.12), 23, 23);
    GRect rect_station = GRect(35, (int)(h * 0.13), w - 35, (int)(h * 0.12));
  #endif
  GRect rect_time      = GRect(0, 0, w, (int)(h * 0.12));
  GRect rect_type      = GRect(0, 0, 0, 0);
  GRect rect_dest      = GRect(0, (int)(h * 0.26), w, (int)(h * 0.13));
  GRect rect_countdown = s_countdown_frame;
  GRect rect_depart    = GRect(0, (int)(h * 0.58), w, (int)(h * 0.13));
  GRect rect_note1     = GRect(0, (int)(h * 0.72), w, (int)(h * 0.24));
#endif

  s_time_layer      = text_layer_create(rect_time);
  s_station_layer   = text_layer_create(rect_station);
  s_type_layer      = text_layer_create(rect_type);
  s_dest_layer      = text_layer_create(rect_dest);
  s_countdown_layer = text_layer_create(rect_countdown);
  s_depart_layer    = text_layer_create(rect_depart);
  s_note1_layer     = text_layer_create(rect_note1);
  s_icon_layer      = bitmap_layer_create(rect_icon);

  text_layer_set_background_color(s_time_layer, GColorBlack);
  text_layer_set_text_color(s_time_layer, GColorWhite);
  bitmap_layer_set_compositing_mode(s_icon_layer, GCompOpSet);
  layer_add_child(w_layer, bitmap_layer_get_layer(s_icon_layer));

  TextLayer *layers[] = {
    s_time_layer, s_station_layer, s_type_layer, s_dest_layer, 
    s_countdown_layer, s_depart_layer, s_note1_layer
  };

  for(int i = 0; i < 7; i++) {
    text_layer_set_text_alignment(layers[i], (i == 1) ? GTextAlignmentLeft : GTextAlignmentCenter);
    text_layer_set_font(layers[i], fonts_get_system_font((i == 4) ? FONT_KEY_GOTHIC_28_BOLD : FONT_KEY_GOTHIC_18_BOLD));
    if(i == 6) {
      text_layer_set_overflow_mode(layers[i], GTextOverflowModeWordWrap);
    } else {
      text_layer_set_overflow_mode(layers[i], GTextOverflowModeTrailingEllipsis);
    }
    layer_add_child(w_layer, text_layer_get_layer(layers[i]));
  }
}

static void main_window_unload(Window *window) {
  text_layer_destroy(s_time_layer); text_layer_destroy(s_station_layer);
  text_layer_destroy(s_dest_layer); text_layer_destroy(s_countdown_layer);
  text_layer_destroy(s_depart_layer); text_layer_destroy(s_note1_layer);
  text_layer_destroy(s_type_layer); bitmap_layer_destroy(s_icon_layer);
  if(s_icon_bitmap) gbitmap_destroy(s_icon_bitmap);
}

static void init() {
  s_main_window = window_create();
  window_set_window_handlers(s_main_window, (WindowHandlers) {.load = main_window_load, .unload = main_window_unload});
  window_stack_push(s_main_window, true);
  window_set_click_config_provider(s_main_window, click_config_provider);
  app_message_register_inbox_received(inbox_received_callback);
  app_message_open(512, 128); 
  tick_timer_service_subscribe(SECOND_UNIT, tick_handler);
}

static void deinit() { tick_timer_service_unsubscribe(); window_destroy(s_main_window); }
int main() { init(); app_event_loop(); deinit(); }