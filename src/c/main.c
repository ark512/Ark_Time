#include <pebble.h>

static Window *s_main_window;
static TextLayer *s_time_layer, *s_step_layer, *s_text_layer, *s_weather_layer, *s_date_layer, *s_dow_layer;
//static GFont s_time_font;
static int s_battery_level;
static Layer *s_battery_layer;
static Layer *s_canvas_layer;
static char s_current_time_buffer[8], s_current_steps_buffer[16];
static int s_step_count = 0, s_step_goal = 3000, s_step_average = 1500;

//Ark Functions
static int bat[] = {12, 80, 1}; //width, height, vertical
bool keyfc, keydSteps, useLoc, show_date=true, show_day_of_week=true;
//end Ark Functions


//begin weather
static void inbox_received_callback(DictionaryIterator *iterator, void *context) {
  // Store incoming information
  static char temperatureF_buffer[8];
  static char temperatureC_buffer[8];
  static char conditions_buffer[32];
  static char weather_layer_buffer[32];
  
  //Config calls
  Tuple *keyfc_t = dict_find(iterator, MESSAGE_KEY_FC);
  if(keyfc_t) {
    APP_LOG(APP_LOG_LEVEL_INFO, "Fer or Cel %d", keyfc_t->value->uint8);
    keyfc = keyfc_t->value->uint8;
    persist_write_int(MESSAGE_KEY_FC, keyfc);
  }
  
  Tuple *keydSteps_t = dict_find(iterator, MESSAGE_KEY_dSteps);
  if(keydSteps_t) {
    APP_LOG(APP_LOG_LEVEL_INFO, "Disable Steps %d", keydSteps_t->value->uint8);
    keydSteps = keydSteps_t->value->uint8;
    persist_write_int(MESSAGE_KEY_dSteps, keydSteps);
  }
  
  Tuple *useLoc_t = dict_find(iterator, MESSAGE_KEY_UseLoc);
  if(useLoc_t) {
    APP_LOG(APP_LOG_LEVEL_INFO, "Disable Steps %d", useLoc_t->value->uint8);
    useLoc = useLoc_t->value->uint8;
    persist_write_int(MESSAGE_KEY_UseLoc, useLoc);
  }
  //End Config calls
  
  Tuple *temp_tuple = NULL;
  
  // Read tuples for data
  if(keyfc){
    temp_tuple = dict_find(iterator, MESSAGE_KEY_KEY_TEMPERATUREF);
  }
  else
  {
    temp_tuple = dict_find(iterator, MESSAGE_KEY_KEY_TEMPERATUREC);
  }
  
  Tuple *conditions_tuple = dict_find(iterator, MESSAGE_KEY_KEY_CONDITIONS);

  // If all data is available, use it
   char *a_temp;
  if(temp_tuple && conditions_tuple) {
    if(keyfc){
      snprintf(temperatureF_buffer, sizeof(temperatureF_buffer), "%d F", (int)temp_tuple->value->int32);
      a_temp = temperatureF_buffer;
    }
    else{
      snprintf(temperatureC_buffer, sizeof(temperatureC_buffer), "%d C", (int)temp_tuple->value->int32);
      a_temp = temperatureC_buffer;
    }
    snprintf(conditions_buffer, sizeof(conditions_buffer), "%s", conditions_tuple->value->cstring);

    // Assemble full string and display
    snprintf(weather_layer_buffer, sizeof(weather_layer_buffer), "%s, %s", a_temp, conditions_buffer);
    text_layer_set_text(s_weather_layer, weather_layer_buffer);
  }
}

static void inbox_dropped_callback(AppMessageResult reason, void *context) {
  APP_LOG(APP_LOG_LEVEL_ERROR, "Message dropped!");
}

static void outbox_failed_callback(DictionaryIterator *iterator, AppMessageResult reason, void *context) {
  APP_LOG(APP_LOG_LEVEL_ERROR, "Outbox send failed!");
}

static void outbox_sent_callback(DictionaryIterator *iterator, void *context) {
  APP_LOG(APP_LOG_LEVEL_INFO, "Outbox send success!");
}
//end Weather

