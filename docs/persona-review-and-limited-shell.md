# 人设优先审核与受限命令行

## 回复恢复

`review.roleOnly` 默认为 false，正式实例本次开启。开启后，`meta_leak`、`persona`、`continuity` 仍可驳回、改写或重建；`unsupported_claim`、`tool_intent`、`other` 的非通过意见仅作建议，追踪同时保留原始 verdict、effective_decision 和 advisory_only。审核提示词明确普通闲聊、主观感受和拒绝无需事实证明。程序自己的工具目录、Schema、来源、工作区和路由权限保持执行。

本模式不请求审核附带的 focus_update、continuity_updates，已有记录不删除；角色仍可通过既有来源受控的工具主动留下约定。人设相关 deny 优先转成有上限的改写请求，沿用 maxRewrites，耗尽仍阻断，原判定和有效判定均可追踪。不是把审核失败当通过：审核超时、传输失败、429、5xx 在本轮总时限和预算内重试同一候选，最多沿用 reviewAttempts（1–2）次。401、402、权限撤销不因此重试。重试不重放角色或已完成操作。

正式配置 `contextControl.protocolRepairs=2`：格式错误可修复两次，仍受轮数、总时限、调用预算限制。QQ 原先 reviewReplies=false，维持不额外启动审核连接；它也可使用相同有界格式修复。最终确实失败时发送简短真实提示，不再使用“暂未取得回复”标题，不伪造角色心情、经历或稍后会自动回复的承诺。余额不足仍沿用静默离线机制。

## 受限命令行

`computer.limitedShellEnabled` 默认为 false。开启后向角色公开 `shell.run {command}`，支持 `help`、`roots`、`pwd`、`list/ls/dir [path]`、`cd [path]`、`cat/type/read path`。这是应用内命令解释器，不启动 PowerShell、cmd、脚本或外部进程。

默认根为 drafts:/，另有 materials:/ 和管理员配置的 computer.readRoots；可用根内绝对路径、相对路径或虚拟根路径。路径含空格用引号。`cd /` 显示虚拟根目录；`cd` 回到 drafts:/。不开放整盘。列目录和读取通过引擎 AYIO Workspace 的规范路径、挂载和只读授权；文本上限 32KiB，拒绝二进制、非法 UTF-8、越界、网络路径、管道、重定向、环境变量展开及非白名单命令。只保存会话 cwd，不修改文件。

cwd 在 ha.shell 加密持久化，桌面与不同 QQ 会话独立，移除消息 ID 后识别稳定会话。QQ 沿用管理员的电脑控制授权，由主进程代理执行。审核改写时复用已完成回执，避免相对 cd 再走一次。

## 检查

- 单元：命令解析、越界/大小/二进制、审核分类、瞬时错误与凭据错误区别。
- LayeredReview：重试不重放操作、人设拒绝保持、其他建议不阻断、两次格式修复、命令读取与重启后 cwd。
- OwnerContinuity_native/json：QQ 跨消息及后台重启保存 cwd，桌面目录保持独立。
- NapcatTests：未通过格式的内容不得部分发出，简短失败提示不暴露内部错误。

所有自动验证使用本机模拟服务与合成资料，无正式记忆输入、无真实 QQ 发信或付费调用。实际语气和模型可用性仍需日常观察，重试不保证上游故障必能恢复。
