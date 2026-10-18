#ifndef LVGL_PORT_H
#define LVGL_PORT_H

#include <lvgl.h>

// 初始化双屏驱动（I2C + LVGL 显示器注册）
void lvgl_port_init(void);

// 获取显示器句柄
lv_display_t* lvgl_port_get_disp1(void);
lv_display_t* lvgl_port_get_disp2(void);

#endif