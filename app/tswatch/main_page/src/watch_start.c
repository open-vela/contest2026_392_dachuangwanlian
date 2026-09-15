#include <lvgl/lvgl.h>

#include "watch_start.h"
#include "main_page.h"
#include "../../applist/include/list.h"
#include "../../xiaozhi_voice/xiaozhi_voice.h"
#include "../../utils/include/font_manager.h"
#include <string.h>
#include <stdio.h>
#include <time.h>
#include <errno.h>
#include <pthread.h>
#include <fcntl.h>
#include <unistd.h>
#include <poll.h>
#include <nuttx/input/buttons.h>

typedef struct {
    lv_obj_t * root;
    lv_obj_t * dail;

    lv_obj_t *time_label;    /* 顶部时间 */
    lv_obj_t *date_label;    /* 顶部日期 */
    lv_obj_t *temp_label;    /* 左上温度 */
    lv_obj_t *humid_label;   /* 右上湿度 */
    lv_obj_t *hr_label;      /* 左下心率 */
    lv_obj_t *data1_label;   /* 右下数据1 (百分比) */
    lv_obj_t *data2_label;   /* 右下数据2 (数值) */
    lv_obj_t *step_label;    /* 底部步数 */
    lv_obj_t *hour_hand;     /* 时针 */
    lv_obj_t *minute_hand;   /* 分针 */
    lv_obj_t *second_hand;   /* 秒针 */
    lv_timer_t *timer;        /* 1秒定时器 */
} watch_ui_t;

static watch_ui_t g_ui;

/* 轻量页面栈：只记当前页 + 上层页，进入新页前销毁当前页，返回时重建上层 */
/* PAGE_HOME / PAGE_APPLIST / PAGE_APP / PAGE_VOICE 定义在 watch_start.h */
static int g_current_page = PAGE_HOME;
static int g_prev_page    = PAGE_HOME;

static lv_obj_t *watch_face_create(lv_obj_t *parent);

/* 电源按键异步回调：在 LVGL 线程中打开 xiaozhi_voice */
static void power_btn_async_cb(void *arg)
{
    (void)arg;
    if (mpd && !strcmp(mpd->current_page, HOME_PAGE)) {
        LV_LOG_USER("Power button: opening xiaozhi_voice");
        nav_enter_app_from(PAGE_HOME);
        xiaozhi_voice_start();
    }
}

/* 电源按键监听线程：从 /dev/buttons 读取电源键事件 (BES HAL key)
 * 电源键 = HAL_KEY_CODE_PWR = bit 0
 */
#define PWR_BUTTON_BIT  (1 << 0)

static void *power_btn_thread(void *arg)
{
    (void)arg;
    int fd = open("/dev/buttons", O_RDONLY);
    if (fd < 0) {
        printf("[watch_start] Failed to open /dev/buttons: %d\n", errno);
        return NULL;
    }

    btn_buttonset_t prev_state = 0;
    struct pollfd fds[1];
    fds[0].fd     = fd;
    fds[0].events = POLLIN;

    while (1) {
        int ret = poll(fds, 1, -1);  /* 阻塞等待按钮事件 */
        if (ret > 0 && (fds[0].revents & POLLIN)) {
            btn_buttonset_t state = 0;
            ssize_t n = read(fd, &state, sizeof(state));
            if (n == sizeof(state)) {
                /* 检测电源键的按下边沿（从0变1） */
                btn_buttonset_t pressed = state & ~prev_state;
                if (pressed & PWR_BUTTON_BIT) {
                    printf("[watch_start] Power button pressed! state=0x%x\n",
                           (unsigned)state);
                    lv_async_call(power_btn_async_cb, NULL);
                }
                prev_state = state;
            }
        }
    }

    close(fd);
    return NULL;
}

/* 动画回调包装：lv_obj_set_style_translate_y 需要 selector 第三个参数，
 * 但 lv_anim_exec_xcb_t 只传 (var, value)，故固定 selector=0。 */
static void anim_translate_y_cb(void *var, int32_t v)
{
    lv_obj_set_style_translate_y((lv_obj_t *)var, v, 0);
}

