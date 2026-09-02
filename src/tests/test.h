#ifndef __TEST_H__
#define __TEST_H__

#include "lvgl/lvgl.h"
#include "camera_lvgl.h"
#include "v4l2_camera.h"
#include <pthread.h>
#include <stdbool.h>

// 摄像头采集线程全局标记
extern pthread_t g_cam_thread;
extern bool g_cam_thread_run;
// LVGL摄像头预览控件全局句柄
extern lv_obj_t *g_cam_preview;

/**
 * @brief 摄像头模块单元测试入口
 * @return 0正常，-1失败
 */
int camera_unit_test(void);

/**
 * @brief 停止并释放摄像头测试所有资源
 */
void camera_unit_test_deinit(void);

#endif