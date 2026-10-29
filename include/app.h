#ifndef APP_H
#define APP_H

/*
 * 项目总头文件
 * ------------------------------------------------------------
 * 只给 main.cpp 这类"上层"文件用：一次拿到所有模块的公共接口。
 * 各模块内部（lib/xxx/xxx.cpp）仍然只包含自己需要的头，避免互相牵连。
 *
 * 分层关系：
 *   lvgl_port  ── 双屏驱动 + LVGL 互斥锁      （最底层，谁都能用）
 *   keyboard   ── 矩阵键盘 + 按键事件
 *   joystick   ── 摇杆采样 + 校准状态机
 *   crsf       ── CRSF 帧收发（依赖 joystick 的通道值）
 *   menu       ── 菜单框架 + disp1 覆盖层（依赖 joystick：校准、调试页）
 *   OLED2      ── disp2 通道监视页（依赖 joystick + crsf）
 */

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "lvgl_port.h"
#include "menu.h"
#include "keyboard.h"
#include "joystick.h"
#include "OLED2.h"
#include "crsf.h"

#endif
