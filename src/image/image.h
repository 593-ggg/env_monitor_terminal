#ifndef __IMAGE_H__
#define __IMAGE_H__
#include <stdint.h>
#include <stdbool.h>
#include "yolov8_wrap.h"
#include "bmp.h"

int yolov8_render_detection_inplace(unsigned char* frame_buf,
                                    int width,
                                    int height,
                                    const yolov8_result_t* det_result);

/**
 * @brief 抓拍并保存为 BMP
 * @param dir_path 存储目录
 * @param bpp 输出位深度 BMP_BPP_24 / BMP_BPP_32
 */
int image_snapshot_bmp(const char *dir_path, bmp_bpp_t bpp);

/**
 * @brief 抓拍保存到指定文件路径
 * @param filepath 完整输出路径
 * @param bpp 输出位深度
 */
int image_save_bmp(const char *filepath, bmp_bpp_t bpp);

/**
 * @brief 对已加载完成的 ARGB8888 内存图像执行YOLO推理并原地绘制检测框
 * @param argb_buf 输入ARGB8888像素缓冲，函数会直接修改该内存
 * @param w 图像宽度
 * @param h 图像高度
 * @return 0 推理渲染成功；-1 推理/绘制失败
 */
int image_argb_detect(uint32_t *argb_buf, int w, int h);

/**
 * @brief 读取BMP推理绘制框，返回ARGB8888缓存，同时输出原图位深度
 * @param bmp_path 输入bmp路径
 * @param out_buf 输出图像缓存
 * @param out_w 输出宽
 * @param out_h 输出高
 * @param src_bpp 输出原始bmp的位深度（24/32）
 */
int image_bmp_detect(const char *bmp_path, unsigned char **out_buf, int *out_w, int *out_h, bmp_bpp_t *src_bpp);

/**
 * @brief 读取BMP推理后保存新BMP，输出位深度和输入原图一致，无需传bpp
 * @param input_path 输入bmp
 * @param output_path 输出bmp路径
 */
int image_bmp_detect_save(const char *input_path, const char *output_path);

#endif