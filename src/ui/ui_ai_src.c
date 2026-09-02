#include "stdbool.h"
#include "ui.h"
#include "ui_widget.h"
#include "config.h"
#include "image.h"
#include "ima_utils.h"
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <dirent.h>

// ai识别输出路径
#define AI_OUTPUT_PATH "./image/ai_output.bmp"

// 容器宽高
#define AI_IMG_W 772
#define AI_IMG_H 579

// 拍照保存路径
static char *photo_path = NULL;

// ai识别界面
static lv_obj_t *ai_scr = NULL;

// ai img 控件
static lv_obj_t *ai_img = NULL;

// 显示图像缓存，ARGB8888格式
static uint32_t *ai_img_buf = NULL;
// 是否加载图片
static bool ai_img_loaded = false;
// 是否进行了ai识别
static bool ai_recognized = false;

// 图片加载路径
static char ai_load_path[128] = {0};

// 当前扫描的目录
static char ai_cur_dir[128] = {0};

// 列表控件指针
static lv_obj_t *ai_list = NULL;

// 自定义路径弹窗控件
static lv_obj_t *ai_popup_win = NULL;
static lv_obj_t *ai_popup_ta = NULL;

/**
 * @brief 加载BMP，等比例缩放居中绘制到全局ai_img_buf画布
 * 原图内存全部在函数内管理，函数退出自动释放，无全局原图缓存
 * @param bmp_path 图片路径
 * @return 0成功 -1失败
 */
static int ui_ai_refresh_image(const char *bmp_path)
{
    if (bmp_path == NULL || ai_img_buf == NULL)
    {
        printf("[UI_AI] canvas or path invalid\n");
        return -1;
    }

    // 函数局部变量：仅当前调用有效，函数结束自动销毁
    uint32_t *load_buf = NULL;
    int load_w = 0, load_h = 0;
    bmp_bpp_t load_bpp;

    // 1. 读取BMP到局部缓存
    load_buf = bmp_load_to_argb8888(bmp_path, &load_w, &load_h, &load_bpp);
    if (load_buf == NULL)
    {
        printf("[UI_AI] load bmp fail: %s\n", bmp_path);
        memset(ai_img_buf, 0, AI_IMG_W * AI_IMG_H * sizeof(uint32_t));
        return -1;
    }

    // 2. 计算等比例适配缩放系数
    float scale_w = (float)AI_IMG_W / load_w;
    float scale_h = (float)AI_IMG_H / load_h;
    float fit_scale = (scale_w < scale_h) ? scale_w : scale_h;

    int dst_fit_w, dst_fit_h;
    ima_calc_scale_size(load_w, load_h, fit_scale, &dst_fit_w, &dst_fit_h);

    // 3. 临时缩放缓存
    uint32_t *fit_buf = malloc(dst_fit_w * dst_fit_h * sizeof(uint32_t));
    if (fit_buf == NULL)
    {
        printf("[UI_AI] malloc fit buf failed\n");
        free(load_buf);  // 销毁原图缓存
        memset(ai_img_buf, 0, AI_IMG_W * AI_IMG_H * sizeof(uint32_t));
        return -1;
    }

    // 4. 原图缩放
    ima_scale_argb8888((uint8_t *)load_buf, load_w, load_h, (uint8_t *)fit_buf, fit_scale);

    // 5. 清空全局画布
    memset(ai_img_buf, 0, AI_IMG_W * AI_IMG_H * sizeof(uint32_t));

    // 6. 居中拷贝到画布
    int off_x = (AI_IMG_W - dst_fit_w) / 2;
    int off_y = (AI_IMG_H - dst_fit_h) / 2;
    for (int y = 0; y < dst_fit_h; y++)
    {
        uint32_t *dst_line = ai_img_buf + (off_y + y) * AI_IMG_W + off_x;
        uint32_t *src_line = fit_buf + y * dst_fit_w;
        memcpy(dst_line, src_line, dst_fit_w * sizeof(uint32_t));
    }

    lv_obj_invalidate(ai_img);

    ai_img_loaded = true;
    ai_recognized = false;

    // 7. 全部临时内存销毁
    free(fit_buf);
    free(load_buf);

    return 0;
}

