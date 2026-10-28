/****************************************************************************
 * bt_pair_ui.h - 配对码屏幕显示
 *
 * 手表侧"数字比较"配对(SSP Passkey Confirmation)时, 栈会协商出一个 6 位配对码,
 * 手机与手表需显示同一串数字, 由用户在手机端点确认。
 *
 * 本文件把配对码渲染到手表屏幕: 半透明居中覆盖层 + 大字号数字 + 提示文字。
 * 配对完成(BONDED)或取消(NONE)时由 bt_pair 通过 bt_pair_ui_hide_passkey() 立即收起;
 * 另设 30s 自动隐藏兜底。显示/隐藏动作均通过 lv_async_call() 投到 LVGL 主线程执行,
 * 因此可从蓝牙后台线程安全调用(参考 xiaozhi_voice 的跨线程 UI 做法)。
 ****************************************************************************/
#ifndef __TSWATCH_BT_PAIR_UI_H__
#define __TSWATCH_BT_PAIR_UI_H__

#include "bluetooth.h"     /* bt_address_t */
#include "bt_device.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * 注册"配对码显示"回调到 bt_pair 模块, 使数字比较配对时把 6 位配对码画到屏幕。
 * 应在 bt_manager_start 中调用一次(幂等)。
 */
void bt_pair_ui_init(void);

/**
 * 配对码显示回调: 把 passkey 投到屏幕。可直接作为 bt_pair_passkey_disp_cb_t 使用。
 * 运行于蓝牙线程, 内部用 lv_async_call 保证线程安全。
 *
 * @param passkey 6 位十进制配对码(0~999999)
 * @param peer    对端(手机)地址
 */
void bt_pair_ui_show_passkey(uint32_t passkey, const bt_address_t *peer);

/**
 * 隐藏配对码覆盖层。配对完成(BONDED)或取消(NONE)时由 bt_pair 调用,
 * 内部经 lv_async_call 投到 LVGL 主线程安全删除, 使页面在配对结束后立即消失,
 * 而非等到 30s 超时兜底。对端地址无需传入, 任何配对结束都收起当前覆盖层。
 */
void bt_pair_ui_hide_passkey(void);

#ifdef __cplusplus
}
#endif

#endif /* __TSWATCH_BT_PAIR_UI_H__ */
