#ifndef __UI_WIDGET_H__
#define __UI_WIDGET_H__

#include "lvgl/lvgl.h"

// 容器/底板的圆角大小
#define RADIUS_BOX          12
// 按钮的圆角大小
#define RADIUS_BTN          6
// 文本框的圆角大小
#define RADIUS_TEXTAREA     6

// 弹窗背后黑色半透明遮罩的透明度（0~255，越大越黑）
#define COLOR_MASK_OPA      140

// 按钮正常状态的背景色（蓝色）
#define COLOR_BTN_NORM      0x0088FF

// 按钮按下时的背景色（深蓝色，按压反馈）
#define COLOR_BTN_PRESS     0x0055AA

/**
 * @brief 创建基础圆角容器box，用作界面底板/背景框
 * @param parent 父对象
 * @param w 容器宽度
 * @param h 容器高度
 * @param align 对齐基准
 * @param x_ofs X偏移
 * @param y_ofs Y偏移
 * @param bg_color 容器背景颜色
 * @param pad 容器边距，-1 默认
 * @param scrollable 是否开启滚动条
 * @return lv_obj_t* 容器控件
 */
lv_obj_t* ui_widget_create_box(lv_obj_t* parent, int32_t w, int32_t h, lv_align_t align,
                               int32_t x_ofs, int32_t y_ofs, lv_color_t bg_color, int32_t pad, bool scrollable);

/**
 * @brief 初始化控件模块内置统一样式
 * @note 所有界面创建前调用一次即可，全局共用一套按钮样式
 */
void ui_widget_style_init(void);

/**
 * @brief 创建指示灯/状态圆点 (基础控件)
 * @param parent 父容器
 * @param size 圆点直径
 * @param color 圆点颜色
 * @param align 对齐基准
 * @param x_ofs X偏移
 * @param y_ofs Y偏移
 * @return lv_obj_t* 圆点对象
 */
lv_obj_t* ui_widget_create_dot(lv_obj_t* parent, int32_t size, lv_color_t color,
                               lv_align_t align, int32_t x_ofs, int32_t y_ofs);

/**
 * @brief 创建标准化通用按钮（宽高、两种背景色外部自定义传入）
 * @param parent 父控件容器
 * @param text 按钮文字 (可以为 NULL 或空字符串, 此时不创建默认文本, 方便自定义布局)
 * @param btn_w 按钮宽度
 * @param btn_h 按钮高度
 * @param norm_color 常态背景色
 * @param press_color 按压背景色
 * @param align 对齐基准
 * @param x_ofs X偏移
 * @param y_ofs Y偏移
 * @param tag 区分标记
 * @param cb 点击回调
 * @return 按钮对象
 */
lv_obj_t* ui_widget_create_btn(lv_obj_t* parent, const char* text,
                               int32_t btn_w, int32_t btn_h,
                               lv_color_t norm_color, lv_color_t press_color,
                               lv_align_t align, int32_t x_ofs, int32_t y_ofs,
                               uintptr_t tag, lv_event_cb_t cb);


/**
 * @brief 创建标准文字标签label
 * @param parent 父容器
 * @param text 显示文字内容
 * @param align 对齐基准
 * @param x_ofs X偏移
 * @param y_ofs Y偏移
 * @param font 指定使用的字体指针
 * @param text_color 文字颜色
 * @return lv_obj_t* 文字控件对象
 */
lv_obj_t* ui_widget_create_label(lv_obj_t* parent, const char* text, lv_align_t align,
                                 int32_t x_ofs, int32_t y_ofs,
                                 const lv_font_t* font, lv_color_t text_color);

/**
 * @brief 创建静态文字标签label
 * @param parent 父容器
 * @param text 显示文字内容
 * @param align 对齐基准
 * @param x_ofs X偏移
 * @param y_ofs Y偏移
 * @param font 指定使用的字体指针
 * @param text_color 文字颜色
 * @return lv_obj_t* 文字控件对象
 */
lv_obj_t* ui_widget_create_label_static(lv_obj_t* parent, const char* text, lv_align_t align,
                                 int32_t x_ofs, int32_t y_ofs,
                                 const lv_font_t* font, lv_color_t text_color);

/**
 * @brief 创建完整弹窗（遮罩+主体框）
 * @param win_w 弹窗主体宽度
 * @param win_h 弹窗主体高度
 * @param bg_color 弹窗背景色
 * @param has_close_bth 是否需要默认返回键
 * @return 弹窗主体容器（遮罩自动创建在顶层）
 */
