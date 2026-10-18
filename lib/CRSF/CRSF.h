#ifndef CRSF_H
#define CRSF_H
#include "AlfredoCRSF.h"
#include <Arduino.h>

extern crsf_channels_t channels;

// 初始化 CRSF 通信（创建任务）
void crsf_init(void);
int mapChannel(int raw, int rawMin, int rawMax, int rawMid,
               int targetMin, int targetMax, int targetMid);
// 获取最新的通道值（如果需要外部读取，可以声明）
// 这里暂时不需要暴露，因为任务内部使用

#endif