/* 隐藏表盘：加 LV_OBJ_FLAG_HIDDEN 让 LVGL 整棵跳过表盘绘制（省 CPU）。
 * 关键：不暂停 timer！表盘虽不绘制，指针角度仍每秒更新；右滑 clear HIDDEN 时
 * 秒针已是当前正确角度，无定格→跳变。timer 持续写属性对隐藏对象是轻活（不触发渲染）。 */
static void watch_hide(void)
{
    if (mpd && mpd->mainPage) {
        lv_obj_add_flag(mpd->mainPage, LV_OBJ_FLAG_HIDDEN);
    }
    /* 暂停表盘定时器，避免后台图像旋转消耗CPU */
    if (g_ui.timer) {
        lv_timer_pause(g_ui.timer);
    }
}

/* 恢复表盘：取消 HIDDEN。timer 一直在走，秒针角度已是当前值，直接显示无跳动。 */
static void watch_show(void)
{
    if (mpd && mpd->mainPage) {
        lv_obj_clear_flag(mpd->mainPage, LV_OBJ_FLAG_HIDDEN);
    }
    if (g_ui.timer) {
        lv_timer_resume(g_ui.timer);
    }
}

/* 从二级页返回 home 时恢复表盘可见性（表盘对象树始终未删除）。 */
static void watch_show_home(void)
{
    if (mpd && mpd->mainPage) {
        lv_obj_clear_flag(mpd->mainPage, LV_OBJ_FLAG_HIDDEN);
    }
    if (g_ui.timer) {
        lv_timer_resume(g_ui.timer);
    }
}

/* 二级应用页退出后回调：按 g_prev_page 重建上层。
 * 暴露给各二级页（stopwatch/calendar/...）的退出函数末尾调用。 */
void nav_return_from_app(void)
{
    if (g_prev_page == PAGE_APPLIST) {
        /* 二级页返回列表：建好列表即可（表盘此时仍 hidden，列表盖满；无滑入动画免卡顿） */
        mpd->appListCenter = lv_obj_create(mpd->watchScr);
        applist_create_list(mpd->appListCenter);
        g_current_page = PAGE_APPLIST;
        strcpy(mpd->current_page, APPS_PAGE);
    } else if (g_prev_page == PAGE_VOICE) {
        /* 从闹钟/秒表等返回 xiaozhi_voice：重建 xiaozhi_voice UI */
        LV_LOG_USER("nav_return: PAGE_VOICE, restarting xiaozhi_voice");
        xiaozhi_voice_start();
        g_current_page = PAGE_APP;
        strcpy(mpd->current_page, INSOMEONEAPP_PAGE);
    } else {
        watch_show_home();
        g_current_page = PAGE_HOME;
        strcpy(mpd->current_page, HOME_PAGE);
    }
}

/* 进入二级应用页前回调：隐藏列表（不删除，避免 TLSF 堆损坏），记栈上层=列表。
 * 供 list.c 的 app_click_event_cb 在调用 xxx_start() 前调用。
 *
 * CRITICAL: Do NOT call lv_obj_delete/lv_obj_del here. On BES1700,
 * lv_obj_del corrupts the TLSF heap, causing必现 MMFAR=0x15c crash
 * when LVGL later allocates memory during rendering. Instead, hide the
 * object with LV_OBJ_FLAG_HIDDEN. */
void nav_enter_app(void)
{
    if (mpd->appListCenter != NULL) {
        lv_anim_delete(mpd->appListCenter, anim_translate_y_cb);
        applist_hide_page();
        lv_obj_delete(mpd->appListCenter);
        mpd->appListCenter = NULL;
    }
    g_prev_page = PAGE_APPLIST;
    g_current_page = PAGE_APP;
    strcpy(mpd->current_page, INSOMEONEAPP_PAGE);
}

/* 从指定页面进入应用（用于电源键→xiaozhi_voice 或 语音→闹钟等场景） */
void nav_enter_app_from(int prev)
{
    if (mpd->appListCenter != NULL) {
        lv_anim_delete(mpd->appListCenter, anim_translate_y_cb);
        applist_hide_page();
        lv_obj_delete(mpd->appListCenter);
        mpd->appListCenter = NULL;
    }
    /* 从表盘进入时隐藏表盘并暂停定时器，减少后台CPU消耗 */
    if (prev == PAGE_HOME) {
        watch_hide();
    }
    g_prev_page = prev;
    g_current_page = PAGE_APP;
    strcpy(mpd->current_page, INSOMEONEAPP_PAGE);
}

