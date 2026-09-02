#include "ui.h"
#include "lvgl/lvgl.h"
#include "config.h"
#include "ui_widget.h"
#include "thread.h"
#include "my_font.h"
#include "config_manager.h"
#include <stdio.h>

static lv_obj_t *env_scr = NULL;

/* 刷新定时器 */
static lv_timer_t *s_timer_env = NULL;

/* 顶部状态栏 */
static lv_obj_t *s_env_status_lbl = NULL;
static lv_obj_t *s_env_status_dot = NULL;

/* 5 个参数卡片的数据标签 */
static lv_obj_t *s_val_temp  = NULL;  /* 温度 */
static lv_obj_t *s_val_humi  = NULL;  /* 湿度 */
static lv_obj_t *s_val_press = NULL;  /* 气压 */
static lv_obj_t *s_val_lux   = NULL;  /* 光照 */
static lv_obj_t *s_val_alt   = NULL;  /* 海拔 */

/* 5 个参数卡片的背景容器（用于变色） */
static lv_obj_t *s_card_temp  = NULL;
static lv_obj_t *s_card_humi  = NULL;
static lv_obj_t *s_card_press = NULL;
static lv_obj_t *s_card_lux   = NULL;
static lv_obj_t *s_card_alt   = NULL;

/* ======================================================================================================= */
/* ---------------------------------------------定时器----------------------------------------------------- */
/* ======================================================================================================= */

static void env_refresh_timer_cb(lv_timer_t *timer)
{
    (void)timer;

    AppConfig *cfg = config_get_global();
    bool enabled = (cfg && cfg->env_monitor_enabled);

    if (!enabled)
    {
        /* 未启用：全部显示 0，状态置灰 */
        if (s_env_status_dot)
            lv_obj_set_style_bg_color(s_env_status_dot, COLOR_GRAY_MID, 0);
        if (s_env_status_lbl)
            lv_label_set_text(s_env_status_lbl, "环境监测未启用（请在设置中开启）");

        lv_color_t gray_bg = COLOR_GRAY_LIGHTEST;
        if (s_card_temp)  lv_obj_set_style_bg_color(s_card_temp,  gray_bg, 0);
        if (s_card_humi)  lv_obj_set_style_bg_color(s_card_humi,  gray_bg, 0);
        if (s_card_press) lv_obj_set_style_bg_color(s_card_press, gray_bg, 0);
        if (s_card_lux)   lv_obj_set_style_bg_color(s_card_lux,   gray_bg, 0);
        if (s_card_alt)   lv_obj_set_style_bg_color(s_card_alt,   gray_bg, 0);

        if (s_val_temp)  lv_label_set_text(s_val_temp,  "0.0");
        if (s_val_humi)  lv_label_set_text(s_val_humi,  "0.0");
        if (s_val_press) lv_label_set_text(s_val_press, "0.0");
        if (s_val_lux)   lv_label_set_text(s_val_lux,   "0");
        if (s_val_alt)   lv_label_set_text(s_val_alt,   "0.0");
        return;
    }

    /* 已启用：读取传感器数据 */
    Gy39LuxData lux;
    Gy39EnvData env;
    bool has_data = (gy39_get_lux_data(&lux) == 0) && (gy39_get_env_data(&env) == 0);

    if (has_data && gy39_thread_is_running())
    {
        if (s_env_status_dot)
            lv_obj_set_style_bg_color(s_env_status_dot, COLOR_GREEN_NORMAL, 0);
        if (s_env_status_lbl)
            lv_label_set_text(s_env_status_lbl, "传感器已连接，数据实时更新中");

        lv_color_t active_bg = lv_color_hex(0xE8F4E8);
        if (s_card_temp)  lv_obj_set_style_bg_color(s_card_temp,  active_bg, 0);
        if (s_card_humi)  lv_obj_set_style_bg_color(s_card_humi,  active_bg, 0);
        if (s_card_press) lv_obj_set_style_bg_color(s_card_press, active_bg, 0);
        if (s_card_lux)   lv_obj_set_style_bg_color(s_card_lux,   active_bg, 0);
        if (s_card_alt)   lv_obj_set_style_bg_color(s_card_alt,   active_bg, 0);

        if (s_val_temp)  lv_label_set_text_fmt(s_val_temp,  "%.1f", env.temp);
        if (s_val_humi)  lv_label_set_text_fmt(s_val_humi,  "%.1f", env.hum);
        if (s_val_press) lv_label_set_text_fmt(s_val_press, "%.1f", env.press);
        if (s_val_lux)   lv_label_set_text_fmt(s_val_lux,   "%.1f", lux.lux);
        if (s_val_alt)   lv_label_set_text_fmt(s_val_alt,   "%.1f", env.alt);
    }
    else
    {
        if (s_env_status_dot)
            lv_obj_set_style_bg_color(s_env_status_dot, COLOR_ORANGE_NORMAL, 0);
        if (s_env_status_lbl)
            lv_label_set_text(s_env_status_lbl, "传感器未连接或无数据");

        lv_color_t warn_bg = lv_color_hex(0xFFF3E0);
        if (s_card_temp)  lv_obj_set_style_bg_color(s_card_temp,  warn_bg, 0);
        if (s_card_humi)  lv_obj_set_style_bg_color(s_card_humi,  warn_bg, 0);
        if (s_card_press) lv_obj_set_style_bg_color(s_card_press, warn_bg, 0);
        if (s_card_lux)   lv_obj_set_style_bg_color(s_card_lux,   warn_bg, 0);
        if (s_card_alt)   lv_obj_set_style_bg_color(s_card_alt,   warn_bg, 0);

        if (s_val_temp)  lv_label_set_text(s_val_temp,  "0.0");
        if (s_val_humi)  lv_label_set_text(s_val_humi,  "0.0");
        if (s_val_press) lv_label_set_text(s_val_press, "0.0");
        if (s_val_lux)   lv_label_set_text(s_val_lux,   "0");
        if (s_val_alt)   lv_label_set_text(s_val_alt,   "0.0");
    }
}

