# 项目开发规则

## CodeGraph 代码图谱

当前 `xiaozhi_voice/` 目录下已生成 CodeGraph 代码图谱（`.codegraph/` 目录）。

- **修改前阅读图谱**：每次涉及 `xiaozhi_voice/` 模块的开发工作前，先使用 `codegraph_explore` 或 `codegraph explore "<查询>"` 阅读代码图谱，理解调用关系和数据流，避免盲改。
- **修改后同步图谱**：每次对 `xiaozhi_voice/` 下的代码进行较多修改后，执行 `codegraph sync` 更新代码图谱，确保图谱与实际代码保持一致。

## 语言

思考和输出回答请使用**中文**。

## 日志记录

工作产生的 log 和记录请写入到 `/home/ts/code/code11/contest2026_392_dachuangwanlian/logs/liyc0623/` 目录下，格式参考：

```
/home/ts/code/code11/contest2026_392_dachuangwanlian/logs/liyc0623/2026-01-01/claude-code__example.jsonl
```

每条记录为一行 JSON，schema 如下：

```json
{
  "schema_version": "1.0",
  "session_id": "<会话ID>",
  "team_id": "contest2026_000_openvela",
  "github_login": "liyc0623",
  "tool": "claude-code",
  "seq": 0,
  "ts": "ISO8601时间戳",
  "role": "user|assistant|tool",
  "model": "模型名（assistant时填写）",
  "tokens_in": 0,
  "tokens_out": 0,
  "text": "消息内容",
  "tool_name": "工具名（tool时填写）",
  "tool_call_id": "调用ID（tool时填写）",
  "input": {},
  "output": {},
  "files_touched": []
}
```

日期子目录格式为 `YYYY-MM-DD`，例如 `2026-09-08/`。

## LVGL v8 开发注意事项

- LVGL 使用 TLSF 堆分配器，`lv_obj_del()` 会递归释放所有子对象，在嵌入式系统上可能导致栈溢出或堆损坏。
- `lv_refr_now(NULL)` 会触发完整的渲染周期，如果堆已损坏会导致 MMFAR=0x15c 崩溃。
- 修改 LVGL 对象树时注意线程安全，避免在渲染回调中直接删除对象。