//begin time
static void update_time() {
  // Get a tm structure
  time_t temp = time(NULL);
  struct tm *tick_time = localtime(&temp);

  // Write the current hours and minutes into a buffer
  static char s_buffer[8];
  strftime(s_buffer, sizeof(s_buffer), clock_is_24h_style() ?
                                          "%H:%M" : "%I:%M", tick_time);

  // Display this time on the TextLayer
  text_layer_set_text(s_time_layer, s_buffer);
  
  static char s_date_text[] = "Xxxx 00";
  
  strftime(s_date_text, sizeof(s_date_text), "%b %e", tick_time);
 
  text_layer_set_text(s_date_layer, s_date_text);  
  
  //update day of week!
  static char week_day[] = "Xxx";
  strftime(week_day,sizeof(week_day),"%a",tick_time);
  text_layer_set_text(s_dow_layer,week_day);
}

static void tick_handler(struct tm *tick_time, TimeUnits units_changed) {
  update_time();
  
  // Get weather update every 30 minutes
  if(tick_time->tm_min % 30 == 0) {
    // Begin dictionary
    DictionaryIterator *iter;
    app_message_outbox_begin(&iter);

    // Add a key-value pair
    dict_write_uint8(iter, 0, 0);

    // Send the message!
    app_message_outbox_send();
  }
}
//end time

//begin health
bool step_data_is_available() {
  return HealthServiceAccessibilityMaskAvailable &
    health_service_metric_accessible(HealthMetricStepCount,
      time_start_of_today(), time(NULL));
}

static void get_step_average() {
  const time_t start = time_start_of_today();
  const time_t end = time(NULL);
  s_step_average = (int)health_service_sum_averaged(HealthMetricStepCount,
    start, end, HealthServiceTimeScopeDaily);
}

static void get_step_count() {
  s_step_count = (int)health_service_sum_today(HealthMetricStepCount);
}

static void display_step_count() {
  int thousands = s_step_count / 1000;
  int hundreds = s_step_count % 1000;
  static char s_emoji[5];

  if(s_step_count >= s_step_average) {
    text_layer_set_text_color(s_step_layer, GColorRed);
    snprintf(s_emoji, sizeof(s_emoji), "\U0001F60C");
  } else {
    text_layer_set_text_color(s_step_layer, GColorElectricBlue);
    snprintf(s_emoji, sizeof(s_emoji), "\U0001F4A9");
  }

  if(thousands > 0) {
    snprintf(s_current_steps_buffer, sizeof(s_current_steps_buffer),
      "%d,%03d %s", thousands, hundreds, s_emoji);
  } else {
    snprintf(s_current_steps_buffer, sizeof(s_current_steps_buffer),
      "%d %s", hundreds, s_emoji);
  }

  text_layer_set_text(s_step_layer, s_current_steps_buffer);
}

static void health_handler(HealthEventType event, void *context) {
  get_step_count();
  display_step_count();
}
//end health

//begin Battery
static void battery_callback(BatteryChargeState state) {
  // Record the new battery level
  s_battery_level = state.charge_percent;
}

