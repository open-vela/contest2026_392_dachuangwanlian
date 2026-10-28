// include NuttX headers
#include <nuttx/config.h>
#include <unistd.h>
#include <uv.h>
#include "watch_start.h"
#include "utils/include/circle_screen.h"
#include "utils/include/font_manager.h"
#include "bt/include/bt_manager.h"
#define TSW_LOG_TAG "main_page"
#include "tsw_log.h"   /* tswatch 统一日志: TSW_LOGE/W/I/D, 用法见头文件 */

/* 当 xiaozhi_voice 清理完成后置为 true，主循环跳过 lv_timer_handler
 * 以避免 TLSF 堆损坏导致的必现 MMFAR=0x15c 崩溃。 */
volatile bool g_lvgl_stop_rendering = false;

// static void lv_nuttx_uv_loop(uv_loop_t* loop, lv_nuttx_result_t* result)
// {
//     lv_nuttx_uv_t uv_info;
//     void* data;

//     uv_loop_init(loop);

//     lv_memset(&uv_info, 0, sizeof(uv_info));
//     uv_info.loop = loop;
//     uv_info.disp = result->disp;
//     uv_info.indev = result->indev;
// #ifdef CONFIG_UINPUT_TOUCH
//     uv_info.uindev = result->utouch_indev;
// #endif

//     data = lv_nuttx_uv_init(&uv_info);
//     uv_run(loop, UV_RUN_DEFAULT);
//     lv_nuttx_uv_deinit(&data);
// }

/* tsw_log 用法示例 (首次集成验证, 后续各模块可直接照抄使用):
 * #define TSW_LOG_TAG "xxx"  // include tsw_log.h 之前定义, 输出按模块过滤 */
int main(int argc, FAR char* argv[])
{
    // init lvgl
    lv_nuttx_dsc_t info;
    lv_nuttx_result_t result;
    //uv_loop_t ui_loop;
    //lv_memset(&ui_loop, 0, sizeof(uv_loop_t));

    /* tsw_log 自检: 启动时各打印一条, 串口立刻可见 */
    TSW_LOGI("tswatch start, argc=%d", argc);
    TSW_LOGW("this is a WARN sample");
    TSW_LOGE("this is an ERROR sample");
    /* DEBUG 与 hexdump 默认不输出, 需要:
     *   tsw_log_set_level(TSW_LOG_LEVEL_DEBUG); */

    if (lv_is_initialized()) {
        TSW_LOGE("LVGL already initialized! aborting.");
        return -1;
    }

    lv_init();

    lv_nuttx_dsc_init(&info);
    lv_nuttx_init(&info, &result);

    if (result.disp == NULL) {
        TSW_LOGE("lv_nuttx_init failure: no display!");
        return 1;
    }
    font_manager_init();

    lv_obj_clear_flag(lv_screen_active(), LV_OBJ_FLAG_SCROLLABLE);
    watch_start();

    /* 开机启动蓝牙子系统: 自动连接已配对设备 / 未连接时保持可发现可配对 /
     * 连接成功后关闭可发现 / 提供手表<->手机 SPP 数据通道(详见 bt/include) */
    bt_manager_start();

    // refresh lvgl ui
    //lv_nuttx_uv_loop(&ui_loop, &result);

    while (1)
    {
      uint32_t idle;

      /* TLSF 堆可能在 lv_obj_delete 递归释放时被损坏，
       * 崩溃发生在后续 lv_timer_handler 的渲染周期中。
       * 清理完成后直接跳过 lv_timer_handler，规避崩溃。 */
      if (g_lvgl_stop_rendering) {
        usleep(100 * 1000);  /* 100ms sleep, 避免 busy loop */
        continue;
      }

      idle = lv_timer_handler();

      /* Minimum sleep of 1ms */

      idle = idle ? idle : 1;
      usleep(idle * 1000);
    }

    lv_display_delete(result.disp);
    lv_deinit();

    return 0;
}
