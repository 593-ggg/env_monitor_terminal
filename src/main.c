#include "device/led.h"
#include "lvgl/lvgl.h"
#include "ui_widget.h"
#include "ui.h"
#include "led.h"
#include "beep.h"
#include <stdio.h>
#include <unistd.h>
#include "thread.h"

int main(void)
{
    // LVGL 基础初始化
    lv_init();

    // Framebuffer 显示驱动 /dev/fb0
    lv_display_t* disp = lv_linux_fbdev_create();
    lv_linux_fbdev_set_file(disp, "/dev/fb0");

    // 触摸屏输入（event6 根据你硬件修改）
    lv_indev_t* indev = lv_evdev_create(LV_INDEV_TYPE_POINTER, "/dev/input/event6");

    // 初始化全局UI控件统一样式（必须提前调用）     
    ui_widget_style_init();

    // 初始化LED和蜂鸣器
    device_led_init_all();
    device_beep_init();

    // 创建全部界面 (内部会加载开机动画屏, 2.5s 后跳转登录界面)
    ui_main();

    // LVGL 主循环
    while (1)
    {
        lv_timer_handler();
        usleep(5000);
    }

exit_lv:
    lv_deinit();
    return 0;

}