#include "lvgl_port.h"
#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// 任务函数声明
SemaphoreHandle_t lvgl_mutex = NULL;

void lvgl_lock_init(void) {
    if (lvgl_mutex == NULL) {
        lvgl_mutex = xSemaphoreCreateMutex();
    }
}

void lvgl_lock(void) {
    xSemaphoreTake(lvgl_mutex, portMAX_DELAY);
}

void lvgl_unlock(void) {
    xSemaphoreGive(lvgl_mutex);
}
// ========== 屏幕1 配置 ==========
#define SCREEN1_WIDTH  128
#define SCREEN1_HEIGHT 64
#define I2C1_SDA       16
#define I2C1_SCL       15
#define OLED1_ADDR     0x3D

// ========== 屏幕2 配置 ==========
#define SCREEN2_WIDTH  128
#define SCREEN2_HEIGHT 64
#define I2C2_SDA       18
#define I2C2_SCL       17
#define OLED2_ADDR     0x3C

static Adafruit_SSD1306 display1(SCREEN1_WIDTH, SCREEN1_HEIGHT, &Wire, -1);
static Adafruit_SSD1306 display2(SCREEN2_WIDTH, SCREEN2_HEIGHT, &Wire1, -1);

static lv_display_t *disp1 = nullptr;
static lv_display_t *disp2 = nullptr;


// 颜色转灰度
static inline uint8_t rgb565_to_gray(uint16_t color) {
    uint8_t r = (color >> 11) & 0x1F;
    uint8_t g = (color >> 5) & 0x3F;
    uint8_t b = color & 0x1F;
    r = (r * 255) / 31;
    g = (g * 255) / 63;
    b = (b * 255) / 31;
    return (r * 299 + g * 587 + b * 114) / 1000;
}

// 屏幕1 刷新回调
static void flush_cb1(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map) {
    display1.startWrite();
    for (int32_t y = area->y1; y <= area->y2; y++) {
        for (int32_t x = area->x1; x <= area->x2; x++) {
            uint16_t color = *(uint16_t *)px_map;
            uint8_t gray = rgb565_to_gray(color);
            display1.writePixel(x, y, gray > 128 ? SSD1306_WHITE : SSD1306_BLACK);
            px_map += 2;
        }
    }
    display1.endWrite();
    display1.display();
    lv_display_flush_ready(disp);
}

// 屏幕2 刷新回调
static void flush_cb2(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map) {
    display2.startWrite();
    for (int32_t y = area->y1; y <= area->y2; y++) {
        for (int32_t x = area->x1; x <= area->x2; x++) {
            uint16_t color = *(uint16_t *)px_map;
            uint8_t gray = rgb565_to_gray(color);
            display2.writePixel(x, y, gray > 128 ? SSD1306_WHITE : SSD1306_BLACK);
            px_map += 2;
        }
    }
    display2.endWrite();
    display2.display();
    lv_display_flush_ready(disp);
}

void lvgl_port_init(void) {
    // 初始化 I2C 总线
    Wire.begin(I2C1_SDA, I2C1_SCL);
    Wire1.begin(I2C2_SDA, I2C2_SCL);

    // 初始化屏幕1
    if (!display1.begin(SSD1306_SWITCHCAPVCC, OLED1_ADDR)) {
        Serial.println("Display1 init failed");
        while (1);
    }
    display1.setRotation(2);   // 根据你的安装方向调整，可改成 0,1,2,3
    display1.clearDisplay();
    display1.display();

    // 初始化屏幕2
    if (!display2.begin(SSD1306_SWITCHCAPVCC, OLED2_ADDR)) {
        Serial.println("Display2 init failed");
        while (1);
    }
    display2.clearDisplay();
    display2.display();

    // 初始化 LVGL
    lv_init();

    // 注册显示器1
    disp1 = lv_display_create(SCREEN1_WIDTH, SCREEN1_HEIGHT);
    lv_display_set_flush_cb(disp1, flush_cb1);
    static uint8_t buf1[SCREEN1_WIDTH * SCREEN1_HEIGHT * 2]; // RGB565 全屏缓冲
    lv_display_set_buffers(disp1, buf1, NULL, sizeof(buf1), LV_DISPLAY_RENDER_MODE_FULL);
    lv_display_set_color_format(disp1, LV_COLOR_FORMAT_RGB565);

    // 注册显示器2
    disp2 = lv_display_create(SCREEN2_WIDTH, SCREEN2_HEIGHT);
    lv_display_set_flush_cb(disp2, flush_cb2);
    static uint8_t buf2[SCREEN2_WIDTH * SCREEN2_HEIGHT * 2];
    lv_display_set_buffers(disp2, buf2, NULL, sizeof(buf2), LV_DISPLAY_RENDER_MODE_FULL);
    lv_display_set_color_format(disp2, LV_COLOR_FORMAT_RGB565);

    Serial.println("LVGL dual display port initialized");
}

lv_display_t* lvgl_port_get_disp1(void) {
    return disp1;
}

lv_display_t* lvgl_port_get_disp2(void) {
    return disp2;
}