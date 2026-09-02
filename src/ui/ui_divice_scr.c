#include "ui.h"
#include "ui_widget.h"
#include "config.h"
#include "my_font.h"
#include "led.h"
#include "beep.h"
#include <stdio.h>
#include <string.h>

// 设备界面全局屏幕对象
static lv_obj_t *device_scr = NULL;

// LED状态标签
static lv_obj_t *lbl_led1_stat = NULL;
static lv_obj_t *lbl_led2_stat = NULL;
static lv_obj_t *lbl_led3_stat = NULL;
static lv_obj_t *lbl_led4_stat = NULL;

// LED按钮句柄
static lv_obj_t *btn_led1 = NULL;
static lv_obj_t *btn_led2 = NULL;
static lv_obj_t *btn_led3 = NULL;
static lv_obj_t *btn_led4 = NULL;

// 功能按钮
static lv_obj_t *btn_led_all_on = NULL;
static lv_obj_t *btn_led_all_off = NULL;
static lv_obj_t *btn_led_flow = NULL;

// 蜂鸣器按钮
static lv_obj_t *btn_beep = NULL;

// 流水灯定时器
static lv_timer_t *flow_timer = NULL;
// 流水灯当前点亮索引
static uint8_t flow_idx = 0;
// 流水灯运行标记
static bool flow_running = false;

/**
 * @brief 同步全部LED硬件状态到UI标签
 */
static void ui_device_refresh_all_led(void)
{
    uint8_t sta[LED_MAX];
    device_led_get_all_state(sta);
    ui_device_set_led_stat(LED_0, sta[LED_0]);
    ui_device_set_led_stat(LED_1, sta[LED_1]);
    ui_device_set_led_stat(LED_2, sta[LED_2]);
    ui_device_set_led_stat(LED_3, sta[LED_3]);
}

/**
 * 流水灯定时逻辑：单灯亮，其余灭，循环0→1→2→3→0
 */
static void led_flow_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    if (!flow_running)
    {
        if (flow_timer)
        {
            lv_timer_del(flow_timer);
            flow_timer = NULL;
        }
        return;
    }
    // 全部熄灭
    device_led_all_off();
    // 点亮当前序号LED
    device_led_on((led_idx_t)flow_idx);
    // 更新UI全部状态标签
    ui_device_refresh_all_led();
    // 索引自增循环
    flow_idx = (flow_idx + 1) % LED_MAX;
}

// 返回首页、LED、流水灯、BEEP完整业务回调
static void ui_device_btn_cb(lv_event_t *e)
{
    uintptr_t tag = (uintptr_t)lv_event_get_user_data(e);
    switch (tag)
    {
        case 0:
            // 返回首页
            flow_running = false;
            if (flow_timer)
            {
                lv_timer_del(flow_timer);
                flow_timer = NULL;
            }
            device_led_all_off();
            ui_device_refresh_all_led();
            ui_home_scr_load();
            break;
        case 1:
            // LED0 翻转
            if(device_led_get_state(LED_0))
                device_led_off(LED_0);
            else
                device_led_on(LED_0);
            ui_device_set_led_stat(LED_0, device_led_get_state(LED_0));
            break;
        case 2:
            // LED1 翻转
            if(device_led_get_state(LED_1))
                device_led_off(LED_1);
            else
                device_led_on(LED_1);
            ui_device_set_led_stat(LED_1, device_led_get_state(LED_1));
            break;
        case 3:
            // LED2 翻转
            if(device_led_get_state(LED_2))
                device_led_off(LED_2);
            else
                device_led_on(LED_2);
            ui_device_set_led_stat(LED_2, device_led_get_state(LED_2));
            break;
        case 4:
            // LED3 翻转
            if(device_led_get_state(LED_3))
                device_led_off(LED_3);
            else
                device_led_on(LED_3);
            ui_device_set_led_stat(LED_3, device_led_get_state(LED_3));
            break;
        case 5:
            // 全部点亮
            device_led_all_on();
            ui_device_refresh_all_led();
            break;
        case 6:
            // 全部熄灭
            device_led_all_off();
            ui_device_refresh_all_led();
            break;
        case 7:
            // 切换流水灯运行状态
            flow_running = !flow_running;
            if (flow_running)
            {
                // 启动定时器，间隔300ms可自行修改
                if (flow_timer == NULL)
                {
                    flow_timer = lv_timer_create(led_flow_timer_cb, 300, NULL);
                }
                flow_idx = 0; // 从头开始流水
            }
            else
            {
                // 停止流水，全部熄灭LED
                if (flow_timer)
                {
                    lv_timer_del(flow_timer);
                    flow_timer = NULL;
                }
                device_led_all_off();
                ui_device_refresh_all_led();
            }
            break;
        case 10:
            // BEEP 按住持续发声
            device_beep_on();
            break;
        case 11:
            // BEEP 松开关闭蜂鸣
            device_beep_off();
            break;
        default:
            break;
    }
}

