#include "joystick.h"
#include "driver/adc.h"
#include <Arduino.h>
#include "keyboard.h"
#include "menu.h"
#include "lvgl_port.h"
#define L_xuanniu_Pin 4
#define R_xuanniu_Pin 5
#define L_X_Pin 7
#define L_Y_Pin 6
#define R_X_Pin 2
#define R_Y_Pin 1

bool joystick_initialized = false;

int L_xuanniu = 0;
int R_xuanniu = 0;
int L_X = 0;
int L_Y = 0;
int R_X = 0;
int R_Y = 0;

int filtered = 0;  // 全局或静态变量

int filterADC(int pin) {
    int sum = 0;
    for (int i = 0; i < 10; i++) {   // 读4次
        sum += analogRead(pin);
        delayMicroseconds(500);      // 两次之间稍延时
    }
    return sum / 10;

} 

void xuanniu_read()
{
    L_xuanniu = analogRead(L_xuanniu_Pin);
    R_xuanniu = analogRead(R_xuanniu_Pin);
}

void joystick_read()
{
    // if (joystick_initialized) {
        L_X = filterADC(L_X_Pin);
        L_Y = filterADC(L_Y_Pin);
        R_X = filterADC(R_X_Pin);
        R_Y = filterADC(R_Y_Pin);
    // }
}



AxisCal cal_LX, cal_LY, cal_RX, cal_RY;

enum CalState {
    CAL_CENTER,   // 采中心
    CAL_RANGE,    // 采行程
    CAL_DONE,     // 校准完成，可发数据
    CAL_ERROR     // 行程不合格，需重校
};

CalState cal_state = CAL_CENTER;
uint32_t cal_start_ms = 0;

// 中心采样累加器
long sum_LX = 0, sum_LY = 0, sum_RX = 0, sum_RY = 0;
int  cnt_center = 0;
const int CENTER_SAMPLES = 100;   // 约 2 秒 @50Hz


void update_range(AxisCal &c, int raw) {
    if (raw < c.min_val) c.min_val = raw;
    if (raw > c.max_val) c.max_val = raw;
}

void finish_calibration() {
    const int MIN_SPAN = 300;   // 每侧至少 300 个 ADC 计数，可按实际摇杆调整
    cal_LX.range_ok = (cal_LX.max_val - cal_LX.center > MIN_SPAN) &&
                      (cal_LX.center - cal_LX.min_val > MIN_SPAN);

    cal_RX.range_ok = (cal_RX.max_val - cal_RX.center > MIN_SPAN) &&
                      (cal_RX.center - cal_RX.min_val > MIN_SPAN);
                    
    cal_LY.range_ok = (cal_LY.max_val - cal_LY.center > MIN_SPAN) &&
                      (cal_LY.center - cal_LY.min_val > MIN_SPAN);
                    
    cal_RY.range_ok = (cal_RY.max_val - cal_RY.center > MIN_SPAN) &&
                      (cal_RY.center - cal_RY.min_val > MIN_SPAN);
                      
    // ... 四轴同理
    bool all_ok = cal_LX.range_ok && cal_LY.range_ok &&
                  cal_RX.range_ok && cal_RY.range_ok;

    cal_state = all_ok ? CAL_DONE : CAL_ERROR;

    // screen_log_line("[CAL] %s\n", all_ok ? "OK" : "FAILED, recalibrate");
}


void joystick_update() {
    // 每次主循环只读 1 个样本，避免阻塞

    int raw_LX = analogRead(L_X_Pin);
    int raw_LY = analogRead(L_Y_Pin);
    int raw_RX = analogRead(R_X_Pin);
    int raw_RY = analogRead(R_Y_Pin);
    
    switch (cal_state) {
    case CAL_CENTER:
        sum_LX += raw_LX; sum_LY += raw_LY;
        sum_RX += raw_RX; sum_RY += raw_RY;
        if (++cnt_center >= CENTER_SAMPLES) {
            cal_LX.center = sum_LX / cnt_center;
            cal_LY.center = sum_LY / cnt_center;
            cal_RX.center = sum_RX / cnt_center;
            cal_RY.center = sum_RY / cnt_center;
            cal_state = CAL_RANGE;
            cal_start_ms = millis();
            // screen_log_line("[CAL] center done, push sticks to limits...");
        }
        break;

    case CAL_RANGE:
        update_range(cal_LX, raw_LX);
        update_range(cal_LY, raw_LY);
        update_range(cal_RX, raw_RX);
        update_range(cal_RY, raw_RY);
        // 按键确认或超时结束
        // if (cal_finish_requested || millis() - cal_start_ms > 15000) {
        if (event_L_right || millis() - cal_start_ms > 15000) {
            // screen_log_line("[CAL] range done, finishing...");
            finish_calibration();
        }
        break;

    case CAL_DONE:
        // 正常输出
        joystick_initialized = true;
        break;

    case CAL_ERROR:
        // 等待用户重新进入校准
        break;
    }
}

int normalize_axis(const AxisCal &c, int raw, int deadzone) {
    int delta = raw - c.center;
    if (abs(delta) < deadzone) return 0;   // 死区

    int out;
    if (delta > 0) {
        int span = c.max_val - c.center - deadzone;
        if (span <= 0) return 0;
        out = (long)(delta - deadzone) * 100 / span;
    } else {
        int span = c.center - c.min_val - deadzone;
        if (span <= 0) return 0;
        out = (long)(delta + deadzone) * 100 / span;
    }
    return constrain(out, -100, 100);
}

int normalize_axis_to_crsf(const AxisCal &c, int raw, int deadzone) {
    int delta = raw - c.center;
    if (abs(delta) < deadzone) return 0;   // 死区

    int out;
    if (delta > 0) {
        int span = c.max_val - c.center - deadzone;
        if (span <= 0) return 0;
        out = (long)(delta - deadzone) * 1024 / span;
    } else {
        int span = c.center - c.min_val - deadzone;
        if (span <= 0) return 0;
        out = (long)(delta + deadzone) * 1024 / span;
    }
    return constrain(out, -1024, 1024);
}