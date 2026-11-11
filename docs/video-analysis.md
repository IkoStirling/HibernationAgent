# Video analysis adapter

The optional video-analysis plugin imports the pinned dsh-video-storyboard 0.6.6 source snapshot in vendor/. Apache-2.0 LICENSE, NOTICE, CREDITS and archive digest are retained. Upstream sources are unmodified; worker.mjs adapts them. No DSH host, React UI, remote downloader, browser cookie export, ASR service startup or model connection is loaded by the adapter.

Tools: video.probe reads metadata; video.storyboard creates sampled sheets; video.zoom samples a narrow window more densely; video.locate finds literal text in timed subtitles. Inputs use mount (materials or drafts) and a forward-slash relative path. Optional transcript_path is an SRT or timed JSON file in the same mount. Embedded text subtitles are used when available. Default window is 30 seconds, maximum 120 seconds, at most 48 samples / two images; zoom uses at most 12 samples. Longer material is traversed through explicit windows. This does not prove continuous viewing or understanding of audio. There is no automatic full-film memory or advance plot feed.

Enable the plugin in dashboard and set trusted absolute paths for Node.js >=20, ffmpeg and ffprobe. The current machine uses E:/Aliyat/Tools/VideoAnalysis/node.exe and ffmpeg-9.0.2-essentials_build/bin/{ffmpeg,ffprobe}.exe. These optional dependencies are external to immutable application versions. Node was copied from the installed Codex runtime; the FFmpeg archive came from Gyan and its SHA256 was verified against 60f467265b1e312373dbcd92200c2618a74850f98d3d078e94296bb3fa2047ba. They are not silently downloaded by the plugin. Missing paths return video_dependencies_missing; an old Node returns video_node_version. Configure dependencies separately on another machine.

Application permission checks reject private mounts, absolute paths, traversal, reparse points, unsupported media, oversized files and disguised playlists. Worker child calls hide windows and restrict FFmpeg to local file/pipe/fd protocols. The engine owns the complete worker process tree, with 60-second timeout, cancellation and 4 MiB output bound. A single plugin instance processes one job at a time with two extraction workers. Request scratch directories are removed on success, failure and cancellation. Native plugins are trusted code, not an OS sandbox against a malicious local process replacing files concurrently.

Actual PNGs use the existing tool-image slots: model text contains receipts without base64; role and configured reviewer receive the same image evidence. Original observations remain in the existing encrypted archive. Prior observations are revisitable with existing source-access checks. DeepSeek-specific token estimates and nonexistent upstream evidence-file instructions are removed from rendered manifests. Subtitle text is media content, not an instruction source.

The television login implementation already reparents the same WebView into a resizable window and back. Dashboard's platform/login button now opens that window immediately after opening the platform. The television's own login button and context menu remain supported. Profiles persist under dataRoot/runtime/browser/<platform>; no cookies are moved to this analysis plugin. Real website login remains manual, and website expiration is not overridden.

Validation: tests/VideoAnalysis.ps1 generates a synthetic video with Chinese subtitles in an independent Mock archive, tests real native plugin/worker processing and rejected paths, then uses a local fixture model and reviewer to verify identical PNG delivery. It makes no paid API calls and uses no real account. Tests/TelevisionLogin.ps1 covers native window/profile persistence; television sequence/configuration regressions must also pass. The packaging allowlist includes .js/.mjs plus vendor package metadata and attribution files.
## 持续陪看（2026-11-04）

本地播放器仍由引擎 AYVideoPlayer 负责解码、音视频同步和播放；本插件只处理分镜与观看任务。开启 `video-analysis` 后，可在 dashboard 的“视频理解／陪看控制”中填写已在小电视播放的素材区相对路径，或让角色调用：

1. `television.open` 打开本地影片，`video.watch` 用 `action=start`、`mount=materials|drafts`、`path` 开始陪看。可选 `transcript_path` 指定同区字幕。
2. 程序每 250ms 读取播放器位置。连续实际播放默认累计 30 秒（`watchWindowSeconds`，10–120）后，发出 `video.watch_ready`，经现有后台调度唤醒角色；用户输入和前台对话优先。此时角色可 `video.watch_next` 获取该段的分镜和台词，也可以安静观看，不要求逐段发言。
3. `video.note` 用真实返回的 `watch_receipt.id` 写 `character/event/question/impression` 笔记；来源固定为影片、陪看会话与时间窗。`video.notes` 可按素材区／路径读取，后续对话上下文提供最近 8 条，不反复装载全部历史。
4. `video.watch action=pause/resume/stop/status` 控制任务；电视本身仍用 `television.control`。恢复任务从当前播放位置重新累计，不把暂停任务期间的影片算作已观察。暂停播放器会中断正在分析的任务，恢复播放后保留此前尚未分析的连续范围。

