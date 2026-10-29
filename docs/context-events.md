# Runtime events and reviewer input snapshots

The existing **Keep continuous dialogue context** option controls character
session projection. An already sent message is immutable while that bounded
projection remains valid. New focus, expression and final-turn constraints are
sent as separate `runtime_event` user messages, rather than replacing the initial
event. Original encrypted archives remain unchanged. This is a model-facing
projection, not a replacement event bus or execution queue.

```json
{"runtime_event":{"version":1,"kind":"focus.changed","source_event_id":"evt-example","sequence":1,"changes":{"conversation_focus":{"questions":"natural"}}}}
```

The event id binds an update to its dialogue turn. Sequence numbers increase
within the turn; explicit null is a value. The program also maintains a complete
current view for policy review. This interface cannot replace the user input,
grant source access or change system instructions. A field whitelist rejects
such updates before changing the current view.

An update approved after the last character call follows the approved reply in
the next request. It is not retroactively inserted into the previous call. The
next full state comparison includes that approved update. Final-only constraints
expire with their turn. Dialogue resets rebuild from the complete current view.

## Complete current review input (default)

`contextControl.reviewLayout` defaults to `direct`. The reviewer receives the
complete current candidate, source evidence and authority without old review
candidates. `event_context` is a JSON object instead of a second JSON document
inside a string. Only exactly duplicated top-level fields are removed; their
complete current values remain outside that object. Unequal facts, source ids,
original evidence, operation receipts and explicit null values are preserved.
Stable capability declarations precede changing inputs; a plugin's current state
can be separated into `current_plugin_state` under its unchanged plugin id.
Duplicate ids or existing state fields retain their complete original form.
No reference dictionary or state-patch replay is required of the reviewer.

`legacy` retains the original complete request representation. The full/disabled
context-control preset also restores legacy review input. Visual review follows
the original complete image path. These options change request layout, not the
required reviewer response or approval rules.

## Optional reviewer snapshots

`reviewLayout: "snapshots"` is retained for explicit experiments and requires
continuous projection. Stable capability/evidence views have local ids and each
candidate gets a complete current snapshot. Only the current authorized context
confers source eligibility. The stream is bounded to the current turn and 64 KiB;
old reviewer verdicts and private reasoning are never retained. Capacity/reset
rebuilds the input. An individually oversized request is sent intact but not
retained. Fixed-format repair notices preserve the retry prefix without keeping
invalid model output.

Trials improved hit rates but showed variable output cost, including one
4096-token reply without usable JSON. This layout is not the default. Cross-turn
reviewer histories, context-only histories and view-only retention were not
adopted. They remain reproducible in the manual lab evidence.

For projected turns, source authority is rechecked after model/reviewer calls
and before each operation in a batch. Revocation stops remaining work while
preserving operations that actually completed. Reusing input never grants
permissions. Background generation also stops before further reviewer calls
when the source fence changes; normal cancellation is not a user-facing error.

## Background experiments

`contextControl.backgroundProjection` is an advanced, **default false** option,
also gated by the continuous-context setting. It can retain bounded input-only
planning and reviewer views in memory. These views are bound to model/persona,
policy fences and every retained source revision; cancellation clears them.
Authority is checked during generation and before accepting its queue. Model
usage is recorded immediately, including a call followed by cancellation or
review failure, with ownership markers preventing duplicate host accounting.

The October 29 synthetic trials reduced missed input with this background
option, but their increased output cost erased the saving. It is not enabled in
the normal workflow. The dashboard does not recommend or enable it automatically.

## Validation and measurements

`Hibernation_ContextEvents`, `Hibernation_SessionProjection`, native-tool,
protocol, vision and focus tests use local fixture providers and make no paid
calls. New metrics/ledger tests check weighted token totals, missing or
inconsistent cache data, interrupted streams and shared budget limits. The
ordinary build test selection includes these context and accounting checks.

Live lab tools are explicit manual experiments, not startup tasks or CTest tests.
They use an authorized deadline and shared durable request/token ledger, public
persona data, synthetic experiences and separate empty encrypted archives.
Configured credentials are decrypted only into environment memory; no keys,
headers or private model reasoning are stored. Saved request bodies contain only
the synthetic lab material. The activity probe never executes its plans.

