#include "config_manager.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// 全局应用配置（任何模块可通过 config_get_global() 访问）
static AppConfig g_app_config;

// 键名（与配置文件中的 key 对应）
#define KEY_SERVER_PORT "server_port"
#define KEY_SERVER_IP "server_display_ip"
#define KEY_SERVER_UPLOAD_DIR "server_upload_dir"
#define KEY_CAMERA_FPS "camera_fps"
#define KEY_CAMERA_SCALE "camera_scale"
#define KEY_PHOTO_PATH "photo_save_path"
#define KEY_AI_MODEL_PATH "ai_model_path"
#define KEY_AI_CONFIDENCE "ai_confidence"
#define KEY_LOGIN_PASSWORD "login_password"
#define KEY_ENV_MONITOR_ENABLED "env_monitor_enabled"

void config_set_defaults(AppConfig *cfg)
{
    if (!cfg)
        return;
    memset(cfg, 0, sizeof(AppConfig));
    cfg->server_port = DEF_SERVER_PORT;
    strncpy(cfg->server_display_ip, DEF_SERVER_DISPLAY_IP, sizeof(cfg->server_display_ip) - 1);
    strncpy(cfg->server_upload_dir, DEF_SERVER_UPLOAD_DIR, sizeof(cfg->server_upload_dir) - 1);
    cfg->camera_fps = DEF_CAMERA_FPS;
    cfg->camera_scale = DEF_CAMERA_SCALE;
    strncpy(cfg->photo_save_path, DEF_PHOTO_PATH, sizeof(cfg->photo_save_path) - 1);
    strncpy(cfg->ai_model_path, DEF_AI_MODEL_PATH, sizeof(cfg->ai_model_path) - 1);
    cfg->ai_confidence = DEF_AI_CONFIDENCE;
    strncpy(cfg->login_password, DEF_LOGIN_PASSWORD, sizeof(cfg->login_password) - 1);
    cfg->env_monitor_enabled = DEF_ENV_MONITOR_ENABLED;
}

void config_get_camera_resolution(int scale, int *out_w, int *out_h)
{
    if (scale < CAMERA_SCALE_MIN)
        scale = CAMERA_SCALE_MIN;
    if (scale > CAMERA_SCALE_MAX)
        scale = CAMERA_SCALE_MAX;
    *out_w = (CAMERA_MAX_WIDTH * scale) / 100;
    *out_h = (CAMERA_MAX_HEIGHT * scale) / 100;
}

float config_get_ai_confidence(int percent)
{
    if (percent < AI_CONF_MIN)
        percent = AI_CONF_MIN;
    if (percent > AI_CONF_MAX)
        percent = AI_CONF_MAX;
    return (float) percent / 100.0f;
}

/**
 * @brief 解析一行 "key=value" 格式，跳过空行和 # 注释
 * @return 0 有效键值对，-1 空行/注释，-2 格式错误
 */
static int parse_line(const char *line, char *key, char *value)
{
    // 跳过前导空格
    while (*line == ' ' || *line == '\t')
        line++;

    // 空行或注释
    if (*line == '\0' || *line == '\n' || *line == '#')
        return -1;

    // 查找 '='
    const char *eq = strchr(line, '=');
    if (!eq)
        return -2;

    // 提取 key
    size_t klen = eq - line;
    if (klen >= 64)
        klen = 63;
    strncpy(key, line, klen);
    key[klen] = '\0';

    // 去除 key 尾部空格
    while (klen > 0 && (key[klen - 1] == ' ' || key[klen - 1] == '\t'))
        key[--klen] = '\0';

    // 提取 value（跳过 '=' 后空格）
    const char *v = eq + 1;
    while (*v == ' ' || *v == '\t')
        v++;

    // 去除尾部换行/空格
    size_t vlen = strlen(v);
    while (vlen > 0 && (v[vlen - 1] == '\n' || v[vlen - 1] == '\r' || v[vlen - 1] == ' ' || v[vlen - 1] == '\t'))
        vlen--;

    if (vlen >= 256)
        vlen = 255;
    strncpy(value, v, vlen);
    value[vlen] = '\0';

    return 0;
}