static void battery_update_proc(Layer *layer, GContext *ctx) {
  GRect bounds = layer_get_bounds(layer);

  if (!bat[2]){
    
    // Find the width of the bar
    int width = (int)(float)(((float)s_battery_level / 100.0F) * bounds.size.w);
    
    // Draw the background
    graphics_context_set_fill_color(ctx, GColorWhite);
    graphics_fill_rect(ctx, bounds, 0, GCornerNone);
  
    // Draw the bar
    graphics_context_set_fill_color(ctx, GColorRed);
    graphics_fill_rect(ctx, GRect(0, 0, width, bounds.size.h), 0, GCornerNone);
  
    //Draw Separators
    graphics_context_set_fill_color(ctx, GColorBlack);
    graphics_fill_rect(ctx, GRect(bounds.size.w * 0.10, 0, 1, bounds.size.h), 0, 0);
    graphics_fill_rect(ctx, GRect(bounds.size.w * 0.20, 0, 1, bounds.size.h), 0, 0);
    graphics_fill_rect(ctx, GRect(bounds.size.w * 0.30, 0, 1, bounds.size.h), 0, 0);
    graphics_fill_rect(ctx, GRect(bounds.size.w * 0.40, 0, 1, bounds.size.h), 0, 0);
    graphics_fill_rect(ctx, GRect(bounds.size.w * 0.50, 0, 1, bounds.size.h), 0, 0);
    graphics_fill_rect(ctx, GRect(bounds.size.w * 0.60, 0, 1, bounds.size.h), 0, 0);
    graphics_fill_rect(ctx, GRect(bounds.size.w * 0.70, 0, 1, bounds.size.h), 0, 0);
    graphics_fill_rect(ctx, GRect(bounds.size.w * 0.80, 0, 1, bounds.size.h), 0, 0);
    graphics_fill_rect(ctx, GRect(bounds.size.w * 0.90, 0, 1, bounds.size.h), 0, 0); 
  }
  else
    {
    // Find the width of the bar
    int width = (int)(float)(((float)s_battery_level / 100.0F) * bounds.size.h);
    
    // Draw the background
    graphics_context_set_fill_color(ctx, GColorWhite);
    graphics_fill_rect(ctx, bounds, 0, GCornerNone);
  
    // Draw the bar
    graphics_context_set_fill_color(ctx, GColorRed);
    graphics_fill_rect(ctx, GRect(0, bounds.size.h-width, bounds.size.w, width), 0, GCornerNone);
  
    //Draw Separators
    graphics_context_set_fill_color(ctx, GColorBlack);
    graphics_fill_rect(ctx, GRect(0, bounds.size.h * 0.10, bounds.size.w, 1), 0, 0);
    graphics_fill_rect(ctx, GRect(0, bounds.size.h * 0.20, bounds.size.w, 1), 0, 0);
    graphics_fill_rect(ctx, GRect(0, bounds.size.h * 0.30, bounds.size.w, 1), 0, 0);
    graphics_fill_rect(ctx, GRect(0, bounds.size.h * 0.40, bounds.size.w, 1), 0, 0);
    graphics_fill_rect(ctx, GRect(0, bounds.size.h * 0.50, bounds.size.w, 1), 0, 0);
    graphics_fill_rect(ctx, GRect(0, bounds.size.h * 0.60, bounds.size.w, 1), 0, 0);
    graphics_fill_rect(ctx, GRect(0, bounds.size.h * 0.70, bounds.size.w, 1), 0, 0);
    graphics_fill_rect(ctx, GRect(0, bounds.size.h * 0.80, bounds.size.w, 1), 0, 0);
    graphics_fill_rect(ctx, GRect(0, bounds.size.h * 0.90, bounds.size.w, 1), 0, 0); 
  }
}
//end battery

static void tab (GContext *ctx, float beginx, float beginy, float width, float height, float end, GColor primary, float stroke){
  
  graphics_context_set_stroke_color(ctx, primary);
  graphics_context_set_stroke_width(ctx, stroke);
  //primary line
  graphics_draw_line(ctx, GPoint(beginx, beginy), GPoint(beginx+width, beginy));
  graphics_draw_line(ctx, GPoint(beginx+width, beginy), GPoint(beginx+(width*2), beginy+height));
  graphics_draw_line(ctx, GPoint(beginx+(width*2), beginy+height), GPoint(end, beginy+height));
  
  //adds
  graphics_draw_line(ctx, GPoint(beginx+width, beginy), GPoint(beginx+width+stroke, beginy));
  graphics_draw_line(ctx, GPoint(beginx+width+stroke, beginy), GPoint(beginx+(width*2)+stroke, beginy+height));
  graphics_draw_line(ctx, GPoint(beginx+(end*0.65), beginy+height), GPoint(beginx+(end*0.65)+(stroke*3), beginy+height*0.9));
  graphics_draw_line(ctx, GPoint(beginx+(end*0.65)+(stroke*3), beginy+height*0.9), GPoint(end, beginy+height*0.9));
}

