#ifndef __CONFIG_MANAGER_H__
#define __CONFIG_MANAGER_H__

#include "config.h"
#include <stdint.h>

// 配置文件路径（相对于工作目录）
#define CONFIG_FILE_PATH "./system_config.txt"

// 摄像头最大分辨率（宽x高）
#define CAMERA_MAX_WIDTH   772
#define CAMERA_MAX_HEIGHT  579

// 默认值
#define DEF_SERVER_PORT        8888
#define DEF_SERVER_DISPLAY_IP  "0.0.0.0"
#define DEF_SERVER_UPLOAD_DIR  "./uploads/"  // 服务端上传目录默认值
#define DEF_CAMERA_FPS         30
#define DEF_CAMERA_SCALE       100     // 百分比 (10-100)
#define DEF_AI_MODEL_PATH      "./model/yolov8.rknn"
#define DEF_AI_CONFIDENCE      75      // 百分比 (0-100)
#define DEF_LOGIN_PASSWORD     "123456"    // 登录密码默认值
#define DEF_PHOTO_PATH         "./photo"   // 拍照保存路径默认值
#define DEF_ENV_MONITOR_ENABLED  0         // 环境监测默认关闭（0=关闭, 1=开启）

// 登录密码长度范围
#define LOGIN_PWD_MIN_LEN   1
#define LOGIN_PWD_MAX_LEN   32

// AI 置信度范围（实际阈值 = confidence / 100.0）
#define AI_CONF_MIN  0
#define AI_CONF_MAX  100

// 摄像头帧率范围
#define CAMERA_FPS_MIN   5
#define CAMERA_FPS_MAX   30

// 端口范围
#define SERVER_PORT_MIN  1
#define SERVER_PORT_MAX  65535

// 摄像头分辨率缩放范围
#define CAMERA_SCALE_MIN  10
#define CAMERA_SCALE_MAX  100

// 全局应用配置结构体
typedef struct {
    // TCP 服务器
    int   server_port;               // 监听端口
    char  server_display_ip[64];     // 界面显示用IP（不影响绑定）
    char  server_upload_dir[256];    // 服务端上传目录（对外暴露的文件夹）

    // 摄像头
    int   camera_fps;                // 帧率 (5-30)
    int   camera_scale;              // 分辨率缩放百分比 (10-100)
    char  photo_save_path[256];      // 拍照保存路径

    // AI 推理
    char  ai_model_path[256];        // 模型文件路径
    int   ai_confidence;             // 置信度百分比 (0-100)

    // 系统安全
    char  login_password[64];        // 登录密码

    // 环境监测
    int   env_monitor_enabled;       // 环境监测开关（0=关闭, 1=开启）
} AppConfig;

/**
 * @brief 用默认值填充配置结构体
 * @param cfg 待填充的配置结构体
 */
void config_set_defaults(AppConfig* cfg);

/**
 * @brief 从配置文件加载配置
 * @param filepath 配置文件路径，传NULL使用 CONFIG_FILE_PATH
 * @param cfg 输出配置结构体
 * @return 0成功，-1文件不存在(已用默认值填充)，-2解析错误
 */
int config_load(const char* filepath, AppConfig* cfg);

/**
 * @brief 保存配置到文件
 * @param filepath 配置文件路径，传NULL使用 CONFIG_FILE_PATH
 * @param cfg 待保存的配置
 * @return 0成功，-1写入失败
 */
int config_save(const char* filepath, const AppConfig* cfg);

/**
 * @brief 获取摄像头实际分辨率（根据缩放百分比计算）
 * @param scale 缩放百分比 (10-100)
 * @param out_w 输出宽度
 * @param out_h 输出高度
 */
void config_get_camera_resolution(int scale, int* out_w, int* out_h);

/**
 * @brief 获取AI置信度浮点值（百分比转0.0-1.0）
 * @param percent 百分比 (0-100)
 * @return float 置信度值
 */
float config_get_ai_confidence(int percent);

/**
 * @brief 获取全局应用配置指针
 * @return AppConfig* 全局配置指针
 */
AppConfig* config_get_global(void);

#endif
