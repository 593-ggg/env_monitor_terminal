#ifndef __UI_H__
#define __UI_H__

#include "lvgl/lvgl.h"
#include "led.h"
#include "config_manager.h"

/* ================================================================
 *  ui_camera  摄像头画面控件
 * ================================================================ */

typedef struct
{
    lv_obj_t *cam_box;      /* 容器 */
    lv_obj_t *cam_img;      /* 图片对象 */
    lv_timer_t *timer;      /* 刷新定时器 */
} ui_camera_ctx_t;

/**
 * @brief 创建摄像头UI控件，只创建GUI，不初始化硬件，硬件由外部提前初始化完成
 * @param parent 父控件
 * @param align 对齐方式
 * @param x_ofs x偏移
 * @param y_ofs y偏移
 * @param width 容器宽
 * @param height 容器高
 * @return ctx 返回上下文，失败返回NULL
 */
ui_camera_ctx_t* ui_camera_create(lv_obj_t* parent,
                                   lv_align_t align,
                                   int32_t x_ofs, int32_t y_ofs,
                                   int32_t width, int32_t height);

// 启动/停止定时器 (外部调用)
void ui_camera_start_timers(ui_camera_ctx_t *ctx);
void ui_camera_stop_timers(ui_camera_ctx_t *ctx);

/**
 * @brief 销毁摄像头UI控件，删除定时器、GUI对象；不操作摄像头硬件
 * @param ctx ui_camera_create返回的上下文
 */
void ui_camera_delete(ui_camera_ctx_t* ctx);

/* ================================================================
 *  ui_gy39  GY39 传感器控件
 * ================================================================ */

typedef struct
{
    lv_obj_t *gy39_box;       /* 容器 */
    lv_obj_t *label_lux;      /* 光照数据 label */
    lv_obj_t *label_env;      /* 环境数据 label (温度/气压/湿度/海拔) */
    lv_timer_t *timer;        /* 刷新定时器 */
} ui_gy39_ctx_t;

/**
 * @brief 创建 GY39 传感器UI控件，只创建GUI，不初始化硬件，硬件由外部提前初始化完成
 * @param parent 父控件
 * @param align 对齐方式
 * @param x_ofs x偏移
 * @param y_ofs y偏移
 * @param width 容器宽
 * @param height 容器高
 * @return ctx 返回上下文，失败返回NULL
 */
ui_gy39_ctx_t* ui_gy39_create(lv_obj_t* parent,
                               lv_align_t align,
                               int32_t x_ofs, int32_t y_ofs,
                               int32_t width, int32_t height);

// 启动/停止定时器 (外部调用)
void ui_gy39_start_timers(ui_gy39_ctx_t *ctx);
void ui_gy39_stop_timers(ui_gy39_ctx_t *ctx);

/**
 * @brief 销毁 GY39 UI控件，删除定时器、GUI对象；不操作硬件
 * @param ctx ui_gy39_create返回的上下文
 */
void ui_gy39_delete(ui_gy39_ctx_t* ctx);

/* ================================================================
 *  ui_boot  开机动画界面
 * ================================================================ */

/**
 * @brief 创建开机动画界面 (spinner + 系统名 + 加载提示)
 * @return lv_obj_t* 开机屏幕容器对象
 */
lv_obj_t* ui_boot_scr_create(void);

/**
 * @brief 加载开机动画到屏幕, 并启动 2.5s 定时器
 * @note  定时器到期后自动跳转到登录界面
 */
void ui_boot_scr_load(void);


/* ================================================================
 *  ui_login  登录界面
 * ================================================================ */

/**
 * @brief 创建登录界面 (密码输入框 + 确认/退出按钮)
 * @return lv_obj_t* 登录屏幕容器对象
 */
lv_obj_t* ui_login_scr_create(void);

/**
 * @brief 加载登录界面到屏幕, 并清空上次输入状态
 */
void ui_login_scr_load(void);