Experiments and actual provider usage are summarized by `tools/CacheEventReport.mjs`.
Reports separate cold requests from steady subsets, weigh hits by input tokens,
and include output tokens and estimated whole-task fees. Missing metrics remain
unknown. Estimated prices are not account debit measurements.

The first equal-call snapshot application sample showed a 19.23% estimated
saving, but a later equal-call sample was essentially flat (0.64% lower). Fixed
candidate repetitions exposed output variance and a truncated reply; the initial
saving must not be treated as a guarantee.

The selected direct layout reduced missed input by 41.13% and total input by
22.99% in a fixed eight-candidate comparison, with a 17.26% estimated fee saving.
A related direct-layout comparison saved about 5.6%. Six plugin-state scenes
verified sleeping/walking, video playback and item existence; all three false
claims were rejected with either layout. The final four-turn application sample
had 85.57% weighted input hits, but used 21 calls instead of the baseline's 15
because of additional queries, private intentions and legitimate reply rewrites.
Its total fee was higher. Input reuse is verified; total fee and the number of
operations still depend on character/reviewer choices. A 98% aggregate hit rate
is not promised. See [the work log](cache-event-worklog.md).


## 审核失败后的事实续接（2026-10-29）

审核未通过或审核连接失败时，程序将本轮保存为事实检查点，而不是删除整轮或把未通过的对白当作角色经历：

- 原始用户输入保持不变；仅保留已实际返回的程序调用/回执，区分成功、失败、排队和最终完成。被拒对白、未经准许的工具提案、格式修复文本及审核改写意见不进入续接上下文。
- 会话投影追加 `operation_receipt` 和 `turn_outcome` 程序事件，明确 `reply_delivered=false`。保持此前已保留的历史前缀及本轮初始输入字节不变；失败轮内部的原生/JSON工具过程整理为统一的事实事件，不保存未发布的助手话语或不完整的原生工具消息对。
- 检查点另存为加密原始档案 `kind=turn_outcome`，不等于新增 `ha.memory` 或人格/情绪更新。下轮在普通检索、缓存重建或关闭投影时仍可取回最新未答复轮次的用户输入与回执；该记录在角色和审核器两边优先保留。后续已有正式回复时不再把较早失败标为当前未答复轮。
- 未通过最终审核的 carry_forward、appraisal 和对白仍不提交；此前已经通过审批并执行的记忆、草稿、状态或外部操作保留。审核服务临时故障不会再触发整轮角色作业的自动重跑；同一请求的原有执行账本去重继续有效。新的明确用户要求仍可发起新操作，不把所有跨轮相同参数调用永久封禁。
- 续接仍受来源权限、版本、对话重建与窗口容量限制。来源撤回后检查点不能绕过隐藏策略重新带入旧内容；不安全的旧投影不复用，真实原始记录与操作账本仍保留。图片只保留事实元数据，不重放历史像素。

诊断位于 `trace.failure_continuation`：`record_id`、`stored`、`operations`、`projection_saved`、`reason`。模型当前事件的 `previous_turn_outcome_id` 仅引用本次已获准装配的最近失败记录，不授予新权限。无需模型返回新增字段，turn-v1/原生工具协议维持。

定向检查覆盖：文件写入后对白被拒、重启后续接、保持原前缀、排除拒绝对白与拒绝提案、下一轮不自动重写文件、审核器与角色依据一致、记忆写入后审核HTTP503、关闭投影仍可续接、来源撤回、JSON与原生工具两种流程。本轮不使用私人对话或付费模型调用。

本轮验证：226/226 单元用例、146770/146770 断言通过（使用较短的非系统盘 `AY_TEST_TMPDIR`）；标准深层 CTest 临时目录下已有导师存档恢复用例出现路径相关失败，同一用例及全套在短目录通过，未修改恢复实现。分层审核28项、会话投影24项及缓存/事件追加四组入口通过，另有协议、NapCat、JSON/原生长任务、新调度、前台优先、私人延续和离线九组回归通过。本轮付费模型调用0次。

部署完成：功能提交 `a7cf72f`。主程序与匹配 NapCat 后台已更新，配置、人设、阶段和密钥保持。63215 个部署前加密版本逐条核对无缺失或变化；完整备份位于 `E:/Aliyat/Archives/DeploymentBackups/failure-continuation-20261029-123736`。该行为对更新后的失败轮次生效，不自动重放以前失败的请求。
