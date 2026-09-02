#include "ui.h"
#include "ui_widget.h"
#include "lvgl/lvgl.h"
#include "config.h"
#include <stdio.h>

/* 开机动画持续时间 (毫秒) */
#define BOOT_ANIM_DURATION_MS   2500

static lv_obj_t *boot_scr = NULL;
static lv_timer_t *boot_timer = NULL;

/**
 * @brief 开机动画定时器回调：动画结束后跳转到登录界面
 * @note  只执行一次后自动删除定时器
 */
static void boot_timer_cb(lv_timer_t *timer)
{
    (void)timer;

    /* 删除定时器，避免重复触发 */
    if (boot_timer != NULL)
    {
        lv_timer_del(boot_timer);
        boot_timer = NULL;
    }

    /* 跳转到登录界面 */
    ui_login_scr_load();
}

lv_obj_t* ui_boot_scr_create(void)
{
    if (boot_scr != NULL)
        return boot_scr;

    boot_scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(boot_scr, COLOR_GRAY_BLACKISH, 0);
    lv_obj_set_style_bg_opa(boot_scr, LV_OPA_COVER, 0);
    lv_obj_clear_flag(boot_scr, LV_OBJ_FLAG_SCROLLABLE);

    /* 屏幕中央: spinner 加载圈 */
    ui_widget_create_spinner(
        boot_scr,
        80,                             /* 直径 */
        1200,                           /* 旋转一圈 1.2s */
        270,                            /* 弧线角度 */
        LV_ALIGN_CENTER, 0, -40
    );

    /* 标题: 系统名称 */
    ui_widget_create_label_static(
        boot_scr,
        "环境监测终端 RK3568",
        LV_ALIGN_TOP_MID, 0, 180,
        FONT_TITLE,
        COLOR_TEXT_WHITE
    );

    /* 副标题: 加载提示 */
    ui_widget_create_label_static(
        boot_scr,
        "系统加载中...",
        LV_ALIGN_BOTTOM_MID, 0, -60,
        FONT_SUBTITLE,
        COLOR_GRAY_LIGHT
    );

    return boot_scr;
}

void ui_boot_scr_load(void)
{
    if (boot_scr == NULL)
    {
        printf("[Boot] screen not created, create first\n");
        return;
    }

    /* 加载开机屏 (带淡入动画) */
    lv_screen_load(boot_scr);

    /* 启动一次性定时器，BOOT_ANIM_DURATION_MS 后跳转登录界面 */
    if (boot_timer != NULL)
    {
        lv_timer_del(boot_timer);
    }
    boot_timer = lv_timer_create(boot_timer_cb, BOOT_ANIM_DURATION_MS, NULL);
    lv_timer_set_repeat_count(boot_timer, 1);
}
