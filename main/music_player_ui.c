/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "music_player_ui.h"

#include <stdio.h>
#include <string.h>

#include "esp_check.h"
#include "esp_log.h"
#include "lvgl.h"
#include "esp_lv_adapter.h"
#include "app.h"
#include "music_player_display.h"
#include "music_player_playback.h"
#include "music_player_playlist.h"
#include "music_player_config.h"

static const char *TAG = MUSIC_PLAYER_TAG_UI;

#define MUSIC_PLAYER_PLAYLIST_MAX_ITEMS      128

#define MUSIC_PLAYER_COLOR_BG           0x0E0E14
#define MUSIC_PLAYER_COLOR_PANEL        0x181824
#define MUSIC_PLAYER_COLOR_BTN          0x2A2A36
#define MUSIC_PLAYER_COLOR_ART          0x1A1A26
#define MUSIC_PLAYER_COLOR_TEXT         0xF2F2F5
#define MUSIC_PLAYER_COLOR_TEXT_DIM     0xA8A8B8
#define MUSIC_PLAYER_COLOR_TIME         0x8E8E9E
#define MUSIC_PLAYER_COLOR_ACCENT       0xFFD166
#define MUSIC_PLAYER_COLOR_PROGRESS_BG  0x2A2A36

#define UI_CLAMP(v, lo, hi)  ((v) < (lo) ? (lo) : ((v) > (hi) ? (hi) : (v)))

typedef struct {
    int  pad_x, pad_y_top, pad_y_bottom;
    int  btn_side, btn_nav, btn_play, art_size;
    int  dialog_w, dialog_h, ctrl_pad, ctrl_radius, font_size;
} music_player_ui_metrics_t;

typedef struct {
    lv_obj_t                  *screen;
    lv_obj_t                  *title_label;
    lv_obj_t                  *meta_label;
    lv_obj_t                  *progress_bar;
    lv_obj_t                  *elapsed_label;
    lv_obj_t                  *duration_label;
    lv_obj_t                  *play_btn;
    lv_obj_t                  *playlist_panel;
    lv_timer_t                *progress_timer;
    music_player_ui_metrics_t  metrics;
    const lv_font_t           *title_font;
#if CONFIG_ESP_LVGL_ADAPTER_ENABLE_FREETYPE
    esp_lv_adapter_ft_font_handle_t  ft_font_handle;
#endif
    bool  playing;
    int   volume;
    char  mode_text[32];
} music_player_ui_ctx_t;

static music_player_ui_ctx_t s_ui = {0};
static bool s_ui_inited = false;

typedef enum {
    MUSIC_PLAYER_UI_BTN_PREV = 1,
    MUSIC_PLAYER_UI_BTN_NEXT,
    MUSIC_PLAYER_UI_BTN_MODE,
    MUSIC_PLAYER_UI_BTN_LIST,
    MUSIC_PLAYER_UI_BTN_VOLUME_DOWN,
    MUSIC_PLAYER_UI_BTN_VOLUME_UP,
} music_player_ui_btn_id_t;

/**
 * @brief 按当前屏幕分辨率算出整套界面尺寸，实现大/中/小三档自适应。
 *
 * 判定规则：宽高都够大算 large；否则只要有一边比较大算 mid；再小就是默认档。
 * 本机 Korvo-2 是 320x240，会落到默认档（小屏），字号用 16。
 */
static void ui_metrics_init(music_player_ui_metrics_t *m)
{
    int w = lv_display_get_horizontal_resolution(NULL);
    int h = lv_display_get_vertical_resolution(NULL);
    if (w <= 0) {
        w = 800;
    }
    if (h <= 0) {
        h = 480;
    }
    bool large = (w >= 960 && h >= 560);
    bool mid = (!large && (w >= 700 || h >= 460));
    m->pad_x = large ? 56 : (mid ? 32 : 12);
    m->pad_y_top = large ? 28 : (mid ? 18 : 10);
    m->pad_y_bottom = large ? 26 : (mid ? 16 : 10);
    m->btn_side = large ? 56 : (mid ? 48 : 36);
    m->btn_nav = large ? 60 : (mid ? 52 : 40);
    m->btn_play = large ? 78 : (mid ? 64 : 48);
    m->art_size = large ? 120 : (mid ? 96 : 72);
    m->ctrl_pad = large ? 16 : (mid ? 12 : 8);
    m->ctrl_radius = large ? 24 : (mid ? 20 : 16);
    m->font_size = large ? MUSIC_PLAYER_FONT_SIZE : (mid ? 22 : 16);
    m->dialog_w = UI_CLAMP(large ? 720 : (mid ? 640 : 280), 200, w - m->pad_x * 2);
    m->dialog_h = UI_CLAMP(large ? 420 : (mid ? 360 : 220), 160, h - 24);
}

