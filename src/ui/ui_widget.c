#include "ui_widget.h"
#include "config.h"
#include "lvgl/lvgl.h"
#include "my_font.h"
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

// 模块私有静态样式，仅本文件可见
static lv_style_t s_btn_norm;  // 按钮常态通用基础样式
static lv_style_t s_btn_press; // 按钮按压通用基础样式

lv_obj_t* ui_widget_create_box(lv_obj_t* parent, int32_t w, int32_t h, lv_align_t align, int32_t x_ofs, int32_t y_ofs,
                               lv_color_t bg_color, int32_t pad, bool scrollable)
{
    // 创建基础容器
    lv_obj_t* box = lv_obj_create(parent);
    // 设置宽高
    lv_obj_set_size(box, w, h);
    // 设置位置
    lv_obj_align(box, align, x_ofs, y_ofs);
    // 背景颜色与透明度
    lv_obj_set_style_bg_color(box, bg_color, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(box, LV_OPA_COVER, LV_PART_MAIN);
    // 圆角美化和裁剪角角框
    lv_obj_set_style_radius(box, RADIUS_BOX, LV_PART_MAIN);
    lv_obj_set_style_clip_corner(box, true, LV_PART_MAIN);

    // pad != -1 才设置统一内边距，等于-1则保留LVGL原生默认padding
    if (pad != -1)
    {
        lv_obj_set_style_pad_all(box, pad, LV_PART_MAIN);
    }

    // 是否开启滚动条
    if (!scrollable)
    {
        lv_obj_clear_flag(box, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_scrollbar_mode(box, LV_SCROLLBAR_MODE_OFF);
    }

    return box;
}

void ui_widget_style_init(void)
{
    // 初始化按钮常态基础样式（只保留全局统一不变的属性）
    lv_style_init(&s_btn_norm);
    lv_style_set_radius(&s_btn_norm, RADIUS_BTN);           // 统一圆角
    lv_style_set_text_color(&s_btn_norm, lv_color_white()); // 统一白色文字
    lv_style_set_pad_all(&s_btn_norm, 5);                   // 统一内边距
    lv_style_set_bg_opa(&s_btn_norm, LV_OPA_COVER);         // 统一完全不透明

    // 初始化按钮按压基础样式（无固定属性，运行时动态覆盖背景色）
    lv_style_init(&s_btn_press);
}

lv_obj_t* ui_widget_create_dot(lv_obj_t* parent, int32_t size, lv_color_t color,
                               lv_align_t align, int32_t x_ofs, int32_t y_ofs)
{
    lv_obj_t* dot = lv_obj_create(parent);
    lv_obj_set_size(dot, size, size);
    lv_obj_align(dot, align, x_ofs, y_ofs);
    lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(dot, color, 0);
    lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(dot, 0, 0);
    lv_obj_clear_flag(dot, LV_OBJ_FLAG_SCROLLABLE);
    return dot;
}

lv_obj_t* ui_widget_create_btn(lv_obj_t* parent, const char* text, int32_t btn_w, int32_t btn_h, lv_color_t norm_color,
                               lv_color_t press_color, lv_align_t align, int32_t x_ofs, int32_t y_ofs, uintptr_t tag,
                               lv_event_cb_t cb)
{
    // 1. 创建按钮控件
    lv_obj_t* btn = lv_button_create(parent);

    // 2. 添加全局基础样式（文档第三步：绑定样式到控件）
    lv_obj_add_style(btn, &s_btn_norm, LV_STATE_DEFAULT);

    // 3. 单独设置当前按钮独有属性，覆盖基础样式对应属性
    // 设置宽高
    lv_obj_set_size(btn, btn_w, btn_h);
    // 常态背景色
    lv_obj_set_style_bg_color(btn, norm_color, LV_STATE_DEFAULT);
    // 按下背景色
    lv_obj_set_style_bg_color(btn, press_color, LV_STATE_PRESSED);

    // 设置位置
    lv_obj_align(btn, align, x_ofs, y_ofs);
    // 绑定点击回调
    lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, (void*) tag);

    // 4. 如果传入了有效文本，则创建默认按钮文本 (方便快速创建, 也方便自定义布局时传 NULL)
    if (text != NULL && text[0] != '\0')
    {
        lv_obj_t* lab = lv_label_create(btn);
        lv_label_set_text(lab, text);
        lv_obj_center(lab);
        lv_obj_set_style_text_font(lab, FONT_BODY, LV_PART_MAIN);
    }

    return btn;
}

lv_obj_t* ui_widget_create_label(lv_obj_t* parent, const char* text, lv_align_t align, int32_t x_ofs, int32_t y_ofs,
                                 const lv_font_t* font, lv_color_t text_color)
{
    // 创建文字控件
    lv_obj_t* lab = lv_label_create(parent);
    // 设置显示文本
    lv_label_set_text(lab, text);
    // 设置对齐与偏移
    lv_obj_align(lab, align, x_ofs, y_ofs);
    // 设置指定字体
    lv_obj_set_style_text_font(lab, font, LV_PART_MAIN);
    // 设置文字颜色
    lv_obj_set_style_text_color(lab, text_color, LV_PART_MAIN);

    return lab;
}

lv_obj_t* ui_widget_create_label_static(lv_obj_t* parent, const char* text, lv_align_t align, int32_t x_ofs,
                                        int32_t y_ofs, const lv_font_t* font, lv_color_t text_color)
{
    // 创建文字控件
    lv_obj_t* lab = lv_label_create(parent);
    // 设置显示文本
    lv_label_set_text_static(lab, text);
    // 设置对齐与偏移
    lv_obj_align(lab, align, x_ofs, y_ofs);
    // 设置指定字体
    lv_obj_set_style_text_font(lab, font, LV_PART_MAIN);
    // 设置文字颜色
    lv_obj_set_style_text_color(lab, text_color, LV_PART_MAIN);

    return lab;
}

// 关闭弹窗回调
static void popup_close_btn_cb(lv_event_t* e)
{
    // 获取关闭按钮控件
    lv_obj_t* close_btn = lv_event_get_target(e);
    // 向上一级：弹窗主体win
    lv_obj_t* win = lv_obj_get_parent(close_btn);
    // 再向上一级：全屏遮罩mask
    lv_obj_t* mask = lv_obj_get_parent(win);
    // 同时隐藏遮罩和弹窗窗口
    lv_obj_add_flag(mask, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(win, LV_OBJ_FLAG_HIDDEN);
}

lv_obj_t* ui_widget_create_popup(int32_t win_w, int32_t win_h, lv_color_t bg_color, bool has_close_btn)
{
    // 创建全屏遮罩，顶层图层
    lv_obj_t* mask = lv_obj_create(lv_layer_top());
    lv_obj_set_size(mask, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(mask, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(mask, COLOR_MASK_OPA, LV_PART_MAIN);
    lv_obj_add_flag(mask, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(mask, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_PRESS_LOCK);

    // 弹窗主体容器
    lv_obj_t* win = ui_widget_create_box(mask, win_w, win_h, LV_ALIGN_CENTER, 0, 0, bg_color, -1, true);
    lv_obj_add_flag(win, LV_OBJ_FLAG_HIDDEN);

    // 只有需要关闭按钮时才创建
    if (has_close_btn)
    {
        ui_widget_create_btn(win, "x", 20, 20, lv_color_hex(COLOR_BTN_NORM), lv_color_hex(COLOR_BTN_PRESS),
                             LV_ALIGN_TOP_RIGHT, 0, 0, 0, popup_close_btn_cb);
    }

    return win;
}

lv_obj_t* ui_widget_create_img_file(lv_obj_t* parent, const char* file_path, int32_t img_w, int32_t img_h,
                                    lv_image_align_t align_mode, lv_align_t align, int32_t x_ofs, int32_t y_ofs)
{
    // LVGL官方标准创建图片接口 lv_image_create()
    lv_obj_t* img = lv_image_create(parent);
    // 设置图片资源源：文件路径（支持A:/xxx格式Linux盘符路径）
    lv_image_set_src(img, file_path);
    // 设置图片控件画布可视宽高
    lv_obj_set_size(img, img_w, img_h);
    // 官方接口：设置图片适配/对齐模式 lv_image_set_align
    // 头文件明确标注：STRETCH模式会重置旋转、缩放、轴心参数
    lv_image_set_align(img, align_mode);
    // 设置图片控件在父容器内的整体位置偏移
    lv_obj_align(img, align, x_ofs, y_ofs);
   
    return img;
}

lv_obj_t* ui_widget_create_img_buffer(lv_obj_t* parent, const lv_img_dsc_t* dsc,
                                      int32_t img_w, int32_t img_h,
                                      lv_image_align_t align_mode,
                                      lv_align_t align, int32_t x_ofs, int32_t y_ofs)
{
    lv_obj_t* img = lv_image_create(parent);
    // 绑定内存图像描述符（摄像头帧核心）
    lv_image_set_src(img, dsc);
    // 控件画布尺寸
    lv_obj_set_size(img, img_w, img_h);
    // 图像填充拉伸模式
    lv_image_set_align(img, align_mode);
    // 父容器内位置对齐
    lv_obj_align(img, align, x_ofs, y_ofs);

    return img;
}

// 静态回调：文本框获取/失去焦点，自动显示/隐藏软键盘
static void ta_focus_event_cb(lv_event_t* e)
{
    lv_obj_t* ta = lv_event_get_target(e);
    lv_obj_t* kb = lv_event_get_user_data(e);

    if (lv_event_get_code(e) == LV_EVENT_FOCUSED)
    {
        // 获得焦点：显示软键盘并绑定当前文本框
        lv_obj_remove_flag(kb, LV_OBJ_FLAG_HIDDEN);
        lv_keyboard_set_textarea(kb, ta);
    }
    else if (lv_event_get_code(e) == LV_EVENT_DEFOCUSED)
    {
        // 失去焦点：隐藏软键盘，解绑文本框
        lv_obj_add_flag(kb, LV_OBJ_FLAG_HIDDEN);
        lv_keyboard_set_textarea(kb, NULL);
    }
}

lv_obj_t* ui_widget_create_textarea(lv_obj_t* parent, int32_t w, int32_t h, lv_align_t align, int32_t x_ofs,
                                    int32_t y_ofs, lv_obj_t* kb)
{
    lv_obj_t* ta = lv_textarea_create(parent);
    // 设置尺寸
    lv_obj_set_size(ta, w, h);
    // 设置位置
    lv_obj_align(ta, align, x_ofs, y_ofs);
    // 基础美化圆角
    lv_obj_set_style_radius(ta, RADIUS_TEXTAREA, LV_PART_MAIN);
    lv_obj_set_style_text_font(ta, FONT_BODY, LV_PART_MAIN);

    // 如果传入软键盘，绑定焦点/失焦事件
    if (kb != NULL)
    {
        lv_obj_add_event_cb(ta, ta_focus_event_cb, LV_EVENT_FOCUSED, kb);
        lv_obj_add_event_cb(ta, ta_focus_event_cb, LV_EVENT_DEFOCUSED, kb);
    }

    return ta;
}

lv_obj_t* ui_widget_create_keyboard(lv_obj_t* parent, int32_t kb_w, int32_t kb_h, lv_align_t align, int32_t x_ofs,
                                    int32_t y_ofs)
{
    lv_obj_t* kb = lv_keyboard_create(parent);
    lv_obj_set_size(kb, kb_w, kb_h);
    lv_obj_align(kb, align, x_ofs, y_ofs);
    // 默认隐藏，点击文本框才弹出
    lv_obj_add_flag(kb, LV_OBJ_FLAG_HIDDEN);

    return kb;
}

lv_obj_t* ui_widget_create_spinner(lv_obj_t* parent, int32_t size,
                                   uint32_t anim_ms, uint16_t sweep_deg,
                                   lv_align_t align, int32_t x_ofs, int32_t y_ofs)
{
    lv_obj_t* spinner = lv_spinner_create(parent);
    lv_obj_set_size(spinner, size, size);
    lv_obj_align(spinner, align, x_ofs, y_ofs);

    // 自定义参数为0则使用全局默认值
    uint32_t def_time = (anim_ms == 0) ? 1000 : anim_ms;
    uint16_t def_sweep = (sweep_deg == 0) ? 270 : sweep_deg;
    lv_spinner_set_anim_params(spinner, def_time, def_sweep);

    // 全局统一线条粗细、颜色，如需修改后期原生style接口调整
    lv_obj_set_style_arc_width(spinner, 4, LV_PART_MAIN);
    lv_obj_set_style_arc_color(spinner, lv_color_hex(0x0088FF), LV_PART_MAIN);

    return spinner;
}

lv_obj_t* ui_widget_create_roller(lv_obj_t* parent, const char* options,
                                  uint32_t row_cnt, lv_roller_mode_t mode,
                                  lv_align_t align, int32_t x_ofs, int32_t y_ofs,
                                  lv_event_cb_t cb)
{
    lv_obj_t* roller = lv_roller_create(parent);
    lv_roller_set_options(roller, options, mode);
    lv_roller_set_visible_row_count(roller, row_cnt);
    lv_obj_align(roller, align, x_ofs, y_ofs);

    if (cb != NULL)
    {
        lv_obj_add_event_cb(roller, cb, LV_EVENT_VALUE_CHANGED, NULL);
    }

    return roller;
}

lv_obj_t* ui_widget_create_list(lv_obj_t* parent, int32_t w, int32_t h,
                                 lv_align_t align, int32_t x_ofs, int32_t y_ofs)
{
    lv_obj_t* list = lv_list_create(parent);
    lv_obj_set_size(list, w, h);
    lv_obj_align(list, align, x_ofs, y_ofs);
    // 统一box风格：深灰背景、圆角、裁剪
    lv_obj_set_style_bg_color(list, COLOR_GRAY_DARK, 0);
    lv_obj_set_style_bg_opa(list, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(list, RADIUS_BOX, LV_PART_MAIN);
    lv_obj_set_style_clip_corner(list, true, LV_PART_MAIN);
    // 列表项之间的间距
    lv_obj_set_style_pad_row(list, 4, 0);
    // 列表上下边距，避免按钮紧贴列表边缘
    lv_obj_set_style_pad_top(list, 6, 0);
    lv_obj_set_style_pad_bottom(list, 6, 0);
    return list;
}

lv_obj_t* ui_widget_list_add_button(lv_obj_t* list, const char* txt, lv_event_cb_t cb)
{
    lv_obj_t* btn = lv_list_add_button(list, NULL, txt);
    // 按钮圆角
    lv_obj_set_style_radius(btn, RADIUS_BTN, LV_PART_MAIN);
    if (cb != NULL)
    {
        lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, NULL);
    }
    return btn;
}

/* =======================================================================================================
 *  ui_slider  滑块控件实现
 * ======================================================================================================= */

// 滑块内部事件回调：值变化时更新标签
static void slider_internal_cb(lv_event_t* e)
{
    ui_slider_ctx_t* ctx = (ui_slider_ctx_t*)lv_event_get_user_data(e);
    if (!ctx) return;
    ctx->value = (int32_t)lv_slider_get_value(ctx->slider);
    ui_widget_slider_update_label(ctx);
}

ui_slider_ctx_t* ui_widget_create_slider(lv_obj_t* parent,
                                         int32_t min, int32_t max, int32_t init_val,
                                         const char* unit_text,
                                         lv_align_t align, int32_t x_ofs, int32_t y_ofs,
                                         lv_event_cb_t cb)
{
    ui_slider_ctx_t* ctx = (ui_slider_ctx_t*)malloc(sizeof(ui_slider_ctx_t));
    if (!ctx) return NULL;
    memset(ctx, 0, sizeof(ui_slider_ctx_t));

    ctx->min = min;
    ctx->max = max;
    ctx->value = init_val;

    if (unit_text)
        strncpy(ctx->unit, unit_text, sizeof(ctx->unit) - 1);

    // 创建滑块
    ctx->slider = lv_slider_create(parent);
    lv_slider_set_range(ctx->slider, min, max);
    lv_slider_set_value(ctx->slider, init_val, LV_ANIM_OFF);
    lv_obj_set_size(ctx->slider, 200, 20);
    lv_obj_align(ctx->slider, align, x_ofs, y_ofs);
    lv_obj_set_style_height(ctx->slider, 8, LV_PART_INDICATOR);
    lv_obj_set_style_height(ctx->slider, 8, LV_PART_KNOB);
    lv_obj_set_style_bg_color(ctx->slider, COLOR_GRAY_DARK, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(ctx->slider, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(ctx->slider, COLOR_BLUE_NORMAL, LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(ctx->slider, LV_OPA_COVER, LV_PART_INDICATOR);

    // 值变化回调（内部更新标签）
    lv_obj_add_event_cb(ctx->slider, slider_internal_cb, LV_EVENT_VALUE_CHANGED, ctx);

    // 外部回调（可选）
    if (cb)
        lv_obj_add_event_cb(ctx->slider, cb, LV_EVENT_VALUE_CHANGED, ctx);

    // 创建数值显示标签
    ctx->label = lv_label_create(parent);
    ui_widget_slider_update_label(ctx);
    lv_obj_set_style_text_font(ctx->label, FONT_BODY, LV_PART_MAIN);
    lv_obj_set_style_text_color(ctx->label, COLOR_TEXT_DARK, LV_PART_MAIN);

    // 将标签对齐到滑块下方
    lv_obj_align_to(ctx->label, ctx->slider, LV_ALIGN_OUT_BOTTOM_MID, 0, 8);

    return ctx;
}

int32_t ui_widget_slider_get_value(ui_slider_ctx_t* ctx)
{
    if (!ctx) return 0;
    return ctx->value;
}

void ui_widget_slider_set_value(ui_slider_ctx_t* ctx, int32_t val)
{
    if (!ctx) return;
    if (val < ctx->min) val = ctx->min;
    if (val > ctx->max) val = ctx->max;
    ctx->value = val;
    if (ctx->slider)
        lv_slider_set_value(ctx->slider, val, LV_ANIM_ON);
    ui_widget_slider_update_label(ctx);
}

void ui_widget_slider_update_label(ui_slider_ctx_t* ctx)
{
    if (!ctx || !ctx->label) return;
    char buf[64];
    if (ctx->unit[0] != '\0')
        snprintf(buf, sizeof(buf), "%d %s", ctx->value, ctx->unit);
    else
        snprintf(buf, sizeof(buf), "%d", ctx->value);
    lv_label_set_text(ctx->label, buf);
    lv_obj_align_to(ctx->label, ctx->slider, LV_ALIGN_OUT_BOTTOM_MID, 0, 8);
}

void ui_widget_slider_delete(ui_slider_ctx_t* ctx)
{
    if (!ctx) return;
    if (ctx->slider) lv_obj_del(ctx->slider);
    if (ctx->label) lv_obj_del(ctx->label);
    free(ctx);
}