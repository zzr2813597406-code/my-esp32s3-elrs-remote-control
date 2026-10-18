#ifndef KEYBOARD_H
#define KEYBOARD_H

// 按键按下事件标志（只在按下瞬间为 true）
extern bool event_L_left ;
extern bool event_L_right;
extern bool event_L_up   ;
extern bool event_L_down ;
extern bool event_R_left ;
extern bool event_R_right;
extern bool event_R_up   ;
extern bool event_R_down;

void keyboard_init();
void keyboard_scan();

#endif