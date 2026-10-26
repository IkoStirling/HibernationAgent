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
