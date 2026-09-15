/****************************************************************************
 * apps/examples/xiaozhi_voice/xiaozhi_main.c
 *
 * Xiaozhi AI Voice App — Phase 2 entry point.
 *
 * Full dialog: OTA -> wss connect -> hello -> listen start -> mic capture
 * (Opus uplink) -> listen stop -> receive stt/tts -> TTS Opus playback.
 *
 * The dialog runs in a worker thread; the main thread drives the LVGL
 * event loop and reflects the dialog state on screen so it is obvious
 * whether the app launched and how far it got.
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

#include <nuttx/config.h>

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>
#include <arpa/inet.h>
#include <math.h>
#include <netutils/netlib.h>

#include <lvgl/lvgl.h>

#include <netutils/cJSON.h>

#include "xiaozhi_ota.h"
#include "xiaozhi_ws.h"
#include "xiaozhi_audio.h"
#include "xiaozhi_voice.h"
#include "mcp_server.h"
#include "../applist/include/list.h"
#include "../utils/include/font_manager.h"
#include "../main_page/include/watch_start.h"
#include "../stopwatch/include/stopwatch.h"
#include "../clock/include/alarm_presenter.h"
#include "../exercise/include/exercise.h"
#include "../heart/include/heart.h"
#include "../sleep/include/sleep.h"
#include "../timer/include/timer_main.h"
#include "../settings/include/setting.h"

/* watch_main.c 中的全局停止标记，清理后跳过 lv_timer_handler 规避崩溃 */
extern volatile bool g_lvgl_stop_rendering;

/* 按钮图片资源声明 */
/* 注意：需要将PNG图片转换为C数组格式才能使用 */
/* 暂时使用默认的圆形按钮样式 */

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/
/*====================================================
 * Listening动画配置
 *====================================================*/
#define BAR_COUNT       7           /* 7个柱子 */
#define BAR_GAP         8           /* 柱子间距 */
#define BAR_WIDTH       12          /* 柱子宽度 */
#define ANIM_MS         60          /* 动画刷新间隔，加快频率 */
#define BAR_COLOR       0xFF6900    /* 橙色 */
#define BAR_MAX_HEIGHT  100         /* 柱子最大高度 */

/*====================================================
 * LVGL UI互斥锁 - 保护UI操作
 *====================================================*/
static pthread_mutex_t s_ui_mutex = PTHREAD_MUTEX_INITIALIZER;

/* 保存进入xiaozhi_voice时的原始入口页面（PAGE_HOME或PAGE_APPLIST），
 * MCP工具会修改g_prev_page，因此需要在入口处快照。 */
static int s_xiaozhi_entry_page = PAGE_HOME;

/*====================================================
 * Listening动画状态
 *====================================================*/
static float    s_tick = 0;
static float    s_bar_h[BAR_COUNT];
static float    s_bar_tgt[BAR_COUNT];
static lv_obj_t *s_bars[BAR_COUNT] = {NULL};
static lv_obj_t *s_bar_container = NULL;
static lv_timer_t *s_timer = NULL;
static lv_obj_t *xiaozhi_container = NULL;
static lv_obj_t *s_listen_label = NULL;

/*====================================================
 * 状态圆点动画
 *====================================================*/
static lv_obj_t *s_status_pill = NULL;      /* 状态胶囊容器 */
static lv_obj_t *s_status_dot = NULL;       /* 状态圆点 */
static lv_timer_t *s_dot_pulse_timer = NULL; /* 圆点脉冲动画定时器 */
static int s_dot_pulse_tick = 0;

/*====================================================
 * You/Xiaozhi文本标签引用（用于动态调整位置）
 *====================================================*/
static lv_obj_t *s_stt_caption = NULL;
static lv_obj_t *s_tts_caption = NULL;

/*====================================================
 * Thinking动画配置
 *====================================================*/
#define DOT_COUNT       3
#define DOT_SIZE        12
#define DOT_GAP         8
#define DOT_ANIM_MS     250         /* 适当降低刷新频率，减少卡顿 */

/*====================================================
 * Thinking动画状态
 *====================================================*/
static lv_obj_t *s_dots[DOT_COUNT] = {NULL};
static lv_obj_t *s_dot_container = NULL;
static lv_timer_t *s_think_timer = NULL;
static lv_obj_t *s_think_label = NULL;
static int s_think_tick = 0;

/*====================================================
 * 就绪状态气泡胶囊（快捷命令）
 *====================================================*/
static lv_obj_t *s_bubble_container = NULL;   /* 气泡容器 */
static lv_obj_t *s_bubble_alarm = NULL;       /* "打开闹钟"气泡 */
static lv_obj_t *s_bubble_timer = NULL;       /* "倒计时5分钟"气泡 */

/*====================================================
 * 识别中状态动效（三个紫色圆点 + 提示文字）
 *====================================================*/
static lv_obj_t *s_recog_dots_container = NULL;  /* 识别动效容器 */
static lv_obj_t *s_recog_dots[3] = {NULL};       /* 三个紫色圆点 */
static lv_obj_t *s_recog_label = NULL;           /* "正在理解你的指令..." */
static lv_timer_t *s_recog_anim_timer = NULL;    /* 识别动效定时器 */
static int s_recog_anim_tick = 0;

/* ui_refresh 状态变化检测：从局部静态提升为模块级，
 * 以便 xiaozhi_voice_delete() 重置，确保再次进入时动画正常触发。 */
static int s_ui_last_state = -1;

/*====================================================
 * 全局字体引用（在ui_create中初始化）
 *====================================================*/
static const lv_font_t *s_cjk_font = NULL;

/*====================================================
 * MCP (Model Context Protocol) tool server
 *====================================================*/
static struct xiaozhi_app_s *s_mcp_app = NULL; /* for MCP send_json callback */
static char s_mcp_stt_text[128] = {0}; /* STT文本副本，供MCP工具读取 */

/* Forward declarations (defined later in this file). */

static int send_json(struct xiaozhi_app_s *app, cJSON *root);
void bubble_container_delete(void);
void recog_anim_delete(void);

/* MCP tool: send_json wrapper (app context via static pointer).
 * Wraps the JSON-RPC response in the xiaozhi transport format:
 * {"type":"mcp","payload":{...}}
 */

static int mcp_send_json(cJSON *msg)
{
  if (s_mcp_app == NULL)
    {
      return -1;
    }

  /* Wrap in transport format: {"type":"mcp","payload":<original>} */

  cJSON *wrapper = cJSON_CreateObject();
  cJSON_AddStringToObject(wrapper, "type", "mcp");
  cJSON *payload = cJSON_Duplicate(msg, 1);
  cJSON_AddItemToObject(wrapper, "payload", payload);

  int ret = send_json(s_mcp_app, wrapper);
  cJSON_Delete(wrapper);
  return ret;
}

/*--- MCP Tool Callbacks ---*/

/* Async wrappers for stopwatch — run UI work on the main LVGL thread. */

static void stopwatch_start_async_cb(void *arg)
{
  (void)arg;
  nav_enter_app_from(PAGE_VOICE);
  stopwatch_start();
  start_stopwatch();
}

static void stopwatch_pause_async_cb(void *arg)
{
  (void)arg;
  nav_enter_app_from(PAGE_VOICE);
  stopwatch_start();
  stopwatch_pause();
}

static void stopwatch_reset_async_cb(void *arg)
{
  (void)arg;
  nav_enter_app_from(PAGE_VOICE);
  stopwatch_reset();
}

/* Tool: self.stopwatch.start — open stopwatch and start timing. */

static cJSON *mcp_tool_stopwatch_start(const cJSON *args)
{
  lv_async_call(stopwatch_start_async_cb, NULL);
  return cJSON_CreateString("ok");
}

static cJSON *mcp_tool_stopwatch_pause(const cJSON *args)
{
  lv_async_call(stopwatch_pause_async_cb, NULL);
  return cJSON_CreateString("ok");
}

static cJSON *mcp_tool_stopwatch_reset(const cJSON *args)
{
  lv_async_call(stopwatch_reset_async_cb, NULL);
  return cJSON_CreateString("ok");
}

/* Async wrappers — run UI work on the main LVGL thread. */

static void clock_open_async_cb(void *arg)
{
  nav_enter_app_from(PAGE_VOICE);
  clock_main_start();
}

struct alarm_add_args
{
  char hour[8];
  char minute[8];
  char mode[16];
};

static void alarm_add_async_cb(void *arg)
{
  struct alarm_add_args *a = (struct alarm_add_args *)arg;
  /* Ensure clock view is initialized before adding alarm.
   * clock_main_start() is safe to call multiple times —
   * alarm_model_init() has a static initialized guard. */
  nav_enter_app_from(PAGE_VOICE);
  clock_main_start();
  alarm_presenter_add_alarm_with_mode(a->hour, a->minute, a->mode);
  free(a);
}

/* Tool: self.clock.open — open the clock/alarm app. */

static cJSON *mcp_tool_clock_open(const cJSON *args)
{
  lv_async_call(clock_open_async_cb, NULL);
  return cJSON_CreateString("ok");
}

/* Tool: self.alarm.add — add an alarm.
 * Arguments: {"hour": "08", "minute": "30", "mode": "once"}
 * mode: "每天" (default) | "仅一次" | "工作日"
 * 语义映射：今晚/今天/明早/明天/明日/这次 = "仅一次"；工作日/周一到周五 = "工作日"；其他 = "每天"
 */

static cJSON *mcp_tool_alarm_add(const cJSON *args)
{
  const char *hour = "8";
  const char *minute = "0";
  const char *mode = "每天";

  if (args != NULL)
    {
      cJSON *h = cJSON_GetObjectItem(args, "hour");
      cJSON *m = cJSON_GetObjectItem(args, "minute");
      cJSON *md = cJSON_GetObjectItem(args, "mode");
      if (cJSON_IsString(h))
        {
          hour = h->valuestring;
        }

      if (cJSON_IsString(m))
        {
          minute = m->valuestring;
        }

      if (cJSON_IsString(md))
        {
          mode = md->valuestring;
        }
    }

  /* 根据STT文本纠正mode */
  if (s_mcp_stt_text[0] != '\0')
    {
      const char *stt = s_mcp_stt_text;
      if (strstr(stt, "今天") || strstr(stt, "今晚") ||
          strstr(stt, "明早") || strstr(stt, "明天") ||
          strstr(stt, "明日") || strstr(stt, "这次"))
        {
          mode = "仅一次";
        }
      else if (strstr(stt, "工作日") || strstr(stt, "周一") ||
               strstr(stt, "周二") || strstr(stt, "周三") ||
               strstr(stt, "周四") || strstr(stt, "周五"))
        {
          mode = "工作日";
        }
    }

  struct alarm_add_args *a = malloc(sizeof(*a));
  if (a)
    {
      snprintf(a->hour, sizeof(a->hour), "%s", hour);
      snprintf(a->minute, sizeof(a->minute), "%s", minute);
      snprintf(a->mode, sizeof(a->mode), "%s", mode);
      lv_async_call(alarm_add_async_cb, a);
    }

  char resp[64];
  snprintf(resp, sizeof(resp), "alarm set to %s:%s (%s)", hour, minute, mode);
  return cJSON_CreateString(resp);
}

/* Tool: self.alarm.remove — remove an alarm by time.
 * Arguments: {"hour": "10", "minute": "00"}
 */

struct alarm_remove_args
{
  char hour[8];
  char minute[8];
};

static void alarm_remove_async_cb(void *arg)
{
  struct alarm_remove_args *a = (struct alarm_remove_args *)arg;
  nav_enter_app_from(PAGE_VOICE);
  clock_main_start();
  alarm_presenter_remove_alarm_by_time(a->hour, a->minute);
  free(a);
}

static cJSON *mcp_tool_alarm_remove(const cJSON *args)
{
  const char *hour = "8";
  const char *minute = "0";

  if (args != NULL)
    {
      cJSON *h = cJSON_GetObjectItem(args, "hour");
      cJSON *m = cJSON_GetObjectItem(args, "minute");
      if (cJSON_IsString(h))
        {
          hour = h->valuestring;
        }

      if (cJSON_IsString(m))
        {
          minute = m->valuestring;
        }
    }

  struct alarm_remove_args *a = malloc(sizeof(*a));
  if (a)
    {
      snprintf(a->hour, sizeof(a->hour), "%s", hour);
      snprintf(a->minute, sizeof(a->minute), "%s", minute);
      lv_async_call(alarm_remove_async_cb, a);
    }

  char resp[64];
  snprintf(resp, sizeof(resp), "alarm %s:%s removed", hour, minute);
  return cJSON_CreateString(resp);
}

/* ──── MCP 工具：运动应用 ──── */

static void exercise_start_async_cb(void *arg)
{
  nav_enter_app_from(PAGE_VOICE);
  exercise_start_running();
}

static void exercise_stop_async_cb(void *arg)
{
  nav_enter_app_from(PAGE_VOICE);
  exercise_start();
  exercise_stop();
}

static cJSON *mcp_tool_exercise_start(const cJSON *args)
{
  lv_async_call(exercise_start_async_cb, NULL);
  return cJSON_CreateString("ok");
}

static cJSON *mcp_tool_exercise_stop(const cJSON *args)
{
  lv_async_call(exercise_stop_async_cb, NULL);
  return cJSON_CreateString("ok");
}

static void heart_open_async_cb(void *arg)
{
  nav_enter_app_from(PAGE_VOICE);
  heart_start();
}

static cJSON *mcp_tool_heart_open(const cJSON *args)
{
  lv_async_call(heart_open_async_cb, NULL);
  return cJSON_CreateString("ok");
}

static void sleep_open_async_cb(void *arg)
{
  nav_enter_app_from(PAGE_VOICE);
  sleep_start();
}

static cJSON *mcp_tool_sleep_open(const cJSON *args)
{
  lv_async_call(sleep_open_async_cb, NULL);
  return cJSON_CreateString("ok");
}

static void timer_open_async_cb(void *arg)
{
  nav_enter_app_from(PAGE_VOICE);
  timer_start();
}

static cJSON *mcp_tool_timer_open(const cJSON *args)
{
  lv_async_call(timer_open_async_cb, NULL);
  return cJSON_CreateString("ok");
}

static void settings_open_async_cb(void *arg)
{
  (void)arg;
  nav_enter_app_from(PAGE_VOICE);
  settings_app_start();
}

static cJSON *mcp_tool_settings_open(const cJSON *args)
{
  lv_async_call(settings_open_async_cb, NULL);
  return cJSON_CreateString("ok");
}

/* Register all MCP tools. Called once during dialog init. */