static void env_start_timer(void)
{
    if (s_timer_env)
        lv_timer_del(s_timer_env);
    s_timer_env = lv_timer_create(env_refresh_timer_cb, 500, NULL);
}

static void env_stop_timer(void)
{
    if (s_timer_env)
    {
        lv_timer_del(s_timer_env);
        s_timer_env = NULL;
    }
}

/* ======================================================================================================= */
/* ---------------------------------------------按钮回调--------------------------------------------------- */
/* ======================================================================================================= */

static void env_btn_back_cb(lv_event_t *e)
{
    (void)e;
    env_stop_timer();
    ui_home_scr_load();
}

/* ======================================================================================================= */
/* ---------------------------------------------ui设计---------------------------------------------------- */
/* ======================================================================================================= */

/**
 * @brief 创建单个参数卡片
 * @param parent 父容器
 * @param x X坐标
 * @param y Y坐标
 * @param w 宽度
 * @param h 高度
 * @param name 参数名称（如 "温度"）
 * @param unit 单位（如 "℃"）
 * @param out_card 输出卡片容器指针
 * @param out_val 输出数值标签指针
 * @return 卡片容器对象
 */
static lv_obj_t *env_create_param_card(lv_obj_t *parent,
                                        int32_t x, int32_t y, int32_t w, int32_t h,
                                        const char *name, const char *unit,
                                        lv_obj_t **out_card, lv_obj_t **out_val)
{
    lv_obj_t *card = ui_widget_create_box(
        parent, w, h,
        LV_ALIGN_TOP_LEFT, x, y,
        COLOR_GRAY_LIGHTEST, 12, false
    );

    /* 参数名称（顶部居中） */
    ui_widget_create_label_static(
        card, name,
        LV_ALIGN_TOP_MID, 0, 8,
        FONT_SUBTITLE, COLOR_TEXT_DARK
    );

    /* 数值（大号字体，居中） */
    lv_obj_t *val = ui_widget_create_label(
        card, "0",
        LV_ALIGN_CENTER, 0, -4,
        FONT_TITLE, COLOR_BLUE_DARK
    );

    /* 单位（底部居中） */
    ui_widget_create_label_static(
        card, unit,
        LV_ALIGN_BOTTOM_MID, 0, -8,
        FONT_BODY, COLOR_GRAY_DARK
    );

    if (out_card) *out_card = card;
    if (out_val)  *out_val  = val;
    return card;
}

