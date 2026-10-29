#ifndef JOYSTICK_H
#define JOYSTICK_H

extern int L_xuanniu;
extern int R_xuanniu;
extern int L_X;
extern int L_Y;
extern int R_X;
extern int R_Y;

extern bool joystick_initialized;

void xuanniu_read();
void joystick_read();

struct AxisCal {
    int center = 0;
    int min_val = 4095;   // 极值反过来初始化，便于 min/max 累积
    int max_val = 0;
    bool range_ok = false;
};

int normalize_axis(const AxisCal &c, int raw, int deadzone);
int normalize_axis_to_crsf(const AxisCal &c, int raw, int deadzone);

extern AxisCal cal_LX, cal_LY, cal_RX, cal_RY;

void joystick_update();

// 重新开始一次摇杆校准（只重置状态，采样在 joystick_update() 里推进）
void joystick_start_calibration(void);

#endif // XUANNIU_H