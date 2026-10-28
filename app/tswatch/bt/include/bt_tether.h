/****************************************************************************
 * bt_tether.h - 手表蓝牙上网(PAN / BNEP)对外 API
 *
 * 原理: 手机开启系统"蓝牙网络共享"即成为 NAP(Network Access Point);
 * 手表作为 PANU 去连接它。BNEP 链路建立后, 手表侧由蓝牙守护进程自动
 * 创建一个名为 "bt-pan" 的虚拟网卡, 手机侧的 NAP 通过 DHCP 给手表分配
 * IP 并把默认路由指向自身, 手表即可"借"手机移动网络上网。
 *
 * 该方案手机端无需安装任何 App(用系统自带的蓝牙共享即可)。
 *
 * 本模块随固件默认编译, 手表固件已支持蓝牙上网(PAN/BNEP), 直接调用
 * 接口即可, 不依赖任何编译期开关。
 *
 * 日志前缀: [bt_tether]
 ****************************************************************************/
#ifndef __TSWATCH_BT_TETHER_H__
#define __TSWATCH_BT_TETHER_H__

#include <stdbool.h>
#include <stdint.h>
#include "bluetooth.h"   /* bt_instance_t, bt_address_t */

#ifdef __cplusplus
extern "C" {
#endif

/**
 * 状态监听回调: connected=true 表示已成功借手机网络上网(可联网),
 * false 表示已断开。回调在蓝牙 IPC 线程中触发, 实现方需自行线程安全
 * 地更新 UI(本项目统一用 lv_async_call 投到 LVGL 主线程)。
 */
typedef void (*bt_tether_state_cb_t)(bool connected);

/* 由 bt_manager 后台线程初始化(注册 PAN 回调), 应用一般不需直接调用。 */
void bt_tether_init(void *ins);

/* 当前固件是否支持蓝牙上网(编译期决定)。 */
bool bt_tether_is_supported(void);

/* 开始连接: 手表作为 PANU 去连当前已连接的手机(NAP)。
 * 返回 0 表示已发起连接(异步, 结果经状态回调通知), <0 表示失败。 */
int bt_tether_start(void);

/* 断开蓝牙上网。返回 0 表示已发起, <0 表示失败/未初始化。 */
int bt_tether_stop(void);

/* 是否已建立蓝牙上网(PAN 连接 + bt-pan 网卡 up + 已拿到 IP)。 */
bool bt_tether_is_connected(void);

/* 取得实际 bt-pan 接口名(由回调回传, 默认 "bt-pan")。 */
const char *bt_tether_get_ifname(void);

/* 注册状态监听(用于 UI 同步开关/状态文字)。传 NULL 取消。 */
void bt_tether_set_state_listener(bt_tether_state_cb_t cb);

#ifdef __cplusplus
}
#endif

#endif /* __TSWATCH_BT_TETHER_H__ */