陪看打开时，storyboard/zoom/locate 也受相同进度边界约束；不能借换参数抢看后续或其他影片。字幕跨越当前片段边界时整句暂不交付，以免一句台词提前透露后续。分镜仍是抽样，不保证捕捉每个动作、每句台词。快进、后退或超过 3 秒的遥测缺口会重建覆盖区，跳过的片段不会自动变成已看。换片、切换本地／网页片源或播放失败使任务挂起；重启保留笔记与任务，未完成任务挂起，必须手动续看，不自动播放。已经看过的笔记不会因倒退播放而被删除。

一次只运行一个分析进程；暂停／结束控制不需要等待解码互斥锁。失效的结果在返回模型前被丢弃，旧的已排队唤醒也会核对会话和分析游标。连续 3 次处理失败或 3 次未消费的观察机会后暂停；未消费机会至少间隔 60 秒再提醒，不每帧调用模型。成功观察后按实际播放片段继续，服从后台调用预算和用户输入优先级。图像、回执与笔记经原有加密存档保存；最近 64 条笔记／回执属于热索引，淘汰热索引不删除原始事件、工具观察和历史版本。

### 缺字幕时的可选转写

默认 `asrEnabled=false`，不自动开启服务、下载模型或调用付费接口。优先使用指定字幕和内嵌文本字幕；无字幕且有音轨时，才使用明确配置的兼容 multipart `/audio/transcriptions` 服务：

```json
{
  "asrEnabled": true,
  "asrEndpoint": "http://127.0.0.1:8000/v1/audio/transcriptions",
  "asrModel": "your-installed-model",
  "asrKeyEnv": "XIAOAI_VIDEO_ASR_KEY",
  "asrLanguage": "zh"
}
```

本地可用 HTTP；远端必须 HTTPS。密钥只从指定环境变量读取，不写进命令行、字幕或日志，禁止重定向携带凭据。远端开启后会发送所选片段的音频，可能按服务计费。仅抽取当前合法窗口的单声道 16kHz WAV，按最多 4 个 30 秒片段上传；限制响应大小、超时及整个进程生命周期。服务只返回全文时标记 `chunk_only` 粗时间码；转写有误或不可用时保留画面，反馈 `asr_status`，不把转写当成语气／音乐理解。应用麦克风录音转文字仍是未来功能。

网页／Bilibili 继续复用当前页面观察、手动独立登录和已保存的浏览器档案。此轮没有把登录会话导出给 FFmpeg，也没有接入网页视频下载或自动全片陪看。

验证包含：`Hibernation_VideoWatch` 的状态机边界、`Hibernation_VideoCoWatch` 的真实引擎播放→后台唤醒→分镜→来源笔记→加密存档重启恢复、`Hibernation_VideoTranscription` 的本地假服务与真实合成音轨；未使用私人影片、账号或付费 API。

### 发布运行库补充

本机候选的旧 FFmpeg 8.1.2 DLL 带 Debug CRT 依赖。已替换为 Gyan 同版本共享 Release 包（https://www.gyan.dev/ffmpeg/builds/packages/ffmpeg-8.1.2-full_build-shared.7z），发布页 SHA256 `cba748035c21ce1431d0823c7a3a711f38616f89f87a265dceddf9b7f6749d2d` 验证后由 PrepareVideo.ps1 生成导入库。构建新增始终刷新的 Hibernation_VideoRuntime 目标，防止导入符号不变时增量构建继续携带旧 DLL。PrepareVideo 拒绝 Debug CRT；发行包包含 headers/source license 和 binary LICENSE/README。此项修正是播放器运行库准备与复制，不替换引擎播放器或另写音视频同步。

网页持续陪看现已接入同一 `video.watch` / `video.watch_next` 会话：`source=webpage` 使用网页播放遥测，真实时间码的新帧回执和来源笔记。旧文中的“没有接入网页自动陪看”为 11 月 4 日状态；当前范围和中断规则见 [television-dom-video.md](television-dom-video.md)。无网页音轨／字幕识别，不代表逐帧理解全部影片。