lv_obj_t* ui_device_scr_create(void)
{
    if (device_scr != NULL)
        return device_scr;

    device_scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(device_scr, COLOR_GRAY_LIGHT, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(device_scr, LV_OPA_COVER, LV_PART_MAIN);

    // 顶部标题栏
    lv_obj_t *title_box = ui_widget_create_box(
        device_scr,
        984, 80,
        LV_ALIGN_TOP_MID, 0, 12,
        COLOR_BLUE_DARK, 12, false
    );
    ui_widget_create_label(
        title_box, "外设控制面板  LED  BEEP",
        LV_ALIGN_LEFT_MID, 20, 0,
        FONT_TITLE, lv_color_white()
    );
    ui_widget_create_btn(
        title_box, "返回首页",
        160, 50,
        COLOR_GREEN_LIGHT, COLOR_GREEN_DARK,
        LV_ALIGN_RIGHT_MID, -12, 0,
        0, ui_device_btn_cb
    );

    // LED总面板
    lv_obj_t *led_box = ui_widget_create_box(
        device_scr,
        480, 480,
        LV_ALIGN_TOP_LEFT, 12, 110,
        COLOR_GRAY_DARK, 14, true
    );
    ui_widget_create_label(
        led_box, "四路LED开关（单击切换亮灭）",
        LV_ALIGN_TOP_MID, 0, 8,
        FONT_SUBTITLE, lv_color_white()
    );

    // LED0 行
    lv_obj_t *row1 = ui_widget_create_box(
        led_box, 
        LV_PCT(100), 70, 
        LV_ALIGN_TOP_LEFT, 
        0, 50, 
        COLOR_GRAY_LIGHTEST, 
        8, 
        false
    );
    ui_widget_create_label(
        row1, 
        "LED1", 
        LV_ALIGN_LEFT_MID, 
        10, 0, 
        FONT_BODY, 
        COLOR_TEXT_DARK
    );
    lbl_led1_stat = ui_widget_create_label(
        row1, 
        "已关闭", 
        LV_ALIGN_LEFT_MID, 
        100, 0, 
        FONT_BODY, 
        COLOR_RED_NORMAL
    );
    btn_led1 = ui_widget_create_btn(
        row1, 
        "切换", 
        100, 42, 
        COLOR_GREEN_LIGHT, COLOR_GREEN_DARK, 
        LV_ALIGN_RIGHT_MID, 
        -10, 0, 
        1, 
        ui_device_btn_cb
    );

    // LED1 行
    lv_obj_t *row2 = ui_widget_create_box(
        led_box, 
        LV_PCT(100), 70, 
        LV_ALIGN_TOP_LEFT, 
        0, 130, 
        COLOR_GRAY_LIGHTEST, 
        8, 
        false
    );
    ui_widget_create_label(
        row2, 
        "LED2", 
        LV_ALIGN_LEFT_MID, 
        10, 0, 
        FONT_BODY, 
        COLOR_TEXT_DARK
    );
    lbl_led2_stat = ui_widget_create_label(
        row2, 
        "已关闭", 
        LV_ALIGN_LEFT_MID, 
        100, 0, 
        FONT_BODY, 
        COLOR_RED_NORMAL
    );
    btn_led2 = ui_widget_create_btn(
        row2, 
        "切换", 
        100, 42, 
        COLOR_GREEN_LIGHT, COLOR_GREEN_DARK, 
        LV_ALIGN_RIGHT_MID, 
        -10, 0, 
        2, 
        ui_device_btn_cb
    );

    // LED2 行
    lv_obj_t *row3 = ui_widget_create_box(
        led_box, 
        LV_PCT(100), 70, 
        LV_ALIGN_TOP_LEFT, 
        0, 210, 
        COLOR_GRAY_LIGHTEST, 
        8, 
        false
    );
    ui_widget_create_label(
        row3, 
        "LED3", 
        LV_ALIGN_LEFT_MID, 
        10, 0, 
        FONT_BODY, 
        COLOR_TEXT_DARK
    );
    lbl_led3_stat = ui_widget_create_label(
        row3, 
        "已关闭", 
        LV_ALIGN_LEFT_MID, 
        100, 0, 
        FONT_BODY, 
        COLOR_RED_NORMAL
    );
    btn_led3 = ui_widget_create_btn(
        row3, 
        "切换", 
        100, 42, 
        COLOR_GREEN_LIGHT, COLOR_GREEN_DARK, 
        LV_ALIGN_RIGHT_MID, 
        -10, 0, 
        3, 
        ui_device_btn_cb
    );

    // LED3 行
    lv_obj_t *row4 = ui_widget_create_box(
        led_box, 
        LV_PCT(100), 70, 
        LV_ALIGN_TOP_LEFT, 
        0, 290, 
        COLOR_GRAY_LIGHTEST, 
        8, 
        false
    );
    ui_widget_create_label(
        row4, 
        "LED4", 
        LV_ALIGN_LEFT_MID, 
        10, 0, 
        FONT_BODY, 
        COLOR_TEXT_DARK
    );
    lbl_led4_stat = ui_widget_create_label(
        row4, 
        "已关闭", 
        LV_ALIGN_LEFT_MID, 
        100, 0, 
        FONT_BODY, 
        COLOR_RED_NORMAL
    );
    btn_led4 = ui_widget_create_btn(
        row4, 
        "切换", 
        100, 42, 
        COLOR_GREEN_LIGHT, COLOR_GREEN_DARK, 
        LV_ALIGN_RIGHT_MID, 
        -10, 0, 
        4, 
        ui_device_btn_cb
    );

    // 底部功能按钮行
    lv_obj_t *func_row = ui_widget_create_box(
        led_box, 
        LV_PCT(100), 70, 
        LV_ALIGN_TOP_LEFT, 
        0, 370, 
        COLOR_GRAY_LIGHTEST, 
        8, 
        false
    );
    btn_led_all_on  = ui_widget_create_btn(
        func_row, 
        "全部点亮", 
        130, 42, 
        COLOR_BLUE_LIGHT, COLOR_BLUE_DARK, 
        LV_ALIGN_LEFT_MID, 
        10, 0, 
        5, 
        ui_device_btn_cb);
    btn_led_all_off = ui_widget_create_btn(
        func_row, 
        "全部熄灭", 
        130, 42, 
        COLOR_RED_LIGHT, COLOR_RED_DARK, 
        LV_ALIGN_LEFT_MID, 
        150, 0, 
        6, 
        ui_device_btn_cb);
    btn_led_flow    = ui_widget_create_btn(
        func_row, 
        "流水灯启停", 
        130, 42, 
        COLOR_PURPLE_NORMAL, COLOR_PURPLE_DARK, 
        LV_ALIGN_RIGHT_MID, 
        -10, 0, 
        7, 
        ui_device_btn_cb);

    // BEEP面板
    lv_obj_t *beep_box = ui_widget_create_box(
        device_scr,
        480, 480, 
        LV_ALIGN_TOP_RIGHT, 
        -12, 110,
        COLOR_GRAY_DARK, 
        14, 
        false
    );
    ui_widget_create_label(
        beep_box, 
        "蜂鸣器BEEP（按住发声，松开停止）", 
        LV_ALIGN_TOP_MID, 
        0, 8,
        FONT_SUBTITLE, lv_color_white()
    );
    btn_beep = ui_widget_create_btn(
        beep_box, 
        "长按鸣笛", 
        320, 200, 
        COLOR_ORANGE_NORMAL, COLOR_RED_DARK, 
        LV_ALIGN_CENTER, 
        0, 0, 
        99, 
        ui_device_btn_cb
    );
    lv_obj_add_event_cb(btn_beep, ui_device_btn_cb, LV_EVENT_PRESSING, (void*)10);
    lv_obj_add_event_cb(btn_beep, ui_device_btn_cb, LV_EVENT_RELEASED, (void*)11);

    // 底部说明
    ui_widget_create_label(
        device_scr, 
        "操作说明：LED单击切换；一键全亮/全灭；流水灯；蜂鸣器长按发声", 
        LV_ALIGN_BOTTOM_MID, 
        0, -16,
        FONT_BODY, 
        COLOR_TEXT_DARK
    );

    // 创建完成同步当前硬件LED状态到界面
    ui_device_refresh_all_led();

    return device_scr;
}

/**
 * @brief 更新单路LEDUI文字与颜色
 * @param led_idx LED_0 ~ LED_3
 * @param is_on 1亮 0灭
 */
void ui_device_set_led_stat(led_idx_t led_idx, bool is_on)
{
    lv_obj_t *target_lbl = NULL;
    switch(led_idx)
    {
        case LED_0: 
            target_lbl = lbl_led1_stat; 
            break;
        case LED_1: 
            target_lbl = lbl_led2_stat; 
            break;
        case LED_2: 
            target_lbl = lbl_led3_stat; 
            break;
        case LED_3: 
            target_lbl = lbl_led4_stat; 
            break;
        default: return;
    }
    if (is_on)
    {
        lv_label_set_text(target_lbl, "已开启");
        lv_obj_set_style_text_color(target_lbl, COLOR_GREEN_NORMAL, LV_PART_MAIN);
    }
    else
    {
        lv_label_set_text(target_lbl, "已关闭");
        lv_obj_set_style_text_color(target_lbl, COLOR_RED_NORMAL, LV_PART_MAIN);
    }
}

// 加载外设界面
void ui_device_scr_load(void)
{
    if (device_scr)
    {
        lv_screen_load_anim(
            device_scr,
            LV_SCR_LOAD_ANIM_MOVE_LEFT,
            300, 0, false
        );
        // 切页同步最新LED状态
        ui_device_refresh_all_led();
    }
}