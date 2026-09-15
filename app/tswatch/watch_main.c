// include NuttX headers
#include <nuttx/config.h>
#include <unistd.h>
#include <uv.h>
#include "watch_start.h"
#include "utils/include/circle_screen.h"
#include "utils/include/font_manager.h"

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

int main(int argc, FAR char* argv[])
{
    // init lvgl
    lv_nuttx_dsc_t info;
    lv_nuttx_result_t result;
    //uv_loop_t ui_loop;
    //lv_memset(&ui_loop, 0, sizeof(uv_loop_t));

    if (lv_is_initialized()) {
        LV_LOG_ERROR("LVGL already initialized! aborting.");
        return -1;
    }

    lv_init();

    lv_nuttx_dsc_init(&info);
    lv_nuttx_init(&info, &result);

    if (result.disp == NULL) {
        LV_LOG_ERROR("lv_demos initialization failure!");
        return 1;
    }
    font_manager_init();

    lv_obj_clear_flag(lv_screen_active(), LV_OBJ_FLAG_SCROLLABLE);
    watch_start();

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
