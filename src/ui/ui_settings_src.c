#include "ui.h"
#include "ui_widget.h"
#include "config_manager.h"
#include "my_font.h"
#include "thread.h"
#include "tcp_server.h"
#include "camera_lvgl.h"
#include "v4l2_camera.h"
#include "yolov8_wrap.h"
#include "beep.h"
#include "led.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>

// 屏幕
static lv_obj_t *settings_scr = NULL;

// 配置输入控件
static ui_slider_ctx_t *s_slider_fps       = NULL;
static ui_slider_ctx_t *s_slider_scale     = NULL;
static ui_slider_ctx_t *s_slider_conf      = NULL;
static lv_obj_t *s_textarea_port           = NULL;
static lv_obj_t *s_textarea_ip             = NULL;
static lv_obj_t *s_textarea_upload_dir     = NULL;
static lv_obj_t *s_textarea_password       = NULL;
static lv_obj_t *s_textarea_photo          = NULL;
static lv_obj_t *s_btn_env_monitor         = NULL;   
static lv_obj_t *s_lbl_resolution          = NULL;
static lv_obj_t *s_lbl_error               = NULL;  // 错误提示标签

// 工作副本（编辑期间暂存，保存时才写回全局）
static AppConfig s_working_cfg;

// 外部传入的当前配置指针
static AppConfig *s_current_cfg = NULL;

// 软键盘直接复用 ui_main.c 创建的全局 kb (在 lv_layer_sys() 上, 永远在最上层)
// extern lv_obj_t *kb;  -- 已在 ui.h 中声明

// 前一次缩放值（用于检测变化时更新分辨率标签）
static int s_last_scale = 0;

static void ui_settings_refresh_resolution_label(void);
static void ui_settings_load_to_form(void);
static void ui_settings_read_from_form(void);
static void ui_settings_btn_cb(lv_event_t *e);
static bool ui_settings_validate(char *err_buf, size_t err_buf_size);
static bool is_valid_ip(const char *ip);
static bool is_dir_writable(const char *path);

/**
 * @brief 更新分辨率显示标签
 */
static void ui_settings_refresh_resolution_label(void)
{
    if (!s_lbl_resolution) return;
    int w, h;
    config_get_camera_resolution(s_working_cfg.camera_scale, &w, &h);
    char buf[64];
    snprintf(buf, sizeof(buf), "%d%%  (%d x %d)", s_working_cfg.camera_scale, w, h);
    lv_label_set_text(s_lbl_resolution, buf);
}

/**
 * @brief 将当前配置加载到表单控件
 */
static void ui_settings_load_to_form(void)
{
    if (!s_current_cfg) return;

    // 拷贝到工作副本
    memcpy(&s_working_cfg, s_current_cfg, sizeof(AppConfig));

    // 端口文本框
    if (s_textarea_port)
    {
        char port_buf[16];
        snprintf(port_buf, sizeof(port_buf), "%d", s_working_cfg.server_port);
        lv_textarea_set_text(s_textarea_port, port_buf);
    }

    // IP 文本框
    if (s_textarea_ip)
        lv_textarea_set_text(s_textarea_ip, s_working_cfg.server_display_ip);

    // 服务端上传目录文本框
    if (s_textarea_upload_dir)
        lv_textarea_set_text(s_textarea_upload_dir, s_working_cfg.server_upload_dir);

    // 登录密码文本框
    if (s_textarea_password)
        lv_textarea_set_text(s_textarea_password, s_working_cfg.login_password);

    // 照片保存路径文本框
    if (s_textarea_photo)
        lv_textarea_set_text(s_textarea_photo, s_working_cfg.photo_save_path);

    // 环境监测开关按钮
    if (s_btn_env_monitor)
    {
        if (s_working_cfg.env_monitor_enabled)
        {
            lv_obj_set_style_bg_color(s_btn_env_monitor, COLOR_GREEN_NORMAL, 0);
            lv_obj_t *label = lv_obj_get_child(s_btn_env_monitor, 0);
            if (label) lv_label_set_text(label, "已开启");
        }
        else
        {
            lv_obj_set_style_bg_color(s_btn_env_monitor, COLOR_GRAY_MID, 0);
            lv_obj_t *label = lv_obj_get_child(s_btn_env_monitor, 0);
            if (label) lv_label_set_text(label, "已关闭");
        }
    }

    // 帧率滑块
    if (s_slider_fps)
        ui_widget_slider_set_value(s_slider_fps, s_working_cfg.camera_fps);

    // 分辨率滑块
    if (s_slider_scale)
        ui_widget_slider_set_value(s_slider_scale, s_working_cfg.camera_scale);

    // 置信度滑块
    if (s_slider_conf)
        ui_widget_slider_set_value(s_slider_conf, s_working_cfg.ai_confidence);

    // 更新分辨率标签
    ui_settings_refresh_resolution_label();
}

