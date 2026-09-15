#include "../include/systemtime.h"
#include <string.h>

void system_time_get_string(char *buffer, size_t buffer_size)
{
    time_t now = time(NULL);
    struct tm *tm_info = localtime(&now);
    tm_info->tm_hour = (tm_info->tm_hour + 8) % 24;
    strftime(buffer, buffer_size, "%H:%M", tm_info);
}