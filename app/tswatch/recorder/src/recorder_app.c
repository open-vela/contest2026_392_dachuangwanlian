/****************************************************************************
 * apps/examples/tswatch/recorder/recorder_app.c
 *
 * 录音 App UI（455×455 圆形屏）：
 *   单主按钮（按状态切换：开始 / 停止 / 再来一次）+ 顶部连接状态胶囊
 *   + 录音中真实振幅律动（主按钮背后的振幅呼吸层 + 3 圈外围呼吸环）+ mm:ss 计时
 *   + 已保存完成态（绿勾 + 时长 + 去向说明）
 *   + 底部「离线 N」入口（点击打开离线列表覆盖层）。右滑退出。
 *
 * 设计要点：
 *   - 所有展示字段均来自固件现有 API，无虚构数据：
 *       连接状态  bt_transport_is_connected()
 *       录音状态  spp_audio_stream_is_active()
 *       麦克风能量 spp_audio_stream_get_amplitude()
 *       离线文件数 offline_recordings_count()
 *     计时器由本文件的 lv_timer 自行累加（lv_tick_get 差值），不新增固件接口。
 *   - 录音启停 / SPP 推流 / 离线落盘回传逻辑全部保持原样，本文件只负责画界面。
 *   - 所有元素落在直径 ~350 的圆屏安全区内（原右上角离线入口 (84,-140) 已越界）。
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

#include <lvgl/lvgl.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>     /* int32_t：lv_obj_set_style_shadow_width 的实参类型 */
#include <stdbool.h>

#include "../../utils/include/circle_screen.h"
#include "../../utils/include/font_manager.h"
#include "../../main_page/include/watch_start.h"
#include "../../spp_audio/include/spp_audio_stream.h"
#include "../../offline_recording/include/offline_recording_list.h"
#include "bt_transport.h"

/****************************************************************************
 * 布局常量（455 圆屏：中心为 (0,0)，安全区半径 ~227）
 *
 * 三态共用同一套坐标，靠「显示/隐藏 + 换色」区分，杜绝元素错位：
 *     标题 28 → 连接胶囊 64(仅待机/已保存) → 计时 60(仅录音中)
 *     → 主按钮 CENTER 0 → 提示 116 → 离线入口(140×40, 底-46)
 *
 * 录音中主按钮自身保持静态（尺寸/位置/transform 全程不动，命中区恒定，点击极稳）；
 * 其背后有一层纯装饰的「振幅呼吸层」随麦克风能量脉动，外围再叠 3 圈呼吸环；点击按钮=停止。
 * 开始按钮与它同一直径（MAIN_BTN_D），同样静态无脉动。
 ****************************************************************************/
#define RING_COUNT      3            /* 呼吸环数量 */
#define RING_GAP        8            /* 环与按钮的基准间距 */
#define RING_PULSE      0.16f        /* 光环随振幅的额外放大比例（×(1+amp*RING_PULSE)） */
#define BTN_PULSE       0.10f        /* 主按钮呼吸缩放幅度 */

#define ANIM_MS         60           /* 律动刷新间隔 */
#define TICK_MS         200          /* 计时/状态轮询间隔 */
#define OFFLINE_POLL_MS 1000         /* 离线入口可见性轮询 */

#define MAIN_BTN_D      132          /* 主按钮直径（开始/停止/振幅背景，三态同尺寸） */
#define OFFLINE_BTN_W   140
#define OFFLINE_BTN_H   40

#define Y_TITLE         28           /* 标题 Y（三态一致） */
#define Y_TIMER_REC     60           /* 计时 Y（仅录音中显示） */
#define Y_MAIN          0            /* 主按钮中心 Y（= 圆心，三态一致） */
#define Y_HINT          116          /* 提示语 Y（中心+116，位于按钮下方、离线键上方） */
#define Y_CONN_PILL     64           /* 连接胶囊 Y（标题正下方，录音中隐藏） */
#define Y_OFFLINE_BOT   (-46)        /* 离线入口距底 */

#define SAVED_HOLD_MS   2000         /* 「已保存」态停留时长 */

/****************************************************************************
 * 颜色（与 offline_recording_list.c 保持一致的深色体系）
 ****************************************************************************/
#define BG_BASE         lv_color_hex(0x06070D)
#define CARD_BG         lv_color_hex(0x131520)
#define STROKE_SOFT     lv_color_hex(0x1F2133)
#define TEXT_PRIMARY    lv_color_hex(0xF8F9FF)
#define TEXT_SECONDARY  lv_color_hex(0x9AA4B2)
#define TEXT_DIM        lv_color_hex(0x5C6675)
#define BAR_COLOR       lv_color_hex(0x00CEC9)   /* 品牌青：律动/主按钮 */
#define START_COLOR     lv_color_hex(0x22C55E)   /* 已连接·主按钮 */
#define STOP_COLOR      lv_color_hex(0xEF4444)   /* 录音中·停止 */
#define OFFLINE_COLOR   lv_color_hex(0xF59E0B)   /* 离线入口 */

/****************************************************************************
 * 页面状态
 ****************************************************************************/
typedef enum {
    UI_IDLE = 0,      /* 待机（可开始） */
    UI_RECORDING,     /* 录音中 */
    UI_SAVED          /* 已保存（短暂停留后回IDLE） */
} ui_state_t;

/****************************************************************************
 * 静态对象 / 状态
 ****************************************************************************/
