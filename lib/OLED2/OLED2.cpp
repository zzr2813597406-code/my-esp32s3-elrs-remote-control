#include "OLED2.h"
#include "lvgl_port.h"
#include "keyboard.h"
#include <Arduino.h>
#include "joystick.h"
#include "CRSF.h"

lv_obj_t *CH0_label = nullptr;
lv_obj_t *CH0_bar = nullptr;

static lv_obj_t *CH1_label = nullptr;
static lv_obj_t *CH1_bar = nullptr;

static lv_obj_t *CH2_label = nullptr;
static lv_obj_t *CH2_bar = nullptr;

static lv_obj_t *CH3_label = nullptr;
static lv_obj_t *CH3_bar = nullptr;

static lv_obj_t *CH4_label = nullptr;
static lv_obj_t *CH4_bar = nullptr;

static lv_obj_t *CH5_label = nullptr;
static lv_obj_t *CH5_bar = nullptr;

static bool initialized = false;

void bar_style_set(lv_obj_t *&bar,lv_obj_t *&label) {
    bar = lv_bar_create(lv_scr_act());
    label = lv_label_create(lv_scr_act());

    lv_obj_set_style_text_font(label, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(label, lv_color_white(), 0);

    // 设置外围颜色
    lv_obj_set_style_bg_color(bar, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, LV_PART_MAIN);
    // 设置指示器（已填充部分）颜色
    lv_obj_set_style_bg_color(bar, lv_color_white(), LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, LV_PART_INDICATOR);

    lv_obj_set_size(bar, 64, 10);
    lv_bar_set_range(bar, -100, 100);
}

void show_info_on_oled2() {
    lv_display_t *disp2 = lvgl_port_get_disp2();
    if (!disp2) {
        Serial.println("Error: disp2 not available");
        return;
    }
    lv_display_set_default(disp2);

    if (!initialized) {
        // 第一次调用时设置背景和创建标签
        lv_obj_set_style_bg_color(lv_scr_act(), lv_color_black(), 0);

        bar_style_set(CH0_bar,CH0_label);
        lv_obj_set_pos(CH0_label, 0, 1);
        lv_obj_set_pos(CH0_bar, 64, 1);

        bar_style_set(CH1_bar,CH1_label);
        lv_obj_set_pos(CH1_label, 0, 1+12);
        lv_obj_set_pos(CH1_bar, 64, 1+12);

        bar_style_set(CH2_bar,CH2_label);
        lv_obj_set_pos(CH2_label, 0, 1+24);
        lv_obj_set_pos(CH2_bar, 64, 1+24);

        bar_style_set(CH3_bar,CH3_label);
        lv_obj_set_pos(CH3_label, 0, 1+36);
        lv_obj_set_pos(CH3_bar, 64, 1+36);

        bar_style_set(CH4_bar,CH4_label);
        lv_obj_set_pos(CH4_label, 0, 1+48);
        lv_obj_set_pos(CH4_bar, 64, 1+48);

        initialized = true;
         
    }
    // 更新文本内容
    lv_label_set_text_fmt(CH0_label, "CH0:%04d", normalize_axis(cal_LX, L_X, 80));
    lv_bar_set_value(CH0_bar, normalize_axis(cal_LX, L_X, 80), LV_ANIM_OFF);

    lv_label_set_text_fmt(CH1_label, "CH1:%04d", normalize_axis(cal_LY, L_Y, 80));
    lv_bar_set_value(CH1_bar, normalize_axis(cal_LY, L_Y, 80), LV_ANIM_OFF);

    lv_label_set_text_fmt(CH2_label, "CH2:%04d", normalize_axis(cal_RX, R_X, 80));
    lv_bar_set_value(CH2_bar, normalize_axis(cal_RX, R_X, 80), LV_ANIM_OFF);

    lv_label_set_text_fmt(CH3_label, "CH3:%04d", normalize_axis(cal_RY, R_Y, 80));
    lv_bar_set_value(CH3_bar, normalize_axis(cal_RY, R_Y, 80), LV_ANIM_OFF);

    // lv_label_set_text_fmt(CH4_label, "CH4:%04d", normalize_axis(cal_RY, R_Y, 80));
    lv_bar_set_value(CH4_bar, channels.ch4, LV_ANIM_OFF);
    // 强制刷新
    // lv_refr_now(disp2);
}