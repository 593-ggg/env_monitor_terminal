#ifndef __YOLOV8_WRAP_H__
#define __YOLOV8_WRAP_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>
#include "utils/image_utils.h"

// 模型路径
#define YOLOV8_MODEL_PATH "./model/yolov8.rknn"
// 标签路径
#define YOLOV8_LABEL_PATH LABEL_NALE_TXT_PATH

#define YOLOV8_MAX_DET 128

typedef struct {
    int   cls_id;
    float prop;
    int   left;
    int   top;
    int   right;
    int   bottom;
} yolov8_detect_t;

typedef struct {
    int count;
    yolov8_detect_t items[YOLOV8_MAX_DET];
} yolov8_result_t;

/**
 * @brief 检查模型与标签文件是否都存在可读
 * @return true 两个文件都存在；false 缺失任意一个
 */
bool yolov8_file_all_exist(void);

/**
 * @brief 初始化 YOLOv8 模型 (包含后处理标签加载)
 * @param model_path RKNN 模型文件路径, 如 "./model/yolov8.rknn"
 * @return 0 成功, -1 失败
 */
int yolov8_init(const char *model_path);

/**
 * @brief 执行一次目标检测
 * @param img   输入图像, 必须是 IMAGE_FORMAT_RGB888
 * @param out   输出检测结果
 * @return 0 成功, -1 失败
 */
int yolov8_detect(const image_buffer_t *img, yolov8_result_t *out);

/**
 * @brief 释放 YOLOv8 模型和后处理资源
 */
void yolov8_deinit(void);

/**
 * @brief 查询 YOLOv8 模型是否已初始化
 * @return true 已初始化, false 未初始化
 */
bool yolov8_is_initialized(void);

/**
 * @brief 根据类别 ID 获取类别名称
 */
const char *yolov8_cls_name(int cls_id);

#ifdef __cplusplus
}
#endif

#endif
