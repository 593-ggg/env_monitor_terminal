#include "thread.h"
#include "v4l2_camera.h"
#include "image_utils.h"
#include "yolov8_wrap.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* ==================== AI 线程配置 ==================== */
#define AI_INFER_INTERVAL_US  200000   /* 推理间隔约 200ms = 5fps  */

/* ==================== 线程状态 ==================== */
static pthread_t ai_thread_id = 0;
static volatile bool ai_thread_run = false;

/* 最新检测结果 (加锁保护) */
static yolov8_result_t g_latest_result;
static pthread_mutex_t g_result_lock = PTHREAD_MUTEX_INITIALIZER;

/* 输入缓冲: 摄像头直接输出 ARGB8888, RGA 负责转换为模型需要的 RGB888 */
static cam_pixel_t *g_cam_buf = NULL;
static image_buffer_t g_img_buf;

/* ==================== 线程入口 ==================== */

static void *ai_thread_entry(void *arg)
{
    (void) arg;
    printf("[AI_THREAD] inference thread start, interval %dus (~%dfps)\n",
           AI_INFER_INTERVAL_US, 1000000 / AI_INFER_INTERVAL_US);

    while (ai_thread_run)
    {
        if (!camera_is_run())
        {
            usleep(AI_INFER_INTERVAL_US);
            continue;
        }

        /* 1. 从摄像头获取当前帧 (ARGB8888) */
        if (camera_snapshot(g_cam_buf, CAM_FRAME_SIZE) < 0)
        {
            usleep(AI_INFER_INTERVAL_US);
            continue;
        }

        /* 2. 执行 YOLOv8 推理 (RGA 自动完成 ARGB8888→RGB888 + resize) */
        yolov8_result_t result;
        memset(&result, 0, sizeof(result));
        if (yolov8_detect(&g_img_buf, &result) == 0)
        {
            /* 3. 保存最新结果 (加锁) */
            pthread_mutex_lock(&g_result_lock);
            memcpy(&g_latest_result, &result, sizeof(yolov8_result_t));
            pthread_mutex_unlock(&g_result_lock);
        }

        /* 4. 限频 */
        usleep(AI_INFER_INTERVAL_US);
    }

    printf("[AI_THREAD] inference thread exit\n");
    return NULL;
}

/* ==================== 公共接口实现 ==================== */

int ai_thread_start()
{
    /* 模型路径使用硬编码宏 (在 yolov8_wrap.h 中定义) */
    const char *model_path = YOLOV8_MODEL_PATH;

    if (ai_thread_run)
    {
        printf("[AI_THREAD] thread already running\n");
        return 0;
    }

    /* 1. 初始化 YOLOv8 模型 */
    if (yolov8_init(model_path) < 0)
    {
        printf("[AI_THREAD] yolov8_init failed\n");
        return -1;
    }

    /* 2. 分配摄像头帧缓冲 (ARGB8888, 4字节/像素) */
    g_cam_buf = (cam_pixel_t *) malloc(CAM_FRAME_SIZE);
    if (g_cam_buf == NULL)
    {
        printf("[AI_THREAD] malloc buffer failed\n");
        yolov8_deinit();
        return -1;
    }

    /* 3. 初始化图像输入结构 (ARGB8888 → RGA 自动转换为模型需要的 RGB888) */
    memset(&g_img_buf, 0, sizeof(g_img_buf));
    g_img_buf.width     = CAM_WIDTH;
    g_img_buf.height    = CAM_HEIGHT;
    g_img_buf.format    = IMAGE_FORMAT_RGBA8888;
    g_img_buf.virt_addr = (unsigned char *)g_cam_buf;
    g_img_buf.size      = CAM_FRAME_SIZE;

    /* 4. 清空初始检测结果 */
    memset(&g_latest_result, 0, sizeof(g_latest_result));

    ai_thread_run = true;

    /* 5. 创建线程 */
    if (pthread_create(&ai_thread_id, NULL, ai_thread_entry, NULL) != 0)
    {
        printf("[AI_THREAD] pthread_create failed\n");
        ai_thread_run = false;
        free(g_cam_buf);
        g_cam_buf = NULL;
        yolov8_deinit();
        return -1;
    }

    printf("[AI_THREAD] start success, model=%s\n", model_path);
    return 0;
}

void ai_thread_stop(void)
{
    ai_thread_run = false;

    if (ai_thread_id != 0)
    {
        pthread_join(ai_thread_id, NULL);
        ai_thread_id = 0;
    }

    yolov8_deinit();

    if (g_cam_buf)
    {
        free(g_cam_buf);
        g_cam_buf = NULL;
    }

    printf("[AI_THREAD] stop ok\n");
}

bool ai_thread_is_running(void)
{
    return ai_thread_run;
}

int ai_thread_get_result(yolov8_result_t *out)
{
    if (out == NULL)
        return -1;

    pthread_mutex_lock(&g_result_lock);
    memcpy(out, &g_latest_result, sizeof(yolov8_result_t));
    pthread_mutex_unlock(&g_result_lock);

    return 0;
}