int config_load(const char *filepath, AppConfig *cfg)
{
    if (!cfg)
        return -1;
    config_set_defaults(cfg);

    const char *path = filepath ? filepath : CONFIG_FILE_PATH;
    FILE *fp = fopen(path, "r");
    if (!fp)
    {
        // 文件不存在，用默认值，返回 -1 让调用方知道是首次创建
        return -1;
    }

    char line[512];
    char key[64], value[256];
    while (fgets(line, sizeof(line), fp))
    {
        int ret = parse_line(line, key, value);
        if (ret != 0)
            continue;

        if (strcmp(key, KEY_SERVER_PORT) == 0)
            cfg->server_port = atoi(value);
        else if (strcmp(key, KEY_SERVER_IP) == 0)
        {
            strncpy(cfg->server_display_ip, value, sizeof(cfg->server_display_ip) - 1);
            cfg->server_display_ip[sizeof(cfg->server_display_ip) - 1] = '\0';
        }
        else if (strcmp(key, KEY_SERVER_UPLOAD_DIR) == 0)
        {
            strncpy(cfg->server_upload_dir, value, sizeof(cfg->server_upload_dir) - 1);
            cfg->server_upload_dir[sizeof(cfg->server_upload_dir) - 1] = '\0';
        }
        else if (strcmp(key, KEY_CAMERA_FPS) == 0)
            cfg->camera_fps = atoi(value);
        else if (strcmp(key, KEY_CAMERA_SCALE) == 0)
            cfg->camera_scale = atoi(value);
        else if (strcmp(key, KEY_PHOTO_PATH) == 0)
        {
            strncpy(cfg->photo_save_path, value, sizeof(cfg->photo_save_path) - 1);
            cfg->photo_save_path[sizeof(cfg->photo_save_path) - 1] = '\0';
        }
        else if (strcmp(key, KEY_AI_MODEL_PATH) == 0)
        {
            strncpy(cfg->ai_model_path, value, sizeof(cfg->ai_model_path) - 1);
            cfg->ai_model_path[sizeof(cfg->ai_model_path) - 1] = '\0';
        }
        else if (strcmp(key, KEY_AI_CONFIDENCE) == 0)
            cfg->ai_confidence = atoi(value);
        else if (strcmp(key, KEY_LOGIN_PASSWORD) == 0)
        {
            strncpy(cfg->login_password, value, sizeof(cfg->login_password) - 1);
            cfg->login_password[sizeof(cfg->login_password) - 1] = '\0';
        }
        else if (strcmp(key, KEY_ENV_MONITOR_ENABLED) == 0)
            cfg->env_monitor_enabled = atoi(value);
    }
    fclose(fp);

    // 范围限制
    if (cfg->server_port < SERVER_PORT_MIN || cfg->server_port > SERVER_PORT_MAX)
        cfg->server_port = DEF_SERVER_PORT;
    if (cfg->camera_fps < CAMERA_FPS_MIN || cfg->camera_fps > CAMERA_FPS_MAX)
        cfg->camera_fps = DEF_CAMERA_FPS;
    if (cfg->camera_scale < CAMERA_SCALE_MIN || cfg->camera_scale > CAMERA_SCALE_MAX)
        cfg->camera_scale = DEF_CAMERA_SCALE;
    if (cfg->ai_confidence < AI_CONF_MIN || cfg->ai_confidence > AI_CONF_MAX)
        cfg->ai_confidence = DEF_AI_CONFIDENCE;
    // 密码长度限制: 太短则恢复默认
    {
        size_t pwd_len = strlen(cfg->login_password);
        if (pwd_len < LOGIN_PWD_MIN_LEN || pwd_len > LOGIN_PWD_MAX_LEN)
        {
            strncpy(cfg->login_password, DEF_LOGIN_PASSWORD, sizeof(cfg->login_password) - 1);
            cfg->login_password[sizeof(cfg->login_password) - 1] = '\0';
        }
    }

    return 0;
}

int config_save(const char *filepath, const AppConfig *cfg)
{
    if (!cfg)
        return -1;

    const char *path = filepath ? filepath : CONFIG_FILE_PATH;
    FILE *fp = fopen(path, "w");
    if (!fp)
        return -1;

    fprintf(fp, "# ============================================\n");
    fprintf(fp, "# 系统配置文件 - 自动生成，请勿手动修改\n");
    fprintf(fp, "# ============================================\n\n");

    fprintf(fp, "[server]\n");
    fprintf(fp, "%s=%d\n", KEY_SERVER_PORT, cfg->server_port);
    fprintf(fp, "%s=%s\n", KEY_SERVER_IP, cfg->server_display_ip);
    fprintf(fp, "%s=%s\n\n", KEY_SERVER_UPLOAD_DIR, cfg->server_upload_dir);

    fprintf(fp, "[camera]\n");
    fprintf(fp, "%s=%d\n", KEY_CAMERA_FPS, cfg->camera_fps);
    fprintf(fp, "%s=%d\n", KEY_CAMERA_SCALE, cfg->camera_scale);
    fprintf(fp, "%s=%s\n\n", KEY_PHOTO_PATH, cfg->photo_save_path);

    fprintf(fp, "[ai]\n");
    fprintf(fp, "%s=%s\n", KEY_AI_MODEL_PATH, cfg->ai_model_path);
    fprintf(fp, "%s=%d\n\n", KEY_AI_CONFIDENCE, cfg->ai_confidence);

    fprintf(fp, "[security]\n");
    fprintf(fp, "%s=%s\n", KEY_LOGIN_PASSWORD, cfg->login_password);

    fprintf(fp, "\n[env_monitor]\n");
    fprintf(fp, "%s=%d\n", KEY_ENV_MONITOR_ENABLED, cfg->env_monitor_enabled);

    fclose(fp);
    return 0;
}

AppConfig *config_get_global(void)
{
    return &g_app_config;
}
