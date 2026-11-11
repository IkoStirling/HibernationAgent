# 导师连续会话

导师使用本机 Codex 的 ChatGPT 登录。应用维护一个持久的逻辑会话，导师在其中使用独立的加密记忆和工作区；小爱的记忆正文不会在管理界面展示。导师可以延续先前交流，不必为每次咨询重新建立一个互不相干的身份。

## 开发需求转交（0.6）

桌面或号主 QQ 私聊中明确要求转交开发需求时，小爱可调用 `development.request`。程序先保存有原始消息来源的加密需求，再通过内部 `mentor.forward` 进入上述同一个导师逻辑会话；模型不能指定外部会话、任意线程标识或进程。默认 `execute=false`，导师仅接收、整理和回应，后续明确委托执行仍走原来的 `mentor.execute`。

- `recorded`：只表示本地保留，尚不代表通知成功。
- `dispatching`：正在连接，尚未确认供应商接受。
- `accepted`／`delivered=true`：原生 `turn/start` 已返回实际线程和轮次回执；导师可能还在准备回答。
- `completed`：导师已经回应，可用 `development.status` 查询摘要；不是代码已经修改或部署。
- `failed`／`pending`：保留失败原因或待转需求；`delivered` 仍单独反映是否曾被接受。
- `uncertain`：进程重启前已经派发，但结果不能确认，不自动重发。

查询不调用模型。同源同内容按稳定编号去重；忙碌时保留本地需求，不绕过单会话互斥。明确的用户转交免除普通闲聊冷却，仍计入每小时请求上限。应用启动不自动发送历史待办。开发需求的异步转交当前使用工作通道；工作额度不足会保留失败回执，不在后台自动重发到网页。原有 `mentor.ask`／`mentor.execute` 的网页回退维持原行为。0.6 新增加密转交回执类型；旧版工作进程不识别它，产生新回执后不能仅换回旧工作进程来读取同一目录。

管理页进入“可选插件”，展开“导师”，即可看到“导师的连续会话”区域：

- 会话服务是否就绪，以及当前是否正在咨询。
- 是否已有可续接的原生上下文和当前代次。
- 本地会话记录数、独立记忆数量、加密确认状态。
- 只读的导师工作区路径和实际权限策略。
- 当前模型路由、最近实际选择的档位/模型/推理强度，以及服务端报告的工作额度。
- 实验性普通 Chat 网页桥接的连接、登录和就绪状态。

缺失的计数显示“未报告”，不当作零。页面不展示记忆正文、原始对话、内部思维、进程输出、工具回执、凭据或原生会话标识。工作区是导师实际使用的独立目录，页面只显示路径，不列出或读取文件。

## 逻辑会话与原生上下文

逻辑会话保存连续性和持久档案；原生上下文是当前 Codex 咨询所续接的上下文。二者分别管理。

“取消当前咨询”请求停止正在处理的咨询，保留之前保存的记忆与工作区。应用忙碌、暂停或离线时仍可发出这一管理请求，最终状态以后台回报为准。

“重建原生上下文，保留记忆”用于当前原生上下文不适合继续的情况。它只更换后续咨询使用的原生上下文，保留逻辑会话档案、导师独立记忆和工作区；它不代表删除所有供应商侧或 Codex 侧的历史副本。正在咨询时需先取消，后台会对竞争中的重建请求返回 `mentor_busy`。

导师直接继承它自己的 Codex 用户权限配置。具体可用能力取决于该配置和当前授权，管理页不会通过一个状态标签承诺未实际验证的系统权限。页面本身不启动咨询，也不修改权限策略。

## 接口与独立挂载

`GET /api/mentor/session` 返回只读元数据，预期字段如下，所有字段缺失时均有界面兜底：

```json
{
  "session_id": "logical-session-id",
  "provider_thread_id": "native-context-id",
  "generation": 1,
  "record_count": 12,
  "memory_count": 3,
  "workspace": "E:/Aliyat/mentor/workspace",
  "busy": false,
  "last_issue": "",
  "permission_policy": "codex_user_configuration",
  "encrypted": true,
  "service_running": true
}
```