int nav_get_prev_page(void)
{
    return g_prev_page;
}

static void panel_setup(lv_obj_t * obj)
{
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(obj, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_border_width(obj, 0, 0);
    lv_obj_set_style_pad_all(obj, 0, 0);
}

static lv_obj_t *create_data_label(lv_obj_t *parent, const char *text,
                                     int16_t x, int16_t y, int16_t w,
                                     const lv_font_t *font, lv_color_t color)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, color, 0);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(label, w);
    lv_obj_align(label, LV_ALIGN_TOP_LEFT, x, y);
    lv_obj_set_style_bg_opa(label, LV_OPA_TRANSP, 0);  /* 透明背景 */
    lv_obj_set_style_border_opa(label, LV_OPA_TRANSP, 0);
    return label;
}

void watch_update_time(lv_timer_t *timer)
{
    time_t t = time(NULL);
    struct tm *localTime = localtime(&t);
    uint16_t hour = (localTime->tm_hour + 8) % 24;
    uint16_t minute = localTime->tm_min;
    uint16_t second = localTime->tm_sec;

    /* _1 指针图(16x455)的旋转轴心在图正中(w/2, h/2)。
     * lv_obj_center 已把图中心对齐表盘中心 → pivot=图中心=表盘中心，
     * 不再 set_pos（加 set_pos 反而把图挪偏，之前"中心偏移"就是这原因）。
     * LVGL v9 angle 0.1°：秒*60 | 分*60+秒 | (hour%12)*300+分*5 */
    int second_w = lv_obj_get_width(g_ui.second_hand);
    int second_h = lv_obj_get_height(g_ui.second_hand);
    lv_image_set_pivot(g_ui.second_hand, second_w / 2, second_h / 2);
    lv_image_set_zoom(g_ui.second_hand, 170);
    lv_image_set_rotation(g_ui.second_hand, second * 60);

    int minute_w = lv_obj_get_width(g_ui.minute_hand);
    int minute_h = lv_obj_get_height(g_ui.minute_hand);
    lv_image_set_pivot(g_ui.minute_hand, minute_w / 2, minute_h / 2);
    lv_image_set_zoom(g_ui.minute_hand, 170);
    lv_image_set_rotation(g_ui.minute_hand, minute * 60 + second);

    int hour_w = lv_obj_get_width(g_ui.hour_hand);
    int hour_h = lv_obj_get_height(g_ui.hour_hand);
    lv_image_set_pivot(g_ui.hour_hand, hour_w / 2, hour_h / 2);
    lv_image_set_zoom(g_ui.hour_hand, 170);
    lv_image_set_rotation(g_ui.hour_hand, (hour % 12) * 300 + minute * 5);
}