/**
 * @brief 从表单控件读取值到工作副本
 */
static void ui_settings_read_from_form(void)
{
    // 端口
    if (s_textarea_port)
    {
        const char *txt = lv_textarea_get_text(s_textarea_port);
        int port = atoi(txt);
        if (port >= SERVER_PORT_MIN && port <= SERVER_PORT_MAX)
            s_working_cfg.server_port = port;
    }

    // IP
    if (s_textarea_ip)
    {
        const char *txt = lv_textarea_get_text(s_textarea_ip);
        strncpy(s_working_cfg.server_display_ip, txt, sizeof(s_working_cfg.server_display_ip) - 1);
        s_working_cfg.server_display_ip[sizeof(s_working_cfg.server_display_ip) - 1] = '\0';
    }

    // 服务端上传目录
    if (s_textarea_upload_dir)
    {
        const char *txt = lv_textarea_get_text(s_textarea_upload_dir);
        if (txt == NULL) txt = "";
        if (strlen(txt) > 0)
        {
            strncpy(s_working_cfg.server_upload_dir, txt, sizeof(s_working_cfg.server_upload_dir) - 1);
            s_working_cfg.server_upload_dir[sizeof(s_working_cfg.server_upload_dir) - 1] = '\0';
        }
    }

    // 登录密码 (长度限制 LOGIN_PWD_MIN_LEN ~ LOGIN_PWD_MAX_LEN, 无效则保留旧值)
    if (s_textarea_password)
    {
        const char *txt = lv_textarea_get_text(s_textarea_password);
        if (txt == NULL) txt = "";
        size_t len = strlen(txt);
        if (len >= LOGIN_PWD_MIN_LEN && len <= LOGIN_PWD_MAX_LEN)
        {
            strncpy(s_working_cfg.login_password, txt, sizeof(s_working_cfg.login_password) - 1);
            s_working_cfg.login_password[sizeof(s_working_cfg.login_password) - 1] = '\0';
        }
    }

    // 照片保存路径
    if (s_textarea_photo)
    {
        const char *txt = lv_textarea_get_text(s_textarea_photo);
        if (txt == NULL) txt = "";
        if (strlen(txt) > 0)
        {
            strncpy(s_working_cfg.photo_save_path, txt, sizeof(s_working_cfg.photo_save_path) - 1);
            s_working_cfg.photo_save_path[sizeof(s_working_cfg.photo_save_path) - 1] = '\0';
        }
    }

    // 滑块
    if (s_slider_fps)
        s_working_cfg.camera_fps = ui_widget_slider_get_value(s_slider_fps);
    if (s_slider_scale)
        s_working_cfg.camera_scale = ui_widget_slider_get_value(s_slider_scale);
    if (s_slider_conf)
        s_working_cfg.ai_confidence = ui_widget_slider_get_value(s_slider_conf);
}

/**
 * @brief 校验IP地址格式 (IPv4)
 * @param ip IP字符串
 * @return true 合法, false 非法
 */
