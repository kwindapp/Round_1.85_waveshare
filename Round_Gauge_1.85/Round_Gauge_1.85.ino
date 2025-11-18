#include "Display_ST77916.h"
#include "Audio_PCM5101.h"
#include "RTC_PCF85063.h"
#include "Gyro_QMI8658.h"
#include "LVGL_Driver.h"
#include "MIC_MSM.h"
#include "PWR_Key.h"
#include "SD_Card.h"
#include "LVGL_Example.h"
#include "BAT_Driver.h"
#include <Audio.h>
#include "lvgl.h"

#include <esp_now.h>
#include <esp_wifi.h>
#include <WiFi.h>

//====== CUSTOM FONTS FROM ui_font_Hollow*.c ======
extern const lv_font_t ui_font_Hollow18;
extern const lv_font_t ui_font_Hollow22;
extern const lv_font_t ui_font_Hollow38;
extern const lv_font_t ui_font_Hollow85;

// You forgot this: (needed for "RAUH Welt BEGRIFF")
extern const lv_font_t ui_font_t20;

//==================================================
//              GLOBAL COLOR FLAGS
//==================================================

// SCREEN BG
#define SCR_BG_TOP      0x141414
#define SCR_BG_BOTTOM   0x857B7A

// TICKS
#define TICK_COLOR      0xD6CFCB

// NUMBERS
#define NUMBER_COLOR    0xE6DFD8

// RPM TEXT
#define RPM_TEXT_COLOR  0xE6DFD8

// NAME TEXT
#define NAME_COLOR      0x85807F
#define NAME_COLOR1     0x000000   // Black
#define NAME_COLOR2     0x000000 

// ARC COLORS
#define ARC_GREEN       0x21543A
#define ARC_YELLOW      0x807116
#define ARC_RED         0xA64E12

// NEEDLE COLOR
#define NEEDLE_COLOR    0xFF4444

// CENTER HUB COLORS
#define HUB_BG          0x141414
#define HUB_BORDER      0x403B3B

//==================================================
//           EASY LABEL POSITION SETTINGS
//==================================================
static const int RPM_Y_OFFSET   =  50;
static const int NAME_Y_OFFSET  = -50;   // "911 RWB"
static const int NAME1_Y_OFFSET = 100;   // "rwb janine"
static const int NAME2_Y_OFFSET = 120;   // "RAUH Welt BEGRIFF"

//==================================================
//            ESP-NOW PACKET STRUCT
//==================================================
typedef struct {
  uint16_t rpm;
  float batt;
  float motor;
  float dk;
  float gp;
  uint8_t funk;
} DashPacket;

DashPacket lastPacket;
volatile uint16_t g_rpm = 0;

//==================================================
//                 GAUGE OBJECTS
//==================================================
static lv_obj_t *g_meter        = nullptr;
static lv_meter_indicator_t *g_needle = nullptr;
static lv_obj_t *g_rpm_label    = nullptr;
static lv_obj_t *g_name_label   = nullptr;
static lv_obj_t *g_name1_label  = nullptr;
static lv_obj_t *g_name2_label  = nullptr;   // 🔥 NEW label added
static lv_obj_t *g_center       = nullptr;

static void meter_event_cb(lv_event_t * e);

