#include "keyboard.h"
#include <Arduino.h>

#define Row1 9
#define Row2 10
#define Row3 13
#define Row4 14
#define Col1 48
#define Col2 47

// 按键状态变量（改为 volatile 如果在中断中使用）
bool L_left  = false;
bool L_right = false;
bool L_up    = false;
bool L_down  = false;
bool R_left  = false;
bool R_right = false;
bool R_up    = false;
bool R_down  = false;

// 按键上次状态（用于边沿检测）
bool last_L_left  = false;
bool last_L_right = false;
bool last_L_up    = false;
bool last_L_down  = false;
bool last_R_left  = false;
bool last_R_right = false;
bool last_R_up    = false;
bool last_R_down  = false;

// 定义事件标志
bool event_L_left   = false;
bool event_L_right  = false;
bool event_L_up     = false;
bool event_L_down   = false;
bool event_R_left   = false;
bool event_R_right  = false;
bool event_R_up     = false;
bool event_R_down   = false;

void keyboard_init() {
    pinMode(Row1, INPUT_PULLUP);
    pinMode(Row2, INPUT_PULLUP);
    pinMode(Row3, INPUT_PULLUP);
    pinMode(Row4, INPUT_PULLUP);
    pinMode(Col1, OUTPUT);
    pinMode(Col2, OUTPUT);
    digitalWrite(Col1, HIGH);
    digitalWrite(Col2, HIGH);
}

void keyboard_scan() {
    // 先保存上一轮的状态
    last_L_left  = L_left;
    last_L_right = L_right;
    last_L_up    = L_up;
    last_L_down  = L_down;
    last_R_left  = R_left;
    last_R_right = R_right;
    last_R_up    = R_up;
    last_R_down  = R_down;

    // 扫描左侧按键
    digitalWrite(Col1, LOW);
    digitalWrite(Col2, HIGH);
    delayMicroseconds(10);  // 等待电平稳定
    L_up  = (digitalRead(Row1) == LOW);
    L_left = (digitalRead(Row2) == LOW);
    L_right    = (digitalRead(Row3) == LOW);
    L_down  = (digitalRead(Row4) == LOW);

    // 扫描右侧按键
    digitalWrite(Col1, HIGH);
    digitalWrite(Col2, LOW);
    delayMicroseconds(10);
    R_up  = (digitalRead(Row1) == LOW);
    R_left = (digitalRead(Row2) == LOW);
    R_down = (digitalRead(Row3) == LOW);
    R_right  = (digitalRead(Row4) == LOW);

    // 恢复空闲状态
    digitalWrite(Col1, HIGH);
    digitalWrite(Col2, HIGH);

    // 边沿检测：当前为 LOW 且上次为 HIGH，表示刚刚按下
    event_L_left  = (L_left  && !last_L_left);
    event_L_right = (L_right && !last_L_right);
    event_L_up    = (L_up    && !last_L_up);
    event_L_down  = (L_down  && !last_L_down);
    event_R_left  = (R_left  && !last_R_left);
    event_R_right = (R_right && !last_R_right);
    event_R_up    = (R_up    && !last_R_up);
    event_R_down  = (R_down  && !last_R_down);
}