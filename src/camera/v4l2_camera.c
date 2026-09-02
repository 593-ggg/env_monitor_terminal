#include "v4l2_camera.h"

#include <fcntl.h>
#include <linux/videodev2.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <stdbool.h>


/* ==================== V4L 缓冲配置 ==================== */
#define V4L_BUF_COUNT 3 /* 驱动侧缓冲队列深度, 建议设为 2~3 */

/* ==================== 模块内部状态 ==================== */
static int cam_fd = -1;
static void* v4l2_mmap_ptr[V4L_BUF_COUNT] = {NULL}; /* 每个缓冲的 mmap 地址 */
static size_t v4l2_mmap_len[V4L_BUF_COUNT] = {0};   /* 每个缓冲的实际长度 */

/* 双缓冲: 采集写空闲 buf, UI 读 active buf */
static cam_pixel_t* frame_buf[2] = {NULL, NULL};
static int active_idx = 0;
static bool new_frame_flag = false;
static pthread_mutex_t frame_lock;
static bool frame_lock_initialized = false;

/* YUYV 临时缓冲 (一帧原始数据) */
static uint8_t* yuyv_buf = NULL;

// 摄像头是否在工作
static bool camera_is_running = false;

/* ==================== 内部工具函数 ==================== */

static inline uint8_t clamp_u8(int v)
{
    return (uint8_t) ((v < 0) ? 0 : (v > 255) ? 255 : v);
}

/**
 * @brief YUYV → ARGB8888 转换, 写入 dst
 *        输出格式: 32‑bit ARGB, 内存字节序 B, G, R, 0xFF
 *        与 LV_COLOR_FORMAT_ARGB8888 和 RK_FORMAT_ARGB_8888 一致
 */
static void yuyv_to_argb8888(const uint8_t* yuyv, cam_pixel_t* dst)
{
    if (!yuyv || !dst)
        return;

    const size_t total = CAM_WIDTH * CAM_HEIGHT;
    for (size_t n = 0; n < total; n += 2)
    {
        uint8_t y0 = yuyv[0];
        uint8_t u  = yuyv[1];
        uint8_t y1 = yuyv[2];
        uint8_t v  = yuyv[3];
        yuyv += 4;

        int cb = (int)u - 128;
        int cr = (int)v - 128;

        int cr359    = 359 * cr;
        int cb88cr183= 88 * cb + 183 * cr;
        int cb454    = 454 * cb;

        int r0 = y0 + ((cr359 + 128) >> 8);
        int g0 = y0 - ((cb88cr183 + 128) >> 8);
        int b0 = y0 + ((cb454 + 128) >> 8);
        dst[0] = (0xFFu << 24) | ((uint32_t)clamp_u8(r0) << 16) | ((uint32_t)clamp_u8(g0) << 8) | clamp_u8(b0);

        int r1 = y1 + ((cr359 + 128) >> 8);
        int g1 = y1 - ((cb88cr183 + 128) >> 8);
        int b1 = y1 + ((cb454 + 128) >> 8);
        dst[1] = (0xFFu << 24) | ((uint32_t)clamp_u8(r1) << 16) | ((uint32_t)clamp_u8(g1) << 8) | clamp_u8(b1);

        dst += 2;
    }
}

/* ==================== 公共接口实现 ==================== */

bool camera_is_device_exist(void)
{
    int fd = open(CAM_DEV_PATH, O_RDWR);
    if (fd < 0)
        return false;

    struct v4l2_capability cap;
    bool ret = false;
    if (ioctl(fd, VIDIOC_QUERYCAP, &cap) == 0)
    {
        if (cap.capabilities & V4L2_CAP_VIDEO_CAPTURE)
        {
            ret = true;
        }
    }
    close(fd);
    return ret;
}

