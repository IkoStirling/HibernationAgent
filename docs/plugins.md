# 可选原生插件

**Date:**2026-10-25 · **API version:**1 · **Owner:**HibernationAgent

插件放在配置 pluginRoot 的独立子目录，每个目录包含 plugin.json 和匹配 DLL。
清单目录名必须等于 id，library 是同目录文件名，api_version 为1。宿主不会按模型
给出的路径加载动态库；只有管理员已安装并启用的插件进入工具目录。

```json
{
  "id":"example",
  "name":"示例",
  "description":"独立能力",
  "version":"0.2.0",
  "api_version":1,
  "library":"Example.dll",
  "capabilities":["example.feature"]
}
```

DLL 导出 CreatePlugin，返回实现 RolePlugin.h 中 IRolePlugin 的实例。使用同一
编译器、C++运行库、接口头和 JSON 库编译。原生插件为受信代码，不是恶意代码沙箱。
引擎 IPluginLoader 负责装载、初始化失败清理及卸载；应用 PluginRuntime 负责角色
工具、配置、事件、快照、停用与加载事务的业务约束。

bind/tick/event/shutdown 位于桌面 owner 线程。invoke 位于唯一模型工作线程，
必须线程安全、限定网络/文件预算、响应取消，不能直接从工作线程操控 HWND。
媒体、语音插件通过自己的受控线程执行工作，UI状态由 owner 更新。关闭程序先
停止/等待模型工作，再保存快照和卸载插件；不让对象、回调或线程跨越 DLL 卸载。

tools 返回 name/description/parameters 数组，parameters 使用引擎支持的 JSON
Schema 子集。名字必须有命名空间且不冲突；memory/state/stage/schedule/workspace
为核心保留空间。invoke 返回 toolSuccess(result) 或 toolFailure(code,retryable)。
输出限1MiB，事件队列最多128项，异常转为有限错误码，不返回异常正文。

host 提供已授权草稿/素材根、桌面模式与 emit。snapshot/restore 只保存可恢复状态，
不恢复旧语音或旧气泡。插件快照与事件正文进入加密账本。新插件先暂存加载，失败
则回收；现有插件配置失败尝试回滚。底层原生外部副作用不具备数据库原子回滚。
启动加载失败会关闭该批插件并保留核心与 Dashboard，便于修正。

runtime.paused 事件用于停止朗读、暂停电视/角色游走及天气刷新；恢复不会擅自重播
历史语音或电影。当前四个插件可作为实现参考。桌面角色的 showInTaskSwitcher 为
可选可访问性设置，默认不加入任务切换列表。

电视本地入口会解析到素材根内，并限制文件协议和普通容器格式；网络入口仅允许
HTTP(S)及必要的内部传输协议，不允许播放列表转开本地 file/pipe/concat 路径。
这层限制由引擎 DemuxerOpenParams 的正向列表提供机制，具体范围由插件选择。

天气支持 Open-Meteo 兼容的 geocodingOrigin/forecastOrigin 覆盖（HTTPS或回环HTTP），
用于自托管与离线测试。默认官方服务，不保存/猜测真实位置。第三方API不可用时
保留已有观测时间并报告失败，不生成虚构天气。

电视结束与小角色点击/拖动可产生角色唤醒事件，分别由 wakeOnEnd、wakeOnInteraction
控制，默认开启。事件正文先写入加密账本，待处理指针持久保存；同一插件合并为
最新事件，最多8项，角色空闲且未暂停时每20秒最多送入一次现有外层事件队列。
因此重启可恢复待处理经历，连续鼠标操作不会逐次触发模型。实际调用仍计入小时
预算，角色允许沉默。天气刷新只缓慢影响数值，语音开始/结束不唤醒模型，避免循环。
