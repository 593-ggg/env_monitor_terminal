#include "ima_utils.h"
#include <stdio.h>
#include <string.h>
#include <math.h>

static inline uint32_t get_argb_pixel(const uint8_t *buf, int w, int x, int y)
{
    const uint32_t *line = (const uint32_t *)buf + y * w;
    return line[x];
}

static inline void set_argb_pixel(uint8_t *buf, int w, int x, int y, uint32_t pix)
{
    uint32_t *line = (uint32_t *)buf + y * w;
    line[x] = pix;
}

/**
 * 内部基础缩小：最近邻插值
 */
static int ima_downscale_nearest(const uint8_t *src, int sw, int sh,
                                 uint8_t *dst, int dw, int dh, float scale)
{
    if (!src || !dst || sw <= 0 || sh <=0 || dw <=0 || dh <=0)
        return -1;

    float inv_scale = 1.0f / scale;
    for (int dy = 0; dy < dh; dy++)
    {
        int sy = (int)roundf(dy * inv_scale);
        if (sy >= sh) sy = sh - 1;
        for (int dx = 0; dx < dw; dx++)
        {
            int sx = (int)roundf(dx * inv_scale);
            if (sx >= sw) sx = sw - 1;
            uint32_t pix = get_argb_pixel(src, sw, sx, sy);
            set_argb_pixel(dst, dw, dx, dy, pix);
        }
    }
    return 0;
}

/**
 * 内部基础放大：双线性插值，保留ARGB四通道
 */
static int ima_upscale_bilinear(const uint8_t *src, int sw, int sh,
                                uint8_t *dst, int dw, int dh, float scale)
{
    if (!src || !dst || sw <= 0 || sh <=0 || dw <=0 || dh <=0)
        return -1;

    float inv_scale = 1.0f / scale;
    for (int dy = 0; dy < dh; dy++)
    {
        float fy = dy * inv_scale;
        int sy0 = (int)floorf(fy);
        int sy1 = sy0 + 1;
        float alpha_y = fy - sy0;
        if (sy1 >= sh) sy1 = sh - 1;

        for (int dx = 0; dx < dw; dx++)
        {
            float fx = dx * inv_scale;
            int sx0 = (int)floorf(fx);
            int sx1 = sx0 + 1;
            float alpha_x = fx - sx0;
            if (sx1 >= sw) sx1 = sw - 1;

            uint32_t p00 = get_argb_pixel(src, sw, sx0, sy0);
            uint32_t p01 = get_argb_pixel(src, sw, sx1, sy0);
            uint32_t p10 = get_argb_pixel(src, sw, sx0, sy1);
            uint32_t p11 = get_argb_pixel(src, sw, sx1, sy1);

            // 分离RGBA各通道
            uint8_t b00 = (p00 >> 0) & 0xFF;
            uint8_t g00 = (p00 >> 8) & 0xFF;
            uint8_t r00 = (p00 >> 16) & 0xFF;
            uint8_t a00 = (p00 >> 24) & 0xFF;

            uint8_t b01 = (p01 >> 0) & 0xFF;
            uint8_t g01 = (p01 >> 8) & 0xFF;
            uint8_t r01 = (p01 >> 16) & 0xFF;
            uint8_t a01 = (p01 >> 24) & 0xFF;

            uint8_t b10 = (p10 >> 0) & 0xFF;
            uint8_t g10 = (p10 >> 8) & 0xFF;
            uint8_t r10 = (p10 >> 16) & 0xFF;
            uint8_t a10 = (p10 >> 24) & 0xFF;

            uint8_t b11 = (p11 >> 0) & 0xFF;
            uint8_t g11 = (p11 >> 8) & 0xFF;
            uint8_t r11 = (p11 >> 16) & 0xFF;
            uint8_t a11 = (p11 >> 24) & 0xFF;

            // 水平插值
            float b0 = b00 * (1 - alpha_x) + b01 * alpha_x;
            float g0 = g00 * (1 - alpha_x) + g01 * alpha_x;
            float r0 = r00 * (1 - alpha_x) + r01 * alpha_x;
            float a0 = a00 * (1 - alpha_x) + a01 * alpha_x;

            float b1 = b10 * (1 - alpha_x) + b11 * alpha_x;
            float g1 = g10 * (1 - alpha_x) + g11 * alpha_x;
            float r1 = r10 * (1 - alpha_x) + r11 * alpha_x;
            float a1 = a10 * (1 - alpha_x) + a11 * alpha_x;

            // 垂直插值
            uint8_t B = (uint8_t)(b0 * (1 - alpha_y) + b1 * alpha_y + 0.5f);
            uint8_t G = (uint8_t)(g0 * (1 - alpha_y) + g1 * alpha_y + 0.5f);
            uint8_t R = (uint8_t)(r0 * (1 - alpha_y) + r1 * alpha_y + 0.5f);
            uint8_t A = (uint8_t)(a0 * (1 - alpha_y) + a1 * alpha_y + 0.5f);

            uint32_t out_pix = (A << 24) | (R << 16) | (G << 8) | B;
            set_argb_pixel(dst, dw, dx, dy, out_pix);
        }
    }
    return 0;
}

void ima_calc_scale_size(int src_w, int src_h, float scale, int *out_w, int *out_h)
{
    *out_w = (int)(src_w * scale + 0.5f);
    *out_h = (int)(src_h * scale + 0.5f);
    if (*out_w < 1) *out_w = 1;
    if (*out_h < 1) *out_h = 1;
}

int ima_scale_argb8888(const uint8_t *src_buf, int src_w, int src_h,
                       uint8_t *dst_buf, float scale)
{
    if (!src_buf || !dst_buf)
    {
        printf("[IMA_UTILS] null buffer\n");
        return -1;
    }
    if (src_w <= 0 || src_h <= 0 || scale <= 0.0001f)
    {
        printf("[IMA_UTILS] invalid size or scale\n");
        return -1;
    }

    int dst_w, dst_h;
    ima_calc_scale_size(src_w, src_h, scale, &dst_w, &dst_h);

    // 倍率1：原图直接拷贝
    if (fabsf(scale - 1.0f) < 0.001f)
    {
        size_t copy_len = src_w * src_h * 4;
        memcpy(dst_buf, src_buf, copy_len);
        return 0;
    }

    int ret;
    if (scale < 1.0f)
    {
        // 缩小：最近邻，速度快
        ret = ima_downscale_nearest(src_buf, src_w, src_h, dst_buf, dst_w, dst_h, scale);
    }
    else
    {
        // 放大：双线性，平滑无锯齿
        ret = ima_upscale_bilinear(src_buf, src_w, src_h, dst_buf, dst_w, dst_h, scale);
    }
    return ret;
}