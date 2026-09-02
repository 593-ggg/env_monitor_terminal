#include "test.h"
#include <stdio.h>
#include <unistd.h>

// 全局变量定义
pthread_t g_cam_thread = 0;
bool g_cam_thread_run = false;
lv_obj_t *g_cam_preview = NULL;

// 摄像头采集线程主循环
static void *camera_thread_entry(void *arg)
{
    (void)arg;
    g_cam_thread_run = true;
    printf("[TEST] camera capture thread start\n");

    while (g_cam_thread_run)
    {
        if (camera_capture_frame() != 0)
        {
            usleep(10000);
            continue;
        }
        usleep(33000); // 限制30FPS
    }

    printf("[TEST] camera capture thread exit\n");
    return NULL;
}

// LVGL定时器：刷新摄像头画面
static void cam_refresh_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    if (g_cam_preview != NULL)
    {
        camera_lvgl_update(g_cam_preview);
    }
}

int camera_unit_test(void)
{
    printf("[TEST] camera unit test start\n");

    // 1. 初始化底层V4L摄像头硬件
    if (camera_init() != 0)
    {
        printf("[TEST ERROR] camera_init fail\n");
        return -1;
    }

    // 2. 启动独立采集线程
    if (pthread_create(&g_cam_thread, NULL, camera_thread_entry, NULL) != 0)
    {
        printf("[TEST ERROR] create cam thread fail\n");
        camera_deinit();
        return -1;
    }

    // 3. LVGL创建居中摄像头预览窗口
    // 屏幕居中，显示尺寸400*300(4:3和640*480等比例不变形)
    g_cam_preview = camera_lvgl_create(
        lv_scr_act(),
        400,
        300,
        LV_ALIGN_CENTER, // 屏幕正中间
        0,
        0
    );
    if (g_cam_preview == NULL)
    {
        printf("[TEST ERROR] create cam preview widget fail\n");
        camera_unit_test_deinit();
        return -1;
    }

    // 4. 创建33ms刷新定时器，实时更新画面
    lv_timer_create(cam_refresh_timer_cb, 33, NULL);

    printf("[TEST] camera test run success, preview in screen center\n");
    return 0;
}

void camera_unit_test_deinit(void)
{
    // 1. 停止采集线程
    if (g_cam_thread_run)
    {
        g_cam_thread_run = false;
        pthread_join(g_cam_thread, NULL);
        g_cam_thread = 0;
    }

    // 2. 释放LVGL摄像头适配层缓冲
    camera_lvgl_deinit();
    g_cam_preview = NULL;

    // 3. 释放V4L摄像头底层资源
    camera_deinit();

    printf("[TEST] camera unit test resource released\n");
}