/* ================================================================
 *  ui_main  总控制
 * ================================================================ */

 // 全局键盘
 extern lv_obj_t *kb;

 /**
 * @brief 创建全部界面
 * @return lv_obj_t* 全部界面容器对象
 */
void ui_main(void);

 /**
 * @brief 加载指定界面
 */
void ui_src_load(lv_obj_t *scr);

/* ================================================================
 *  ui_home  首页界面
 * ================================================================ */

/**
 * @brief 创建首页界面
 * @return lv_obj_t* 首页屏幕容器对象
 */
lv_obj_t* ui_home_scr_create(void);

/**
 * @brief 加载首页到屏幕
 */
void ui_home_scr_load(void);

/**
 * @brief 启动首页定时器 (时间/状态/底部栏刷新)
 */
void ui_home_start_timers(void);

/**
 * @brief 停止首页定时器
 */
void ui_home_stop_timers(void);


/* ================================================================
 *  ui_camera  摄像头界面
 * ================================================================ */

/**
 * @brief 创建摄像头界面
 * @return lv_obj_t* 摄像头屏幕容器对象
 */
lv_obj_t* ui_camera_scr_create(void);

/**
 * @brief 获取摄像头控件上下文
 * @return ui_camera_ctx_t* 摄像头控件上下文
 */
ui_camera_ctx_t *ui_camera_get_ctx(void);

/**
 * @brief 加载摄像头界面到屏幕
 */
void ui_camera_scr_load(void);

/* ================================================================
 *  ui_ai  ai界面
 * ================================================================ */

/**
 * @brief 创建ai界面界面
 * @return lv_obj_t* ai界面屏幕容器对象
 */

lv_obj_t* ui_ai_scr_create(void);

/**
 * @brief 加载ai界面到屏幕
 */
void ui_ai_scr_load(void);


/* ================================================================
 *  ui_server  服务器管理界面
 * ================================================================ */

/**
 * @brief 创建服务器管理界面
 * @return lv_obj_t* 服务器管理屏幕容器对象
 */
lv_obj_t* ui_server_scr_create(void);

/**
 * @brief 加载服务器管理界面到屏幕
 */
void ui_server_scr_load(void);

/**
 * @brief 启动服务器界面定时器（2秒自动刷新）
 */
void ui_server_start_timers(void);

/**
 * @brief 停止服务器界面所有定时器
 */
void ui_server_stop_timers(void);

/* ================================================================
 *  ui_device  设备管理界面
 * ================================================================ */

/**
 * @brief 创建设备管理界面
 * @return lv_obj_t* 设备管理屏幕容器对象
 */
lv_obj_t* ui_device_scr_create(void);

/**
 * @brief 加载设备管理界面到屏幕
 */
void ui_device_scr_load(void);

/**
 * @brief 设置设备LED状态
 * @param led_idx LED索引
 * @param is_on 是否亮
 */
void ui_device_set_led_stat(led_idx_t led, bool is_on);

/* ================================================================
 *  ui_settings  系统设置界面
 * ================================================================ */

/**
 * @brief 创建系统设置界面
 * @return lv_obj_t* 设置屏幕容器对象
 */
lv_obj_t* ui_settings_scr_create(void);

/**
 * @brief 加载系统设置界面到屏幕
 */
void ui_settings_scr_load(void);

/**
 * @brief 设置全局配置指针（用于读取/保存配置）
 * @param cfg 全局配置结构体指针
 */
void ui_settings_set_config_ptr(AppConfig* cfg);

/**
 * @brief 系统清理：停止所有线程，释放全部资源（fd/内存/设备句柄）
 * @note  可供登录界面、设置界面等多处调用，确保资源完全释放
 */
void system_cleanup(void);

/* ================================================================
 *  ui_env  环境参数监测界面
 * ================================================================ */

/**
 * @brief 创建环境参数监测界面
 * @return lv_obj_t* 环境参数屏幕容器对象
 */
lv_obj_t* ui_env_scr_create(void);

/**
 * @brief 加载环境参数界面到屏幕，并启动刷新定时器
 */
void ui_env_scr_load(void);


#endif  /* __UI_H__ */
