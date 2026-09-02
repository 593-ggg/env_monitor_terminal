#include "lvgl/lvgl.h"
#include "thread.h"
#include "ui.h"
#include "ui_widget.h"
#include "config_manager.h"
#include <stdio.h>

// 全局键盘
lv_obj_t *kb = NULL;

void ui_main(void)
{
    // 加载配置文件（不存在则创建默认配置）
    AppConfig* cfg = config_get_global();
    int ret = config_load(NULL, cfg);
    if (ret != 0)
    {
        // 配置文件不存在，用默认值并创建文件
        config_set_defaults(cfg);
        config_save(NULL, cfg);
        printf("[Config] created default config: %s\n", CONFIG_FILE_PATH);
    }
    else
    {
        printf("[Config] loaded: port=%d fps=%d scale=%d conf=%d\n",
               cfg->server_port, cfg->camera_fps,
               cfg->camera_scale, cfg->ai_confidence);
    }

    // 创建全局键盘
    kb = ui_widget_create_keyboard(
       lv_layer_sys(), 
        1024, 200,                    
        LV_ALIGN_BOTTOM_MID, 
        0, 0
    );
    // 创建首页界面 (不启动定时器, 等用户登录成功后再启动)
    ui_home_scr_create();
    // 创建摄像头界面
    ui_camera_scr_create();
    // 创建ai识别界面
    ui_ai_scr_create();
    // 创建服务器管理界面
    ui_server_scr_create();
    // 创建硬件控制界面
    ui_device_scr_create();
    // 创建系统设置界面
    ui_settings_scr_create();
    ui_settings_set_config_ptr(cfg);

    // 创建环境参数监测界面
    ui_env_scr_create();

    // 创建开机动画界面 + 登录界面
    ui_boot_scr_create();
    ui_login_scr_create();

    // 加载开机动画屏 (2.5s 后自动跳转登录界面, 登录成功后进入首页)
    ui_boot_scr_load();
}

void ui_src_load(lv_obj_t *scr)
{
    lv_screen_load(scr);
}