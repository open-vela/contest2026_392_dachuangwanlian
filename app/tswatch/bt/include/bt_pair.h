/****************************************************************************
 * bt_pair.h - 配对/绑定 回调
 *
 * 手表无配对 UI, 对所有配对请求自动接受(Just Works)。
 * 这里只负责"装配"配对/绑定相关的适配器回调到 bt_manager 的统一回调表,
 * 实际的注册与连接状态处理由 bt_manager 完成。
 ****************************************************************************/
#ifndef __TSWATCH_BT_PAIR_H__
#define __TSWATCH_BT_PAIR_H__

#include "bluetooth.h"     /* bt_instance_t */
#include "bt_adapter.h"    /* adapter_callbacks_t */
#include "bt_device.h"     /* bt_address_t */

#ifdef __cplusplus
extern "C" {
#endif

/**
 * 把配对/绑定相关回调(on_pair_request / on_pair_display / on_bond_state_changed)
 * 装配到 adapter 回调表。调用方(bt_manager)负责填写 on_connection_state_changed
 * 并最终调用 bt_adapter_register_callback。
 *
 * 注意: 由于框架 bt_remote_callbacks_register 只保存回调表指针、不拷贝内容,
 *       传入的 adapter_callbacks_t 必须是长期有效的对象(由 bt_manager 用
 *       文件级静态变量保存), 切勿传入栈局部变量。
 *
 * 本函数同时会把设备的 IO 能力设为 BT_IO_CAPABILITY_DISPLAYYESNO(经典)与
 * DisplayYesNo(LE), 使配对协商出"数字比较"方式并生成可显示的配对码。
 *
 * 注意: 由于框架 bt_remote_callbacks_register 只保存回调表指针、不拷贝内容,
 *       传入的 adapter_callbacks_t 必须是长期有效的对象(由 bt_manager 用
 *       文件级静态变量保存), 切勿传入栈局部变量。
 *
 * @param ins 蓝牙实例(用于自动接受配对时回确认)
 * @param cbs 待填充的回调表
 */
void bt_pair_attach_cbs(bt_instance_t *ins, adapter_callbacks_t *cbs);


/**
 * 配对码显示回调: 当栈协商出"数字比较"配对时, 上层(UI)通过此回调拿到 6 位配对码,
 * 把它渲染到手表屏幕。若未注册, 则仅在日志中以醒目方式打印。
 *
 * @param passkey 6 位十进制配对码(0~999999)
 * @param peer    对端(手机)地址
 */
typedef void (*bt_pair_passkey_disp_cb_t)(uint32_t passkey, const bt_address_t *peer);

/**
 * 注册配对码显示回调(用于把配对码画到屏幕)。不注册也行, 默认只打日志。
 */
void bt_pair_set_passkey_display_cb(bt_pair_passkey_disp_cb_t cb);

#ifdef __cplusplus
}
#endif

#endif /* __TSWATCH_BT_PAIR_H__ */