static lv_obj_t *watch_face_create(lv_obj_t *parent)
{
    memset(&g_ui, 0, sizeof(g_ui));

    /* 根容器：圆形表盘背景，与 watchScr (LV_CIRCLE_WATCH) 对齐 */
    lv_obj_t *root = lv_obj_create(parent);
    lv_obj_set_size(root, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_color(root, COLOR_BG_DARK, 0);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(root, LV_RADIUS_CIRCLE, 0);
    panel_setup(root);
    g_ui.root = root;

    /* 表盘底图 */
    g_ui.dail = lv_image_create(root);
    lv_image_set_src(g_ui.dail, "/emmc/test.bin");
    lv_obj_center(g_ui.dail);


    /* ---------- 7. 三根指针 ----------
     * 指针图从 /emmc/ 加载，lv_obj_center 居中（图中点对齐表盘中心）。
     * pivot 取图中心(w/2,h/2)=表盘中心，绕中心旋转。zoom 在 timer 首帧设。 */
    g_ui.hour_hand = lv_image_create(root);
    lv_image_set_src(g_ui.hour_hand, "/emmc/hour_hand_1.png");
    lv_obj_center(g_ui.hour_hand);

    g_ui.minute_hand = lv_image_create(root);
    lv_image_set_src(g_ui.minute_hand, "/emmc/minute_hand_1.png");
    lv_obj_center(g_ui.minute_hand);

    g_ui.second_hand = lv_image_create(root);
    lv_image_set_src(g_ui.second_hand, "/emmc/second_hand_1.png");
    lv_obj_center(g_ui.second_hand);

    /* 指针层级: 时针在最下, 分针中间, 秒针最上 (创建顺序即层级) */

    /* 中心圆点: 盖在三根指针根部之上，遮住根部不汇聚到中心点的缝隙。
     * 暗色(同背景 0x101418)，与表盘背景融为一体。需在指针之后创建(最上层)。 */
    lv_obj_t *center_cap = lv_obj_create(root);
    lv_obj_set_size(center_cap, 16, 16);
    lv_obj_center(center_cap);
    lv_obj_set_style_radius(center_cap, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(center_cap, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(center_cap, COLOR_BG_DARK, 0);
    lv_obj_set_style_border_width(center_cap, 0, 0);
    lv_obj_clear_flag(center_cap, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(center_cap, LV_SCROLLBAR_MODE_OFF);

    /* ---------- 8. 创建1秒定时器 ---------- */
    g_ui.timer = lv_timer_create(watch_update_time, 1000, mpd);
    lv_timer_ready(g_ui.timer);  /* 立即执行一次, 初始化显示 */

    return root;
}

static void watch_sys_gesture_event_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    lv_indev_t *indev = lv_indev_active();
    if (!indev) return;

    lv_dir_t dir = lv_indev_get_gesture_dir(indev);

    switch (code)
    {
    case LV_EVENT_GESTURE:
        if (dir == LV_DIR_LEFT)
        {
            if (!strcmp(mpd->current_page, HOME_PAGE))
            {
                g_prev_page = PAGE_HOME;

                /* 先建好列表（ui_bg 实色盖满整屏），再隐藏表盘——
                 * 顺序不能反：先 hide 会露出黑底直到列表盖上来（黑屏）；
                 * 列表已就位盖满后再 hide 表盘只是省绘制，用户无感知，且无两层渲染卡顿。 */
                mpd->appListCenter = lv_obj_create(mpd->watchScr);
                app_launcher_create(mpd->appListCenter);
                watch_hide();
                g_current_page = PAGE_APPLIST;
                strcpy(mpd->current_page, APPS_PAGE);
            }
            else
            {
                bool is_home_page = !strcmp(mpd->current_page, HOME_PAGE);
            }
        }
        if (dir == LV_DIR_RIGHT)
        {
            if (!strcmp(mpd->current_page, HOME_PAGE))
            {
            }
            else
            {
                if (mpd->appListCenter != NULL) {
                    lv_anim_delete(mpd->appListCenter, anim_translate_y_cb);
                    applist_hide_page();   /* 删列表 ui_bg 子树 + 清静态指针 */
                    lv_obj_delete(mpd->appListCenter);
                    mpd->appListCenter = NULL;
                }

                /* 恢复表盘：取消 HIDDEN（timer 一直走，秒针已是当前角度，直接显示无跳动） */
                watch_show();
                watch_show();
                g_current_page = PAGE_HOME;

                strcpy(mpd->current_page, HOME_PAGE);
            }
        }
        break;
    default:
        break;
    }
}

void watch_start(void)
{
    lv_obj_t *watchScr = get_watch_scr();

    /* 创建表盘 */
    lv_obj_t * face = watch_face_create(watchScr);
    (void)face;

    if (mpd == NULL) {
        mpd = (MainPageData *)malloc(sizeof(MainPageData));
        memset(mpd, 0, sizeof(MainPageData));
        strcpy(mpd->current_page, HOME_PAGE);
        mpd->mainPage = face;
        mpd->watchScr = watchScr;
        lv_obj_add_event_cb(lv_screen_active(), watch_sys_gesture_event_cb,
                            LV_EVENT_GESTURE, mpd);
    }

    /* 启动电源按键监听线程 */
    pthread_t tid;
    pthread_create(&tid, NULL, power_btn_thread, NULL);
    pthread_detach(tid);
}
