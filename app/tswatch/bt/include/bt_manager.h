/****************************************************************************
 * bt_manager.h - tswatch 蓝牙子系统顶层入口
 *
 * 这是 tswatch 应用端蓝牙功能的统一对外入口。只需在开机时调用一次
 * bt_manager_start(), 内部会自动完成:
 *   1. 等待 bluetoothd(IPC) 就绪并 enable adapter(BREDR + BLE)
 *   2. 设置本地名(默认 TSWATCH)
 *   3. 注册适配器回调: 配对请求自动接受(Just Works)、ACL 连接状态切换可发现性
 *   4. 启动 BLE 可连接广播 + 经典蓝牙可发现
 *   5. 注册并启动 SPP 服务(手表<->手机 数据通道, 见 bt_transport.h)
 *   6. 若已配对(已绑定): 自动连接; 未连接时保持可发现可配对
 *   7. 连接成功后自动关闭可发现(仅可连接); 断开后重新可发现
 *
 * 头文件都放在 bt/include, 源文件在 bt/src。
 ****************************************************************************/
#ifndef __TSWATCH_BT_MANAGER_H__
#define __TSWATCH_BT_MANAGER_H__

#ifdef __cplusplus
extern "C" {
#endif

/**
 * 初始化并启动 tswatch 蓝牙子系统。
 * 异步后台线程执行, 调用后立即返回, 不阻塞。
 * 必须在 watch 主循环/UI 初始化后调用一次。
 */
void bt_manager_start(void);

#ifdef __cplusplus
}
#endif

#endif /* __TSWATCH_BT_MANAGER_H__ */