//==================================================
//                 CREATE GAUGE
//==================================================
void Lvgl_ShowGauge() {

  // Screen background
  lv_obj_t *scr = lv_scr_act();
  lv_obj_set_style_bg_color(scr, lv_color_hex(SCR_BG_TOP), 0);
  lv_obj_set_style_bg_grad_color(scr, lv_color_hex(SCR_BG_BOTTOM), 0);
  lv_obj_set_style_bg_grad_dir(scr, LV_GRAD_DIR_VER, 0);

  g_meter = lv_meter_create(scr);
  lv_obj_set_size(g_meter, 350, 350);
  lv_obj_center(g_meter);

  // Gauge background
  lv_obj_set_style_bg_color(g_meter, lv_color_hex(SCR_BG_TOP), 0);
  lv_obj_set_style_bg_grad_color(g_meter, lv_color_hex(SCR_BG_BOTTOM), 0);
  lv_obj_set_style_bg_grad_dir(g_meter, LV_GRAD_DIR_VER, 0);

  lv_meter_scale_t *scale = lv_meter_add_scale(g_meter);

  // Minor ticks
  lv_meter_set_scale_ticks(g_meter, scale, 9, 2, 14, lv_color_hex(TICK_COLOR));

  // Major ticks
  lv_meter_set_scale_major_ticks(
      g_meter, scale,
      1, 6, 24,
      lv_color_hex(TICK_COLOR),
      12
  );

  // Number styling
  lv_obj_set_style_text_font(g_meter, &ui_font_Hollow38, LV_PART_TICKS);
  lv_obj_set_style_text_color(g_meter, lv_color_hex(NUMBER_COLOR), LV_PART_TICKS);

  // Scale 0–8
  lv_meter_set_scale_range(g_meter, scale, 0, 8, 270, 135);

  // ARC green
  lv_meter_indicator_t *ind_green = lv_meter_add_arc(g_meter, scale, 20, lv_color_hex(ARC_GREEN), 0);
  lv_meter_set_indicator_start_value(g_meter, ind_green, 0);
  lv_meter_set_indicator_end_value(g_meter, ind_green, 4);

  // ARC yellow
  lv_meter_indicator_t *ind_yellow = lv_meter_add_arc(g_meter, scale, 24, lv_color_hex(ARC_YELLOW), 0);
  lv_meter_set_indicator_start_value(g_meter, ind_yellow, 4);
  lv_meter_set_indicator_end_value(g_meter, ind_yellow, 6);

  // ARC red
  lv_meter_indicator_t *ind_red = lv_meter_add_arc(g_meter, scale, 30, lv_color_hex(ARC_RED), 0);
  lv_meter_set_indicator_start_value(g_meter, ind_red, 6);
  lv_meter_set_indicator_end_value(g_meter, ind_red, 8);

  // Needle
  g_needle = lv_meter_add_needle_line(g_meter, scale, 6, lv_color_hex(NEEDLE_COLOR), -45);

  // RPM TEXT
  g_rpm_label = lv_label_create(g_meter);
  lv_label_set_text(g_rpm_label, "0000");
  lv_obj_set_style_text_font(g_rpm_label, &ui_font_Hollow85, 0);
  lv_obj_set_style_text_color(g_rpm_label, lv_color_hex(RPM_TEXT_COLOR), 0);
  lv_obj_align(g_rpm_label, LV_ALIGN_CENTER, 0, RPM_Y_OFFSET);

  // MAIN NAME LABEL (911 RWB)
  g_name_label = lv_label_create(g_meter);
  lv_label_set_text(g_name_label, "911 RWB");
  lv_obj_set_style_text_font(g_name_label, &ui_font_Hollow85, 0);
  lv_obj_set_style_text_color(g_name_label, lv_color_hex(NAME_COLOR), 0);
  lv_obj_align(g_name_label, LV_ALIGN_CENTER, 0, NAME_Y_OFFSET);

  // SECOND NAME LABEL (rwb janine)
  g_name1_label = lv_label_create(g_meter);
  lv_label_set_text(g_name1_label, "rwb janine");
  lv_obj_set_style_text_font(g_name1_label, &ui_font_Hollow22, 0);
  lv_obj_set_style_text_color(g_name1_label, lv_color_hex(NAME_COLOR2), 0);
  lv_obj_align(g_name1_label, LV_ALIGN_CENTER, 0, NAME1_Y_OFFSET);

  // THIRD NAME LABEL (RAUH WELT BEGRIFF)
  g_name2_label = lv_label_create(g_meter);
  lv_label_set_text(g_name2_label, "RAUH Welt BEGRIFF");
  lv_obj_set_style_text_font(g_name2_label, &ui_font_t20, 0);
  lv_obj_set_style_text_color(g_name2_label, lv_color_hex(NAME_COLOR1), 0);
  lv_obj_align(g_name2_label, LV_ALIGN_CENTER, 0, NAME2_Y_OFFSET);

  // CENTER HUB
  g_center = lv_obj_create(g_meter);
  lv_obj_set_size(g_center, 40, 40);
  lv_obj_center(g_center);
  lv_obj_set_style_radius(g_center, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_color(g_center, lv_color_hex(HUB_BG), 0);
  lv_obj_set_style_border_color(g_center, lv_color_hex(HUB_BORDER), 0);
  lv_obj_set_style_border_width(g_center, 3, 0);

  lv_obj_add_event_cb(g_meter, meter_event_cb, LV_EVENT_ALL, NULL);
}

//==================================================
//           TOUCH TO CHANGE RPM (testing)
//==================================================
static void meter_event_cb(lv_event_t * e) {
  if (lv_event_get_code(e) == LV_EVENT_PRESSED) {
    static uint16_t touch_rpm = 0;
    touch_rpm += 500;
    if (touch_rpm > 8000) touch_rpm = 0;
    g_rpm = touch_rpm;
  }
}

//==================================================
//           ESP-NOW RECEIVER CALLBACK
//==================================================
void OnDataRecv(const esp_now_recv_info_t *recv_info,
                const uint8_t *incomingData, int len)
{
  if (len < (int)sizeof(DashPacket)) return;

  DashPacket pkt;
  memcpy(&pkt, incomingData, sizeof(DashPacket));

  if (pkt.rpm > 8000) pkt.rpm = 8000;

  g_rpm = pkt.rpm;
  lastPacket = pkt;
}

//==================================================
//                 DRIVER TASK
//==================================================
void DriverTask(void *parameter) {
  while (1) {
    PWR_Loop();
    BAT_Get_Volts();
    RTC_Loop();
    QMI8658_Loop();
    vTaskDelay(pdMS_TO_TICKS(100));
  }
}

void Driver_Loop() {
  xTaskCreatePinnedToCore(DriverTask, "DriverTask", 4096, NULL, 3, NULL, 0);
}

//==================================================
//                     SETUP
//==================================================
void setup() {
  Serial.begin(115200);
  delay(100);

  Flash_test();
  PWR_Init();
  BAT_Init();
  I2C_Init();
  PCF85063_Init();
  QMI8658_Init();
  TCA9554PWR_Init(0x00);
  Backlight_Init();
  SD_Init();
  Audio_Init();
  MIC_Init();
  LCD_Init();
  Lvgl_Init();

  Lvgl_ShowGauge();

  WiFi.mode(WIFI_STA);
  WiFi.disconnect();

  if (esp_now_init() != ESP_OK) {
    Serial.println("ESP-NOW init failed");
  } else {
    esp_now_register_recv_cb(OnDataRecv);
    Serial.println("ESP-NOW initialized");
  }

  Driver_Loop();
}

//==================================================
//                     LOOP
//==================================================
void loop() {
  Lvgl_Loop();
  vTaskDelay(pdMS_TO_TICKS(5));

  static uint16_t last_rpm = 0;

  if (g_rpm != last_rpm) {
    last_rpm = g_rpm;

    float v = g_rpm / 1000.0f;
    if (v > 8) v = 8;

    if (g_meter && g_needle) {
      lv_meter_set_indicator_value(g_meter, g_needle, v);
    }
    if (g_rpm_label) {
      lv_label_set_text_fmt(g_rpm_label, "%.1fk", v);
    }
  }
}
