#include "ui.h"
#include "ui_widget.h"
#include "lvgl/lvgl.h"
#include "config.h"
#include "config_manager.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static lv_obj_t *login_scr = NULL;
static lv_obj_t *pwd_ta    = NULL;     /* 密码输入框 */
static lv_obj_t *err_lbl   = NULL;     /* 错误提示标签 */

/**
 * @brief 确认按钮回调：校验密码
 *        正确 -> 进入首页
 *        错误 -> 清空输入框并显示提示
 */
static void login_confirm_cb(lv_event_t *e)
{
    (void)e;

    const char *pwd = lv_textarea_get_text(pwd_ta);
    if (pwd == NULL)
        pwd = "";

    /* 从全局配置读取密码 */
    AppConfig *cfg = config_get_global();
    const char *expected = cfg ? cfg->login_password : DEF_LOGIN_PASSWORD;

    if (strcmp(pwd, expected) == 0)
    {
        /* 密码正确: 隐藏软键盘, 进入首页 */
        if (kb != NULL)
        {
            lv_obj_add_flag(kb, LV_OBJ_FLAG_HIDDEN);
            lv_keyboard_set_textarea(kb, NULL);
        }

        ui_home_scr_load();
        ui_home_start_timers();
    }
    else
    {
        /* 密码错误: 清空输入框, 显示红色提示 */
        lv_textarea_set_text(pwd_ta, "");
        if (err_lbl != NULL)
        {
            lv_label_set_text(err_lbl, "密码错误, 请重新输入");
        }
    }
}

/**
 * @brief 退出按钮回调: 清理资源后退出程序
 */
static void login_exit_cb(lv_event_t *e)
{
    (void)e;
    printf("[Login] exit by user, cleaning up resources...\n");

    /* 清理系统资源 (停止所有线程、释放fd/设备句柄) */
    system_cleanup();

    printf("[Login] exit\n");
    exit(0);
}

lv_obj_t* ui_login_scr_create(void)
{
    if (login_scr != NULL)
        return login_scr;

    login_scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(login_scr, COLOR_GRAY_LIGHTEST, 0);
    lv_obj_set_style_bg_opa(login_scr, LV_OPA_COVER, 0);
    lv_obj_clear_flag(login_scr, LV_OBJ_FLAG_SCROLLABLE);

    /* ---------- 中央登录卡片 ---------- */
    lv_obj_t *card = ui_widget_create_box(
        login_scr,
        420, 280,
        LV_ALIGN_CENTER, 0, 0,
        COLOR_GRAY_LIGHTEST, 24, false
    );

    /* 标题: 请输入密码 */
    ui_widget_create_label_static(
        card,
        "请输入密码",
        LV_ALIGN_TOP_MID, 0, 0,
        FONT_TITLE,
        COLOR_TEXT_DARK
    );

    /* 密码输入框 (绑定全局软键盘 kb) */
    pwd_ta = ui_widget_create_textarea(
        card,
        320, 50,
        LV_ALIGN_TOP_MID, 0, 60,
        kb
    );
    /* 开启密码模式 (输入显示 *) */
    lv_textarea_set_password_mode(pwd_ta, true);
    /* 限制最大长度 */
    lv_textarea_set_max_length(pwd_ta, LOGIN_PWD_MAX_LEN);
    /* 占位提示文字 */
    lv_textarea_set_placeholder_text(pwd_ta, "请输入密码");

    /* 错误提示标签 (默认空, 密码错误时显示红色文字) */
    err_lbl = ui_widget_create_label(
        card,
        "",
        LV_ALIGN_TOP_MID, 0, 120,
        FONT_SUBTITLE,
        COLOR_RED_NORMAL
    );

    /* 确认按钮 */
    ui_widget_create_btn(
        card, "确认",
        140, 46,
        COLOR_BLUE_NORMAL, COLOR_BLUE_DARK,
        LV_ALIGN_BOTTOM_LEFT, 30, -20,
        0, login_confirm_cb
    );

    /* 退出按钮 */
    ui_widget_create_btn(
        card, "退出",
        140, 46,
        COLOR_RED_NORMAL, COLOR_RED_DARK,
        LV_ALIGN_BOTTOM_RIGHT, -30, -20,
        0, login_exit_cb
    );

    return login_scr;
}

void ui_login_scr_load(void)
{
    if (login_scr == NULL)
    {
        printf("[Login] screen not created, create first\n");
        return;
    }

    /* 切换到登录屏前清空状态, 防止上次错误残留 */
    if (pwd_ta != NULL)
        lv_textarea_set_text(pwd_ta, "");
    if (err_lbl != NULL)
        lv_label_set_text(err_lbl, "");

    lv_screen_load(login_scr);
}