static void taba (GContext *ctx, float beginx, float beginy, float width, float height, float end, GColor primary, int stroke){
  graphics_context_set_stroke_color(ctx, primary);
  graphics_context_set_stroke_width(ctx, stroke);
  graphics_draw_line(ctx, GPoint(beginx, beginy), GPoint(beginx+width, beginy+height));
  graphics_draw_line(ctx, GPoint(beginx+width, beginy+height), GPoint(beginx+width, end));
}

static void tabb (GContext *ctx, float beginx, float beginy, float width, float height, float end, GColor primary, int stroke){
  graphics_context_set_stroke_color(ctx, primary);
  graphics_context_set_stroke_width(ctx, stroke);
  graphics_draw_line(ctx, GPoint(beginx, beginy), GPoint(beginx+width, beginy));
  graphics_draw_line(ctx, GPoint(beginx+width, beginy), GPoint(beginx+(width*2), beginy+height));
  graphics_draw_line(ctx, GPoint(beginx+(width*2), beginy+height), GPoint(end, beginy+height));
}

static void canvas_update_proc(Layer *layer, GContext *ctx) {
  // Custom drawing happens here!
  GRect bounds = layer_get_bounds(layer);
  
  // Set the stroke width (must be an odd integer value)
  graphics_context_set_stroke_width(ctx, 2);

  // Disable antialiasing (enabled by default where available)
  graphics_context_set_antialiased(ctx, false);
  
  taba(ctx, bounds.size.w*0.2, bounds.size.h*0.2, 15, -20, 0, GColorRed, 1);
  taba(ctx, bounds.size.w*0.21, bounds.size.h*0.21, 16, -21, 0, GColorRed, 1);
  taba(ctx, bounds.size.w*0.2, bounds.size.h*0.8, 15, 20, bounds.size.h, GColorRed, 1);
  taba(ctx, bounds.size.w*0.21, bounds.size.h*0.79, 16, 21, bounds.size.h, GColorRed, 1);
  
  tab(ctx, 0, bounds.size.h*0.20, 15, 30, bounds.size.w, GColorWhite, 2);
  tab(ctx, 0, bounds.size.h*0.80, 15, -30, bounds.size.w, GColorWhite, 2);
  
  //blue
  tabb(ctx, 5, bounds.size.h*0.83, 15, -30, bounds.size.w*0.5, GColorBlueMoon, 2);
  graphics_context_set_fill_color(ctx, GColorBlueMoon);
  graphics_fill_rect(ctx, GRect(0, bounds.size.h*0.83-2, 3, 5), 0, 0);
  graphics_fill_rect(ctx, GRect(bounds.size.w*0.5+3, (bounds.size.h*0.83-2)-30, 5, 5), 0, 0);
  
  tabb(ctx, 5, bounds.size.h*0.17, 15, 30, bounds.size.w*0.5, GColorBlueMoon, 2);
  graphics_fill_rect(ctx, GRect(0, bounds.size.h*0.17-2, 3, 5), 0, 0);
  graphics_fill_rect(ctx, GRect(bounds.size.w*0.5+3, (bounds.size.h*0.17-2)+30, 5, 5), 0, 0);
  
  //gray
  tabb(ctx, 0, bounds.size.h*0.3, 15, 20, bounds.size.w, GColorDarkGray, 1);
  tabb(ctx, 0, bounds.size.h*0.35, 15, 15, bounds.size.w, GColorDarkGray, 1);
  tabb(ctx, 0, bounds.size.h*0.4, 15, 10, bounds.size.w, GColorDarkGray, 1);
  tabb(ctx, 0, bounds.size.h*0.45, 15, 5, bounds.size.w, GColorDarkGray, 1);
  tabb(ctx, 0, bounds.size.h*0.5, 15, 0, bounds.size.w, GColorDarkGray, 1);
  tabb(ctx, 0, bounds.size.h*0.55, 15, -5, bounds.size.w, GColorDarkGray, 1);
  tabb(ctx, 0, bounds.size.h*0.6, 15, -10, bounds.size.w, GColorDarkGray, 1);
  tabb(ctx, 0, bounds.size.h*0.65, 15, -15, bounds.size.w, GColorDarkGray, 1);
  tabb(ctx, 0, bounds.size.h*0.7, 15, -20, bounds.size.w, GColorDarkGray, 1);
}