int camera_init(void)
{
    if (cam_fd >= 0)
    {
        printf("[CAM] camera_init: device already initialized\n");
        return 0;
    }

    // 初始化互斥锁
    if (!frame_lock_initialized)
    {
        pthread_mutex_init(&frame_lock, NULL);
        frame_lock_initialized = true;
    }

    // 1. 打开设备
    cam_fd = open(CAM_DEV_PATH, O_RDWR);
    if (cam_fd < 0)
    {
        perror("[CAM] open device failed");
        goto fail_lock;
    }

    // 2. 设置 YUYV 格式
    struct v4l2_format fmt;
    memset(&fmt, 0, sizeof(fmt));
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    fmt.fmt.pix.width = CAM_WIDTH;
    fmt.fmt.pix.height = CAM_HEIGHT;
    fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_YUYV;

    if (ioctl(cam_fd, VIDIOC_S_FMT, &fmt) < 0)
    {
        perror("[CAM] set format failed");
        goto fail_close;
    }

    // 3. 请求 V4L_BUF_COUNT 个 mmap 缓冲
    struct v4l2_requestbuffers req;
    memset(&req, 0, sizeof(req));
    req.count = V4L_BUF_COUNT;
    req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    req.memory = V4L2_MEMORY_MMAP;

    if (ioctl(cam_fd, VIDIOC_REQBUFS, &req) < 0)
    {
        perror("[CAM] reqbufs failed");
        goto fail_close;
    }

    // 4. 查询每个缓冲并 mmap, 然后入队
    for (int i = 0; i < V4L_BUF_COUNT; i++)
    {
        struct v4l2_buffer buf;
        memset(&buf, 0, sizeof(buf));
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index = i;

        if (ioctl(cam_fd, VIDIOC_QUERYBUF, &buf) < 0)
        {
            perror("[CAM] querybuf failed");
            goto fail_unmap;
        }

        v4l2_mmap_len[i] = buf.length;
        v4l2_mmap_ptr[i] = mmap(NULL, v4l2_mmap_len[i], PROT_READ, MAP_SHARED, cam_fd, buf.m.offset);
        if (v4l2_mmap_ptr[i] == MAP_FAILED)
        {
            perror("[CAM] mmap failed");
            v4l2_mmap_ptr[i] = NULL;
            goto fail_unmap;
        }

        if (ioctl(cam_fd, VIDIOC_QBUF, &buf) < 0)
        {
            perror("[CAM] qbuf failed");
            goto fail_unmap;
        }
    }

    // 5. 开始采集
    int type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (ioctl(cam_fd, VIDIOC_STREAMON, &type) < 0)
    {
        perror("[CAM] streamon failed");
        goto fail_unmap;
    }

    // 6. 动态分配帧缓冲 (双缓冲 + YUYV 临时缓冲)
    frame_buf[0] = (cam_pixel_t*) malloc(CAM_FRAME_SIZE);
    frame_buf[1] = (cam_pixel_t*) malloc(CAM_FRAME_SIZE);
    yuyv_buf = (uint8_t*) malloc(CAM_WIDTH * CAM_HEIGHT * 2);

    if (!frame_buf[0] || !frame_buf[1] || !yuyv_buf)
    {
        printf("[CAM] malloc frame buffer failed\n");
        camera_deinit();
        return -1;
    }

    active_idx = 0;
    new_frame_flag = false;
    camera_is_running = true;

    printf("[CAM] init success (%dx%d ARGB8888)\n", CAM_WIDTH, CAM_HEIGHT);
    return 0;

fail_unmap:
    for (int i = V4L_BUF_COUNT - 1; i >= 0; i--)
    {
        if (v4l2_mmap_ptr[i])
        {
            munmap(v4l2_mmap_ptr[i], v4l2_mmap_len[i]);
            v4l2_mmap_ptr[i] = NULL;
            v4l2_mmap_len[i] = 0;
        }
    }
fail_close:
    close(cam_fd);
    cam_fd = -1;
fail_lock:
    if (frame_lock_initialized)
    {
        pthread_mutex_destroy(&frame_lock);
        frame_lock_initialized = false;
    }
    return -1;
}

void camera_deinit(void)
{
    if (cam_fd < 0)
    {
        printf("[CAM] camera_deinit: device not initialized\n");
        return;
    }

    // 停止采集流
    int type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    ioctl(cam_fd, VIDIOC_STREAMOFF, &type);

    // 释放 mmap
    for (int i = 0; i < V4L_BUF_COUNT; i++)
    {
        if (v4l2_mmap_ptr[i])
        {
            munmap(v4l2_mmap_ptr[i], v4l2_mmap_len[i]);
            v4l2_mmap_ptr[i] = NULL;
            v4l2_mmap_len[i] = 0;
        }
    }

    // 关闭设备
    close(cam_fd);
    cam_fd = -1;

    camera_is_running = false;

    // 释放帧缓冲
    free(frame_buf[0]);
    frame_buf[0] = NULL;
    free(frame_buf[1]);
    frame_buf[1] = NULL;
    free(yuyv_buf);
    yuyv_buf = NULL;

    if (frame_lock_initialized)
    {
        pthread_mutex_destroy(&frame_lock);
        frame_lock_initialized = false;
    }
    printf("[CAM] deinit ok\n");
}

int camera_capture_frame(void)
{
    if (cam_fd < 0)
    {
        printf("[CAM] camera_capture_frame: device not initialized\n");
        return -1;
    }

    struct v4l2_buffer buf;
    memset(&buf, 0, sizeof(buf));
    buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buf.memory = V4L2_MEMORY_MMAP;

    if (ioctl(cam_fd, VIDIOC_DQBUF, &buf) < 0)
    {
        perror("[CAM] dqbuf failed");
        return -1;
    }

    memcpy(yuyv_buf, v4l2_mmap_ptr[buf.index], buf.bytesused);

    if (ioctl(cam_fd, VIDIOC_QBUF, &buf) < 0)
    {
        perror("[CAM] qbuf failed");
        return -1;
    }

    // 锁内拿到空闲buffer下标，立刻释放锁
    int write_idx;
    pthread_mutex_lock(&frame_lock);
    write_idx = !active_idx;
    pthread_mutex_unlock(&frame_lock);

    // YUV转换：锁外执行，不阻塞UI
    yuyv_to_argb8888(yuyv_buf, frame_buf[write_idx]);

    // 同一把锁：更新索引 + 设置新帧标记，保证顺序
    pthread_mutex_lock(&frame_lock);
    active_idx = write_idx;
    new_frame_flag = true;
    pthread_mutex_unlock(&frame_lock);

    return 0;
}

bool camera_has_new_frame(void)
{
    bool ret;
    pthread_mutex_lock(&frame_lock);
    ret = new_frame_flag;
    pthread_mutex_unlock(&frame_lock);
    return ret;
}

bool camera_is_run(void)
{
    return camera_is_running;
}

void camera_clear_new_frame(void)
{
    pthread_mutex_lock(&frame_lock);
    new_frame_flag = false;
    pthread_mutex_unlock(&frame_lock);
}

int camera_snapshot(cam_pixel_t* dst, int size)
{
    if (!dst || size < (int) CAM_FRAME_SIZE)
    {
        printf("[CAM] camera_snapshot: invalid dst or size\n");
        return -1;
    }

    pthread_mutex_lock(&frame_lock);
    int idx = active_idx;
    memcpy(dst, frame_buf[idx], CAM_FRAME_SIZE);
    pthread_mutex_unlock(&frame_lock);

    return 0;
}