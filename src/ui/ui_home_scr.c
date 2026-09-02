#include "ui.h"
#include "lvgl/lvgl.h"
#include "config.h"
#include "ui_widget.h"
#include "thread.h"
#include "my_font.h"
#include "v4l2_camera.h"
#include "yolov8_wrap.h"
#include <stdint.h>
#include <time.h>
#include <stdio.h>

static lv_obj_t *home_scr = NULL;

/* 定时器句柄，用于启动/停止 */
static lv_timer_t *s_timer_time = NULL;      /* 时间刷新: 1s */
static lv_timer_t *s_timer_status = NULL;    /* 状态卡片刷新: 2s */
static lv_timer_t *s_timer_bottom = NULL;    /* 底部栏刷新: 1s */

/* 顶部栏 */
static lv_obj_t *s_home_time_lbl = NULL;
static lv_obj_t *s_home_title_lbl = NULL;

/* 4 个状态卡片里的"状态点" (指示灯色点) */
static lv_obj_t *s_home_dot_cam = NULL;
static lv_obj_t *s_home_dot_ai  = NULL;
static lv_obj_t *s_home_dot_net = NULL;
static lv_obj_t *s_home_dot_sen = NULL;

/* 4 个状态卡片里的"数值"标签 */
static lv_obj_t *s_home_val_cam = NULL;
static lv_obj_t *s_home_val_ai  = NULL;
static lv_obj_t *s_home_val_net = NULL;
static lv_obj_t *s_home_val_sen = NULL;

/* 底部栏标签 */
static lv_obj_t *s_home_bottom_lbl = NULL;

/* ======================================================================================================= */
/* ---------------------------------------------计时器----------------------------------------------------- */
/* ======================================================================================================= */

// 定时器回调: 刷新时间 (1s)
static void home_timer_time_cb(lv_timer_t *timer)
{
    (void)timer;
    if (s_home_time_lbl == NULL) return;

    time_t now = time(NULL);
    struct tm *t = localtime(&now);
    lv_label_set_text_fmt(s_home_time_lbl,
                          "%04d-%02d-%02d %02d:%02d:%02d",
                          t->tm_year + 1900, t->tm_mon + 1, t->tm_mday,
                          t->tm_hour, t->tm_min, t->tm_sec);
}