static void main_window_load(Window *window) {
  // Get information about the Window
  Layer *window_layer = window_get_root_layer(window);
  GRect bounds = layer_get_bounds(window_layer);
  
  window_set_background_color(window, GColorBlack);
  
  if (persist_exists(MESSAGE_KEY_FC)) {
	  keyfc = persist_read_int(MESSAGE_KEY_FC);
	}
  if (persist_exists(MESSAGE_KEY_dSteps)) {
	  keydSteps = persist_read_int(MESSAGE_KEY_dSteps);
	}
  if (persist_exists(MESSAGE_KEY_UseLoc)) {
	  useLoc = persist_read_int(MESSAGE_KEY_UseLoc);
	}
  
  // Assign the custom drawing procedure
  s_canvas_layer = layer_create(bounds);
  layer_set_update_proc(s_canvas_layer, canvas_update_proc);
  layer_add_child(window_layer, s_canvas_layer);
  layer_mark_dirty(s_canvas_layer);
  
  //ARK Text layer
  s_text_layer = text_layer_create(GRect(0, bounds.size.h*0.9, PBL_IF_ROUND_ELSE(bounds.size.w, bounds.size.w*0.95), 15));
  text_layer_set_background_color(s_text_layer, GColorClear);
  text_layer_set_text_color(s_text_layer, GColorRed);
  text_layer_set_text_alignment(s_text_layer, GTextAlignmentCenter);
  text_layer_set_text(s_text_layer, "ARK");
  text_layer_set_font(s_text_layer, fonts_get_system_font(FONT_KEY_GOTHIC_14 ));
  layer_add_child(window_layer, text_layer_get_layer(s_text_layer));
  

  // Create battery meter Layer & mark dirty
  s_battery_layer = layer_create(GRect(PBL_IF_ROUND_ELSE((bounds.size.w*0.1-bat[0]), bounds.size.w*0.1-bat[0]), PBL_IF_ROUND_ELSE((bounds.size.h*0.725-bat[1]+1), bounds.size.h*0.725-bat[1]+1), bat[0], bat[1]));
  layer_set_update_proc(s_battery_layer, battery_update_proc);
  layer_add_child(window_layer, s_battery_layer);
  layer_mark_dirty(s_battery_layer);
  
  // Create temperature Layer
  s_weather_layer = text_layer_create(GRect(0, PBL_IF_ROUND_ELSE(bounds.size.h*0.15, bounds.size.h*0.15), bounds.size.w, 27));
  text_layer_set_background_color(s_weather_layer, GColorClear);
  text_layer_set_text_color(s_weather_layer, GColorElectricBlue);
  text_layer_set_font(s_weather_layer, fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD));
  text_layer_set_text_alignment(s_weather_layer, PBL_IF_ROUND_ELSE(GTextAlignmentCenter, GTextAlignmentRight));
  text_layer_set_text(s_weather_layer, "Loading...");
  layer_add_child(window_layer, text_layer_get_layer(s_weather_layer));
  
  if(!keydSteps){
   // Step Layer
  s_step_layer = text_layer_create(GRect(0, PBL_IF_ROUND_ELSE(bounds.size.h*0.65, bounds.size.h*0.65), bounds.size.w, 30));
  text_layer_set_background_color(s_step_layer, GColorClear);
  text_layer_set_text_color(s_step_layer, GColorElectricBlue);
  text_layer_set_font(s_step_layer, fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD));
  text_layer_set_text_alignment(s_step_layer, PBL_IF_ROUND_ELSE(GTextAlignmentCenter, GTextAlignmentRight));
  layer_add_child(window_layer, text_layer_get_layer(s_step_layer));

  // Subscribe to health events if we can
  if(step_data_is_available()) {
    health_service_events_subscribe(health_handler, NULL);
  }
    }
  
  //Setup Date Layer
  s_date_layer = text_layer_create(GRect(0, PBL_IF_ROUND_ELSE(bounds.size.h*0.80, bounds.size.h*0.80), bounds.size.w, 30));
  text_layer_set_background_color(s_date_layer, GColorClear);
  text_layer_set_text_color(s_date_layer, GColorElectricBlue); //blueish text
  text_layer_set_text_alignment(s_date_layer, PBL_IF_ROUND_ELSE(GTextAlignmentCenter, GTextAlignmentRight));

  text_layer_set_font(s_date_layer, fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD));
  if (show_date== true)
    layer_add_child(window_layer, text_layer_get_layer(s_date_layer));

  //Create 'DAY OF WEEK layer:
  s_dow_layer = text_layer_create(GRect(0, PBL_IF_ROUND_ELSE(bounds.size.h*0.05, bounds.size.h*0.05), bounds.size.w, 30));
  text_layer_set_background_color(s_dow_layer, GColorClear);
  text_layer_set_text_color(s_dow_layer, GColorElectricBlue); //dark blue text
  text_layer_set_text_alignment(s_dow_layer, PBL_IF_ROUND_ELSE(GTextAlignmentCenter, GTextAlignmentRight));
  text_layer_set_font(s_dow_layer, fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD));
  if (show_day_of_week ==true)
    layer_add_child(window_layer, text_layer_get_layer(s_dow_layer));
  
  //Setup and Make Time layer
  s_time_layer = text_layer_create(GRect(0, PBL_IF_ROUND_ELSE((bounds.size.h*0.5-28), bounds.size.h*0.5-28), PBL_IF_ROUND_ELSE(bounds.size.w-15, bounds.size.w), 60));
  //s_time_font = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_MAGENTA_60));
  text_layer_set_background_color(s_time_layer, GColorClear);
  text_layer_set_text_color(s_time_layer, GColorWhite);
  text_layer_set_text(s_time_layer, "00:00");
  text_layer_set_font(s_time_layer, fonts_get_system_font(FONT_KEY_LECO_42_NUMBERS));
  text_layer_set_text_alignment(s_time_layer, GTextAlignmentRight);
  layer_add_child(window_layer, text_layer_get_layer(s_time_layer));
}

