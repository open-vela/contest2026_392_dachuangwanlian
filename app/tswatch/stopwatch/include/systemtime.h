#ifndef SYSTEMTIME_H
#define SYSTEMTIME_H

#include <stdint.h>
#include <stdbool.h>
#include <lvgl/lvgl.h>
#include <time.h>

void system_time_get_string(char *buffer, size_t buffer_size);

#endif

