#include "yolov8_wrap.h"
#include "yolov8.h"
#include "postprocess.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static rknn_app_context_t g_app_ctx;
static bool g_initialized = false;

static bool file_is_exist(const char *path)
{
    if (path == nullptr || strlen(path) == 0)
        return false;
    FILE *fp = fopen(path, "r");
    if (fp == nullptr)
        return false;
    fclose(fp);
    return true;
}

extern "C" bool yolov8_file_all_exist(void)
{
    bool model_ok = file_is_exist(YOLOV8_MODEL_PATH);
    bool label_ok = file_is_exist(YOLOV8_LABEL_PATH);

    if (!model_ok)
        printf("[YOLO] Model file missing: %s\n", YOLOV8_MODEL_PATH);
    if (!label_ok)
        printf("[YOLO] Label file missing: %s\n", YOLOV8_LABEL_PATH);

    return model_ok && label_ok;
}

extern "C" int yolov8_init(const char *model_path)
{
    if (!yolov8_file_all_exist())
    {
        return -1;
    }

    if (g_initialized)
    {
        printf("[YOLOv8] already initialized\n");
        return 0;
    }

    if (model_path == NULL)
    {
        printf("[YOLOv8] model_path is NULL\n");
        return -1;
    }

    memset(&g_app_ctx, 0, sizeof(g_app_ctx));

    /* 1. 初始化后处理 (加载COCO标签列表) */
    if (init_post_process() < 0)
    {
        printf("[YOLOv8] init_post_process failed\n");
        return -1;
    }

    /* 2. 加载 RKNN 模型 */
    if (init_yolov8_model(model_path, &g_app_ctx) < 0)
    {
        printf("[YOLOv8] init_yolov8_model failed\n");
        deinit_post_process();
        return -1;
    }

    g_initialized = true;
    printf("[YOLOv8] init success, model=%dx%d, quant=%d\n",
           g_app_ctx.model_width, g_app_ctx.model_height, g_app_ctx.is_quant);
    return 0;
}

extern "C" int yolov8_detect(const image_buffer_t *img, yolov8_result_t *out)
{
    if (!g_initialized)
    {
        printf("[YOLOv8] not initialized\n");
        return -1;
    }
    if (img == NULL || out == NULL)
    {
        printf("[YOLOv8] invalid param\n");
        return -1;
    }

    memset(out, 0, sizeof(yolov8_result_t));

    /* 1. 执行推理+后处理 */
    object_detect_result_list od_results;
    if (inference_yolov8_model(&g_app_ctx, const_cast<image_buffer_t *>(img), &od_results) < 0)
    {
        printf("[YOLOv8] inference failed\n");
        return -1;
    }

    /* 2. 转换结果结构 */
    int cnt = od_results.count;
    if (cnt > YOLOV8_MAX_DET)
        cnt = YOLOV8_MAX_DET;

    out->count = cnt;
    for (int i = 0; i < cnt; i++)
    {
        const object_detect_result &src = od_results.results[i];
        yolov8_detect_t &dst = out->items[i];
        dst.cls_id = src.cls_id;
        dst.prop   = src.prop;
        dst.left   = src.box.left;
        dst.top    = src.box.top;
        dst.right  = src.box.right;
        dst.bottom = src.box.bottom;
    }

    return 0;
}

extern "C" void yolov8_deinit(void)
{
    if (!g_initialized)
        return;

    release_yolov8_model(&g_app_ctx);
    deinit_post_process();

    memset(&g_app_ctx, 0, sizeof(g_app_ctx));
    g_initialized = false;
    printf("[YOLOv8] deinit ok\n");
}

extern "C" bool yolov8_is_initialized(void)
{
    return g_initialized;
}

extern "C" const char *yolov8_cls_name(int cls_id)
{
    return coco_cls_to_name(cls_id);
}