// 初始化显示图像缓存
static lv_image_dsc_t *ui_ai_init_img_buf(void)
{
    // 分配显示图像缓存
    ai_img_buf = malloc(AI_IMG_W * AI_IMG_H * sizeof(uint32_t));
    if (ai_img_buf == NULL)
    {
        printf("[UI AI] malloc ai_img_buf failed\n");
        return NULL;
    }
    // 初始化显示图像缓存
    memset(ai_img_buf, 0, AI_IMG_W * AI_IMG_H * sizeof(uint32_t));

    lv_image_dsc_t *ai_img_dsc = malloc(sizeof(lv_image_dsc_t));
    if (ai_img_dsc == NULL)
    {
        printf("[UI AI] malloc ai_img_dsc failed\n");
        return NULL;
    }

    // 初始化图像描述符
    memset(ai_img_dsc, 0, sizeof(lv_image_dsc_t));
    ai_img_dsc->header.cf      = LV_COLOR_FORMAT_ARGB8888;
    ai_img_dsc->header.w       = AI_IMG_W;
    ai_img_dsc->header.h       = AI_IMG_H;
    ai_img_dsc->header.stride  = AI_IMG_W * 4;
    ai_img_dsc->data           = (const uint8_t *)ai_img_buf;
    ai_img_dsc->data_size      = AI_IMG_W * AI_IMG_H * sizeof(uint32_t);

    ai_img_loaded = false;
    ai_recognized = false;
    return ai_img_dsc;
}

// ai识别按钮回调
static void ui_ai_btn_cb(lv_event_t *e)
{
    uintptr_t tag = (uintptr_t) lv_event_get_user_data(e);
    switch (tag)
    {
        case 0:
            ui_home_scr_load();
            break;

        case 1:
            if (!ai_img_loaded)
            {
                break;
            }
            if (ai_recognized)
            {
                ui_ai_refresh_image(ai_load_path);
            }
            if (image_argb_detect(ai_img_buf, AI_IMG_W, AI_IMG_H) < 0)
            {
                printf("[IMAGE_ARG] detect failed\n");
                break;
            }
            ai_recognized = true;
            lv_obj_invalidate(ai_img);
            break;

        case 2:
            if (!ai_img_loaded || !ai_recognized)
            {
                break;
            }
            bmp_save_from_argb8888(
                AI_OUTPUT_PATH,
                (const uint8_t *)ai_img_buf,
                AI_IMG_W,
                AI_IMG_H,
                BMP_BPP_32
            );
            break;

        default: 
            break;
    }
}

// list按钮选择bmp回调
static void ui_ai_list_cb(lv_event_t *e)
{
    lv_obj_t *btn = lv_event_get_target(e);
    lv_obj_t *list = lv_obj_get_parent(btn);
    const char *name = lv_list_get_button_text(list, btn);
    if (name)
    {
        snprintf(ai_load_path, sizeof(ai_load_path), "%s/%s", ai_cur_dir, name);
        ui_ai_refresh_image(ai_load_path);
    }
}

// 扫描目录并将bmp文件添加到列表
static void ui_ai_scan_dir(const char *dir_path)
{
    if (ai_list == NULL || dir_path == NULL) return;

    // 保存当前目录
    strncpy(ai_cur_dir, dir_path, sizeof(ai_cur_dir) - 1);
    ai_cur_dir[sizeof(ai_cur_dir) - 1] = '\0';

    // 清空列表
    lv_obj_clean(ai_list);

    // 扫描目录
    DIR *dir = opendir(dir_path);
    if (!dir)
    {
        printf("[UI_AI] open dir fail: %s\n", dir_path);
        return;
    }

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL)
    {
        int len = strlen(entry->d_name);
        if (len > 4 &&
            entry->d_name[len-4] == '.' &&
            (entry->d_name[len-3] == 'b' || entry->d_name[len-3] == 'B') &&
            (entry->d_name[len-2] == 'm' || entry->d_name[len-2] == 'M') &&
            (entry->d_name[len-1] == 'p' || entry->d_name[len-1] == 'P'))
        {
            ui_widget_list_add_button(ai_list, entry->d_name, ui_ai_list_cb);
        }
    }
    closedir(dir);
}

