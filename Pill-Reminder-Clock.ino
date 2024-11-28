/*
  Copyright (c) [2024] [Fabio Rossato]

  This program is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see <https://www.gnu.org/licenses/>.

  This code is licensed under the GPL-3.0 License.
*/

/*
||=============================||
||           TO DO             ||
||=============================||

- add WiFi Manager
- maybe switch reminders logic to json + add callback every minute to check for reminders
- add custom pill names and quantities, maybe switch logic -> Pills = create your pill names, Reminders = set reminders for your pills
- add reminder buzzer
- add load reminders and settings from saved file on start
- tidy up


||=============================||
||          CREDITS            ||
||=============================||

A Huge thank you to Brian Lough for creating a great community on his discord server ( https://discord.gg/wsdEhHyb45 )
as well as to all the server members who always offer advice and helped this project


||=============================||
||         DISCLAIMER          ||
||=============================||

This is a WIP little project by a self taught hobbyist, take it as is

*/

// Wi-Fi credentials
const char* ssid = "YOUR SSID";
const char* password = "YOUR PASSWORD";

#include <lvgl.h>
#include "lv_conf.h"

lv_obj_t * screen[2];

#include <ArduinoJson.h>
#include <FS.h>
#include <LittleFS.h>

#include <iostream>
#include <vector>
#include <memory>
#include <string>

#include <ezTime.h>
Timezone myTZ;
lv_obj_t * clockLabel;

struct PillData {
  char *name;
  char *dosage;
  char *unit;
  int hh;
  int mm;
};


struct Reminders {
    std::vector<char *> name;
    std::vector<char *> dosage;
    std::vector<char *> unit;
    std::vector<int> hh;
    std::vector<int> mm;
};

PillData *tempData = new PillData;
Reminders *snoozed = new Reminders;

#define DISPLAY_TYPE DISPLAY_CYD_543
#define TOUCH_CAPACITIVE
#define TOUCH_SDA 8
#define TOUCH_SCL 4
#define TOUCH_INT 3
#define TOUCH_RST -1
#define TOUCH_MIN_X 1
#define TOUCH_MAX_X 480
#define TOUCH_MIN_Y 1
#define TOUCH_MAX_Y 272

#include <bb_spi_lcd.h>
#include "lv_bb_spi_lcd.h"

#ifdef TOUCH_CAPACITIVE
#include <bb_captouch.h>

BBCapTouch bbct;
uint16_t touchMinX = TOUCH_MIN_X, touchMaxX = TOUCH_MAX_X, touchMinY = TOUCH_MIN_Y, touchMaxY = TOUCH_MAX_Y;
#else
BB_SPI_LCD * lcd;
#endif

#include <WiFi.h>
#include <HTTPClient.h>

uint8_t brightness = 255, oldBrightness = 255;
lv_display_t * disp;
TOUCHINFO ti;

static void slider_event_cb(lv_event_t * e);

static lv_obj_t * bat_label;

float readBatteryVoltage() {
  int adcValue = analogRead(5);
  float vGPIO = (adcValue * 3.3) / 4095.0;
  float vBattery = vGPIO * (1 + (float(33000) / 100000));
  return vBattery;
}

int calculateBatteryPercentage(float voltage) {
  if (voltage >= 4.2) return 100;
  if (voltage <= 3.0) return 0;
  // Linear approximation for voltage between 3.0V and 4.2V
  return (float)((voltage - 3.0) * 100 / (4.2 - 3.0));
}

void connectWifi() {
  // Connect to Wi-Fi
  WiFi.begin(ssid, password);
  Serial.print("Connecting to Wi-Fi");
  while (WiFi.status() != WL_CONNECTED && millis() < 15000) {
    delay(500);
    Serial.print(".");
  }
  Serial.println(" Connected!");
}

void set_custom_theme(void) {
  static lv_theme_t *my_theme;

  my_theme = lv_theme_default_init(
    NULL,
    lv_color_hex(0xFFCC00), //0xFFCC00 0x0088FF 0x008C3F
    lv_color_hex(0x000000),
    LV_THEME_DEFAULT_DARK,
    &lv_font_montserrat_14
  );

  lv_obj_set_style_bg_color(lv_scr_act(), lv_color_hex(0x000000), 0); // Set to a dark background
  lv_obj_set_style_bg_opa(lv_scr_act(), LV_OPA_COVER, 0);

  lv_disp_set_theme(NULL, my_theme);
}