static bool is_valid_ip(const char *ip)
{
    if (!ip || strlen(ip) == 0)
        return false;

    // 允许 0.0.0.0 和 hostname 格式
    if (strcmp(ip, "0.0.0.0") == 0)
        return true;

    // 检查是否为合法的 IPv4 格式
    int parts = 0;
    int num = 0;
    bool has_dot = false;
    const char *p = ip;

    while (*p)
    {
        if (*p == '.')
        {
            if (num < 0 || num > 255) return false;
            parts++;
            has_dot = true;
            num = 0;
        }
        else if (*p >= '0' && *p <= '9')
        {
            num = num * 10 + (*p - '0');
            if (num > 255) return false;
        }
        else
        {
            return false;
        }
        p++;
    }
    if (num < 0 || num > 255) return false;
    parts++;

    // 必须有3个点，4个部分
    return (has_dot && parts == 4);
}

/**
 * @brief 检查目录是否存在且可写（如果不存在则尝试创建）
 * @param path 目录路径
 * @return true 可用, false 不可用
 */
static bool is_dir_writable(const char *path)
{
    if (!path || strlen(path) == 0)
        return false;

    // 尝试创建目录（如果不存在）
    if (mkdir(path, 0755) != 0)
    {
        // 目录已存在或创建失败
        // 检查是否存在
        struct stat st;
        if (stat(path, &st) != 0)
            return false;  // 不存在且无法创建
        if (!S_ISDIR(st.st_mode))
            return false;  // 不是目录
    }

    // 检查写权限
    return (access(path, W_OK) == 0);
}

/**
 * @brief 校验所有设置值的正确性
 * @param err_buf 错误信息输出缓冲区
 * @param err_buf_size 缓冲区大小
 * @return true 全部正确, false 存在错误
 */
static bool ui_settings_validate(char *err_buf, size_t err_buf_size)
{
    if (!err_buf || err_buf_size == 0)
        return false;

    err_buf[0] = '\0';
    bool ok = true;

    // 1. 校验端口
    if (s_textarea_port)
    {
        const char *txt = lv_textarea_get_text(s_textarea_port);
        int port = atoi(txt);
        if (port < SERVER_PORT_MIN || port > SERVER_PORT_MAX)
        {
            snprintf(err_buf, err_buf_size, "端口必须在 %d-%d 之间", SERVER_PORT_MIN, SERVER_PORT_MAX);
            return false;
        }
        // 额外检查: atoi 返回0表示非法输入
        if (port == 0 && strcmp(txt, "0") != 0)
        {
            snprintf(err_buf, err_buf_size, "端口必须是有效的数字");
            return false;
        }
    }

    // 2. 校验IP地址
    if (s_textarea_ip)
    {
        const char *txt = lv_textarea_get_text(s_textarea_ip);
        if (!is_valid_ip(txt))
        {
            snprintf(err_buf, err_buf_size, "IP地址格式错误 (如 192.168.1.100 或 0.0.0.0)");
            return false;
        }
    }

    // 3. 校验服务端上传目录
    if (s_textarea_upload_dir)
    {
        const char *txt = lv_textarea_get_text(s_textarea_upload_dir);
        if (strlen(txt) == 0)
        {
            snprintf(err_buf, err_buf_size, "上传目录不能为空");
            return false;
        }
        // 检查路径是否以 / 结尾 (因为代码用 "%s%s" 拼接文件名)
        size_t len = strlen(txt);
        if (txt[len - 1] != '/')
        {
            snprintf(err_buf, err_buf_size, "上传目录必须以 '/' 结尾 (如 ./uploads/)");
            return false;
        }
        if (!is_dir_writable(txt))
        {
            snprintf(err_buf, err_buf_size, "上传目录无法创建或不可写: %s", txt);
            return false;
        }
    }

    // 4. 校验照片保存路径
    if (s_textarea_photo)
    {
        const char *txt = lv_textarea_get_text(s_textarea_photo);
        if (strlen(txt) == 0)
        {
            snprintf(err_buf, err_buf_size, "拍照路径不能为空");
            return false;
        }
        if (!is_dir_writable(txt))
        {
            snprintf(err_buf, err_buf_size, "拍照路径无法创建或不可写: %s", txt);
            return false;
        }
    }

    // 5. 校验密码强度 (至少包含字母或数字)
    if (s_textarea_password)
    {
        const char *txt = lv_textarea_get_text(s_textarea_password);
        if (txt == NULL) txt = "";
        size_t len = strlen(txt);
        if (len > 0 && len < LOGIN_PWD_MIN_LEN)
        {
            snprintf(err_buf, err_buf_size, "密码至少 %d 个字符", LOGIN_PWD_MIN_LEN);
            return false;
        }
        if (len > LOGIN_PWD_MAX_LEN)
        {
            snprintf(err_buf, err_buf_size, "密码最多 %d 个字符", LOGIN_PWD_MAX_LEN);
            return false;
        }
        // 检查是否只包含空格
        if (len > 0)
        {
            bool only_spaces = true;
            for (size_t i = 0; i < len; i++)
            {
                if (txt[i] != ' ' && txt[i] != '\t')
                {
                    only_spaces = false;
                    break;
                }
            }
            if (only_spaces)
            {
                snprintf(err_buf, err_buf_size, "密码不能全为空格");
                return false;
            }
        }
    }

    return ok;
}

