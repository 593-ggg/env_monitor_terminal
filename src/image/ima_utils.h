#ifndef __IMA_UTILS_H__
#define __IMA_UTILS_H__

#include <stdint.h>
#include <stdbool.h>

/**
 * @brief ARGB8888 图像缩放对外统一接口
 * @param src_buf 源图像ARGB8888缓存
 * @param src_w 源宽度
 * @param src_h 源高度
 * @param dst_buf 输出缩放图像缓存，外部提前分配内存
 * @param scale 缩放倍率
 *              >1 放大；<1 缩小；=1 原图拷贝
 * @return 0成功 -1参数非法
 */
int ima_scale_argb8888(const uint8_t *src_buf, int src_w, int src_h,
                       uint8_t *dst_buf, float scale);

/**
 * @brief 计算缩放后的图像宽高
 * @param src_w 源宽 src_h 源高
 * @param scale 缩放倍率
 * @param out_w 输出目标宽
 * @param out_h 输出目标高
 */
void ima_calc_scale_size(int src_w, int src_h, float scale, int *out_w, int *out_h);

#endif