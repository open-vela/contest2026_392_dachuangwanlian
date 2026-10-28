#ifndef SPP_TEST_H
#define SPP_TEST_H

#include <stdint.h>

/**
 * 启动「SPP 蓝牙测试」应用。
 * 该应用用于手动验证 tswatch(作为 SPP Server) 与手机(Android SPP Client) 的
 * 经典蓝牙串口连接，并能：
 *   1. 显示当前 SPP 通道连接状态(手机是否已连接)；
 *   2. 点击「发送测试」向手机发送一条字串；
 *   3. 点击「连续发送」每 500ms 自动发送递增序号字串(压测/验证连续性)；
 *   4. 实时显示手机经 SPP 回传(Android 端 sendText)或本机发出的字串日志；
 *   5. 右滑返回上一页。
 *
 * 依赖固件 bt_transport 模块：bt_transport_send_str / bt_transport_is_connected /
 * bt_transport_set_rx_callback（均由 bt_manager_start() 拉起的 SPP Server 提供）。
 */
void spp_test_start(void);

#endif /* SPP_TEST_H */