static void mcp_register_tools(void)
{
  mcp_server_init();

  mcp_server_add_tool("self.stopwatch.start",
                      "启动秒表应用并开始计时",
                      NULL,
                      mcp_tool_stopwatch_start);

  mcp_server_add_tool("self.stopwatch.pause",
                      "暂停/结束秒表计时",
                      NULL,
                      mcp_tool_stopwatch_pause);

  mcp_server_add_tool("self.stopwatch.reset",
                      "重置秒表为零",
                      NULL,
                      mcp_tool_stopwatch_reset);

  mcp_server_add_tool("self.clock.open",
                      "打开时钟/闹钟应用",
                      NULL,
                      mcp_tool_clock_open);

  mcp_server_add_tool("self.alarm.add",
                      "添加闹钟，参数: hour(时), minute(分), mode(模式: 每天/仅一次/工作日)",
                      "{\"type\":\"object\",\"properties\":{\"hour\":{\"type\":\"string\",\"description\":\"小时，0-23\"},\"minute\":{\"type\":\"string\",\"description\":\"分钟，0-59\"},\"mode\":{\"type\":\"string\",\"description\":\"模式: 每天=每天重复; 仅一次=今晚/今天/明早/明天/明日/这次; 工作日=周一到周五。用户说今晚/今天/明早/明天时必须用仅一次\"}},\"required\":[\"hour\",\"minute\"]}",
                      mcp_tool_alarm_add);

  mcp_server_add_tool("self.alarm.remove",
                      "删除闹钟，参数: hour(时), minute(分)",
                      "{\"type\":\"object\",\"properties\":{\"hour\":{\"type\":\"string\",\"description\":\"小时，0-23\"},\"minute\":{\"type\":\"string\",\"description\":\"分钟，0-59\"}},\"required\":[\"hour\",\"minute\"]}",
                      mcp_tool_alarm_remove);

  mcp_server_add_tool("self.exercise.start",
                      "打开运动应用并直接开始跑步运动",
                      NULL,
                      mcp_tool_exercise_start);

  mcp_server_add_tool("self.exercise.stop",
                      "停止/结束当前运动",
                      NULL,
                      mcp_tool_exercise_stop);

  mcp_server_add_tool("self.heart.open",
                      "打开心率应用查看心率数据",
                      NULL,
                      mcp_tool_heart_open);

  mcp_server_add_tool("self.sleep.open",
                      "打开睡眠应用查看睡眠数据",
                      NULL,
                      mcp_tool_sleep_open);

  mcp_server_add_tool("self.timer.open",
                      "打开倒计时应用",
                      NULL,
                      mcp_tool_timer_open);

  mcp_server_add_tool("self.settings.open",
                      "打开设置应用",
                      NULL,
                      mcp_tool_settings_open);
}

/****************************************************************************
 * Listening动画函数
 ****************************************************************************/

/* 生成目标高度 */
static void wave_gen_targets(void)
{
    for (int i = 0; i < BAR_COUNT; i++) {
        float t = (float)i / BAR_COUNT;
        float env = expf(-5.0f * (t - 0.5f) * (t - 0.5f));
        float r = fabsf(sinf(t * 13.7f + s_tick * 0.9f)) * 0.4f
                + fabsf(cosf(t * 23.1f - s_tick * 0.6f)) * 0.35f
                + fabsf(sinf(t * 7.3f + s_tick * 1.4f)) * 0.25f;
        s_bar_tgt[i] = r * env;
    }
}

/* 定时器回调 */
static void wave_tick_cb(lv_timer_t *t)
{
    (void)t;

    pthread_mutex_lock(&s_ui_mutex);

    s_tick += 0.15f;

    /* 每次都生成新的目标高度，让动画持续变化 */
    wave_gen_targets();

    /* 更新每个柱子的高度和位置 */
    for (int i = 0; i < BAR_COUNT; i++) {
        float speed = 0.1f + (float)i / BAR_COUNT * 0.1f;  /* 加快响应速度 */
        s_bar_h[i] += (s_bar_tgt[i] - s_bar_h[i]) * speed;

        /* 计算高度（像素） */
        int h = (int)(s_bar_h[i] * BAR_MAX_HEIGHT);
        if (h < 4) h = 4;

        /* 更新柱子对象的高度和位置（保持底部对齐） */
        if (s_bars[i] != NULL) {
            lv_obj_set_height(s_bars[i], h);
            /* 底部对齐：y坐标 = 容器高度 - 柱子高度 */
            lv_obj_set_y(s_bars[i], BAR_MAX_HEIGHT + 10 - h);
        }
    }

    pthread_mutex_unlock(&s_ui_mutex);
}

