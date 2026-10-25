# 可选原生插件

**Date:**2026-10-26 · **API version:**2 · **Owner:**HibernationAgent

插件放在配置 pluginRoot 的独立子目录，每个目录包含 plugin.json 和匹配 DLL。
清单目录名必须等于 id，library 是同目录文件名，api_version 为2。宿主不会按模型
给出的路径加载动态库；只有管理员已安装并启用的插件进入工具目录。

```json
{
  "id":"example",
  "name":"示例",
  "description":"独立能力",
  "version":"0.3.0",
  "api_version":2,
  "library":"Example.dll",
  "capabilities":["example.feature"]
}
```

DLL 导出 CreatePlugin，返回实现 RolePlugin.h 中 IRolePlugin 的实例。使用同一
编译器、C++运行库、接口头和 JSON 库编译。原生插件为受信代码，不是恶意代码沙箱。
引擎 IPluginLoader 负责装载、初始化失败清理及卸载；应用 PluginRuntime 负责角色
工具、配置、事件、快照、停用与加载事务的业务约束。

清单可选 `dependencies:["widget"]`，其集合必须与原生 getDependencies 一致；启用
仍由后台核对，元数据不授予额外能力。管理页可联动启用依赖、关闭依赖时关闭相关
插件。新文件安装后重启后台以重新发现清单；已有配置可保存后热更新。

简单配置可由 `settings_ui` 声明，管理页无需为每个插件单写界面。最多32项，支持
toggle/number/select/text，使用 key、label、default 及数值 min/max/step 或选项列表。
见 rhythm/plugin.json。文字按普通文本显示，不加载插件 JavaScript；保留字段、原型
字段和疑似密钥字段不会生成普通输入。密钥继续使用单独的加密保存流程。声明式表单
只是编辑偏好，原生 bind 与应用策略仍负责类型、范围、依赖和权限。

构建会跟踪清单修改，不必为了复制新设置说明而修改/重新链接插件源码。复杂声库、
网站平台和工作区仍使用对应专用设置。停用插件保留加密快照，重启不自动重播旧声音。

bind/tick/event/shutdown 位于桌面 owner 线程。invoke 位于唯一模型工作线程，
必须线程安全、限定网络/文件预算、响应取消，不能直接从工作线程操控 HWND。
媒体、语音插件通过自己的受控线程执行工作，UI状态由 owner 更新。关闭程序先
停止/等待模型工作，再保存快照和卸载插件；不让对象、回调或线程跨越 DLL 卸载。

tools 返回 name/description/parameters 数组，parameters 使用引擎支持的 JSON
Schema 子集。名字必须有命名空间且不冲突；memory/state/stage/schedule/workspace/computer/capabilities
为核心保留空间。invoke 返回 toolSuccess(result) 或 toolFailure(code,retryable)。
输出限1MiB，事件队列最多128项，异常转为有限错误码，不返回异常正文。

host 提供已授权草稿/素材根、桌面模式、emit 与宿主 browser 协调入口。ABI 2 增加 browser；旧插件须重编译，版本不符拒绝加载。snapshot/restore 只保存可恢复状态，
不恢复旧语音或旧气泡。插件快照与事件正文进入加密账本。新插件先暂存加载，失败
则回收；现有插件配置失败尝试回滚。底层原生外部副作用不具备数据库原子回滚。
启动加载失败会关闭该批插件并保留核心与 Dashboard，便于修正。

runtime.paused 事件用于停止朗读、暂停电视/角色游走及天气刷新；恢复不会擅自重播
历史语音或电影。当前四个插件可作为实现参考。桌面角色的 showInTaskSwitcher 为
可选可访问性设置，默认不加入任务切换列表。

widget.skin 支持 dinosaur / moonbird / lumen，widget.palette 支持 forest / mist / amber；
由用户配置，不改变角色人格。reducedMotion 或 Windows 关闭客户端区域动画时停用
呼吸、步态和拉伸。朗读嘴部由实际 speech.progress 的字符推进驱动，拖拽释放经过
landing 再回到 curious；失去鼠标捕获会恢复，不遗留 dragged 状态。暂停冻结动画
时钟，恢复不补播暂停期间的帧。全部图形为本项目的代码像素素材。

电视本地入口会解析到素材根或草稿根内，并限制文件协议和普通容器格式；网络入口仅允许
HTTP(S)及必要的内部传输协议，不允许播放列表转开本地 file/pipe/concat 路径。
这层限制由引擎 DemuxerOpenParams 的正向列表提供机制，具体范围由插件选择。

天气支持 Open-Meteo 兼容的 geocodingOrigin/forecastOrigin 覆盖（HTTPS或回环HTTP），
用于自托管与离线测试。默认官方服务，不保存/猜测真实位置。第三方API不可用时
保留已有观测时间并报告失败，不生成虚构天气。

电视结束可由 wakeOnEnd 唤醒角色；待处理指针最多8项，同插件合并，两分钟后丢弃，
后台空闲时每20秒最多处理一次。点击/拖动立即切换动画，不直接唤醒语言模型；旧版本
遗留的鼠标唤醒项会被清理。天气缓慢影响数值，语音事件不形成自我调用循环。

widget.reactionCacheEnabled 可开启应用负责的拖动短反应缓存（示例默认关闭）。启动或
空闲时准备最多六条，每条最多40字，受共享模型预算、超时、暂停与前台交谈优先级控制。
已配置的审核连接在后台批量审批；手势当下采用 cache_only，不等待审核网络请求。
speech.prepare 是内部事件，TTS 静默预合成 PCM 后返回 speech.prepared；不向模型开放
伪造准备回执的工具。消费使用 audio_cache_id + prepared_only，缺失 PCM 就失败，
不会临时合成而迟到。PCM 只在内存保存，候选/消耗记录进入加密版本账本；重启只为
未消费候选重新准备语音。实际消费前的候选不会被当成发生过的经历。

反应有效期20分钟；模型、人格版本、声音设置、活动或心情区间变化会淘汰不适配候选。
默认冷却8秒、回应概率45%，可配置 reactionCooldownMs / reactionProbability。忙碌、正在输入、朗读中、无就绪条目、
超过500毫秒才处理的拖动直接忽略，不创建待播放手势。只保留最近30秒实际互动供
下一轮对话理解，不把所有鼠标反应塞进主聊天记录。

widget.position 是合并后的瞬时位置事件，每50毫秒最多一次，按所在显示器中心映射
pan[-1,1]。它只转发给插件，不入档案或事件索引、不成为记忆。小角色隐藏/卸载时
active=false 使语音居中。tts.spatialAudio 可热配置关闭；speech.status 提供当前声像
及等功率左右增益。三种声音均复用 AYAudio，Mei 分段流关闭欠载时钟推进。

电视通过 AYVideoPlayback/AYAudioPlayback 复用完整 AYVideoPlayer；窗口保留最后一帧，
双缓冲绘制，不自行解码、维护第二套音视频时钟或 WinMM 音频队列。媒体协议和本地
路径授权是不同边界；openWithOptions 保留插件选择的传输限制。

bind 会在创建后台工作前检查已知字符串、布尔值、数字范围及无凭证代理地址；
错误配置拒绝加载/重配，不把类型错误留给媒体、语音或鼠标回调线程处理。
