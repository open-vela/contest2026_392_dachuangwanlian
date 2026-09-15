#include <string.h>
#include <time.h>
#include <stdio.h>
#include "../include/alarm_model.h"
#include "../include/alarm_view.h"
#include "../include/alarm_presenter.h"


// static AlarmModel alarm_model;
static AlarmModel alarm_model = {0};

void alarm_model_init() {
    AlarmModel* model = get_alarm_model();
    static bool initialized = false;
    if (!initialized) {
        memset(model, 0, sizeof(AlarmModel));
        initialized = true;
    }
}

void alarm_model_get_current_time() {
    time_t rawtime = time(NULL);
    struct tm *timeinfo = localtime(&rawtime);
    timeinfo->tm_hour = (timeinfo->tm_hour + 8) % 24;
    strftime(alarm_model.current_time, sizeof(alarm_model.current_time), "%H:%M", timeinfo);
}

bool alarm_model_add_alarm(const char* time_str) {
    return alarm_model_add_alarm_with_mode(time_str, "每天");
}

bool alarm_model_add_alarm_with_mode(const char* time_str, const char* mode) {
    if (alarm_model.alarm_count >= MAX_ALARM_COUNT) return false;

    AlarmData* alarm = &alarm_model.alarms[alarm_model.alarm_count];

    strncpy(alarm->time, time_str, sizeof(alarm->time));
    strncpy(alarm->mode, mode ? mode : "每天", sizeof(alarm->mode) - 1);
    alarm->mode[sizeof(alarm->mode) - 1] = '\0';
    alarm->selected = false;
    alarm_model.alarm_count++;
    return true;
}

int alarm_model_find_alarm(const char* time_str) {
    if (!time_str) return -1;
    for (int i = 0; i < alarm_model.alarm_count; i++) {
        if (strcmp(alarm_model.alarms[i].time, time_str) == 0) {
            return i;
        }
    }
    return -1;
}

bool alarm_model_remove_alarm(int index) {
    if (index < 0 || index >= MAX_ALARM_COUNT) return false;
    
    // 将后面的闹钟前移
    for (int i = index; i < MAX_ALARM_COUNT - 1; i++) {
        alarm_model.alarms[i] = alarm_model.alarms[i + 1];
    }
    
    alarm_model.alarm_count--;
    return true;
}

void alarm_model_select_alarm(int index, bool selected) {
    if (index < 0 || index >= MAX_ALARM_COUNT) return;
    
    if (selected && !alarm_model.alarms[index].selected) {
        alarm_model.del_alarm_count++;
    } else if (!selected && alarm_model.alarms[index].selected) {
        alarm_model.del_alarm_count--;
    }
    
    alarm_model.alarms[index].selected = selected;
}

void alarm_model_remove_selected_alarms() {

    for (int i = MAX_ALARM_COUNT - 1; i >= 0; i--) {
        if (alarm_model.alarms[i].selected) {
            alarm_model_remove_alarm(i);
        }
    }
    alarm_model.del_alarm_count = 0;
}

AlarmModel* get_alarm_model() {
    return &alarm_model;
}