# 构建与运行依赖

`Build.ps1 -Configuration Release -BuildRoot out/release` 生成优化的宿主与插件，默认
Debug继续用于开发。显式指定的配置必须与既有构建目录一致，修改类型先Configure
或选择新目录。所有应用内插件应使用同一套构建，不单独混换DLL。

视频仍复用引擎现有四个FFmpeg二进制和C接口，不重写解码、时钟或音视频同步。
PrepareVideo只复制avcodec-62、avformat-62、avutil-60、swresample-6，不把引擎演示
目录的其他调试依赖一起带入。当前复用的FFmpeg本身仍依赖已安装的MSVC调试运行库，
优化宿主不等于完整可分发安装包。

构建按CMake的实际运行依赖图补齐对应配置的DLL。旧目录里存在时间较新的错误
同名文件时也按内容修正，避免vcpkg仅比较时间而保留不匹配的Brotli或SQLite。
`host-runtime-<配置>.txt`记录共享目标来源；Find模块未暴露为共享目标的已知依赖
由独立检查补充。`Hibernation_RuntimeComposition`核对宿主、插件与复用视频的
实际文件指纹，包含在默认检查列表。

```powershell
pwsh -NoProfile -File tests/RuntimeComposition.ps1 -BuildRoot out/release
pwsh -NoProfile -File scripts/Build.ps1 -BuildRoot out/release -Configuration Release -Action Test
```

本机曾复现混入Debug的Brotli公共库后，浏览器/PowerShell请求br压缩会超时；简单
不压缩请求仍成功。修正依赖后，br及组合压缩头均正常返回，同一轮21组回归通过。
这项修正属于应用装配，未更改引擎网络接口或视频播放器。

## V0.7 便携工具

默认启用 `Hibernation_RuntimeTools`：逐文件验证仓库内固定版本的便携 Git / 7-Zip，
并复制到构建目录的 `tools/`，直接构建 `hibernation-agent` 也会执行。
不依赖可执行文件重新链接，不在构建中联网下载，也不会复制用户的 Git 凭证或本机配置。
`tools/bin/git.cmd` 和 `tools/bin/7z.cmd` 使用相对路径定位自身依赖；
`. tools/UseTools.ps1` 只修改当前 PowerShell 的工具路径。
`Hibernation_RuntimeTools` 回归在清除系统工具路径、隔离用户 Git 配置的子进程中验证
提交、标签、本地克隆及中文路径文件的 `.7z` / `.zip` 往返。

源仓库继续忽略 `out/`；携带 Git 工具不代表自动把构建输出纳入源仓库。
需要单独版本化某个构建目录时，可用附带 Git 初始化，明确选择发行文件，
排除本机配置、令牌、日志、档案和编译缓存。源码与工具许可证均在 `main` 上跟踪。

## 本地源码快照

共享工作目录不适合把所有并行改动当作单项提交。`scripts/SnapshotSources.ps1` 可将源码、公开角色配置、示例配置、素材和文档备份到 E 盘备份目录，保留文件指纹及应用/引擎提交号。复制前后核对源文件；并行编辑导致内容变化时停止打包，保留未完成目录供检查，不清理工作区。

快照排除本机配置、密钥、档案、运行目录、构建产物及实验报告；它标明包含共享工作区改动，不代替独立提交或对外安装包。仓库内的便携工具、许可证和构建脚本按原样包含；引擎源码、引擎外部依赖和本机未提交的引擎修改不包含在应用 ZIP 中。正常运行档案的备份仍须在后台优雅停止后单独处理，不能用源码快照恢复聊天。
