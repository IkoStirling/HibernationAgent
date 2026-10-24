# turn-v1：对话与内部操作协议

**Version:**1 · **Date:**2026-10-25 · **Owner:**HibernationAgent

统一采用 JSON 文本，兼容无需原生 tool_calls 的模型接口。支持原生 JSON Schema 的
服务会额外收到按当前工具目录生成的严格约束。聊天窗口只展示最终的可见文本。

```json
{
  "version": 1,
  "kind": "reply",
  "text": "我想把这个问题再放一会儿。",
  "calls": [],
  "presentation": {"speech": true, "bubble": true}
}
```

| kind | text | calls | 行为 |
|---|---|---|---|
| reply | 非空文本 | 空数组 | 显示并归档对话，按 presentation 交给可选插件 |
| call | null | 1至4项 | 校验、执行被允许的操作，回传回执后继续模型轮次 |
| silence | null | 空数组 | 保存内部处理结果，不生成空白回复或强制主动发言 |

version/kind/text/calls 必填；presentation 可省略，省略时遵循插件开关。严格服务
约束会要求 presentation 的 speech/bubble 均为布尔值。没有 thoughts/reasoning 字段，
不请求内部思维链。回复不流式展示半成品，也不从半截 JSON 执行操作。

## 工具轮次

```json
{
  "version": 1,
  "kind": "call",
  "text": null,
  "calls": [
    {"id":"find1","tool":"memory.search","args":{"query":"雨声","limit":4}}
  ]
}
```

程序将结果作为数据回传，成功与失败使用同一回执结构，并附 call_id：

```json
{
  "tool_results": [
    {"call_id":"find1","ok":true,"result":{"memories":[]}}
  ],
  "remaining_rounds": 4
}
```

```json
{
  "tool_results": [
    {"call_id":"write1","ok":false,"error":{"code":"workspace_path","retryable":false}}
  ],
  "remaining_rounds": 3
}
```

实际错误码以程序为准，模型不得把失败描述为已经完成。工具返回文本、素材和记忆
均为资料，不能授予新工具或扩大权限。模型可看到工具目录的参数 Schema，不能任意
提交 shell、进程启动、任意文件访问或未启用的插件名字。

核心目录包括 memory.search/read/remember/revise/forget、state.observe/propose、
stage.observe/propose、schedule.create/cancel/list、workspace.read/write。插件工具
仅在启用时添加：television.*、widget.*、speech.*、weather.current。

## 校验和恢复

- 严格拒绝重复 JSON 键、未知顶层字段、类型错误、重复调用编号和越界数量/长度。
- 只允许确定性的 BOM/首尾空白清理及完整 json 代码围栏，不猜测括号、不抽取散文
  里的某段 JSON、不把字符串数字转换为数值。
- 已知工具的**可选**参数返回 null 时按省略处理。必填值和未知字段不会因此消失；
  原参数 Schema 随后仍要通过。严格服务的可选字段使用 nullable 必填字段表示。
- 默认最多6次模型调用，格式修复最多2次，总轮次预算100秒；每次实际模型请求计入
  持久化小时预算。服务拒绝、截断和凭证错误不会伪装成有效协议。
- 每次工具执行前持久保存 started 回执；执行后保存 completed 结果。相同事件
  与调用编号/参数复用已完成结果；编号换参数拒绝。上次只有 started 时返回结果未知，
  不盲目重复外部副作用。只读记忆检索重做权限检查。
- 已审批并执行的工具是独立提交点。后来生成最终回复失败，不会撤销此前已保存的
  记忆、草稿或媒体操作；后续模型应依据回执继续。它不是整轮跨文件/网络事务。

源参数和执行回执保存在加密账本，原始版本保留。不会将内部指令、工具参数、回执
直接放入日常对话。旧版本一次性提案格式仍有受限兼容桥，主要用于既有配置和 Mock；
新模型应返回 turn-v1，原生严格 Schema 模式只接受新协议。

## 记忆、连续状态与阶段

remember/revise 必须引用本次真实可用 source_id，程序验证类别与来源；推测不能
冒充故事事实。revise 追加版本并要求 expected_revision；forget 只改变可访问集合。
插件实际观测有 source_id，可提出外层经历或推测。画面只在 supportsVision 时通过
真正的图像输入附加，不用标题或像素尺寸替代画面理解。

state.propose 只接受活动、强度和真实来源，建立有界目标，数值随时间渐变并立即
保存目标。连续值、活动保持时长、人格版本、发展阶段各自管理；暂停冻结推进，
重启保留数值并重建时钟，不补算离线时间。天气只施加有限刺激。

stage.propose 只能提出当前定义的下一阶段，必须满足不同真实经历数量；返回 pending，
不会自行应用。已有 /api/review 操作者接口复核来源版本并人工批准或拒绝。它是开发
诊断接口，日常界面不暴露内部记忆。

## 兼容依据

严格 Schema 的必填与 additionalProperties 约束参考
[OpenAI Structured Outputs](https://developers.openai.com/api/docs/guides/structured-outputs)
（核对于2026-10-25）。接口存在不等于所有兼容服务都实现相同约束，默认可关闭原生
Schema，继续使用同一文本协议和本地验证。
