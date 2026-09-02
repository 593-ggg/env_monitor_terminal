#include "image.h"
#include "config_manager.h"
#include "bmp.h"
#include "v4l2_camera.h"
#include "camera_lvgl.h"
#include "utils/image_drawing.h"
#include "utils/common.h"
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>

static const unsigned int g_cls_colors[] = {
    COLOR_RED,    COLOR_GREEN,  COLOR_BLUE,   COLOR_YELLOW,
    COLOR_ORANGE, 0xFF00FFFF,   0xFFFF00FF,   0xFF008000,
};
#define CLS_COLOR_COUNT (sizeof(g_cls_colors) / sizeof(g_cls_colors[0]))
#define DEF_BOX_THICKNESS  2
#define DEF_TEXT_COLOR     COLOR_WHITE
#define DEF_TEXT_FONT_SIZE 12
#define DEF_TEXT_OFFSET_Y  16

int yolov8_render_detection_inplace(unsigned char* frame_buf,
                                    int width,
                                    int height,
                                    const yolov8_result_t* det_result)
{
    if (!frame_buf || !det_result)
    {
        printf("[YOLO_RENDER] null frame buffer or detection result\n");
        return -1;
    }
    if (width <= 0 || height <= 0)
    {
        printf("[YOLO_RENDER] invalid image size w=%d h=%d\n", width, height);
        return -1;
    }
    if (det_result->count <= 0)
        return 0;

    /* 从全局配置读取置信度阈值（百分比转0.0~1.0） */
    float conf_threshold = 0.0f;
    AppConfig* cfg = config_get_global();
    if (cfg)
        conf_threshold = config_get_ai_confidence(cfg->ai_confidence);

    image_buffer_t draw_ctx;
    memset(&draw_ctx, 0, sizeof(draw_ctx));
    draw_ctx.width     = width;
    draw_ctx.height    = height;
    draw_ctx.format    = IMAGE_FORMAT_RGBA8888;
    draw_ctx.virt_addr = frame_buf;
    draw_ctx.size      = (size_t)width * height * 4;
    for (int i = 0; i < det_result->count; i++)
    {
        const yolov8_detect_t* d = &det_result->items[i];

        /* 置信度低于阈值的检测结果不画框 */
        if (d->prop < conf_threshold)
            continue;

        int box_w = d->right  - d->left;
        int box_h = d->bottom - d->top;
        if (box_w <= 0 || box_h <= 0)
            continue;
        unsigned int box_color = g_cls_colors[d->cls_id % CLS_COLOR_COUNT];
        draw_rectangle(&draw_ctx, d->left, d->top, box_w, box_h, box_color, DEF_BOX_THICKNESS);
        char label_buf[48] = {0};
        const char* cls_name = yolov8_cls_name(d->cls_id);
        if (!cls_name)
            cls_name = "unknown";
        snprintf(label_buf, sizeof(label_buf), "%s %.2f", cls_name, d->prop);
        int text_x = d->left;
        int text_y = d->top - DEF_TEXT_OFFSET_Y;
        if (text_y < 0)
            text_y = d->top + 2;
        draw_text(&draw_ctx, label_buf, text_x, text_y, DEF_TEXT_COLOR, DEF_TEXT_FONT_SIZE);
    }
    return 0;
}

int image_save_bmp(const char *filepath, bmp_bpp_t bpp)
{
    if (filepath == NULL)
    {
        printf("[IMAGE] filepath is NULL\n");
        return -1;
    }
    const cam_pixel_t *src_frame = NULL;
    cam_pixel_t *snapshot_frame = NULL;
    src_frame = camera_lvgl_get_display_buffer();
    if (src_frame == NULL)
    {
        snapshot_frame = (cam_pixel_t *)malloc(CAM_FRAME_SIZE);
        if (snapshot_frame == NULL)
        {
            printf("[IMAGE] malloc frame failed\n");
            return -1;
        }
        if (camera_snapshot(snapshot_frame, CAM_FRAME_SIZE) != 0)
        {
            printf("[IMAGE] camera_snapshot failed\n");
            free(snapshot_frame);
            return -1;
        }
        src_frame = snapshot_frame;
    }
    int ret = bmp_save_from_argb8888(filepath, (const unsigned char *)src_frame, CAM_WIDTH, CAM_HEIGHT, bpp);
    if (snapshot_frame) free(snapshot_frame);
    return ret;
}

int image_snapshot_bmp(const char *dir_path, bmp_bpp_t bpp)
{
    if (dir_path == NULL)
    {
        printf("[IMAGE] invalid param\n");
        return -1;
    }

    int ret = mkdir(dir_path, 0755);
    if (ret != 0)
    {
        // EEXIST 代表目录已经存在，不算错误；其他错误才返回失败
        if (errno != EEXIST)
        {
            printf("[IMAGE] create dir failed, dir:%s errno:%d\n", dir_path, errno);
            return -1;
        }
    }

    time_t now = time(NULL);
    struct tm tm_buf;   // 调用者栈上分配缓冲区
    struct tm *tm_info = localtime_r(&now, &tm_buf);
    if (tm_info == NULL)
    {
        printf("[IMAGE] get local time failed\n");
        return -1;
    }

    char filename[128];
    strftime(filename, sizeof(filename), "/snap_%Y%m%d_%H%M%S.bmp", tm_info);
    char filepath[256];
    snprintf(filepath, sizeof(filepath), "%s%s", dir_path, filename);
    return image_save_bmp(filepath, bpp);
}

