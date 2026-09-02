#include "config.h"
#include "my_font.h"
#include "thread.h"
#include "ui.h"
#include "ui_widget.h"
#include <stdio.h>


/* ======================================================================================================= */
/* ---------------------------------------------计时器----------------------------------------------------- */
/* ======================================================================================================= */

/* 定时器回调：从 GY39 线程读取最新数据并刷新 label */
static void gy39_refresh_timer_cb(lv_timer_t *timer)
{
    ui_gy39_ctx_t *ctx = (ui_gy39_ctx_t *) lv_timer_get_user_data(timer);
    if (ctx == NULL)
        return;

    /* 读取最新传感器数据 */
    Gy39LuxData lux;
    Gy39EnvData env;
    gy39_get_lux_data(&lux);
    gy39_get_env_data(&env);

    /* 更新光照 label */
    if (ctx->label_lux != NULL)
    {
        char buf[64];
        snprintf(buf, sizeof(buf), "光照: %.1f lux", lux.lux);
        lv_label_set_text(ctx->label_lux, buf);
    }

    /* 更新环境 label */
    if (ctx->label_env != NULL)
    {
        char buf[128];
        snprintf(buf, sizeof(buf), "温度: %.1f℃  气压: %.1f hPa\n湿度: %.1f%%  海拔: %.1f m", env.temp, env.press,
                 env.hum, env.alt);
        lv_label_set_text(ctx->label_env, buf);
    }
}

void ui_gy39_start_timers(ui_gy39_ctx_t *ctx)
{
    if (ctx == NULL)
        return;

    /* 已经有timer就先删掉, 避免重复 */
    if (ctx->timer != NULL)
    {
        lv_timer_del(ctx->timer);
        ctx->timer = NULL;
    }

    // 创建计时器
    ctx->timer = lv_timer_create(gy39_refresh_timer_cb, 500, ctx);
}

void ui_gy39_stop_timers(ui_gy39_ctx_t *ctx)
{
    if (ctx->timer != NULL)
    {
        lv_timer_del(ctx->timer);
        ctx->timer = NULL;
    }
}

/* ======================================================================================================= */
/* ---------------------------------------------ui设计---------------------------------------------------- */
/* ======================================================================================================= */


ui_gy39_ctx_t *ui_gy39_create(lv_obj_t *parent, lv_align_t align, int32_t x_ofs, int32_t y_ofs, int32_t width,
                              int32_t height)
{
    if (parent == NULL || width <= 0 || height <= 0)
    {
        printf("[UI GY39] invalid param\n");
        return NULL;
    }

    ui_gy39_ctx_t *ctx = malloc(sizeof(ui_gy39_ctx_t));
    if (ctx == NULL)
        return NULL;

    /* 创建容器底板 */
    ctx->gy39_box = ui_widget_create_box(parent, width, height, align, x_ofs, y_ofs, COLOR_GRAY_LIGHTEST, 10, false);
    if (ctx->gy39_box == NULL)
    {
        free(ctx);
        return NULL;
    }

    /* 创建光照数据 label */
    ctx->label_lux =
        ui_widget_create_label(ctx->gy39_box, "光照: -- lux", LV_ALIGN_TOP_MID, 0, 10, FONT_BODY, COLOR_TEXT_DARK);
    if (ctx->label_lux == NULL)
    {
        lv_obj_del(ctx->gy39_box);
        free(ctx);
        return NULL;
    }

    /* 创建环境数据 label */
    ctx->label_env = ui_widget_create_label(ctx->gy39_box, "温度: --  气压: --\n湿度: --  海拔: --",
                                            LV_ALIGN_BOTTOM_MID, 0, -10, FONT_BODY, COLOR_TEXT_DARK);
    if (ctx->label_env == NULL)
    {
        lv_obj_del(ctx->gy39_box);
        free(ctx);
        return NULL;
    }

    printf("[UI GY39] create success\n");
    return ctx;
}

void ui_gy39_delete(ui_gy39_ctx_t *ctx)
{
    if (ctx == NULL)
        return;

    if (ctx->timer != NULL)
    {
        lv_timer_del(ctx->timer);
    }
    if (ctx->gy39_box != NULL)
    {
        lv_obj_del(ctx->gy39_box);
    }
    free(ctx);
}