/**
 * @brief  投递一条不带数据的播放控制事件。
 *
 * 界面只表达意图，能不能播、播哪一首由播放引擎决定；
 * 空卡之类的无效操作在引擎侧统一忽略，界面不需要自己判断。
 *
 * @param[in]  evt  事件 ID，见 app.h 里的 music_player_event_t
 */
static inline void post_event(int32_t evt)
{
    app_event_post(evt, NULL, 0);
}

/**
 * @brief  投递"点播第 index 首"事件。
 *
 * @param[in]  index  曲目在播放列表中的序号
 */
static inline void post_play_index(int index)
{

    app_event_post(MUSIC_PLAYER_EVT_PLAY_INDEX, &index, sizeof(index));
}

/**
 * @brief 把毫秒格式化成 "分:秒" 文本，例如 83000 -> "1:23"。
 */
static void format_time_ms(int ms, char *buf, size_t buf_size)
{
    if (buf == NULL || buf_size == 0) {
        return;
    }
    if (ms < 0) {
        ms = 0;
    }
    int total_sec = ms / 1000;
    int min = total_sec / 60;
    int sec = total_sec % 60;
    snprintf(buf, buf_size, "%d:%02d", min, sec);
}

/**
 * @brief 刷新顶部那行"循环模式 · 音量 xx%"。
 */
static void refresh_meta_label(void)
{
    if (s_ui.meta_label == NULL) {
        return;
    }
    const char *mode = (s_ui.mode_text[0] != '\0') ? s_ui.mode_text : "";
    lv_label_set_text_fmt(s_ui.meta_label, "%s · 音量 %d%%", mode, s_ui.volume);
}

/**
 * @brief 刷新进度条与左右两端的已播放/总时长文本。
 *
 * 进度条内部量程是 0~1000 而不是 0~100，这样能表达 0.1% 的精度；
 * 总时长未知（duration_ms <= 0）时进度条归零、右侧显示 "--:--"。
 */
static void update_progress_widgets(int elapsed_ms, int duration_ms)
{
    char elapsed_text[16] = {0};
    char duration_text[16] = {0};

    if (duration_ms > 0) {
        int value = (int)(((int64_t)elapsed_ms * 1000) / duration_ms);
        if (value < 0) {
            value = 0;
        } else if (value > 1000) {
            value = 1000;
        }
        if (s_ui.progress_bar != NULL) {
            lv_bar_set_value(s_ui.progress_bar, value, LV_ANIM_OFF);
        }
        format_time_ms(elapsed_ms, elapsed_text, sizeof(elapsed_text));
        format_time_ms(duration_ms, duration_text, sizeof(duration_text));
    } else {
        if (s_ui.progress_bar != NULL) {
            lv_bar_set_value(s_ui.progress_bar, 0, LV_ANIM_OFF);
        }
        format_time_ms(elapsed_ms, elapsed_text, sizeof(elapsed_text));
        snprintf(duration_text, sizeof(duration_text), "--:--");
    }

    if (s_ui.elapsed_label != NULL) {
        lv_label_set_text(s_ui.elapsed_label, elapsed_text);
    }
    if (s_ui.duration_label != NULL) {
        lv_label_set_text(s_ui.duration_label, duration_text);
    }
}

/**
 * @brief 进度刷新定时器回调（运行在 LVGL 任务里）。
 *
 * 每 MUSIC_PLAYER_PROGRESS_POLL_MS 毫秒向播放模块要一次进度。
 * 这是"界面拉取状态"的方式，与播放模块那边"状态变化时推送刷新"互为补充。
 */
static void progress_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    int elapsed_ms = 0;
    int duration_ms = 0;
    if (music_player_playback_get_progress(&elapsed_ms, &duration_ms) != ESP_OK) {
        return;
    }
    update_progress_widgets(elapsed_ms, duration_ms);
}

/**
 * @brief 关闭播放列表弹层，并清掉句柄。
 *
 * 同时挂在"弹层背景"和"关闭按钮"上，因此点空白处也能关闭。
 */