lv_obj_t* ui_widget_create_popup(int32_t win_w, int32_t win_h, lv_color_t bg_color, bool has_close_btn);

/**
 * @brief 从磁盘文件创建普通图片控件(bmp/png/jpg)，完全匹配LVGL9官方lv_image.h接口
 * @param parent 父容器控件
 * @param file_path 文件路径，Linux环境必须以 A: 开头（LVGL自定义盘符规则）
 * @param img_w 图片控件画布宽度（可视区域宽）
 * @param img_h 图片控件画布高度（可视区域高）
 * @param align_mode 图片内部适配模式 lv_image_align_t
 *        LV_IMAGE_ALIGN_DEFAULT：原图尺寸，左上角对齐画布，大图自动裁剪
 *        LV_IMAGE_ALIGN_TOP_LEFT/TOP_MID/TOP_RIGHT...：原图按对应方位摆放，超出裁剪
 *        LV_IMAGE_ALIGN_CENTER：原图居中放置，大图四周裁剪
 *        LV_IMAGE_ALIGN_STRETCH：强制拉伸填满画布，图片变形；会覆盖手动旋转/缩放/轴心
 *        LV_IMAGE_ALIGN_TILE：平铺重复填充画布
 * @param align 图片控件整体在父容器中的对齐基准 LV_ALIGN_xxx
 * @param x_ofs 整体X轴偏移像素
 * @param y_ofs 整体Y轴偏移像素
 * @return lv_obj_t* lv_image图片控件指针
 * @note 若传入LV_IMAGE_ALIGN_STRETCH，手动设置的rotation、scale、pivot会被内部重置失效
 */
lv_obj_t* ui_widget_create_img_file(lv_obj_t* parent, const char* file_path,
                                    int32_t img_w, int32_t img_h,
                                    lv_image_align_t align_mode,
                                    lv_align_t align, int32_t x_ofs, int32_t y_ofs);

/**
 * @brief 创建内存图像控件（用于摄像头ARGB8888帧、内存缓存图像）
 * @param parent 父容器
 * @param dsc 图像描述符 lv_img_dsc_t（摄像头帧数据）
 * @param img_w 控件画布宽
 * @param img_h 控件画布高
 * @param align_mode 图片内部填充模式
 * @param align 控件在父容器对齐基准
 * @param x_ofs X偏移
 * @param y_ofs Y偏移
 * @return lv_image控件指针
 */
lv_obj_t* ui_widget_create_img_buffer(lv_obj_t* parent, const lv_img_dsc_t* dsc,
                                      int32_t img_w, int32_t img_h,
                                      lv_image_align_t align_mode,
                                      lv_align_t align, int32_t x_ofs, int32_t y_ofs);

/**
 * @brief 创建文本框，支持绑定软键盘焦点联动
 * @param parent 父容器
 * @param w 文本框宽度
 * @param h 文本框高度
 * @param align 父容器对齐方式
 * @param x_ofs X偏移
 * @param y_ofs Y偏移
 * @param kb 软键盘句柄，传NULL则不绑定焦点事件
 * @return 文本框控件指针
 */
lv_obj_t* ui_widget_create_textarea(lv_obj_t* parent,
                                    int32_t w, int32_t h,
                                    lv_align_t align, int32_t x_ofs, int32_t y_ofs,
                                    lv_obj_t* kb);

/**
 * @brief 创建软键盘，默认隐藏，放在屏幕底层
 * @param parent 父容器（一般填屏幕）
 * @param kb_w 软键盘宽度
 * @param kb_h 软键盘高度
 * @param align 对齐方式（推荐LV_ALIGN_BOTTOM_MID贴底部）
 * @param x_ofs X偏移
 * @param y_ofs Y偏移
 * @return 软键盘控件指针
 */
lv_obj_t* ui_widget_create_keyboard(lv_obj_t* parent,
                                    int32_t kb_w, int32_t kb_h,
                                    lv_align_t align, int32_t x_ofs, int32_t y_ofs);
                                    
/**
 * @brief 创建Loading转圈spinner加载动画
 * @param parent 父容器
 * @param size 正方形控件尺寸
 * @param anim_ms 旋转一圈耗时(ms)，传0使用默认1000
 * @param sweep_deg 弧线角度0~360，传0使用默认270
 * @param align 父容器对齐基准
 * @param x_ofs X偏移
 * @param y_ofs Y偏移
 * @return spinner控件句柄，可后续原生API微调
 */