/**
 * @brief 显示错误提示
 * @param msg 错误信息
 */
static void show_error_label(const char *msg)
{
    if (s_lbl_error)
    {
        if (msg && strlen(msg) > 0)
        {
            lv_label_set_text(s_lbl_error, msg);
            lv_obj_set_style_text_color(s_lbl_error, COLOR_RED_DARK, 0);
            lv_obj_clear_flag(s_lbl_error, LV_OBJ_FLAG_HIDDEN);
        }
        else
        {
            lv_obj_add_flag(s_lbl_error, LV_OBJ_FLAG_HIDDEN);
        }
    }
}

/**
 * @brief 保存按钮回调
 */
static void ui_settings_save(void)
{
    if (!s_current_cfg) return;

    // 先读取表单值
    ui_settings_read_from_form();

    // 校验输入值
    char err_buf[256];
    if (!ui_settings_validate(err_buf, sizeof(err_buf)))
    {
        // 校验失败，显示错误
        show_error_label(err_buf);
        printf("[Settings] Validation error: %s\n", err_buf);
        return;
    }

    // 校验通过，隐藏错误提示
    show_error_label("");

    // 写回全局配置
    memcpy(s_current_cfg, &s_working_cfg, sizeof(AppConfig));

    // 保存到文件
    if (config_save(NULL, s_current_cfg) == 0)
    {
        printf("[Settings] config saved: port=%d upload_dir=%s fps=%d scale=%d photo=%s conf=%d pwd=%s\n",
               s_current_cfg->server_port, s_current_cfg->server_upload_dir,
               s_current_cfg->camera_fps, s_current_cfg->camera_scale,
               s_current_cfg->photo_save_path, s_current_cfg->ai_confidence,
               s_current_cfg->login_password);
        show_error_label("配置已保存!");
        lv_obj_set_style_text_color(s_lbl_error, COLOR_GREEN_DARK, 0);
    }
    else
    {
        printf("[Settings] config save failed!\n");
        show_error_label("保存失败，请检查文件权限");
    }
}

/**
 * @brief 重置为默认值
 */
static void ui_settings_reset_defaults(void)
{
    config_set_defaults(&s_working_cfg);
    ui_settings_load_to_form();
    show_error_label("");  // 清除错误提示
    printf("[Settings] reset to defaults\n");
}

/**
 * @brief 系统清理：停止所有线程，释放全部资源（fd/内存/设备句柄）
 * @note  公共接口，可供 ui_login_src.c 等其他模块调用
 */
