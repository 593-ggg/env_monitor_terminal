#include "lvgl/lvgl.h"
#include "src/misc/lv_area.h"
#include "ui_widget.h"
#include "ui.h"
#include "config.h"
#include "config_manager.h"
#include "thread.h"
#include "image.h"
#include "my_font.h"
#include <stdio.h>

// 摄像头界面
static lv_obj_t *camera_scr = NULL;

// 摄像头控件
static ui_camera_ctx_t *camera_ctx = NULL;

// ai识别按钮
static lv_obj_t *btn_ai = NULL;

// 摄像头开关按钮
static lv_obj_t *btn_camera = NULL;

// 摄像头是否已开启 (进入界面默认 false, 需手动点按钮开启)
static bool s_camera_enabled = false;

// AI识别结果显示
static lv_obj_t *ai_result_box = NULL;
static lv_obj_t *ai_result_label = NULL;
static lv_timer_t *ai_result_timer = NULL;

// 摄像头按钮回调
static void ui_camera_btn_cb(lv_event_t *e)
{
    uintptr_t tag = (uintptr_t) lv_event_get_user_data(e);
    switch (tag)
    {
        case 0:
            // 返回首页: 离开前自动关闭摄像头 (如果在运行)
            // 1. 停止AI结果定时器
            if (ai_result_timer)
            {
                lv_timer_del(ai_result_timer);
                ai_result_timer = NULL;
            }
            // 2. 停止AI线程 (如果在运行)
            if (ai_thread_is_running())
            {
                ai_thread_stop();
                lv_obj_set_style_bg_color(btn_ai, COLOR_BLUE_LIGHT, LV_STATE_DEFAULT);
            }
            // 3. 停止摄像头 (如果已开启)
            if (s_camera_enabled)
            {
                cam_thread_stop();
                ui_camera_stop_timers(camera_ctx);
                s_camera_enabled = false;
                lv_obj_set_style_bg_color(btn_camera, COLOR_GREEN_LIGHT, LV_STATE_DEFAULT);
            }
            ui_home_scr_load();
            break;

        case 1:
            // 拍照: 摄像头未开启时无效
            if (!s_camera_enabled) return;
            {
                AppConfig *cfg = config_get_global();
                const char *path = (cfg && cfg->photo_save_path[0]) ? cfg->photo_save_path : DEF_PHOTO_PATH;
                image_snapshot_bmp(path, BMP_BPP_32);
            }
            break;

        case 2:
            // AI识别: 摄像头未开启时无效
            if (!s_camera_enabled) return;
            if (!ai_thread_is_running())
            {
                ai_thread_start();
                lv_obj_set_style_bg_color(btn_ai, COLOR_BLUE_DARK, LV_STATE_DEFAULT);
            }
            else
            {
                ai_thread_stop();
                lv_obj_set_style_bg_color(btn_ai, COLOR_BLUE_LIGHT, LV_STATE_DEFAULT);
            }
            break;

        case 3:
            // 开启/关闭摄像头
            if (!s_camera_enabled)
            {
                // 开启摄像头
                cam_thread_start();
                ui_camera_start_timers(camera_ctx);
                s_camera_enabled = true;
                lv_obj_set_style_bg_color(btn_camera, COLOR_GREEN_DARK, LV_STATE_DEFAULT);
            }
            else
            {
                // 关闭前先停止AI线程 (如果在运行)
                if (ai_thread_is_running())
                {
                    ai_thread_stop();
                    lv_obj_set_style_bg_color(btn_ai, COLOR_BLUE_LIGHT, LV_STATE_DEFAULT);
                }
                cam_thread_stop();
                ui_camera_stop_timers(camera_ctx);
                s_camera_enabled = false;
                lv_obj_set_style_bg_color(btn_camera, COLOR_GREEN_LIGHT, LV_STATE_DEFAULT);
            }
            break;

        default: break;
    }

}

// AI识别结果定时器回调
static void ai_result_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    if (ai_result_label == NULL) return;

    // 摄像头未开启时显示提示
    if (!s_camera_enabled)
    {
        lv_label_set_text(ai_result_label, "摄像头未开启");
        return;
    }

    if (!ai_thread_is_running())
    {
        lv_label_set_text(ai_result_label, "AI未启动");
        return;
    }

    yolov8_result_t result;
    memset(&result, 0, sizeof(result));
    if (ai_thread_get_result(&result) != 0 || result.count <= 0)
    {
        lv_label_set_text(ai_result_label, "AI已启动\n未检测到目标");
        return;
    }

    // 格式化显示前15个检测结果
    static char buf[512];
    int pos = snprintf(buf, sizeof(buf), "检测到 %d 个目标\n\n", result.count);
    int max_show = result.count < 15 ? result.count : 15;
    for (int i = 0; i < max_show; i++)
    {
        const char *name = yolov8_cls_name(result.items[i].cls_id);
        if (!name) name = "未知";
        pos += snprintf(buf + pos, sizeof(buf) - pos,
                        "%s  %.0f%%\n", name, result.items[i].prop * 100);
        if (pos >= (int)sizeof(buf) - 20) break;
    }
    lv_label_set_text(ai_result_label, buf);
}

