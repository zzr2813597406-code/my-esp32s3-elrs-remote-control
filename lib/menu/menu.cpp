#include <Arduino.h>
#include "lvgl_port.h"
#include <stdarg.h>
#include <stdio.h>
#include "joystick.h"
#include "menu.h"

#pragma region "Menu Display and Logging"

#define LOG_LINES 8
#define LOG_LINE_LEN 64

static char log_lines[LOG_LINES][LOG_LINE_LEN];
static int  log_head = 0;      // 下一个写入位置
static int  log_count = 0;

static lv_obj_t *log_label = NULL;
static char log_buf[1024];

#pragma endregion "Menu Display and Logging"


lv_display_t *disp1 = NULL;

void screen_log_line(const char *fmt, ...)
{
    char line[LOG_LINE_LEN];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(line, sizeof(line), fmt, ap);
    va_end(ap);
    lvgl_lock();
    strncpy(log_lines[log_head], line, LOG_LINE_LEN - 1);
    log_lines[log_head][LOG_LINE_LEN - 1] = '\0';
    log_head = (log_head + 1) % LOG_LINES;
    if (log_count < LOG_LINES) log_count++;

    // 拼接到显示缓冲
    static char view[LOG_LINES * LOG_LINE_LEN];
    view[0] = '\0';
    for (int i = 0; i < log_count; i++) {
        int idx = (log_head - log_count + i + LOG_LINES) % LOG_LINES;
        strncat(view, log_lines[idx], sizeof(view) - strlen(view) - 2);
        strncat(view, "\n", sizeof(view) - strlen(view) - 1);
    }

    // 同最小版：判空创建 label，然后 set_text(view)
    
    disp1 = lvgl_port_get_disp1();
    if (!disp1) { /* lvgl_port_unlock(); */ return; }
    lv_display_set_default(disp1);

    if (!log_label) {
        lv_obj_set_style_bg_color(lv_scr_act(), lv_color_black(), 0);
        log_label = lv_label_create(lv_scr_act());
        lv_obj_set_pos(log_label, 10, 10);
        lv_obj_set_width(log_label,lv_display_get_horizontal_resolution(disp1) - 20);
        lv_label_set_long_mode(log_label, LV_LABEL_LONG_WRAP);
        lv_obj_set_style_text_color(log_label, lv_color_white(), 0);
        lv_obj_set_style_text_font(log_label, &lv_font_montserrat_8, 0);
    }
    
    lv_label_set_text(log_label, view);
    
    // lv_refr_now(disp1);
    
    lvgl_unlock();
}

void screen_log(const char *fmt, ...)
{
    // 若 LVGL 跑在独立任务，这行 unlock 必须配对；同线程可去掉
    lvgl_lock();
    disp1 = lvgl_port_get_disp1();
    if (!disp1) { 
        lvgl_unlock(); 
        return; 
    }
    lv_display_set_default(disp1);

    if (!log_label) {
        lv_obj_set_style_bg_color(lv_scr_act(), lv_color_black(), 0);
        log_label = lv_label_create(lv_scr_act());
        lv_obj_set_pos(log_label, 10, 10);
        lv_obj_set_width(log_label,
            lv_display_get_horizontal_resolution(disp1) - 20);
        lv_label_set_long_mode(log_label, LV_LABEL_LONG_WRAP);
        lv_obj_set_style_text_color(log_label, lv_color_white(), 0);
        lv_obj_set_style_text_font(log_label, &lv_font_montserrat_8, 0);
    }

    va_list ap;
    va_start(ap, fmt);
    vsnprintf(log_buf, sizeof(log_buf), fmt, ap);
    va_end(ap);

    lv_label_set_text(log_label, log_buf);   // 关键：只改文本

    // lv_refr_now(disp1);
    lvgl_unlock();
}

/*_________________________________菜单实现______________________________________________________________________________________________________________________________________________________________*/

typedef struct {
    const menu_node_t *items;
    int count;
    lv_obj_t *scr;
    lv_obj_t *viewport; //只显示屏幕大小的窗口，行放在里面，超出的被裁剪；
    lv_obj_t *rows[MENU_MAX_ITEMS_PER_PAGE];
    lv_obj_t *val_lbl[MENU_MAX_ITEMS_PER_PAGE];   // 仅 VALUE/TOGGLE 有
} menu_page_t;

typedef struct {
    const menu_node_t *items;
    int count;
    int cursor;
} nav_frame_t;

static menu_page_t  cache[MENU_CACHE_MAX];
static int          cache_n = 0;

static nav_frame_t  nav[MENU_STACK_MAX];
static int          depth = 0;

static bool         editing = false;
static lv_display_t *menu_disp = NULL;

#define ROW_W   220
#define ROW_H    24
#define ROW_GAP   4
#define ROW_Y0   28

// ============================================================
// 页面缓存
// ============================================================

/**
 * 在缓存中查找与给定菜单项匹配的页面
 * @param items 要查找的菜单项指针
 * @return 如果找到匹配的页面，返回该页面的指针；否则返回NULL
 */
