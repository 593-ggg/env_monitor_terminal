#include "thread.h"
#include "v4l2_camera.h"
#include "config_manager.h"
#include <stdio.h>
#include <unistd.h>

/* ==================== 线程状态 ==================== */
static pthread_t cam_thread_id = 0;
static volatile bool cam_thread_run = false;

/* ==================== 线程入口函数 ==================== */

static void *camera_thread_entry(void *arg)
{
    (void) arg;

    /* 从配置读取帧率，计算每帧间隔 */
    int fps = DEF_CAMERA_FPS;
    AppConfig* cfg = config_get_global();
    if (cfg)
        fps = cfg->camera_fps;
    int interval_us = 1000000 / fps;

    printf("[CAM_THREAD] capture thread start, FPS limit %d (interval %dus)\n", fps, interval_us);

    while (cam_thread_run)
    {
        if (camera_capture_frame() != 0)
        {
            usleep(10000);
            continue;
        }
        usleep(interval_us);
    }

    printf("[CAM_THREAD] capture thread exit\n");
    return NULL;
}

/* ==================== 公共接口实现 ==================== */

int cam_thread_start(void)
{
    // 检查摄像头设备是否存在
    if (!camera_is_device_exist())
    {
        printf("[CAM_THREAD] camera device not exist\n");
        return -1;
    }

    if (cam_thread_run)
    {
        printf("[CAM_THREAD] thread already running\n");
        return 0;
    }

    // 先初始化摄像头硬件
    if (camera_init() != 0)
    {
        printf("[CAM_THREAD] camera_init failed\n");
        return -1;
    }

    cam_thread_run = true;

    if (pthread_create(&cam_thread_id, NULL, camera_thread_entry, NULL) != 0)
    {
        printf("[CAM_THREAD] pthread_create failed\n");
        cam_thread_run = false;
        camera_deinit();
        return -1;
    }

    printf("[CAM_THREAD] start success\n");
    return 0;
}

void cam_thread_stop(void)
{
    if (!cam_thread_run)
        return;

    cam_thread_run = false;

    if (cam_thread_id != 0)
    {
        pthread_join(cam_thread_id, NULL);
        cam_thread_id = 0;
    }

    camera_deinit();
}

bool cam_thread_is_running(void)
{
    return cam_thread_run;
}
