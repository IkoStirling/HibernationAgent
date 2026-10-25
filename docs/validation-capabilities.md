# 桌面工具与网页电视验收

2026-10-26，Windows x64，本机轻量构建。真实档案与模型未用于测试。

- scripts/Build.ps1 -Action Build：成功，四个 ABI 2 插件全部重建。
- scripts/Build.ps1 -Action Test：18/18 组通过，含协议、进程、连续状态、启动器、
  存储、加密、网络、模型、插件、视频解码、音频与完整播放器。
- 最终重建后的应用单测、AYPlatform_Test、AYPlatform_WebViewTests、
  AYNetwork_HttpTests：4/4 通过。
- tests/Capabilities.ps1：24 项通过。模型自行发现能力、目录真实回执、越界拒绝、
  清单程序启动及环境隔离、自己的空白对话窗口图像输入、隐藏/禁用小角色后显式
  重新显示、网页搜索/文字/链接/JPEG、密码输入值排除、域名限制、页面控制、
  资料目录跨导航/插件重载保留、切回引擎解码及关闭均已验证。
- tests/Browser.mjs：36 项通过，无页面异常；电脑能力配置、屏幕观察默认关闭、
  禁用工具后从模型目录移除、窗口重开按钮、平台登录入口及移动布局已核对。
- tests/DesktopMedia.ps1：58 项通过，继续验证空间音频、即时拖动行为、视频无
  背景闪屏、引擎主时钟、插件卸载与正常退出。
- 引擎 SDK doctor：181 能力、59 模块、130 搜索用例、873 入口符号；0 错误/警告。

能力测试使用 E:/Aliyat/CapabilityTests 下的独立合成档案与本地 HTTP 页面，不使用
真实网站账号、真实 API、用户文件或整个屏幕。程序启动夹具只写一个成功标记即退出。
管理页与桌面媒体测试资料在忽略的 artifacts 内。

最终能力测试证据：E:/Aliyat/CapabilityTests/7f7fd2e1464a4e15abae50bc29ff195c。
SDK 报告为 docs/sdk-doctor.json。原有 18 组测试先完整通过；随后仅调整网页模式
快照以避免带入旧视频状态，再重建并通过上面的 4 组和 24 项定向检查。

生产后台已用原配置重新启动，E 盘加密档案、原模型、四个插件与既有语音设置保留。
真实 B 站账号/验证/付费内容未验收；电视边框皮肤、任意网页点击与键盘自动化未实现。
当前网页观察基于实际可访问文本、链接和画面，不能自动理解整部影片。

复验能力进程流程：

```powershell
pwsh -NoProfile -File tests/Capabilities.ps1 -Executable out/build/hibernation-agent.exe -ProviderExecutable out/build/Hibernation_TestProvider.exe -DesktopFixture out/build/engine/AYFoundation/AYPlatform/unittest/AYPlatform_DesktopFixture.exe -ProjectRoot D:/Aliyat/HibernationAgent
```
