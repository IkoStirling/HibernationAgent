# 后台每日额度与运行诊断

`backgroundBudget` 独立限制后台模型调用，默认如下；管理页保存后即时生效。

```json
"backgroundBudget": {
  "enabled": true,
  "maxCallsPerDay": 120,
  "maxInputTokensPerDay": 500000,
  "autoEconomy": false
}
```

每日调用范围为 1–10000，输入用量范围为 1000–100000000。关闭 `enabled` 只关闭这层每日限制，仍记录已用量，重新开启不会清空。原有应用每小时总额度及前台预留继续生效。用户消息与 QQ 交谈属于前台，不消耗独立后台额度。

后台范围包括活动规划、互动反应缓存、独处、外层事件、故事结束与任务完成，包含它们产生的角色、审核、格式修复及后续工具轮次。每次尝试先持久化预留一次调用；调用失败或取消不退还，重启不清零。后台每日和公共每小时预留在同一次存储事务中提交，不会出现只记了其中一个额度的情况。

日期固定采用 **UTC+08:00（Asia/Shanghai）**，在北京时间零点更新。保存日标记及最高已见时间；时间回退、重启或来回改到已经用过的日期都不会重新领取当天额度。系统没有独立可信时钟，若系统时间被人为跳到从未到过的未来日期，仍以该日期计算；界面展示实际采用日期及 `clock_reversed`，不宣称能够识别所有系统时间篡改。

从0.9.2开始，输入上限只累计供应商**实际报告的未命中缓存输入**。总输入和缓存命中仍分别保存，`input_tokens`是计入预算的数值。仅报告总输入或缓存拆分互相矛盾时，该部分保守计入并标为`unknown_cache_input_tokens`；有总输入及一个可靠缓存分量时可准确相减，不能把没有报告的数据当成零。调用次数仍逐次记录，防止缓存命中被当作无限调用许可。已观察未命中量达到上限后阻止下一次后台调用；单次或已经在途的并发调用仍可能越过观察上限。跨午夜输入归收到报告时的日期，调用次数归预留日期。

预算存档升级到v2。旧v1计数不会清零：程序先从预算版本历史确定同一日期／人工重置后的统计窗口，再核对该窗口的后台用途缓存计数。只有总输入变化与旧预算完全吻合、缓存计数可信且未饱和时才重分类命中量；否则旧量保守标为缓存拆分未知。迁移回执保存在加密`ha.maintenance`，保留旧总输入、原调用次数与用途快照；原始版本不改写。`accounting_migration`明确标为迁移时刻的快照，不冒充之后的实时用途分布。

`autoEconomy`默认关闭以兼容旧配置，本次按用户要求在正式配置中开启。它只读取Windows当前会话的系统输入空闲时长，不读取按键、窗口文字或屏幕。5分钟无输入后，新活动规划至少间隔10分钟；30分钟后至少30分钟。每批最多2／1项，减少记忆与证据片段，只提出轻量本地活动；无人拖动时不新增反应缓存调用。普通周期独处同样降频，已安排的计划、持续任务和明确约定的动作结果接续不受该降频限制。

整机空闲时，普通周期独处的可选历史装配目标降到12KB；短动作后续为16KB。当前问题和本批真实结果等必要来源仍优先完整保留，超过目标时按必要字节扩展，不伪造或删掉原档。普通前台对话及用户明确安排的完整任务保持原上下文策略。缓存稳定前缀和来源权限验证继续使用原流程。

节能时优先使用现有本地活动及磁盘缓存，仍保持角色状态、动作和已安排事件。可选预取还会保护返场余量及一轮审核的观察前余量，达到保护阈值后不继续消耗剩余预算填缓存；明确任务仍受原来的完整日预算和每小时额度检查。它是软性的申请前保护，不能预测单次供应商实际用量，不能保证任何规模的后续任务一定有足够额度。检测到新的整机输入后恢复正常节奏，不将键鼠活动伪装成与角色交谈或情绪刺激。管理页可关闭节能，原始计数保留。

`GET /api/background-budget` 及 dashboard 的 `background_budget` 提供日期、时区、重置时间、已用/剩余调用、实际输入量、已报告和未知请求数、限制及阻止原因。计数位于现有加密存档，不额外生成明文对话或记忆文件。`settings.backgroundBudget` 即使旧配置未写此项，也提供有效默认值。

dashboard 的 `runtime_status` 区分离线、人工暂停、存档操作、模型处理中、准备活动/互动缓存、等待语音、正在朗读、活动执行/排队、等待每日额度和最近调用失败。它仅依据进程、队列、播放回执与调用结果，不把没有活动解释为情绪或自主拒绝。`last_turn_silent` 仅说明最近完成交谈的明确 silence 输出，不等于当前角色意图；内部记忆、草稿、推理及审核内容不进入诊断卡片。

验证入口：`tests/BackgroundBudget.cpp` 覆盖配置、预留、未知用量、输入上限、午夜、回退及重启序列化；`tests/BackgroundBudget.ps1` 使用本地供应商进程，检查真正停止请求、前台保留、即时配置、持久化与诊断。测试不访问真实模型、QQ 或生产存档。

## Explicit continuous goals — 2026-11-05

Daily autonomous call/uncached-input budgets govern unsolicited activity. A valid
user-accepted `current_task` is charged to the global hourly budget and its own
existing step/deadline limits, not the ambient daily bucket. Actor and reviewer
usage remains recorded under continuous_task/continuous_task_review; unrelated
autonomy retains the same configured daily limits. No counters are erased.

Budget exhaustion is a deferral, not permanent failure: hourly/daily codes wait
for the correct reset and do not consume the task's failure allowance. Previously
blocked budget-only tasks recover with the same step identity after source,
workspace, authority and deadline checks. Paused/cancelled/expired or inaccessible
tasks do not get resumed. JSON/native process fixtures exhaust the ambient daily
budget before completing real multi-slice work.
