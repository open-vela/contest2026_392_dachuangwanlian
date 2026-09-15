# xiaozhi_voice 项目

## 项目概述
xiaozhi_voice 是小智语音助手模块，运行在基于 Vela RTOS (NuttX) 的智能手表上，使用 LVGL v8 图形库。

## 核心架构
- **状态机**: BOOT(0) → OTA(1) → CONNECT(2) → READY(3) → LISTENING(4) → THINKING(5) → PLAYING(6) → DONE(7) → ERROR(8)
- **线程模型**: 
  - LVGL 主线程: UI渲染、动画、定时器
  - recv_thread: WebSocket 接收、状态变更
  - dialog_round_thread: 语音对话轮次处理
  - connect_thread: WebSocket 连接管理

## 关键文件
- `xiaozhi_main.c`: UI界面、状态机、所有动画逻辑
- `xiaozhi_ws.c/h`: WebSocket 通信
- `xiaozhi_audio.c/h`: 音频处理(录音/播放)
- `xiaozhi_ota.c/h`: OTA升级
- `xiaozhi_ota_tls.c`: TLS相关

## 工作规则

### 1. CodeGraph 优先
每次修改 `xiaozhi_voice/` 下的代码前，**必须先阅读 codegraph 内容**，了解当前代码结构、符号引用关系和调用路径。使用 `codegraph_explore` 工具查询相关函数/文件，避免凭记忆猜测代码结构。

### 2. CodeGraph 同步
每次对 `xiaozhi_voice/` 下的代码进行**较多修改**后，必须执行 `codegraph sync` 更新代码图谱，确保图谱与实际代码保持一致。

### 3. 语言规范
所有思考过程和输出回答**使用中文**。

### 4. 日志记录
工作产生的日志和记录写入以下目录：
```
/home/ts/code/code11/contest2026_392_dachuangwanlian/logs/liyc0623/
```
按日期建子目录，格式参考：`/home/ts/code/code11/contest2026_392_dachuangwanlian/logs/your-github-login/2026-01-01/claude-code__example.jsonl`

## 线程安全规范
1. **所有UI操作必须在LVGL线程执行**
2. 跨线程状态变更使用 `pthread_mutex_lock(&s_ui_mutex)` 保护
3. 轻量级信号使用 `volatile bool` 标志 (如 `ws_reconnecting`)
4. UI操作使用 `lv_async_call()` 异步执行

## LVGL API 规范 (v8)
- 颜色格式: `lv_color_hex(0xRRGGBB)`
- 透明度: `lv_opa_t` (LV_OPA_TRANSP=0, LV_OPA_COVER=255)
- 对齐: `lv_obj_align(obj, LV_ALIGN_*, x_ofs, y_ofs)`
- 样式: `lv_obj_set_style_*(obj, value, selector)` (0=主样式)
- 事件: `lv_obj_add_event_cb(obj, cb, event, user_data)`
- Canvas: `lv_canvas_create()`, `lv_canvas_set_buffer()`, `lv_canvas_fill_bg()`
- 定时器: `lv_timer_create(cb, period_ms, user_data)`

## 常用 LVGL v8 函数参考
- `lv_obj_create(parent)` - 创建基础对象
- `lv_label_create(parent)` - 创建标签
- `lv_image_create(parent)` - 创建图片对象
- `lv_image_set_src(obj, path)` - 设置图片源
- `lv_canvas_create(parent)` - 创建画布
- `lv_canvas_set_buffer(canvas, buf, w, h, cf)` - 设置画布缓冲区
- `lv_timer_create(cb, period, user_data)` - 创建定时器
- `lv_timer_del(timer)` - 删除定时器
- `lv_obj_del(obj)` - 删除对象(自动删除子对象)
- `lv_async_call(cb, user_data)` - 异步调用(线程安全)

## 构建信息
- 目标平台: BES2600 Watch (ARM Cortex-M33, ~4MB Flash, ~512KB RAM)
- 编译工具: arm-none-eabi-gcc
- 构建命令: `./build.sh nuttx` (在 nuttx/ 目录下)

## 注意事项
- RAM非常有限(~512KB)，避免大内存分配
- LVGL对象创建可能因内存不足返回NULL，必须检查返回值
- 所有 `lv_obj_create`/`lv_image_create`/`lv_canvas_create` 等创建函数必须检查NULL
- `lv_obj_del` 会自动删除所有子对象，但不会将父对象的子指针置NULL
- WebSocket重连期间注意状态同步，避免竞态条件