lv_obj_t* ui_env_scr_create(void)
{
    if (env_scr != NULL)
        return env_scr;

    env_scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(env_scr, COLOR_GRAY_LIGHT, 0);
    lv_obj_set_style_bg_opa(env_scr, LV_OPA_COVER, 0);

    const int32_t SCR_W = 1024;
    const int32_t SCR_H = 600;
    const int32_t MARGIN = 16;

    /* ---------- 顶部标题栏 ---------- */
    {
        const int32_t BAR_H = 56;
        lv_obj_t *bar = ui_widget_create_box(
            env_scr,
            SCR_W - MARGIN * 2, BAR_H,
            LV_ALIGN_TOP_MID, 0, MARGIN,
            COLOR_BLUE_DARK, 14, false
        );

        /* 返回按钮 */
        ui_widget_create_btn(
            bar, "< 返回",
            80, 36,
            COLOR_BLUE_NORMAL, COLOR_BLUE_DARK,
            LV_ALIGN_LEFT_MID, 12, 0,
            0, env_btn_back_cb
        );

        /* 标题 */
        ui_widget_create_label_static(
            bar, "环境参数监测",
            LV_ALIGN_CENTER, 0, 0,
            FONT_TITLE, COLOR_TEXT_WHITE
        );

        /* 状态指示灯 + 文字 */
        s_env_status_dot = ui_widget_create_dot(
            bar, 12, COLOR_GRAY_MID,
            LV_ALIGN_RIGHT_MID, -16, 0
        );

        s_env_status_lbl = ui_widget_create_label(
            bar, "环境监测未启用",
            LV_ALIGN_RIGHT_MID, -16 - 12 - 8, 0,
            FONT_BODY, COLOR_TEXT_WHITE
        );
    }

    /* ---------- 参数卡片区 2x3 网格 ---------- */
    {
        const int32_t CARD_Y = MARGIN + 56 + MARGIN;
        const int32_t CARD_AREA_H = SCR_H - CARD_Y - MARGIN - 60; /* 留底部状态栏 */
        const int32_t NET_W = SCR_W - MARGIN * 2;
        const int32_t GAP = 14;
        const int32_t COLS = 3;
        const int32_t ROWS = 2;
        const int32_t CARD_W = (NET_W - GAP * (COLS - 1)) / COLS;
        const int32_t CARD_H = (CARD_AREA_H - GAP) / ROWS;
        const int32_t CARD_X0 = MARGIN;

        /* 第1行: 温度 / 湿度 / 气压 */
        env_create_param_card(
            env_scr,
            CARD_X0 + 0 * (CARD_W + GAP), CARD_Y + 0 * (CARD_H + GAP),
            CARD_W, CARD_H,
            "温度", "℃",
            &s_card_temp, &s_val_temp
        );

        env_create_param_card(
            env_scr,
            CARD_X0 + 1 * (CARD_W + GAP), CARD_Y + 0 * (CARD_H + GAP),
            CARD_W, CARD_H,
            "湿度", "%",
            &s_card_humi, &s_val_humi
        );

        env_create_param_card(
            env_scr,
            CARD_X0 + 2 * (CARD_W + GAP), CARD_Y + 0 * (CARD_H + GAP),
            CARD_W, CARD_H,
            "气压", "hPa",
            &s_card_press, &s_val_press
        );

        /* 第2行: 光照 / 海拔 */
        env_create_param_card(
            env_scr,
            CARD_X0 + 0 * (CARD_W + GAP), CARD_Y + 1 * (CARD_H + GAP),
            CARD_W, CARD_H,
            "光照强度", "lux",
            &s_card_lux, &s_val_lux
        );

        env_create_param_card(
            env_scr,
            CARD_X0 + 1 * (CARD_W + GAP), CARD_Y + 1 * (CARD_H + GAP),
            CARD_W, CARD_H,
            "海拔高度", "m",
            &s_card_alt, &s_val_alt
        );

        /* 第3列第2行: 留空或放提示 */
        lv_obj_t *tip_card = ui_widget_create_box(
            env_scr,
            CARD_W, CARD_H,
            LV_ALIGN_TOP_LEFT,
            CARD_X0 + 2 * (CARD_W + GAP), CARD_Y + 1 * (CARD_H + GAP),
            COLOR_GRAY_LIGHTEST, 12, false
        );
        ui_widget_create_label_static(
            tip_card, "更多传感器\n接入中...",
            LV_ALIGN_CENTER, 0, 0,
            FONT_BODY, COLOR_GRAY_DARK
        );
    }

    /* ---------- 立即刷新一次显示默认值 ---------- */
    env_refresh_timer_cb(NULL);

    return env_scr;
}

void ui_env_scr_load(void)
{
    if (env_scr)
    {
        lv_screen_load_anim(env_scr, LV_SCR_LOAD_ANIM_MOVE_LEFT, 300, 0, false);
        env_start_timer();
    }
}