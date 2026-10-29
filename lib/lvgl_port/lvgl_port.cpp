#include "lvgl_port.h"
#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// ============================================================
// 性能相关参数
// ============================================================
#define OLED_I2C_HZ     400000UL   // 模块够稳的话可以试 1000000（全屏刷新能快 2.5 倍）
#define LVGL_BUF_LINES  16         // 局部渲染缓冲的高（行）：越小越省内存、刷新粒度越细
#define LVGL_REFR_MS    16         // LVGL 刷新周期，默认 33ms(30FPS)，改 16 就是 60FPS 上限
#define GRAY_LEVEL      128        // 二值化阈值：0-255

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

// 任务函数声明
SemaphoreHandle_t lvgl_mutex = NULL;

void lvgl_lock_init(void) {
    if (lvgl_mutex == NULL) {
        // 递归锁：允许同一个任务重复进入
        // （例如 menu_input 持锁时调用 on_action，on_action 里又调 screen_log）
        lvgl_mutex = xSemaphoreCreateRecursiveMutex();
    }
}

void lvgl_lock(void) {
    xSemaphoreTakeRecursive(lvgl_mutex, portMAX_DELAY);
}

void lvgl_unlock(void) {
    xSemaphoreGiveRecursive(lvgl_mutex);
}

// ============================================================
// 一块屏的绑定关系
// ============================================================
struct OledLink {
    Adafruit_SSD1306 *dev;
    TwoWire          *bus;
    uint8_t           addr;
    bool              rot180;   // 屏1 装反了，用 180° 对应原来的 setRotation(2)
};

static OledLink link1 = { &display1, &Wire,  OLED1_ADDR, true  };
static OledLink link2 = { &display2, &Wire1, OLED2_ADDR, false };

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

// 发一串命令（0x00 是控制字节：Co=0, D/C#=0）
static void ssd_cmds(OledLink &o, const uint8_t *c, size_t n) {
    o.bus->beginTransmission(o.addr);
    o.bus->write((uint8_t)0x00);
    for (size_t i = 0; i < n; i++) o.bus->write(c[i]);
    o.bus->endTransmission();
}

/**
 * 把一块区域对应的 SSD1306 页发出去（水平寻址模式，整页整页地传）。
 * 只传 area 覆盖到的页，所以小范围改动只有几百字节，而不是 1024 字节。
 */
static void oled_flush_pages(OledLink &o, const lv_area_t *area) {
    // LVGL 坐标 -> 面板坐标（旋转 180° 时两个轴都翻）
    int32_t x1 = area->x1, x2 = area->x2;
    int32_t y1 = area->y1, y2 = area->y2;
    if (o.rot180) {
        x1 = SCREEN1_WIDTH  - 1 - area->x2;
        x2 = SCREEN1_WIDTH  - 1 - area->x1;
        y1 = SCREEN1_HEIGHT - 1 - area->y2;
        y2 = SCREEN1_HEIGHT - 1 - area->y1;
    }

    // 行按 8 像素一页对齐：页内没被刷到的行，缓冲里还是旧值，发出去也不会错
    uint8_t p0 = (uint8_t)(y1 >> 3);
    uint8_t p1 = (uint8_t)(y2 >> 3);
    uint8_t c0 = (uint8_t)x1;
    uint8_t c1 = (uint8_t)x2;

    uint8_t cmd[] = {
        0x22, p0, p1,        // PAGEADDR：页起止
        0x21, c0, c1         // COLUMNADDR：列起止
    };
    ssd_cmds(o, cmd, sizeof(cmd));

    const uint8_t *buf = o.dev->getBuffer();

    // 0x40 是数据控制字节；单次 transmission 别塞太满（Wire 默认 128 字节缓冲）
    const uint16_t CHUNK = 96;
    o.bus->beginTransmission(o.addr);
    o.bus->write((uint8_t)0x40);
    uint16_t sent = 1;

    for (uint8_t p = p0; p <= p1; p++) {
        const uint8_t *src = buf + (uint16_t)p * SCREEN1_WIDTH + c0;
        for (uint8_t x = c0; x <= c1; x++) {
            if (sent >= CHUNK) {
                o.bus->endTransmission();
                o.bus->beginTransmission(o.addr);
                o.bus->write((uint8_t)0x40);
                sent = 1;
            }
            o.bus->write(*src++);
            sent++;
        }
        if (p == 255) break;            // 防止 uint8_t 回绕（实际不会发生）
    }
    o.bus->endTransmission();
}

