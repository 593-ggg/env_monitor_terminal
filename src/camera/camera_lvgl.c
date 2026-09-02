#include "camera_lvgl.h"
#include "ui_widget.h"
#include "thread.h"
#include "image.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

/* LVGL 显示用的 ARGB8888 缓冲 */
static cam_pixel_t *lvgl_frame_buf = NULL;

/* LVGL 图像描述符 */
static lv_image_dsc_t img_dsc;

/* ============================================================
 *                  摄像头 LVGL 控件创建
 * ============================================================ */
lv_obj_t* camera_lvgl_create(lv_obj_t *parent, int32_t disp_w, int32_t disp_h,
                             lv_align_t align, int32_t x_ofs, int32_t y_ofs)
{
    if (!lvgl_frame_buf)
    {
        lvgl_frame_buf = (cam_pixel_t *)malloc(CAM_FRAME_SIZE);
        if (!lvgl_frame_buf) return NULL;
    }
    memset(lvgl_frame_buf, 0, CAM_FRAME_SIZE);

    memset(&img_dsc, 0, sizeof(img_dsc));
    img_dsc.header.cf      = LV_COLOR_FORMAT_ARGB8888;
    img_dsc.header.w       = CAM_WIDTH;
    img_dsc.header.h       = CAM_HEIGHT;
    img_dsc.header.stride  = CAM_WIDTH * 4;
    img_dsc.data           = (const uint8_t *)lvgl_frame_buf;
    img_dsc.data_size      = CAM_FRAME_SIZE;

    // 使用传入的自定义宽高，对齐由外部控制
    lv_obj_t *img = ui_widget_create_img_buffer(parent, &img_dsc,
                                                disp_w, disp_h,
                                                LV_IMAGE_ALIGN_STRETCH,
                                                align,
                                                x_ofs, y_ofs);
    return img;
}

/* ============================================================
 *              UI 定时器每帧调用的画面更新
 * ============================================================ */
void camera_lvgl_update(lv_obj_t *img)
{
    if (!img || !lvgl_frame_buf) return;
    if (!camera_has_new_frame()) return;

    /* 步骤1: 拷贝当前摄像头原始画面 (ARGB8888) */
    camera_snapshot(lvgl_frame_buf, CAM_FRAME_SIZE);
    camera_clear_new_frame();

    /* 步骤2: AI 线程运行中 → 在画面上叠加检测框 (未启动则跳过) */
    if (ai_thread_is_running())
    {
        /* 1. 从 AI 线程取最新检测结果 */
        yolov8_result_t result;
        memset(&result, 0, sizeof(result));
        if (ai_thread_get_result(&result) == 0 && result.count > 0)
        {
            /* 2. 调用通用原地绘制函数，直接修改 lvgl_frame_buf */
            yolov8_render_detection_inplace(
                (unsigned char *)lvgl_frame_buf,
                CAM_WIDTH,
                CAM_HEIGHT,
                &result
            );
        }
        /* 结果未就绪时跳过画框，保持原始画面继续显示 */
    }

    /* 步骤3: 通知 LVGL 重绘 */
    lv_obj_invalidate(img);
}

const cam_pixel_t *camera_lvgl_get_display_buffer(void)
{
    return lvgl_frame_buf;
}

void camera_lvgl_deinit(void)
{
    free(lvgl_frame_buf);
    lvgl_frame_buf = NULL;
}