void maybeAdjustBrightness(lv_display_t * disp) {
  if (brightness == oldBrightness) {
    return;
  }

  oldBrightness = brightness;
  lv_bb_spi_lcd_t * dsc = (lv_bb_spi_lcd_t *)lv_display_get_driver_data(disp);
  dsc->lcd->setBrightness(brightness);
  Serial.println("Brightness Adjusted");
}

void touch_read( lv_indev_t * indev, lv_indev_data_t * data ) {

#ifdef TOUCH_CAPACITIVE
  // Capacitive touch needs to be mapped to display pixels
  if(bbct.getSamples(&ti)) {
    //Serial.print("raw touch x: ");
    //Serial.print(ti.x[0]);
    //Serial.print(" y: ");
    //Serial.println(ti.y[0]);

    if(ti.x[0] < touchMinX) touchMinX = ti.x[0];
    if(ti.x[0] > touchMaxX) touchMaxX = ti.x[0];
    if(ti.y[0] < touchMinY) touchMinY = ti.y[0];
    if(ti.y[0] > touchMaxY) touchMaxY = ti.y[0];

    //Map this to the pixel position
    data->point.x = map(ti.x[0], touchMinX, touchMaxX, 1, lv_display_get_horizontal_resolution(NULL)); // X touch mapping
    data->point.y = map(ti.y[0], touchMinY, touchMaxY, 1, lv_display_get_vertical_resolution(NULL)); // Y touch mapping
    data->state = LV_INDEV_STATE_PRESSED;
#else
  // Resistive touch is already mapped by the bb_spi_lcd library
  if(lcd->rtReadTouch(&ti)) {
    data->point.x = ti.x[0];
    data->point.y = ti.y[0];
#endif

    data->state = LV_INDEV_STATE_PRESSED;

    //Serial.print("mapped touch x: ");
    //Serial.print(data->point.x);
    //Serial.print(" y: ");
    //Serial.println(data->point.y);
  } else {
    data->state = LV_INDEV_STATE_RELEASED;
  }
}

void display_snoozed_reminders() {
  if (snoozed->name.size() < 0) {
    return;
  }

  pillReminder(snoozed->name[0], snoozed->dosage[0], snoozed->unit[0], snoozed->hh[0], snoozed->mm[0]);
  snoozed->name.erase(snoozed->name.begin());
  snoozed->dosage.erase(snoozed->dosage.begin());
  snoozed->unit.erase(snoozed->unit.begin());
  snoozed->hh.erase(snoozed->hh.begin());
  snoozed->mm.erase(snoozed->mm.begin());
}

static void reminder_event_cb(lv_event_t * e) {
    lv_obj_t * btn = (lv_obj_t *) lv_event_get_target(e);
    PillData *data = static_cast<PillData *>(lv_event_get_user_data(e));
    tempData->name = data->name;
    tempData->dosage = data->dosage;
    tempData->unit = data->unit;
    tempData->hh = data->hh;
    tempData->mm = data->mm;

    if (btn == lv_obj_get_child(lv_obj_get_parent(btn), 0)) {
      //close msgbox, next day reminder will be handled by display reminders callback
      lv_msgbox_close(lv_obj_get_parent(lv_obj_get_parent(btn)));
      return;
    }

    if (btn == lv_obj_get_child(lv_obj_get_parent(btn), 1)) {      
      //add reminder to snooze queue
      snoozed->name.push_back(data->name);
      snoozed->dosage.push_back(data->dosage);
      snoozed->unit.push_back(data->unit);
      snoozed->hh.push_back(data->hh);
      snoozed->mm.push_back(data->mm);
      //set display snoozed reminders for 30 minutes from now
      myTZ.setEvent(display_snoozed_reminders, myTZ.now() + 1800UL);
      //close msgbox
      lv_msgbox_close(lv_obj_get_parent(lv_obj_get_parent(btn)));
      return;
    }
}

//function to create msgbox with pill reminder
void pillReminder(char * name, char * dosage, char * unit, int hh, int mm) {
  //structure to pass pill as userdata to LVGL
  PillData *data = new PillData;
  data->name = name;
  data->dosage = dosage;
  data->unit = unit;
  data->hh = hh;
  data->mm = mm;

  lv_obj_t * mbox1 = lv_msgbox_create(NULL);
  lv_obj_set_size(mbox1, 440, 240);

  char time [8];
  sprintf(time, " %02d:%02d ", hh, mm);

  std::string title = (std::string) LV_SYMBOL_BELL + (std::string) time + "Medicine reminder";
  std::string reminderText = "It's time to take a medicine!\n\n" + (std::string) LV_SYMBOL_RIGHT + " " + (std::string) name + "\n" + (std::string) LV_SYMBOL_RIGHT + " " + (std::string) dosage + " " + (std::string) unit;

  lv_msgbox_add_title(mbox1, title.c_str());

  lv_obj_t * text = lv_msgbox_add_text(mbox1, reminderText.c_str());
  static lv_style_t style, style_btn1, style_btn2;
  lv_style_init(&style);
  lv_style_init(&style_btn1);
  lv_style_init(&style_btn2);
  //lv_style_set_text_font(&style, &lv_font_montserrat_18);
  lv_style_set_text_font(&style, &lv_font_montserrat_24);
  lv_obj_add_style(text, &style, 0);

  lv_style_set_shadow_width(&style_btn1, 0);
  lv_style_set_shadow_width(&style_btn2, 0);
  lv_style_set_radius(&style_btn1, 4);
  lv_style_set_radius(&style_btn2, 4);
  lv_style_set_text_font(&style_btn1, &lv_font_montserrat_22);
  lv_style_set_text_font(&style_btn2, &lv_font_montserrat_22);
  lv_style_set_bg_color(&style_btn1, lv_palette_main(LV_PALETTE_GREEN));
  lv_style_set_bg_color(&style_btn2, lv_palette_main(LV_PALETTE_RED));

  lv_obj_t * btn1, * btn2;
  btn1 = lv_msgbox_add_footer_button(mbox1, LV_SYMBOL_OK" Done");
  lv_obj_add_style(btn1, &style_btn1, 0);
  lv_obj_set_user_data(btn1, data);
  lv_obj_add_event_cb(btn1, reminder_event_cb, LV_EVENT_CLICKED, data);

  btn2 = lv_msgbox_add_footer_button(mbox1, LV_SYMBOL_REFRESH" Remind later");
  lv_obj_add_style(btn2, &style_btn2, 0);
  lv_obj_set_user_data(btn2, data);
  lv_obj_add_event_cb(btn2, reminder_event_cb, LV_EVENT_CLICKED, data);
  return;
}

void settings_btn_event_cb(lv_event_t *e) {
    lv_scr_load(screen[1]);
}

void main_tabs(void) {
    screen[0] = lv_obj_create(NULL);
    screen[1] = lv_obj_create(NULL);

    lv_screen_load(screen[0]);

    //create clock label
    clockLabel = lv_label_create(lv_screen_active());
    //align center
    lv_obj_align(clockLabel , LV_ALIGN_CENTER, 0, 0);
    //make text bigger
    static lv_style_t style;
    lv_style_init(&style);
    lv_style_set_text_font(&style, &lv_font_montserrat_48);
    lv_obj_add_style(clockLabel, &style, 0);

    lv_obj_t * settingsIcon = lv_image_create(lv_screen_active());
    lv_image_set_src(settingsIcon, LV_SYMBOL_SETTINGS);
    lv_obj_add_flag(settingsIcon, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_align(settingsIcon, LV_ALIGN_TOP_RIGHT, -10, 10);
    
    lv_obj_add_event_cb(settingsIcon, settings_btn_event_cb, LV_EVENT_CLICKED, NULL);
    build_settings_menu();
}


/*
**  ||======================================||
**  ||                                      ||
**  ||              BUILD MENU              ||
**  ||                                      ||
**  ||======================================||
*/

typedef enum {
    LV_MENU_ITEM_BUILDER_VARIANT_1,
    LV_MENU_ITEM_BUILDER_VARIANT_2
} lv_menu_builder_variant_t;

static void back_event_handler(lv_event_t * e);
static void switch_handler(lv_event_t * e);
lv_obj_t * root_page;

static lv_obj_t * create_text(lv_obj_t * parent, const char * icon, const char * txt, lv_menu_builder_variant_t builder_variant);
static lv_obj_t * create_slider(lv_obj_t * parent, const char * icon, const char * txt, int32_t min, int32_t max, int32_t val);
static lv_obj_t * create_switch(lv_obj_t * parent, const char * icon, const char * txt, bool chk);
static lv_obj_t * create_list(lv_obj_t * parent, const char * icon, const char * txt, const char * options);
static lv_obj_t * create_time_select(lv_obj_t * parent, const char * icon);
static lv_obj_t * create_dosage_select(lv_obj_t * parent, const char * icon);

static lv_obj_t * sub_pills_page, * sub_pills_cont, * sub_pills_section;
static lv_obj_t * menu;
static uint32_t btn_cnt = 0;

void build_settings_menu(void) {
    menu = lv_menu_create(screen[1]);

    lv_color_t bg_color = lv_obj_get_style_bg_color(menu, 0);
    if(lv_color_brightness(bg_color) > 127) {
        lv_obj_set_style_bg_color(menu, lv_color_darken(lv_obj_get_style_bg_color(menu, 0), 10), 0);
    }
    else {
        lv_obj_set_style_bg_color(menu, lv_color_darken(lv_obj_get_style_bg_color(menu, 0), 50), 0);
    }
    lv_menu_set_mode_root_back_button(menu, LV_MENU_ROOT_BACK_BUTTON_ENABLED);
    lv_obj_add_event_cb(menu, back_event_handler, LV_EVENT_CLICKED, menu);
    lv_obj_set_size(menu, lv_display_get_horizontal_resolution(NULL), lv_display_get_vertical_resolution(NULL));
    lv_obj_center(menu);
    lv_obj_t * back_btn = lv_menu_get_main_header_back_button(menu);
    lv_obj_t * back_button_label = lv_label_create(back_btn);
    lv_label_set_text(back_button_label, "Back");

    lv_obj_t * cont;
    lv_obj_t * section;

    /*Create sub pages*/
    sub_pills_page = lv_menu_page_create(menu, NULL);
    lv_obj_set_style_pad_hor(sub_pills_page, lv_obj_get_style_pad_left(lv_menu_get_main_header(menu), 0), 0);
    lv_menu_separator_create(sub_pills_page);
    sub_pills_cont = lv_menu_cont_create(sub_pills_page);
    create_add_pill_button();

    lv_obj_t * sub_timezone_page = lv_menu_page_create(menu, NULL);
    lv_obj_set_style_pad_hor(sub_timezone_page, lv_obj_get_style_pad_left(lv_menu_get_main_header(menu), 0), 0);
    lv_menu_separator_create(sub_timezone_page);
    section = lv_menu_section_create(sub_timezone_page);
    create_list(section, LV_SYMBOL_GPS, "Timezone", "CET-12:00\nCET-11:00\nCET-10:00\nCET-9:00\nCET-8:00\nCET-7:00\nCET-6:00\nCET-5:00\nCET-4:00\nCET-3:00\nCET-2:00\nCET-1:00\nCET\nCET+1:00\nCET+2:00\nCET+3:00\nCET+4:00\nCET+5:00\nCET+6:00\nCET+7:00\nCET+8:00\nCET+9:00\nCET+10:00\nCET+11:00\nCET+12:00");

    lv_obj_t * sub_display_page = lv_menu_page_create(menu, NULL);
    lv_obj_set_style_pad_hor(sub_display_page, lv_obj_get_style_pad_left(lv_menu_get_main_header(menu), 0), 0);
    lv_menu_separator_create(sub_display_page);
    section = lv_menu_section_create(sub_display_page);
    create_slider(section, LV_SYMBOL_IMAGE, "Brightness", 1, 255, 255);

    lv_obj_t * sub_menu_mode_page = lv_menu_page_create(menu, NULL);
    lv_obj_set_style_pad_hor(sub_menu_mode_page, lv_obj_get_style_pad_left(lv_menu_get_main_header(menu), 0), 0);
    lv_menu_separator_create(sub_menu_mode_page);
    section = lv_menu_section_create(sub_menu_mode_page);
    cont = create_switch(section, LV_SYMBOL_LIST, "Sidebar enable", true);
    lv_obj_add_event_cb(lv_obj_get_child(cont, 2), switch_handler, LV_EVENT_VALUE_CHANGED, menu);

    /*Create a root page*/
    root_page = lv_menu_page_create(menu, "Back");
    lv_obj_set_style_pad_hor(root_page, lv_obj_get_style_pad_left(lv_menu_get_main_header(menu), 0), 0);
    section = lv_menu_section_create(root_page);
    cont = create_text(section, LV_SYMBOL_BELL, "Pills", LV_MENU_ITEM_BUILDER_VARIANT_1);
    lv_menu_set_load_page_event(menu, cont, sub_pills_page);
    cont = create_text(section, LV_SYMBOL_GPS, "Timezone", LV_MENU_ITEM_BUILDER_VARIANT_1);
    lv_menu_set_load_page_event(menu, cont, sub_timezone_page);
    cont = create_text(section, LV_SYMBOL_IMAGE, "Display", LV_MENU_ITEM_BUILDER_VARIANT_1);
    lv_menu_set_load_page_event(menu, cont, sub_display_page);

    create_text(root_page, NULL, "Other", LV_MENU_ITEM_BUILDER_VARIANT_1);
    section = lv_menu_section_create(root_page);
    cont = create_text(section, LV_SYMBOL_SETTINGS, "Menu mode", LV_MENU_ITEM_BUILDER_VARIANT_1);
    lv_menu_set_load_page_event(menu, cont, sub_menu_mode_page);

    lv_menu_set_sidebar_page(menu, root_page);

    lv_obj_send_event(lv_obj_get_child(lv_obj_get_child(lv_menu_get_cur_sidebar_page(menu), 0), 0), LV_EVENT_CLICKED, NULL);
}

lv_obj_t * pill_buttons[2];

static void add_pill_button_event_cb(lv_event_t * e) {
    LV_UNUSED(e);

    btn_cnt++;

    lv_obj_t * cont;
    lv_obj_t * label;
    lv_obj_t * section;

    if (btn_cnt == 1) {
      //show remove pill button
      lv_obj_clear_flag(pill_buttons[1], LV_OBJ_FLAG_HIDDEN);
    }

    section = lv_menu_section_create(sub_pills_cont);
    lv_obj_add_flag(section, LV_OBJ_FLAG_FLEX_IN_NEW_TRACK);
    cont = lv_menu_cont_create(section);
    label = lv_label_create(cont);
    lv_obj_set_flex_flow(cont, LV_FLEX_FLOW_COLUMN);

    create_time_select(cont, LV_SYMBOL_BELL);

    create_list(cont, LV_SYMBOL_SHUFFLE, NULL, "Blood Pressure\nStatin\nVitamin D\nSleep Drops");

    create_dosage_select(cont, LV_SYMBOL_BATTERY_2);

    lv_label_set_text_fmt(label, "Medicine %"LV_PRIu32"", btn_cnt);

    if (btn_cnt == 20) {
      //hide add pill button when limit is reached
      lv_obj_add_flag(pill_buttons[0], LV_OBJ_FLAG_HIDDEN);
      lv_obj_set_style_pad_bottom(sub_pills_cont, 140, NULL);
    }

    lv_obj_scroll_to_view_recursive(cont, LV_ANIM_ON);
}

static void remove_pill_button_event_cb(lv_event_t * e) {
    LV_UNUSED(e);

    btn_cnt--;

    lv_obj_t * last_pill;   

    last_pill = lv_obj_get_child(sub_pills_cont, -1);
    lv_obj_delete(last_pill);
    last_pill = lv_obj_get_child(sub_pills_cont, -1);

    if (btn_cnt == 0) {
      //hide remove pill button when no pills are set
      lv_obj_add_flag(pill_buttons[1], LV_OBJ_FLAG_HIDDEN);
    } else {
      lv_obj_scroll_to_view_recursive(last_pill, LV_ANIM_ON);
    }

    if (lv_obj_has_flag(pill_buttons[0], LV_OBJ_FLAG_HIDDEN)) {
      //show add pill button again when pills are limit - 1
      lv_obj_clear_flag(pill_buttons[0], LV_OBJ_FLAG_HIDDEN);
    }
}

static void back_event_handler(lv_event_t * e) {
    lv_obj_t * obj = (lv_obj_t *) lv_event_get_target(e);
    lv_obj_t * menu = (lv_obj_t *) lv_event_get_user_data(e);

    if(lv_menu_back_button_is_root(menu, obj)) {
        schedule_reminders_on_save();
        lv_screen_load(screen[0]);
    }
}

static void list_event_handler(lv_event_t * e) {
  lv_event_code_t code = lv_event_get_code(e);
  lv_obj_t * obj = (lv_obj_t *) lv_event_get_target(e);

  if(code == LV_EVENT_VALUE_CHANGED) {
      char buf[32];
      lv_dropdown_get_selected_str(obj, buf, sizeof(buf));
      myTZ.setPosix(buf);
  }
}

static void switch_handler(lv_event_t * e) {
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t * menu = (lv_obj_t *) lv_event_get_user_data(e);
    lv_obj_t * obj = (lv_obj_t *) lv_event_get_target(e);
    if(code == LV_EVENT_VALUE_CHANGED) {
        if(lv_obj_has_state(obj, LV_STATE_CHECKED)) {
            lv_menu_set_page(menu, NULL);
            lv_menu_set_sidebar_page(menu, root_page);
            lv_obj_send_event(lv_obj_get_child(lv_obj_get_child(lv_menu_get_cur_sidebar_page(menu), 0), 0), LV_EVENT_CLICKED, NULL);
        }
        else {
            lv_menu_set_sidebar_page(menu, NULL);
            lv_menu_clear_history(menu); /* Clear history because we will be showing the root page later */
            lv_menu_set_page(menu, root_page);
        }
    }
}

void create_add_pill_button() {
  //create add pill button
  pill_buttons[0] = lv_button_create(sub_pills_page);
  lv_obj_set_size(pill_buttons[0], 50, 50);
  lv_obj_add_flag(pill_buttons[0], LV_OBJ_FLAG_FLOATING);
  lv_obj_align(pill_buttons[0], LV_ALIGN_BOTTOM_RIGHT, 0, -10);
  lv_obj_add_event_cb(pill_buttons[0], add_pill_button_event_cb, LV_EVENT_CLICKED, menu);
  lv_obj_set_style_radius(pill_buttons[0], LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_image_src(pill_buttons[0], LV_SYMBOL_PLUS, 0);
  lv_obj_set_style_text_font(pill_buttons[0], lv_theme_get_font_large(pill_buttons[0]), 0);
  //create remove pill button
  pill_buttons[1] = lv_button_create(sub_pills_page);
  lv_obj_set_size(pill_buttons[1], 50, 50);
  lv_obj_add_flag(pill_buttons[1], LV_OBJ_FLAG_FLOATING);
  lv_obj_align(pill_buttons[1], LV_ALIGN_BOTTOM_RIGHT, 0, -70);
  lv_obj_add_event_cb(pill_buttons[1], remove_pill_button_event_cb, LV_EVENT_CLICKED, menu);
  lv_obj_set_style_radius(pill_buttons[1], LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_image_src(pill_buttons[1], LV_SYMBOL_MINUS, 0);
  lv_obj_set_style_text_font(pill_buttons[1], lv_theme_get_font_large(pill_buttons[1]), 0);
  //hide the remove button for now
  lv_obj_add_flag(pill_buttons[1], LV_OBJ_FLAG_HIDDEN);
}

static lv_obj_t * create_text(lv_obj_t * parent, const char * icon, const char * txt, lv_menu_builder_variant_t builder_variant) {
    lv_obj_t * obj = lv_menu_cont_create(parent);

    lv_obj_t * img = NULL;
    lv_obj_t * label = NULL;

    if(icon) {
        img = lv_image_create(obj);
        lv_image_set_src(img, icon);
    }

    if(txt) {
        label = lv_label_create(obj);
        lv_label_set_text(label, txt);
        lv_label_set_long_mode(label, LV_LABEL_LONG_SCROLL_CIRCULAR);
        lv_obj_set_flex_grow(label, 1);
    }

    if(builder_variant == LV_MENU_ITEM_BUILDER_VARIANT_2 && icon && txt) {
        lv_obj_add_flag(img, LV_OBJ_FLAG_FLEX_IN_NEW_TRACK);
        lv_obj_swap(img, label);
    }

    return obj;
}

static lv_obj_t * create_switch(lv_obj_t * parent, const char * icon, const char * txt, bool chk) {
    lv_obj_t * obj = create_text(parent, icon, txt, LV_MENU_ITEM_BUILDER_VARIANT_1);

    lv_obj_t * sw = lv_switch_create(obj);
    lv_obj_add_state(sw, chk ? LV_STATE_CHECKED : 0);

    return obj;
}

static lv_obj_t * create_slider(lv_obj_t * parent, const char * icon, const char * txt, int32_t min, int32_t max, int32_t val) {
    lv_obj_t * obj = create_text(parent, icon, txt, LV_MENU_ITEM_BUILDER_VARIANT_2);

    lv_obj_t * slider = lv_slider_create(obj);
    lv_obj_set_flex_grow(slider, 1);
    lv_slider_set_range(slider, min, max);
    lv_slider_set_value(slider, val, LV_ANIM_OFF);

    if (txt == "Brightness") {
      lv_obj_add_event_cb(slider, brightness_slider_event_cb, LV_EVENT_VALUE_CHANGED, NULL);
    }

    if(icon == NULL) {
        lv_obj_add_flag(slider, LV_OBJ_FLAG_FLEX_IN_NEW_TRACK);
    }

    return obj;
}

static lv_obj_t * create_list(lv_obj_t * parent, const char * icon, const char * txt, const char * options) {
    lv_obj_t * obj = create_text(parent, icon, txt, LV_MENU_ITEM_BUILDER_VARIANT_2);

    lv_obj_t * list = lv_dropdown_create(obj);
    lv_dropdown_set_options(list, options);
    lv_obj_set_flex_grow(list, 1);

    if (txt == "Timezone") {
      lv_obj_add_event_cb(list, list_event_handler, LV_EVENT_ALL, NULL);
      lv_dropdown_set_selected(list, 12);
    }

    if(icon == NULL) {
        //lv_obj_add_flag(list, LV_OBJ_FLAG_FLEX_IN_NEW_TRACK);
    }

    return obj;
}

static lv_obj_t * create_time_select(lv_obj_t * parent, const char * icon) {    
    lv_obj_t * obj = create_text(parent, icon, NULL, LV_MENU_ITEM_BUILDER_VARIANT_2);
    lv_obj_set_flex_flow(obj, LV_FLEX_FLOW_ROW);

    lv_obj_t * list = lv_dropdown_create(obj);
    lv_dropdown_set_options(list, "00\n1\n2\n3\n4\n5\n6\n7\n8\n9\n10\n11\n12\n13\n14\n15\n16\n17\n18\n19\n20\n21\n22\n23");
    lv_dropdown_set_selected(list, hour(myTZ.now()));
    lv_obj_set_flex_grow(list, 1);

    lv_obj_t * label = lv_label_create(obj);
    lv_label_set_text(label, ":");

    list = lv_dropdown_create(obj);
    lv_dropdown_set_options(list, "00\n01\n02\n03\n04\n05\n06\n07\n08\n09\n10\n11\n12\n13\n14\n15\n16\n17\n18\n19\n20\n21\n22\n23\n24\n25\n26\n27\n28\n29\n30\n31\n32\n33\n34\n35\n36\n37\n38\n39\n40\n41\n42\n43\n44\n45\n46\n47\n48\n49\n50\n51\n52\n53\n54\n55\n56\n57\n58\n59");
    lv_dropdown_set_selected(list, minute(myTZ.now()));
    lv_obj_set_flex_grow(list, 1);

    return obj;
}

static lv_obj_t * create_dosage_select(lv_obj_t * parent, const char * icon) {    
    lv_obj_t * obj = create_text(parent, icon, NULL, LV_MENU_ITEM_BUILDER_VARIANT_2);
    lv_obj_set_flex_flow(obj, LV_FLEX_FLOW_ROW);

    lv_obj_t * list = lv_dropdown_create(obj);
    lv_dropdown_set_options(list, "1/4\n1/2\n1\n2\n3\n4\n5\n6\n7\n8\n9\n10");
    lv_dropdown_set_selected(list, 2);
    lv_obj_set_flex_grow(list, 1);

    lv_obj_t * label = lv_label_create(obj);
    lv_label_set_text(label, "-");

    list = lv_dropdown_create(obj);
    lv_dropdown_set_options(list, "Cpr.\nDrop(s)\nVial(s)\nUnit(s)");
    lv_obj_set_flex_grow(list, 1);

    return obj;
}


static void brightness_slider_event_cb(lv_event_t * e) {
    lv_obj_t * slider = (lv_obj_t *) lv_event_get_target(e);
    brightness = (int) lv_slider_get_value(slider);
    Serial.println(brightness);
}

void setup() {
  Serial.begin(115200);
  while (!Serial) {
    delay(1);
  }

  if (!LittleFS.begin(true)) {
        Serial.println("LittleFS Mount Failed");
        return;
  }
  Serial.println("LittleFS Mount Successful");


  connectWifi();
  analogReadResolution(12);

  // Initialise LVGL
  lv_init();
  lv_tick_set_cb([](){ 
    return (uint32_t) (esp_timer_get_time() / 1000ULL);
  });
  disp = lv_bb_spi_lcd_create(DISPLAY_TYPE);

#ifdef TOUCH_CAPACITIVE
  // Initialize touch screen
  bbct.init(TOUCH_SDA, TOUCH_SCL, TOUCH_RST, TOUCH_INT);
#else
  lv_bb_spi_lcd_t* dsc = (lv_bb_spi_lcd_t *)lv_display_get_driver_data(disp);
  lcd = dsc->lcd;
  lcd->rtInit(TOUCH_MOSI, TOUCH_MISO, TOUCH_CLK, TOUCH_CS);
#endif

  // Register touch
  lv_indev_t* indev = lv_indev_create();
  lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);  
  lv_indev_set_read_cb(indev, touch_read);

  waitForSync();
  //myTZ.setLocation("Europe/Rome");
  myTZ.setPosix("CET");
  set_custom_theme();
  main_tabs();
  Serial.println("Setup done");
}

void loop() {   
  lv_timer_periodic_handler();
  maybeAdjustBrightness(disp);

  time_t local = myTZ.now();

  int dd = day(local);
  int mth = month(local);
  int yr = year(local);
  int hh = hour(local);
  int mm = minute(local);
  int ss = second(local);

  lv_label_set_text_fmt(clockLabel, "%02d : %02d : %02d", hh, mm, ss);

  events();
}


char * pillNames[] = {"Blood Pressure", "Statin", "Vitamin D", "Sleep Drops"};
char * dosageNames[] = {"1/4","1/2","1","2","3","4","5","6","7","8","9","10"};
char * unitNames[] = {"Cpr.", "Drop(s)", "Vial(s)", "Unit(s)"};

static void schedule_reminders_on_save() {
  uint32_t count = lv_obj_get_child_count(sub_pills_cont);
  if (count > 0) {
    StaticJsonDocument<1024> jsonDoc;
    JsonArray dataArray = jsonDoc.createNestedArray("reminders");
    
    for (int i=0; i<count; i++) {
      lv_obj_t * content = lv_obj_get_child(sub_pills_cont, i);
      
      lv_obj_t * pill = lv_obj_get_child(content, 0);
      lv_obj_t * label = lv_obj_get_child(pill,0);

      //pill -> cont -> (txt), (img), dropdown
      //pill structure = pill (container) -> line = lv_obj_get_child(pill, *line number*) -> lv_obj_get_child(line, *if single select == 1; if double select == 1 or 3*)

      //Serial.printf("Pill child count: %d \n", lv_obj_get_child_count(pill));
      //Serial.printf("sub Pill child 1 child count: %d \n", lv_obj_get_child_count(lv_obj_get_child(pill, 1)));
      //Serial.printf("sub Pill child 2 child count: %d \n", lv_obj_get_child_count(lv_obj_get_child(pill, 2)));


      lv_obj_t * time, * type;
      JsonObject reminder = dataArray.createNestedObject();
      //hours
      time = lv_obj_get_child(lv_obj_get_child(pill, 1), 1);
      reminder["hh"] = lv_dropdown_get_selected(time);
      //minutes
      time = lv_obj_get_child(lv_obj_get_child(pill, 1), 3);
      reminder["mm"] = lv_dropdown_get_selected(time);
      type = lv_obj_get_child(lv_obj_get_child(pill, 2), 1);
      reminder["name"] = pillNames[lv_dropdown_get_selected(type)];
      type = lv_obj_get_child(lv_obj_get_child(pill, 3), 1);
      reminder["dosage"] = dosageNames[lv_dropdown_get_selected(type)];
      type = lv_obj_get_child(lv_obj_get_child(pill, 3), 3);
      reminder["unit"] = unitNames[lv_dropdown_get_selected(type)];
    }

    // Open file for writing
    File file = LittleFS.open("/pills.json", "w");
    if (!file) {
        Serial.println("Failed to open file for writing");
        return;
    }

    // Serialize JSON to file
    if (serializeJson(jsonDoc, file) == 0) {
        Serial.println("Failed to write JSON to file");
        file.close();
        return;
    }
    file.close();
    deleteEvent(display_reminders);
    //call beginning of next minute
    myTZ.setEvent(display_reminders, myTZ.now() + 60UL - second());
  }
}

void display_reminders() {
    File file = LittleFS.open("/pills.json", "r");
    if (!file) {
        Serial.println("Failed to open file for reading");
        return;
    }

    // Parse JSON from file
    StaticJsonDocument<1024> jsonDoc;
    DeserializationError error = deserializeJson(jsonDoc, file);
    file.close();

    if (error) {
        Serial.print("Failed to parse JSON: ");
        Serial.println(error.c_str());
        return;
    }

    JsonArray reminders = jsonDoc["reminders"];
    for (JsonObject reminder : reminders) {
        const char * name = (const char *) reminder["name"];
        const char * dosage = reminder["dosage"].as<const char*>();
        const char * unit = reminder["unit"].as<const char*>();
        int hh = reminder["hh"].as<int>();
        int mm = reminder["mm"].as<int>();

        if (myTZ.hour() == (uint8_t)hh && myTZ.minute() == (uint8_t)mm) {
          pillReminder((char *)name, (char *) dosage, (char *) unit, hh, mm);
        }
    }

    //call every minute
    myTZ.setEvent(display_reminders, myTZ.now() + 60UL);
}