/* 启动Listening动画 */
void wave_anim_start(lv_obj_t *parent)
{
    /* 如果已经创建，先停止 */
    wave_anim_stop();

    memset(s_bar_h, 0, sizeof(s_bar_h));
    memset(s_bar_tgt, 0, sizeof(s_bar_tgt));
    s_tick = 0;

    /* 创建柱子容器 - 放在按钮下方 */
    s_bar_container = lv_obj_create(parent);
    lv_obj_set_size(s_bar_container, 200, BAR_MAX_HEIGHT + 10);  /* 容器高度 */
    lv_obj_align(s_bar_container, LV_ALIGN_CENTER, 0, 70);     /* 在按钮下方，避免与status_label重叠 */
    lv_obj_set_style_bg_opa(s_bar_container, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_bar_container, 0, 0);
    lv_obj_set_style_pad_all(s_bar_container, 0, 0);
    lv_obj_set_style_pad_top(s_bar_container, 0, 0);
    lv_obj_set_style_pad_bottom(s_bar_container, 0, 0);
    lv_obj_set_style_pad_left(s_bar_container, 0, 0);
    lv_obj_set_style_pad_right(s_bar_container, 0, 0);
    lv_obj_clear_flag(s_bar_container, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(s_bar_container, LV_OBJ_FLAG_HIDDEN);

    /* 计算起始位置 */
    int total_width = BAR_COUNT * BAR_WIDTH + (BAR_COUNT - 1) * BAR_GAP;
    int x_start = (200 - total_width) / 2;

    /* 创建7个柱子，底部对齐在容器底部，使用青色 */
    for (int i = 0; i < BAR_COUNT; i++) {
        s_bars[i] = lv_obj_create(s_bar_container);
        lv_obj_set_size(s_bars[i], BAR_WIDTH, 4);  /* 初始高度4像素 */
        /* 柱子底部对齐：y坐标 = 容器高度 - 初始高度 */
        lv_obj_set_pos(s_bars[i], x_start + i * (BAR_WIDTH + BAR_GAP), BAR_MAX_HEIGHT + 10 - 4);
        lv_obj_set_style_bg_color(s_bars[i], lv_color_hex(0x00CEC9), 0);  /* 青色 */
        lv_obj_set_style_bg_opa(s_bars[i], LV_OPA_COVER, 0);
        lv_obj_set_style_radius(s_bars[i], BAR_WIDTH / 2, 0);  /* 圆角 */
        lv_obj_set_style_border_width(s_bars[i], 0, 0);
        lv_obj_clear_flag(s_bars[i], LV_OBJ_FLAG_HIDDEN);
    }

    wave_gen_targets();
    s_timer = lv_timer_create(wave_tick_cb, ANIM_MS, NULL);

}

/* 停止Listening动画 */
void wave_anim_stop(void)
{
    if (s_timer) { lv_timer_del(s_timer); s_timer = NULL; }
    /* 删除容器会自动删除所有子对象（bars） */
    if (s_bar_container) { lv_obj_del(s_bar_container); s_bar_container = NULL; }
    for (int i = 0; i < BAR_COUNT; i++) {
        s_bars[i] = NULL;
    }
    if (s_listen_label) { lv_obj_del(s_listen_label); s_listen_label = NULL; }
}

/****************************************************************************
 * Thinking动画函数
 ****************************************************************************/

/* 定时器回调：更新圆点大小 */
static void think_tick_cb(lv_timer_t *t)
{
    (void)t;

    pthread_mutex_lock(&s_ui_mutex);

    s_think_tick++;

    for (int i = 0; i < DOT_COUNT; i++) {
        float phase = (float)(s_think_tick - i * 2) / 4.0f;  /* 调整相位 */
        float tri = 1.0f - fabsf(fmodf(phase, 2.0f) - 1.0f);
        int size = (int)(DOT_SIZE * (0.4f + tri * 0.6f));

        if (s_dots[i] != NULL) {
            lv_obj_set_size(s_dots[i], size, size);
            lv_obj_set_style_radius(s_dots[i], size / 2, 0);
        }
    }

    pthread_mutex_unlock(&s_ui_mutex);
}

/* 启动Thinking动画 */
void think_anim_start(lv_obj_t *parent)
{
    /* 如果已经创建，先停止 */
    think_anim_stop();

    s_think_tick = 0;

    /* 创建圆点容器 - 放在按钮下方 */
    s_dot_container = lv_obj_create(parent);
    lv_obj_set_size(s_dot_container, 150, 40);
    lv_obj_align(s_dot_container, LV_ALIGN_CENTER, 0, 70);  /* 在按钮下方，避免与status_label重叠 */
    lv_obj_set_style_bg_opa(s_dot_container, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_dot_container, 0, 0);
    lv_obj_set_style_pad_all(s_dot_container, 0, 0);
    lv_obj_clear_flag(s_dot_container, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(s_dot_container, LV_OBJ_FLAG_HIDDEN);

    /* 计算起始位置 */
    int total_w = DOT_COUNT * DOT_SIZE + (DOT_COUNT - 1) * DOT_GAP;
    int x_start = (150 - total_w) / 2;
    int y_pos = (40 - DOT_SIZE) / 2;

    /* 创建3个圆点，使用紫色 */
    for (int i = 0; i < DOT_COUNT; i++) {
        s_dots[i] = lv_obj_create(s_dot_container);
        lv_obj_set_size(s_dots[i], DOT_SIZE, DOT_SIZE);
        lv_obj_set_pos(s_dots[i], x_start + i * (DOT_SIZE + DOT_GAP), y_pos);
        lv_obj_set_style_bg_color(s_dots[i], lv_color_hex(0x6C5CE7), 0);  /* 紫色 */
        lv_obj_set_style_bg_opa(s_dots[i], LV_OPA_COVER, 0);
        lv_obj_set_style_radius(s_dots[i], DOT_SIZE / 2, 0);  /* 圆形 */
        lv_obj_set_style_border_width(s_dots[i], 0, 0);
        lv_obj_clear_flag(s_dots[i], LV_OBJ_FLAG_HIDDEN);
    }

    s_think_timer = lv_timer_create(think_tick_cb, DOT_ANIM_MS, NULL);

}

/* 停止Thinking动画 */
void think_anim_stop(void)
{
    if (s_think_timer) { lv_timer_del(s_think_timer); s_think_timer = NULL; }
    /* 删除容器会自动删除所有子对象（dots） */
    if (s_dot_container) { lv_obj_del(s_dot_container); s_dot_container = NULL; }
    for (int i = 0; i < DOT_COUNT; i++) {
        s_dots[i] = NULL;
    }
    if (s_think_label) { lv_obj_del(s_think_label); s_think_label = NULL; }
}

/****************************************************************************
 * Recognizing动画函数 - success_check.png + 旋转弧形进度条
 ****************************************************************************/

/* success_check.png显示参数（recognizing和playing共用） */
#define SUCCESS_SIZE    120     /* 圆形直径 */

/* 圆形进度条动画参数 */
#define CIRCLE_INDICATOR_DEG    90      /* 弧形bar弧度(度) = 1/4圆 */
#define CIRCLE_ANIM_MS          80      /* 动画刷新间隔(12.5fps，避免闪烁) */

/* 圆形进度条动画状态 — 使用lv_arc替代lv_canvas，节省78KB内存 */
static lv_obj_t *s_circle_container = NULL;  /* 容器 */
static lv_obj_t *s_circle_bg_img = NULL;     /* 背景图(success_check.png) */
static lv_obj_t *s_circle_arc = NULL;        /* 旋转弧形(lv_arc) */
static lv_timer_t *s_circle_timer = NULL;
static int s_circle_tick = 0;

/* 前向声明 */
void recognize_anim_stop(void);

/* 定时器回调：旋转弧形 */
static void circle_progress_tick_cb(lv_timer_t *t)
{
    (void)t;

    s_circle_tick += 16;
    if (s_circle_tick >= 360) s_circle_tick -= 360;

    if (s_circle_arc != NULL)
    {
        lv_arc_set_rotation(s_circle_arc, s_circle_tick);
    }
}

/* 启动Recognizing动画 — success_check.png + lv_arc旋转弧形 */
void recognize_anim_start(lv_obj_t *parent)
{
    recognize_anim_stop();
    s_circle_tick = 0;

    /* 创建容器 */
    s_circle_container = lv_obj_create(parent);
    if (s_circle_container == NULL) {
        printf("[xiaozhi] recognize_anim_start: failed to create container\n");
        return;
    }
    lv_obj_set_size(s_circle_container, SUCCESS_SIZE + 20, SUCCESS_SIZE + 20);
    lv_obj_align(s_circle_container, LV_ALIGN_CENTER, 0, -85);
    lv_obj_set_style_bg_opa(s_circle_container, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_circle_container, 0, 0);
    lv_obj_set_style_pad_all(s_circle_container, 0, 0);
    lv_obj_clear_flag(s_circle_container, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(s_circle_container, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(s_circle_container, LV_OBJ_FLAG_CLICKABLE);

    /* 背景图 - success_check.png */
    s_circle_bg_img = lv_image_create(s_circle_container);
    if (s_circle_bg_img == NULL) {
        printf("[xiaozhi] recognize_anim_start: failed to create bg_img\n");
        lv_obj_del(s_circle_container);
        s_circle_container = NULL;
        return;
    }
    lv_image_set_src(s_circle_bg_img, "/emmc/success_check.png");
    lv_obj_align(s_circle_bg_img, LV_ALIGN_CENTER, 0, 0);
    lv_obj_remove_flag(s_circle_bg_img, LV_OBJ_FLAG_CLICKABLE);

    /* 旋转弧形 — 使用lv_arc替代lv_canvas，无需78KB buffer */
    s_circle_arc = lv_arc_create(s_circle_container);
    if (s_circle_arc == NULL) {
        printf("[xiaozhi] recognize_anim_start: failed to create arc\n");
        lv_obj_del(s_circle_container);
        s_circle_container = NULL;
        s_circle_bg_img = NULL;
        return;
    }
    lv_obj_set_size(s_circle_arc, SUCCESS_SIZE / 2, SUCCESS_SIZE / 2);
    lv_obj_align(s_circle_arc, LV_ALIGN_CENTER, 0, 0);
    /* 隐藏arc背景圆环和knob，只显示弧形bar */
    lv_obj_set_style_bg_opa(s_circle_arc, LV_OPA_TRANSP, 0);
    lv_obj_set_style_arc_opa(s_circle_arc, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_opa(s_circle_arc, LV_OPA_0, LV_PART_KNOB);
    /* 弧形bar参数：90度弧，浅紫色 */
    lv_arc_set_bg_angles(s_circle_arc, 0, 360);
    lv_arc_set_angles(s_circle_arc, 0, CIRCLE_INDICATOR_DEG);
    lv_arc_set_value(s_circle_arc, 25);
    lv_obj_set_style_arc_color(s_circle_arc, lv_color_hex(0xA29BFE), LV_PART_INDICATOR);
    lv_obj_set_style_arc_width(s_circle_arc, 6, LV_PART_INDICATOR);
    lv_obj_remove_flag(s_circle_arc, LV_OBJ_FLAG_CLICKABLE);

    /* 启动定时器 */
    s_circle_timer = lv_timer_create(circle_progress_tick_cb, CIRCLE_ANIM_MS, NULL);

}

/* 停止Recognizing动画 */
void recognize_anim_stop(void)
{
    if (s_circle_timer) { lv_timer_del(s_circle_timer); s_circle_timer = NULL; }
    if (s_circle_container) { lv_obj_del(s_circle_container); s_circle_container = NULL; }
    s_circle_bg_img = NULL;
    s_circle_arc = NULL;
}

/****************************************************************************
 * Connecting动画函数 - 轨道圆环
 ****************************************************************************/

/* 轨道动画参数 */
#define ORBIT_INNER_SIZE    180     /* 内圈直径 */
#define ORBIT_OUTER_SIZE    230     /* 外圈直径 */
#define ORBIT_ANIM_MS       80      /* 动画刷新间隔(12.5fps，避免闪烁) */

/* 轨道动画状态 */
static lv_obj_t *s_orbit_container = NULL;   /* 轨道容器 */
static lv_obj_t *s_orbit_inner = NULL;       /* 内圈 */
static lv_obj_t *s_orbit_outer = NULL;       /* 外圈 */
static lv_timer_t *s_orbit_timer = NULL;
static int s_orbit_tick = 0;

/* 定时器回调：更新轨道旋转 */
static void orbit_tick_cb(lv_timer_t *t)
{
    (void)t;

    s_orbit_tick++;

    /* 内圈旋转 ~2.25s/圈, 每次转13度 (80ms间隔) */
    if (s_orbit_inner != NULL)
    {
        int32_t angle = (s_orbit_tick * 13) % 360;
        lv_arc_set_rotation(s_orbit_inner, angle);
    }

    /* 外圈反向旋转 ~3s/圈, 每次转10度 */
    if (s_orbit_outer != NULL)
    {
        int32_t angle = 360 - ((s_orbit_tick * 10) % 360);
        lv_arc_set_rotation(s_orbit_outer, angle);
    }
}

/* 前向声明 */
void ripple_anim_stop(void);
void playing_anim_stop(void);

/* 启动Connecting动画 - 轨道弧形条 */
void connecting_anim_start(lv_obj_t *parent)
{
    /* 如果已经创建，先停止 */
    connecting_anim_stop();

    s_orbit_tick = 0;

    /* 创建轨道容器 */
    s_orbit_container = lv_obj_create(parent);
    if (s_orbit_container == NULL) {
        printf("[xiaozhi] connecting_anim_start: failed to create container\n");
        return;
    }
    lv_obj_set_size(s_orbit_container, ORBIT_OUTER_SIZE + 10, ORBIT_OUTER_SIZE + 10);
    lv_obj_align(s_orbit_container, LV_ALIGN_CENTER, 0, -85);  /* 与按钮重叠 */
    lv_obj_set_style_bg_opa(s_orbit_container, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_orbit_container, 0, 0);
    lv_obj_set_style_pad_all(s_orbit_container, 0, 0);
    lv_obj_clear_flag(s_orbit_container, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(s_orbit_container, LV_OBJ_FLAG_CLICKABLE);

    /* 内圈弧 - 90度弧形（1/4圆），橙色 */
    s_orbit_inner = lv_arc_create(s_orbit_container);
    lv_obj_set_size(s_orbit_inner, ORBIT_INNER_SIZE, ORBIT_INNER_SIZE);
    lv_obj_align(s_orbit_inner, LV_ALIGN_CENTER, 0, 0);
    lv_arc_set_bg_angles(s_orbit_inner, 0, 360);       /* 背景完整圆 */
    lv_arc_set_angles(s_orbit_inner, 0, 90);           /* 显示90度弧(1/4圆) */
    lv_arc_set_value(s_orbit_inner, 25);
    /* 隐藏arc对象自身的背景圆 */
    lv_obj_set_style_bg_opa(s_orbit_inner, LV_OPA_TRANSP, 0);
    /* 背景轨道：淡橙色 */
    lv_obj_set_style_arc_width(s_orbit_inner, 2, LV_PART_MAIN);
    lv_obj_set_style_arc_color(s_orbit_inner, lv_color_hex(0xFFE0B2), LV_PART_MAIN);
    lv_obj_set_style_arc_opa(s_orbit_inner, LV_OPA_60, LV_PART_MAIN);
    /* 指示器：深橙色 */
    lv_obj_set_style_arc_width(s_orbit_inner, 2, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(s_orbit_inner, lv_color_hex(0xE08A00), LV_PART_INDICATOR);
    lv_obj_set_style_arc_opa(s_orbit_inner, LV_OPA_COVER, LV_PART_INDICATOR);
    /* 隐藏指示器端点圆球 */
    lv_obj_set_style_arc_rounded(s_orbit_inner, false, LV_PART_INDICATOR);
    /* 移除knob（主题默认蓝色圆点） */
    lv_obj_remove_style(s_orbit_inner, NULL, LV_PART_KNOB);
    lv_obj_remove_flag(s_orbit_inner, LV_OBJ_FLAG_CLICKABLE);

    /* 外圈弧 - 90度弧形，反向旋转 */
    s_orbit_outer = lv_arc_create(s_orbit_container);
    lv_obj_set_size(s_orbit_outer, ORBIT_OUTER_SIZE, ORBIT_OUTER_SIZE);
    lv_obj_align(s_orbit_outer, LV_ALIGN_CENTER, 0, 0);
    lv_arc_set_bg_angles(s_orbit_outer, 0, 360);       /* 背景完整圆 */
    lv_arc_set_angles(s_orbit_outer, 0, 90);           /* 显示90度弧 */
    lv_arc_set_value(s_orbit_outer, 25);
    /* 隐藏arc对象自身的背景圆 */
    lv_obj_set_style_bg_opa(s_orbit_outer, LV_OPA_TRANSP, 0);
    /* 背景轨道：淡橙色 */
    lv_obj_set_style_arc_width(s_orbit_outer, 2, LV_PART_MAIN);
    lv_obj_set_style_arc_color(s_orbit_outer, lv_color_hex(0xFFE0B2), LV_PART_MAIN);
    lv_obj_set_style_arc_opa(s_orbit_outer, LV_OPA_60, LV_PART_MAIN);
    /* 指示器：深橙色 */
    lv_obj_set_style_arc_width(s_orbit_outer, 2, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(s_orbit_outer, lv_color_hex(0xE08A00), LV_PART_INDICATOR);
    lv_obj_set_style_arc_opa(s_orbit_outer, LV_OPA_COVER, LV_PART_INDICATOR);
    /* 隐藏指示器端点圆球 */
    lv_obj_set_style_arc_rounded(s_orbit_outer, false, LV_PART_INDICATOR);
    /* 移除knob（主题默认蓝色圆点） */
    lv_obj_remove_style(s_orbit_outer, NULL, LV_PART_KNOB);
    lv_obj_remove_flag(s_orbit_outer, LV_OBJ_FLAG_CLICKABLE);

    /* 启动旋转定时器 */
    s_orbit_timer = lv_timer_create(orbit_tick_cb, ORBIT_ANIM_MS, NULL);

    /* 将轨道容器移到最上层，避免被按钮等元素遮挡 */
    lv_obj_move_foreground(s_orbit_container);
}

/* 停止Connecting动画 */
void connecting_anim_stop(void)
{
    if (s_orbit_timer) { lv_timer_del(s_orbit_timer); s_orbit_timer = NULL; }
    /* 删除容器会自动删除所有子对象 */
    if (s_orbit_container) { lv_obj_del(s_orbit_container); s_orbit_container = NULL; }
    s_orbit_inner = NULL;
    s_orbit_outer = NULL;
}

/****************************************************************************
 * 波纹动画函数 - 请说话状态
 ****************************************************************************/

/* 波纹动画参数 */
#define RIPPLE_COUNT        3       /* 波纹数量 */
#define RIPPLE_BASE_SIZE    132     /* 波纹基础大小（与voice按钮相同） */
#define RIPPLE_ANIM_MS      20      /* 动画刷新间隔 */
#define RIPPLE_DURATION_MS  800     /* 单个波纹动画周期（加快） */

/* 波纹动画状态 */
static lv_obj_t *s_ripple_container = NULL;
static lv_obj_t *s_ripples[RIPPLE_COUNT];
static lv_timer_t *s_ripple_timer = NULL;
static int s_ripple_tick = 0;

/* 定时器回调：更新波纹动画 */
static void ripple_tick_cb(lv_timer_t *t)
{
    (void)t;

    s_ripple_tick++;

    for (int i = 0; i < RIPPLE_COUNT; i++)
    {
        if (s_ripples[i] == NULL) continue;

        /* 每个波纹延迟600ms (18帧 @ 30ms) */
        int delay_frames = i * 18;
        int local_tick = s_ripple_tick - delay_frames;
        if (local_tick < 0) local_tick += (RIPPLE_DURATION_MS / RIPPLE_ANIM_MS);

        /* 计算进度 0~1 */
        int total_frames = RIPPLE_DURATION_MS / RIPPLE_ANIM_MS;
        float progress = (float)(local_tick % total_frames) / (float)total_frames;

        /* scale: 1.0 -> 2.2 */
        float scale = 1.0f + 1.2f * progress;
        int size = (int)(RIPPLE_BASE_SIZE * scale);
        lv_obj_set_size(s_ripples[i], size, size);
        lv_obj_set_style_radius(s_ripples[i], size / 2, 0);

        /* opacity: 内圈(i=0)最深, 外圈(i=2)最淡
         * 基础: 179*(1-progress), 再乘以权重 (3-i)/3 */
        float weight = (float)(RIPPLE_COUNT - i) / (float)RIPPLE_COUNT;
        int opacity = (int)(179 * (1.0f - progress) * weight);
        lv_obj_set_style_bg_opa(s_ripples[i], LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_opa(s_ripples[i], opacity, 0);
    }
}

/* 启动波纹动画 */
void ripple_anim_start(lv_obj_t *parent)
{
    /* 如果已经创建，先停止 */
    ripple_anim_stop();

    s_ripple_tick = 0;

    /* 创建波纹容器 */
    s_ripple_container = lv_obj_create(parent);
    lv_obj_set_size(s_ripple_container, RIPPLE_BASE_SIZE + 160, RIPPLE_BASE_SIZE + 160);
    lv_obj_align(s_ripple_container, LV_ALIGN_CENTER, 0, -85);  /* 与按钮重叠 */
    lv_obj_set_style_bg_opa(s_ripple_container, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_ripple_container, 0, 0);
    lv_obj_set_style_pad_all(s_ripple_container, 0, 0);
    lv_obj_clear_flag(s_ripple_container, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(s_ripple_container, LV_OBJ_FLAG_CLICKABLE);

    /* 创建3个波纹圆环 */
    for (int i = 0; i < RIPPLE_COUNT; i++)
    {
        s_ripples[i] = lv_obj_create(s_ripple_container);
        lv_obj_set_size(s_ripples[i], RIPPLE_BASE_SIZE, RIPPLE_BASE_SIZE);
        lv_obj_align(s_ripples[i], LV_ALIGN_CENTER, 0, 0);
        /* 青色边框 #00CEC9，内圈深外圈淡 */
        lv_obj_set_style_bg_opa(s_ripples[i], LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(s_ripples[i], 2, 0);
        lv_obj_set_style_border_color(s_ripples[i], lv_color_hex(0x00CEC9), 0);
        float weight = (float)(RIPPLE_COUNT - i) / (float)RIPPLE_COUNT;
        lv_obj_set_style_border_opa(s_ripples[i], (int)(179 * weight), 0);
        lv_obj_remove_flag(s_ripples[i], LV_OBJ_FLAG_CLICKABLE);
    }

    /* 启动动画定时器 */
    s_ripple_timer = lv_timer_create(ripple_tick_cb, RIPPLE_ANIM_MS, NULL);
}

/* 停止波纹动画 */
void ripple_anim_stop(void)
{
    if (s_ripple_timer) { lv_timer_del(s_ripple_timer); s_ripple_timer = NULL; }
    /* 删除容器会自动删除所有子对象 */
    if (s_ripple_container) { lv_obj_del(s_ripple_container); s_ripple_container = NULL; }
    for (int i = 0; i < RIPPLE_COUNT; i++) {
        s_ripples[i] = NULL;
    }
}

/****************************************************************************
 * 状态圆点动画函数
 ****************************************************************************/

/* 圆点脉冲动画回调 */
static void dot_pulse_tick_cb(lv_timer_t *t)
{
    (void)t;

    if (s_status_dot == NULL) return;

    s_dot_pulse_tick++;

    /* 脉冲效果：使用transform_zoom实现以圆心为中心的缩放
     * LVGL zoom范围：256 = 1.0倍，128 = 0.5倍，384 = 1.5倍 */
    float scale = 1.0f + 0.5f * sinf(s_dot_pulse_tick * 0.35f);
    int zoom = (int)(256 * scale);  /* 256 = 1.0倍 */

    lv_obj_set_style_transform_zoom(s_status_dot, zoom, 0);
}

/* 启动圆点脉冲动画 */
void status_dot_pulse_start(lv_color_t color)
{
    if (s_status_dot == NULL) return;

    /* 设置圆点颜色 */
    lv_obj_set_style_bg_color(s_status_dot, color, 0);

    /* 启动脉冲定时器 */
    if (s_dot_pulse_timer == NULL)
    {
        s_dot_pulse_tick = 0;
        s_dot_pulse_timer = lv_timer_create(dot_pulse_tick_cb, 50, NULL);
    }
}

/* 停止圆点脉冲动画（保持圆点显示） */
void status_dot_pulse_stop(void)
{
    if (s_dot_pulse_timer) { lv_timer_del(s_dot_pulse_timer); s_dot_pulse_timer = NULL; }

    /* 恢复圆点为正常大小（zoom = 256 = 1.0倍） */
    if (s_status_dot != NULL)
    {
        lv_obj_set_style_transform_zoom(s_status_dot, 256, 0);
    }
}

/* 创建状态胶囊容器（包含圆点和status_label） */
void status_pill_create(lv_obj_t *parent, lv_obj_t *status_label)
{
    if (s_status_pill != NULL) return;

    s_status_pill = lv_obj_create(parent);

    /* 设置圆角边框 */
    lv_obj_set_style_radius(s_status_pill, 22, 0);           /* 圆角半径 */
    lv_obj_set_style_border_width(s_status_pill, 2, 0);      /* 边框宽度 */
    lv_obj_set_style_border_color(s_status_pill, lv_color_hex(0xFFA502), 0);  /* 默认橙色 */
    lv_obj_set_style_border_opa(s_status_pill, 90, 0);       /* 35%透明度 */
    lv_obj_set_style_bg_color(s_status_pill, lv_color_hex(0xFFA502), 0);      /* 默认橙色 */
    lv_obj_set_style_bg_opa(s_status_pill, 25, 0);           /* 10%透明度 */

    /* padding: 上下10, 左右28 - 给圆点和label更多空间 */
    lv_obj_set_style_pad_top(s_status_pill, 10, 0);
    lv_obj_set_style_pad_bottom(s_status_pill, 10, 0);
    lv_obj_set_style_pad_left(s_status_pill, 28, 0);
    lv_obj_set_style_pad_right(s_status_pill, 28, 0);

    lv_obj_remove_flag(s_status_pill, LV_OBJ_FLAG_CLICKABLE);

    /* 使用 fit_content 自动调整大小 */
    lv_obj_set_width(s_status_pill, LV_SIZE_CONTENT);
    lv_obj_set_height(s_status_pill, LV_SIZE_CONTENT);

    /* 定位在status_label位置 */
    lv_obj_align(s_status_pill, LV_ALIGN_CENTER, 0, 20);
}

/* 创建状态圆点（在胶囊容器内，status_label左侧） */
void status_dot_create(lv_obj_t *pill, lv_obj_t *status_label)
{
    if (s_status_dot != NULL || pill == NULL) return;

    s_status_dot = lv_obj_create(pill);
    lv_obj_set_size(s_status_dot, 8, 8);
    lv_obj_set_style_bg_color(s_status_dot, lv_color_hex(0xFFA502), 0);  /* 默认橙色 */
    lv_obj_set_style_bg_opa(s_status_dot, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(s_status_dot, 4, 0);  /* 圆形 */
    lv_obj_set_style_border_width(s_status_dot, 0, 0);
    lv_obj_remove_flag(s_status_dot, LV_OBJ_FLAG_CLICKABLE);
    /* 设置缩放中心为圆点中心 */
    lv_obj_set_style_transform_pivot_x(s_status_dot, 4, 0);
    lv_obj_set_style_transform_pivot_y(s_status_dot, 4, 0);

    /* 定位在status_label左侧，间隔约一个圆点直径 */
    lv_obj_align_to(s_status_dot, status_label, LV_ALIGN_OUT_LEFT_MID, -7, 0);
}

/* 删除状态圆点 */
void status_dot_delete(void)
{
    status_dot_pulse_stop();
    if (s_status_dot) { lv_obj_del(s_status_dot); s_status_dot = NULL; }
    /* 删除胶囊容器 */
    if (s_status_pill) { lv_obj_del(s_status_pill); s_status_pill = NULL; }
}

/****************************************************************************
 * 就绪状态气泡胶囊（快捷命令）
 ****************************************************************************/

/* 创建气泡胶囊 */
static lv_obj_t* bubble_create(lv_obj_t *parent, const char *text, int y_offset)
{
    lv_obj_t *bubble = lv_obj_create(parent);
    if (bubble == NULL) return NULL;

    /* 气泡样式：圆角矩形，半透明背景 */
    lv_obj_set_size(bubble, 140, 36);
    lv_obj_set_style_bg_color(bubble, lv_color_hex(0x2D3436), 0);  /* 深灰色背景 */
    lv_obj_set_style_bg_opa(bubble, LV_OPA_60, 0);  /* 半透明 */
    lv_obj_set_style_radius(bubble, 18, 0);  /* 圆角 */
    lv_obj_set_style_border_width(bubble, 1, 0);
    lv_obj_set_style_border_color(bubble, lv_color_hex(0x636E72), 0);  /* 灰色边框 */
    lv_obj_set_style_border_opa(bubble, LV_OPA_40, 0);
    lv_obj_clear_flag(bubble, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(bubble, LV_OBJ_FLAG_CLICKABLE);  /* 不拦截手势 */
    lv_obj_set_style_clip_corner(bubble, false, 0);  /* 不裁剪 */

    /* 文字标签 */
    lv_obj_t *label = lv_label_create(bubble);
    lv_label_set_text(label, text);
    if (s_cjk_font) lv_obj_set_style_text_font(label, s_cjk_font, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(0xDFE6E9), 0);  /* 浅灰色文字 */
    lv_obj_center(label);

    /* 定位在subtitle_label下方 */
    lv_obj_align(bubble, LV_ALIGN_TOP_MID, 0, y_offset);

    return bubble;
}

/* 创建就绪状态气泡容器 */
void bubble_container_create(lv_obj_t *parent)
{
    /* 先清理旧的 */
    bubble_container_delete();

    s_bubble_container = lv_obj_create(parent);
    if (s_bubble_container == NULL) return;

    /* 容器透明，无边框，不裁剪子对象 */
    lv_obj_set_size(s_bubble_container, 280, 210);
    lv_obj_set_style_bg_opa(s_bubble_container, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_bubble_container, 0, 0);
    lv_obj_set_style_pad_all(s_bubble_container, 0, 0);
    lv_obj_clear_flag(s_bubble_container, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(s_bubble_container, LV_OBJ_FLAG_CLICKABLE);  /* 不拦截手势 */
    lv_obj_set_style_clip_corner(s_bubble_container, false, 0);  /* 不裁剪子对象 */

    /* 打开闹钟 气泡 */
    s_bubble_alarm = bubble_create(s_bubble_container, "设置闹钟", 125);
    /* 倒计时5分钟 气泡 */
    s_bubble_timer = bubble_create(s_bubble_container, "启动秒表", 165);

    /* 定位容器在subtitle_label下方 */
    lv_obj_align(s_bubble_container, LV_ALIGN_CENTER, 0, 75);
}

/* 删除气泡容器 */
void bubble_container_delete(void)
{
    if (s_bubble_alarm) { lv_obj_del(s_bubble_alarm); s_bubble_alarm = NULL; }
    if (s_bubble_timer) { lv_obj_del(s_bubble_timer); s_bubble_timer = NULL; }
    if (s_bubble_container) { lv_obj_del(s_bubble_container); s_bubble_container = NULL; }
}

/****************************************************************************
 * 识别中状态动效（三个紫色圆点 + 提示文字）
 ****************************************************************************/

/* 识别动效定时器回调：三个圆点依次缩放 */
static void recog_anim_tick_cb(lv_timer_t *t)
{
    (void)t;

    s_recog_anim_tick++;

    for (int i = 0; i < 3; i++)
    {
        if (s_recog_dots[i] == NULL) continue;

        /* 每个圆点延迟不同的相位，形成波浪效果 */
        float phase = s_recog_anim_tick * 0.3f - i * 0.8f;
        float scale = 0.6f + 0.4f * sinf(phase);
        int zoom = (int)(256 * scale);  /* 256 = 1.0倍 */

        lv_obj_set_style_transform_zoom(s_recog_dots[i], zoom, 0);
    }
}

/* 创建识别中状态动效 */
void recog_anim_create(lv_obj_t *parent)
{
    /* 先清理旧的 */
    recog_anim_delete();

    s_recog_dots_container = lv_obj_create(parent);
    if (s_recog_dots_container == NULL) return;

    /* 容器透明 */
    lv_obj_set_size(s_recog_dots_container, 200, 80);
    lv_obj_set_style_bg_opa(s_recog_dots_container, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_recog_dots_container, 0, 0);
    lv_obj_set_style_pad_all(s_recog_dots_container, 0, 0);
    lv_obj_clear_flag(s_recog_dots_container, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(s_recog_dots_container, LV_OBJ_FLAG_CLICKABLE);  /* 不拦截手势 */

    /* 三个紫色圆点 */
    int dot_size = 12;
    int gap = 16;
    int total_width = 3 * dot_size + 2 * gap;
    int x_start = (200 - total_width) / 2;

    for (int i = 0; i < 3; i++)
    {
        s_recog_dots[i] = lv_obj_create(s_recog_dots_container);
        if (s_recog_dots[i] == NULL) continue;

        lv_obj_set_size(s_recog_dots[i], dot_size, dot_size);
        lv_obj_set_style_bg_color(s_recog_dots[i], lv_color_hex(0x6C5CE7), 0);  /* 紫色 */
        lv_obj_set_style_bg_opa(s_recog_dots[i], LV_OPA_COVER, 0);
        lv_obj_set_style_radius(s_recog_dots[i], dot_size / 2, 0);  /* 圆形 */
        lv_obj_set_style_border_width(s_recog_dots[i], 0, 0);
        lv_obj_remove_flag(s_recog_dots[i], LV_OBJ_FLAG_CLICKABLE);
        /* 设置缩放中心为圆点中心 */
        lv_obj_set_style_transform_pivot_x(s_recog_dots[i], dot_size / 2, 0);
        lv_obj_set_style_transform_pivot_y(s_recog_dots[i], dot_size / 2, 0);

        lv_obj_align(s_recog_dots[i], LV_ALIGN_TOP_LEFT,
                     x_start + i * (dot_size + gap), 10);
    }

    /* "正在理解你的指令..." 提示文字 */
    s_recog_label = lv_label_create(s_recog_dots_container);
    lv_label_set_text(s_recog_label, "正在理解你的指令...");
    if (s_cjk_font) lv_obj_set_style_text_font(s_recog_label, s_cjk_font, 0);
    lv_obj_set_style_text_color(s_recog_label, lv_color_hex(0x6C5CE7), 0);  /* 紫色 */
    lv_obj_align(s_recog_label, LV_ALIGN_TOP_MID, 0, 50);

    /* 定位容器在状态胶囊下方 */
    lv_obj_align(s_recog_dots_container, LV_ALIGN_CENTER, 0, 100);

    /* 启动动画定时器 */
    s_recog_anim_tick = 0;
    s_recog_anim_timer = lv_timer_create(recog_anim_tick_cb, 50, NULL);
}

/* 删除识别中状态动效 */
void recog_anim_delete(void)
{
    if (s_recog_anim_timer) { lv_timer_del(s_recog_anim_timer); s_recog_anim_timer = NULL; }
    for (int i = 0; i < 3; i++)
    {
        if (s_recog_dots[i]) { lv_obj_del(s_recog_dots[i]); s_recog_dots[i] = NULL; }
    }
    if (s_recog_label) { lv_obj_del(s_recog_label); s_recog_label = NULL; }
    if (s_recog_dots_container) { lv_obj_del(s_recog_dots_container); s_recog_dots_container = NULL; }
}

/****************************************************************************
 * Playing动画函数 - 识别成功（√图标）
 ****************************************************************************/

/* 识别成功动画参数 */
#define SUCCESS_COLOR   0x6C5CE7  /* 紫色 */

/* 识别成功动画状态 */
static lv_obj_t *s_success_container = NULL;  /* 容器 */
static lv_obj_t *s_success_bg = NULL;         /* 背景圆 */
static lv_obj_t *s_success_icon = NULL;       /* √图标 */

/* 启动Playing动画 - 识别成功 */
void playing_anim_start(lv_obj_t *parent)
{
    /* 如果已经创建，先停止 */
    playing_anim_stop();

    /* 创建容器 */
    s_success_container = lv_obj_create(parent);
    if (s_success_container == NULL) {
        printf("[xiaozhi] playing_anim_start: failed to create container\n");
        return;
    }
    lv_obj_set_size(s_success_container, SUCCESS_SIZE + 20, SUCCESS_SIZE + 20);
    lv_obj_align(s_success_container, LV_ALIGN_CENTER, 0, -85);  /* 与按钮重叠 */
    lv_obj_set_style_bg_opa(s_success_container, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_success_container, 0, 0);
    lv_obj_set_style_pad_all(s_success_container, 0, 0);
    lv_obj_clear_flag(s_success_container, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(s_success_container, LV_OBJ_FLAG_CLICKABLE);  /* 不拦截手势 */
    lv_obj_clear_flag(s_success_container, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(s_success_container, LV_OBJ_FLAG_CLICKABLE);

    /* 创建背景圆 - 绿色半透明 */
    s_success_bg = lv_obj_create(s_success_container);
    if (s_success_bg == NULL) {
        printf("[xiaozhi] playing_anim_start: failed to create bg\n");
        lv_obj_del(s_success_container);
        s_success_container = NULL;
        return;
    }
    lv_obj_set_size(s_success_bg, SUCCESS_SIZE, SUCCESS_SIZE);
    lv_obj_align(s_success_bg, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_color(s_success_bg, lv_color_hex(SUCCESS_COLOR), 0);  /* 紫色 */
    lv_obj_set_style_bg_opa(s_success_bg, LV_OPA_80, 0);  /* 半透明 */
    lv_obj_set_style_radius(s_success_bg, SUCCESS_SIZE / 2, 0);  /* 圆形 */
    lv_obj_set_style_border_width(s_success_bg, 0, 0);
    lv_obj_remove_flag(s_success_bg, LV_OBJ_FLAG_CLICKABLE);

    /* 创建√图标 - 使用PNG图片 */
    s_success_icon = lv_image_create(s_success_container);
    if (s_success_icon == NULL) {
        printf("[xiaozhi] playing_anim_start: failed to create icon\n");
        lv_obj_del(s_success_container);
        s_success_container = NULL;
        s_success_bg = NULL;
        return;
    }
    lv_image_set_src(s_success_icon, "/emmc/success_check.png");
    lv_obj_align(s_success_icon, LV_ALIGN_CENTER, 0, 0);

}

/* 停止Playing动画 */
void playing_anim_stop(void)
{
    /* 删除容器会自动删除所有子对象 */
    if (s_success_container) { lv_obj_del(s_success_container); s_success_container = NULL; }
    s_success_icon = NULL;
    s_success_bg = NULL;
}

/*====================================================
 * 手势事件处理：右滑返回到applist
 *====================================================*/
static void xiaozhi_gesture_event_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    lv_indev_t *indev = lv_indev_active();
    if (!indev) return;

    lv_dir_t dir = lv_indev_get_gesture_dir(indev);

    switch (code)
    {
    case LV_EVENT_GESTURE:
        if (dir == LV_DIR_RIGHT)
        {
            LV_LOG_USER("xiaozhi: LV_DIR_RIGHT - exit (entry_page=%d)", s_xiaozhi_entry_page);
            /* 隐藏AI应用UI */
            xiaozhi_voice_delete();

            /* Give the connect_thread time to notice app_running==false,
             * join recv_thread, and exit. Without this delay, the old
             * connect_thread is still running when nav_return_from_app()
             * triggers a new xiaozhi instance, causing use-after-free
             * when the new instance resets g_app while the old thread
             * is still accessing it. connect_thread checks app_running
             * every 200ms, so 300ms is enough for it to exit. */
            usleep(100 * 1000);

            /* 恢复进入时的原始入口页面（PAGE_HOME 或 PAGE_APPLIST）。
             * xiaozhi_voice_start() 入口处已快照 g_prev_page 到
             * s_xiaozhi_entry_page，MCP 工具可能将其改为 PAGE_VOICE，
             * 这里用快照值确保右滑退出到正确的页面。 */
            nav_enter_app_from(s_xiaozhi_entry_page);

            /* 返回到applist */
            nav_return_from_app();
            /* CRITICAL: Re-enable LVGL rendering after returning to applist.
             * xiaozhi_voice_delete() sets g_lvgl_stop_rendering = true to
             * prevent TLSF heap corruption during cleanup. But nav_return_from_app()
             * creates new LVGL objects that need rendering. We must re-enable
             * rendering now that cleanup is complete. */
            extern volatile bool g_lvgl_stop_rendering;
            g_lvgl_stop_rendering = false;
        }
        break;
    default:
        break;
    }
}





#define XIAOZHI_MAC_DEFAULT "6b:48:a8:95:7d:e1"
#define XZ_OPUS_MAX         512

/* Speak button UX tuning. */
#define XZ_MIN_INPUT_MS     500    /* press shorter than this = accidental */
/* RMS threshold for "did the user actually speak?". Measured on this board:
 * silence ~0, normal speech ~60-100. So 30 sits clearly above the noise
 * floor but below real speech. If you see false rejects, lower it; if
 * ambient noise triggers false accepts, raise it. */
#define XZ_SPEAK_RMS_THRESH 30

/* Audio capture buffer: while Speak is held, opus frames are captured into
 * this ring of buffers (NOT sent yet). On release, if the input is valid
 * (min duration + RMS), the whole buffer is drained to the server in one
 * shot between listen:start and listen:stop. ~10s max hold at 60ms/frame. */
#define XZ_FRAME_BUF_COUNT  170    /* 170 * 60ms ~= 10.2s max */

struct xz_frame_buf_s
{
  uint16_t len;                    /* opus payload length, 0 = empty slot */
  uint16_t pad;
  uint8_t  data[XZ_OPUS_MAX];
};

/* 小智二进制音频帧头（协议版本 v3）。
 * 服务器靠这个头定位 opus 负载长度；直接发裸 opus 帧会导致服务器把前 4
 * 字节误当头解析，音频错位/截断，ASR 识别不准。
 *   type=0 (OPUS), reserved=0, payload_size=htons(帧长) */
struct __attribute__((packed)) xz_binary_hdr_s
{
  uint8_t  type;
  uint8_t  reserved;
  uint16_t payload_size;
};

/* App state shown on screen. */
#define XZ_STATE_BOOT       0     /* starting up */
#define XZ_STATE_OTA        1     /* fetching OTA config */
#define XZ_STATE_CONNECT    2     /* wss connect + hello */
#define XZ_STATE_READY      3     /* handshake done, Speak button live */
#define XZ_STATE_LISTENING  4     /* capturing mic + uplink */
#define XZ_STATE_THINKING   5     /* waiting for stt/tts */
#define XZ_STATE_PLAYING    6     /* TTS playback */
#define XZ_STATE_DONE       7     /* turn finished */
#define XZ_STATE_ERROR      8     /* error */

/* 按钮图片路径定义 */
#define VOICE_BTN_CONNECTING  "/emmc/voice_button_connecting.png"
#define VOICE_BTN_READY       "/emmc/voice_button_ready.png"
#define VOICE_BTN_LISTENING   "/emmc/voice_button_listening.png"
#define VOICE_BTN_RECOGNIZING "/emmc/voice_button_recognizing.png"

/****************************************************************************
 * Private Types
 ****************************************************************************/

struct xiaozhi_app_s
{
  struct xiaozhi_ws_s        ws;
  struct xiaozhi_audio_s    *audio;

  /* Shared dialog state (written by worker threads, read by UI thread). */
  volatile int  state;
  char          session_id[16];
  bool          ws_connected;
  bool          mcp_done;       /* MCP handshake complete (tools/list sent). */
  bool          got_stt;
  bool          tts_playing;
  int           tts_sentences;

  /* Latest text to show: the STT transcript or the TTS sentence. */
  char          stt_text[128];
  char          tts_text[128];

  /* UI handles (main thread only). */
  lv_obj_t     *status_label;
  lv_obj_t     *subtitle_label;
  lv_obj_t     *stt_label;
  lv_obj_t     *tts_label;
  lv_obj_t     *speak_btn;
  lv_obj_t     *speak_label;

  /* Lifecycle flags. */
  volatile bool handshake_done;    /* OTA+hello+MCP complete, Speak live */
  volatile bool handshake_failed;
  volatile bool ws_dead;           /* connection lost / goodbye received */
  volatile bool app_running;       /* false = app being deleted, exit threads */
  volatile bool ws_reconnecting;   /* recv_thread: transient error, try light reconnect */
  volatile uint32_t reconnect_ts;  /* tick when reconnect started */

  /* Live capture state for the Speak button (UI thread writes press/release,
   * capture thread reads while holding). */
  volatile bool capturing;        /* PRESSED received, capturing mic */
  volatile bool capture_stop;      /* RELEASED received, stop capture loop */
  volatile bool dialog_running;   /* a dialog round is in progress */
  uint32_t      speak_press_ms;   /* tick at Speak press (for min duration) */

  /* Buffered opus frames captured while Speak is held. */
  struct xz_frame_buf_s frame_buf[XZ_FRAME_BUF_COUNT];
  volatile int frame_buf_cnt;      /* number of valid frames captured */
  pthread_t capture_tid;          /* capture thread id (for join on release) */
  pthread_t recv_tid;            /* recv thread id (for join on delete) */
  pthread_t connect_tid;         /* connect thread id (for join on delete) */
  bool       capture_tid_valid;
  bool       recv_tid_valid;     /* recv_tid is valid and joinable */
  bool       connect_tid_valid;  /* connect_tid is valid and joinable */
  volatile bool capture_started;  /* capture_thread has finished start_capture */
};

/****************************************************************************
 * Private Data
 ****************************************************************************/

static struct xiaozhi_app_s g_app;
static lv_timer_t *ui_refresh_timer = NULL;

/* Friendly state names for the status line. */
static const char *const g_state_names[] =
{
  "正在连接...",      /* BOOT */
  "正在连接...",      /* OTA */
  "连接中",           /* CONNECT */
  "就绪",             /* READY */
  "请说话",           /* LISTENING */
  "识别中",           /* THINKING */
  "识别成功",         /* PLAYING */
  "完成",             /* DONE */
  "连接中",           /* ERROR (显示为连接中) */
};

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/* Sanitize UTF-8 text: replace fullwidth CJK punctuation with ASCII
 * equivalents so the limited CJK font (basic ideographs only) can render
 * all visible characters without falling back to [][].
 */
static void sanitize_text(char *dst, size_t dstsize, const char *src)
{
  size_t di = 0;
  const unsigned char *s = (const unsigned char *)src;

  while (*s && di < dstsize - 1)
    {
      /* 3-byte UTF-8: U+0800 .. U+FFFF */
      if (s[0] >= 0xe0 && s[1] >= 0x80 && s[1] < 0xc0 &&
          s[2] >= 0x80 && s[2] < 0xc0)
        {
          unsigned int cp = ((unsigned int)(s[0] & 0x0f) << 12) |
                            ((unsigned int)(s[1] & 0x3f) << 6) |
                            ((unsigned int)(s[2] & 0x3f));
          const char *r = NULL;

          switch (cp)
            {
              case 0xff0c: r = ",";  break;  /* ， */
              case 0x3002: r = ".";  break;  /* 。 */
              case 0xff01: r = "!";  break;  /* ！ */
              case 0xff1f: r = "?";  break;  /* ？ */
              case 0xff1a: r = ":";  break;  /* ： */
              case 0xff1b: r = ";";  break;  /* ； */
              case 0x3001: r = ",";  break;  /* 、 */
              case 0x201c: r = "\""; break;  /* " */
              case 0x201d: r = "\""; break;  /* " */
              case 0x2018: r = "'";  break;  /* ' */
              case 0x2019: r = "'";  break;  /* ' */
              case 0xff08: r = "(";  break;  /* （ */
              case 0xff09: r = ")";  break;  /* ） */
              case 0x300a: r = "<<"; break;  /* 《 */
              case 0x300b: r = ">>"; break;  /* 》 */
              case 0x2026: r = "..."; break; /* … */
              case 0x2014: r = "--"; break;  /* — */
              default:
                {
                  /* Keep the original character if it's in CJK range */
                  if (cp >= 0x4e00 && cp <= 0x9fa5)
                    {
                      dst[di++] = s[0];
                      dst[di++] = s[1];
                      dst[di++] = s[2];
                    }
                  else
                    {
                      /* Unknown char: skip */
                    }
                  s += 3;
                  continue;
                }
            }

          /* Write replacement string */
          while (*r && di < dstsize - 1)
            {
              dst[di++] = *r++;
            }

          s += 3;
        }
      /* 2-byte UTF-8: U+0080 .. U+07FF (skip, not CJK) */
      else if (s[0] >= 0xc0 && s[0] < 0xe0 && s[1] >= 0x80)
        {
          s += 2;
        }
      /* ASCII */
      else if (s[0] < 0x80)
        {
          dst[di++] = *s++;
        }
      else
        {
          /* Invalid UTF-8 byte, skip */
          s++;
        }
    }

  dst[di] = '\0';
}

/* Send a JSON text frame built with cJSON. */
static int send_json(struct xiaozhi_app_s *app, cJSON *root)
{
  char *json = cJSON_PrintUnformatted(root);
  if (json == NULL)
    {
      return -ENOMEM;
    }

  int ret = xiaozhi_ws_send_text(&app->ws, json, strlen(json));
  free(json);
  return ret;
}

/* Capture thread: while Speak is held, read opus frames from the mic and
 * store them in the frame buffer (NOT sent yet). Stops when the user
 * releases Speak (capture_stop) or the buffer fills.
 */
static void *capture_thread(void *arg)
{
  struct xiaozhi_app_s *app = arg;

  /* start_capture touches the SMF codec and can block ~150ms (codec init).
   * It MUST run off the LVGL thread, otherwise the touch indev loses the
   * PRESSED state and fires a spurious RELEASED. */
  int ret = xiaozhi_audio_start_capture(app->audio);
  app->capture_started = true;   /* set even on failure so round thread stops */
  if (ret != 0)
    {
      printf("[xiaozhi] capture: start_capture failed: %d\n", ret);
      app->capturing = false;
      return NULL;
    }

  while (app->capturing && !app->capture_stop && app->ws_connected &&
         app->frame_buf_cnt < XZ_FRAME_BUF_COUNT)
    {
      ssize_t n = xiaozhi_audio_read_opus(app->audio,
                                          app->frame_buf[app->frame_buf_cnt].data,
                                          XZ_OPUS_MAX);
      if (n < 0)
        {
          break;
        }

      if (n > 0)
        {
          app->frame_buf[app->frame_buf_cnt].len = (uint16_t)n;
          app->frame_buf_cnt++;
        }
    }

  app->capturing = false;
  return NULL;
}

/* Drain all buffered opus frames to the server as binary frames, wrapping
 * each in the BinaryProtocol3 header. Called after the validity check passes.
 */
static void drain_buffer_to_server(struct xiaozhi_app_s *app)
{
  int cnt = app->frame_buf_cnt;
  for (int i = 0; i < cnt && app->ws_connected; i++)
    {
      uint16_t n = app->frame_buf[i].len;
      if (n == 0)
        {
          continue;
        }

      struct xz_binary_hdr_s *hdr;
      uint8_t outbuf[sizeof(*hdr) + XZ_OPUS_MAX];

      hdr = (struct xz_binary_hdr_s *)outbuf;
      hdr->type = 0;                 /* 0 = OPUS */
      hdr->reserved = 0;
      hdr->payload_size = htons(n);
      memcpy(outbuf + sizeof(*hdr), app->frame_buf[i].data, n);

      int ret = xiaozhi_ws_send_binary(&app->ws, outbuf, sizeof(*hdr) + n);
      if (ret != 0)
        {
          break;
        }
    }
}


/* Background receive thread: parses server messages and feeds TTS audio to
 * the player. Runs for the lifetime of the wss connection — it is NOT a
 * one-shot loop. Per-turn events (stt, tts start/stop) flip flags that the
 * dialog-round thread waits on.
 *
 * The handshake phase (hello + MCP tools/list) is also handled here: the
 * connect thread sends hello, then joins (waits on) this thread's processing
 * of the hello response + MCP exchange via the handshake_done flag.
 */
static void *recv_thread(void *arg)
{
  struct xiaozhi_app_s *app = arg;

  while (app->ws_connected)
    {
      const uint8_t *payload;
      size_t plen;
      bool is_binary;
      int ret;

      ret = xiaozhi_ws_recv(&app->ws, &payload, &plen, &is_binary);
      if (ret != 0)
        {
          if (app->state == XZ_STATE_READY)
            {
              app->ws_reconnecting = true;
              app->reconnect_ts = lv_tick_get();
              app->ws_connected = false;
            }
          else
            {
              app->ws_connected = false;
              app->ws_dead = true;
            }
          break;
        }

      if (is_binary)
        {
          /* Server TTS Opus frame -> feed to player. */
          if (app->tts_playing)
            {
              xiaozhi_audio_write_opus(app->audio, payload, plen);
            }
          continue;
        }

      /* Text frame: parse JSON. */
      {
        char *json = malloc(plen + 1);
        if (json == NULL)
          {
            continue;
          }

        memcpy(json, payload, plen);
        json[plen] = '\0';

        cJSON *root = cJSON_Parse(json);
        if (root != NULL)
          {
            cJSON *type = cJSON_GetObjectItem(root, "type");
            const char *tstr = cJSON_IsString(type) ?
              type->valuestring : "";

            if (strcmp(tstr, "stt") == 0)
              {
                cJSON *text = cJSON_GetObjectItem(root, "text");
                const char *s = text ? text->valuestring : "?";
                app->got_stt = true;
                sanitize_text(app->stt_text, sizeof(app->stt_text), s);
                /* 同步到MCP工具可用的静态缓冲区 */
                strncpy(s_mcp_stt_text, app->stt_text, sizeof(s_mcp_stt_text) - 1);
                s_mcp_stt_text[sizeof(s_mcp_stt_text) - 1] = '\0';
              }
            else if (strcmp(tstr, "llm") == 0)
              {
                /* LLM text response - no action needed */
              }
            else if (strcmp(tstr, "tts") == 0)
              {
                cJSON *state = cJSON_GetObjectItem(root, "state");
                const char *sstr = cJSON_IsString(state) ?
                  state->valuestring : "";

                if (strcmp(sstr, "start") == 0)
                  {
                    pthread_mutex_lock(&s_ui_mutex);
                    app->tts_playing = true;
                    app->state = XZ_STATE_PLAYING;
                    pthread_mutex_unlock(&s_ui_mutex);
                    xiaozhi_audio_start_playback(app->audio);
                  }
                else if (strcmp(sstr, "stop") == 0)
                  {
                    app->tts_playing = false;
                    xiaozhi_audio_stop_playback(app->audio);
                  }
                else if (strcmp(sstr, "sentence_start") == 0)
                  {
                    cJSON *text = cJSON_GetObjectItem(root, "text");
                    const char *s = text ? text->valuestring : "?";
                    app->tts_sentences++;
                    sanitize_text(app->tts_text, sizeof(app->tts_text), s);
                  }
              }
            else if (strcmp(tstr, "goodbye") == 0)
              {
                app->ws_connected = false;
                app->ws_dead = true;
                cJSON_Delete(root);
                free(json);
                break;
              }
            else if (strcmp(tstr, "mcp") == 0)
              {
                cJSON *payload_obj = cJSON_GetObjectItem(root, "payload");
                cJSON *method = payload_obj ?
                  cJSON_GetObjectItem(payload_obj, "method") : NULL;
                bool is_tools_list = cJSON_IsString(method) &&
                  strcmp(method->valuestring, "tools/list") == 0;

                mcp_server_handle_message(root, mcp_send_json);

                if (is_tools_list)
                  {
                    app->mcp_done = true;
                  }
              }
            else if (strcmp(tstr, "hello") == 0)
              {
                cJSON *sid = cJSON_GetObjectItem(root, "session_id");
                if (cJSON_IsString(sid))
                  {
                    strncpy(app->session_id, sid->valuestring,
                            sizeof(app->session_id) - 1);
                    app->session_id[sizeof(app->session_id) - 1] = '\0';
                  }
              }
            else
              {
                /* unhandled message type */
              }

            cJSON_Delete(root);
          }
        else
          {
            printf("[xiaozhi] <<< (parse err) %.*s\n",
                   (int)plen, (const char *)payload);
          }

        free(json);
      }
    }

  return NULL;
}

/* Send the hello handshake JSON. */
static void send_hello(struct xiaozhi_app_s *app)
{
  cJSON *hello = cJSON_CreateObject();
  cJSON_AddStringToObject(hello, "type", "hello");
  cJSON_AddNumberToObject(hello, "version", 3);
  cJSON_AddStringToObject(hello, "transport", "websocket");

  cJSON *ap = cJSON_CreateObject();
  cJSON_AddStringToObject(ap, "format", "opus");
  cJSON_AddNumberToObject(ap, "sample_rate", 16000);
  cJSON_AddNumberToObject(ap, "channels", 1);
  cJSON_AddNumberToObject(ap, "frame_duration", 60);
  cJSON_AddItemToObject(hello, "audio_params", ap);

  mcp_server_add_hello_features(hello);

  char *s = cJSON_PrintUnformatted(hello);
  if (s)
    {
      free(s);
    }
  send_json(app, hello);
  cJSON_Delete(hello);
}

/* WiFi ready flag (defined below, used by connect_thread) */
static volatile bool s_wifi_ready = false;

/* Connect thread: runs once at app open. Does OTA -> wss -> hello -> MCP,
 * updating the status label at each node. Then launches the background
 * recv_thread and signals handshake_done so the Speak button goes live.
 */
static void *connect_thread(void *arg)
{
  struct xiaozhi_app_s *app = arg;
  struct xiaozhi_ota_config_s ota;
  const char *mac = XIAOZHI_MAC_DEFAULT;
  char client_id[40];
  int ret;

  /* 等待WiFi连接就绪 */
  while (!s_wifi_ready && app->app_running)
    {
      usleep(100000);  /* 100ms */
    }
  if (!app->app_running)
    {
      app->connect_tid_valid = false;
      return NULL;
    }

  snprintf(client_id, sizeof(client_id),
           "00000000-0000-0000-0000-%08x", 0xa1b2c3d7);

  /* Init audio subsystem (creates Opus encoder) — done once. */
  app->audio = xiaozhi_audio_init();
  if (app->audio == NULL)
    {
      printf("[xiaozhi] audio init failed\n");
      app->state = XZ_STATE_ERROR;
      app->handshake_failed = true;
      app->connect_tid_valid = false;
      return NULL;
    }

  /* Init MCP tool server — done once. */
  s_mcp_app = app;
  mcp_register_tools();

  /* Reconnect loop: keep trying to establish a session. */
  while (app->app_running)
    {
      /* Step 1: OTA. */
      pthread_mutex_lock(&s_ui_mutex);
      app->state = XZ_STATE_OTA;
      pthread_mutex_unlock(&s_ui_mutex);
      ret = xiaozhi_ota_fetch(mac, &ota);
      if (ret != 0)
        {
          printf("[xiaozhi] OTA failed: %d, retrying in 5s\n", ret);
          app->state = XZ_STATE_ERROR;
          for (int i = 0; i < 50 && app->app_running; i++)
            {
              usleep(100 * 1000);
            }
          continue;
        }

      /* Step 2: wss connect (TLS + ws upgrade). */
      pthread_mutex_lock(&s_ui_mutex);
      app->state = XZ_STATE_CONNECT;
      pthread_mutex_unlock(&s_ui_mutex);
      ret = xiaozhi_ws_connect(&app->ws, ota.ws_url, ota.ws_token, mac,
                               client_id);
      if (ret != 0)
        {
          printf("[xiaozhi] ws connect failed: %d, retrying in 5s\n", ret);
          app->state = XZ_STATE_ERROR;
          for (int i = 0; i < 50 && app->app_running; i++)
            {
              usleep(100 * 1000);
            }
          continue;
        }

      app->ws_connected = true;

      /* Launch the background receive thread BEFORE sending hello, so we
       * don't miss the server's hello/MCP response. */
      pthread_attr_t attr;
      pthread_attr_init(&attr);
      pthread_attr_setstacksize(&attr, 32768);
      ret = pthread_create(&app->recv_tid, &attr, recv_thread, app);
      pthread_attr_destroy(&attr);
      if (ret != 0)
        {
          printf("[xiaozhi] recv thread create failed: %d\n", ret);
          if (!app->app_running) break;
          xiaozhi_ws_close(&app->ws);
          app->ws_connected = false;
          app->recv_tid_valid = false;
          app->state = XZ_STATE_ERROR;
          for (int i = 0; i < 50 && app->app_running; i++)
            {
              usleep(100 * 1000);
            }
          continue;
        }
      app->recv_tid_valid = true;

      /* Step 3: send hello. recv_thread will capture session_id. */
      send_hello(app);

      /* Wait for the server hello response (session_id). */
      int waited = 0;
      while (app->session_id[0] == '\0' && app->ws_connected && app->app_running && waited < 8000)
        {
          usleep(100 * 1000);
          waited += 100;
        }
      if (app->session_id[0] == '\0')
        {
          printf("[xiaozhi] no session_id after hello, retrying\n");
          app->ws_connected = false;
          if (!app->app_running) break;
          xiaozhi_ws_close(&app->ws);
          /* Sleep 5s but check app_running every 100ms so we can exit
           * quickly when the user swipes to quit. */
          for (int i = 0; i < 50 && app->app_running; i++)
            {
              usleep(100 * 1000);
            }
          if (!app->app_running) break;
          continue;
        }

      /* Step 3b: wait for MCP initialize (tools/list). */
      waited = 0;
      while (!app->mcp_done && app->ws_connected && app->app_running && waited < 5000)
        {
          usleep(100 * 1000);
          waited += 100;
        }

      if (!app->ws_connected)
        {
          printf("[xiaozhi] connection lost during handshake, retrying\n");
          if (!app->app_running) break;
          xiaozhi_ws_close(&app->ws);
          for (int i = 0; i < 50 && app->app_running; i++)
            {
              usleep(100 * 1000);
            }
          if (!app->app_running) break;
          continue;
        }

      /* Handshake complete — Speak button goes live. */
      pthread_mutex_lock(&s_ui_mutex);
      app->handshake_done = true;
      app->state = XZ_STATE_READY;
      pthread_mutex_unlock(&s_ui_mutex);

      /* Keep this thread alive so recv_thread persists.
       * When the connection drops, recv_thread exits, ws_connected goes
       * false, and we fall through to cleanup + retry. */
wait_connected:
      while (app->ws_connected && app->app_running)
        {
          usleep(200 * 1000);
        }

      /* If the app is being deleted, do NOT call xiaozhi_ws_close() here.
       * xiaozhi_voice_delete() already closed the socket fd. If we call
       * xiaozhi_ws_close() here, it will free the SSL context while
       * recv_thread is still using it in mbedtls_ssl_read(), causing a
       * use-after-free crash (MMFAR=0x15c). Just return and let
       * xiaozhi_voice_delete() handle cleanup. */
      if (!app->app_running)
        {
          /* Wait for recv_thread to exit before we return.
           * xiaozhi_voice_delete() closed the socket fd, which unblocks
           * mbedtls_ssl_read in recv_thread. We must join recv_thread
           * here to prevent use-after-free when the new instance starts
           * and reinitializes g_app.ws while recv_thread is still using it. */
          if (app->recv_tid_valid)
            {
              pthread_join(app->recv_tid, NULL);
              app->recv_tid_valid = false;
            }
          app->connect_tid_valid = false;
          return NULL;
        }

      if (app->tts_playing)
        {
          xiaozhi_audio_stop_playback(app->audio);
        }
      xiaozhi_ws_close(&app->ws);

      /* Reset per-connection state for the next attempt.
       * NOTE: Do NOT clear s_mcp_stt_text here — the STT text may be needed
       * by an MCP tool_call that arrives after reconnection. It will be
       * cleared when the round completes or when a new STT text arrives.
       * If we're in LISTENING state (mid-capture), keep capturing=true
       * so the capture thread can continue and the round can complete. */
      pthread_mutex_lock(&s_ui_mutex);
      app->handshake_done = false;
      app->mcp_done = false;
      app->ws_dead = false;
      /* Only clear capturing if we're NOT in LISTENING state */
      if (app->state != XZ_STATE_LISTENING)
        {
          app->capturing = false;
        }
      app->capture_stop = false;
      app->dialog_running = false;
      app->capture_tid_valid = false;
      app->capture_started = false;
      app->frame_buf_cnt = 0;
      app->stt_text[0] = '\0';
      /* s_mcp_stt_text intentionally NOT cleared — see comment above */
      app->tts_text[0] = '\0';
      memset(app->session_id, 0, sizeof(app->session_id));
      if (app->state == XZ_STATE_LISTENING)
        {
          /* Keep LISTENING state — don't switch to ERROR.
           * Wait for capture to finish, then reconnect. */
          pthread_mutex_unlock(&s_ui_mutex);

          /* Stop the recorder so xiaozhi_audio_read_opus() unblocks */
          if (app->audio != NULL)
            {
              xiaozhi_audio_stop_capture(app->audio);
            }

          /* Wait for capture_thread to exit */
          if (app->capture_tid_valid)
            {
              pthread_join(app->capture_tid, NULL);
              app->capture_tid_valid = false;
            }
          app->capturing = false;
          app->capture_started = false;

          /* Reset state from LISTENING to allow reconnect */
          pthread_mutex_lock(&s_ui_mutex);
          app->state = XZ_STATE_READY;
          pthread_mutex_unlock(&s_ui_mutex);
          /* Fall through to reconnect logic below */
        }
      pthread_mutex_unlock(&s_ui_mutex);

      /* Light reconnect path: if the disconnect happened while in READY
       * state (e.g. transient TLS read error), try reconnecting at the
       * WebSocket level only — skip OTA since we already have a valid
       * token.  This avoids the READY→CONNECTING→READY UI flicker. */
      if (app->ws_reconnecting)
        {
          bool light_ok = false;
          for (int i = 0; i < 5 && app->app_running && !light_ok; i++)
            {
              ret = xiaozhi_ws_connect(&app->ws, ota.ws_url, ota.ws_token,
                                       mac, client_id);
              if (ret == 0)
                {
                  light_ok = true;
                }
              else
                {
                  for (int j = 0; j < 10 && app->app_running; j++)
                    {
                      usleep(100 * 1000);
                    }
                }
            }

          if (light_ok)
            {
              /* Success — resume recv_thread and go back to wait. */
              pthread_mutex_lock(&s_ui_mutex);
              app->ws_connected = true;
              app->ws_reconnecting = false;
              app->handshake_done = true;
              app->state = XZ_STATE_READY;
              pthread_mutex_unlock(&s_ui_mutex);

              pthread_attr_t attr2;
              pthread_attr_init(&attr2);
              pthread_attr_setstacksize(&attr2, 32768);
              ret = pthread_create(&app->recv_tid, &attr2, recv_thread, app);
              if (ret != 0)
                {
                  app->ws_connected = false;
                  app->ws_dead = true;
                  app->recv_tid_valid = false;
                }
              else
                {
                  app->recv_tid_valid = true;
                  /* Go back to the ws_connected wait loop instead of
                   * restarting OTA. */
                  goto wait_connected;
                }
            }
          else
            {
              /* Light reconnect failed — fall through to full
               * OTA+CONNECT. */
              app->ws_reconnecting = false;
            }
        }

      for (int i = 0; i < 30 && app->app_running; i++)
        {
          usleep(100 * 1000);  /* 100ms */
        }
    }

  /* App is being deleted (app_running is false).
   * Skip cleanup here — xiaozhi_voice_delete handles it after joining
   * all threads. This avoids double-free and use-after-free issues when
   * resources are cleaned up in a different order. */
  app->connect_tid_valid = false;  /* Signal that we've exited */
  return NULL;
}

/* Dialog-round thread: triggered by Speak release.
 * Joins the capture thread (finishes buffering), runs the validity check
 * (min duration + RMS), and if valid: sends listen:start -> drains buffered
 * opus frames -> listen:stop, then waits for the server stt/tts playback.
 * The persistent recv_thread handles the incoming stt/tts frames.
 */
static void *dialog_round_thread(void *arg)
{
  struct xiaozhi_app_s *app = arg;

  /* Signal the capture thread to stop. It may be blocked inside
   * xiaozhi_audio_read_opus() on media_recorder_read_data(); we must
   * stop the recorder first so that read unblocks, THEN join.
   * But wait for start_capture to finish first — stopping before the
   * recorder is open would race the capture thread's setup. */
  app->capture_stop = true;
  app->capturing = false;
  int spin = 0;
  while (!app->capture_started && spin < 3000)
    {
      usleep(10 * 1000);
      spin += 10;
    }

  xiaozhi_audio_stop_capture(app->audio);

  if (app->capture_tid_valid)
    {
      pthread_join(app->capture_tid, NULL);
      app->capture_tid_valid = false;
    }
  app->capture_started = false;

  uint32_t dur = lv_tick_get() - app->speak_press_ms;
  int rms = xiaozhi_audio_rms_get(app->audio);

  /* Validity check: min duration + actual speech. */
  if (dur < XZ_MIN_INPUT_MS)
    {
      goto back_to_ready;
    }

  if (rms < XZ_SPEAK_RMS_THRESH)
    {
      goto back_to_ready;
    }

  /* Valid input — send listen:start, drain buffered audio, listen:stop. */
  if (!app->ws_connected)
    {
      goto back_to_ready;
    }

  pthread_mutex_lock(&s_ui_mutex);
  app->state = XZ_STATE_THINKING;
  pthread_mutex_unlock(&s_ui_mutex);

  {
    cJSON *listen = cJSON_CreateObject();
    cJSON_AddStringToObject(listen, "session_id", app->session_id);
    cJSON_AddStringToObject(listen, "type", "listen");
    cJSON_AddStringToObject(listen, "state", "start");
    cJSON_AddStringToObject(listen, "mode", "manual");
    send_json(app, listen);
    cJSON_Delete(listen);
  }

  drain_buffer_to_server(app);

  {
    cJSON *stop = cJSON_CreateObject();
    cJSON_AddStringToObject(stop, "session_id", app->session_id);
    cJSON_AddStringToObject(stop, "type", "listen");
    cJSON_AddStringToObject(stop, "state", "stop");
    send_json(app, stop);
    cJSON_Delete(stop);
  }

  /* Wait for STT + TTS playback. recv_thread flips tts_playing true (tts
   * start) then false (tts stop). */
  pthread_mutex_lock(&s_ui_mutex);
  app->state = XZ_STATE_THINKING;
  pthread_mutex_unlock(&s_ui_mutex);

  int waited = 0;
  while (!app->tts_playing && app->ws_connected && waited < 15000)
    {
      usleep(100 * 1000);
      waited += 100;
    }
  while (app->tts_playing && app->ws_connected)
    {
      usleep(100 * 1000);
    }

  /* If the connection dropped during THINKING, the audio was already sent
   * and the server's response is lost.  Just fail the round and go back
   * to READY — don't wait for reconnect because the new session won't
   * have the old conversation context. */
  if (!app->ws_connected && app->state == XZ_STATE_THINKING)
    {
      /* round failed due to connection loss */
    }

back_to_ready:
  /* Round done — back to ready (if still connected). */
  pthread_mutex_lock(&s_ui_mutex);
  if (app->ws_connected)
    {
      app->state = XZ_STATE_READY;
    }
  else
    {
      app->state = XZ_STATE_ERROR;
    }
  /* Clear per-round display state for the next turn. */
  app->capturing = false;
  app->capture_stop = false;
  app->got_stt = false;
  app->frame_buf_cnt = 0;
  memset(app->stt_text, 0, sizeof(app->stt_text));
  memset(app->tts_text, 0, sizeof(app->tts_text));
  pthread_mutex_unlock(&s_ui_mutex);

  app->dialog_running = false;
  return NULL;
}

/* Speak button event handler.
 * PRESSED: start capturing into the frame buffer (background capture thread).
 * RELEASED: stop capturing and launch the dialog-round thread, which joins
 *           the capture, runs the validity check, and sends to the server.
 */
static void speak_btn_event_cb(lv_event_t *e)
{
  struct xiaozhi_app_s *app = &g_app;
  lv_event_code_t code = lv_event_get_code(e);

  if (!app->handshake_done || app->ws_dead || !app->ws_connected)
    {
      /* If we're mid-capture (connection died while holding Speak), clean
       * up the capture state so the UI doesn't stay stuck in LISTENING. */
      if (app->capturing)
        {
          app->capturing = false;
          app->capture_stop = true;

          pthread_mutex_lock(&s_ui_mutex);
          app->stt_text[0] = '\0';
          s_mcp_stt_text[0] = '\0';
          app->tts_text[0] = '\0';
          app->state = XZ_STATE_ERROR;
          pthread_mutex_unlock(&s_ui_mutex);
        }
      return;
    }

  if (app->dialog_running)
    {
      return;   /* a round is in progress */
    }

  if (code == LV_EVENT_PRESSED)
    {
      app->speak_press_ms = lv_tick_get();
      app->capturing = true;
      app->capture_stop = false;
      app->frame_buf_cnt = 0;
      xiaozhi_audio_rms_reset(app->audio);

      pthread_mutex_lock(&s_ui_mutex);
      app->state = XZ_STATE_LISTENING;
      pthread_mutex_unlock(&s_ui_mutex);

      /* Launch the capture thread — it calls start_capture (which blocks on
       * the SMF codec ~150ms) and then buffers opus frames. Doing start_capture
       * here on the LVGL thread would stall the touch indev and drop the press. */
      pthread_attr_t attr;
      pthread_attr_init(&attr);
      pthread_attr_setstacksize(&attr, 32768);
      int ret = pthread_create(&app->capture_tid, &attr, capture_thread, app);
      pthread_attr_destroy(&attr);
      if (ret != 0)
        {
          printf("[xiaozhi] speak: capture thread failed: %d\n", ret);
          app->capturing = false;
          pthread_mutex_lock(&s_ui_mutex);
          app->state = XZ_STATE_READY;
          pthread_mutex_unlock(&s_ui_mutex);
        }
      app->capture_tid_valid = true;
    }
  else if (code == LV_EVENT_RELEASED)
    {
      if (!app->capturing)
        {
          /* Connection may have dropped mid-capture (PRESSED started capture
           * but ws died before RELEASED arrived).  The PRESSED guard above
           * should have cleaned up, but if we still reach here with stale
           * dialog text, clear it so the UI doesn't show old content. */
          if (!app->dialog_running &&
              (app->stt_text[0] != '\0' || app->tts_text[0] != '\0'))
            {
              pthread_mutex_lock(&s_ui_mutex);
              app->stt_text[0] = '\0';
              s_mcp_stt_text[0] = '\0';
              app->tts_text[0] = '\0';
              if (app->handshake_done && !app->ws_dead)
                {
                  app->state = XZ_STATE_READY;
                }
              pthread_mutex_unlock(&s_ui_mutex);
            }
          return;
        }

      /* Tell the capture thread to stop, then hand off to the dialog-round
       * thread (which joins the capture thread and does the validity check
       * + send). All blocking work happens off the LVGL thread. */
      app->capture_stop = true;
      app->capturing = false;

      app->dialog_running = true;
      pthread_mutex_lock(&s_ui_mutex);
      app->state = XZ_STATE_THINKING;
      pthread_mutex_unlock(&s_ui_mutex);

      pthread_attr_t attr;
      pthread_attr_init(&attr);
      pthread_attr_setstacksize(&attr, 32768);
      int ret = pthread_create(NULL, &attr, dialog_round_thread, app);
      pthread_attr_destroy(&attr);
      if (ret != 0)
        {
          printf("[xiaozhi] dialog round thread create failed: %d\n", ret);
          if (app->capture_tid_valid)
            {
              pthread_join(app->capture_tid, NULL);
              app->capture_tid_valid = false;
            }
          xiaozhi_audio_stop_capture(app->audio);
          app->dialog_running = false;
          pthread_mutex_lock(&s_ui_mutex);
          app->state = XZ_STATE_READY;
          pthread_mutex_unlock(&s_ui_mutex);
        }
    }
}

/* Build the LVGL main screen. */
static void ui_create(struct xiaozhi_app_s *app)
{
  lv_obj_t *scr = get_watch_scr();

  /* 创建AI应用容器 */
  xiaozhi_container = lv_obj_create(scr);
  lv_obj_set_size(xiaozhi_container, LV_CIRCLE_WATCH, LV_CIRCLE_WATCH);
  lv_obj_align(xiaozhi_container, LV_ALIGN_CENTER, 0, 0);
  lv_obj_set_style_bg_color(xiaozhi_container, lv_color_hex(0x1A1A2E), 0);  /* 深色背景 */
  lv_obj_set_style_bg_opa(xiaozhi_container, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(xiaozhi_container, 0, 0);
  lv_obj_set_style_pad_all(xiaozhi_container, 0, 0);
  lv_obj_set_scrollbar_mode(xiaozhi_container, LV_SCROLLBAR_MODE_OFF);
  lv_obj_clear_flag(xiaozhi_container, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_clip_corner(xiaozhi_container, false, 0);  /* 不裁剪子对象 */

  /* 注册手势事件处理，用于右滑返回 */
  lv_obj_add_event_cb(xiaozhi_container, xiaozhi_gesture_event_cb,
                      LV_EVENT_GESTURE, NULL);
  lv_obj_remove_flag(xiaozhi_container, LV_OBJ_FLAG_GESTURE_BUBBLE);

  /* Use Chinese-capable font */
  const lv_font_t *cjk_font = font_manager_get_font(FONT_LARGE);
  s_cjk_font = cjk_font;  /* 保存到全局变量供其他函数使用 */

  /* Speak button: 使用图片按钮，居中偏上显示 */
  app->speak_btn = lv_image_create(xiaozhi_container);
  lv_image_set_src(app->speak_btn, VOICE_BTN_CONNECTING);  /* 默认显示连接中图标 */
  lv_obj_align(app->speak_btn, LV_ALIGN_CENTER, 0, -85);  /* 向上偏移，为下方文案留空间 */
  lv_obj_add_flag(app->speak_btn, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(app->speak_btn, speak_btn_event_cb, LV_EVENT_PRESSED,
                      NULL);
  lv_obj_add_event_cb(app->speak_btn, speak_btn_event_cb, LV_EVENT_RELEASED,
                      NULL);

  /* Status line - 按钮下方状态标签 */

  /* 创建状态胶囊容器（包裹圆点和label） */
  status_pill_create(xiaozhi_container, app->status_label);

  /* status_label 创建在胶囊容器内 */
  app->status_label = lv_label_create(s_status_pill);
  lv_label_set_text(app->status_label, g_state_names[app->state]);
  lv_obj_set_style_text_font(app->status_label, cjk_font, 0);
  lv_obj_set_style_text_color(app->status_label, lv_color_hex(0xFFA502), 0);  /* 橙色 */
  lv_obj_align(app->status_label, LV_ALIGN_LEFT_MID, 5, 0);  /* 右移5px */

  /* 创建状态圆点（在胶囊容器内，status_label左侧） */
  status_dot_create(s_status_pill, app->status_label);

  /* 副标题标签 - 状态标签下方 */
  app->subtitle_label = lv_label_create(xiaozhi_container);
  lv_label_set_text(app->subtitle_label, "正在连接小智服务器");
  lv_obj_set_style_text_font(app->subtitle_label, cjk_font, 0);  /* 使用与状态label相同的字体 */
  lv_obj_set_style_text_color(app->subtitle_label, lv_color_hex(0x9BA1A8), 0);
  lv_obj_align(app->subtitle_label, LV_ALIGN_CENTER, 0, 65);

  /* STT (what you said) label - 紧跟在副标题下方 */
  s_stt_caption = lv_label_create(xiaozhi_container);
  lv_label_set_text(s_stt_caption, "");
  lv_obj_set_style_text_font(s_stt_caption, cjk_font, 0);
  lv_obj_set_style_text_color(s_stt_caption, lv_color_hex(0xCCCCCC), 0);
  lv_obj_align(s_stt_caption, LV_ALIGN_CENTER, 0, 60);

  app->stt_label = lv_label_create(xiaozhi_container);
  lv_label_set_text(app->stt_label, "");
  lv_obj_set_style_text_font(app->stt_label, cjk_font, 0);
  lv_obj_set_style_text_color(app->stt_label, lv_color_hex(0xCCCCCC), 0);
  lv_label_set_long_mode(app->stt_label, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(app->stt_label, 280);
  lv_obj_set_style_text_align(app->stt_label, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_align(app->stt_label, LV_ALIGN_CENTER, 0, 80);

  /* TTS (reply) label - 在STT下方显示xiaozhi的回答 */
  s_tts_caption = lv_label_create(xiaozhi_container);
  lv_label_set_text(s_tts_caption, "");
  lv_obj_set_style_text_font(s_tts_caption, cjk_font, 0);
  lv_obj_set_style_text_color(s_tts_caption, lv_color_hex(0x26DE81), 0);  /* 绿色 */
  lv_obj_align(s_tts_caption, LV_ALIGN_CENTER, 0, 115);

  app->tts_label = lv_label_create(xiaozhi_container);
  lv_label_set_text(app->tts_label, "");
  lv_obj_set_style_text_font(app->tts_label, cjk_font, 0);
  lv_obj_set_style_text_color(app->tts_label, lv_color_hex(0x26DE81), 0);  /* 绿色 */
  lv_label_set_long_mode(app->tts_label, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(app->tts_label, 280);
  lv_obj_set_style_text_align(app->tts_label, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_align(app->tts_label, LV_ALIGN_CENTER, 0, 150);
}

/* Refresh the LVGL screen from the shared app state.
 * NOTE: This function performs LVGL operations and should only be called
 * from LVGL timer callbacks (which run in the LVGL thread context).
 * Do NOT call this from worker threads directly.
 */
static void ui_refresh(struct xiaozhi_app_s *app)
{
  if (app->status_label != NULL)
    {
      lv_label_set_text(app->status_label, g_state_names[app->state]);
    }

  /* 更新副标题文案 */
  if (app->subtitle_label != NULL)
    {
      switch (app->state)
        {
          case XZ_STATE_BOOT:
          case XZ_STATE_OTA:
          case XZ_STATE_CONNECT:
            lv_label_set_text(app->subtitle_label, "正在连接小智服务器");
            break;

          case XZ_STATE_READY:
            lv_label_set_text(app->subtitle_label, "你好 小智");
            break;

          case XZ_STATE_LISTENING:
          case XZ_STATE_THINKING:
          case XZ_STATE_PLAYING:
            lv_label_set_text(app->subtitle_label, "");
            break;

          case XZ_STATE_ERROR:
            lv_label_set_text(app->subtitle_label, "连接出错");
            break;

          default:
            lv_label_set_text(app->subtitle_label, "");
            break;
        }
    }

  /* Only start/stop animations on state transitions, not every refresh. */
  if (s_ui_last_state != app->state)
    {
      /* 先停止所有动画 */
      wave_anim_stop();
      think_anim_stop();
      recognize_anim_stop();
      playing_anim_stop();
      ripple_anim_stop();
      bubble_container_delete();
      recog_anim_delete();

      /* 判断是否为连接中状态 */
      bool cur_connecting = (app->state == XZ_STATE_BOOT ||
                             app->state == XZ_STATE_OTA ||
                             app->state == XZ_STATE_CONNECT ||
                             app->state == XZ_STATE_ERROR);
      bool prev_connecting = (s_ui_last_state == XZ_STATE_BOOT ||
                              s_ui_last_state == XZ_STATE_OTA ||
                              s_ui_last_state == XZ_STATE_CONNECT ||
                              s_ui_last_state == XZ_STATE_ERROR);

      /* 仅在离开连接状态时停止轨道动画，连接中子状态切换时不重启 */
      if (!cur_connecting) {
          connecting_anim_stop();
      }

      if (app->state == XZ_STATE_LISTENING)
        {
          /* Listening状态：显示波形动画和波纹动画 */
          if (xiaozhi_container != NULL)
            {
              wave_anim_start(xiaozhi_container);
              ripple_anim_start(xiaozhi_container);
              /* 将voice按钮、胶囊和副标题移到最上层，避免被波纹遮挡 */
              if (app->speak_btn != NULL) {
                  lv_obj_move_foreground(app->speak_btn);
              }
              if (s_status_pill) {
                  lv_obj_move_foreground(s_status_pill);
              }
              if (app->subtitle_label != NULL) {
                  lv_obj_move_foreground(app->subtitle_label);
              }
            }
          /* 更新状态标签颜色为青色 */
          if (app->status_label != NULL)
            {
              lv_obj_set_style_text_color(app->status_label,
                                          lv_color_hex(0x00CEC9), 0);
            }
          /* 圆点脉冲动画（青色） */
          status_dot_pulse_start(lv_color_hex(0x00CEC9));
          /* 胶囊边框颜色（青色） */
          if (s_status_pill) {
              lv_obj_set_style_border_color(s_status_pill, lv_color_hex(0x00CEC9), 0);
              lv_obj_set_style_bg_color(s_status_pill, lv_color_hex(0x00CEC9), 0);
          }
        }
      else if (app->state == XZ_STATE_THINKING)
        {
          /* Thinking状态：显示圆形进度条动画 + 三个紫色圆点动效 */
          if (xiaozhi_container != NULL)
            {
              recognize_anim_start(xiaozhi_container);
              recog_anim_create(xiaozhi_container);
            }
          /* 更新状态标签颜色为紫色 */
          if (app->status_label != NULL)
            {
              lv_obj_set_style_text_color(app->status_label,
                                          lv_color_hex(0x6C5CE7), 0);
            }
          /* 圆点脉冲动画（紫色） */
          status_dot_pulse_start(lv_color_hex(0x6C5CE7));
          /* 胶囊边框颜色（紫色） */
          if (s_status_pill) {
              lv_obj_set_style_border_color(s_status_pill, lv_color_hex(0x6C5CE7), 0);
              lv_obj_set_style_bg_color(s_status_pill, lv_color_hex(0x6C5CE7), 0);
          }
        }
      else if (app->state == XZ_STATE_PLAYING)
        {
          /* Playing状态：显示识别成功（√图标） */
          if (xiaozhi_container != NULL)
            {
              playing_anim_start(xiaozhi_container);
            }
          /* 更新状态标签颜色为紫色 */
          if (app->status_label != NULL)
            {
              lv_obj_set_style_text_color(app->status_label,
                                          lv_color_hex(0x6C5CE7), 0);
            }
          /* 圆点静态显示（紫色，无动画） */
          status_dot_pulse_stop();
          if (s_status_dot != NULL)
            {
              lv_obj_set_style_bg_color(s_status_dot, lv_color_hex(0x6C5CE7), 0);
            }
          /* 胶囊边框颜色（紫色） */
          if (s_status_pill) {
              lv_obj_set_style_border_color(s_status_pill, lv_color_hex(0x6C5CE7), 0);
              lv_obj_set_style_bg_color(s_status_pill, lv_color_hex(0x6C5CE7), 0);
          }
        }
      else if (app->state == XZ_STATE_READY)
        {
          /* Ready状态：显示快捷命令气泡胶囊 */
          if (xiaozhi_container != NULL)
            {
              bubble_container_create(xiaozhi_container);
            }
          /* 更新状态标签颜色为绿色 */
          if (app->status_label != NULL)
            {
              lv_obj_set_style_text_color(app->status_label,
                                          lv_color_hex(0x26DE81), 0);
            }
          /* 圆点静态显示（绿色，无动画） */
          status_dot_pulse_stop();
          if (s_status_dot != NULL)
            {
              lv_obj_set_style_bg_color(s_status_dot, lv_color_hex(0x26DE81), 0);
            }
          /* 胶囊边框颜色（绿色） */
          if (s_status_pill) {
              lv_obj_set_style_border_color(s_status_pill, lv_color_hex(0x26DE81), 0);
              lv_obj_set_style_bg_color(s_status_pill, lv_color_hex(0x26DE81), 0);
          }
        }
      else
        {
          /* 其他状态（连接中等）：使用橙色 */
          if (app->status_label != NULL)
            {
              lv_obj_set_style_text_color(app->status_label,
                                          lv_color_hex(0xFFA502), 0);
            }
          /* 圆点脉冲动画（橙色） */
          status_dot_pulse_start(lv_color_hex(0xFFA502));
          /* 胶囊边框颜色（橙色） */
          if (s_status_pill) {
              lv_obj_set_style_border_color(s_status_pill, lv_color_hex(0xFFA502), 0);
              lv_obj_set_style_bg_color(s_status_pill, lv_color_hex(0xFFA502), 0);
          }
          /* 连接中状态：仅在首次进入连接状态时启动轨道动画 */
          if (xiaozhi_container != NULL && cur_connecting && !prev_connecting)
            {
              connecting_anim_start(xiaozhi_container);
            }
        }

      /* 根据状态调整胶囊和副标题位置 */
      if (app->state == XZ_STATE_BOOT || app->state == XZ_STATE_OTA ||
          app->state == XZ_STATE_CONNECT || app->state == XZ_STATE_ERROR)
        {
          /* 连接中状态：胶囊和副标题下移，并移到最上层 */
          if (s_status_pill) {
              lv_obj_align(s_status_pill, LV_ALIGN_CENTER, 0, 95);
              lv_obj_move_foreground(s_status_pill);
          }
          if (app->subtitle_label != NULL) {
              lv_obj_align(app->subtitle_label, LV_ALIGN_CENTER, 0, 140);
              lv_obj_move_foreground(app->subtitle_label);
          }
          if (app->speak_btn != NULL) {
              lv_obj_move_foreground(app->speak_btn);
          }
        }
      else
        {
          /* 非连接状态：恢复默认位置 */
          if (s_status_pill) {
              lv_obj_align(s_status_pill, LV_ALIGN_CENTER, 0, 20);
          }
          if (app->subtitle_label != NULL) {
              lv_obj_align(app->subtitle_label, LV_ALIGN_CENTER, 0, 65);
          }
        }

      s_ui_last_state = app->state;
    }

  /* 更新STT文本显示 */
  if (app->stt_label != NULL)
    {
      bool has_stt = app->stt_text[0] != '\0';
      lv_label_set_text(app->stt_label, has_stt ? app->stt_text : "");
    }

  /* 更新TTS文本显示（xiaozhi的回答） */
  if (app->tts_label != NULL)
    {
      bool has_tts = app->tts_text[0] != '\0';
      lv_label_set_text(app->tts_label, has_tts ? app->tts_text : "");
    }
  if (s_tts_caption != NULL)
    {
      bool has_tts = app->tts_text[0] != '\0';
      lv_label_set_text(s_tts_caption, has_tts ? "小智回复" : "");
    }

  /* Speak button: 根据状态切换图片和可见性 */
  if (app->speak_btn != NULL)
    {
      bool live = app->handshake_done && !app->ws_dead && !app->dialog_running;

      /* 识别中和识别成功状态：隐藏voice图标，显示动画 */
      if (app->state == XZ_STATE_THINKING || app->state == XZ_STATE_PLAYING)
        {
          lv_obj_add_flag(app->speak_btn, LV_OBJ_FLAG_HIDDEN);
        }
      else
        {
          /* 其他状态：显示voice图标 */
          lv_obj_clear_flag(app->speak_btn, LV_OBJ_FLAG_HIDDEN);

          if (live || app->state == XZ_STATE_READY)
            {
              /* 根据状态选择对应的图片 */
              if (app->state == XZ_STATE_LISTENING)
                {
                  lv_image_set_src(app->speak_btn, VOICE_BTN_LISTENING);
                }
              else
                {
                  lv_image_set_src(app->speak_btn, VOICE_BTN_READY);
                }
            }
          else
            {
              /* 连接中或错误状态 */
              lv_image_set_src(app->speak_btn, VOICE_BTN_CONNECTING);
            }
        }
    }
}

/* Timer callback to refresh UI periodically.
 * NOTE: This runs in the LVGL timer thread context.
 * Do NOT hold s_ui_mutex here - ui_refresh performs LVGL operations
 * that should not be blocked by worker threads network I/O.
 */
static void ui_refresh_timer_cb(lv_timer_t *timer)
{
  LV_UNUSED(timer);

  ui_refresh(&g_app);
}

/****************************************************************************
 * WiFi连接线程 - 后台执行避免阻塞UI
 ****************************************************************************/

static void *wifi_connect_thread(void *arg)
{
  (void)arg;

  system("ifup wlan0");
  usleep(100000);                   /* 等待100ms */
  system("wapi mode wlan0 2");      /* 设置为station模式 */
  usleep(50000);
  system("wapi psk wlan0 12345678 3");  /* 设置密码 */
  usleep(50000);
  system("wapi essid wlan0 ZTE-kSQCKP-5G 1");  /* 设置SSID */
  usleep(100000);
  system("ifup wlan0");             /* 再次启动确保连接 */
  usleep(300000);                   /* 等待300ms让WiFi连接 */
  system("renew wlan0");            /* DHCP获取IP地址 */

  /* 等待真正的IP地址就绪，而不是用固定延时。
   * DHCP可能需要数秒才能完成，固定延时会导致DNS失败和重连循环。 */
  struct in_addr ip_addr;
  int ip_wait;
  for (ip_wait = 0; ip_wait < 30 && g_app.app_running; ip_wait++)
    {
      if (netlib_get_ipv4addr("wlan0", &ip_addr) == 0 &&
          ip_addr.s_addr != 0)
        {
          break;
        }
      usleep(500000);               /* 每500ms检查一次 */
    }

  s_wifi_ready = true;
  return NULL;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

void xiaozhi_voice_start(void)
{
  LV_LOG_USER("==================xiaozhi_voice_start==================");

  /* 首次进入时快照入口页面（nav_enter_app_from 已在调用方设置好 g_prev_page），
   * MCP 工具在运行期间会覆盖 g_prev_page 为 PAGE_VOICE，
   * 再次返回 xiaozhi_voice 时不应覆盖快照。 */
  if (xiaozhi_container == NULL)
    {
      s_xiaozhi_entry_page = nav_get_prev_page();
      LV_LOG_USER("xiaozhi_voice: entry_page=%d", s_xiaozhi_entry_page);
    }

  /* 如果已经初始化过，直接显示 */
  if (xiaozhi_container != NULL)
    {
      /* 显示容器 */
      lv_obj_clear_flag(xiaozhi_container, LV_OBJ_FLAG_HIDDEN);
      return;
    }

  /* 初始化应用状态 */
  memset(&g_app, 0, sizeof(g_app));
  g_app.state = XZ_STATE_BOOT;
  g_app.app_running = true;
  g_app.ws.fd = -1;  /* 初始化为无效值，避免关闭 stdin */

  /* 初始化字体管理器（加载中文字体） */
  font_manager_init();

  /* 创建UI */
  ui_create(&g_app);

  /* 自动连接WiFi - 后台线程执行，避免阻塞UI */
  pthread_attr_t wifi_attr;
  pthread_attr_init(&wifi_attr);
  pthread_attr_setstacksize(&wifi_attr, 4096);
  pthread_create(NULL, &wifi_attr, wifi_connect_thread, NULL);
  pthread_attr_destroy(&wifi_attr);

  /* 启动UI刷新定时器，每100ms刷新一次 */
  if (ui_refresh_timer == NULL)
    {
      ui_refresh_timer = lv_timer_create(ui_refresh_timer_cb, 100, NULL);
    }

  /* Launch the connect thread: runs OTA -> wss -> hello -> MCP in the
   * background. The status label updates at each node; when done, the
   * ui_refresh timer will enable the Speak button automatically. */
  pthread_attr_t attr;
  pthread_attr_init(&attr);
  pthread_attr_setstacksize(&attr, 65536);
  int ret = pthread_create(&g_app.connect_tid, &attr, connect_thread, &g_app);
  pthread_attr_destroy(&attr);
  if (ret != 0)
    {
      printf("[xiaozhi] connect thread create failed: %d\n", ret);
      g_app.state = XZ_STATE_ERROR;
      g_app.handshake_failed = true;
      g_app.connect_tid_valid = false;
    }
  else
    {
      g_app.connect_tid_valid = true;
    }
}

void xiaozhi_voice_delete(void)
{
  /* CRITICAL: Stop LVGL rendering IMMEDIATELY to prevent TLSF heap corruption.
   * We set this BEFORE stopping timers and hiding objects, so no new render
   * cycle can start while we're cleaning up. */
  extern volatile bool g_lvgl_stop_rendering;
  g_lvgl_stop_rendering = true;

  /* CRITICAL: Stop all animation timers IMMEDIATELY.
   * Timer callbacks may still be executing in LVGL's timer handler and
   * accessing LVGL objects that will be hidden/deleted. If we don't stop
   * them now, they may access freed memory during the current or next
   * render cycle, causing TLSF heap corruption. */
  if (s_timer != NULL) { lv_timer_del(s_timer); s_timer = NULL; }
  if (s_think_timer != NULL) { lv_timer_del(s_think_timer); s_think_timer = NULL; }
  if (s_dot_pulse_timer != NULL) { lv_timer_del(s_dot_pulse_timer); s_dot_pulse_timer = NULL; }
  if (s_recog_anim_timer != NULL) { lv_timer_del(s_recog_anim_timer); s_recog_anim_timer = NULL; }
  if (s_ripple_timer != NULL) { lv_timer_del(s_ripple_timer); s_ripple_timer = NULL; }
  if (s_circle_timer != NULL) { lv_timer_del(s_circle_timer); s_circle_timer = NULL; }
  if (s_orbit_timer != NULL) { lv_timer_del(s_orbit_timer); s_orbit_timer = NULL; }
  if (ui_refresh_timer != NULL) { lv_timer_delete(ui_refresh_timer); ui_refresh_timer = NULL; }

  /* Signal all threads to stop. */
  g_app.app_running = false;
  g_app.ws_connected = false;
  g_app.capturing = false;
  g_app.capture_stop = true;

  /* Close the socket fd to unblock recv_thread from mbedtls_ssl_read.
   * This causes recv_thread to exit immediately. We do NOT wait for any
   * threads here — this function runs inside lv_timer_handler() via the
   * gesture callback, and blocking here freezes the entire LVGL rendering
   * loop, making the UI unresponsive and exit slow. Threads will exit on
   * their own: recv_thread exits when socket read fails, connect_thread
   * exits when it checks app_running, capture_thread exits when it checks
   * capture_stop. They don't touch LVGL objects. */
  if (g_app.ws.fd >= 0)
    {
      close(g_app.ws.fd);
      g_app.ws.fd = -1;
    }

  /* Stop capture if active, so capture_thread's blocking read unblocks. */
  if (g_app.audio != NULL)
    {
      xiaozhi_audio_stop_capture(g_app.audio);
    }

  /* ---- LVGL cleanup (SYNCHRONOUS, NO DEFERRED) ----
   *
   * CRITICAL: Do NOT call lv_obj_del() anywhere. It corrupts the TLSF
   * heap on BES1700, causing必现 MMFAR=0x15c crash on next render cycle.
   *
   * We hide the container and NULL out all pointers here SYNCHRONOUSLY.
   * The container is hidden with LV_OBJ_FLAG_HIDDEN, so LVGL will skip
   * it during rendering. NULLing pointers prevents any code from
   * accessing freed objects.
   *
   * NO deferred timer: the previous approach deferred cleanup to the next
   * lv_timer_handler() tick, but g_lvgl_stop_rendering was reset BEFORE
   * the deferred callback ran, causing LVGL to render with corrupted
   * pointers → MMFAR=0x15c crash.
   */

  /* 1. Hide the container — LVGL will skip drawing it. */
  if (xiaozhi_container != NULL)
    {
      lv_obj_add_flag(xiaozhi_container, LV_OBJ_FLAG_HIDDEN);
    }

  /* 2. NULL out all LVGL pointers immediately.
   * The container is hidden, so LVGL won't render it.
   * The pointers are NULL, so no code will access freed objects. */
  xiaozhi_container = NULL;
  s_bubble_alarm = NULL;
  s_bubble_timer = NULL;
  s_bubble_container = NULL;
  s_recog_dots_container = NULL;
  s_recog_dots[0] = NULL;
  s_recog_dots[1] = NULL;
  s_recog_dots[2] = NULL;
  s_recog_label = NULL;
  s_status_dot = NULL;
  s_status_pill = NULL;
  s_bar_container = NULL;
  s_dot_container = NULL;
  s_circle_container = NULL;
  s_orbit_container = NULL;
  s_ripple_container = NULL;
  s_success_container = NULL;
  s_listen_label = NULL;
  s_stt_caption = NULL;
  s_tts_caption = NULL;

  /* 重置状态变化检测，确保再次进入时能触发连接动画 */
  s_ui_last_state = -1;
}
