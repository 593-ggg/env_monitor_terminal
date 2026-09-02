#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include "beep.h"
#include "gpio_util.h"

#define BUZZER_PIN 111

void device_beep_init(void)
{
    gpio_export(BUZZER_PIN);
    gpio_dir_out(BUZZER_PIN);
    gpio_write(BUZZER_PIN, 0);
    printf("[BEEP] init ok\n");
}

void device_beep_deinit(void)
{
    gpio_write(BUZZER_PIN, 0);
    gpio_unexport(BUZZER_PIN);
    printf("[BEEP] deinit ok\n");
}

void device_beep_on(void)
{
    gpio_write(BUZZER_PIN, 1);
}

void device_beep_off(void)
{
    gpio_write(BUZZER_PIN, 0);
}