`POST /api/mentor/session/control` 接受 `{"action":"cancel"}`、`{"action":"rebuild"}` 或 `{"action":"chat_open"}`。前两项由后台直接调用 `mentor.session.cancel` / `mentor.session.rebuild`；第三项只请求打开专用普通 Chat 窗口，供用户手动登录。它们不通过模型作业队列，不发起付费模型咨询。界面接受直接返回值或标准 `{ok,result,error}` 工具封装，成功后重新读取元数据。鉴权沿用管理页的本机管理凭据。

`ui/mentor-session.js` 从导师插件卡片自行挂载，无需替换 `app.js` 的渲染函数。插件卡片重建时面板重新附着；打开插件页时每三秒读取元数据，只更新面板内部的状态节点，不替换配置输入、不调用通用工具试用作业。页面隐藏或离开插件页后暂停定时读取。取消和重建具有独立的在途互斥，避免重复点击。

初次挂载、刷新、取消和重建均可使用合成响应进行浏览器验证，不需要真实 ChatGPT 登录或咨询。应检查：缺失元数据、计数、权限和加密提示；暂停/离线时取消；忙碌时重建拒绝；重建后记忆与路径保留；轮询时未保存的表单输入和焦点保持；原始记忆哨兵和会话标识不出现在页面；错误码不回显后台大段输出。

## 存储、恢复与配置

插件版本 0.5.0 使用 Python 标准库工作进程和本机 Codex App Server，不安装额外 Python 包。配置 `plugins.mentor.pythonExecutable` 为 Python 的绝对路径；留空时先查找本项目已有的 `out/deps/style-lora-venv/Scripts/python.exe`，再查找系统 Python。WindowsApps 的未安装商店占位程序不作为可用 Python。Codex 更新后若旧路径失效，需要更新 `codexExecutable`。

独立目录为 `dataRoot/mentors/default/`，其中 `state.dpapi` 保存逻辑索引，`records/<id>.dpapi` 保存交谈原文、导师笔记、执行元数据与中断记录。文件使用当前 Windows 用户的 DPAPI 加密。原始记录不因上下文重建而删除；导师通过 `mentor_memory.search/write` 检索或新增带真实来源引用的笔记。工作区 `workspace/` 保存导师实际工作产物，可以是明文，指南文件不含记忆正文。

同一工作进程复用一个 `ephemeral` 原生线程，避免依赖 Codex 明文会话档案。进程重启会建立新的原生线程，逻辑会话 ID 保持，原始记录仍在；恢复时有界装配近期原文和笔记，较早内容可按需检索。每次实际创建原生线程才增加代次。应用不保存内部思维链和原生工具标准输出。

当前已处理取消、超时、失败及中断后不重放任务。失败前已返回的操作回执保留，无法确认的操作不声称没有发生。原生审批请求会返回 `approval_required` 并中断本次任务，当前界面还没有原生审批交互；应用不会自动批准。用户可按自己的 Codex 权限配置决定可执行范围，应用不强制更改沙箱或审批策略。

导师记录也纳入可迁移加密备份包；迁移会重新绑定目标 Windows 用户，原始备份保持原样。需要备份与导出包来自同一暂停后的检查点，内容不一致或超过当前包容量时明确拒绝，不能通过单独复制 DPAPI 文件来跨账户恢复。

## 固定模型与三档路由

初始默认使用 `routingMode: "fixed"`、`model: "gpt-6.1-sol"`、`reasoningEffort: "high"`。固定模式每次使用该固定模型和强度，保留用户要求的 High 默认值。推理强度可选 `low`、`medium`、`high`、`xhigh`，并由实际账号与模型支持情况决定能否使用。

设置 `routingMode: "character"` 后，小爱可以在调用参数 `level` 中为本次请求选择档位：