/**
 * @brief 将 ARGB8888 (4字节/像素) 转换为 RGB888 (3字节/像素)
 *        内存字节序: {B,G,R,A} → {R,G,B}
 * @param argb  输入 ARGB8888 缓冲
 * @param rgb   输出 RGB888 缓冲 (调用方需保证 size >= w*h*3)
 * @param w     图像宽度
 * @param h     图像高度
 */
static void argb8888_to_rgb888(const uint32_t *argb, uint8_t *rgb, int w, int h)
{
    for (int i = 0; i < w * h; i++)
    {
        uint32_t p = argb[i];
        // argb 内存布局: B[7:0] | G[15:8] | R[23:16] | A[31:24]
        rgb[i * 3 + 0] = (uint8_t)((p >> 16) & 0xFF); // R
        rgb[i * 3 + 1] = (uint8_t)((p >> 8)  & 0xFF); // G
        rgb[i * 3 + 2] = (uint8_t)( p        & 0xFF); // B
    }
}

int image_argb_detect(uint32_t *argb_buf, int w, int h)
{
    if (!argb_buf || w <= 0 || h <= 0)
    {
        printf("[IMAGE_ARG] invalid argb buffer or size w=%d h=%d\n", w, h);
        return -1;
    }

    // 按需初始化模型，用完释放
    bool need_deinit = !yolov8_is_initialized();
    if (need_deinit)
    {
        if (yolov8_init(YOLOV8_MODEL_PATH) < 0)
        {
            printf("[IMAGE_ARG] yolov8 init fail\n");
            return -1;
        }
    }

    // 构造推理输入上下文
    // 将ARGB8888转换为RGB888，确保不论走RGA还是CPU都能正确转换
    uint8_t *rgb_buf = (uint8_t *)malloc((size_t)w * h * 3);
    if (rgb_buf == NULL)
    {
        printf("[IMAGE_ARG] malloc rgb888 buffer failed\n");
        if (need_deinit) yolov8_deinit();
        return -1;
    }
    argb8888_to_rgb888(argb_buf, rgb_buf, w, h);

    image_buffer_t img_buf;
    memset(&img_buf, 0, sizeof(img_buf));
    img_buf.width     = w;
    img_buf.height    = h;
    img_buf.format    = IMAGE_FORMAT_RGB888;
    img_buf.virt_addr = rgb_buf;
    img_buf.size      = (size_t)w * h * 3;

    // 执行推理
    yolov8_result_t result;
    memset(&result, 0, sizeof(result));
    int ret = yolov8_detect(&img_buf, &result);
    free(rgb_buf);
    if (ret < 0)
    {
        printf("[IMAGE_ARG] yolov8 detect failed\n");
        if (need_deinit)
        {
            yolov8_deinit();
        }
        return ret;
    }
    printf("[IMAGE_ARG] detection done: %d objects found\n", result.count);

    // 原地绘制检测框文字
    ret = yolov8_render_detection_inplace((unsigned char *)argb_buf, w, h, &result);
    if (ret < 0)
    {
        printf("[IMAGE_ARG] render detection box failed\n");
    }

    // 仅本函数初始化模型才释放
    if (need_deinit)
    {
        yolov8_deinit();
    }
    return ret;
}

int image_bmp_detect(const char *bmp_path, unsigned char **out_buf, int *out_w, int *out_h, bmp_bpp_t *src_bpp)
{
    if (bmp_path == NULL || out_buf == NULL || out_w == NULL || out_h == NULL || src_bpp == NULL)
    {
        printf("[IMAGE] invalid param for image_bmp_detect\n");
        return -1;
    }
    int w = 0, h = 0;
    bmp_bpp_t raw_bpp;
    // 1. 读取BMP文件，分配ARGB8888缓冲
    uint32_t *argb_buf = bmp_load_to_argb8888(bmp_path, &w, &h, &raw_bpp);
    if (argb_buf == NULL)
    {
        printf("[IMAGE] bmp_load_to_argb8888 failed\n");
        return -1;
    }

    // 2. 调用通用推理渲染函数
    int render_ret = image_argb_detect(argb_buf, w, h);
    if (render_ret < 0)
    {
        free(argb_buf);
        return -1;
    }
    
    // 3. 回填输出参数，缓冲交给调用方释放
    *out_buf = (unsigned char *)argb_buf;
    *out_w = w;
    *out_h = h;
    *src_bpp = raw_bpp;
    return 0;
}

int image_bmp_detect_save(const char *input_path, const char *output_path)
{
    if (input_path == NULL || output_path == NULL)
    {
        printf("[IMAGE] invalid param for image_bmp_detect_save\n");
        return -1;
    }
    unsigned char *buf = NULL;
    int w = 0, h = 0;
    bmp_bpp_t src_bpp;
    if (image_bmp_detect(input_path, &buf, &w, &h, &src_bpp) < 0)
    {
        printf("[IMAGE] image_bmp_detect failed\n");
        return -1;
    }
    // 自动使用原图的位深度保存
    int ret = bmp_save_from_argb8888(output_path, buf, w, h, src_bpp);
    free(buf);
    return ret;
}