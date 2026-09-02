#ifndef __CAMERA_LVGL_H__
#define __CAMERA_LVGL_H__

#include "lvgl/lvgl.h"
#include "v4l2_camera.h"

/**
 * @brief 创建摄像头预览控件
 * @param parent 父容器
 * @param disp_w 界面显示宽度
 * @param disp_h 界面显示高度
 * @param align 整体对齐方式（居中填 LV_ALIGN_CENTER）
 * @param x_ofs x偏移
 * @param y_ofs y偏移
 * @return lv_image控件
 */
lv_obj_t* camera_lvgl_create(lv_obj_t *parent, int32_t disp_w, int32_t disp_h,
                             lv_align_t align, int32_t x_ofs, int32_t y_ofs);

/**
 * @brief 刷新摄像头画面到 lv_image (在 lv_timer 回调中调用)
 *        内部检查新帧标志, 有新帧时拷贝数据并触发重绘
 * @param img  camera_lvgl_create 返回的 lv_image 控件
 */
void camera_lvgl_update(lv_obj_t *img);

/**
 * @brief 获取当前显示缓冲 (ARGB8888). 包含 AI 画框等最终画面, 可用于抓拍保存.
 *        注: 返回指针只读有效, 直到下次 camera_lvgl_update 被调用.
 * @return 当前显示缓冲指针; 若尚未初始化则返回 NULL
 */
const cam_pixel_t *camera_lvgl_get_display_buffer(void);

/**
 * @brief 释放适配层内部缓冲 (camera_deinit 前调用)
 */
void camera_lvgl_deinit(void);

#endif
