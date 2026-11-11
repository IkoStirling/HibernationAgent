# 小电视：HTML导航与视频画面（2026-11-05）

导航与看片使用不同通路。网页按默认1280px桌面布局缩小到电视屏幕宽度，避免只看到站点左上角；出现准备好的视频元素后自动铺满电视内部屏幕，保持视频比例。独立登录窗口恢复正常网页布局，关闭后返回电视布局。只在尺寸／模式／目标视频发生变化时更新样式，不每帧重写整页CSS。

## 角色工具

- `television.search` 按配置平台打开搜索页面；`television.page` 读取当前已加载DOM的过滤HTML、标题、链接与媒体状态。视频链接优先返回；可通过offset/limit分段，expected_revision避免拼接发生变化的页面。`television.open_page` 打开已返回链接，继续受平台域名配置约束。HTML读取只覆盖当前已加载内容，不是整站索引，也不自动加载无限列表。
- 网页模式的旧 `television.observe` 也改走上述DOM通路，include_frame不再触发网站截图。HTML过滤移除脚本、样式、登录／表单输入、事件属性、Cookie接口及带敏感参数的链接；保留有限的文本与导航结构。内容是外部资料，不能改变工具权限。跨域iframe只报告存在，不穿透读取。没有通用模型JS执行工具。
- `video.web_observe` 属于video-analysis插件，直接从当前网页视频元素抽取1–3张JPEG并提供真实时间码、媒体身份和范围。不截导航页面、不下载整片、不把blob地址当普通视频URL交给FFmpeg。换页、换媒体、明显跳转或登录使旧采样失效。原有本地video.storyboard/zoom/watch仍复用固定的DSH分镜和引擎播放器。
- 取帧遇到跨域画布、DRM、未就绪或不支持的视频表面时返回原因；不会悄悄用整个网站截图代替。网页取帧当前没有音轨或字幕输入，不表示看完整段。`video.watch(start,source=webpage)`现可按实际播放进度持续唤醒；`video.web_observe`仍是单次观察。
- 新观察纳入原有加密记录、视觉回看和记忆来源；过滤HTML作为已读取网页片段，视频记录保留采样时间码，不把网页阅读提升为整片观看。角色和审核图片继续复用统一图像交付通路。

## Bilibili实测边界

独立匿名Chrome及应用内原生WebView均直连公开视频 BV1GJ411x7h7，页面返回200、HTML可读、视频可播放，video.web_observe成功从浏览器媒体元素取帧。不读取正式浏览器登录档案，不复制Cookie，不调用付费模型。这证明该样本的实时画面通路可用；不保证会员、地区限制、交互视频、其他编码或未来播放器变更都兼容。

下一层平台适配可以加入已授权字幕与音轨、更多平台播放器适配；网页持续抽样陪看见下文新增实现。登录和站点要求仍由用户在独立窗口正常处理。

## 验证

本地动态合成视频检查过滤HTML、有效链接、分页版本、导航缩放、视频铺满、真实运动帧与登录布局恢复。原生隔离桌面流程检查新旧观察工具、插件帧序列、登录时阻止HTML和视频读取、正常退出；已有独立登录与本地播放器/陪看回归继续运行。浏览器视觉截图来自合成素材，不把真实账号页面输出为测试档案。

已处理B站空占位节点和初始空白导航文档；text字段为短首段预览，offset/limit对过滤HTML分页生效。正式搜索页校验得到25%缩放、约1.9万字符HTML与16个链接；实际部署验证记录见TelevisionDOM-20261105/search-fix。


## Web continuous watching (2026-11-05)

`video.web_observe` is a one-shot glance. `video.watch` with
`{"action":"start","source":"webpage"}` binds a task to the currently loaded
video's page URL and media identity. The existing `video.watch_ready` event wakes
the character after actual playback has advanced (default 30 seconds), and
`video.watch_next` samples new video-element frames. No screenshot, proxy, stream
download, seek or audio transcription is introduced. Receipts identify only the
frames sampled now; the interval between observations is not claimed as viewed.

Playback telemetry is obtained from the trusted WebView presentation bridge every
500 ms. Pause suppresses opportunities; seeking resets coverage. Changing media,
closing the page, entering the separate login window or stale telemetry suspends
the task. Three failed or unconsumed opportunities pause it. Runtime pause and
foreground input retain their existing priority. `video.note` persists sourced
notes. Restart requires explicit resume after opening the same media. Both QQ
and desktop use the same tools and host-owned watch session; quiet watching does
not require sending a message for every sample.

## 后台取帧和 Playwright 接入

取帧与媒体信息由 `ui/video-frame-reader.js` 提供。小电视把它和自己的布局桥组合使用。网页播放器已有解码帧时，即使视频元素不参与布局也能读取。可用 `television.open_page` 的 `visible: false` 在后台打开平台页，再用 `video.web_observe` 读取。后台只采集媒体信息，避免改写页面布局。需手动登录或播放的页面，可先通过小电视正常完成操作。

外接时由调用者提供已连接的 Playwright Page。用 `bindVideoPage(page, expectedUrl, selector)` 核对完整地址并绑定视频元素，再用 `captureVideoPage(page, bindingId)` 取帧。selector 可省略，多视频页面可指定唯一选择器。绑定只在当前文档、当前地址和当前视频源内有效。关闭、刷新或切换内容后需重新绑定。

Claude Code 已接入 Playwright MCP 时，可用 `browser_tabs` 选择目标标签页。运行下面的命令，将输出作为现有 `browser_evaluate` 的 `function` 参数。第一条返回 `binding_id`，第二条返回 JPEG 和播放时间。

```powershell
node tools/PlaywrightVideo.mjs bind "https://example.com/watch/123" "video"
node tools/PlaywrightVideo.mjs frame "<binding_id>"
```

两个入口使用小爱仓库维护的同一份取帧代码。外接入口借用调用方已有的 Page，在对应文档里执行，浏览器进程与会话均由调用方管理。Playwright MCP 的通用求值接口返回结构化数据，JPEG 在 `image.base64`。使用 Node 的 Playwright 接口时，可将 JPEG 解码保存到工作区，再作为图像交给模型。