static lv_obj_t *s_container      = NULL;
static lv_obj_t *s_title          = NULL;

/* 连接状态胶囊 */
static lv_obj_t *s_conn_pill      = NULL;
static lv_obj_t *s_conn_dot       = NULL;
static lv_obj_t *s_conn_label     = NULL;

/* 录音中：计时 + 呼吸环（律动柱已移除，改由主按钮本身做振幅背景） */
static lv_obj_t *s_timer_label    = NULL;
static lv_obj_t *s_rings[RING_COUNT] = {NULL};

/* 振幅背景层：在按钮之下、纯装饰、不可点击，录音中随音量呼吸（transform）。
 * 把"振幅律动"从按钮本身剥离，使主按钮永远静态、命中区恒定，点击 100% 可靠。 */
static lv_obj_t *s_amp_bg         = NULL;

/* 主按钮（三态同一直径）：待机=开始、录音中=振幅背景兼停止、已保存=再来一次。
 * 整个生命周期内尺寸/位置/transform 一律不动，仅换色与显隐。 */
static lv_obj_t *s_btn_main       = NULL;
static lv_obj_t *s_btn_main_icon  = NULL;

/* 状态提示语 */
static lv_obj_t *s_hint_label     = NULL;

/* 离线入口 */
static lv_obj_t *s_offline_entry  = NULL;
static lv_obj_t *s_offline_label  = NULL;

/* 计时器 */
static lv_timer_t *s_anim_timer    = NULL;
static lv_timer_t *s_tick_timer    = NULL;
static lv_timer_t *s_offline_timer = NULL;

/* 运行态 */
static ui_state_t s_state          = UI_IDLE;
static uint32_t   s_record_start_ms = 0;   /* 录音起始 tick */
static uint32_t   s_elapsed_ms      = 0;   /* 已录音时长 */
static bool       s_was_connected   = false; /* stop 时刻的连接快照：决定「已发送/已保存到手表」 */
static bool       s_last_conn       = false; /* 上一次 tick 观察到的连接态，用于变化检测 */
static uint32_t   s_saved_since_ms  = 0;    /* 「已保存」态起点 tick（0=未在该态） */
static int        s_last_offline_n  = -1;

/****************************************************************************
 * 小工具
 ****************************************************************************/
static void obj_reset(lv_obj_t **p)
{
    if (*p)
      {
        lv_obj_del(*p);
        *p = NULL;
      }
}

