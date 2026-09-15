#ifndef ALARM_MODEL_H
#define ALARM_MODEL_H

#include <stdbool.h>
#include <time.h>
#define MAX_ALARM_COUNT 10
typedef struct {
    char time[6];           // HH:MM格式的时间
    char mode[10];          // 闹钟模式（如"每天"）
    //bool enabled;           // 是否启用
    bool selected;          // 是否选中（用于删除）
} AlarmData;

typedef struct {
    AlarmData alarms[MAX_ALARM_COUNT];   // 最多10个闹钟
    int alarm_count;        // 当前闹钟数量
    char current_time[8];   // 当前时间HH:MM
    bool long_pressed;      // 是否长按状态
    int del_alarm_count;    // 待删除闹钟计数
} AlarmModel;

void alarm_model_init();
void alarm_model_get_current_time();
bool alarm_model_add_alarm(const char* time_str);
bool alarm_model_add_alarm_with_mode(const char* time_str, const char* mode);
bool alarm_model_remove_alarm(int index);
int alarm_model_find_alarm(const char* time_str);
void alarm_model_select_alarm(int index, bool selected);
void alarm_model_remove_selected_alarms();
AlarmModel* get_alarm_model(void);

#endif