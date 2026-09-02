#include "ui.h"
#include "ui_widget.h"
#include "config.h"
#include "config_manager.h"
#include "thread.h"
#include "tcp_server.h"
#include "my_font.h"
#include <stdio.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <stdlib.h>
#include <unistd.h>

// 客户端列表最大显示行数
#define MAX_CLIENT_ROWS  64

// 服务器界面屏幕
static lv_obj_t *server_scr = NULL;

// 服务器信息标签
static lv_obj_t *lbl_server_ip   = NULL;
static lv_obj_t *lbl_server_port = NULL;
static lv_obj_t *lbl_server_stat = NULL;

// 服务器开关按钮
static lv_obj_t *btn_server_switch = NULL;

// 客户端列表容器（可滚动box）
static lv_obj_t *client_list     = NULL;
// 客户端数量标签
static lv_obj_t *lbl_client_count = NULL;

// 上传文件列表控件
static lv_obj_t *file_list = NULL;

// 刷新定时器
static lv_timer_t *refresh_timer = NULL;

// 当前列表中存储的客户端ID（供断开按钮回调使用）
static char s_client_ids[MAX_CLIENT_ROWS][64];
static int  s_client_count = 0;

// 全局共享选中文件名
static char g_sel_file[256] = {0};
// 文件操作弹窗句柄，复用不重复创建
static lv_obj_t *g_file_op_win = NULL;
// 重命名输入框
static lv_obj_t *g_ta_rename = NULL;
// 当前浏览的子目录（相对上传目录，如 "photo/" 或 ""）
static char g_current_subdir[256] = {0};
// 选中的是目录还是文件
static bool g_sel_is_dir = false;

static void ui_server_refresh(void);
static void ui_server_scan_uploads(void);

/* ==================== 递归删除目录辅助函数 ==================== */
static int remove_dir_recursive(const char *path)
{
    DIR *dir = opendir(path);
    if (!dir)
        return remove(path);  // 非目录或不存在，尝试直接删除文件

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL)
    {
        // 跳过 "." 和 ".."
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
            continue;

        char child[512];
        snprintf(child, sizeof(child), "%s/%s", path, entry->d_name);

        struct stat st;
        if (lstat(child, &st) == 0)
        {
            if (S_ISDIR(st.st_mode))
                remove_dir_recursive(child);  // 递归删除子目录
            else
                unlink(child);                // 删除文件/符号链接
        }
    }
    closedir(dir);
    return rmdir(path);  // 目录已清空，删除自身
}


/**
 * @brief 获取服务端上传目录（从配置读取，带尾部斜杠）
 */
static const char* get_server_upload_dir(void)
{
    AppConfig* cfg = config_get_global();
    if (cfg && cfg->server_upload_dir[0])
        return cfg->server_upload_dir;
    return DEF_SERVER_UPLOAD_DIR;
}

