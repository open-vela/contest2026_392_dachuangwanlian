#include "../include/alarm_main.h"

#include "../include/alarm_presenter.h"
#include "../include/alarm_view.h"
#include "../include/alarm_model.h"


// 定时器回调函数
int clock_main_start() {
    //lv_init();
    alarm_presenter_init();

    return 0;
}