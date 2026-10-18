#include <Arduino.h>
#include "lvgl_port.h"


#define MENU_ITEMS 6
static const char *menu_items[MENU_ITEMS] = {"menu1", "menu2", "menu3", "menu4", "menu5", "menu6"};
static int current_item = 0;
static lv_obj_t *menu_labels[MENU_ITEMS] = {nullptr};



int line_h = 20;

void update_menu_display(void)
{
    lv_display_t *disp1 = lvgl_port_get_disp1();
    if (!disp1) return;
    lv_display_set_default(disp1);
    // 设置黑色背景
    lv_obj_set_style_bg_color(lv_scr_act(), lv_color_black(), 0);

    // 创建或更新标签
    int start_y = 0;
    for (int i = 0; i < MENU_ITEMS; i++) {
        char line[32];
        if (i == current_item)
            snprintf(line, sizeof(line), "-> %s", menu_items[i]);
        else
            snprintf(line, sizeof(line), "   %s", menu_items[i]);

        if (menu_labels[i] == nullptr) {
            menu_labels[i] = lv_label_create(lv_scr_act());
            lv_obj_set_style_text_color(menu_labels[i], lv_color_white(), 0);
        }
        lv_label_set_text(menu_labels[i], line);
        lv_obj_set_pos(menu_labels[i], 10, start_y + i * line_h);
    }
    // 强制刷新
    lv_refr_now(disp1);
}

// ---------- 菜单操作 ----------
void menu_up(void) {
    if (current_item > 0) {
        current_item--;
        update_menu_display();
        Serial.printf("Menu up, index=%d\n", current_item);
    }
}

void menu_down(void) {
    if (current_item < MENU_ITEMS - 1) {
        current_item++;
        update_menu_display();
        Serial.printf("Menu down, index=%d\n", current_item);
    }
}

void menu_enter(void) {
    Serial.printf("Enter on: %s\n", menu_items[current_item]);
    // 暂时不创建任何 UI，只打印
}