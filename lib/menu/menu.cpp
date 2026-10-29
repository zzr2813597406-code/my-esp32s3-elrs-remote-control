#include <Arduino.h>
#include "lvgl_port.h"
#include <stdarg.h>
#include <stdio.h>
#include "joystick.h"
#include "menu.h"

/* ============================================================
 * 屏幕覆盖层：挂在 disp1 的顶层（lv_layer_top），盖在所有菜单页之上。
 * 用来显示校准进度、错误、日志——不占页面、不打断导航。
 * ============================================================ */
#define OVL_W         128
#define OVL_H          64
#define LOG_LINES       5
#define LOG_LINE_LEN   64

static lv_obj_t *ovl_box   = NULL;
static lv_obj_t *ovl_title = NULL;
static lv_obj_t *ovl_body  = NULL;
static bool      ovl_visible = false;

static char log_lines[LOG_LINES][LOG_LINE_LEN];
static int  log_head  = 0;
static int  log_count = 0;

lv_display_t *disp1 = NULL;

static void ovl_ensure(void)
{
    if (ovl_box) return;

    disp1 = lvgl_port_get_disp1();
    if (!disp1) return;

    lv_display_set_default(disp1);

    ovl_box = lv_obj_create(lv_display_get_layer_top(disp1));
    lv_obj_set_size(ovl_box, OVL_W, OVL_H);
    lv_obj_set_pos(ovl_box, 0, 0);
    lv_obj_set_style_bg_color(ovl_box, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(ovl_box, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(ovl_box, 0, 0);
    lv_obj_set_style_radius(ovl_box, 0, 0);
    lv_obj_set_style_pad_all(ovl_box, 0, 0);
    lv_obj_clear_flag(ovl_box, LV_OBJ_FLAG_SCROLLABLE);

    ovl_title = lv_label_create(ovl_box);
    lv_obj_set_style_text_color(ovl_title, lv_color_white(), 0);
    lv_obj_set_style_text_font(ovl_title, &lv_font_montserrat_10, 0);
    lv_obj_align(ovl_title, LV_ALIGN_TOP_MID, 0, 1);

    ovl_body = lv_label_create(ovl_box);
    lv_obj_set_width(ovl_body, OVL_W - 8);
    lv_obj_set_pos(ovl_body, 4, 14);
    lv_label_set_long_mode(ovl_body, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_color(ovl_body, lv_color_white(), 0);
    lv_obj_set_style_text_font(ovl_body, &lv_font_montserrat_8, 0);

    lv_obj_add_flag(ovl_box, LV_OBJ_FLAG_HIDDEN);
}

static void ovl_apply(const char *title, const char *body)
{
    ovl_ensure();
    if (!ovl_box) return;

    lv_label_set_text(ovl_title, title ? title : "");
    lv_label_set_text(ovl_body, body ? body : "");

    lv_obj_clear_flag(ovl_box, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(ovl_box);
    ovl_visible = true;
}

void ui_overlay_show(const char *title, const char *body)
{
    lvgl_lock();
    ovl_apply(title, body);
    lvgl_unlock();
}

void ui_overlay_update(const char *title, const char *body)
{
    lvgl_lock();
    ovl_apply(title, body);
    lvgl_unlock();
}

void ui_overlay_hide(void)
{
    lvgl_lock();
    if (ovl_box) lv_obj_add_flag(ovl_box, LV_OBJ_FLAG_HIDDEN);
    ovl_visible = false;
    lvgl_unlock();
}

bool ui_overlay_active(void)
{
    return ovl_visible;
}

// 兼容原来的屏幕日志：滚动显示最近几行
void screen_log_line(const char *fmt, ...)
{
    char line[LOG_LINE_LEN];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(line, sizeof(line), fmt, ap);
    va_end(ap);

    strncpy(log_lines[log_head], line, LOG_LINE_LEN - 1);
    log_lines[log_head][LOG_LINE_LEN - 1] = '\0';
    log_head = (log_head + 1) % LOG_LINES;
    if (log_count < LOG_LINES) log_count++;

    static char view[LOG_LINES * LOG_LINE_LEN];
    view[0] = '\0';
    for (int i = 0; i < log_count; i++) {
        int idx = (log_head - log_count + i + LOG_LINES) % LOG_LINES;
        strncat(view, log_lines[idx], sizeof(view) - strlen(view) - 2);
        strncat(view, "\n", sizeof(view) - strlen(view) - 1);
    }

    ui_overlay_show("LOG", view);
}

void screen_log(const char *fmt, ...)
{
    char buf[256];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);

    ui_overlay_show("LOG", buf);
}

/*_________________________________菜单实现______________________________________________________________________________________________________________________________________________________________*/

typedef struct {
    const menu_node_t *items;
    int count;
    lv_obj_t *scr;
    lv_obj_t *viewport; //只显示屏幕大小的窗口，行放在里面，超出的被裁剪；
    lv_obj_t *rows[MENU_MAX_ITEMS_PER_PAGE];
    lv_obj_t *name_lbl[MENU_MAX_ITEMS_PER_PAGE];  // 名称（编辑态要改颜色）
    lv_obj_t *val_lbl[MENU_MAX_ITEMS_PER_PAGE];   // 仅 VALUE/TOGGLE 有
    lv_obj_t *focus;                              // 四角选择框（整页只有一个）
    bool      is_page;                            // true: 自定义内容页（没有行）
} menu_page_t;

typedef struct {
    const menu_node_t *items;
    menu_page_t       *page;    // 指向缓存里的页面，省的每次 find_page
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

// 页面切换动画时长（ms）。
// 128x64 I2C 屏全屏一帧就要 ~20ms，滑动动画实际只有 3~4 帧，看起来是跳变而不是滑动，
// 所以默认 0（瞬时切换，最跟手）。想要滑动效果就改回 100。
#define MENU_ANIM_MS 0

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

// ---- 选中框：四角括号 ----
#define FOCUS_CORNER_LEN 6      // 每个角的边长（像素）
#define FOCUS_INSET      1      // 相对行内缩，避免压到相邻行

static void focus_draw_cb(lv_event_t *e)
{
    lv_obj_t        *obj   = (lv_obj_t *)lv_event_get_target(e);
    lv_layer_t      *layer = lv_event_get_layer(e);

    lv_area_t a;
    lv_obj_get_coords(obj, &a);

    lv_draw_line_dsc_t d;
    lv_draw_line_dsc_init(&d);
    d.color       = lv_color_white();
    d.width       = 1;
    d.round_start = 0;
    d.round_end   = 0;

    const int32_t L = FOCUS_CORNER_LEN;

#define FOCUS_SEG(x1, y1, x2, y2)                       \
    do {                                                \
        d.p1.x = (x1); d.p1.y = (y1);                   \
        d.p2.x = (x2); d.p2.y = (y2);                   \
        lv_draw_line(layer, &d);                        \
    } while (0)

    FOCUS_SEG(a.x1,     a.y1,     a.x1 + L, a.y1);       // 左上 横
    FOCUS_SEG(a.x1,     a.y1,     a.x1,     a.y1 + L);   // 左上 竖
    FOCUS_SEG(a.x2 - L, a.y1,     a.x2,     a.y1);       // 右上 横
    FOCUS_SEG(a.x2,     a.y1,     a.x2,     a.y1 + L);   // 右上 竖
    FOCUS_SEG(a.x1,     a.y2 - L, a.x1,     a.y2);       // 左下 竖
    FOCUS_SEG(a.x1,     a.y2,     a.x1 + L, a.y2);       // 左下 横
    FOCUS_SEG(a.x2,     a.y2 - L, a.x2,     a.y2);       // 右下 竖
    FOCUS_SEG(a.x2 - L, a.y2,     a.x2,     a.y2);       // 右下 横

#undef FOCUS_SEG
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
        lv_obj_set_style_text_font(t, &lv_font_montserrat_10, 0);
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
        lv_obj_set_style_text_font(name, &lv_font_montserrat_10, 0);
        lv_obj_align(name, LV_ALIGN_LEFT_MID, 3, 0);
        pg->name_lbl[i] = name;

        // 数值：右对齐
        if (n->type == MI_VALUE || n->type == MI_TOGGLE) {
            lv_obj_t *v = lv_label_create(row);
            lv_obj_set_style_text_color(v, lv_color_hex(0xFFD060), 0);
            lv_obj_set_style_text_font(v, &lv_font_montserrat_10, 0);
            lv_obj_align(v, LV_ALIGN_RIGHT_MID, -3, 0);
            pg->val_lbl[i] = v;
            update_value_text(v, n);
        }

        pg->rows[i] = row;
    }

    // ---- 四角选择框：整页只建一个，高亮时挪到当前行上 ----
    pg->focus = lv_obj_create(vp);
    lv_obj_remove_style_all(pg->focus);                 // 不要默认的白底/边框
    lv_obj_set_size(pg->focus, row_w - 2 * FOCUS_INSET,
                               row_h - 2 * FOCUS_INSET);
    lv_obj_set_pos(pg->focus, margin + FOCUS_INSET, pad_top + FOCUS_INSET);
    lv_obj_clear_flag(pg->focus, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(pg->focus, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(pg->focus, focus_draw_cb, LV_EVENT_DRAW_MAIN, NULL);
    lv_obj_move_foreground(pg->focus);

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
        bool sel = (i == idx);

        if (sel && edit_mode) {
            // 编辑态：整行反色（白底黑字），单色屏上比任何颜色都醒目
            lv_obj_set_style_bg_color(pg->rows[i], lv_color_white(), 0);
            lv_obj_set_style_bg_opa(pg->rows[i], LV_OPA_COVER, 0);
            if (pg->name_lbl[i])
                lv_obj_set_style_text_color(pg->name_lbl[i], lv_color_black(), 0);
            if (pg->val_lbl[i])
                lv_obj_set_style_text_color(pg->val_lbl[i], lv_color_black(), 0);
        } else {
            // 普通态：行底色透明，选中与否交给四角框表示
            lv_obj_set_style_bg_opa(pg->rows[i], LV_OPA_TRANSP, 0);
            if (pg->name_lbl[i])
                lv_obj_set_style_text_color(pg->name_lbl[i], lv_color_white(), 0);
            if (pg->val_lbl[i])
                lv_obj_set_style_text_color(pg->val_lbl[i], lv_color_hex(0xFFD060), 0);
        }
    }

    // ---- 四角框：编辑态藏起来（白底上画白线等于没画）----
    if (pg->focus) {
        if (edit_mode) {
            lv_obj_add_flag(pg->focus, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_set_pos(pg->focus,
                           lv_obj_get_x(pg->rows[idx]) + FOCUS_INSET,
                           lv_obj_get_y(pg->rows[idx]) + FOCUS_INSET);
            lv_obj_clear_flag(pg->focus, LV_OBJ_FLAG_HIDDEN);
            lv_obj_move_foreground(pg->focus);
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

    lv_display_set_default(menu_disp);//提前设置默认显示，防止在动画过程中被切换
    menu_page_t *pg = get_or_create_page(items, count, title);
    if (!pg) return;

    nav[depth].items  = items;
    nav[depth].page   = pg;
    nav[depth].count  = pg->count;
    nav[depth].cursor = 0;
    depth++;
    editing = false;

    
    lv_scr_load_anim(pg->scr, LV_SCR_LOAD_ANIM_MOVE_LEFT, MENU_ANIM_MS, 0, false);

    if (pg->viewport) lv_obj_scroll_to_y(pg->viewport, 0, LV_ANIM_OFF);
    highlight(pg, 0, false);
}

/**
 * 进入 MI_PAGE 自定义页：首次进入时建屏并调用 page_open，
 * 之后复用同一块屏（状态保留），只由 page_update 刷新内容。
 */
static void push_page(const menu_node_t *n)
{
    if (depth >= MENU_STACK_MAX) return;

    lv_display_set_default(menu_disp);      // 建屏前必须切好，理由同 push()

    menu_page_t *pg   = find_page(n);       // 按节点地址做 key
    bool         fresh = false;

    if (!pg) {
        if (cache_n >= MENU_CACHE_MAX) return;

        pg = &cache[cache_n];
        memset(pg, 0, sizeof(*pg));
        pg->items   = n;
        pg->count   = 0;
        pg->is_page = true;

        pg->scr = lv_obj_create(NULL);
        lv_obj_set_style_bg_color(pg->scr, lv_color_black(), 0);
        lv_obj_clear_flag(pg->scr, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_pad_all(pg->scr, 0, 0);

        cache_n++;
        fresh = true;
    }

    nav[depth].items  = n;
    nav[depth].page   = pg;
    nav[depth].count  = 0;
    nav[depth].cursor = 0;
    depth++;
    editing = false;

    if (fresh && n->page_open) n->page_open(pg->scr);   // 先建内容再切屏，避免闪一下空白

    lv_scr_load_anim(pg->scr, LV_SCR_LOAD_ANIM_MOVE_LEFT, MENU_ANIM_MS, 0, false);
}

static void pop(void)
{
    if (depth <= 1) return;

    // 离开自定义页时给它一次收尾机会
    menu_page_t *from = nav[depth - 1].page;
    if (from && from->is_page && from->items && from->items->page_close) {
        from->items->page_close(from->scr);
    }

    depth--;
    editing = false;

    menu_page_t *pg = nav[depth - 1].page;
    if (!pg) return;

    lv_display_set_default(menu_disp);
    lv_scr_load_anim(pg->scr, LV_SCR_LOAD_ANIM_MOVE_RIGHT, MENU_ANIM_MS, 0, false);
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
    nav[0].page   = pg;
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
    menu_page_t *pg = f->page ? f->page : find_page(f->items);

    // ---- 自定义内容页：只响应 BACK，其余按键留给页面自己（这里先忽略）----
    if (pg && pg->is_page) {
        bool consumed_page = true;
        if (ev == MENU_BACK && depth > 1) {
            pop();
        } else {
            consumed_page = false;
        }
        lvgl_unlock();
        return consumed_page;
    }

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
        case MI_PAGE:
            push_page(cur);
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

/**
 * 周期调用（比如 key_task 的 50ms 循环）：
 * 如果当前停在 MI_PAGE 页上，就调用它的 page_update 刷实时数据。
 * 内部会加 lvgl 锁，所以不要再持锁调用。
 */
void menu_update(void)
{
    if (depth == 0) return;

    lvgl_lock();
    menu_page_t *pg = nav[depth - 1].page;
    if (pg && pg->is_page && pg->items && pg->items->page_update) {
        pg->items->page_update(pg->scr);
    }
    lvgl_unlock();
}

// ============================================================
//          菜单树 / 自定义页内容（平时加东西就改这一段）
// ============================================================

// ---- 运行时配置：MI_VALUE / MI_TOGGLE 的 value_ptr 指向它们 ----
int cfg_deadzone   = 80;
int cfg_expo       = 0;
int cfg_reverse_lx = 0;
int cfg_reverse_ly = 0;

// ------------------------------------------------------------
// 示例：Debug 自定义页
//   进来时 page_open 建一次 UI，之后 menu_update() 每轮调 page_update 刷数据。
//   想换内容，整段替换即可：一个 128x64 的 scr 上随便摆 label / bar / line。
// ------------------------------------------------------------
static lv_obj_t *dbg_body = NULL;

static void debug_page_open(lv_obj_t *scr)
{
    lv_obj_set_style_bg_color(scr, lv_color_black(), 0);

    lv_obj_t *t = lv_label_create(scr);
    lv_label_set_text(t, "Debug");
    lv_obj_set_style_text_color(t, lv_color_white(), 0);
    lv_obj_set_style_text_font(t, &lv_font_montserrat_10, 0);
    lv_obj_align(t, LV_ALIGN_TOP_MID, 0, 1);

    dbg_body = lv_label_create(scr);
    lv_obj_set_style_text_color(dbg_body, lv_color_white(), 0);
    lv_obj_set_style_text_font(dbg_body, &lv_font_montserrat_10, 0);
    lv_obj_set_pos(dbg_body, 4, 16);
}

static void debug_page_update(lv_obj_t *scr)
{
    if (!dbg_body) return;
    lv_label_set_text_fmt(dbg_body, "LX %4d\nLY %4d\nRX %4d\nRY %4d",
                          L_X, L_Y, R_X, R_Y);
}

// ---- 最深层：Reverse 子菜单 ----
static const menu_node_t reverse_items[] = {
    { .name = "LX",   .type = MI_TOGGLE, .value_ptr = &cfg_reverse_lx },
    { .name = "LY",   .type = MI_TOGGLE, .value_ptr = &cfg_reverse_ly },
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

// 校准入口：只重置状态机，真正的采样在 key_task 的 joystick_update() 里推进
static void start_calibrate(void)
{
    joystick_start_calibration();
}

// ---- 根菜单 ----
const menu_node_t root_items[] = {
    { .name = "Calibrate", .type = MI_ACTION,  .on_action = start_calibrate },
    { .name = "Settings",  .type = MI_SUBMENU,
      .children = settings_items,
      .child_count = sizeof(settings_items) / sizeof(settings_items[0]) },
    { .name = "Debug",     .type = MI_PAGE,
      .page_open   = debug_page_open,
      .page_update = debug_page_update },
    { .name = "About",     .type = MI_ACTION,  .on_action = NULL },
};

const int root_items_count = sizeof(root_items) / sizeof(root_items[0]);