lv_obj_t* ui_widget_create_spinner(lv_obj_t* parent, int32_t size,
                                   uint32_t anim_ms, uint16_t sweep_deg,
                                   lv_align_t align, int32_t x_ofs, int32_t y_ofs);

/**
 * @brief 创建滚动选择器 Roller，用于选项列表选择（如文件列表）
 * @param parent    父容器
 * @param options   '\n' 分隔的选项字符串，如 "1.bmp\n2.bmp\n3.bmp"
 * @param row_cnt   可见行数（推荐 3~5）
 * @param mode      滚动模式 LV_ROLLER_MODE_NORMAL / LV_ROLLER_MODE_INFINITE
 * @param align     对齐基准
 * @param x_ofs     X偏移
 * @param y_ofs     Y偏移
 * @param cb        值变化回调 LV_EVENT_VALUE_CHANGED，传 NULL 则忽略
 * @return lv_obj_t* roller 控件指针
 */
lv_obj_t* ui_widget_create_roller(lv_obj_t* parent, const char* options,
                                  uint32_t row_cnt, lv_roller_mode_t mode,
                                  lv_align_t align, int32_t x_ofs, int32_t y_ofs,
                                  lv_event_cb_t cb);

/**
 * @brief 创建列表控件 List，用于展示可点击的选项列表
 * @param parent    父容器
 * @param w         列表宽度
 * @param h         列表高度
 * @param align     对齐基准
 * @param x_ofs     X偏移
 * @param y_ofs     Y偏移
 * @return lv_obj_t* list 控件指针
 */
lv_obj_t* ui_widget_create_list(lv_obj_t* parent, int32_t w, int32_t h,
                                 lv_align_t align, int32_t x_ofs, int32_t y_ofs);

/**
 * @brief 向列表中添加一个按钮项
 * @param list  列表控件指针
 * @param txt   按钮文字
 * @param cb    点击回调 LV_EVENT_CLICKED，传 NULL 则忽略
 * @return lv_obj_t* 按钮控件指针
 */
lv_obj_t* ui_widget_list_add_button(lv_obj_t* list, const char* txt, lv_event_cb_t cb);

/* =======================================================================================================
 *  ui_slider  滑块控件封装
 * ======================================================================================================= */

/**
 * @brief 滑块控件上下文（包含滑块对象和数值显示标签）
 */
typedef struct {
    lv_obj_t *slider;   /**< 滑块对象 */
    lv_obj_t *label;    /**< 数值显示标签 */
    int32_t   min;      /**< 最小值 */
    int32_t   max;      /**< 最大值 */
    int32_t   value;    /**< 当前值 */
    char      unit[16]; /**< 单位文本（如 "fps", "%", "px"） */
} ui_slider_ctx_t;

/**
 * @brief 创建滑块控件（带数值显示标签）
 * @param parent 父容器
 * @param min 最小值
 * @param max 最大值
 * @param init_val 初始值
 * @param unit_text 单位文本（显示在数值后，如 "fps"），传NULL则不显示单位
 * @param align 对齐基准
 * @param x_ofs X偏移
 * @param y_ofs Y偏移
 * @param cb 值变化回调 LV_EVENT_VALUE_CHANGED，传NULL则忽略
 * @return ui_slider_ctx_t* 滑块上下文指针，失败返回NULL
 */
ui_slider_ctx_t* ui_widget_create_slider(lv_obj_t* parent,
                                         int32_t min, int32_t max, int32_t init_val,
                                         const char* unit_text,
                                         lv_align_t align, int32_t x_ofs, int32_t y_ofs,
                                         lv_event_cb_t cb);

/**
 * @brief 获取滑块当前值
 * @param ctx 滑块上下文
 * @return int32_t 当前值
 */
int32_t ui_widget_slider_get_value(ui_slider_ctx_t* ctx);

/**
 * @brief 设置滑块值并更新显示标签
 * @param ctx 滑块上下文
 * @param val 新值（自动限制在范围内）
 */
void ui_widget_slider_set_value(ui_slider_ctx_t* ctx, int32_t val);

/**
 * @brief 更新滑块显示标签（内部使用，也可外部调用刷新）
 * @param ctx 滑块上下文
 */
void ui_widget_slider_update_label(ui_slider_ctx_t* ctx);

/**
 * @brief 销毁滑块控件，释放资源
 * @param ctx 滑块上下文
 */
void ui_widget_slider_delete(ui_slider_ctx_t* ctx);

#endif