// 目录按钮回调
static void ui_ai_dir_btn_cb(lv_event_t *e)
{
    uintptr_t tag = (uintptr_t)lv_event_get_user_data(e);
    switch (tag)
    {
        case 0: 
            ui_ai_scan_dir("./image"); 
            break;
        case 1: 
            ui_ai_scan_dir(photo_path); 
            break;
        case 2:
            // 显示自定义路径弹窗
            if (ai_popup_win)
            {
                lv_obj_t *mask = lv_obj_get_parent(ai_popup_win);
                lv_obj_remove_flag(mask, LV_OBJ_FLAG_HIDDEN);
                lv_obj_remove_flag(ai_popup_win, LV_OBJ_FLAG_HIDDEN);
                if (ai_popup_ta)
                {
                    lv_textarea_set_text(ai_popup_ta, "");
                    lv_obj_clear_state(ai_popup_ta, LV_STATE_FOCUSED);
                }
            }
            break;
    }
}

// 弹窗确认回调
static void ui_ai_path_confirm_cb(lv_event_t *e)
{
    if (ai_popup_ta == NULL) return;

    const char *path = lv_textarea_get_text(ai_popup_ta);
    if (path && strlen(path) > 0)
    {
        ui_ai_scan_dir(path);
    }

    // 隐藏弹窗和键盘
    lv_obj_t *win = lv_obj_get_parent(ai_popup_ta);
    lv_obj_t *mask = lv_obj_get_parent(win);
    lv_obj_add_flag(mask, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(win, LV_OBJ_FLAG_HIDDEN);
    if (kb) lv_obj_add_flag(kb, LV_OBJ_FLAG_HIDDEN);
}

// 弹窗取消回调
static void ui_ai_path_cancel_cb(lv_event_t *e)
{
    if (ai_popup_win)
    {
        lv_obj_t *mask = lv_obj_get_parent(ai_popup_win);
        lv_obj_add_flag(mask, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(ai_popup_win, LV_OBJ_FLAG_HIDDEN);
    }
    if (kb) lv_obj_add_flag(kb, LV_OBJ_FLAG_HIDDEN);
}

lv_obj_t* ui_ai_scr_create(void)
{
    if (ai_scr != NULL)
        return ai_scr;


    AppConfig *cfg = config_get_global();
    photo_path = (cfg && cfg->photo_save_path[0]) ? cfg->photo_save_path : DEF_PHOTO_PATH;
    // 从配置读取拍照保存路径

    ai_scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(ai_scr, COLOR_GRAY_LIGHT, 0);
    lv_obj_set_style_bg_opa(ai_scr, LV_OPA_COVER, 0);

    // 创建容器
    lv_obj_t *img_box = ui_widget_create_box(
        ai_scr,
        772, 579,
        LV_ALIGN_TOP_LEFT,
        10, 10,
        COLOR_GRAY_DARK,
        0,
        false
    );

    // 初始化显示图像缓存
    lv_image_dsc_t *ai_img_dsc = ui_ai_init_img_buf();
    if (ai_img_dsc == NULL)
    {
        printf("[UI AI] init ai_img_dsc failed\n");
        return NULL;
    }

    // 创建img控件
    ai_img = ui_widget_create_img_buffer(
        img_box, 
        ai_img_dsc,
        AI_IMG_W, AI_IMG_H,
        LV_IMAGE_ALIGN_STRETCH,
        LV_ALIGN_TOP_LEFT,
        0, 0
    );

    // 创建返回按钮
    ui_widget_create_btn(
        ai_scr, 
        "返回", 
        200, 50, 
        COLOR_BLUE_LIGHT, COLOR_BLUE_DARK, 
        LV_ALIGN_TOP_LEFT, 
        800, 10, 
        0, 
        ui_ai_btn_cb
    );

    // 创建ai识别按钮
    ui_widget_create_btn(
        ai_scr, 
        "ai识别", 
        200, 50, 
        COLOR_BLUE_LIGHT, COLOR_BLUE_DARK, 
        LV_ALIGN_TOP_LEFT, 
        800, 80, 
        1, 
        ui_ai_btn_cb
    );

    // 创建保存按钮
    ui_widget_create_btn(
        ai_scr, 
        "保存", 
        200, 50, 
        COLOR_BLUE_LIGHT, COLOR_BLUE_DARK, 
        LV_ALIGN_TOP_LEFT, 
        800, 150, 
        2, 
        ui_ai_btn_cb
    );

    // 目录选择按钮：图库、快照、自定义
    ui_widget_create_btn(
        ai_scr, 
        "图库", 
        65, 30, 
        COLOR_BLUE_LIGHT, COLOR_BLUE_DARK,
        LV_ALIGN_TOP_LEFT, 
        800, 215, 
        0, 
        ui_ai_dir_btn_cb
    );
    ui_widget_create_btn(
        ai_scr, 
        "快照", 
        65, 30, 
        COLOR_BLUE_LIGHT, COLOR_BLUE_DARK,
        LV_ALIGN_TOP_LEFT, 
        870, 215, 
        1, 
        ui_ai_dir_btn_cb
    );
    ui_widget_create_btn(
        ai_scr, 
        "自定义", 
        65, 30, 
        COLOR_BLUE_LIGHT, COLOR_BLUE_DARK,
        LV_ALIGN_TOP_LEFT, 
        940, 215, 
        2, 
        ui_ai_dir_btn_cb
    );

    // 标题文本
    ui_widget_create_label(
        ai_scr, 
        "选择图片", 
        LV_ALIGN_TOP_LEFT, 
        800, 255,
        FONT_BODY, 
        COLOR_TEXT_BLACK
    );

    // 创建选择图片列表
    ai_list = ui_widget_create_list(
        ai_scr, 
        200, 305, 
        LV_ALIGN_TOP_LEFT, 
        800, 280
    );

    // 扫描默认目录
    ui_ai_scan_dir("./image");

    // 弹窗：自定义目录选择
    ai_popup_win = ui_widget_create_popup(
        400, 200, 
        COLOR_GRAY_DARK, 
        false
    );
    // 弹窗内：提示文字
    ui_widget_create_label(
        ai_popup_win, 
        "输入目录路径", 
        LV_ALIGN_TOP_MID, 
        0, 10,  
        FONT_BODY, 
        COLOR_TEXT_BLACK
    );
    // 弹窗内：文本框
    ai_popup_ta = ui_widget_create_textarea(
        ai_popup_win, 
        360, 35,                  
        LV_ALIGN_TOP_MID, 
        0, 50, 
        kb
    );
    // 弹窗内：确认按钮
    ui_widget_create_btn(
        ai_popup_win, 
        "确认", 
        100, 40, 
        COLOR_BLUE_LIGHT, COLOR_BLUE_DARK,
        LV_ALIGN_BOTTOM_MID, 
        -70, -15, 
        0, 
        ui_ai_path_confirm_cb
    );
    // 弹窗内：取消按钮
    ui_widget_create_btn(
        ai_popup_win, 
        "取消", 
        100, 40, 
        COLOR_BLUE_LIGHT, COLOR_BLUE_DARK,
        LV_ALIGN_BOTTOM_MID, 
        70, -15, 
        0, 
        ui_ai_path_cancel_cb
    );

    return ai_scr;
}

void ui_ai_scr_load(void)
{
    if (ai_scr && ai_img_buf) 
    {
        if (ai_img_loaded)
        {
            ai_img_loaded = false;
            ai_recognized = false;
            memset(ai_img_buf, 0, AI_IMG_W * AI_IMG_H * sizeof(uint32_t));
        }

         lv_screen_load_anim(
            ai_scr, 
            LV_SCR_LOAD_ANIM_MOVE_LEFT, 
            300, 
            0, 
            false
        );
    }
}