static void file_op_all_cb(lv_event_t *e)
{
    uintptr_t tag = (uintptr_t)lv_event_get_user_data(e);
    lv_obj_t *btn = lv_event_get_target(e);
    lv_obj_t *win = lv_obj_get_parent(btn);
    lv_obj_t *mask = lv_obj_get_parent(win);

    // 0=取消：直接关闭弹窗销毁
    if(tag == 0)
    {
        lv_obj_del(mask);
        g_file_op_win = NULL;
        memset(g_sel_file,0,sizeof(g_sel_file));
        g_ta_rename = NULL;
        return;
    }

    // 1=重命名
    if(tag == 1)
    {
        // 关闭选择弹窗
        lv_obj_del(mask);
        g_file_op_win = NULL;

        // 创建重命名弹窗
        lv_obj_t *rename_win = ui_widget_create_popup(
            420,300,
            COLOR_GRAY_DARK,
            true
        );
        lv_obj_t *rm_mask = lv_obj_get_parent(rename_win);
        lv_obj_clear_flag(rm_mask, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(rename_win, LV_OBJ_FLAG_HIDDEN);

        ui_widget_create_label(
            rename_win, 
            "重命名文件", 
            LV_ALIGN_TOP_MID,
            0,10,
            FONT_TITLE,
            lv_color_white()
        );
        char tip[256];
        snprintf(tip,sizeof(tip),"原名：%s",g_sel_file);
        ui_widget_create_label(
            rename_win, 
            tip, 
            LV_ALIGN_TOP_LEFT,
            10,50,
            FONT_BODY, 
            lv_color_white()
        );

        // 绑定全局共用键盘
        g_ta_rename = ui_widget_create_textarea(
            rename_win,
            380,40,
            LV_ALIGN_TOP_LEFT,
            10,90,
            kb
        );
        lv_textarea_set_text(g_ta_rename, g_sel_file);

        // 确认/取消按钮
        ui_widget_create_btn(
            rename_win, 
            "确认",
            120,40,
            COLOR_GREEN_LIGHT,
            COLOR_GREEN_DARK,
            LV_ALIGN_BOTTOM_LEFT,
            20,-10,
            101,
            file_op_all_cb
        );
        ui_widget_create_btn(
            rename_win, 
            "取消",
            120,40,
            COLOR_GRAY_LIGHT,
            COLOR_GRAY_DARK,
            LV_ALIGN_BOTTOM_RIGHT,
            -20,-10,
            0,
            file_op_all_cb
        );
        return;
    }

    // 2=删除，弹出二次确认
    if(tag == 2)
    {
        lv_obj_del(mask);
        g_file_op_win = NULL;

        lv_obj_t *del_win = ui_widget_create_popup(
            400,180,
            COLOR_GRAY_DARK,
            true
        );
        lv_obj_t *del_mask = lv_obj_get_parent(del_win);
        lv_obj_clear_flag(del_mask, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(del_win, LV_OBJ_FLAG_HIDDEN);

        char tip[256];
        if (g_sel_is_dir)
            snprintf(tip,sizeof(tip),"确定删除整个目录 %s ?",g_sel_file);
        else
            snprintf(tip,sizeof(tip),"确定删除 %s ?",g_sel_file);
        ui_widget_create_label(
            del_win, 
            tip, 
            LV_ALIGN_TOP_MID,
            0,30,
            FONT_BODY, 
            lv_color_white()
        );

        ui_widget_create_btn(
            del_win, 
            "确认删除",
            130,40,
            COLOR_RED_LIGHT,
            COLOR_RED_DARK,
            LV_ALIGN_BOTTOM_LEFT,
            30,-10,
            201,
            file_op_all_cb
        );
        ui_widget_create_btn(
            del_win, 
            "取消",
            130,40,
            COLOR_GRAY_LIGHT,
            COLOR_GRAY_DARK,
            LV_ALIGN_BOTTOM_RIGHT,
            -30,-10,
            0,
            file_op_all_cb  
        );
        return;
    }

    // 3=进入目录
    if(tag == 3)
    {
        lv_obj_del(mask);
        g_file_op_win = NULL;
        // 拼接新路径
        size_t len = strlen(g_current_subdir);
        snprintf(g_current_subdir + len, sizeof(g_current_subdir) - len, "%s/", g_sel_file);
        memset(g_sel_file, 0, sizeof(g_sel_file));
        ui_server_scan_uploads();
        return;
    }

    // 101：重命名确认
    if(tag == 101)
    {
        const char *new_name = lv_textarea_get_text(g_ta_rename);
        if(strlen(new_name) == 0 || strcmp(new_name, g_sel_file)==0)
        {
            lv_obj_del(mask);
            memset(g_sel_file,0,sizeof(g_sel_file));
            return;
        }
        char old_path[512],new_path[512];
        snprintf(old_path,sizeof(old_path),"%s%s%s",get_server_upload_dir(),g_current_subdir,g_sel_file);
        snprintf(new_path,sizeof(new_path),"%s%s%s",get_server_upload_dir(),g_current_subdir,new_name);
        rename(old_path, new_path);

        lv_obj_del(mask);
        memset(g_sel_file,0,sizeof(g_sel_file));
        ui_server_scan_uploads(); // 刷新文件列表
        return;
    }

    // 201：删除确认
    if(tag == 201)
    {
        char path[512];
        snprintf(path,sizeof(path),"%s%s%s",get_server_upload_dir(),g_current_subdir,g_sel_file);
        if (g_sel_is_dir)
        {
            // 删除整个目录（递归，使用 nftw 避免 system() 命令注入风险）
            remove_dir_recursive(path);
        }
        else
        {
            remove(path);
        }

        lv_obj_del(mask);
        memset(g_sel_file,0,sizeof(g_sel_file));
        ui_server_scan_uploads();
        return;
    }
}

// 点击文件列表项，弹出操作选择弹窗
static void file_item_click_cb(lv_event_t *e)
{
    lv_obj_t *btn = lv_event_get_target(e);
    lv_obj_t *lab = lv_obj_get_child(btn, 0);
    const char *fname = lv_label_get_text(lab);

    // 处理"返回上级"
    if (strcmp(fname, "../") == 0)
    {
        // 去掉最后一级目录
        size_t len = strlen(g_current_subdir);
        if (len > 0)
        {
            // 去掉末尾的 '/'
            if (g_current_subdir[len - 1] == '/')
                g_current_subdir[len - 1] = '\0';
            // 找到最后一个 '/'
            char* last_slash = strrchr(g_current_subdir, '/');
            if (last_slash)
                *(last_slash + 1) = '\0';  // 保留末尾 '/'
            else
                g_current_subdir[0] = '\0';  // 回到根目录
        }
        ui_server_scan_uploads();
        return;
    }

    // 处理"[DIR] xxx"格式，提取纯目录名
    const char* real_name = fname;
    if (strncmp(fname, "[DIR] ", 6) == 0)
        real_name = fname + 6;
    strncpy(g_sel_file, real_name, sizeof(g_sel_file)-1);
    g_sel_file[sizeof(g_sel_file)-1] = 0;

    // 判断选中的是目录还是文件
    char full_path[512];
    snprintf(full_path, sizeof(full_path), "%s%s%s", get_server_upload_dir(), g_current_subdir, g_sel_file);
    struct stat st;
    g_sel_is_dir = (stat(full_path, &st) == 0 && S_ISDIR(st.st_mode));

    // 弹窗已存在先销毁，避免多份堆积
    if(g_file_op_win)
    {
        lv_obj_del(lv_obj_get_parent(g_file_op_win));
        g_file_op_win = NULL;
    }
    // 创建弹窗 宽400高200，带关闭叉号
    g_file_op_win = ui_widget_create_popup(
        400, 200,
        COLOR_GRAY_DARK,
        true
    );
    lv_obj_t *mask = lv_obj_get_parent(g_file_op_win);
    lv_obj_clear_flag(mask, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(g_file_op_win, LV_OBJ_FLAG_HIDDEN);

    // 标题
    char buf[256];
    if (g_sel_is_dir)
        snprintf(buf, sizeof(buf), "目录操作：%s", g_sel_file);
    else
        snprintf(buf, sizeof(buf), "文件操作：%s", g_sel_file);
    ui_widget_create_label(
        g_file_op_win, 
        buf, 
        LV_ALIGN_TOP_MID,
        0,20,
        FONT_BODY, 
        lv_color_white()
    );

    if (g_sel_is_dir)
    {
        // 目录操作：进入 / 删除
        ui_widget_create_btn(
            g_file_op_win, 
            "进入",
            90,40,
            COLOR_GREEN_LIGHT,
            COLOR_GREEN_DARK,
            LV_ALIGN_BOTTOM_LEFT,
            30,-15,
            3,
            file_op_all_cb
        );
        ui_widget_create_btn(
            g_file_op_win, 
            "删除",
            90,40,
            COLOR_RED_LIGHT,
            COLOR_RED_DARK,
            LV_ALIGN_BOTTOM_MID,
            0,-15,
            2,
            file_op_all_cb
        );
        ui_widget_create_btn(
            g_file_op_win, 
            "取消",
            90,40,
            COLOR_GRAY_LIGHT,
            COLOR_GRAY_DARK,
            LV_ALIGN_BOTTOM_RIGHT,
            -30,-15,
            0,
            file_op_all_cb
        );
    }
    else
    {
        // 文件操作：重命名 / 删除 / 取消
        ui_widget_create_btn(
            g_file_op_win, 
            "重命名",
            70,40,
            COLOR_BLUE_LIGHT,
            COLOR_BLUE_DARK,
            LV_ALIGN_BOTTOM_LEFT,
            40,-15,
            1,
            file_op_all_cb
        );
        ui_widget_create_btn(
            g_file_op_win, 
            "删除",
            70,40,
            COLOR_RED_LIGHT,
            COLOR_RED_DARK,
            LV_ALIGN_BOTTOM_MID,
            0,-15,
            2,
            file_op_all_cb
        );
        ui_widget_create_btn(
            g_file_op_win, 
            "取消",
            70,40,
            COLOR_GRAY_LIGHT,
            COLOR_GRAY_DARK,
            LV_ALIGN_BOTTOM_RIGHT,
            -40,-15,
            0,
            file_op_all_cb
        );
    }
}

/**
 * @brief 服务器开关按钮回调
 *         根据当前服务器运行状态自动判断启动或关闭
 */
static void ui_server_switch_cb(lv_event_t *e)
{
    (void)e;
    if (tcp_server_is_running())
    {
        tcp_server_parse_ui_cmd(TCP_UI_CMD_CLOSE);
    }
    else
    {
        tcp_server_parse_ui_cmd(TCP_UI_CMD_START);
    }
    // 立即刷新UI反映状态变化
    ui_server_refresh();
}

/**
 * @brief 断开客户端按钮回调
 *         tag = 客户端在s_client_ids数组中的索引
 */
static void ui_server_disconnect_cb(lv_event_t *e)
{
    uintptr_t tag = (uintptr_t)lv_event_get_user_data(e);
    if (tag < (uintptr_t)s_client_count)
    {
        tcp_server_disconnect_client(s_client_ids[tag]);
    }
}

/**
 * @brief 刷新服务器信息区和客户端列表
 */
static void ui_server_refresh(void)
{
    // 从全局配置读取最新值
    AppConfig* cfg = config_get_global();
    if (cfg && lbl_server_ip && lbl_server_port)
    {
        lv_label_set_text(lbl_server_ip, cfg->server_display_ip);
        char port_buf[16];
        snprintf(port_buf, sizeof(port_buf), "%d", cfg->server_port);
        lv_label_set_text(lbl_server_port, port_buf);
    }

    // 更新服务器状态
    bool running = tcp_server_is_running();
    // 按钮内部子label（ui_widget_create_btn创建的第一个子对象）
    lv_obj_t *btn_label = lv_obj_get_child(btn_server_switch, 0);
    if (running)
    {
        lv_label_set_text(lbl_server_stat, "运行中");
        lv_obj_set_style_text_color(lbl_server_stat, COLOR_GREEN_NORMAL, LV_PART_MAIN);
        if (btn_label) lv_label_set_text(btn_label, "关闭服务器");
    }
    else
    {
        lv_label_set_text(lbl_server_stat, "已停止");
        lv_obj_set_style_text_color(lbl_server_stat, COLOR_RED_NORMAL, LV_PART_MAIN);
        if (btn_label) lv_label_set_text(btn_label, "启动服务器");
    }

    // 服务器未运行时清空客户端列表
    if (!running)
    {
        s_client_count = 0;
        lv_obj_clean(client_list);
        // 重建数量标签（lv_obj_clean会删除所有子对象）
        lbl_client_count = ui_widget_create_label(
            client_list, "在线客户端: 0",
            LV_ALIGN_TOP_LEFT, 0, 0,
            FONT_BODY, COLOR_TEXT_DARK
        );
        ui_server_scan_uploads();
        return;
    }

    // 查询在线客户端列表
    ClientInfo info_buf[MAX_CLIENT_ROWS];
    int n = client_mgr_get_info_list(info_buf, MAX_CLIENT_ROWS);
    s_client_count = n;

    // 清空列表重建
    lv_obj_clean(client_list);

    // 重建数量标签
    char count_buf[64];
    snprintf(count_buf, sizeof(count_buf), "在线客户端: %d", n);
    lbl_client_count = ui_widget_create_label(
        client_list, count_buf,
        LV_ALIGN_TOP_LEFT, 0, 0,
        FONT_BODY, COLOR_TEXT_DARK
    );

    for (int i = 0; i < n; i++)
    {
        // 保存ID供断开回调使用
        strncpy(s_client_ids[i], info_buf[i].id, sizeof(s_client_ids[i]) - 1);
        s_client_ids[i][sizeof(s_client_ids[i]) - 1] = '\0';

        // 每行容器（LV_ALIGN_TOP_LEFT: 紧贴标签下方, 垂直从顶部开始排列）
        lv_obj_t *row = ui_widget_create_box(
            client_list, 
            LV_PCT(100), 40, 
            LV_ALIGN_TOP_LEFT, 
            0, 35 + i * 45,
            COLOR_GRAY_LIGHT, 
            -1, 
            true
        );

        // 文本标签：序号. ID    IP:Port
        char txt[128];
        snprintf(txt, sizeof(txt), "%d. %s    %s:%d",
                 i + 1, info_buf[i].id, info_buf[i].ip, info_buf[i].port);
        ui_widget_create_label(
            row, txt,
            LV_ALIGN_LEFT_MID, 
            0, 0,
            FONT_BODY, 
            COLOR_TEXT_DARK
        );

        // 断开按钮（右侧独立按钮，不会误触）
        ui_widget_create_btn(
            row, "断开",
            70, 30,
            COLOR_RED_LIGHT, COLOR_RED_DARK,
            LV_ALIGN_RIGHT_MID, -5, 0,
            i,  // tag=行索引
            ui_server_disconnect_cb
        );
    }

    // 刷新上传文件列表
    ui_server_scan_uploads();
}

/**
 * @brief 扫描上传目录，列出文件和目录到右侧列表（支持目录浏览）
 */
static void ui_server_scan_uploads(void)
{
    if (file_list == NULL) return;

    lv_obj_clean(file_list);

    // 标题：显示当前路径
    char title[300];
    if (g_current_subdir[0] != '\0')
        snprintf(title, sizeof(title), "路径: /%s", g_current_subdir);
    else
        snprintf(title, sizeof(title), "上传文件列表");
    ui_widget_create_label(
        file_list, title,
        LV_ALIGN_TOP_LEFT, 0, 0,
        FONT_BODY, COLOR_TEXT_DARK
    );

    // 如果在子目录中，添加"返回上级"按钮
    if (g_current_subdir[0] != '\0')
    {
        lv_obj_t* btn = ui_widget_list_add_button(file_list, "../", file_item_click_cb);
        lv_obj_set_style_text_color(btn, lv_color_hex(0x0066CC), LV_PART_MAIN);
    }

    // 构造当前扫描路径
    char scan_path[512];
    snprintf(scan_path, sizeof(scan_path), "%s%s", get_server_upload_dir(), g_current_subdir);

    DIR *dir = opendir(scan_path);
    if (!dir)
    {
        ui_widget_list_add_button(file_list, "(无法打开目录)", NULL);
        return;
    }

    int count = 0;
    struct dirent *entry;
    // 先列目录，再列文件
    while ((entry = readdir(dir)) != NULL)
    {
        if (entry->d_name[0] == '.')
            continue;
        if (entry->d_type == DT_DIR)
        {
            char dir_label[300];
            snprintf(dir_label, sizeof(dir_label), "[DIR] %s", entry->d_name);
            lv_obj_t* btn = ui_widget_list_add_button(file_list, dir_label, file_item_click_cb);
            // 目录项用蓝色高亮
            lv_obj_set_style_text_color(btn, lv_color_hex(0x0066CC), LV_PART_MAIN);
            count++;
        }
    }
    rewinddir(dir);
    while ((entry = readdir(dir)) != NULL)
    {
        if (entry->d_name[0] == '.')
            continue;
        if (entry->d_type == DT_REG)
        {
            ui_widget_list_add_button(file_list, entry->d_name, file_item_click_cb);
            count++;
        }
    }
    closedir(dir);

    if (count == 0 && g_current_subdir[0] == '\0')
    {
        ui_widget_list_add_button(file_list, " (无文件)", NULL);
    }
}

/**
 * @brief 定时器回调：周期刷新服务器状态和客户端列表
 */
static void ui_server_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    ui_server_refresh();
}

/**
 * @brief 启动服务器界面定时器（2秒刷新）
 */
void ui_server_start_timers(void)
{
    if (refresh_timer == NULL)
    {
        refresh_timer = lv_timer_create(ui_server_timer_cb, 2000, NULL);
    }
}

/**
 * @brief 停止服务器界面所有定时器
 */
void ui_server_stop_timers(void)
{
    if (refresh_timer)
    {
        lv_timer_del(refresh_timer);
        refresh_timer = NULL;
    }
}

/**
 * @brief 返回/刷新按钮回调
 *         tag=0: 返回首页  tag=1: 手动刷新列表
 */
static void ui_server_btn_cb(lv_event_t *e)
{
    uintptr_t tag = (uintptr_t)lv_event_get_user_data(e);
    if (tag == 0)
    {
        ui_server_stop_timers();
        ui_home_scr_load();
    }
    else if (tag == 1)
    {
        // 手动刷新客户端列表
        ui_server_refresh();
    }
}

lv_obj_t* ui_server_scr_create(void)
{
    if (server_scr != NULL)
        return server_scr;

    server_scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(server_scr, COLOR_GRAY_LIGHT, 0);
    lv_obj_set_style_bg_opa(server_scr, LV_OPA_COVER, 0);

    // ============ 左侧：服务器信息面板 ============
    lv_obj_t *info_box = ui_widget_create_box(
        server_scr,
        772, 120,
        LV_ALIGN_TOP_LEFT,
        10, 10,
        COLOR_GRAY_DARK,
        10, 
        false
    );

    // 标题
    ui_widget_create_label(
        info_box, 
        "TCP 服务器",
        LV_ALIGN_TOP_LEFT, 
        0, 0,
        FONT_TITLE, 
        COLOR_TEXT_LIGHT
    );

    // IP地址
    ui_widget_create_label(
        info_box, 
        "IP:",
        LV_ALIGN_TOP_LEFT, 
        0, 40,
        FONT_BODY, 
        COLOR_TEXT_LIGHT
    );
    // 从配置获取初始显示IP和端口
    AppConfig* init_cfg = config_get_global();
    char init_port_buf[16];
    snprintf(init_port_buf, sizeof(init_port_buf), "%d", init_cfg ? init_cfg->server_port : 8888);
    const char* init_ip = init_cfg ? init_cfg->server_display_ip : "0.0.0.0";

    lbl_server_ip = ui_widget_create_label(
        info_box, 
        init_ip,
        LV_ALIGN_TOP_LEFT, 
        35, 40,
        FONT_BODY, 
        COLOR_TEXT_LIGHT
    );

    // 端口
    ui_widget_create_label(
        info_box, 
        "端口:",
        LV_ALIGN_TOP_LEFT, 
        200, 40,
        FONT_BODY, 
        COLOR_TEXT_LIGHT
    );
    lbl_server_port = ui_widget_create_label(
        info_box, 
        init_port_buf,
        LV_ALIGN_TOP_LEFT, 
        250, 40,
        FONT_BODY, 
        COLOR_TEXT_LIGHT
    );

    // 状态
    ui_widget_create_label(
        info_box, 
        "状态:",
        LV_ALIGN_TOP_LEFT, 
        350, 40,
        FONT_BODY, 
        COLOR_TEXT_LIGHT
    );
    lbl_server_stat = ui_widget_create_label(
        info_box, 
        "已停止",
        LV_ALIGN_TOP_LEFT, 
        400, 40,
        FONT_BODY, 
        COLOR_RED_NORMAL
    );

    // ============ 左侧：客户端列表面板（可滚动容器） ============
    client_list = ui_widget_create_box(
        server_scr,
        772, 459,
        LV_ALIGN_TOP_LEFT,
        10, 140,
        COLOR_GRAY_LIGHTEST,
        8, 
        true   // 可滚动
    );

    // 客户端列表标题
    lbl_client_count = ui_widget_create_label(
        client_list, 
        "在线客户端: 0",
        LV_ALIGN_TOP_LEFT, 
        0, 0,
        FONT_BODY, 
        COLOR_TEXT_DARK
    );

    // ============ 右侧：操作按钮区 ============

    // 返回按钮
    ui_widget_create_btn(
        server_scr, 
        "返回",
        200, 50,
        COLOR_BLUE_LIGHT, COLOR_BLUE_DARK,
        LV_ALIGN_TOP_LEFT, 
        800, 10,
        0, 
        ui_server_btn_cb
    );

    // 服务器开关按钮
    btn_server_switch = ui_widget_create_btn(
        server_scr, 
        "启动服务器",
        200, 50,
        COLOR_GREEN_LIGHT, COLOR_GREEN_DARK,
        LV_ALIGN_TOP_LEFT, 
        800, 80,
        0, 
        ui_server_switch_cb
    );

    // 手动刷新按钮
    ui_widget_create_btn(
        server_scr, 
        "刷新列表",
        200, 50,
        COLOR_BLUE_LIGHT, COLOR_BLUE_DARK,
        LV_ALIGN_TOP_LEFT, 
        800, 150,
        1, 
        ui_server_btn_cb    
    );

    // ============ 右侧下方：上传文件列表 ============
    // 文件列表（可滚动）
    file_list = ui_widget_create_list(
        server_scr,
        200, 370,
        LV_ALIGN_TOP_LEFT,
        800, 220
    );

    // 首次刷新
    ui_server_refresh();

    return server_scr;
}

void ui_server_scr_load(void)
{
    if (server_scr)
    {
        lv_screen_load_anim(
            server_scr,
            LV_SCR_LOAD_ANIM_MOVE_LEFT,
            300, 0, false
        );
        ui_server_start_timers();
    }
}