// 把 LVGL 的 RGB565 区域二值化后写进 Adafruit 的 1024 字节分辨率缓冲
static void oled_draw_area(OledLink &o, const lv_area_t *area, const uint8_t *px_map) {
    const int32_t w = lv_area_get_width(area);

    o.dev->startWrite();
    for (int32_t y = area->y1; y <= area->y2; y++) {
        const uint8_t *p = px_map + (uint32_t)(y - area->y1) * (uint32_t)w * 2;
        for (int32_t x = area->x1; x <= area->x2; x++) {
            uint16_t color = (uint16_t)(p[0] | (p[1] << 8));
            uint8_t  gray  = rgb565_to_gray(color);
            o.dev->writePixel(x, y, gray > GRAY_LEVEL ? SSD1306_WHITE : SSD1306_BLACK);
            p += 2;
        }
    }
    o.dev->endWrite();
}

static void flush_cb1(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map) {
    oled_draw_area(link1, area, px_map);
    oled_flush_pages(link1, area);
    lv_display_flush_ready(disp);
}

static void flush_cb2(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map) {
    oled_draw_area(link2, area, px_map);
    oled_flush_pages(link2, area);
    lv_display_flush_ready(disp);
}

void lvgl_port_init(void) {
    // 初始化 I2C 总线
    Wire.begin(I2C1_SDA, I2C1_SCL);
    Wire1.begin(I2C2_SDA, I2C2_SCL);
    Wire.setClock(OLED_I2C_HZ);
    Wire1.setClock(OLED_I2C_HZ);

    // 初始化屏幕1
    if (!display1.begin(SSD1306_SWITCHCAPVCC, OLED1_ADDR)) {
        Serial.println("Display1 init failed");
        while (1);
    }
    display1.setRotation(2);   // 根据你的安装方向调整，可改成 0,1,2,3（改完记得同步 link1.rot180）
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

    lv_tick_set_cb([]() -> uint32_t { return (uint32_t)millis(); }); // 设置 LVGL 的定时器回调函数

    // 注册显示器1：局部渲染（只渲染/传输脏区），缓冲区取 16 行
    disp1 = lv_display_create(SCREEN1_WIDTH, SCREEN1_HEIGHT);
    lv_display_set_flush_cb(disp1, flush_cb1);
    static uint8_t buf1[SCREEN1_WIDTH * LVGL_BUF_LINES * 2];   // RGB565，16 行
    lv_display_set_buffers(disp1, buf1, NULL, sizeof(buf1), LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_color_format(disp1, LV_COLOR_FORMAT_RGB565);

    // 注册显示器2
    disp2 = lv_display_create(SCREEN2_WIDTH, SCREEN2_HEIGHT);
    lv_display_set_flush_cb(disp2, flush_cb2);
    static uint8_t buf2[SCREEN2_WIDTH * LVGL_BUF_LINES * 2];
    lv_display_set_buffers(disp2, buf2, NULL, sizeof(buf2), LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_color_format(disp2, LV_COLOR_FORMAT_RGB565);

    // 把两块屏的刷新周期从默认 33ms 提到 16ms，动画才有 60FPS 的上限
    lv_timer_set_period(lv_display_get_refr_timer(disp1), LVGL_REFR_MS);
    lv_timer_set_period(lv_display_get_refr_timer(disp2), LVGL_REFR_MS);

    Serial.println("LVGL dual display port initialized");
}

lv_display_t* lvgl_port_get_disp1(void) {
    return disp1;
}

lv_display_t* lvgl_port_get_disp2(void) {
    return disp2;
}