// 定时器回调: 刷新状态卡片 (2s)
static void home_timer_status_cb(lv_timer_t *timer)
{
    (void)timer;

    /* ---- 摄像头状态 ---- */
    if (s_home_dot_cam && s_home_val_cam)
    {
        if (camera_is_device_exist())
        {
            lv_obj_set_style_bg_color(s_home_dot_cam, COLOR_GREEN_NORMAL, 0);
            AppConfig* cfg = config_get_global();
            int w, h;
            int fps = DEF_CAMERA_FPS;
            if (cfg)
            {
                config_get_camera_resolution(cfg->camera_scale, &w, &h);
                fps = cfg->camera_fps;
            }
            else
            {
                w = CAMERA_MAX_WIDTH;
                h = CAMERA_MAX_HEIGHT;
            }
            char cam_buf[64];
            snprintf(cam_buf, sizeof(cam_buf), "%dx%d / %dfps", w, h, fps);
            lv_label_set_text(s_home_val_cam, cam_buf);
        }
        else
        {
            lv_obj_set_style_bg_color(s_home_dot_cam, COLOR_GRAY_MID, 0);
            lv_label_set_text(s_home_val_cam, "未接入");
        }
    }

    /* ---- AI 状态 ---- */
    if (s_home_dot_ai && s_home_val_ai)
    {
        if (yolov8_file_all_exist())
        {
            lv_obj_set_style_bg_color(s_home_dot_ai, COLOR_GREEN_NORMAL, 0);
            lv_label_set_text(s_home_val_ai, "模型存在，可启动");
        }
        else
        {
            lv_obj_set_style_bg_color(s_home_dot_ai, COLOR_GRAY_MID, 0);
            lv_label_set_text(s_home_val_ai, "模型缺失");
        }
    }

    /* ---- 网络状态 (占位: TODO: 接入真正的网络检测) ---- */
    if (s_home_dot_net && s_home_val_net)
    {
        // 从配置获取初始显示IP和端口
        AppConfig* init_cfg = config_get_global();
        char init_port_buf[16];
        snprintf(init_port_buf, sizeof(init_port_buf), "%d", init_cfg ? init_cfg->server_port : 8888);
        const char* init_ip = init_cfg ? init_cfg->server_display_ip : "0.0.0.0";
        lv_obj_set_style_bg_color(s_home_dot_net, COLOR_GREEN_NORMAL, 0);
        lv_label_set_text_fmt(s_home_val_net, "19    %s:%s", init_ip, init_port_buf);
    }

    /* ---- 传感器 (GY39) 状态 ---- */
    if (s_home_dot_sen && s_home_val_sen)
    {
        AppConfig *cfg = config_get_global();
        bool enabled = (cfg && cfg->env_monitor_enabled);

        if (!enabled)
        {
            lv_obj_set_style_bg_color(s_home_dot_sen, COLOR_GRAY_MID, 0);
            lv_label_set_text(s_home_val_sen, "未启用");
        }
        else if (gy39_thread_is_running())
        {
            lv_obj_set_style_bg_color(s_home_dot_sen, COLOR_GREEN_NORMAL, 0);
            lv_label_set_text(s_home_val_sen, "已启动");
        }
        else
        {
            lv_obj_set_style_bg_color(s_home_dot_sen, COLOR_GRAY_MID, 0);
            lv_label_set_text(s_home_val_sen, "未连接");
        }
    }
}

// 定时器回调: 刷新底部栏 (1s)
static void home_timer_bottom_cb(lv_timer_t *timer)
{
    (void)timer;
    if (s_home_bottom_lbl == NULL) return;

    /* 计算运行时长 (以程序启动时间为基准) */
    static time_t s_start_time = 0;
    if (s_start_time == 0) s_start_time = time(NULL);

    time_t elapsed = time(NULL) - s_start_time;
    int h = elapsed / 3600;
    int m = (elapsed % 3600) / 60;
    int s = elapsed % 60;

    lv_label_set_text_fmt(s_home_bottom_lbl,
                          "固件 v1.0.0  |  运行 %02d:%02d:%02d",
                          h, m, s);
}


void ui_home_start_timers(void)
{
    if (s_timer_time)    
    { 
        lv_timer_del(s_timer_time);
    }
    if (s_timer_status) 
    { 
        lv_timer_del(s_timer_status);
    }
    if (s_timer_bottom) 
    { 
        lv_timer_del(s_timer_bottom);
    }

    s_timer_time   = lv_timer_create(home_timer_time_cb, 1000, NULL);
    s_timer_status = lv_timer_create(home_timer_status_cb, 2000, NULL);
    s_timer_bottom = lv_timer_create(home_timer_bottom_cb, 1000, NULL);
}

void ui_home_stop_timers(void)
{
    if (s_timer_time)    
    { 
        lv_timer_del(s_timer_time);   
        s_timer_time = NULL; 
    }
    if (s_timer_status) 
    { 
        lv_timer_del(s_timer_status);   
        s_timer_status = NULL; 
    }
    if (s_timer_bottom) 
    { 
        lv_timer_del(s_timer_bottom);   
        s_timer_bottom = NULL; 
    }
}

/* ======================================================================================================= */
/* ---------------------------------------------按钮回调--------------------------------------------------- */
/* ======================================================================================================= */

