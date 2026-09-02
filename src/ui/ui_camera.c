#include "camera_lvgl.h"
#include "config.h"
#include "my_font.h"
#include "thread.h"
#include "ui.h"
#include "ui_widget.h"
#include "v4l2_camera.h"
#include <stdio.h>

/* ======================================================================================================= */
/* ---------------------------------------------计时器----------------------------------------------------- */
/* ======================================================================================================= */

// LVGL定时器：刷新摄像头画面
static void cam_refresh_timer_cb(lv_timer_t *timer)
{
    ui_camera_ctx_t *ctx = (ui_camera_ctx_t *) lv_timer_get_user_data(timer);
    if (ctx && ctx->cam_img != NULL)
    {
        camera_lvgl_update(ctx->cam_img);
    }
}

void ui_camera_start_timers(ui_camera_ctx_t *ctx)
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
    ctx->timer = lv_timer_create(cam_refresh_timer_cb, 33, ctx);
}

void ui_camera_stop_timers(ui_camera_ctx_t *ctx)
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

ui_camera_ctx_t *ui_camera_create(lv_obj_t *parent, lv_align_t align, int32_t x_ofs, int32_t y_ofs, int32_t width,
                                  int32_t height)
{
    if (parent == NULL || width <= 0 || height <= 40)
    {
        printf("[UI CAMERA] invalid param\n");
        return NULL;
    }

    ui_camera_ctx_t *ctx = malloc(sizeof(ui_camera_ctx_t));
    if (ctx == NULL)
        return NULL;

    // 可用绘图区域
    int32_t avail_w = width;
    int32_t avail_h = height;

    int32_t disp_w, disp_h;
    // 4:3计算，在avail_w × avail_h内最大4:3
    if (avail_w * 3 > avail_h * 4)
    {
        disp_w = avail_h * 4 / 3;
        disp_h = avail_h;
    }
    else
    {
        disp_w = avail_w;
        disp_h = avail_w * 3 / 4;
    }

    // 创建摄像头图像容器
    ctx->cam_box = ui_widget_create_box(parent, disp_w, disp_h, align, x_ofs, y_ofs, COLOR_GRAY_LIGHTEST, 0, false);

    // 创建摄像头画面
    ctx->cam_img = camera_lvgl_create(ctx->cam_box, disp_w, disp_h, LV_ALIGN_CENTER, 0, 0);
    if (!ctx->cam_img)
    {
        printf("[UI CAMERA ERROR] camera_lvgl_create fail\n");
        return NULL;
    }

    ctx->timer = NULL;

    return ctx;
}


void ui_camera_delete(ui_camera_ctx_t *ctx)
{
    if (ctx == NULL)
        return;

    if (ctx->timer != NULL)
    {
        lv_timer_del(ctx->timer);
    }
    if (ctx->cam_box != NULL)
    {
        lv_obj_del(ctx->cam_box);
    }
    if (ctx->cam_img != NULL)
    {
        lv_obj_del(ctx->cam_img);
    }
    free(ctx);
}