static void close_playlist_event_cb(lv_event_t *e)
{
    (void)e;
    if (s_ui.playlist_panel != NULL) {
        lv_obj_delete(s_ui.playlist_panel);
        s_ui.playlist_panel = NULL;
    }
}

/**
 * @brief 点击播放列表里某一行的处理：点播该曲目后自动关闭弹层。
 *
 * 行号是通过事件的 user_data 传进来的（创建行时把 i 强转成指针存进去）。
 */
static void playlist_row_event_cb(lv_event_t *e)
{
    int index = (int)(intptr_t)lv_event_get_user_data(e);
    post_play_index(index);
    close_playlist_event_cb(e);
}

/**
 * @brief 创建播放列表弹层；若已打开则关闭它（列表按钮的切换行为）。
 *
 * 半透明黑底铺满整屏，中间是带标题栏和关闭按钮的面板，
 * 下面是可纵向滚动的曲目行；当前播放的那首会高亮并自动滚动到可见位置。
 * 曲目数超过上限时只渲染前若干首，末尾提示还有多少首未显示。
 */
static void create_playlist_dialog(void)
{
    if (s_ui.playlist_panel != NULL) {
        lv_obj_delete(s_ui.playlist_panel);
        s_ui.playlist_panel = NULL;
        return;
    }

    lv_obj_t *parent = (s_ui.screen != NULL) ? s_ui.screen : lv_screen_active();

    s_ui.playlist_panel = lv_obj_create(parent);
    lv_obj_remove_style_all(s_ui.playlist_panel);
    lv_obj_set_size(s_ui.playlist_panel, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_color(s_ui.playlist_panel, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(s_ui.playlist_panel, LV_OPA_50, 0);
    lv_obj_add_flag(s_ui.playlist_panel, LV_OBJ_FLAG_FLOATING | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(s_ui.playlist_panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(s_ui.playlist_panel, close_playlist_event_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *dialog = lv_obj_create(s_ui.playlist_panel);
    lv_obj_set_size(dialog, s_ui.metrics.dialog_w, s_ui.metrics.dialog_h);
    lv_obj_center(dialog);
    lv_obj_set_style_bg_opa(dialog, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(dialog, lv_color_hex(MUSIC_PLAYER_COLOR_PANEL), 0);
    lv_obj_set_style_text_color(dialog, lv_color_hex(MUSIC_PLAYER_COLOR_TEXT), 0);
    lv_obj_set_style_border_width(dialog, 1, 0);
    lv_obj_set_style_border_color(dialog, lv_color_hex(0x2A2A36), 0);
    lv_obj_set_style_radius(dialog, 16, 0);
    lv_obj_set_style_pad_all(dialog, 12, 0);
    lv_obj_set_flex_flow(dialog, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(dialog, 8, 0);
    lv_obj_clear_flag(dialog, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(dialog, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t *header = lv_obj_create(dialog);
    lv_obj_remove_style_all(header);
    lv_obj_set_width(header, lv_pct(100));
    lv_obj_set_height(header, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(header, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(header, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t *title = lv_label_create(header);
    lv_obj_set_style_text_font(title, s_ui.title_font, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(MUSIC_PLAYER_COLOR_TEXT), 0);
    lv_label_set_text(title, "播放列表");

    lv_obj_t *close_btn = lv_button_create(header);
    lv_obj_set_style_bg_color(close_btn, lv_color_hex(MUSIC_PLAYER_COLOR_BTN), 0);
    lv_obj_set_style_shadow_width(close_btn, 0, 0);
    lv_obj_add_event_cb(close_btn, close_playlist_event_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *close_label = lv_label_create(close_btn);
    lv_label_set_text(close_label, LV_SYMBOL_CLOSE);
    lv_obj_set_style_text_color(close_label, lv_color_hex(MUSIC_PLAYER_COLOR_TEXT), 0);

    lv_obj_t *list = lv_obj_create(dialog);
    lv_obj_set_width(list, lv_pct(100));
    lv_obj_set_flex_grow(list, 1);
    lv_obj_set_style_bg_opa(list, LV_OPA_TRANSP, 0);
    lv_obj_set_style_bg_color(list, lv_color_hex(MUSIC_PLAYER_COLOR_PANEL), 0);
    lv_obj_set_style_border_width(list, 0, 0);
    lv_obj_set_style_pad_all(list, 4, 0);
    lv_obj_set_style_radius(list, 0, 0);
    lv_obj_set_style_text_color(list, lv_color_hex(MUSIC_PLAYER_COLOR_TEXT), 0);
    lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scroll_dir(list, LV_DIR_VER);
    lv_obj_add_flag(list, LV_OBJ_FLAG_SCROLLABLE);

    int count = music_player_playlist_count();
    int current = -1;

    music_player_playlist_current(&current, NULL, 0);
    int show_count = count > MUSIC_PLAYER_PLAYLIST_MAX_ITEMS ? MUSIC_PLAYER_PLAYLIST_MAX_ITEMS : count;
    if (show_count <= 0) {
        lv_obj_t *empty = lv_label_create(list);
        lv_obj_set_style_text_font(empty, s_ui.title_font, 0);
        lv_obj_set_style_text_color(empty, lv_color_hex(MUSIC_PLAYER_COLOR_TEXT), 0);
        lv_label_set_text(empty, "未找到音乐");
        return;
    }

    lv_obj_t *current_row = NULL;
    for (int i = 0; i < show_count; i++) {
        char title_buf[MUSIC_PLAYER_TITLE_MAX] = {0};
        if (music_player_playlist_title(i, title_buf, sizeof(title_buf)) != ESP_OK) {
            continue;
        }
        char row_text[MUSIC_PLAYER_TITLE_MAX + 16] = {0};
        snprintf(row_text, sizeof(row_text), "%c %02d. %s", i == current ? '>' : ' ', i + 1, title_buf);
        lv_obj_t *row_btn = lv_button_create(list);
        lv_obj_set_width(row_btn, lv_pct(100));
        lv_obj_set_style_bg_opa(row_btn, i == current ? LV_OPA_30 : LV_OPA_TRANSP, 0);
        lv_obj_set_style_bg_color(row_btn, lv_color_hex(MUSIC_PLAYER_COLOR_ACCENT), 0);
        lv_obj_set_style_border_width(row_btn, 0, 0);
        lv_obj_set_style_shadow_width(row_btn, 0, 0);
        lv_obj_set_style_pad_all(row_btn, 6, 0);
        lv_obj_add_event_cb(row_btn, playlist_row_event_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);
        if (i == current) {
            current_row = row_btn;
        }

        lv_obj_t *row = lv_label_create(row_btn);
        lv_obj_set_width(row, lv_pct(100));
        lv_obj_set_style_text_font(row, s_ui.title_font, 0);
        lv_obj_set_style_text_color(row,
                                    lv_color_hex(i == current ? MUSIC_PLAYER_COLOR_ACCENT : MUSIC_PLAYER_COLOR_TEXT),
                                    0);
        lv_label_set_long_mode(row, LV_LABEL_LONG_DOT);
        lv_label_set_text(row, row_text);
    }

    if (count > show_count) {
        char more_text[48] = {0};
        snprintf(more_text, sizeof(more_text), "还有 %d 首未显示", count - show_count);
        lv_obj_t *more = lv_label_create(list);
        lv_obj_set_style_text_font(more, s_ui.title_font, 0);
        lv_obj_set_style_text_color(more, lv_color_hex(MUSIC_PLAYER_COLOR_TEXT_DIM), 0);
        lv_label_set_text(more, more_text);
    }

    if (current_row != NULL) {
        lv_obj_update_layout(list);
        lv_obj_scroll_to_y(list, lv_obj_get_y(current_row), LV_ANIM_OFF);
    }
}

/**
 * @brief 各控件共用的点击回调：按按钮编号分发到对应的播放命令。
 *
 * 播放/暂停键只投递一条 TOGGLE 命令，由控制任务按真实播放状态决定播放还是暂停，
 * 这样界面状态还没来得及刷新时连按也不会出现方向相反的操作；
 * 其余按键通过事件 user_data 里的编号区分。
 */
static void btn_event_cb(lv_event_t *e)
{
    lv_obj_t *btn = lv_event_get_target_obj(e);
    if (btn == s_ui.play_btn) {

        post_event(MUSIC_PLAYER_EVT_TOGGLE);
        return;
    }
    intptr_t id = (intptr_t)lv_event_get_user_data(e);
    switch (id) {
        case MUSIC_PLAYER_UI_BTN_PREV:
            post_event(MUSIC_PLAYER_EVT_PREV);
            break;
        case MUSIC_PLAYER_UI_BTN_NEXT:
            post_event(MUSIC_PLAYER_EVT_NEXT);
            break;
        case MUSIC_PLAYER_UI_BTN_MODE:
            post_event(MUSIC_PLAYER_EVT_TOGGLE_MODE);
            break;
        case MUSIC_PLAYER_UI_BTN_LIST:
            create_playlist_dialog();
            break;
        case MUSIC_PLAYER_UI_BTN_VOLUME_DOWN:
            post_event(MUSIC_PLAYER_EVT_VOLUME_DOWN);
            break;
        case MUSIC_PLAYER_UI_BTN_VOLUME_UP:
            post_event(MUSIC_PLAYER_EVT_VOLUME_UP);
            break;
        default:
            break;
    }
}

/**
 * @brief 装载标题字体：优先用 F: 盘里的 font.ttf（FreeType 渲染，可显示中文）。
 *
 * 失败时退回 LVGL 内置的 CJK 字体，保证界面不会因为字体缺失而崩掉。
 */
static const lv_font_t *load_title_font(void)
{
#if CONFIG_ESP_LVGL_ADAPTER_ENABLE_FREETYPE
    esp_lv_adapter_ft_font_config_t font_cfg = {
        .name = MUSIC_PLAYER_FONT_PATH,
        .size = s_ui.metrics.font_size > 0 ? s_ui.metrics.font_size : MUSIC_PLAYER_FONT_SIZE,
        .style = ESP_LV_ADAPTER_FT_FONT_STYLE_NORMAL,
    };
    if (esp_lv_adapter_ft_font_init(&font_cfg, &s_ui.ft_font_handle) == ESP_OK) {
        const lv_font_t *font = esp_lv_adapter_ft_font_get(s_ui.ft_font_handle);
        if (font != NULL) {
            return font;
        }
        esp_lv_adapter_ft_font_deinit(s_ui.ft_font_handle);
        s_ui.ft_font_handle = NULL;
    }
    ESP_LOGW(TAG, "FreeType 字体不可用，改用内置中文字体");
#endif
#if LV_FONT_SOURCE_HAN_SANS_SC_16_CJK
    return &lv_font_source_han_sans_sc_16_cjk;
#else
    return LV_FONT_DEFAULT;
#endif
}

/**
 * @brief 创建一个圆角/圆形图标按钮，并在其中居中放一个图标字符。
 *
 * id 为 0 表示这是播放/暂停键（回调里靠对象指针识别），否则把 id 存进 user_data；
 * accent 为真时用强调色填充背景（只有播放键用）。
 */
static lv_obj_t *create_icon_button(lv_obj_t *parent, int size, const char *symbol,
                                    music_player_ui_btn_id_t id, bool accent)
{
    lv_obj_t *btn = lv_button_create(parent);
    lv_obj_set_size(btn, size, size);
    lv_obj_set_style_radius(btn, accent ? LV_RADIUS_CIRCLE : 16, 0);
    lv_obj_set_style_shadow_width(btn, 0, 0);
    lv_obj_set_style_bg_color(btn, lv_color_hex(accent ? MUSIC_PLAYER_COLOR_ACCENT : MUSIC_PLAYER_COLOR_BTN), 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
    if (id == 0) {
        lv_obj_add_event_cb(btn, btn_event_cb, LV_EVENT_CLICKED, NULL);
    } else {
        lv_obj_add_event_cb(btn, btn_event_cb, LV_EVENT_CLICKED, (void *)(intptr_t)id);
    }

    lv_obj_t *label = lv_label_create(btn);
    lv_label_set_text(label, symbol);
    lv_obj_set_style_text_color(label, lv_color_hex(accent ? 0x16161C : MUSIC_PLAYER_COLOR_TEXT), 0);
    lv_obj_center(label);
    return btn;
}

/**
 * @brief 搭建整个播放器页面。
 *
 * 由外到内的顺序：整屏容器 -> 顶部标题区 / 中间占位区 / 底部控制栏 -> 各块内的具体控件，
 * 最后切到该屏幕并创建进度刷新定时器。
 * 创建过程中会顺便向播放模块问一次音量，让首帧就显示正确状态。
 */
static void create_player_screen(const lv_font_t *title_font)
{
    const music_player_ui_metrics_t *m = &s_ui.metrics;

    s_ui.screen = lv_obj_create(NULL);
    lv_obj_remove_style_all(s_ui.screen);
    lv_obj_set_size(s_ui.screen, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_color(s_ui.screen, lv_color_hex(MUSIC_PLAYER_COLOR_BG), 0);
    lv_obj_set_style_bg_opa(s_ui.screen, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_top(s_ui.screen, m->pad_y_top, 0);
    lv_obj_set_style_pad_bottom(s_ui.screen, m->pad_y_bottom, 0);
    lv_obj_set_style_pad_left(s_ui.screen, m->pad_x, 0);
    lv_obj_set_style_pad_right(s_ui.screen, m->pad_x, 0);
    lv_obj_set_flex_flow(s_ui.screen, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_ui.screen, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(s_ui.screen, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *title_block = lv_obj_create(s_ui.screen);
    lv_obj_remove_style_all(title_block);
    lv_obj_set_width(title_block, lv_pct(100));
    lv_obj_set_height(title_block, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(title_block, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(title_block, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_top(title_block, 8, 0);
    lv_obj_set_style_pad_row(title_block, 8, 0);

    s_ui.title_label = lv_label_create(title_block);
    lv_obj_set_width(s_ui.title_label, lv_pct(100));
    lv_label_set_long_mode(s_ui.title_label, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_align(s_ui.title_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(s_ui.title_label, title_font, 0);
    lv_obj_set_style_text_color(s_ui.title_label, lv_color_hex(MUSIC_PLAYER_COLOR_TEXT), 0);
    lv_label_set_text(s_ui.title_label, "准备播放");

    s_ui.meta_label = lv_label_create(title_block);
    lv_obj_set_style_text_font(s_ui.meta_label, title_font, 0);
    lv_obj_set_style_text_color(s_ui.meta_label, lv_color_hex(MUSIC_PLAYER_COLOR_TEXT_DIM), 0);

    s_ui.volume = MUSIC_PLAYER_DEFAULT_VOLUME;
    snprintf(s_ui.mode_text, sizeof(s_ui.mode_text), "%s",
             music_player_playlist_mode_text(MUSIC_PLAYER_MODE_REPEAT_ALL));
    refresh_meta_label();

    lv_obj_t *art_wrap = lv_obj_create(s_ui.screen);
    lv_obj_remove_style_all(art_wrap);
    lv_obj_set_width(art_wrap, lv_pct(100));
    lv_obj_set_flex_grow(art_wrap, 1);
    lv_obj_set_flex_flow(art_wrap, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(art_wrap, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(art_wrap, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *art = lv_obj_create(art_wrap);
    lv_obj_set_size(art, m->art_size, m->art_size);
    lv_obj_set_style_radius(art, 22, 0);
    lv_obj_set_style_bg_color(art, lv_color_hex(MUSIC_PLAYER_COLOR_ART), 0);
    lv_obj_set_style_bg_opa(art, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(art, 1, 0);
    lv_obj_set_style_border_color(art, lv_color_hex(0x2A2A36), 0);
    lv_obj_set_style_pad_all(art, 0, 0);
    lv_obj_clear_flag(art, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *art_label = lv_label_create(art);
    lv_label_set_text(art_label, LV_SYMBOL_AUDIO);
    lv_obj_set_style_text_color(art_label, lv_color_hex(MUSIC_PLAYER_COLOR_ACCENT), 0);
#if LV_FONT_MONTSERRAT_28
    lv_obj_set_style_text_font(art_label, &lv_font_montserrat_28, 0);
#endif
    lv_obj_center(art_label);

    lv_obj_t *progress_block = lv_obj_create(s_ui.screen);
    lv_obj_remove_style_all(progress_block);
    lv_obj_set_width(progress_block, lv_pct(100));
    lv_obj_set_height(progress_block, LV_SIZE_CONTENT);
    lv_obj_set_style_pad_bottom(progress_block, 14, 0);
    lv_obj_set_flex_flow(progress_block, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(progress_block, 8, 0);

    s_ui.progress_bar = lv_bar_create(progress_block);
    lv_obj_set_size(s_ui.progress_bar, lv_pct(100), 8);
    lv_bar_set_range(s_ui.progress_bar, 0, 1000);
    lv_bar_set_value(s_ui.progress_bar, 0, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(s_ui.progress_bar, lv_color_hex(MUSIC_PLAYER_COLOR_PROGRESS_BG), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_ui.progress_bar, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(s_ui.progress_bar, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_ui.progress_bar, lv_color_hex(MUSIC_PLAYER_COLOR_ACCENT), LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(s_ui.progress_bar, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_radius(s_ui.progress_bar, LV_RADIUS_CIRCLE, LV_PART_INDICATOR);

    lv_obj_t *time_row = lv_obj_create(progress_block);
    lv_obj_remove_style_all(time_row);
    lv_obj_set_width(time_row, lv_pct(100));
    lv_obj_set_height(time_row, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(time_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(time_row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    s_ui.elapsed_label = lv_label_create(time_row);
    lv_obj_set_style_text_color(s_ui.elapsed_label, lv_color_hex(MUSIC_PLAYER_COLOR_TIME), 0);
    lv_label_set_text(s_ui.elapsed_label, "0:00");

    s_ui.duration_label = lv_label_create(time_row);
    lv_obj_set_style_text_color(s_ui.duration_label, lv_color_hex(MUSIC_PLAYER_COLOR_TIME), 0);
    lv_label_set_text(s_ui.duration_label, "--:--");

    lv_obj_t *ctrl = lv_obj_create(s_ui.screen);
    lv_obj_remove_style_all(ctrl);
    lv_obj_set_width(ctrl, lv_pct(100));
    lv_obj_set_height(ctrl, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(ctrl, lv_color_hex(MUSIC_PLAYER_COLOR_PANEL), 0);
    lv_obj_set_style_bg_opa(ctrl, LV_OPA_80, 0);
    lv_obj_set_style_radius(ctrl, m->ctrl_radius, 0);
    lv_obj_set_style_border_width(ctrl, 1, 0);
    lv_obj_set_style_border_color(ctrl, lv_color_hex(0x2A2A36), 0);
    lv_obj_set_style_pad_all(ctrl, m->ctrl_pad, 0);
    lv_obj_set_flex_flow(ctrl, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(ctrl, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    create_icon_button(ctrl, m->btn_side, LV_SYMBOL_LIST, MUSIC_PLAYER_UI_BTN_LIST, false);
    create_icon_button(ctrl, m->btn_nav, LV_SYMBOL_PREV, MUSIC_PLAYER_UI_BTN_PREV, false);
    s_ui.play_btn = create_icon_button(ctrl, m->btn_play, LV_SYMBOL_PLAY, 0, true);
    create_icon_button(ctrl, m->btn_nav, LV_SYMBOL_NEXT, MUSIC_PLAYER_UI_BTN_NEXT, false);
    create_icon_button(ctrl, m->btn_side, LV_SYMBOL_MINUS, MUSIC_PLAYER_UI_BTN_VOLUME_DOWN, false);
    create_icon_button(ctrl, m->btn_side, LV_SYMBOL_PLUS, MUSIC_PLAYER_UI_BTN_VOLUME_UP, false);
    create_icon_button(ctrl, m->btn_side, LV_SYMBOL_LOOP, MUSIC_PLAYER_UI_BTN_MODE, false);

    lv_screen_load(s_ui.screen);

    s_ui.progress_timer = lv_timer_create(progress_timer_cb, MUSIC_PLAYER_PROGRESS_POLL_MS, NULL);
}

/**
 * @brief 界面初始化回调，在持有 LVGL 锁的上下文里执行真正的建界面动作。
 */
static void ui_init_cb(void *ctx)
{
    (void)ctx;
    s_ui.playing = false;
    ui_metrics_init(&s_ui.metrics);

    const lv_font_t *title_font = load_title_font();
    s_ui.title_font = title_font;
    create_player_screen(title_font);
}

/**
 * @brief 界面销毁回调的前向声明，供初始化失败时回滚使用。
 */
static void ui_deinit_cb(void *ctx);

/**
 * @brief  在持有 LVGL 锁的前提下，把状态快照写进各控件。
 *
 * @param[in]  ctx  指向 music_player_state_t 的状态快照
 */
static void ui_apply_state_cb(void *ctx)
{
    const music_player_state_t *state = (const music_player_state_t *)ctx;
    if (state == NULL) {
        return;
    }

    if (s_ui.title_label != NULL) {
        lv_label_set_text(s_ui.title_label, state->title);
    }
    snprintf(s_ui.mode_text, sizeof(s_ui.mode_text), "%s", state->mode_text);
    s_ui.volume = state->volume;
    refresh_meta_label();

    s_ui.playing = state->playing;
    if (s_ui.play_btn != NULL) {
        lv_obj_t *label = lv_obj_get_child(s_ui.play_btn, 0);
        if (label != NULL) {
            lv_label_set_text(label, state->playing ? LV_SYMBOL_PAUSE : LV_SYMBOL_PLAY);
        }
    }

    int elapsed_ms = 0;
    int duration_ms = 0;
    if (music_player_playback_get_progress(&elapsed_ms, &duration_ms) == ESP_OK) {
        update_progress_widgets(elapsed_ms, duration_ms);
    }
}

/**
 * @brief  播放状态事件回调：把播放引擎发来的状态快照刷到界面上。
 *
 * 回调运行在 mp_evt 任务里，动控件之前必须先拿到 LVGL 锁。
 *
 * @param[in]  arg   订阅时传入的上下文，本文件未使用
 * @param[in]  base  事件基，固定为 MUSIC_PLAYER_EVENT
 * @param[in]  id    事件 ID，本回调只订阅 STATE_CHANGED
 * @param[in]  data  指向 music_player_state_t 的状态快照
 */
static void ui_state_event_handler(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    (void)base;
    (void)id;
    if (data == NULL) {
        return;
    }

    esp_err_t ret = music_player_display_lock_run(ui_apply_state_cb, data);
    if (ret != ESP_OK) {
        ESP_LOGD(TAG, "界面刷新跳过: %s", esp_err_to_name(ret));
    }
}

/**
 * @brief  初始化界面：建控件 -> 启动 LVGL 任务 -> 订阅播放状态事件。
 *
 * 任一步失败都会把已经建好的界面回滚掉，避免留下"有控件但没人刷"的中间状态。
 *
 * @return
 *       - ESP_OK                 成功
 *       - ESP_ERR_INVALID_STATE  界面已经初始化过
 *       - 其它                   显示锁、LVGL 启动或事件订阅返回的错误码
 */
esp_err_t music_player_ui_init(void)
{
    ESP_RETURN_ON_FALSE(!s_ui_inited, ESP_ERR_INVALID_STATE, TAG, "界面已经初始化过");
    ESP_RETURN_ON_ERROR(music_player_display_lock_run(ui_init_cb, NULL), TAG, "创建播放器界面失败");

    esp_err_t ret = music_player_display_start();
    if (ret != ESP_OK) {
        ui_deinit_cb(NULL);
        return ret;
    }

    ESP_GOTO_ON_ERROR(app_event_subscribe(MUSIC_PLAYER_EVT_STATE_CHANGED, ui_state_event_handler, NULL),
                      err_cleanup, TAG, "订阅播放状态事件失败");

    s_ui_inited = true;
    return ESP_OK;

err_cleanup:
    ui_deinit_cb(NULL);
    return ESP_FAIL;
}

/**
 * @brief 销毁界面上的控件、定时器与字体句柄。
 *
 * 删除 screen 会连带释放挂在它下面的所有子控件，因此这里把相关指针统一置空，
 * 避免后续误用已释放的对象。
 */
static void ui_deinit_cb(void *ctx)
{
    (void)ctx;
    if (s_ui.progress_timer != NULL) {
        lv_timer_delete(s_ui.progress_timer);
        s_ui.progress_timer = NULL;
    }
    if (s_ui.playlist_panel != NULL) {
        lv_obj_delete(s_ui.playlist_panel);
        s_ui.playlist_panel = NULL;
    }
    if (s_ui.screen != NULL) {
        lv_obj_delete(s_ui.screen);
        s_ui.screen = NULL;
        s_ui.title_label = NULL;
        s_ui.meta_label = NULL;
        s_ui.progress_bar = NULL;
        s_ui.elapsed_label = NULL;
        s_ui.duration_label = NULL;
        s_ui.play_btn = NULL;
    }
#if CONFIG_ESP_LVGL_ADAPTER_ENABLE_FREETYPE
    if (s_ui.ft_font_handle != NULL) {
        esp_lv_adapter_ft_font_deinit(s_ui.ft_font_handle);
        s_ui.ft_font_handle = NULL;
    }
#endif
}

/**
 * @brief 反初始化界面。
 *
 * 拿不到 LVGL 锁时仍会强行清理一次，宁可有一点竞态风险也不能泄漏资源。
 */
void music_player_ui_deinit(void)
{
    if (!s_ui_inited) {
        return;
    }
    app_event_unsubscribe(MUSIC_PLAYER_EVT_STATE_CHANGED, ui_state_event_handler);
    esp_err_t ret = music_player_display_lock_run(ui_deinit_cb, NULL);
    if (ret != ESP_OK) {
        ui_deinit_cb(NULL);
    }
    memset(&s_ui, 0, sizeof(s_ui));
    s_ui_inited = false;
}
