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

//==================================================
//              GLOBAL COLOR FLAGS
//==================================================

// SCREEN BG
#define SCR_BG_TOP      0x101018
#define SCR_BG_BOTTOM   0x85807F

// TICKS (lines)
#define TICK_COLOR      0xD6CFCB

// NUMBERS (0–8)
#define NUMBER_COLOR    0xE6DFD8

// RPM TEXT COLOR
#define RPM_TEXT_COLOR  0xE6DFD8

// NAME LABEL COLOR
#define NAME_COLOR      0x85807F

// ARC ZONES
#define ARC_GREEN       0x21543A
#define ARC_YELLOW      0x807116
#define ARC_RED         0xA64E12

// NEEDLE COLOR
#define NEEDLE_COLOR    0xFF4444

// CENTER HUB COLORS
#define HUB_BG          0x3B3734
#define HUB_BORDER      0x1F1D1C

//==================================================
//           EASY LABEL POSITION SETTINGS
//==================================================
static const int RPM_Y_OFFSET  =  60;   // RPM text relative to center
static const int NAME_Y_OFFSET = -60;   // "911 RWB" relative to center

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
static lv_obj_t *g_meter      = nullptr;
static lv_meter_indicator_t *g_needle = nullptr;
static lv_obj_t *g_rpm_label  = nullptr;  // dynamic RPM text
static lv_obj_t *g_name_label = nullptr;  // static "911 RWB"
static lv_obj_t *g_center     = nullptr;

static void meter_event_cb(lv_event_t * e);

//==================================================
//                 CREATE GAUGE
//==================================================
void Lvgl_ShowGauge() {

  // Set SCREEN background color / gradient
  lv_obj_t *scr = lv_scr_act();
  lv_obj_set_style_bg_color(scr, lv_color_hex(SCR_BG_TOP), 0);
  lv_obj_set_style_bg_grad_color(scr, lv_color_hex(SCR_BG_BOTTOM), 0);
  lv_obj_set_style_bg_grad_dir(scr, LV_GRAD_DIR_VER, 0);
  lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

  g_meter = lv_meter_create(scr);
  lv_obj_set_size(g_meter, 360, 360);
  lv_obj_center(g_meter);

  // Gauge background
  lv_obj_set_style_bg_color(g_meter, lv_color_hex(SCR_BG_TOP), 0);
  lv_obj_set_style_bg_grad_color(g_meter, lv_color_hex(SCR_BG_BOTTOM), 0);
  lv_obj_set_style_bg_grad_dir(g_meter, LV_GRAD_DIR_VER, 0);

  lv_meter_scale_t *scale = lv_meter_add_scale(g_meter);

  // Tick lines (minor)
  lv_meter_set_scale_ticks(
      g_meter, scale,
      9,                      // ticks: 0..8
      2,                      // line width
      14,                     // line length
      lv_color_hex(TICK_COLOR)
  );

  // Tick numbers 0–8 + major tick style
  lv_meter_set_scale_major_ticks(
      g_meter, scale,
      1,                      // every tick is major
      6,                      // major tick width
      24,                     // major tick length
      lv_color_hex(TICK_COLOR), // this is MAJOR TICK LINE color
      12                      // label distance
  );

  // *** HERE we style the NUMBER TEXT (0–8) ***
  lv_obj_set_style_text_font(
      g_meter,
      &ui_font_Hollow38,           // font used for the 0–8 numbers
      LV_PART_TICKS | LV_STATE_DEFAULT
  );
  lv_obj_set_style_text_color(
      g_meter,
      lv_color_hex(NUMBER_COLOR),  // color of the 0–8 numbers
      LV_PART_TICKS | LV_STATE_DEFAULT
  );

  // scale 0–8 over 270°
  lv_meter_set_scale_range(
      g_meter, scale,
      0, 8,
      270,
      135
  );

  // ARC zones
  lv_meter_indicator_t *ind_green = lv_meter_add_arc(
      g_meter, scale, 20, lv_color_hex(ARC_GREEN), 0);
  lv_meter_set_indicator_start_value(g_meter, ind_green, 0);
  lv_meter_set_indicator_end_value(g_meter, ind_green, 4);

  lv_meter_indicator_t *ind_yellow = lv_meter_add_arc(
      g_meter, scale, 24, lv_color_hex(ARC_YELLOW), 0);
  lv_meter_set_indicator_start_value(g_meter, ind_yellow, 4);
  lv_meter_set_indicator_end_value(g_meter, ind_yellow, 6);

  lv_meter_indicator_t *ind_red = lv_meter_add_arc(
      g_meter, scale, 30, lv_color_hex(ARC_RED), 0);
  lv_meter_set_indicator_start_value(g_meter, ind_red, 6);
  lv_meter_set_indicator_end_value(g_meter, ind_red, 8);

  // Needle
  g_needle = lv_meter_add_needle_line(
      g_meter, scale,
      6,                          // needle length from center
      lv_color_hex(NEEDLE_COLOR),
      -45
  );

  // RPM TEXT
  g_rpm_label = lv_label_create(g_meter);
  lv_label_set_text(g_rpm_label, "0.0k");
  lv_obj_set_style_text_font(g_rpm_label, &ui_font_Hollow85,
                             LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_text_color(g_rpm_label, lv_color_hex(RPM_TEXT_COLOR),
                              LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_align(g_rpm_label, LV_ALIGN_CENTER, 0, RPM_Y_OFFSET);

  // NAME LABEL
  g_name_label = lv_label_create(g_meter);
  lv_label_set_text(g_name_label, "911 RWB");
  lv_obj_set_style_text_font(g_name_label, &ui_font_Hollow85,
                             LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_text_color(g_name_label, lv_color_hex(NAME_COLOR),
                              LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_align(g_name_label, LV_ALIGN_CENTER, 0, NAME_Y_OFFSET);

  // CENTER HUB CAP
  g_center = lv_obj_create(g_meter);
  lv_obj_set_size(g_center, 40, 40);
  lv_obj_center(g_center);
  lv_obj_set_style_radius(g_center, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_color(g_center, lv_color_hex(HUB_BG), 0);
  lv_obj_set_style_bg_opa(g_center, LV_OPA_COVER, 0);
  lv_obj_set_style_border_color(g_center, lv_color_hex(HUB_BORDER), 0);
  lv_obj_set_style_border_width(g_center, 3, 0);

  lv_obj_add_event_cb(g_meter, meter_event_cb, LV_EVENT_ALL, NULL);
}

//==================================================
//             TOUCH TO CHANGE RPM
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
