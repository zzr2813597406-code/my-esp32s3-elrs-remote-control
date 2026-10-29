#ifndef MENU_H
#define MENU_H
#include "lvgl_port.h"

void screen_log_line(const char *fmt, ...);
void screen_log(const char *fmt, ...);

// disp1 顶层覆盖层：盖在所有菜单页之上，用来显示校准进度 / 结果 / 错误
void ui_overlay_show(const char *title, const char *body);
void ui_overlay_update(const char *title, const char *body);
void ui_overlay_hide(void);
bool ui_overlay_active(void);        // 覆盖层显示中（此时按键不再传给菜单）

#define MENU_MAX_ITEMS_PER_PAGE 12
#define MENU_STACK_MAX           8
#define MENU_CACHE_MAX          16

// 菜单项类型
typedef enum {
    MI_SUBMENU,   // 进入子页（children 数组）
    MI_ACTION,    // 执行回调，留在本页
    MI_VALUE,     // 数值调节（选中后上下调值）
    MI_TOGGLE,    // 开关
    MI_BACK,      // 返回上一页
    MI_PAGE,      // 自定义内容页：进去后显示你自己画的东西
} mi_type_t;

typedef struct menu_node menu_node_t;

struct menu_node {
    const char       *name;
    mi_type_t         type;

    // ---- MI_SUBMENU ----
    const menu_node_t *children;
    int                child_count;

    // ---- MI_ACTION ----
    void             (*on_action)(void);

    // ---- MI_VALUE / MI_TOGGLE ----
    int               *value_ptr;
    int                value_min, value_max, value_step;
    const char        *value_fmt;      // 例如 "%d" 或 "%d%%"，NULL 默认 "%d"

    // ---- MI_PAGE ----
    void             (*page_open)(lv_obj_t *scr);    // 首次进入时建 UI，scr 是画布（128x64 的全屏对象）
    void             (*page_update)(lv_obj_t *scr);  // 每次 menu_update() 调用，用来刷实时数据（可为 NULL）
    void             (*page_close)(lv_obj_t *scr);   // 离开本页时调用（可为 NULL）
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

// 周期调用：驱动自定义页的 page_update（内部加锁，不要在持锁时调）
void menu_update(void);

// ---- 运行时配置变量（定义在 menu.cpp）----
extern int cfg_deadzone;
extern int cfg_expo;
extern int cfg_reverse_lx;
extern int cfg_reverse_ly;

// ---- 根菜单（定义在 menu.cpp，加项就改那份数组）----
extern const menu_node_t root_items[];
extern const int         root_items_count;

#endif
