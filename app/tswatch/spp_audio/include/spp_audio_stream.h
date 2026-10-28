#ifndef SPP_AUDIO_STREAM_H
#define SPP_AUDIO_STREAM_H

#include <stdbool.h>
#include <stdint.h>

/**
 * SPP 音频流：把手表麦克风采集到的 16kHz/16bit/单声道 PCM，经蓝牙 SPP
 * (bt_transport) 实时发到手机端，由手机端流式上传讯飞做识别/摘要。
 *
 * 设计要点：
 *   - 本模块自己打开麦克风采集（media_recorder），不再依赖小智语音会话，
 *     因此"录音"功能可以独立成单独 App，无需先启动小智。
 *   - 每次读 60ms(1920B) PCM，切成 20ms(640B) 帧；每 2 帧(40ms) 用 Opus 编码成一个
 *     音频包经 SPP 发给手机（type=4 OPUS）。手机端解码回 16k/16bit/mono PCM 后上传 ASR。
 *     静音也连续发送（Opus VBR 下静音近乎 0 字节），既降蓝牙数据量又避免 ASR 15s 超时。
 *   - 每帧带简单帧头（magic + type + seq + len），手机端据此拆帧：type=1 PCM / 2 控制 /
 *     3 文本 / 4 OPUS。
 *   - 仅在 SPP 已连接(bt_transport_is_connected)时才发送，未连时静默丢弃，
 *     避免阻塞采集线程。
 *   - 采集同时计算实时振幅 spp_audio_stream_get_amplitude()，供 UI 画声音
 *     振幅条（类似小智"按下开始录音"的律动效果，但由真实麦克风能量驱动）。
 */

void spp_audio_stream_start(void);
void spp_audio_stream_stop(void);
bool spp_audio_stream_is_active(void);

/* 录音 App 进入时调用：注册离线回传所需的"连接状态回调 + 接收回调"，
 * 即使尚未开始录音，已存在的离线文件也能回传；若 SPP 此刻已连，
 * 立即触发一次回传（覆盖"先连蓝牙、后进 App"的场景）。 */
void spp_audio_ensure_offline_sync(void);

/* 返回最近一帧麦克风振幅，范围 0.0(静音) ~ 1.0(满幅)。供录音 App 画律动条。 */
float spp_audio_stream_get_amplitude(void);

#endif /* SPP_AUDIO_STREAM_H */
