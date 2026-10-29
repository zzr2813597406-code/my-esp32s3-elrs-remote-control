#include "joystick.h"
#include <Arduino.h>
#include "keyboard.h"     // 事件标志（校准用 BACK 结束）
#include "menu.h"         // 屏幕覆盖层 ui_overlay_*
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
    CAL_IDLE,     // 空闲：还没开始校准（上电默认）
    CAL_CENTER,   // 采中心
    CAL_RANGE,    // 采行程
    CAL_DONE,     // 校准完成，可发数据
    CAL_ERROR     // 行程不合格，需重校
};

static CalState cal_state = CAL_IDLE;
uint32_t cal_start_ms = 0;
static uint32_t cal_msg_until = 0;   // 结果提示的自动关闭时间
static uint32_t cal_ui_ms    = 0;   // 实时行程提示的刷新节流

// 中心采样累加器
long sum_LX = 0, sum_LY = 0, sum_RX = 0, sum_RY = 0;
int  cnt_center = 0;
const int CENTER_SAMPLES = 100;   // 约 5 秒（key_task 周期 ~70ms）

/**
 * 重新开始一次校准。
 * 由菜单的 Calibrate 项调用；只重置状态，采样在 joystick_update() 里慢慢做。
 * 正在校准时重复调用会被忽略。
 */
void joystick_start_calibration(void)
{
    if (cal_state == CAL_CENTER || cal_state == CAL_RANGE) {
        Serial.println("[CAL] 正在校准中，忽略重复触发");
        return;
    }

    cal_LX = AxisCal();
    cal_LY = AxisCal();
    cal_RX = AxisCal();
    cal_RY = AxisCal();

    sum_LX = sum_LY = sum_RX = sum_RY = 0;
    cnt_center = 0;
    cal_start_ms = millis();
    joystick_initialized = false;
    cal_state = CAL_CENTER;

    ui_overlay_show("Calibrate", "Release sticks\nCenter 0/100");
}


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

    // 屏幕上给结果，3 秒后自动消失
    if (all_ok) {
        ui_overlay_update("CAL OK", "All 4 axes passed");
    } else {
        ui_overlay_update("CAL FAILED", "Span < 300\nRetry Calibrate");
    }
    cal_msg_until = millis() + 3000;

    Serial.printf("[CAL] %s | LX %d..%d @%d | LY %d..%d @%d | RX %d..%d @%d | RY %d..%d @%d\n",
                  all_ok ? "OK" : "FAILED(行程不足，重来)",
                  cal_LX.min_val, cal_LX.max_val, cal_LX.center,
                  cal_LY.min_val, cal_LY.max_val, cal_LY.center,
                  cal_RX.min_val, cal_RX.max_val, cal_RX.center,
                  cal_RY.min_val, cal_RY.max_val, cal_RY.center);
}


void joystick_update() {
    // 每次主循环只读 1 个样本，避免阻塞

    int raw_LX = analogRead(L_X_Pin);
    int raw_LY = analogRead(L_Y_Pin);
    int raw_RX = analogRead(R_X_Pin);
    int raw_RY = analogRead(R_Y_Pin);
    
    switch (cal_state) {
    case CAL_IDLE:
        // 还没开始校准，什么都不做
        break;

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
            ui_overlay_update("Push sticks", "Push to limits\nLEFT = done (15s)");
            Serial.printf("[CAL] 中心 LX=%d LY=%d RX=%d RY=%d\n",
                          cal_LX.center, cal_LY.center, cal_RX.center, cal_RY.center);
        } else if ((cnt_center % 10) == 0) {
            char buf[48];
            snprintf(buf, sizeof(buf), "Release sticks\nCenter %d/%d", cnt_center, CENTER_SAMPLES);
            ui_overlay_update("Calibrate", buf);
        }
        break;

    case CAL_RANGE:
        update_range(cal_LX, raw_LX);
        update_range(cal_LY, raw_LY);
        update_range(cal_RX, raw_RX);
        update_range(cal_RY, raw_RY);
        // 左键(BACK)确认，或用超时结束
        if (event_L_left || millis() - cal_start_ms > 15000) {
            finish_calibration();
        } else if (millis() - cal_ui_ms > 200) {   // 每 200ms 刷一次实时行程
            cal_ui_ms = millis();
            char buf[80];
            snprintf(buf, sizeof(buf), "LX %d/%d\nLY %d/%d\nRX %d/%d\nRY %d/%d",
                     cal_LX.min_val, cal_LX.max_val,
                     cal_LY.min_val, cal_LY.max_val,
                     cal_RX.min_val, cal_RX.max_val,
                     cal_RY.min_val, cal_RY.max_val);
            ui_overlay_update("Push sticks", buf);
        }
        break;

    case CAL_DONE:
        // 正常输出
        joystick_initialized = true;
        if (cal_msg_until && millis() > cal_msg_until) {
            ui_overlay_hide();
            cal_msg_until = 0;
        }
        break;

    case CAL_ERROR:
        if (cal_msg_until && millis() > cal_msg_until) {
            ui_overlay_hide();
            cal_msg_until = 0;
        }
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