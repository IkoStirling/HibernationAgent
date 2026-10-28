# 模型输入缓存复用

本次保持角色、独立审核、工具验证、记忆来源与可见性、后台规划和行为队列的分工。
不保存模型回答作为命中缓存的替代品，也不延长或扩大角色可访问的档案。

## 请求布局

- 固定角色规则、人设和执行协议继续放在 system。连续状态、现场能力与插件快照
  移到本轮 user 事件的 `authoritative_state`、`current_capabilities`、`body`。
- 事件 JSON 的字段和类型保持原契约，序列化时将授权历史放到时间、事件 ID 与
  现场快照之前。历史仍受现有选择、预算、摘要和可见性规则约束；不为提高命中率
  保留已经撤销的来源或伪造旧时间。
- 审核输入先放本轮依据，再放变化的回执和候选；后台规划先放记忆与经历，再放
  实时状态。没有删除独立审核，没有将资料升级为 system 指令。
- 同一轮各次调用保持相同工具定义与输出 Schema。结束阶段用现有指令要求
  reply/silence；程序在审核和执行前阻止超限 call。可见工具并不授予额外执行次数。
  权限、模型/Schema 或插件配置变更仍然可以使前缀变化。

这提升稳定前缀的复用机会，但不是完整追加式聊天重构。历史滚动裁剪、相关记忆
变化、图像替换和必要的回执压缩仍可能打断后部前缀。DeepSeek 的落盘与命中是
服务端尽力而为，不承诺具体百分比，不为预热缓存而发出付费请求。

## 用量与检查

AYInference 0.3.1 将 Chat/Responses 的缓存 token 细项及 DeepSeek hit/miss 规范化为
可选的 `cachedInputTokens` / `uncachedInputTokens`。缺失保持未知，明确的 0 保留；
只在输入总量已知时推导缺失补数，矛盾数据不作为命中统计。

加密 trace 的 `turns[].usage` 和 `reviews[].usage` 记录输入、输出、缓存命中与未命中。
后台生成档案增加 actor/review 的 profile 与 usage，保留旧统计字段。

授权本地接口 `GET /api/model-cache`、dashboard 的 `model_cache` 返回本次升级以来
按连接和用途累积的统计。QQ 通过原生插件事件汇入桌面统计；只含用量元数据，
不复制对白、记忆、图片、密钥或 provider 原始响应。

- `calls` 是程序调用尝试数，不等同于供应商最终计费笔数。
- `usage_reported_calls` 表示供应商提供了输入/输出用量的调用。
- `cache_reported_calls` 只统计 hit/miss 可确定的调用；没有报告的调用不会伪装成 0 命中。
- `cache_hit_ratio` 的分母仅为已报告缓存细项的输入，不包含输出。无分母时为 null。
- 统计不是历史账单回填，也不是费用计算器。供应商账单和后续真实 usage 才能确认
  实际命中率；本地回归中的命中数字仅为模拟数据。

验证入口：`AYInference_Tests`、`Hibernation_UnitTests`、`Hibernation_CachePipeline`，
以及现有上下文、峰谷策略、原生工具、双队列、独立审核和 NapCat 回归。