lv_obj_t* ui_camera_scr_create(void)
{
    if (camera_scr != NULL)
        return camera_scr;

    camera_scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(camera_scr, COLOR_GRAY_LIGHT, 0);
    lv_obj_set_style_bg_opa(camera_scr, LV_OPA_COVER, 0);

    // 从配置读取摄像头分辨率
    int cam_w = CAMERA_MAX_WIDTH;
    int cam_h = CAMERA_MAX_HEIGHT;
    AppConfig* cfg = config_get_global();
    if (cfg)
        config_get_camera_resolution(cfg->camera_scale, &cam_w, &cam_h);

    // 创建摄像头控件
    camera_ctx = ui_camera_create(
        camera_scr,
        LV_ALIGN_TOP_LEFT,
        10, 10,
        cam_w, cam_h
    );

    // 创建返回按钮
    ui_widget_create_btn(
        camera_scr, 
        "返回", 
        200, 50, 
        COLOR_BLUE_LIGHT, COLOR_BLUE_DARK, 
        LV_ALIGN_TOP_LEFT, 
        800, 10, 
        0, 
        ui_camera_btn_cb
    );

    // 创建摄像头开关按钮 (绿色: 浅色=关闭, 深色=开启)
    btn_camera = ui_widget_create_btn(
        camera_scr, 
        "开启摄像头", 
        200, 50, 
        COLOR_GREEN_LIGHT, COLOR_GREEN_DARK, 
        LV_ALIGN_TOP_LEFT, 
        800, 80, 
        3, 
        ui_camera_btn_cb
    );

    // 创建拍照按钮
    ui_widget_create_btn(
        camera_scr, 
        "拍照", 
        200, 50, 
        COLOR_BLUE_LIGHT, COLOR_BLUE_DARK, 
        LV_ALIGN_TOP_LEFT, 
        800, 150, 
        1, 
        ui_camera_btn_cb
    );

    // 创建ai识别按钮
    btn_ai = ui_widget_create_btn(
        camera_scr, 
        "ai识别", 
        200, 50, 
        COLOR_BLUE_LIGHT, COLOR_BLUE_DARK, 
        LV_ALIGN_TOP_LEFT, 
        800, 220, 
        2, 
        ui_camera_btn_cb
    );

    // 创建AI识别结果框 (减小高度: 原 365 -> 295, 给摄像头开关按钮腾出空间)
    ai_result_box = ui_widget_create_box(
        camera_scr, 200, 295, LV_ALIGN_TOP_LEFT, 800, 285,
        COLOR_GRAY_DARK, 5, true
    );

    // 结果框标题
    ui_widget_create_label(ai_result_box, "识别结果", LV_ALIGN_TOP_MID, 0, 5,
                           FONT_BODY, COLOR_TEXT_LIGHT);

    // 结果内容标签
    ai_result_label = ui_widget_create_label(
        ai_result_box, "摄像头未开启", LV_ALIGN_TOP_LEFT, 8, 32,
        FONT_BODY, lv_color_white()
    );
    lv_label_set_long_mode(ai_result_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(ai_result_label, 185);

    return camera_scr;
}

ui_camera_ctx_t *ui_camera_get_ctx(void)
{
    return camera_ctx;
}

void ui_camera_scr_load(void)
{
    if (camera_scr) {
         lv_screen_load_anim(
            camera_scr, 
            LV_SCR_LOAD_ANIM_MOVE_LEFT, 
            300, 
            0, 
            false
        );
        // 进入界面默认不开启摄像头, 等待用户点击"开启摄像头"按钮
        s_camera_enabled = false;
        // 重置按钮颜色到未激活状态
        if (btn_camera)
            lv_obj_set_style_bg_color(btn_camera, COLOR_GREEN_LIGHT, LV_STATE_DEFAULT);
        if (btn_ai)
            lv_obj_set_style_bg_color(btn_ai, COLOR_BLUE_LIGHT, LV_STATE_DEFAULT);
        // 重置结果框提示
        if (ai_result_label)
            lv_label_set_text(ai_result_label, "摄像头未开启");
        // 启动AI结果定时器 (仅用于刷新状态显示, 不会真正调用AI推理)
        if (ai_result_timer == NULL)
        {
            ai_result_timer = lv_timer_create(ai_result_timer_cb, 200, NULL);
        }
    }
}


