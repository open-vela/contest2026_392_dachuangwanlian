#ifndef ALARM_PRESENTER_H
#define ALARM_PRESENTER_H

#include <stdbool.h>

void alarm_presenter_init();
void alarm_presenter_update_time();
void alarm_presenter_add_alarm(const char* hour, const char* minute);
void alarm_presenter_add_alarm_with_mode(const char* hour, const char* minute, const char* mode);
void alarm_presenter_remove_alarm(int index);
int alarm_presenter_remove_alarm_by_time(const char* hour, const char* minute);
void alarm_presenter_select_alarm(int index, bool selected);
void alarm_presenter_remove_selected_alarms();
void alarm_presenter_set_long_pressed(bool long_pressed);

#endif