static menu_page_t *find_page(const menu_node_t *items)
{
    // 遍历缓存数组
    for (int i = 0; i < cache_n; i++)
        // 检查当前缓存项的items是否与给定的items匹配
        if (cache[i].items == items) return &cache[i];
    // 如果遍历完整个缓存都没有找到匹配项，返回NULL
    return NULL;
}

static void update_value_text(lv_obj_t *lbl, const menu_node_t *n)
{
    char buf[24];
    if (!lbl || !n->value_ptr) return;
    if (n->type == MI_TOGGLE) {
        snprintf(buf, sizeof(buf), "%s", *n->value_ptr ? "ON" : "OFF");
    } else {
        snprintf(buf, sizeof(buf), n->value_fmt ? n->value_fmt : "%d", *n->value_ptr);
    }
    lv_label_set_text(lbl, buf);
}

static menu_page_t *create_page(const menu_node_t *items, int count, const char *title)
{
    if (cache_n >= MENU_CACHE_MAX) return NULL;
    if (count > MENU_MAX_ITEMS_PER_PAGE) count = MENU_MAX_ITEMS_PER_PAGE;

    menu_page_t *pg = &cache[cache_n];
    memset(pg, 0, sizeof(*pg));
    pg->items = items;
    pg->count = count;

    // ---- 屏幕尺寸 ----
    int sw = lv_display_get_horizontal_resolution(menu_disp);
    int sh = lv_display_get_vertical_resolution(menu_disp);

    pg->scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(pg->scr, lv_color_hex(0x000000), 0);   // OLED 用纯黑
    lv_obj_clear_flag(pg->scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_all(pg->scr, 0, 0);

    // ---- 标题（占顶部一条，可选）----
    int title_h = 0;
    if (title) {
        lv_obj_t *t = lv_label_create(pg->scr);
        lv_label_set_text(t, title);
        lv_obj_set_style_text_color(t, lv_color_white(), 0);
        lv_obj_set_style_text_font(t, &lv_font_montserrat_8, 0);
        lv_obj_align(t, LV_ALIGN_TOP_MID, 0, 1);
        title_h = 12;
    }

    // ---- viewport：屏幕可用区域的裁剪窗口 ----
    lv_obj_t *vp = lv_obj_create(pg->scr);
    lv_obj_set_pos(vp, 0, title_h);
    lv_obj_set_size(vp, sw, sh - title_h);
    lv_obj_set_style_border_width(vp, 0, 0);
    lv_obj_set_style_pad_all(vp, 0, 0);
    lv_obj_set_style_bg_opa(vp, LV_OPA_TRANSP, 0);
    lv_obj_set_scrollbar_mode(vp, LV_SCROLLBAR_MODE_OFF);   // 不显示滚动条
    lv_obj_set_scroll_dir(vp, LV_DIR_VER);
    pg->viewport = vp;

    // ---- 行尺寸：按屏宽/屏高自适应 ----
    int margin  = 2;
    int row_w   = sw - 2 * margin;
    int row_h   = 16;                        // 小屏一行 16px 够放下 montserrat_8
    int row_gap = 1;
    int row_step = row_h + row_gap;

    // ---- 内容总高 < 窗口时，垂直居中；否则从顶部开始 ----
    int content_h = count * row_step - row_gap;
    int vp_h      = sh - title_h;
    int pad_top   = (content_h < vp_h) ? (vp_h - content_h) / 2 : 0;

    for (int i = 0; i < count; i++) {
        const menu_node_t *n = &items[i];

        lv_obj_t *row = lv_obj_create(vp);
        lv_obj_set_size(row, row_w, row_h);
        lv_obj_set_pos(row, margin, pad_top + i * row_step);
        lv_obj_set_style_border_width(row, 0, 0);
        lv_obj_set_style_radius(row, 2, 0);
        lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
        lv_obj_set_style_pad_all(row, 0, 0);
        lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

        // 名称：水平居中（小屏建议左对齐，见下方说明）
        lv_obj_t *name = lv_label_create(row);
        lv_label_set_text(name, n->name);
        lv_obj_set_style_text_color(name, lv_color_white(), 0);
        lv_obj_set_style_text_font(name, &lv_font_montserrat_8, 0);
        lv_obj_align(name, LV_ALIGN_LEFT_MID, 3, 0);

        // 数值：右对齐
        if (n->type == MI_VALUE || n->type == MI_TOGGLE) {
            lv_obj_t *v = lv_label_create(row);
            lv_obj_set_style_text_color(v, lv_color_hex(0xFFD060), 0);
            lv_obj_set_style_text_font(v, &lv_font_montserrat_8, 0);
            lv_obj_align(v, LV_ALIGN_RIGHT_MID, -3, 0);
            pg->val_lbl[i] = v;
            update_value_text(v, n);
        }

        pg->rows[i] = row;
    }

    cache_n++;
    return pg;
}

static menu_page_t *get_or_create_page(const menu_node_t *items, int count,
                                       const char *title)
{
    menu_page_t *pg = find_page(items);
    if (pg) return pg;
    return create_page(items, count, title);
}

// ============================================================
// 高亮
// ============================================================

static void highlight(menu_page_t *pg, int idx, bool edit_mode)
{
    if (!pg || pg->count == 0) return;

    for (int i = 0; i < pg->count; i++) {
        if (i == idx) {
            lv_color_t c = edit_mode
                ? lv_palette_main(LV_PALETTE_GREEN)
                : lv_palette_main(LV_PALETTE_BLUE);
            lv_obj_set_style_bg_color(pg->rows[i], c, 0);
            lv_obj_set_style_bg_opa(pg->rows[i], LV_OPA_COVER, 0);
        } else {
            lv_obj_set_style_bg_opa(pg->rows[i], LV_OPA_TRANSP, 0);
        }
    }

    // ★ 关键：让当前项滚进可视区（已可见则不动）
    if (pg->viewport) {
        lv_obj_scroll_to_view(pg->rows[idx], LV_ANIM_OFF);
    }
}

// ============================================================
// 导航
// ============================================================

static void push(const menu_node_t *items, int count, const char *title)
{
    if (depth >= MENU_STACK_MAX) return;
    menu_page_t *pg = get_or_create_page(items, count, title);
    if (!pg) return;

    nav[depth].items  = items;
    nav[depth].count  = pg->count;
    nav[depth].cursor = 0;
    depth++;
    editing = false;

    lv_display_set_default(menu_disp);
    lv_scr_load_anim(pg->scr, LV_SCR_LOAD_ANIM_MOVE_LEFT, 100, 0, false);

    if (pg->viewport) lv_obj_scroll_to_y(pg->viewport, 0, LV_ANIM_OFF);
    highlight(pg, 0, false);
}

static void pop(void)
{
    if (depth <= 1) return;
    depth--;
    editing = false;

    menu_page_t *pg = find_page(nav[depth - 1].items);
    if (!pg) return;

    lv_display_set_default(menu_disp);
    lv_scr_load_anim(pg->scr, LV_SCR_LOAD_ANIM_MOVE_RIGHT, 150, 0, false);
    highlight(pg, nav[depth - 1].cursor, false);
}

// ============================================================
// 对外接口
// ============================================================

void menu_init(lv_display_t *disp, const menu_node_t *root, int root_count)
{
    lvgl_lock();

    menu_disp = disp;
    depth = 0;
    cache_n = 0;
    editing = false;

    if (!menu_disp) { lvgl_unlock(); return; }
    lv_display_set_default(menu_disp);

    menu_page_t *pg = get_or_create_page(root, root_count, "MENU");
    if (!pg) { lvgl_unlock(); return; }

    nav[0].items  = root;
    nav[0].count  = pg->count;
    nav[0].cursor = 0;
    depth = 1;

    lv_scr_load(pg->scr);          // 首屏不用动画
    highlight(pg, 0, false);

    lvgl_unlock();
}

bool menu_input(menu_event_t ev)
{
    if (depth == 0) return false;

    lvgl_lock();

    nav_frame_t *f  = &nav[depth - 1];
    menu_page_t *pg = find_page(f->items);
    if (!pg || pg->count == 0) { lvgl_unlock(); return false; }

    const menu_node_t *cur = &f->items[f->cursor];
    bool consumed = true;

    switch (ev) {

    case MENU_UP:
    case MENU_DOWN: {
        int dir = (ev == MENU_UP) ? -1 : +1;
        if (editing && (cur->type == MI_VALUE || cur->type == MI_TOGGLE)) {
            if (cur->type == MI_TOGGLE) {
                *cur->value_ptr = !*cur->value_ptr;
            } else {
                int v = *cur->value_ptr + dir * cur->value_step;
                if (v < cur->value_min) v = cur->value_min;
                if (v > cur->value_max) v = cur->value_max;
                *cur->value_ptr = v;
            }
            update_value_text(pg->val_lbl[f->cursor], cur);
        } else {
            f->cursor = (f->cursor + dir + pg->count) % pg->count;
            highlight(pg, f->cursor, false);
        }
        break;
    }

    case MENU_ENTER:
        if (editing) {
            editing = false;
            highlight(pg, f->cursor, false);
            break;
        }
        switch (cur->type) {
        case MI_SUBMENU:
            push(cur->children, cur->child_count, cur->name);
            break;
        case MI_ACTION:
            if (cur->on_action) cur->on_action();
            break;
        case MI_VALUE:
        case MI_TOGGLE:
            editing = true;
            highlight(pg, f->cursor, true);
            break;
        case MI_BACK:
            pop();
            break;
        }
        break;

    case MENU_BACK:
        if (editing) {
            editing = false;
            highlight(pg, f->cursor, false);
        } else if (depth > 1) {
            pop();
        } else {
            consumed = false;   // 已在根页，交给上层
        }
        break;
    }

    lvgl_unlock();
    return consumed;
}

void menu_refresh_values(void)
{
    lvgl_lock();
    for (int i = 0; i < cache_n; i++) {
        menu_page_t *pg = &cache[i];
        for (int j = 0; j < pg->count; j++) {
            if (pg->val_lbl[j])
                update_value_text(pg->val_lbl[j], &pg->items[j]);
        }
    }
    lvgl_unlock();
}



