#include "bmp.h"
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <stdlib.h>

static ssize_t sys_write_full(int fd, const void *buf, size_t len)
{
    const uint8_t *ptr = (const uint8_t *)buf;
    size_t remain = len;
    while (remain > 0)
    {
        ssize_t w = write(fd, ptr, remain);
        if (w < 0)
        {
            perror("write failed");
            return -1;
        }
        remain -= w;
        ptr += w;
    }
    return (ssize_t)len;
}

int bmp_write_headers(int fd, int32_t width, int32_t height, uint32_t pixel_data_size, bmp_bpp_t bpp)
{
    bmp_file_header_t fh;
    bmp_info_header_t ih;
    /* 文件头 */
    fh.bfType = 0x4D42;  /* "BM" */
    fh.bfSize = IMG_BMP_HEADER + pixel_data_size;
    fh.bfReserved1 = 0;
    fh.bfReserved2 = 0;
    fh.bfOffBits = IMG_BMP_HEADER;
    if (sys_write_full(fd, &fh, sizeof(fh)) < 0)
        return -1;
    /* 信息头 */
    ih.biSize = sizeof(ih);
    ih.biWidth = width;
    ih.biHeight = height;  /* 正数: BMP从下到上存储 */
    ih.biPlanes = 1;
    ih.biBitCount = bpp;
    ih.biCompression = 0;
    ih.biSizeImage = pixel_data_size;
    ih.biXPelsPerMeter = 2835;  /* 72 DPI */
    ih.biYPelsPerMeter = 2835;
    ih.biClrUsed = 0;
    ih.biClrImportant = 0;
    if (sys_write_full(fd, &ih, sizeof(ih)) < 0)
        return -1;
    return 0;
}

int bmp_save_from_argb8888(const char *filepath, const unsigned char *buf, int w, int h, bmp_bpp_t bpp)
{
    if (filepath == NULL || buf == NULL || w <= 0 || h <= 0)
    {
        printf("[BMP] invalid param bmp_save_from_argb8888_ex\n");
        return -1;
    }
    if (bpp != BMP_BPP_24 && bpp != BMP_BPP_32)
    {
        printf("[BMP] unsupported bpp: %d, only 24/32 allowed\n", bpp);
        return -1;
    }
    uint32_t row_byte;
    uint32_t row_size;
    if (bpp == BMP_BPP_24)
    {
        row_byte = w * 3;
        row_size = (row_byte + 3) & ~3; /* 24位行4字节对齐 */
    }
    else
    {
        row_byte = w * 4;
        row_size = row_byte; /* 32位BGRA无需对齐 */
    }
    uint32_t pixel_data_size = row_size * h;
    int fd = open(filepath, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0)
    {
        printf("[BMP] open %s failed\n", filepath);
        return -1;
    }
    if (bmp_write_headers(fd, w, h, pixel_data_size, bpp) != 0)
    {
        printf("[BMP] write bmp header error\n");
        close(fd);
        return -1;
    }
    uint8_t *row_buf = (uint8_t *)malloc(row_size);
    if (row_buf == NULL)
    {
        printf("[BMP] malloc row_buf failed\n");
        close(fd);
        return -1;
    }
    uint32_t *argb_buf = (uint32_t *)buf;
    /* BMP 倒序行：从最后一行写到第一行 */
    for (int32_t y = h - 1; y >= 0; y--)
    {
        memset(row_buf, 0, row_size);
        for (int32_t x = 0; x < w; x++)
        {
            const uint8_t *argb_ptr = (const uint8_t *)&argb_buf[y * w + x];
            if (bpp == BMP_BPP_24)
            {
                uint8_t *bgr = &row_buf[x * 3];
                argb8888_to_bgr24(argb_ptr, bgr);
            }
            else /* 32bit BGRA */
            {
                uint8_t *bgra = &row_buf[x * 4];
                argb8888_to_bgra32(argb_ptr, bgra);
            }
        }
        if (sys_write_full(fd, row_buf, row_size) < 0)
        {
            printf("[BMP] write row data error\n");
            free(row_buf);
            close(fd);
            return -1;
        }
    }
    close(fd);
    free(row_buf);
    printf("[BMP] saved: %s (%dx%d, %dbit, %u bytes)\n",
           filepath, w, h, bpp, IMG_BMP_HEADER + pixel_data_size);
    return 0;
}

