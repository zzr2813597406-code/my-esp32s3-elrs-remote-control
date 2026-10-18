#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include "lvgl_port.h"
#include "menu.h"
#include "keyboard.h"
#include "OLED2.h"
#include "joystick.h"
#include "crsf.h"          // 包含 CRSF 头文件

// 任务函数声明
void key_task(void *pvParameters);
void lvgl_task(void *pvParameters);

void key_task(void *pvParameters) {
    while(1) {
        keyboard_scan();
        show_info_on_oled2();
        xuanniu_read();
        joystick_read();
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

void lvgl_task(void *pvParameters) {
    while (1) {
        lv_timer_handler();
        vTaskDelay(pdMS_TO_TICKS(5));
    }
}

void setup() {
    Serial.begin(115200);
    lvgl_port_init();
    keyboard_init();
    show_info_on_oled2();
    analogReadResolution(11);
    // 初始化 CRSF（会自动创建任务）
    crsf_init();

    // 注意：原来这里有两处创建 lvgl_task 到核心1，可能重复。按原样保留。
    xTaskCreatePinnedToCore(lvgl_task, "LVGL", 8192, NULL, 1, NULL, 0);
    xTaskCreatePinnedToCore(key_task, "Keys", 8192, NULL, 1, NULL, 1);
    // 注意：如果重复创建导致问题，可以删除其中一个 lvgl_task 创建。
}

void loop() {
    vTaskDelay(100);
}