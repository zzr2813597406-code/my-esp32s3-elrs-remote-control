#include "crsf.h"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include "AlfredoCRSF.h"
#include "joystick.h"
// 引脚定义
#define CRSF_RX_PIN 12
#define CRSF_TX_PIN 11

// 全局对象（仅在内部使用）
static AlfredoCRSF crsf;
crsf_channels_t channels;
static TaskHandle_t TaskCrsfHandle = NULL;

int gap = 20;
// 通用分段映射函数
int mapChannel(int raw, int rawMin, int rawMax, int rawMid,int targetMin, int targetMax, int targetMid) 
{
    if (raw < rawMid - 10) 
    {
        if (raw < rawMin + 10) raw = rawMin;  // 限制下界
        return map(raw, rawMin, rawMid, targetMin, targetMid);
    }

    else if (raw > rawMid + 10) 
    {
        if (raw > rawMax - 10) raw = rawMax;  // 限制上界
        return map(raw, rawMid, rawMax, targetMid, targetMax);
    }

    else if(raw >= rawMid - gap && raw <= rawMid + gap)
    {
        return targetMid;  // 中立点
    }
}

// CRSF 任务函数（核心 0 运行）
static void TaskCrsf(void *pvParameters) {
    Serial1.begin(420000, SERIAL_8N1, CRSF_RX_PIN, CRSF_TX_PIN);
    crsf.begin(Serial1);
    Serial.println("[CRSF] 启动");

    memset(&channels, 0, sizeof(channels));
    channels.ch0 = 1024;   // 固定中立点
    channels.ch1 = 1024;
    channels.ch2 = 1024;
    channels.ch3 = 1024;
    channels.ch4 = 1024;

    for (;;) {
        channels.ch0 = mapChannel(L_X, 165, 1920, 985, 0, 2047, 1024);
        channels.ch1 = mapChannel(L_Y, 210, 1910, 1035, 0, 2047, 1024);
        channels.ch2 = mapChannel(R_X, 190, 1950, 1010, 0, 2047, 1024);
        // channels.ch3 = mapChannel(R_Y, 300, 1850, 1030, 0, 2047, 1024);
        channels.ch3 = R_Y;
        channels.ch4 = mapChannel(L_xuanniu, 0, 2047, 1024, 500, 2047, 1024);
        // 发送 RC 通道帧
        crsf.writePacket(CRSF_ADDRESS_FLIGHT_CONTROLLER,
                         CRSF_FRAMETYPE_RC_CHANNELS_PACKED,
                         &channels, sizeof(channels));
        delayMicroseconds(200);
        crsf.update();
        vTaskDelay(20 / portTICK_PERIOD_MS); // 50Hz
    }
}

void crsf_init(void) {
    // 配置摇杆 ADC
    analogReadResolution(11);
    // analogSetPinAttenuation(JOYSTICK_PIN, ADC_11db);
    // 如果有其他 ADC 引脚，可以在这里一并设置

    // 创建 CRSF 任务，固定在核心 0
    xTaskCreatePinnedToCore(
        TaskCrsf,
        "CRSF",
        20000,        // 栈大小（根据实际需要调整）
        NULL,
        1,            // 优先级
        &TaskCrsfHandle,
        0             // 核心 0
    );
}