#ifndef __BMP_H__
#define __BMP_H__
#include <stdint.h>
#include <stdbool.h>
#include <sys/types.h>

/* BMP 支持位深度枚举 */
typedef enum {
    BMP_BPP_24 = 24,
    BMP_BPP_32 = 32
} bmp_bpp_t;

#define IMG_BMP_HEADER          54      /* 文件头+信息头总字节 */

/* BMP 文件头 14字节 */
#pragma pack(1)
typedef struct {
    uint16_t bfType;        /* 文件标记 "BM" = 0x4D42 */
    uint32_t bfSize;        /* 文件整体字节大小 */
    uint16_t bfReserved1;
    uint16_t bfReserved2;
    uint32_t bfOffBits;     /* 像素数据起始偏移 */
} bmp_file_header_t;

/* BMP 信息头 40字节 */
typedef struct {
    uint32_t biSize;          /* 本结构体大小40 */
    int32_t  biWidth;         /* 图像宽度 */
    int32_t  biHeight;        /* 图像高度，正数=倒序存储 */
    uint16_t biPlanes;        /* 固定1 */
    uint16_t biBitCount;      /* 每像素位数 24 / 32 */
    uint32_t biCompression;   /* 0无压缩 */
    uint32_t biSizeImage;     /* 像素数据总大小 */
    int32_t  biXPelsPerMeter; /* 水平DPI分辨率 */
    int32_t  biYPelsPerMeter; /* 垂直DPI分辨率 */
    uint32_t biClrUsed;
    uint32_t biClrImportant;
} bmp_info_header_t;
#pragma pack()

/**
 * @brief ARGB8888单像素转BGR24（丢弃Alpha）
 * @param argb_ptr 源ARGB像素指针
 * @param bgr 输出3字节BGR缓存
 */
static inline void argb8888_to_bgr24(const uint8_t *argb_ptr, uint8_t *bgr)
{
    bgr[0] = argb_ptr[0];  /* B */
    bgr[1] = argb_ptr[1];  /* G */
    bgr[2] = argb_ptr[2];  /* R */
}

/**
 * @brief ARGB8888单像素转BGRA32（保留完整Alpha）
 * @param argb_ptr 源ARGB像素指针
 * @param bgra 输出4字节BGRA缓存
 */
static inline void argb8888_to_bgra32(const uint8_t *argb_ptr, uint8_t *bgra)
{
    bgra[0] = argb_ptr[0];  /* B */
    bgra[1] = argb_ptr[1];  /* G */
    bgra[2] = argb_ptr[2];  /* R */
    bgra[3] = argb_ptr[3];  /* A */
}

/**
 * @brief 完整写入BMP文件头+信息头到文件fd
 * @param fd 打开的可写文件句柄
 * @param width 图像宽
 * @param height 图像高
 * @param pixel_data_size 像素区总字节
 * @param bpp 目标位深度 BMP_BPP_24 / BMP_BPP_32
 * @return 0成功 -1失败
 */
int bmp_write_headers(int fd, int32_t width, int32_t height, uint32_t pixel_data_size, bmp_bpp_t bpp);

/**
 * @brief 将ARGB8888内存图像缓冲保存为指定位深度BMP
 * @param filepath 输出文件路径
 * @param buf ARGB8888图像缓存
 * @param w 宽 h 高
 * @param bpp 输出位深度 BMP_BPP_24 / BMP_BPP_32
 * @return 0成功 -1失败
 */
int bmp_save_from_argb8888(const char *filepath, const unsigned char *buf, int w, int h, bmp_bpp_t bpp);

/**
 * @brief 读取BMP文件，统一转为ARGB8888输出缓存
 *        支持24bit BGR24 / 32bit BGRA/BGRX输入
 * @param path BMP文件路径
 * @param w 输出宽度指针
 * @param h 输出高度指针
 * @param src_bpp 输出原始BMP的位深度（24/32）
 * @return 函数malloc的ARGB8888缓存，调用方free；失败返回NULL
 */
uint32_t *bmp_load_to_argb8888(const char *path, int *w, int *h, bmp_bpp_t *src_bpp);

#endif