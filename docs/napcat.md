# NapCat QQ 插件

## 图片／表情素材库与分段消息（0.9.3）

开启 `mediaLibraryEnabled` 并将 `mediaLibraryRoot` 指向含 `catalog.json` 的独立目录。
起始库位于程序 `plugins/napcat/media`，原样复用小爱的站立与躺下 PNG；建议复制到
存档素材目录后扩充，避免更新程序覆盖自定义资源。每项以稳定 `id` 关联相对 `file`、
`name`、`meaning`、`tags`、`situations`、`avoid` 与 `sticker`。示例见起始库目录。
标签只供选择贴图，不改变角色心情、动作或事实；模型也可以不发。

`napcat.media_search` 只搜索文字释义，不读图、不联网；`napcat.media_send` 接受素材
ID，原样发送 PNG/JPEG/GIF/WebP 到当前会话，无需每次做图像分析。GIF 不抽帧重编码。
群聊对全群可见；不接受路径、URL或其他收件人。最多128项、单文件2MiB，拒绝目录
逃逸与链接；缺失、坏目录和非图片返回失败，不伪造发送成功。每次输入最多尝试发送
两张库内素材；相同内容的重复提案复用实际回执，未知送达结果不会自动重发。
发送图片不需要桌面控制权限，但仍须通过当前会话准入和正常审核，不能操作其他会话。
素材库只收录明确愿意分享的素材，本版不自动收集私聊收到的图片。

`splitParagraphs` 默认开启。模型返回 `messages` 时原样保留其消息边界；只返回 `text`
时，QQ按空行分隔的自然段投递，最多6条，沿用原有逐条回执与暂停／离线保护。
代码围栏中的空行不分条，过大的段落仍保留原单条兼容路径；单条长文本不为凑条数
强行截断。不额外调用模型、不逐条重新审核，桌面文字与语音不受这项QQ排版兜底影响。
连接失败使用中性“⌁ 暂未取得回复”状态提示，余额不足的已有离线／静默策略保持。

