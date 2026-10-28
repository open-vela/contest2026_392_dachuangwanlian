/****************************************************************************
 * bt_adv.h - BLE 广播
 *
 * 仅负责启动 BLE 可连接广播(名字由 bt_manager 通过 bt_adapter_set_name 设置)。
 * 经典蓝牙的可发现/可连接(scan mode)由 bt_manager 统一管理。
 ****************************************************************************/
#ifndef __TSWATCH_BT_ADV_H__
#define __TSWATCH_BT_ADV_H__

#include "bluetooth.h"   /* bt_instance_t */

/**
 * 启动 BLE 可连接广播(ADV_IND)。
 * @param ins 已 ON 的蓝牙实例
 * @return 0 成功, <0 失败
 */
int bt_adv_start_ble(bt_instance_t *ins);

#endif /* __TSWATCH_BT_ADV_H__ */