static void ui_home_btn_cb(lv_event_t *e)
{
    uintptr_t tag = (uintptr_t) lv_event_get_user_data(e);
    switch (tag)
    {
        case 1:
            ui_home_stop_timers();
            ui_camera_scr_load();
            break;
        case 2:
            ui_home_stop_timers();
            ui_ai_scr_load();
            break;
        case 3: /* 环境数据 */
            ui_home_stop_timers();
            ui_env_scr_load();
            break;
        case 4:
            ui_home_stop_timers();
            ui_server_scr_load();
            break;
        case 5: 
            ui_home_stop_timers();
            ui_device_scr_load();
            break;
        case 6:
            ui_home_stop_timers();
            ui_settings_scr_load();
            break;
        default: break;
    }
}

/* ======================================================================================================= */
/* ---------------------------------------------ui设计---------------------------------------------------- */
/* ======================================================================================================= */


/**
 * @brief 创建状态卡片 (圆点 + 标题 + 数值)
 * @param parent 父容器
 * @param x 坐标 (x轴)
 * @param y 坐标 (y轴)
 * @param w 宽度
 * @param h 高度
 * @param title 标题
 * @param init_value 初始数值
 * @param init_dot_color 初始圆点颜色
 * @param out_dot 输出圆点对象指针
 * @param out_value 输出数值对象指针
 * @return 状态卡片对象
 */
static lv_obj_t *home_create_status_card(lv_obj_t *parent,
                                          int32_t x, int32_t y, int32_t w, int32_t h,
                                          const char *title,
                                          const char *init_value,
                                          lv_color_t init_dot_color,
                                          lv_obj_t **out_dot,
                                          lv_obj_t **out_value)
{
    lv_obj_t *card = ui_widget_create_box(
        parent, 
        w, h,                      
        LV_ALIGN_TOP_LEFT, 
        x, y,                  
        COLOR_GRAY_LIGHTEST, 
        12,                          
        false
    );

    lv_obj_t *dot = ui_widget_create_dot(
        card, 
        12, 
        init_dot_color,               
        LV_ALIGN_TOP_LEFT, 
        4, 4
    );

    lv_obj_t *title_lbl = ui_widget_create_label_static(
        card, 
        title,                                              
        LV_ALIGN_TOP_MID, 
        0, 0,                           
        FONT_SUBTITLE, 
        COLOR_TEXT_DARK
    );

    lv_obj_t *value_lbl = ui_widget_create_label(
        card, 
        init_value,                                    
        LV_ALIGN_BOTTOM_MID, 0, 0,                       
        FONT_BODY, 
        COLOR_TEXT_DARK
    );

    if (out_dot)   *out_dot   = dot;
    if (out_value) *out_value = value_lbl;
    (void)title_lbl;
    return card;
}

/**
 * @brief 创建大图标按钮 (图片图标 + 文字标签)
 * @param parent 父容器
 * @param x 坐标 (x轴)
 * @param y 坐标 (y轴)
 * @param w 宽度
 * @param h 高度
 * @param icon_path 图标文件路径 (LVGL A:/xxx 格式, 如 "A:/img/monitor.png")
 *             传 NULL 时不创建图标 (仅保留文字)
 * @param label_text 文字标签
 * @param norm 正常状态颜色
 * @param press 按下状态颜色
 * @param tag 事件回调标签
 * @return 按钮对象
 */
static lv_obj_t *home_create_big_btn(lv_obj_t *parent,
                                     int32_t x, int32_t y, int32_t w, int32_t h,
                                     const char *icon_path,
                                     const char *label_text,
                                     lv_color_t norm, lv_color_t press,
                                     uintptr_t tag)
{
    lv_obj_t *btn = ui_widget_create_btn(
        parent, 
        NULL, 
        w, h, 
        norm, press,       
        LV_ALIGN_TOP_LEFT, 
        x, y,                      
        tag, 
        ui_home_btn_cb
    );

    /* 上半部: 图标图片 (48x48, 居中偏上) */
    if (icon_path != NULL && icon_path[0] != '\0')
    {
        lv_obj_t *icon = ui_widget_create_img_file(
            btn, 
            icon_path,                         
            48, 48,                                    
            LV_IMAGE_ALIGN_CENTER,                              
            LV_ALIGN_TOP_MID, 
            0, 
            14
        );
        (void)icon;
    }

    /* 下半部: 功能名 */
    lv_obj_t *txt = ui_widget_create_label_static(
        btn, 
        label_text,                           
        LV_ALIGN_BOTTOM_MID, 
        0, -14,         
        FONT_SUBTITLE, 
        COLOR_TEXT_WHITE
    );
    (void)txt;
    return btn;
}