uint32_t *bmp_load_to_argb8888(const char *path, int *w, int *h, bmp_bpp_t *src_bpp)
{
    if (path == NULL || w == NULL || h == NULL || src_bpp == NULL)
        return NULL;
    int fd = open(path, O_RDONLY);
    if (fd < 0)
    {
        printf("[BMP] open bmp %s failed\n", path);
        return NULL;
    }
    /* 1. 读取文件头 */
    bmp_file_header_t fh;
    if (read(fd, &fh, sizeof(fh)) != sizeof(fh))
    {
        printf("[BMP] read bmp file header failed\n");
        close(fd);
        return NULL;
    }
    if (fh.bfType != 0x4D42)
    {
        printf("[BMP] not a valid BMP file (bfType=0x%04X)\n", fh.bfType);
        close(fd);
        return NULL;
    }
    /* 2. 读取信息头 */
    bmp_info_header_t ih;
    if (read(fd, &ih, sizeof(ih)) != sizeof(ih))
    {
        printf("[BMP] read bmp info header failed\n");
        close(fd);
        return NULL;
    }
    if (ih.biBitCount != 24 && ih.biBitCount != 32)
    {
        printf("[BMP] unsupported bit count: %d (only 24/32 supported)\n", ih.biBitCount);
        close(fd);
        return NULL;
    }
    if (ih.biSize < 40)
    {
        printf("[BMP] invalid info header size: %u\n", ih.biSize);
        close(fd);
        return NULL;
    }
    if (ih.biCompression != 0)
    {
        printf("[BMP] compressed BMP unsupported\n");
        close(fd);
        return NULL;
    }
    int32_t width = ih.biWidth;
    int32_t height = ih.biHeight;
    bool top_to_bottom = false;
    if (height < 0)
    {
        height = -height;
        top_to_bottom = true;
    }
    if (width <= 0 || height <= 0)
    {
        printf("[BMP] invalid size %dx%d\n", width, height);
        close(fd);
        return NULL;
    }
    // 输出原图位深度
    *src_bpp = (ih.biBitCount == 32) ? BMP_BPP_32 : BMP_BPP_24;
    int32_t bpp = ih.biBitCount;
    /* 3. 计算行大小并读取像素数据 */
    uint32_t src_row_size;
    if (bpp == 24)
        src_row_size = (width * 3 + 3) & ~3;
    else
        src_row_size = width * 4;
    uint32_t pixel_data_size = src_row_size * height;
    if (lseek(fd, fh.bfOffBits, SEEK_SET) == (off_t)-1)
    {
        printf("[BMP] lseek pixel data failed\n");
        close(fd);
        return NULL;
    }
    uint8_t *src_data = (uint8_t *)malloc(pixel_data_size);
    if (src_data == NULL)
    {
        printf("[BMP] malloc pixel data failed\n");
        close(fd);
        return NULL;
    }
    if (read(fd, src_data, pixel_data_size) != (ssize_t)pixel_data_size)
    {
        printf("[BMP] read pixel data incomplete\n");
        free(src_data);
        close(fd);
        return NULL;
    }
    close(fd);
    /* 4. 分配输出ARGB缓存 */
    uint32_t *dst = (uint32_t *)malloc(width * height * sizeof(uint32_t));
    if (dst == NULL)
    {
        printf("[BMP] malloc argb output buf failed\n");
        free(src_data);
        return NULL;
    }
    /* 5. 像素格式转换为统一ARGB8888 */
    for (int32_t y = 0; y < height; y++)
    {
        int32_t src_y = top_to_bottom ? y : (height - 1 - y);
        uint8_t *src_row = src_data + src_y * src_row_size;
        uint32_t *dst_row = dst + y * width;
        for (int32_t x = 0; x < width; x++)
        {
            if (bpp == 24)
            {
                uint8_t b = src_row[x * 3 + 0];
                uint8_t g = src_row[x * 3 + 1];
                uint8_t r = src_row[x * 3 + 2];
                dst_row[x] = (0xFFu << 24) | ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
            }
            else /* 32-bit BGRA */
            {
                uint8_t b = src_row[x * 4 + 0];
                uint8_t g = src_row[x * 4 + 1];
                uint8_t r = src_row[x * 4 + 2];
                uint8_t a = src_row[x * 4 + 3];
                dst_row[x] = ((uint32_t)a << 24) | ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
            }
        }
    }
    free(src_data);
    *w = width;
    *h = height;
    printf("[BMP] loaded %dx%d %dbit -> ARGB8888\n", width, height, bpp);
    return dst;
}