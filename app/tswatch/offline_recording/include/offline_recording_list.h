#ifndef OFFLINE_RECORDING_LIST_H
#define OFFLINE_RECORDING_LIST_H

#include <lvgl/lvgl.h>
#include <stdbool.h>

/**
 * @brief 打开端侧「离线录音」列表（覆盖层，非独立 App）。
 *
 * 该列表以覆盖层形式叠加在调用方页面（录音 App）之上，展示 /emmc 下待同步 /
 * 同步中的离线录音文件（按开始时间倒序，最新在最上），每行显示 开始时间 +
 * 估算时长 + 状态（待同步/同步中/已同步）。
 * - PENDING 行长按 → 弹确认框 → 删除本地文件（不发送到手机）；
 * - 收到手机 TYPE_FILE_ACK(success) 后，对应行显示「已同步」(绿) 并弹轻量 toast，
 *   约 1.5s 后自动移除（本地文件已由离线落盘模块在 ACK 时删除）；
 * - 右滑退出（仅关闭覆盖层，回到下层页面）。
 *
 * 注意：本函数不再调用 nav_enter_app / nav_return_from_app，覆盖层直接挂在
 * get_watch_scr() 之上，关闭后下层（录音 App）自动恢复显示。
 *
 * 与离线落盘模块（offline_recording_store / spp_audio_stream）共用同一份
 * /emmc/rec_*.bin 清单（设计文档 §11）。
 */
void offline_recordings_open(void);

/**
 * @brief 关闭离线录音列表覆盖层（清理定时器 / 弹窗 / 容器）。
 * 正常退出由右滑手势内部调用，外部一般无需手动调用，但供调用方页面退出时兜底清理。
 * 关闭时会调用通过 offline_recordings_set_close_hook 注册的回调（若有）。
 */
void offline_recordings_close(void);

/**
 * @brief 注册「列表关闭」回调。列表覆盖层每次退出（右滑 / 返回键 / 删完最后一文件）
 * 都会触发该回调，供调用方（如录音 App）立即刷新自身状态（如隐藏离线入口），
 * 避免在列表关闭、回到下层页面时状态刷新滞后。
 * 调用方应在自身创建时注册、销毁时传 NULL 注销（刷新函数须自带空指针保护）。
 * @param cb 回调（签名为 void (*)(void)），传 NULL 即注销。
 */
void offline_recordings_set_close_hook(void (*cb)(void));

/**
 * @brief 是否存在离线录音文件（排除正在写入的当前录音文件）。
 * 供录音 App 决定是否显示「离线录音」入口。
 */
bool offline_recordings_available(void);

/**
 * @brief 离线录音文件数量（排除正在写入的当前录音文件）。
 */
int offline_recordings_count(void);

/**
 * @brief 离线文件同步成功（手机 ACK）后由 spp_audio_stream 调用，
 * 触发列表页「已同步」提示。线程安全：内部经 lv_async_call 投到 LVGL 主线程处理。
 * @param name 文件名（rec_<UTCms>.bin）
 */
void offline_recordings_on_synced(const char *name);

#endif /* OFFLINE_RECORDING_LIST_H */
