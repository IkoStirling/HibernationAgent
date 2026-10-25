# 来源、素材与许可记录

**Date:**2026-10-25

- 桌面小恐龙为本项目代码绘制的原创像素图与简单程序动画，没有复制网络精灵图。
- 语音默认 Windows 自带 SAPI 语音。本机测试识别到 Microsoft Huihui Desktop；
  并非 AquesTalk 或原版ゆっくり音色，不附带第三方受限音库。
- [SAPI SpVoice](https://learn.microsoft.com/en-us/previous-versions/windows/desktop/ms723602(v=vs.85))
  用于本机异步语音、语速和完成状态；HTTP方式由用户自行配置兼容服务。
- 天气来自 [Open-Meteo](https://open-meteo.com/en/docs) 与其
  [地名查询](https://open-meteo.com/en/docs/geocoding-api)，界面和观测记录保留来源。
  其公开服务有使用范围与调用限制，对外商业分发须按服务条款另行处理。
- 视频搜索使用 Internet Archive 的公开搜索/metadata接口。返回结果的内容许可由
  各条目决定；搜索结果包含条目与许可地址。程序不承诺每个媒体都可用或均可再分发。
- 手工桌面验收使用 W3C 提供的
  [Sintel 预告片](https://media.w3.org/2010/05/sintel/trailer.mp4)，仅放在 E盘独立测试
  素材目录，未提交进项目。©Blender Foundation，CC-BY3.0；
  [官方分享说明](https://durian.blender.org/sharing/)。
- FFmpeg开发头文件来自官方8.1.2源码，SHA256
  `464beb5e7bf0c311e68b45ae2f04e9cc2af88851abb4082231742a74d97b524c`。
  PrepareVideo 记录 provenance.json，并复用本机引擎已有匹配 DLL。没有宣称这些本机
  二进制构成可直接再分发的发行包；发布前须核对实际 FFmpeg 构建选项、GPL/LGPL
  和所有链接依赖的许可。第三方依赖仍遵守各自许可。

- 网页桥接使用 Microsoft WebView2 Runtime 和固定版本 Microsoft.Web.WebView2
  SDK 1.0.3537.50。SDK 来自官方 NuGet，校验 SHA256
  `5EA526BBD728ADDA0DA4D31219267E96460494A427E4894C4E09D9F320F4B9AA`，
  不将 SDK、浏览器或账号资料提交进仓库。分发需遵守包内 Microsoft 许可。
  [官方嵌入文档](https://learn.microsoft.com/en-us/microsoft-edge/webview2/)
  · [用户资料目录](https://learn.microsoft.com/en-us/microsoft-edge/webview2/concepts/user-data-folder)。
  B 站入口只是网页导航配置，不包含账号、平台专用下载器或视频提取功能。

协议与行为参考在2026-10-25核对：
[FFmpeg protocols](https://ffmpeg.org/ffmpeg-protocols.html)、
[FFmpeg formats](https://ffmpeg.org/ffmpeg-formats.html)、
[OpenAI Structured Outputs](https://developers.openai.com/api/docs/guides/structured-outputs)、
[图像输入](https://developers.openai.com/api/docs/guides/images-vision)、
[Windows DPAPI](https://learn.microsoft.com/en-us/windows/win32/api/dpapi/nf-dpapi-cryptprotectdata)。