void system_cleanup(void)
{
    /* 1. 停止 AI 线程（内部调用 yolov8_deinit → rknn_destroy 释放NPU模型句柄） */
    if (ai_thread_is_running())
        ai_thread_stop();
    else if (yolov8_is_initialized())
        yolov8_deinit();

    /* 2. 停止摄像头线程（内部调用 camera_deinit → STREAMOFF + munmap + close(fd)） */
    if (cam_thread_is_running())
        cam_thread_stop();
    else if (camera_is_run())
        camera_deinit();

    /* 3. 释放 LVGL 显示适配层缓冲 */
    camera_lvgl_deinit();

    /* 4. 停止 GY39 线程（内部 serial_close 关闭串口 fd） */
    if (gy39_thread_is_running())
        gy39_thread_stop();

    /* 5. 关闭 TCP 服务器（关闭 listen fd + 所有客户端 fd） */
    if (tcp_server_is_running())
        tcp_server_parse_ui_cmd(TCP_UI_CMD_CLOSE);

    /* 6. 停止所有 UI 定时器 */
    ui_home_stop_timers();
    ui_server_stop_timers();

    /* 7. 释放硬件设备资源（LED GPIO fd + 蜂鸣器 PWM fd） */
    device_led_deinit_all();
    device_beep_deinit();

    /* 8. 释放 LVGL 核心（关闭 fb0 显示驱动 + evdev 输入设备） */
    lv_deinit();

    printf("[Settings] system cleanup done\n");
}

/* ========== 关机/重启动画 ========== */

/* 关机动画持续时间 (ms), 期间显示遮罩 + spinner */
#define SHUTDOWN_ANIM_MS     1500

/* 标记本次关机动作: 0=退出, 1=重启 */
static int s_shutdown_is_restart = 0;

/**
 * @brief 关机动画定时器回调: 真正执行清理 + 退出/重启
 * @note  延迟 SHUTDOWN_ANIM_MS 后调用, 让用户看到动画
 */
static void shutdown_anim_cb(lv_timer_t *t)
{
    (void)t;

    system_cleanup();

    if (s_shutdown_is_restart)
    {
        printf("[Settings] restarting application...\n");
        execv("/proc/self/exe", (char*[]){ "/proc/self/exe", NULL });
        /* execv 失败才到这里 */
        perror("execv failed");
        exit(1);
    }
    else
    {
        printf("[Settings] exiting application...\n");
        exit(0);
    }
}

/**
 * @brief 显示关机动画遮罩 (全屏黑底 + spinner + 提示文字)
 * @param msg 显示的提示文字, 如 "正在关机..." / "正在重启..."
 */
