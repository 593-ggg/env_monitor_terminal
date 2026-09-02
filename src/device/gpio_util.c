#ifdef __linux__
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include "gpio_util.h"

static int gpio_file_write(const char *path, const char *buf)
{
    int fd = open(path, O_WRONLY);
    if (fd < 0)
    {
        return -1;
    }
    write(fd, buf, strlen(buf));
    close(fd);
    return 0;
}

void gpio_export(int pin)
{
    char buf[64];
    snprintf(buf, sizeof(buf), "%d", pin);
    gpio_file_write("/sys/class/gpio/export", buf);
    usleep(200000);
}

void gpio_unexport(int pin)
{
    char buf[64];
    snprintf(buf, sizeof(buf), "%d", pin);
    gpio_file_write("/sys/class/gpio/unexport", buf);
    usleep(100000);
}

void gpio_dir_out(int pin)
{
    char path[128];
    snprintf(path, sizeof(path), "/sys/class/gpio/gpio%d/direction", pin);
    gpio_file_write(path, "out");
}

void gpio_write(int pin, uint8_t val)
{
    char path[128];
    char buf[16];
    snprintf(path, sizeof(path), "/sys/class/gpio/gpio%d/value", pin);
    snprintf(buf, sizeof(buf), "%d", val);
    gpio_file_write(path, buf);
}

#endif