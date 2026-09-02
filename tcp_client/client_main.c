#include "tcp_client.h"
#include <signal.h>
#include <stdio.h>

int main(int argc, char* argv[])
{
    if (argc > 1)
    {
        server_ip = argv[1];
    }

    if (argc > 2)
    {
        server_port = atoi(argv[2]);
    }

    signal(SIGPIPE, SIG_IGN);
    ClientConn* cli = client_create();
    if (!cli)
    {
        printf("client create failed\n");
        return -1;
    }

    char input_buf[512];

    while (1)
    {
        if (!cli->running)
        {
            break;
        }
        if (fgets(input_buf, sizeof(input_buf), stdin) == NULL)
            break;
        // 去除换行符
        size_t len = strlen(input_buf);
        if (len > 0 && input_buf[len - 1] == '\n')
            input_buf[len - 1] = '\0';

        if (input_buf[0] == '\0')
        {
            printf("%s: ", cli->id);
            fflush(stdout);
            continue;
        }

        if (strcmp(input_buf, "exit") == 0)
            break;

        if (strcmp(input_buf, "help") == 0)
        {
            printf("===== Commands =====\n");
            printf("id xxx            set client id\n");
            printf("msg @user xxx         send private text msg\n");
            printf("msg xxx               broadcast text msg\n");
            printf("heartbeat             send heartbeat\n");
            printf("file @user filepath   send file to user\n");
            printf("file_accept [path]    accept file transfer (optional path, defaults to ./)\n");
            printf("file_reject           reject file transfer\n");
            printf("upload filepath [subdir]  upload file to server (optional subdir, e.g. photo/)\n");
            printf("download file [path]  download file from server\n");
            printf("files                 list files on server\n");
            printf("yolov8 in [out]       YOLOv8 process file on server (optional out, auto-names *_yolov8)\n");
            printf("led idx on|off        control LED (idx 0-3), e.g. led 0 on / led 3 off\n");
            printf("beep on|off           control buzzer, e.g. beep on / beep off\n");
            printf("whoami                show your id\n");
            printf("list                  show online clients\n");
            printf("help                  show this help\n");
            printf("exit                  quit client\n");
            printf("====================\n");
            printf("%s: ", cli->id);
            fflush(stdout);
            continue;
        }

        if (strcmp(input_buf, "whoami") == 0)
        {
            printf("your id: %s\n", cli->id);
            printf("%s: ", cli->id);
            fflush(stdout);
            continue;
        }

        int ret = client_input_cmd(cli, input_buf);
        if (ret != 0)
        {
            printf("input command invalid\n");
        }
        printf("%s: ", cli->id);
        fflush(stdout);
    }

    client_destroy(cli);
    return 0;
}
