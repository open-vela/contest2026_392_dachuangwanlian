/****************************************************************************
 * bt_transport.h - 手表 <-> 手机 数据传输对外 API
 *
 * 底层基于蓝牙 SPP(Serial Port Profile, UUID 0x1101) + 框架 euv_pipe(libuv
 * 命名管道)。手表作为 SPP Server, 手机作为 SPP Client 连接后, 双方即可
 * 像串口一样互发任意二进制数据。
 *
 * 典型用法:
 *   // 注册接收回调(可选)
 *   bt_transport_set_rx_callback(my_rx_cb);
 *   // 发送
 *   bt_transport_send((uint8_t*)"hello", 5);
 *   bt_transport_send_str("hello phone");
 *
 * 接收回调在蓝牙数据线程中触发, 回调内不要做长时间阻塞操作。
 ****************************************************************************/
#ifndef __TSWATCH_BT_TRANSPORT_H__
#define __TSWATCH_BT_TRANSPORT_H__

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "bluetooth.h"   /* bt_address_t */

#ifdef __cplusplus
extern "C" {
#endif

/**
 * 收到对端(手机)数据时的回调。
 * @param data  负载数据指针(生命周期仅在回调内有效, 需自行拷贝)
 * @param len   数据长度
 * @param peer  对端蓝牙地址
 */
typedef void (*bt_transport_rx_cb_t)(const uint8_t *data, uint16_t len,
                                     const bt_address_t *peer);

/**
 * 初始化 SPP 数据通道(由 bt_manager 内部调用, 应用一般不需直接调用)。
 * @param ins 蓝牙实例
 */
void bt_transport_init(void *ins);

/**
 * 发送数据到已连接的对端(手机)。
 * @param data 待发送数据
 * @param len  数据长度(0 表示按字符串处理, 见 bt_transport_send_str)
 * @return     实际发送的字节数(>0); <0 表示未连接或失败
 */
int bt_transport_send(const uint8_t *data, uint16_t len);

/**
 * 便捷接口: 发送以 '\0' 结尾的字符串(自动计算长度, 不含结尾 '\0')。
 * @return 发送的字节数; <0 表示未连接或失败
 */
int bt_transport_send_str(const char *str);

/**
 * 注册/替换数据接收回调。传 NULL 可取消回调。
 */
void bt_transport_set_rx_callback(bt_transport_rx_cb_t cb);

/**
 * 注册/替换 SPP 连接状态变化回调：对端（手机）连接建立时回调一次（peer 为对端地址）。
 * 用于驱动离线录音文件回传等"连上即触发"的逻辑。传 NULL 可取消。
 */
typedef void (*bt_transport_conn_cb_t)(const bt_address_t *peer);
void bt_transport_set_conn_callback(bt_transport_conn_cb_t cb);

/**
 * 当前是否已建立数据通道(SPP proxy 已连接)。
 */
bool bt_transport_is_connected(void);

/**
 * 取得当前已连接的 SPP 对端(手机)蓝牙地址。
 * 供蓝牙上网(PAN)等需要知道"连的是哪台手机"的场景使用。
 * @param out 输出地址(仅当返回 0 时有效)
 * @return    0 成功; <0 表示数据通道未连接
 */
int bt_transport_get_peer_addr(bt_address_t *out);

#ifdef __cplusplus
}
#endif

#endif /* __TSWATCH_BT_TRANSPORT_H__ */