static void main_window_unload(Window *window) {
  // Destroy TextLayer
  text_layer_destroy(s_time_layer);
  text_layer_destroy(s_text_layer);
  text_layer_destroy(s_weather_layer);
  text_layer_destroy(s_date_layer);
  text_layer_destroy(s_dow_layer);
  layer_destroy(s_battery_layer);
  layer_destroy(s_canvas_layer);
  if(!keydSteps){
  layer_destroy(text_layer_get_layer(s_step_layer));
  }
}

static void init() {
  //set default values
  keyfc = false;
  keydSteps = false;
  
  // Create main Window element and assign to pointer
  s_main_window = window_create();

  // Set handlers to manage the elements inside the Window
  window_set_window_handlers(s_main_window, (WindowHandlers) {
    .load = main_window_load,
    .unload = main_window_unload
  });
  
  // Show the Window on the watch, with animated=true
  window_stack_push(s_main_window, true);
  
  // Register with TickTimerService
  tick_timer_service_subscribe(MINUTE_UNIT, tick_handler);
  
  // Make sure the time is displayed from the start
  update_time();
  
  // Register for battery level updates
  battery_state_service_subscribe(battery_callback);
  
  // Ensure battery level is displayed from the start
  battery_callback(battery_state_service_peek());
  
  // Register callbacks
  app_message_register_inbox_received(inbox_received_callback);
  app_message_register_inbox_dropped(inbox_dropped_callback);
  app_message_register_outbox_failed(outbox_failed_callback);
  app_message_register_outbox_sent(outbox_sent_callback);
  // Open AppMessage
  app_message_open(128, 128);
}

static void deinit() {
  // Destroy Window
  window_destroy(s_main_window);
}

int main() {
  init();
  app_event_loop();
  deinit();
}