static void show_shutdown_overlay(const char *msg)
{
    /* 在 lv_layer_top() 上创建全屏黑色遮罩, 拦截所有点击 */
    lv_obj_t *mask = lv_obj_create(lv_layer_top());
    lv_obj_set_size(mask, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(mask, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(mask, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_clear_flag(mask, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(mask, LV_OBJ_FLAG_CLICKABLE);

    /* 中央 spinner */
    ui_widget_create_spinner(
        mask,
        80, 1200, 270,
        LV_ALIGN_CENTER, 0, -30
    );

    /* 提示文字 */
    ui_widget_create_label_static(
        mask,
        msg,
        LV_ALIGN_BOTTOM_MID, 0, -80,
        FONT_SUBTITLE,
        COLOR_TEXT_WHITE
    );

    /* 启动一次性定时器, 延迟执行真正的清理 */
    lv_timer_t *tm = lv_timer_create(shutdown_anim_cb, SHUTDOWN_ANIM_MS, NULL);
    lv_timer_set_repeat_count(tm, 1);
}

/**
 * @brief 退出程序: 显示关机动画 -> 清理资源 -> exit
 */
static void ui_settings_exit_app(void)
{
    s_shutdown_is_restart = 0;
    show_shutdown_overlay("正在关机...");
}

/**
 * @brief 重启程序: 显示关机动画 -> 清理资源 -> execv 重新启动
 */
static void ui_settings_restart_app(void)
{
    s_shutdown_is_restart = 1;
    show_shutdown_overlay("正在重启...");
}

/**
 * @brief 滑块事件回调：分辨率滑块变化时更新标签
 */
static void ui_settings_scale_slider_cb(lv_event_t *e)
{
    ui_slider_ctx_t *ctx = (ui_slider_ctx_t *)lv_event_get_user_data(e);
    if (!ctx) return;
    s_working_cfg.camera_scale = ui_widget_slider_get_value(ctx);
    ui_settings_refresh_resolution_label();
}

/**
 * @brief 环境监测开关按钮回调：切换开启/关闭状态
 */
static void ui_settings_env_monitor_btn_cb(lv_event_t *e)
{
    (void)e;
    s_working_cfg.env_monitor_enabled = !s_working_cfg.env_monitor_enabled;

    if (s_working_cfg.env_monitor_enabled)
    {
        lv_obj_set_style_bg_color(s_btn_env_monitor, COLOR_GREEN_NORMAL, 0);
        lv_obj_t *label = lv_obj_get_child(s_btn_env_monitor, 0);
        if (label) lv_label_set_text(label, "已开启");
    }
    else
    {
        lv_obj_set_style_bg_color(s_btn_env_monitor, COLOR_GRAY_MID, 0);
        lv_obj_t *label = lv_obj_get_child(s_btn_env_monitor, 0);
        if (label) lv_label_set_text(label, "已关闭");
    }
}

/**
 * @brief 设置页面按钮回调
 *        tag=0: 返回  tag=1: 保存  tag=2: 重置默认  tag=3: 重启  tag=4: 退出
 */
static void ui_settings_btn_cb(lv_event_t *e)
{
    uintptr_t tag = (uintptr_t)lv_event_get_user_data(e);
    if (tag == 0)
    {
        // 返回首页
        ui_home_scr_load();
    }
    else if (tag == 1)
    {
        // 保存配置
        ui_settings_save();
    }
    else if (tag == 2)
    {
        // 重置默认值
        ui_settings_reset_defaults();
    }
    else if (tag == 3)
    {
        // 重启程序
        ui_settings_restart_app();
    }
    else if (tag == 4)
    {
        // 退出程序
        ui_settings_exit_app();
    }
}

lv_obj_t* ui_settings_scr_create(void)
{
    if (settings_scr != NULL)
        return settings_scr;

    settings_scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(settings_scr, COLOR_GRAY_LIGHT, 0);
    lv_obj_set_style_bg_opa(settings_scr, LV_OPA_COVER, 0);

    // 左侧：设置表单区（可滚动）
    lv_obj_t *form_box = ui_widget_create_box(
        settings_scr,
        772, 560,
        LV_ALIGN_TOP_LEFT,
        10, 10,
        COLOR_GRAY_LIGHTEST,
        10, true
    );

    // 标题
    ui_widget_create_label(
        form_box, "系统设置",
        LV_ALIGN_TOP_LEFT, 0, 0,
        FONT_TITLE, COLOR_TEXT_DARK
    );

    // ========== TCP 服务器分组 ==========
    ui_widget_create_label(
        form_box, "-- TCP 服务器 --",
        LV_ALIGN_TOP_LEFT, 0, 45,
        FONT_SUBTITLE, COLOR_BLUE_DARK
    );

    // 端口
    ui_widget_create_label(
        form_box, "端口:",
        LV_ALIGN_TOP_LEFT, 0, 75,
        FONT_BODY, COLOR_TEXT_DARK
    );
    s_textarea_port = ui_widget_create_textarea(
        form_box, 120, 36,
        LV_ALIGN_TOP_LEFT, 60, 68,
        kb
    );
    lv_textarea_set_placeholder_text(s_textarea_port, "1-65535");

    // 显示IP
    ui_widget_create_label(
        form_box, "显示IP:",
        LV_ALIGN_TOP_LEFT, 200, 75,
        FONT_BODY, COLOR_TEXT_DARK
    );
    s_textarea_ip = ui_widget_create_textarea(
        form_box, 200, 36,
        LV_ALIGN_TOP_LEFT, 270, 68,
        kb
    );
    lv_textarea_set_placeholder_text(s_textarea_ip, "0.0.0.0");

    // 上传目录
    ui_widget_create_label(
        form_box, "上传目录:",
        LV_ALIGN_TOP_LEFT, 0, 120,
        FONT_BODY, COLOR_TEXT_DARK
    );
    s_textarea_upload_dir = ui_widget_create_textarea(
        form_box, 450, 36,
        LV_ALIGN_TOP_LEFT, 80, 113,
        kb
    );
    lv_textarea_set_placeholder_text(s_textarea_upload_dir, "./uploads/");

    // ========== 摄像头分组 ==========
    ui_widget_create_label(
        form_box, "-- 摄像头 --",
        LV_ALIGN_TOP_LEFT, 0, 150,
        FONT_SUBTITLE, COLOR_BLUE_DARK
    );

    // 帧率滑块
    ui_widget_create_label(
        form_box, "FPS:",
        LV_ALIGN_TOP_LEFT, 0, 180,
        FONT_BODY, COLOR_TEXT_DARK
    );
    s_slider_fps = ui_widget_create_slider(
        form_box,
        CAMERA_FPS_MIN, CAMERA_FPS_MAX, DEF_CAMERA_FPS,
        "fps",
        LV_ALIGN_TOP_LEFT, 60, 180,
        NULL
    );

    // 分辨率滑块
    ui_widget_create_label(
        form_box, "分辨率:",
        LV_ALIGN_TOP_LEFT, 0, 230,
        FONT_BODY, COLOR_TEXT_DARK
    );
    s_slider_scale = ui_widget_create_slider(
        form_box,
        CAMERA_SCALE_MIN, CAMERA_SCALE_MAX, DEF_CAMERA_SCALE,
        "%",
        LV_ALIGN_TOP_LEFT, 60, 230,
        ui_settings_scale_slider_cb
    );

    // 分辨率详情标签
    s_lbl_resolution = ui_widget_create_label(
        form_box, "100%  (772 x 579)",
        LV_ALIGN_TOP_LEFT, 280, 230,
        FONT_BODY, COLOR_TEXT_DARK
    );

    // 照片保存路径
    ui_widget_create_label(
        form_box, "拍照路径:",
        LV_ALIGN_TOP_LEFT, 0, 290,
        FONT_BODY, COLOR_TEXT_DARK
    );
    s_textarea_photo = ui_widget_create_textarea(
        form_box, 450, 36,
        LV_ALIGN_TOP_LEFT, 80, 283,
        kb
    );
    lv_textarea_set_placeholder_text(s_textarea_photo, "./photo");

    // ========== AI 推理分组 ==========
    ui_widget_create_label(
        form_box, "-- AI 推理 --",
        LV_ALIGN_TOP_LEFT, 0, 330,
        FONT_SUBTITLE, COLOR_BLUE_DARK
    );

    // 置信度滑块
    ui_widget_create_label(
        form_box, "置信度:",
        LV_ALIGN_TOP_LEFT, 0, 360,
        FONT_BODY, COLOR_TEXT_DARK
    );
    s_slider_conf = ui_widget_create_slider(
        form_box,
        AI_CONF_MIN, AI_CONF_MAX, DEF_AI_CONFIDENCE,
        "%",
        LV_ALIGN_TOP_LEFT, 80, 360,
        NULL
    );

    // ========== 系统安全分组 ==========
    ui_widget_create_label(
        form_box, "-- 系统安全 --",
        LV_ALIGN_TOP_LEFT, 0, 395,
        FONT_SUBTITLE, COLOR_BLUE_DARK
    );

    // 登录密码
    ui_widget_create_label(
        form_box, "登录密码:",
        LV_ALIGN_TOP_LEFT, 0, 430,
        FONT_BODY, COLOR_TEXT_DARK
    );
    s_textarea_password = ui_widget_create_textarea(
        form_box, 300, 36,
        LV_ALIGN_TOP_LEFT, 90, 423,
        kb
    );
    /* 开启密码模式 (输入显示 *) */
    lv_textarea_set_password_mode(s_textarea_password, true);
    /* 限制最大长度 */
    lv_textarea_set_max_length(s_textarea_password, LOGIN_PWD_MAX_LEN);
    lv_textarea_set_placeholder_text(s_textarea_password, "1-32 字符");

    // ========== 环境监测分组 ==========
    ui_widget_create_label(
        form_box, "-- 环境监测 --",
        LV_ALIGN_TOP_LEFT, 0, 475,
        FONT_SUBTITLE, COLOR_BLUE_DARK
    );

    ui_widget_create_label(
        form_box, "启用监测:",
        LV_ALIGN_TOP_LEFT, 0, 505,
        FONT_BODY, COLOR_TEXT_DARK
    );
    s_btn_env_monitor = ui_widget_create_btn(
        form_box, "已关闭",
        120, 36,
        COLOR_GRAY_MID, COLOR_GRAY_DARK,
        LV_ALIGN_TOP_LEFT, 90, 498,
        0, ui_settings_env_monitor_btn_cb
    );

    // 右侧：操作按钮区
    ui_widget_create_btn(
        settings_scr, "返回",
        200, 50,
        COLOR_BLUE_LIGHT, COLOR_BLUE_DARK,
        LV_ALIGN_TOP_LEFT, 800, 10,
        0, ui_settings_btn_cb
    );

    ui_widget_create_btn(
        settings_scr, "保存配置",
        200, 50,
        COLOR_GREEN_LIGHT, COLOR_GREEN_DARK,
        LV_ALIGN_TOP_LEFT, 800, 80,
        1, ui_settings_btn_cb
    );

    // 错误/成功提示标签 (保存按钮下方, 默认隐藏)
    s_lbl_error = ui_widget_create_label(
        settings_scr, "",
        LV_ALIGN_TOP_LEFT, 800, 135,
        FONT_BODY, COLOR_RED_DARK
    );
    lv_obj_add_flag(s_lbl_error, LV_OBJ_FLAG_HIDDEN);

    ui_widget_create_btn(
        settings_scr, "重置默认",
        200, 50,
        COLOR_YELLOW_LIGHT, COLOR_ORANGE_DARK,
        LV_ALIGN_TOP_LEFT, 800, 170,
        2, ui_settings_btn_cb
    );

    ui_widget_create_btn(
        settings_scr, "重启程序",
        200, 50,
        COLOR_PURPLE_LIGHT, COLOR_PURPLE_DARK,
        LV_ALIGN_TOP_LEFT, 800, 240,
        3, ui_settings_btn_cb
    );

    ui_widget_create_btn(
        settings_scr, "退出程序",
        200, 50,
        COLOR_RED_LIGHT, COLOR_RED_DARK,
        LV_ALIGN_TOP_LEFT, 800, 310,
        4, ui_settings_btn_cb
    );

    return settings_scr;
}

void ui_settings_scr_load(void)
{
    if (settings_scr)
    {
        lv_screen_load_anim(
            settings_scr,
            LV_SCR_LOAD_ANIM_MOVE_LEFT,
            300, 0, false
        );
        // 确保有配置指针
        if (!s_current_cfg)
            s_current_cfg = config_get_global();
        // 从全局配置加载到表单
        if (s_current_cfg)
            ui_settings_load_to_form();
    }
}

void ui_settings_set_config_ptr(AppConfig *cfg)
{
    s_current_cfg = cfg;
}