| level | 用途 | 模型配置 | 初始强度 |
| --- | --- | --- | --- |
| `casual` | 闲聊与轻量交流 | `casualModel: "gpt-6.1-sol"` | `casualEffort: "low"` |
| `normal` | 普通问题与一般讨论 | `normalModel: "gpt-6.1-sol"` | `normalEffort: "medium"` |
| `complex` | 复杂问题或需要更充分推理 | `complexModel: "gpt-6.1-sol"` | `complexEffort: "high"` |

省略 `level` 时按 `complex` 处理，使用复杂档初始 High 强度。三档模型与强度分别可配置；固定模式下不会按 `level` 改档。配置通过现有“保存插件设置”流程生效，不在切换控件时自动发起咨询。

状态响应新增 `routing_mode`、`last_selected_level`、`last_selected_model`、`last_selected_effort`。界面展示后台报告的最近实际选择，不用配置默认值填充尚未发生的请求。

## 工作额度与实验性网页回退

工作额度只读状态为：

```json
{
  "work_quota": {
    "known": true,
    "exhausted": false,
    "checked_ms": 1793180000000,
    "source": "account_rate_limits",
    "buckets": [{
      "limit_id": "codex",
      "primary": {"used_percent": 25, "reset_at": 1793200000, "window_minutes": 300}
    }]
  },
  "web_fallback": {
    "enabled": true,
    "ready": false,
    "login_required": true,
    "last_issue": ""
  }
}
```

`known: false` 表示尚未报告额度，不能当作已用零、剩余零或已耗尽。界面只展示有限的主/次窗口用量与重置时间；`reset_at` 是 Unix 秒，`checked_ms` 是毫秒。缺失的窗口百分比、重置时间或实际选择字段继续显示“未报告”。页面刷新不生成模型请求。界面不显示额度源中的账号信息，也不显示网页会话 URL 或标识。

`quotaFallback` 默认为 `"chat_web"`，也可设置 `"pause"`，在额度不足时暂停导师请求、等待工作额度恢复。`chat_web` 是实验性普通 Chat 网页桥接：先点击“打开普通 Chat 并登录”，再由用户在专用窗口手动完成登录。页面不代填凭据、不代为登录，不提供随意发送提示词的输入框。窗口打开不等于桥接就绪，最终以 `web_fallback.ready` 为准；缺失状态按未连接显示。

网页回退仅交流与建议，不执行导师原生操作。实际回退由插件根据额度与桥接状态协调；尚未登录、未就绪或失败时如实返回状态。三档低/中/高推理仍属于 Codex 工作通道，降低 effort 不代表已经进入普通 Chat，也不代表拥有无限或免费的聊天额度。

网页桥接会把本次必要的记忆片段和问题作为普通 Chat 的上下文发送，无法赋予网页直接读取本地文件的权限。相关内容会成为你账号中的 ChatGPT 网页会话内容；本地文件加密并不隐藏这份远端会话。自动回退时保持桥接窗口隐藏，用户主动点击登录按钮才显示专用窗口。登录必须由用户完成，页面改版、外部 SSO 跳转或无法确认 Chat 模式时可能需要人工处理，程序不会绕过验证。

0.5 默认等待上限为120秒，可在5到300秒之间调整；高强度或网页回答仍可随时取消。已有显式超时配置保持到用户修改。

登录窗口允许 OpenAI 及常见 Google、Microsoft、Apple 身份页面的精确域名跳转；发送动作仍只在 chatgpt.com 上进行。验证码、SSO 或服务方的浏览器限制仍需由用户正常完成，不绕过登录限制。

## Durable contact and public recall — 2026-11-05

The optional `displayName` plugin setting persists the mentor's known contact
name independently of the current model context. It does not rewrite the role
persona, change permissions or replace either side's memory. On this machine,
the recovered name originates from the mentor's own archived reply, rather than
being invented or hardcoded in the role prompt.

`mentor.history(query,limit)` reads earlier public mentor replies from its
existing encrypted session records without a model request. It never returns
private notes, caller-context payloads, native tool stdout or action logs.
Revisiting advice creates an ordinary role tool observation if the character
chooses to remember it. The worker's independent original records and session ID
remain intact; shared-owner QQ and desktop can recall the same mentor exchanges.
Other QQ callers do not gain access to this owner-only history.
