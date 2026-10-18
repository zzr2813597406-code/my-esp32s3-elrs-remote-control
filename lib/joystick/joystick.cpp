#include "joystick.h"
#include "driver/adc.h"
#include <Arduino.h>

#define L_xuanniu_Pin 4
#define R_xuanniu_Pin 5
#define L_X_Pin 7
#define L_Y_Pin 6
#define R_X_Pin 2
#define R_Y_Pin 1

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
    L_X = filterADC(L_X_Pin);
    L_Y = filterADC(L_Y_Pin);
    R_X = filterADC(R_X_Pin);
    R_Y = filterADC(R_Y_Pin);
}