协议依据：[NapCat 官方图片转换](https://github.com/NapNeko/NapCatQQ/blob/main/packages/napcat-onebot/api/msg.ts)。
本地检查使用 FakeBot，不向真实QQ账号发测试消息；真实客户端的贴图显示仍需日常确认。

提供文本私聊、白名单群内 @ 对话。复用当前选中的角色模型、审核连接、结构化返回校验及全局模型调用预算；不另设一个 QQ 人格。默认不启用，不会自动登录 QQ、加好友、加群或发送测试消息。

## 启用

1. 使用包含本插件和通道支持的新后台版本。首次安装需要重启后台，让它发现新 DLL 和 manifest。之后可从管理页插件配置中启停；繁忙时先暂停后台再改配置。
2. 在 NapCat 登录专用机器人账号，记下机器人 QQ 号。`selfUserId` 填这个号码；`ownerUserId` 填号主本人的号码，二者不同。均使用数字字符串。号主身份只按事件的 QQ ID 判断，昵称和自称不起作用。
3. 在启动后台的环境中设置 `HIBERNATION_NAPCAT_API_TOKEN`、`HIBERNATION_NAPCAT_EVENT_SECRET`，每个至少 16 个字符。环境变量值是实际秘密；配置文件只保存变量名。不要将值写进配置、命令行或日志。修改系统环境变量后，需要让后台从新环境启动。
4. 在 NapCat 配置 **HTTP Server**，例如 `127.0.0.1:3000`，启用并设置 token，与 API token 环境变量的值相同。
5. 在 NapCat 配置 **HTTP Client** 事件上报，URL 为 `http://127.0.0.1:18890/onebot/events`；设置其 token，与 event secret 环境变量值相同；消息格式使用 **array**。插件检查 HTTP Client 的 `X-Signature: sha1=...`，这是 NapCat 的实际签名机制，不是 Bearer 回调。
6. 将 [example.json](../plugins/napcat/example.json) 的 `napcat` 对象合并到应用配置的 `plugins` 内，填写两个 ID、白名单并设 `enabled: true`；也可以用管理页的插件表单填写。不要覆盖其他插件配置。初次只允许号主私聊，群白名单为空。
7. 给机器人发一条私聊。管理页插件状态或 `napcat.status` 可查看运行、暂停、队列、发送/失败/不确定数量和错误码，不展示 QQ 聊天内容或凭据。登录账号与 `selfUserId` 不符时不会调用角色或发出回复。

HTTP 回调仅监听 IPv4 本机回环地址。不使用桌面 dashboard 的会话口令。默认只接受有效签名、匹配机器人身份的普通 message 事件；忽略机器人自己的消息、通知和不在允许范围内的事件。HTTP API 支持本机 HTTP 或显式配置的 HTTPS；不自动设置代理。

## 启用失败提示

插件会指出具体缺少的字段或凭据。QQ 号须为纯数字，机器人 QQ 与号主 QQ 不同。
`HIBERNATION_NAPCAT_API_TOKEN` 是 NapCat **HTTP Server** 的 token；
`HIBERNATION_NAPCAT_EVENT_SECRET` 是 **HTTP Client** 事件上报的 token，两者都至少 16 个字符。
在 Windows 用户环境变量中设置实际值后，重新启动后台让它取得新环境；管理页配置保存的是变量名，不能代替变量的值。
端口被占用时更换 callbackPort，同时修改 NapCat 上报 URL 中的端口。保存失败不应停止已有的正常连接。

## 身份、会话与目录

目录从应用 `dataRoot` 派生，所以当前应用使用 E 盘时 QQ 档案也在 E 盘：

```text
<dataRoot>/channels/napcat/<机器人QQ号>/
  transport/
    archive/records.sqlite             # 加密收件箱、发件箱、回执、去重状态
    runtime/memory-key.dpapi
  private/<对方QQ号>/
    archive/records.sqlite             # 该私聊原始对话、记忆、来源、修订、审核记录
    runtime/memory-key.dpapi
  groups/<群号>/members/<发言人QQ号>/
    archive/records.sqlite             # 该群内此人的独立对话与记忆
    runtime/memory-key.dpapi
```

QQ 与桌面档案**分目录保存，默认记忆互通**。各私聊、群和群成员仍拥有自己的加密数据库；检索和有限上下文装配会联查桌面与 QQ 的获准记录，不复制或搬迁原始档案。来源 ID 带存储命名空间，保留原始版本、发言人和渠道身份；更换渠道继续使用相同角色、模型、审核连接、发展阶段和主后台连续状态。隐藏的记忆及其原始来源不能通过跨渠道搜索绕过。跨库读取原文和记忆是只读的；新记忆与修订写在当前会话数据库，外库记录需要在来源侧管理访问权限。资料压缩仍由现有记忆管家承担。现有记录不迁移。

每条原始输入保留不可由模型改写的 channel_identity，包含号主/发言人/机器人/群 ID、消息 ID、是否号主及有效权限。群内成员使用独立存储不代表默认不共享内容；群聊最终回复对群成员可见。引用消息仍不额外调用 QQ 去追溯原消息。跨设备备份需要保存整个目录和 DPAPI 密钥文件；密钥受当前 Windows 用户保护。

## 记忆互通、权限与审核

管理页的 QQ 插件提供以下设置，修改后保存即可生效，无需重启后台：

| 配置项 | 默认 | 作用 |
| --- | --- | --- |
| `stricterSecurity`（更严密的安全性） | `false` | 总开关：强制隔离记忆、关闭 QQ 桌面操作、强制审核。保留各项原选择，关闭总开关后恢复。 |
| `isolateMemory` | `false` | 开启后，各 QQ 会话只能检索自己的档案，桌面也不联查 QQ；物理存放方式始终不变。 |
| `allowDesktopControl` | `true` | 允许通过主后台调用桌面当前开放的工具与插件。 |
| `controlTrust` | `owner` | `owner` 仅号主；`allowed` 所有已获准聊天的人（包括白名单群内成员）；`none` 无人可操作。身份只取数字 ID。 |
| `reviewReplies` | `true` | 审核 QQ 的工具提案与最终回复；使用主审核连接，未配置独立审核连接时复用当前模型审核。 |

默认号主可以从 QQ 操作小电视、小组件、工作区、已有程序启动工具、获准的目录与屏幕观察，以及提出连续状态意图和安排任务。QQ 不另起桌面插件实例：实际操作经主后台 WorkerHost 串行执行，复用工作区权限、程序白名单、截图开关和插件启用状态。它仍不能执行任意 shell 命令、启用被关闭的插件、读取内部密钥或绕过原始档案保护。普通已允许聊天的人默认可以与同一角色交谈和使用互通记忆，但不能操作电脑；可单独扩大可信范围。

QQ 处理期间与桌面模型任务互斥，避免同时操作角色。每次工具执行保留稳定操作身份和真实回执；输入重试不重复执行已完成操作，崩溃后结果未知的操作不自动重做。默认回复只回原 QQ 会话，不自动朗读到桌面；桌面动作和主角色状态更新则立即在那里执行。旧版 proposals 中的 actions/state_update/stage_proposal 仍被拒绝，操作统一通过工具调用，权限在实际分发时再检查。

切换隔离不会撤回已经发送或当前会话中已产生的内容。开启严格模式不会删除已经产生的有来源记忆；它会停止跨库检索，并阻止引用已不可访问外库来源的派生记忆进入上下文。QQ 号与群白名单、事件签名、重复消息处理、预算、路径校验不受总开关关闭影响。

回复只取当前请求 ID 对应、经过校验与已配置审核的最终文本，发回固定原会话。群聊可 @ 原发言者；所有正文使用 OneBot text 消息段，模型输出中的 CQ 代码不会变成 QQ 操作。允许模型沉默，此时只结束任务，不发送占位回复。图片、音视频、文件段不下载、不转交给视觉模型，只标记未读取；首版不提供 QQ 主动群发、语音或文件发送工具。

## 截图发送

支持由小爱选取局部：先 `computer.screenshot {target:"screen"}` 看全图，再以完整
目标的归一化 `region:{x,y,width,height}` 抓局部，最后发送第二次回执的 source_id。
选区需要看图模型；单纯转发已有截图不需要。仍沿用屏幕权限与每轮两次截图上限。

## 接收图片、表情包与连续消息（0.4.0）

0.4.1 修正 QQ NT 图片地址校验：明确允许 `multimedia.nt.qq.com.cn`，仍拒绝伪造的
相似域名。此前该真实域名在下载前被拒绝为 qq_image_url_unavailable，尚未进入 GIF
解码。回环测试使用真实两帧 GIF 字节配 `.jpg` 文件名，验证按内容解码首帧、识别帧数、
传给角色和审核模型并加密缓存。私有图片诊断工具 Hibernation_NapcatImageProbe 从 stdin
接收 URL，仅输出下载/解码尺寸和帧数，不输出 URL 或像素、不调用模型；用于验证实际
AYNetwork/WIC 路径。QQ 的小组件截图也可直接 target=widget，由主后台计算范围。

每条获准消息先保存在加密收件箱；`cooldownSeconds` 只限制模型处理频率，不再把
间隔内的新文字/图片直接以 HTTP 429 拒收。`mergeWindowMs` 默认 1500，范围 0–5000，
0 表示关闭合并。同一私聊或群、同一发言人在窗口内的消息合为一次输入，最多 8 条、
12000 UTF-8 字节；原消息、去重身份和顺序仍分别保留。不同人/群不合并，已经开始的
请求不会被后续消息覆盖；后续消息继续排队。群聊的 @ 要求保持原配置。

角色通过 `channel_identity.received_images` 获知同一发言人最近五分钟的附件索引，
可调用 `napcat.view_image {id:...}` 获取实际图片；图片后的独立文字也能引用该附件。
此工具与电脑操作权限分开，严密安全模式下也能读当前获准对话里的图片。索引不包含
下载网址或磁盘路径，不代表已经看见画面。实际图像复用应用的解码/尺寸限制与模型
视觉输入、审核图像传递、加密来源存档；成功读取后缓存，重启后仍可复用。

支持 OneBot `image` 和 `mface`（NapCat 商城表情也会以 image 上报）；从消息直链读取，
失败后通过 get_image 刷新链接。只请求已配置 NapCat 来源或 QQ 图片 CDN，远程图片
请求不携带 NapCat token、不跟随跳转、不按消息里的本地路径打开文件。图片最大 8 MiB，
解码后最大 8192 边长、1600 万像素，传给模型的图像最长边 960；动图暂读首帧。
内置 `face` 保留 QQ 表情 ID，并给 51 个常用表情附名称（来自 NapCat 的 face_config）；
未知编号保留原样。它与可查看实际像素的图片表情包不同。没有视觉能力或
图片读取失败会得到明确回执，角色不能声称看到了内容。

协议参考：[NapCat 消息格式](https://napneko.github.io/develop/msg) 与
[图片处理/链接刷新](https://napneko.github.io/develop/file)。

在 QQ 请求截图时，角色先使用 `computer.screenshot` 取得本次快照，再使用 `napcat.send_image`，参数是截图回执中的 `source_id`。图片从本次会话的加密观察记录读取，直接作为 OneBot image 消息段发送；不需要把模型返回的 base64 当作图片，也不需要模型支持视觉。接口不接受路径、URL、其他收件人或历史会话来源。发送范围始终固定为原 QQ 会话；群聊图片对全群可见。

整屏截图依然需要 Dashboard「允许观察整个屏幕」开关；未开启时只能截本程序窗口。QQ 工具授权不会自动改变此开关。截图发送保留加密回执，相同来源重复调用只复用回执；网络结果不确定或进程中断后不自动重发图片。不会为发送创建未加密的临时截图文件。

模型/审核失败会发出带 `【程序提示】` 的固定简短通知，区别于角色对白；主动选择 silence 时仍保持沉默。后台控制超时单独记录为 `channel_host_timeout`，不会再当作「0/600 额度已用完」。插件状态中的 `last_control` 显示最近控制消息的种类、耗时与接收/超时状态。等待后台控制回执上限从 5 秒改为 30 秒，暂停会提前取消等待。

协议依据：[NapCat 图片消息段](https://doc.napneko.icu/onebot/segment)、[NapCat 发送消息接口](https://napneko.github.io/onebot/api)。

## 队列与故障

- HTTP 回调先写入加密收件箱再返回 `{}`，不利用 OneBot 快速操作回复，网络线程不调用模型。队列满返回 503；同一会话发言人默认 5 秒模型处理冷却，冷却内新消息仍先排队保存。重复的同一条已接受消息可直接确认。
- 按机器人/会话/消息 ID 去重；同 ID 但内容或发言人不符时拒绝。默认只处理 30 分钟内的事件及队列消息，时间必须合理。最多保留 64 个待处理任务、256 个不同对话范围；原始历史不随队列删除。
- 一次处理一条 QQ 消息。桌面正在交谈或输入时，QQ 等待；收到共享暂停信号后停止模型和发送，消息仍留在加密队列。每次实际 actor/reviewer/摘要调用都使用主后台的小时预算。
- 未完成的入站请求用稳定请求 ID 恢复，已生成的回复优先使用发件箱，不再次生成。模型处理最多尝试 3 次，核心内部另有受预算约束的格式修复和有限重试。
- 明确的 HTTP 401/403/429 发送拒绝最多重试 3 次。超时、连接中断、5xx、异常回执及发送途中进程崩溃不能证明是否已送达，记为 unknown 并停止自动重发。可从独立加密 transport 档案追溯；首版没有让角色重发未知消息的工具。
- 错误码仅在本地管理状态可见。模型失败不会伪造角色回答或把内部错误直接发进 QQ。若持续失败，检查选中模型、审核连接、全局预算以及插件状态。

## 构建与验证

插件代码位于 `plugins/napcat`，应用侧提供共享记忆查询与桌面操作协调。保持其他原生插件 v2 ABI 不变；配置表单使用插件 manifest 声明，不改动角色动画或资产。

```powershell
./scripts/Build.ps1 -BuildRoot ./out/napcat -Action Configure -Configuration Debug
./scripts/Build.ps1 -BuildRoot ./out/napcat -Action Build -Configuration Debug -Targets Hibernation_NapcatTests
D:/Software/Cmake/bin/ctest.exe --test-dir ./out/napcat -R '^Hibernation_NapcatTests$' --output-on-failure
```

测试使用本机模拟 OneBot/API 和假模型，不登录真实 QQ、不调用付费模型。覆盖实际 DLL 加载与 HTTP 收发、签名、号主识别、白名单、去重、排队/暂停/重启、分目录密文、双向记忆检索、来源隐藏、号主桌面工具执行、非号主拒绝、安全总开关联动、旧格式绕过、沉默及共享调用预算。

协议参考：[NapCat HTTP Client 源码（签名与上报）](https://github.com/NapNeko/NapCatQQ/blob/main/packages/napcat-onebot/network/http-client.ts)、[NapCat OneBot API](https://napneko.github.io/onebot/api)。

## 直接截图发送与上下文控制（0.5.0）

当本次 QQ 会话可使用 `computer.screenshot` 时，同步提供 `napcat.capture_and_send`，参数与截图工具一致（包括程序提供的 widget 范围）。仅截图发送时可一轮提出组合操作，之后一轮结束对白；不先向模型发送图片。需要辨认或分析图像时仍使用原截图/观察工具，不能从发送成功推断角色看到了画面。

组合操作仍经过角色操作审核（如启用）、桌面截图权限、当前 QQ 控制权限、账号校验和固定会话路由。原始截图在该 QQ 会话目录加密保存，按已接受请求和参数标识复用；崩溃恢复或审核重写不重新截图/重发同一发送操作，未知交付继续停止自动重发。只支持当前会话，不接收任意收件人、文件路径或 URL。原 `napcat.send_image` 路径继续可用。

QQ 继承主后台 `contextControl`，不会另起一套记忆或状态框架。严密安全模式强制查询/静默审核；其他模式遵循当前审核开关。精简不影响物理分目录存储、共享检索开关、原始档案和程序权限。

## 手动离线

托盘菜单或 Dashboard「此刻」的「进入离线（QQ 和桌面）」即时生效，无需重启。离线独立于后台暂停并持久保存：阻止新模型调用、停止在途活动与语音，桌面保留已有对话并禁用发送；QQ 保持静默。尚未发送的 QQ 请求以及离线期间收到的消息保留在加密传输档案中，不在恢复在线后补回。已经发送至 QQ/模型服务端的请求无法撤回。NapCat 本身仍登录并接收回调，不修改 QQ 账号的在线标识。

余额不足（HTTP 402）不发送 QQ 程序提示；加密传输记录保存 failure_code 或 offline_reason，插件状态显示 insufficient_balance。桌面显示离线状态，Dashboard 保留余额诊断。现已增加默认启用的余额自动保护，参见下文。原有「暂停后台活动」仍按原先规则保留待处理 QQ 消息；恢复在线不会解除单独的手动暂停。

本机受凭据保护的 POST /api/control 接受布尔 offline、paused（至少一个）。模型工具不能改动该开关。GET /api/chat/status 与 /api/dashboard 提供 offline。

## 余额自动离线与恢复

默认开启自动保护。角色、审核、独处与缓存预生成的调用遇到 HTTP 402，或 DeepSeek 官方余额接口明确不可用时，QQ 与桌面统一自动离线。后台余额检查独立于已暂停的工作队列，每 60 秒执行一次 GET /user/balance，不调用聊天/生成接口，也不携带会话或记忆。直连 DeepSeek 的角色与启用的独立审核连接都必须恢复，才解除自动原因。网络、鉴权、返回格式错误，以及“可用但金额为零”等矛盾数据不会误判为充值成功。旧查询结果不能解除随后发生的 402。

手动离线与余额离线分别保存；余额恢复只解除自动原因，保留用户手动离线与后台暂停。Dashboard 显示原因并可立即检查余额（至少间隔 5 秒）。恢复后不补发离线期间的 QQ 消息。服务重启会重新查询，自动原因和原始档案继续保留。

目前自动余额查询仅适配官方 https://api.deepseek.com（及显式标记 balance-fixture 的本机测试服务）。其他服务的 402 仍会触发离线，但不猜测余额地址、不发送付费试探请求；管理页可手动重试，或切换连接。余额和 API 密钥不进入角色上下文，原始余额响应不写日志。官方接口：https://api-docs.deepseek.com/api/get-user-balance/ 。


## 同轮多条消息（0.6.0）

### 主动回复的去向（0.8.0）

程序保存每个计划或持续任务的来源渠道。QQ 创建的定时计划、号主动作接续和持续目标回复返回原私聊或原群聊；桌面创建的计划返回桌面。用户消息触发的事件型计划按实际触发会话投递。普通无绑定的独处主动发言使用最近一天内实际接入的聊天渠道，过期后使用桌面；绑定计划不会因为后来换地方聊天而改收件人。电视播放完成沿用开启播放的渠道。

后台接续同样遵守原渠道权限：群聊／非共享号主私聊仅使用该会话有界来源和有限的公开状态，不继承桌面历史、私人问题、图片或保留的会话前缀；角色与审核上下文均适用。工具仍取原渠道授权与本次任务／Skill权限的交集。共享号主私聊继续按配置延续同一角色记忆。QQ 主动回复也遵循 QQ 自己的 reviewReplies，不因为在主后台执行而套用桌面审核开关。手动暂停、离线或余额离线会取消尚未交给传输层的旧回复，恢复后不补发；尚未生成结果的计划仍依原调度规则恢复。

QQ 主动回复不会触发桌面语音、小组件气泡或进入桌面对话列表。生成后的完整消息数组先与原始回复一起提交到加密 `ha.outgoing`，再由程序专用投递接口送入 NapCat 的加密发送账本；模型不能提供或更换收件人。主账本的 queued 表示传输层已接管，不代表 QQ 已确认；最终发送游标和成功回执仍在 NapCat 账本。重启复用同一投递标识，防止重复投递；权限撤销、离线、暂停或无法确认的发送不会回落到桌面播报。后台尚未接管的结果最多保留一天，已接管结果沿用 QQ 消息时效和发送规则。

### 收藏收到的图片和表情

收藏始终是显式工具行为：收到附件、读取附件、回复对话本身都不会自动调用收藏，也不会自动生成素材条目。小爱可以判断值得保留时主动调用，用户也可以在对话中要求它收藏；不要求每张图片都收藏。

启用图片／表情素材库时，号主会话增加 `napcat.media_save`。参数为当前 `channel_identity.received_images` 中的 id、name、meaning，以及可选 tags、situations、avoid、sticker。只接受当前号主实际发送、程序已索引的附件；不接受任意网址、磁盘路径或收件人。可以依据已读图的结果或号主描述整理释义，不能将文件名当作画面事实。

原图通过已有 QQ 图片来源白名单获取，单图最多 2 MiB，支持 PNG/JPEG/GIF/WebP，GIF 不转为静态首帧。保存到 `dataRoot/channels/napcat/<机器人号>/media`，与不可变程序版本及人工维护素材库分开；相同内容按散列去重，最多 128 项。用途说明和标签保存在素材目录的 catalog.json，来源消息、发言人、附件标识及文件散列留在加密传输账本；素材文件本身是普通图片文件，不冒充加密记忆。

`media_search` 合并自己的收藏与原配置的人工素材；返回文字释义，不把图片发送给模型。`media_send` 按返回的稳定 ID 发送原图，继续使用同一输入的发送预算、当前会话约束和回执去重。QQ 内置 face 不是图片，无法直接收藏成图片素材；下载失败、文件过大、目录异常或达到数量上限均返回明确失败，不宣称已收藏。

### 私聊输入状态、对白容错与开发待办

默认启用 `showTypingStatus`：处理已接入的私聊时调用 NapCat 官方 `set_input_status`，文字输入事件使用官方示例的 event_type=1；较长处理每 12 秒最多刷新一次，群聊不调用。完成、取消、暂停或离线后不再刷新，状态由 QQ 原生时效消退；不猜测不存在的清除状态码。接口失败只影响状态提示，不阻止对话，插件诊断保留最后 HTTP 状态。可在插件设置中关闭。依据：[NapCat 官方实现](https://github.com/NapNeko/NapCatQQ/blob/main/packages/napcat-onebot/action/extends/SetInputStatus.ts)。

QQ 的独立回复审核仍由 `reviewReplies` 控制，严密安全模式仍强制审核。关闭审核时，纯对白可用已有有界兼容器包装成合法 reply，减少因模型直接返回自然语言而多调用一次修复；它不能把工具标记、JSON 片段或不合格工具参数当作对白放行，也不会据此执行操作。工具参数、收件人、来源和目录权限继续程序验证。

号主私聊或桌面对话中可通过 `development.request` 转交小爱程序的开发需求。title 和 details 绑定当前原始消息、渠道和调用记录，先保存到加密 ha.development，再送入小爱现有的导师逻辑会话。同一原始消息的同一提案去重，最多 128 项。目标由程序固定，模型不能指定外部会话或进程；不自动修改代码。`recorded` 仅本地保存，`delivered`／`developer_notified` 只有导师原生 turn/start 接受后才为 true；`completed` 仅导师已经回应，不代表需求已开发部署。`development.status` 可凭返回编号查询实际结果，不调用模型。忙碌或连接失败保留 pending 和原因，不伪造通知；重启后已派发但未完成的请求标为 uncertain，不盲目重发。非号主和群聊不能使用这一私密入口。本机认证的 GET /api/development-requests 可检查持久回执；原始聊天保留在原档案。

最终函数回复允许附带精确的 version=1、kind=reply、calls=[] 协议外壳，也允许只提交有效 messages 而省略 text。它们仍统一验证为同一个最终对白；非空 calls、错误 kind、未知字段、冲突文本及空消息继续拒绝，不把格式容错转换成工具执行权限。

本轮修复清单（2026-11-02）：主动回复按原渠道投递；显式收藏并复用收到的原图／GIF；私聊原生输入中提示；QQ 自然语言对白容错；号主开发待办。通过本机模拟服务验证后，使用引擎安装与原重启脚本应用；不向真实 QQ 发送测试消息。

模型可以为一次回复提供 `messages`，自然分成 1 至 6 条短消息。日常默认一条短应答，确有第二层信息才分两三条；详细解释与创作仍可长答，不要求每次凑条数。单条最多 2000 个 Unicode 码点，全部加上两个换行的兼容汇总文本最多 6000 码点。程序完整验证并审核整个回复后，才按模型提供的边界发送；不把任意长文强制切碎，也不为每条另调用模型。旧单条 `text` 返回继续使用原 `maxReplyCharacters` 限制，新数组验证通过后不再截短。

### 输入轮次与自然节奏（0.9）

连续输入按同一会话、同一发言人等待最近一条后的静默时间；图片及明显未完片段可多等一会儿，最多8条、64KiB、10秒便进入处理，避免无限等待。等待根消息时，其后同会话片段不会抢先启动另一个模型请求，其他发言人仍可处理。每条原始消息、图片编号和时间保留在加密账本，合并后的 `channel_identity.input_turn.parts` 继续保留对应关系；停顿不等于语义上已经说完。

小爱在同一次模型返回中判断本次是提问、指令、补充、应声还是素材，决定短答、长答或 silence；不额外调用一个模型判断意图。可选 `presentation.reply_timing` 为 `natural`（默认）、`immediate`（确认可以立即回应）、`wait_for_followup`（回复备妥后至少等3秒）。默认回复备妥后再等1600毫秒。第一条尚未发送时收到新补充，旧候选记为 superseded，保留原始生成记录和工具事实，再围绕补充处理。独立 `ha.channel-delivery` 标记使本会话、共享桌面历史和保留上下文均不会把这段未发出的候选当成实际对白；已经开始发送的分条仍遵守确认游标与原发送约束。

分条发送默认模拟每秒18字，间隔900～4000毫秒，带稳定的小幅变化；延迟写入账本，重启不丢失剩余间隔，也不重发确认前缀。等待使用下一次可执行时间，不占住后台睡眠等待。私聊继续使用原生“输入中”，群聊不冒充该状态。配置入口在 NapCat 插件设置：`replyPacing`、`replyGraceMs`、`replyCharactersPerSecond`、`replyMinIntervalMs`、`replyMaxIntervalMs`；输入侧为 `mergeWindowMs`、`mediaQuietMs`、`continuationQuietMs`、`maxBurstWaitMs`。关闭 `replyPacing` 可恢复无额外人工等待；关闭合并窗口不禁用明确的回复间隔设置。

图片对话后只保存可追溯的文字回执、来源、当时对白和明确“旧像素本次未重发”的历史标记到上下文投影，后续不反复携带图像。近期窗口按用户轮次和回复组计数，工具观察限额独立，不让多次读图挤掉此前指令。历史身份元数据不再复制一份长原文，分段编号及图片关联仍保留，正文与原档案不截断。

短期图片回看索引从5分钟8张扩为同一会话、同一发言人近30分钟最多16张，传输索引仍有32项上限；新增的是文字指针，不自动下载或向模型发送图片。仍可访问的图片可按需重读，已收藏图片附带其局部用法；不把素材释义扩大成人格、关系或全局价值约定。已经保存的图像、原始聊天与记忆均保留，不为表达修复批量删档；永久收藏仍必须由小爱主动调用。

同一输入对应一个加密投递账本，保存不可变的消息数组、下条游标和每条 QQ 实际成功回执。群聊仅第一条附带 @；截图仍沿用已有媒体工具的独立去重回执，不随每条文字重复发送。每条发送前，桌面后台重新检查当前渠道身份、权限、暂停、离线、作息睡眠与存档操作状态；不再需要旧模型会话租约仍然有效。

成功回执与游标一起提交。重启后，已确认的前缀不会重发；若上次崩溃时某一条还在发送，或传输超时、返回 5xx 等无法确认交付，则整批记为结果不明并停止余条，不盲目重试。只有明确 HTTP 401、403、429 拒绝，才有限重试当前条；不会重发先前成功条。部分失败保留已有回执和失败索引，不记为整批已完成。

后台暂停仍保留尚未处理的输入，但已经准备的回复及其余条在暂停时取消，恢复不会补播旧尾条；发送中的迟到成功回执可以留存，但不能使已取消尾条复活。离线或睡眠、权限变化也停止旧回复的余条。已提交给 QQ 的消息无法撤回。

Windows 开机自启动、隐藏实例管理及原生二维码窗口：参见 [desktop-startup-napcat.md](desktop-startup-napcat.md)。运行时安装包与登录数据保持独立；当前支持接入已有目录，自动下载和升级尚未实现。

## 2026-11-05 complete tools and outgoing work products

Large native tool catalogs no longer silently hide entries after the provider's
128-function ceiling. QQ send tools receive direct slots; a bounded catalog router
can call any remaining tool in the current authorized catalog. It normalizes into
the original canonical call before schema/source/review/permission/execution
checks. It cannot call an undeclared or out-of-scope target or mix a final reply
with operations.

`napcat.send_image(source_id)` accepts a current successful screenshot,
`pixel.preview`, or `workspace.view_image` observation. Sources still require the
same request trace, revision and local archive; paths/base64/user IDs are never
model arguments. Previewing/exporting alone is not delivery.

`napcat.send_file(mount,path)` sends an existing file from host-authorized drafts
or materials, up to 8 MiB, to the current QQ conversation. The engine AYIO
Workspace validates each read and rejects traversal/reparse/private paths. This
uses the local NapCat upload_private_file/upload_group_file API with its validated
workspace filename; no arbitrary external file root or recipient is exposed.
One encrypted durable outbox entry records the result; duplicate proposals reuse
the receipt and unknown outcomes are not automatically resent. No plaintext
conversation/image scratch files are created. Synthetic FakeBot tests cover the
actual upload route, recipient binding, duplicate calls and preview image send.

Owner-private channel context now includes filtered host-owned ongoing tasks,
instead of querying its physically separate, empty QQ task store. History and
source access checks remain in force.

文件上传参数依据：[NapCat 官方 API 目录](https://napneko.github.io/onebot/api) 的 upload_private_file / upload_group_file。