lv_obj_t* ui_home_scr_create(void)
{
    if (home_scr != NULL)
        return home_scr;

    home_scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(home_scr, COLOR_GRAY_LIGHT, 0);
    lv_obj_set_style_bg_opa(home_scr, LV_OPA_COVER, 0);

    const int32_t SCR_W = 1024;
    const int32_t SCR_H = 600;
    const int32_t MARGIN = 16;

    /* ---------- 区块 1: 顶部标题栏 ---------- */
    {
        const int32_t BAR_H = 56;
        lv_obj_t *bar = ui_widget_create_box(
            home_scr,
            SCR_W - MARGIN * 2, BAR_H,
            LV_ALIGN_TOP_MID, 
            0, MARGIN,
            COLOR_BLUE_DARK, 14,
            false
        );

        ui_widget_create_dot(
            bar, 
            18, 
            COLOR_GREEN_NORMAL,
            LV_ALIGN_LEFT_MID, 
            16, 0
        );

        s_home_title_lbl = ui_widget_create_label_static(
            bar,
            "环境监测终端 RK3568",
            LV_ALIGN_LEFT_MID,
            18 + 18 + 10, 0,
            FONT_TITLE, 
            COLOR_TEXT_WHITE
        );

        s_home_time_lbl = ui_widget_create_label(
            bar,                               
            "----/--/-- --:--:--",                               
            LV_ALIGN_RIGHT_MID,                            
            -16, 0,                           
            FONT_SUBTITLE, 
            COLOR_TEXT_WHITE
        );
    }

    /* ---------- 区块 2: 状态卡片 4 联 ---------- */
    {
        const int32_t CARD_Y = MARGIN + 56 + MARGIN;
        const int32_t CARD_H = 110;
        const int32_t GAP   = 14;
        const int32_t NET_W = SCR_W - MARGIN * 2;
        const int32_t CARD_W = (NET_W - GAP * 3) / 4;
        const int32_t CARD_X0 = MARGIN;

        home_create_status_card(
            home_scr,                 
            CARD_X0 + 0 * (CARD_W + GAP), CARD_Y, 
            CARD_W, CARD_H,                
            "摄像头", "初始化中...",               
            COLOR_GRAY_MID,
            &s_home_dot_cam, 
            &s_home_val_cam
        );

        home_create_status_card(
            home_scr,                 
            CARD_X0 + 1 * (CARD_W + GAP), CARD_Y, 
            CARD_W, CARD_H,                
            "AI 识别", "未启动",
            COLOR_GRAY_MID,
            &s_home_dot_ai, 
            &s_home_val_ai
        );

        home_create_status_card(
            home_scr,                 
            CARD_X0 + 2 * (CARD_W + GAP), CARD_Y, 
            CARD_W, CARD_H,                
            "网络连接", "检测中...",
            COLOR_GRAY_MID,
            &s_home_dot_net, 
            &s_home_val_net
        );

        home_create_status_card(
            home_scr,                 
            CARD_X0 + 3 * (CARD_W + GAP), CARD_Y, 
            CARD_W, CARD_H,                
            "环境传感器", "未启动",
            COLOR_GRAY_MID,
            &s_home_dot_sen, 
            &s_home_val_sen
        );
    }

    /* ---------- 区块 3: 功能大按钮 3x2 ---------- */
    {
        const int32_t BTN_Y = MARGIN + 56 + MARGIN + 110 + MARGIN;
        const int32_t BTN_H = (SCR_H - BTN_Y - 40 - MARGIN * 2);
        const int32_t NET_W = SCR_W - MARGIN * 2;
        const int32_t GAP   = 16;
        const int32_t COLS  = 3;
        const int32_t ROWS  = 2;
        const int32_t BTN_W = (NET_W - GAP * (COLS - 1)) / COLS;
        const int32_t ROW_H = (BTN_H - GAP * (ROWS - 1)) / ROWS;
        const int32_t BTN_X0 = MARGIN;

        /* 行 0: 图标路径用 LVGL "A:" 盘符格式, 文件放在 bin/img/ 下 */
        home_create_big_btn(
            home_scr,          
            BTN_X0 + 0 * (BTN_W + GAP), BTN_Y + 0 * (ROW_H + GAP),
            BTN_W, ROW_H,
            NULL, 
            "实时画面",
            COLOR_BLUE_NORMAL, COLOR_BLUE_DARK, 
            1
        );

        home_create_big_btn(
            home_scr,          
            BTN_X0 + 1 * (BTN_W + GAP), BTN_Y + 0 * (ROW_H + GAP),
            BTN_W, ROW_H,
            NULL, 
            "AI 识别功能",
            COLOR_PURPLE_NORMAL, COLOR_PURPLE_DARK, 
            2
        );

        home_create_big_btn(
            home_scr,          
            BTN_X0 + 2 * (BTN_W + GAP), BTN_Y + 0 * (ROW_H + GAP),
            BTN_W, ROW_H,
            NULL, 
            "环境数据",
            COLOR_GREEN_NORMAL, COLOR_GREEN_DARK, 
            3
        );

        /* 行 1 */
        home_create_big_btn(
            home_scr,          
            BTN_X0 + 0 * (BTN_W + GAP), BTN_Y + 1 * (ROW_H + GAP),
            BTN_W, ROW_H,
            NULL, 
            "服务器管理",
            COLOR_ORANGE_NORMAL, COLOR_ORANGE_DARK, 
            4
        );

        home_create_big_btn(
            home_scr,          
            BTN_X0 + 1 * (BTN_W + GAP), BTN_Y + 1 * (ROW_H + GAP),
            BTN_W, ROW_H,
            NULL, 
            "硬件控制",
            COLOR_YELLOW_ORANGE, COLOR_ORANGE_DARK, 
            5
        );

        home_create_big_btn(
            home_scr,          
            BTN_X0 + 2 * (BTN_W + GAP), BTN_Y + 1 * (ROW_H + GAP),
            BTN_W, ROW_H,
            NULL, 
            "系统设置",
            COLOR_GRAY_DARK, COLOR_GRAY_BLACKISH, 
            6
        );
    }

    /* ---------- 区块 4: 底部信息栏 ---------- */
    {
        const int32_t BAR_H = 40;
        lv_obj_t *bar = ui_widget_create_box(
            home_scr,          
            SCR_W - MARGIN * 2, BAR_H,
            LV_ALIGN_BOTTOM_MID, 
            0, 0,
            COLOR_GRAY_MID, 10,
            false
        );

        s_home_bottom_lbl = ui_widget_create_label(
            bar,          
            "固件 v1.0.0  |  运行 00:00:00",
            LV_ALIGN_CENTER, 0, 0,
            FONT_BODY, COLOR_TEXT_DARK
        );
    }

    return home_scr;
}

void ui_home_scr_load(void)
{
    if (home_scr) {
        lv_screen_load_anim(
            home_scr, 
            LV_SCR_LOAD_ANIM_MOVE_RIGHT, 
            300, 
            0, 
            false
        );
        ui_home_start_timers();
    }
}
