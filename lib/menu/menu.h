#ifndef MENU_H
#define MENU_H
#include "lvgl_port.h"


void screen_log_line(const char *fmt, ...);
void screen_log(const char *fmt, ...);


#define MENU_MAX_ITEMS_PER_PAGE 12
#define MENU_STACK_MAX           8
#define MENU_CACHE_MAX          16

// 菜单项类型
typedef enum {
    MI_SUBMENU,   // 进入子页
    MI_ACTION,    // 执行回调
    MI_VALUE,     // 数值调节（选中后上下调值）
    MI_TOGGLE,    // 开关
    MI_BACK,      // 返回上一页
} mi_type_t;

typedef struct menu_node menu_node_t;

struct menu_node {
    const char       *name;
    mi_type_t         type;

    // MI_SUBMENU
    const menu_node_t *children;
    int                child_count;

    // MI_ACTION
    void             (*on_action)(void);

    // MI_VALUE / MI_TOGGLE
    int               *value_ptr;
    int                value_min, value_max, value_step;
    const char        *value_fmt;      // 例如 "%d" 或 "%d%%"，NULL 默认 "%d"
};

// 输入事件
typedef enum {
    MENU_UP = 0,
    MENU_DOWN,
    MENU_ENTER,
    MENU_BACK,
} menu_event_t;

// 初始化（只调用一次），传入根菜单数组
void menu_init(lv_display_t *disp, const menu_node_t *root, int root_count);

// 输入入口，任意任务可调（内部加锁）
bool menu_input(menu_event_t ev);

// 外部修改了 value_ptr 指向的变量后，调它刷新显示
void menu_refresh_values(void);



// ---- 运行时配置变量（value_ptr 指向它们，立即生效）----
static int cfg_deadzone = 80;
static int cfg_expo     = 0;
static int cfg_reverse_lx = 0;
static int cfg_reverse_ly = 0;

// ---- 最深层：Reverse 子菜单 ----
static const menu_node_t reverse_items[] = {
    { .name = "LX", .type = MI_TOGGLE, .value_ptr = &cfg_reverse_lx },
    { .name = "LY", .type = MI_TOGGLE, .value_ptr = &cfg_reverse_ly },
    { .name = "Back", .type = MI_BACK },
};

// ---- 设置页 ----
static const menu_node_t settings_items[] = {
    { .name = "Deadzone", .type = MI_VALUE,
      .value_ptr = &cfg_deadzone, .value_min = 0, .value_max = 200,
      .value_step = 10, .value_fmt = "%d" },
    { .name = "Expo", .type = MI_VALUE,
      .value_ptr = &cfg_expo, .value_min = 0, .value_max = 100,
      .value_step = 5, .value_fmt = "%d%%" },
    { .name = "Reverse", .type = MI_SUBMENU,
      .children = reverse_items,
      .child_count = sizeof(reverse_items) / sizeof(reverse_items[0]) },
    { .name = "Back", .type = MI_BACK },
};

// ---- 根菜单 ----
static void start_calibrate(void) { /* 你的校准入口 */ }
static void enter_debug(void)     { /* 你的 debug 页入口 */ }

static const menu_node_t root_items[] = {
    { .name = "Calibrate", .type = MI_ACTION,  .on_action = start_calibrate },
    { .name = "Settings",  .type = MI_SUBMENU,
      .children = settings_items,
      .child_count = sizeof(settings_items) / sizeof(settings_items[0]) },
    { .name = "Debug",     .type = MI_ACTION,  .on_action = enter_debug },
    { .name = "About",     .type = MI_ACTION,  .on_action = NULL },
};


#endif