/* 创建一个纯样式圆（无子对象），用于呼吸环 / 状态点 */
static lv_obj_t *make_circle(lv_obj_t *parent, int size, lv_color_t color,
                             int radius, lv_opa_t opa)
{
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_set_size(o, size, size);
    lv_obj_set_style_radius(o, radius, 0);
    lv_obj_set_style_bg_color(o, color, 0);
    lv_obj_set_style_bg_opa(o, opa, 0);
    lv_obj_clear_flag(o, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    return o;
}

/****************************************************************************
 * 振幅律动
 *
 * 设计：不再画律动柱（原「条幅」与呼吸环视觉重复，已移除）。
 * 录音时**主按钮本身就是振幅背景**——随麦克风能量做「呼吸缩放 + 亮度起伏」，
 * 外围 3 圈光环同步脉动；点击该大圆即停止录音。
 ****************************************************************************/
static void rings_set_visible(bool visible)
{
    for (int i = 0; i < RING_COUNT; i++)
      {
        if (!s_rings[i])
            continue;
        if (visible) lv_obj_clear_flag(s_rings[i], LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(s_rings[i], LV_OBJ_FLAG_HIDDEN);
      }
}

/** 主按钮尺寸/位置复位到基准（仅在状态切换时调用，绝不参与逐帧动画）。
 *  同时复位背后的振幅背景层呼吸缩放。 */
static void main_btn_reset_size(void)
{
    if (!s_btn_main)
        return;
    lv_obj_set_size(s_btn_main, MAIN_BTN_D, MAIN_BTN_D);
    lv_obj_set_style_radius(s_btn_main, LV_RADIUS_CIRCLE, 0);
    lv_obj_align(s_btn_main, LV_ALIGN_CENTER, 0, Y_MAIN);   /* 固定锚点，命中区恒定 */

    if (s_amp_bg) {
        lv_obj_set_style_transform_scale(s_amp_bg, LV_SCALE_NONE, 0);
        lv_obj_set_style_transform_pivot_x(s_amp_bg, MAIN_BTN_D / 2, 0);
        lv_obj_set_style_transform_pivot_y(s_amp_bg, MAIN_BTN_D / 2, 0);
        lv_obj_set_style_bg_opa(s_amp_bg, LV_OPA_30, 0);
        lv_obj_set_style_shadow_width(s_amp_bg, 0, 0);
    }
}

static void anim_tick_cb(lv_timer_t *t)
{
    (void)t;
    if (s_state != UI_RECORDING || !s_btn_main)
        return;

    /* 固件 spp_update_amplitude() 用 mean-abs / AMP_FULLSCALE(3000) 算振幅，
     * 而正常说话 PCM 的平均绝对值只有几百 → 原始值多在 0.05~0.30之间，
     * 直接拿来驱动动画幅度极小、看起来几乎不动。
     * 这里做两级补偿：① 固定增益放大到可用区间；② 慢速自适应基线，
     * 使「持续说话」也能稳定跑满，「安静」时才回到低幅度。 */
    float raw = spp_audio_stream_get_amplitude();          /* 0..1 固件原始值 */
    static float amp_gain = 1.0f;   /* 自适应增益 */
    static float amp_smooth = 0.0f;

    /* 缓慢回落的自适应增益：以目标 0.55 为准，上限 12倍 */
    if (raw > 0.02f) {
        float want = 0.55f / raw;
        if (want > 12.0f) want = 12.0f;
        amp_gain += (want - amp_gain) * 0.05f;
    } else {
        amp_gain += (1.0f - amp_gain) * 0.02f;             /* 静音时慢慢回落 */
    }

    float amp = raw * amp_gain;
    if (amp > 1.0f) amp = 1.0f;
    /* 起振门限：滤掉静音时的微小抖动，避免光环一直「呼吸」 */
    if (amp < 0.06f) amp = 0.0f;

    /* 平滑：快起慢落，视觉更跟手 */
    float k = (amp > amp_smooth) ? 0.45f : 0.18f;
    amp_smooth += (amp - amp_smooth) * k;

    /* 振幅背景层（在按钮之下、纯装饰、不可点击）随振幅呼吸：用 transform（纯视觉、不改命中区）。
     * 主按钮本身 NEVER 参与动画（尺寸/位置/transform 全程静态），
     * 因此命中区域恒定，点击 100% 可靠，不会因「按下时几何在变」而丢 CLICKED。 */
    if (s_amp_bg) {
        int scale = (int)(256 * (1.0f + amp_smooth * BTN_PULSE));
        lv_obj_set_style_transform_scale(s_amp_bg, scale, 0);
        lv_obj_set_style_transform_pivot_x(s_amp_bg, MAIN_BTN_D / 2, 0);
        lv_obj_set_style_transform_pivot_y(s_amp_bg, MAIN_BTN_D / 2, 0);
        lv_obj_set_style_bg_opa(s_amp_bg,
            (lv_opa_t)(LV_OPA_30 + (lv_opa_t)(amp_smooth * 80)), 0);
        /* 光晕扩散：静默时收紧，说话时明显外扩（SDK 用 int32_t，无 lv32_t） */
        lv_obj_set_style_shadow_width(s_amp_bg,
            (int32_t)(8 + amp_smooth * 34), 0);
    }

    /* 外围 RING_COUNT 圈光环：直径随振幅向外扩散并淡出
     * 尺寸核算：最外环 = (140 + 8*2*3) * (1 + 0.45*0.10) ≈ 201 < 227.5，不溢出 */
    for (int i = 0; i < RING_COUNT; i++)
      {
        if (!s_rings[i])
            continue;
        /* 3 圈错相：i*0.28 + 少量相位差 → 呈「涟漪式」逐圈扩散而非整体同步缩放 */
        float wave = amp_smooth * 0.55f + i * 0.28f;
        if (wave > 1.0f) wave = 1.0f;
        float scale = 1.0f + wave * RING_PULSE;
        int base = MAIN_BTN_D + RING_GAP * 2 * (i + 1);
        int rd = (int)(base * scale);
        lv_obj_set_size(s_rings[i], rd, rd);
        lv_obj_set_style_radius(s_rings[i], LV_RADIUS_CIRCLE, 0);
        /* 透明度：随扩散由亮渐淡，形成「波纹散去」的观感 */
        float falloff = 1.0f - (float)i * 0.22f;
        lv_obj_set_style_bg_opa(s_rings[i],
            (lv_opa_t)((LV_OPA_20 + amp_smooth * 110) * falloff), 0);
        lv_obj_align(s_rings[i], LV_ALIGN_CENTER, 0, Y_MAIN);
      }
}

/****************************************************************************
 * 计时格式化：ms → "MM:SS"（超过 99 分钟归零显示）
 ****************************************************************************/
static void format_duration(char *buf, int sz, uint32_t ms)
{
    uint32_t total_sec = ms / 1000;
    uint32_t mm = total_sec / 60;
    uint32_t ss = total_sec % 60;
    if (mm > 99) mm = 99;
    snprintf(buf, sz, "%02lu:%02lu", (unsigned long)mm, (unsigned long)ss);
}

/****************************************************************************
 * UI 状态切换
 ****************************************************************************/
static void ui_set_hint(const char *txt)
{
    if (s_hint_label)
        lv_label_set_text(s_hint_label, txt);
}

static void ui_set_timer_text(uint32_t ms)
{
    if (!s_timer_label)
        return;
    char buf[16];
    format_duration(buf, sizeof(buf), ms);
    lv_label_set_text(s_timer_label, buf);
}

static void ui_update_conn_pill(void)
{
    if (!s_conn_pill)
        return;
    bool conn = bt_transport_is_connected();
    if (conn)
      {
        lv_obj_set_style_bg_color(s_conn_pill, lv_color_hex(0x132A1C), 0);
        lv_obj_set_style_border_color(s_conn_pill, lv_color_hex(0x22C55E), 0);
        lv_obj_set_style_bg_color(s_conn_dot, START_COLOR, 0);
        lv_obj_set_style_text_color(s_conn_label, START_COLOR, 0);
        lv_label_set_text(s_conn_label, "蓝牙已连接");
      }
    else
      {
        lv_obj_set_style_bg_color(s_conn_pill, CARD_BG, 0);
        lv_obj_set_style_border_color(s_conn_pill, STROKE_SOFT, 0);
        lv_obj_set_style_bg_color(s_conn_dot, TEXT_DIM, 0);
        lv_obj_set_style_text_color(s_conn_label, TEXT_SECONDARY, 0);
        lv_label_set_text(s_conn_label, "蓝牙未连接");
      }
}

/** 进入录音态：主按钮变成「振幅背景 + 停止键」（同一直径，点击即停） */
static void ui_enter_recording(void)
{
    s_state = UI_RECORDING;
    s_record_start_ms = lv_tick_get();
    s_elapsed_ms = 0;

    lv_label_set_text(s_title, "录音中");
    lv_obj_set_style_text_color(s_title, BAR_COLOR, 0);
    lv_obj_align(s_title, LV_ALIGN_TOP_MID, 0, Y_TITLE);

    /* 录音中隐藏连接胶囊；提示语改为说明去向（实时转写 / 存到手表），未连时尤其需要 */
    if (s_conn_pill)
        lv_obj_add_flag(s_conn_pill, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(s_hint_label, LV_OBJ_FLAG_HIDDEN);
    lv_obj_align(s_hint_label, LV_ALIGN_CENTER, 0, Y_HINT);
    ui_set_hint(s_was_connected ? "点击圆点停止 - 实时转写中"
                                : "点击圆点停止 - 录音存到手表");

    /* 计时器显示 */
    lv_obj_clear_flag(s_timer_label, LV_OBJ_FLAG_HIDDEN);
    lv_obj_align(s_timer_label, LV_ALIGN_TOP_MID, 0, Y_TIMER_REC);
    ui_set_timer_text(0);

    /* 主按钮 → 红色大圆：既是振幅背景（anim_tick_cb 里随音量呼吸），也是停止键 */
    main_btn_reset_size();
    lv_obj_set_style_bg_color(s_btn_main, STOP_COLOR, 0);
    lv_obj_set_style_shadow_color(s_btn_main, STOP_COLOR, 0);
    lv_obj_set_style_border_width(s_btn_main, 0, 0);
    lv_obj_set_style_bg_opa(s_btn_main, LV_OPA_80, 0);
    lv_obj_set_style_shadow_width(s_btn_main, 12, 0);

    /* 振幅背景层：录音中随音量呼吸（纯装饰，在按钮之下、不可点击） */
    if (s_amp_bg) {
        lv_obj_set_style_bg_color(s_amp_bg, STOP_COLOR, 0);
        lv_obj_set_style_shadow_color(s_amp_bg, STOP_COLOR, 0);
        lv_obj_clear_flag(s_amp_bg, LV_OBJ_FLAG_HIDDEN);
    }

    /* 中心图标：白色圆角方块 = 停止符号（静态，作为按钮子对象，不可点击，点击落到按钮） */
    lv_obj_set_style_bg_color(s_btn_main_icon, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(s_btn_main_icon, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_btn_main_icon, 0, 0);
    lv_obj_set_style_radius(s_btn_main_icon, 6, 0);
    lv_obj_set_style_pad_all(s_btn_main_icon, 0, 0);
    lv_obj_set_size(s_btn_main_icon, 34, 34);
    lv_obj_align(s_btn_main_icon, LV_ALIGN_CENTER, 0, 0);

    /* 录音中隐藏离线入口，避免误触打断 */
    if (s_offline_entry)
        lv_obj_add_flag(s_offline_entry, LV_OBJ_FLAG_HIDDEN);

    /* 光环落在主按钮之下（z 序：先创建的在下），点击由主按钮接收 */
    rings_set_visible(true);

    if (!s_anim_timer)
        s_anim_timer = lv_timer_create(anim_tick_cb, ANIM_MS, NULL);
}

/** 回到「待机」形态 */
static void ui_back_to_idle(void)
{
    s_state = UI_IDLE;

    lv_label_set_text(s_title, "录音");
    lv_obj_set_style_text_color(s_title, TEXT_PRIMARY, 0);
    lv_obj_align(s_title, LV_ALIGN_TOP_MID, 0, Y_TITLE);

    lv_obj_add_flag(s_timer_label, LV_OBJ_FLAG_HIDDEN);

    /* 连接胶囊只在待机/已保存态显示 */
    if (s_conn_pill)
        lv_obj_clear_flag(s_conn_pill, LV_OBJ_FLAG_HIDDEN);

    /* 主按钮复位到基准直径（录音态的呼吸缩放要还原），与开始态同尺寸 */
    main_btn_reset_size();

    /* 提示语恢复显示 */
    lv_obj_clear_flag(s_hint_label, LV_OBJ_FLAG_HIDDEN);
    lv_obj_align(s_hint_label, LV_ALIGN_CENTER, 0, Y_HINT);

    /* 主按钮形态：任何时候都可用（录音不再依赖蓝牙）。
     * 已连接 = 绿 + 白色圆形播放图标；未连接 = 深色 + 白描边 + 同款图标，
     * 仅用配色与提示语区分「实时发送 / 存到手表」，不再灰化禁用。
     * 注意：lv_obj_set_style_* 是叠加生效，border_width/opa 必须每分支显式设置。 */
    bool conn = bt_transport_is_connected();
    lv_obj_set_style_border_width(s_btn_main, 0, 0);
    lv_obj_set_style_bg_opa(s_btn_main, LV_OPA_COVER, 0);
    lv_obj_set_style_shadow_width(s_btn_main, 14, 0);
    lv_obj_set_style_bg_color(s_btn_main_icon, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(s_btn_main_icon, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_btn_main_icon, 0, 0);
    lv_obj_set_style_radius(s_btn_main_icon, LV_RADIUS_CIRCLE, 0);  /* 圆形 = 播放三角底 */
    lv_obj_set_size(s_btn_main_icon, 40, 40);
    lv_obj_align(s_btn_main_icon, LV_ALIGN_CENTER, 0, 0);

    if (conn)
      {
        lv_obj_set_style_bg_color(s_btn_main, START_COLOR, 0);
        lv_obj_set_style_shadow_color(s_btn_main, START_COLOR, 0);
        lv_obj_set_style_border_color(s_btn_main, START_COLOR, 0);
        lv_obj_set_style_border_width(s_btn_main, 0, 0);
        ui_set_hint("点击开始录音 - 实时转写");
      }
    else
      {
        /* 未连：仍可录，音频落 /emmc，连上手机后自动回传 */
        lv_obj_set_style_bg_color(s_btn_main, lv_color_hex(0x1B2030), 0);
        lv_obj_set_style_shadow_color(s_btn_main, START_COLOR, 0);
        lv_obj_set_style_border_color(s_btn_main, START_COLOR, 0);
        lv_obj_set_style_border_width(s_btn_main, 2, 0);
        ui_set_hint("点击开始录音 - 存到手表");
      }

    rings_set_visible(false);
    if (s_amp_bg)
        lv_obj_add_flag(s_amp_bg, LV_OBJ_FLAG_HIDDEN);   /* 振幅背景层仅在录音中显示 */

    /* 离线入口可见性以真实文件数为准（无条件 clear_flag 会让 0 个文件时也冒出入口） */
    if (s_offline_entry)
      {
        lv_obj_align(s_offline_entry, LV_ALIGN_BOTTOM_MID, 0, Y_OFFLINE_BOT);
        if (offline_recordings_count() > 0)
            lv_obj_clear_flag(s_offline_entry, LV_OBJ_FLAG_HIDDEN);
        else
            lv_obj_add_flag(s_offline_entry, LV_OBJ_FLAG_HIDDEN);
      }
}

/** 「已保存」短暂反馈态 */
static void ui_show_saved(void)
{
    s_state = UI_SAVED;
    s_saved_since_ms = lv_tick_get() ? lv_tick_get() : 1;   /* 避免恰为 0 时被当作「未在该态」 */

    lv_label_set_text(s_title, "录音");
    lv_obj_set_style_text_color(s_title, TEXT_PRIMARY, 0);
    lv_obj_align(s_title, LV_ALIGN_TOP_MID, 0, Y_TITLE);
    lv_obj_add_flag(s_timer_label, LV_OBJ_FLAG_HIDDEN);

    /* 主按钮复位（同一直径），改为「已保存」的成功态配色 */
    main_btn_reset_size();
    if (s_conn_pill)
        lv_obj_clear_flag(s_conn_pill, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(s_hint_label, LV_OBJ_FLAG_HIDDEN);
    lv_obj_align(s_hint_label, LV_ALIGN_CENTER, 0, Y_HINT);

    lv_obj_set_style_bg_color(s_btn_main, lv_color_hex(0x132A1C), 0);
    lv_obj_set_style_shadow_color(s_btn_main, START_COLOR, 0);
    lv_obj_set_style_shadow_width(s_btn_main, 14, 0);
    lv_obj_set_style_bg_opa(s_btn_main, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_btn_main, 2, 0);
    lv_obj_set_style_border_color(s_btn_main, START_COLOR, 0);
    /* 中心图标：绿实心圆点 = 完成 */
    lv_obj_set_style_bg_color(s_btn_main_icon, START_COLOR, 0);
    lv_obj_set_style_bg_opa(s_btn_main_icon, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_btn_main_icon, 0, 0);
    lv_obj_set_style_radius(s_btn_main_icon, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_size(s_btn_main_icon, 44, 44);
    lv_obj_align(s_btn_main_icon, LV_ALIGN_CENTER, 0, 0);

    /* 去向说明：stop 时刻若BT 已连→ 实时发到了手机；否则落到手表本地 */
    char buf[48];
    char dur[16];
    format_duration(dur, sizeof(dur), s_elapsed_ms);
    if (s_was_connected)
        snprintf(buf, sizeof(buf), "%s · 已发送至手机", dur);
    else
        snprintf(buf, sizeof(buf), "%s · 已保存到手表", dur);
    ui_set_hint(buf);

    rings_set_visible(false);
    if (s_amp_bg)
        lv_obj_add_flag(s_amp_bg, LV_OBJ_FLAG_HIDDEN);

    /* 同上：按真实文件数决定，避免 0 个文件时误显示入口 */
    if (s_offline_entry)
      {
        lv_obj_align(s_offline_entry, LV_ALIGN_BOTTOM_MID, 0, Y_OFFLINE_BOT);
        if (offline_recordings_count() > 0)
            lv_obj_clear_flag(s_offline_entry, LV_OBJ_FLAG_HIDDEN);
        else
            lv_obj_add_flag(s_offline_entry, LV_OBJ_FLAG_HIDDEN);
      }
}

/****************************************************************************
 * 录音控制
 ****************************************************************************/
/**
 * 开始录音。
 * 不依赖蓝牙连接：未连时音频会落到 /emmc 离线缓存（spp_audio_stream 内部按
 * bt_transport_is_connected() 决定实时发送还是落盘，连上后由 spp_on_connected 回传），
 * 因此任何时候都可以录。
 */
static void recorder_do_start(void)
{
    if (spp_audio_stream_is_active())
        return;

    s_was_connected = bt_transport_is_connected();   /* 记录本次是否实时发送 */
    spp_audio_stream_start();
    ui_enter_recording();
    printf("[recorder] start (connected=%d)\n", (int)s_was_connected);
}

/* 打开录音 App 并立即开始录音（供小智 MCP 语音指令调用）。
 * 若录音界面尚未创建则先 recorder_start() 建好 UI，再进入录音中态。 */
void recorder_start_recording(void)
{
    if (s_container == NULL)
        recorder_start();
    recorder_do_start();
}

static void recorder_do_stop(void)
{
    if (!spp_audio_stream_is_active())
        return;

    s_elapsed_ms = lv_tick_get() - s_record_start_ms;
    s_was_connected = bt_transport_is_connected();

    spp_audio_stream_stop();

    if (s_anim_timer)
      {
        lv_timer_del(s_anim_timer);
        s_anim_timer = NULL;
      }
    main_btn_reset_size();   /* 停止呼吸缩放，回到基准直径 */

    ui_show_saved();
    printf("[recorder] stop, elapsed=%ums, connected=%d\n",
           (unsigned)s_elapsed_ms, (int)s_was_connected);

    /* 停留 SAVED_HOLD_MS 后自动回到待机 */
    if (s_tick_timer)
        lv_timer_ready(s_tick_timer);
}

/* 确保录音完全停止（退出/异常时调用） */
static void recorder_ensure_stopped(void)
{
    if (spp_audio_stream_is_active())
        spp_audio_stream_stop();
    if (s_anim_timer)
      {
        lv_timer_del(s_anim_timer);
        s_anim_timer = NULL;
      }
}

/****************************************************************************
 * 周期 tick：计时推进 + 已保存态自动返回 + 连接状态变化重绘
 ****************************************************************************/
static void tick_cb(lv_timer_t *t)
{
    (void)t;

    switch (s_state)
      {
        case UI_RECORDING:
          ui_set_timer_text(lv_tick_get() - s_record_start_ms);
          break;

        case UI_SAVED:
          if (s_saved_since_ms == 0)
              s_saved_since_ms = lv_tick_get();   /* 首次进入，记录起点 */
          else if (lv_tick_get() - s_saved_since_ms >= SAVED_HOLD_MS)
            {
              s_saved_since_ms = 0;
              ui_back_to_idle();
            }
          break;

        default:
            break;
      }

    /* 连接状态变化检测：bt_transport 的连接回调是单槽、已被 spp_audio_stream
     * 占用（离线回传要用），此处不能重复注册；改为轮询比对。
     * 变化时重跑一次当前态的UI 铺设，否则主按钮形态/提示语会停留在旧状态，
     * 表现为「连上蓝牙后页面不更新，退出重进才更新」。 */
    bool conn = bt_transport_is_connected();
    if (conn != s_last_conn) {
        s_last_conn = conn;
        ui_update_conn_pill();
        /* 仅在待机态重绘主按钮（录音中/已保存态的主按钮形态与连接无关） */
        if (s_state == UI_IDLE)
            ui_back_to_idle();
    }
}

/****************************************************************************
 * 手势：右滑退出
 ****************************************************************************/
static void gesture_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_GESTURE)
        return;
    lv_dir_t dir = lv_indev_get_gesture_dir(lv_indev_active());
    if (dir != LV_DIR_RIGHT)
        return;

    LV_LOG_USER("recorder: gesture RIGHT -> exit");

    recorder_ensure_stopped();

    if (s_anim_timer)   { lv_timer_del(s_anim_timer); s_anim_timer = NULL; }
    if (s_tick_timer)   { lv_timer_del(s_tick_timer); s_tick_timer = NULL; }
    if (s_offline_timer){ lv_timer_del(s_offline_timer); s_offline_timer = NULL; }
    offline_recordings_set_close_hook(NULL);   /* 录音页将卸载，移除列表关闭回调 */

    obj_reset(&s_container);
    s_title = NULL;
    s_conn_pill = NULL; s_conn_dot = NULL; s_conn_label = NULL;
    s_timer_label = NULL;
    for (int i = 0; i < RING_COUNT; i++) s_rings[i] = NULL;
    s_amp_bg = NULL;
    s_btn_main = NULL; s_btn_main_icon = NULL;
    s_hint_label = NULL;
    s_offline_entry = NULL; s_offline_label = NULL;

    offline_recordings_close();   /* 兜底：若离线列表仍开着，一并关闭 */

    s_state = UI_IDLE;
    s_elapsed_ms = 0;
    s_saved_since_ms = 0;
    s_last_offline_n = -1;
    s_last_conn = false;

    nav_return_from_app();
}

/****************************************************************************
 * 按钮
 ****************************************************************************/
static void btn_event_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED)
        return;

    switch (s_state)
      {
        case UI_RECORDING:
            recorder_do_stop();
            break;
        case UI_IDLE:
            recorder_do_start();
            break;
        case UI_SAVED:
            /* 「再来一次」：立刻回到待机，可立即重录 */
            ui_back_to_idle();
            break;
      }
}

/* 离线入口：打开列表覆盖层 */
static void offline_entry_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED)
        return;
    if (s_state == UI_RECORDING)
        return;   /* 录音中不响应（入口此时已隐藏，双保险） */
    offline_recordings_open();
}

/****************************************************************************
 * 离线入口可见性（有离线文件才显示）
 ****************************************************************************/
static void refresh_offline_entry(void)
{
    if (!s_offline_entry || s_state == UI_RECORDING)
        return;

    int c = offline_recordings_count();
    if (c != s_last_offline_n) {
        printf("[recorder] 离线文件数=%d(列表扫描结果;为 0 表示 /emmc 无可回传文件)\n", c);
        s_last_offline_n = c;
    }
    if (c > 0) {
        char buf[32];
        snprintf(buf, sizeof(buf), "离线 %d", c);
        if (s_offline_label)
            lv_label_set_text(s_offline_label, buf);
        lv_obj_clear_flag(s_offline_entry, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(s_offline_entry, LV_OBJ_FLAG_HIDDEN);
    }
}

static void offline_entry_timer_cb(lv_timer_t *t)
{
    (void)t;
    refresh_offline_entry();
}

/****************************************************************************
 * 公开 API
 ****************************************************************************/
void recorder_start(void)
{
    lv_obj_t *scr = get_watch_scr();
    if (!scr)
        return;

    /* 根容器（圆形屏幕） */
    s_container = lv_obj_create(scr);
    lv_obj_set_size(s_container, LV_CIRCLE_WATCH, LV_CIRCLE_WATCH);
    lv_obj_align(s_container, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_color(s_container, BG_BASE, 0);
    lv_obj_set_style_bg_opa(s_container, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_container, 0, 0);
    lv_obj_set_style_pad_all(s_container, 0, 0);
    lv_obj_set_style_radius(s_container, LV_RADIUS_CIRCLE, 0);
    lv_obj_clear_flag(s_container, LV_OBJ_FLAG_SCROLLABLE);

    /* 手势 */
    lv_obj_add_event_cb(s_container, gesture_cb, LV_EVENT_GESTURE, NULL);
    lv_obj_remove_flag(s_container, LV_OBJ_FLAG_GESTURE_BUBBLE);

    /* ---- 标题 ---- */
    s_title = lv_label_create(s_container);
    lv_label_set_text(s_title, "录音");
    lv_obj_set_style_text_font(s_title, font_manager_get_font(FONT_LARGE), 0);
    lv_obj_set_style_text_color(s_title, TEXT_PRIMARY, 0);
    lv_obj_align(s_title, LV_ALIGN_TOP_MID, 0, Y_TITLE);

    /* ---- 连接状态胶囊 ---- */
    s_conn_pill = lv_obj_create(s_container);
    lv_obj_remove_style_all(s_conn_pill);
    lv_obj_set_size(s_conn_pill, 84, 26);
    lv_obj_align(s_conn_pill, LV_ALIGN_TOP_MID, 0, Y_CONN_PILL);
    lv_obj_set_style_radius(s_conn_pill, 13, 0);
    lv_obj_set_style_border_width(s_conn_pill, 1, 0);
    lv_obj_set_style_bg_opa(s_conn_pill, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(s_conn_pill, 0, 0);
    lv_obj_clear_flag(s_conn_pill, LV_OBJ_FLAG_SCROLLABLE);

    s_conn_dot = make_circle(s_conn_pill, 8, TEXT_DIM, LV_RADIUS_CIRCLE, LV_OPA_COVER);
    lv_obj_align(s_conn_dot, LV_ALIGN_LEFT_MID, 12, 0);

    s_conn_label = lv_label_create(s_conn_pill);
    lv_obj_set_style_text_font(s_conn_label, font_manager_get_font(FONT_SMALL), 0);
    lv_obj_align(s_conn_label, LV_ALIGN_LEFT_MID, 26, 0);

    /* ---- 录音计时（录音中显示） ---- */
    s_timer_label = lv_label_create(s_container);
    lv_label_set_text(s_timer_label, "00:00");
    lv_obj_set_style_text_font(s_timer_label, &lv_font_montserrat_32, 0);
    lv_obj_set_style_text_color(s_timer_label, TEXT_PRIMARY, 0);
    lv_obj_align(s_timer_label, LV_ALIGN_TOP_MID, 0, Y_TIMER_REC);
    lv_obj_add_flag(s_timer_label, LV_OBJ_FLAG_HIDDEN);

    /* ---- 振幅背景层：在主按钮之下（先创建=靠后），纯装饰、不可点击，
     *  录音中由 anim_tick_cb 用 transform 做呼吸（视觉不改命中区，不影响点击）。 ---- */
    s_amp_bg = make_circle(s_container, MAIN_BTN_D, STOP_COLOR, LV_RADIUS_CIRCLE, LV_OPA_30);
    lv_obj_align(s_amp_bg, LV_ALIGN_CENTER, 0, Y_MAIN);
    lv_obj_set_style_transform_pivot_x(s_amp_bg, MAIN_BTN_D / 2, 0);
    lv_obj_set_style_transform_pivot_y(s_amp_bg, MAIN_BTN_D / 2, 0);
    lv_obj_set_style_shadow_opa(s_amp_bg, LV_OPA_40, 0);
    lv_obj_add_flag(s_amp_bg, LV_OBJ_FLAG_HIDDEN);   /* 仅录音中显示 */

    /* ---- 主按钮：待机/已保存=同尺寸大圆；录音中=停止键 ----
     * 整个生命周期内尺寸/位置/transform 一律不动，仅换色与显隐，命中区恒定。
     * 必须在呼吸环之前创建：LVGL 后创建者 z序在上，呼吸环盖在按钮之上呈前景特效；
     * 环不可点击，点击仍由本按钮接收。 */
    s_btn_main = lv_obj_create(s_container);
    lv_obj_set_size(s_btn_main, MAIN_BTN_D, MAIN_BTN_D);
    lv_obj_align(s_btn_main, LV_ALIGN_CENTER, 0, Y_MAIN);
    lv_obj_set_style_radius(s_btn_main, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(s_btn_main, 0, 0);
    lv_obj_set_style_pad_all(s_btn_main, 0, 0);
    lv_obj_set_style_shadow_width(s_btn_main, 14, 0);
    lv_obj_set_style_shadow_opa(s_btn_main, LV_OPA_40, 0);
    lv_obj_clear_flag(s_btn_main, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_ext_click_area(s_btn_main, 14);     /* 略微扩大点击热区，更易命中 */
    lv_obj_add_event_cb(s_btn_main, btn_event_cb, LV_EVENT_CLICKED, NULL);

    s_btn_main_icon = lv_obj_create(s_btn_main);
    lv_obj_remove_style_all(s_btn_main_icon);
    lv_obj_center(s_btn_main_icon);
    lv_obj_clear_flag(s_btn_main_icon, LV_OBJ_FLAG_CLICKABLE);  /* 图标不可点击，点击直达按钮 */

    /* ---- 呼吸环（录音中显示，z序在主按钮之上 → 呈现在振幅特效前景） ---- */
    for (int i = 0; i < RING_COUNT; i++)
      {
        int d = MAIN_BTN_D + RING_GAP * 2 * (i + 1);
        s_rings[i] = make_circle(s_container, d, BAR_COLOR, LV_RADIUS_CIRCLE,
                                 (lv_opa_t)(LV_OPA_50 - i * 14));
        lv_obj_align(s_rings[i], LV_ALIGN_CENTER, 0, Y_MAIN);
        lv_obj_add_flag(s_rings[i], LV_OBJ_FLAG_HIDDEN);
      }

    /* ---- 状态提示语 ---- */
    s_hint_label = lv_label_create(s_container);
    lv_obj_set_style_text_font(s_hint_label, font_manager_get_font(FONT_MEDIUM), 0);
    lv_obj_set_style_text_color(s_hint_label, TEXT_SECONDARY, 0);
    lv_obj_align(s_hint_label, LV_ALIGN_CENTER, 0, Y_HINT);

    /* ---- 离线录音入口（底部居中胶囊，处于圆屏安全区内） ---- */
    s_offline_entry = lv_obj_create(s_container);
    lv_obj_remove_style_all(s_offline_entry);
    lv_obj_set_size(s_offline_entry, OFFLINE_BTN_W, OFFLINE_BTN_H);
    lv_obj_align(s_offline_entry, LV_ALIGN_BOTTOM_MID, 0, Y_OFFLINE_BOT);
    lv_obj_set_style_radius(s_offline_entry, OFFLINE_BTN_H / 2, 0);
    lv_obj_set_style_bg_color(s_offline_entry, lv_color_hex(0x2A2010), 0);
    lv_obj_set_style_bg_opa(s_offline_entry, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_offline_entry, 1, 0);
    lv_obj_set_style_border_color(s_offline_entry, OFFLINE_COLOR, 0);
    lv_obj_add_event_cb(s_offline_entry, offline_entry_cb, LV_EVENT_CLICKED, NULL);

    s_offline_label = lv_label_create(s_offline_entry);
    lv_label_set_text(s_offline_label, "离线");
    lv_obj_set_style_text_font(s_offline_label, font_manager_get_font(FONT_SMALL), 0);
    lv_obj_set_style_text_color(s_offline_label, OFFLINE_COLOR, 0);
    lv_obj_center(s_offline_label);

    /* ---- 进入待机态（按连接状态决定主按钮形态与提示语） ---- */
    s_state = UI_IDLE;
    s_last_conn = bt_transport_is_connected();   /* 记录基线，避免首个 tick 误判为"状态变化" */
    ui_back_to_idle();
    ui_update_conn_pill();

    refresh_offline_entry();
    s_offline_timer = lv_timer_create(offline_entry_timer_cb, OFFLINE_POLL_MS, NULL);
    s_tick_timer    = lv_timer_create(tick_cb, TICK_MS, NULL);

    /* 注册列表关闭回调：离线列表每次退出（含删完最后一个文件）都会立即刷新本页离线入口，
       避免回到录音页时「离线」按钮残留一秒才消失 */
    offline_recordings_set_close_hook(refresh_offline_entry);

    /* 进入录音 App 即注册离线回传回调（即便尚未点"开始录音"，已存在的离线文件
     * 也能回传）；若 SPP 已连，立即触发一次回传，覆盖"先连蓝牙、后进 App"。 */
    spp_audio_ensure_offline_sync();
}