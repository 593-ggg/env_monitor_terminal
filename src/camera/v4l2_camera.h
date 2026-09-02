#ifndef __V4L2_CAMERA_H__
#define __V4L2_CAMERA_H__

#include <stdint.h>
#include <stdbool.h>

/* ==================== 摄像头模块配置 ==================== */
#define CAM_DEV_PATH    "/dev/video9"
#define CAM_WIDTH       640
#define CAM_HEIGHT      480
#define CAM_BPP         4       /* ARGB8888: 每像素 4 字节 */
#define CAM_FRAME_SIZE  (CAM_WIDTH * CAM_HEIGHT * CAM_BPP)

/* 像素类型: ARGB8888 (32-bit, 与 LVGL/RGA 原生格式一致) */
typedef uint32_t cam_pixel_t;

/* ==================== 公共接口 ==================== */

/**
 * @brief 检测摄像头设备是否存在、可打开
 * @return true: /dev/videoX 存在且能正常open；false: 未接入/设备失效
 */
bool camera_is_device_exist(void);

/**
 * @brief 初始化摄像头: 打开 V4L2 设备, 分配帧缓冲
 * @return 0 成功, -1 失败
 */
int camera_init(void);

/**
 * @brief 释放摄像头所有资源: 关闭设备, 释放内存
 */
void camera_deinit(void);

/**
 * @brief 采集一帧并转换为 ARGB8888, 写入空闲缓冲并交换
 *        供采集线程循环调用, 内部阻塞等待 V4L2 帧就绪
 * @return 0 成功, -1 失败
 */
int camera_capture_frame(void);

/**
 * @brief 查询是否有新帧未消费
 */
bool camera_has_new_frame(void);

/**
 * @brief 查询摄像头是否在工作
 */
bool camera_is_run(void);



/**
 * @brief 清除新帧标志 (UI 刷新完成后调用)
 */
void camera_clear_new_frame(void);

/**
 * @brief 拷贝当前帧到外部缓冲 (用于抓拍等场景)
 * @param dst   目标缓冲
 * @param size  缓冲大小 (字节), 至少 CAM_FRAME_SIZE
 * @return 0 成功, -1 缓冲过小
 */
int camera_snapshot(cam_pixel_t *dst, int size);

#endif
