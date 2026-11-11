# 源码留在本机，构建迁到机械硬盘

本轮先实现路径分离，没有搬动现有源码、主程序、依赖或存档。新硬盘的实际盘符确定后再迁移。

2026-10-29 阶段三至五检查：D 盘约 28 GiB 空闲，足够本轮构建，未触发用户指定的
F 盘回退。E 盘正式档案、密钥和工作区不随构建位置改变。当前主目录仍为 out/build；
完整候选验证临时目录为 out/cognitive-runtime-validation，打包并保留验证记录后清理。
候选运行包放在 out/cognitive-runtime-candidates，不是另一份 CMake 增量构建缓存。

2026-10-30 八小时加固复核：D 盘仍有约 34 GiB、E 盘约 218 GiB 空闲，F 盘未挂载，
没有触发迁移。本轮临时构建为 `out/continuity-8h-validation`，独立校验运行包及源码
ZIP 放在 `E:/Aliyat/Backups/HibernationAgent/continuity-8h-20261030`。主实例和正式
E 盘存档路径保持原约定；临时构建只在封存证据、完成更新后退休。

## 目录职责

|内容|位置与原则|
|---|---|
|源码和 Git 仓库|继续留在 D:/Aliyat/HibernationAgent；引擎和 BSimmer 源码也保留原位置|
|CMake/Ninja、obj/lib/exe/dll、编译临时文件|由 -BuildRoot 或 HIBERNATION_BUILD_ROOT 指定，可在机械硬盘|
|FFmpeg/WebView2/miniaudio/应用 Python 等依赖|可另用 -DependencyRoot 或 HIBERNATION_DEPENDENCY_ROOT；不要求和源码/构建同盘|
|正式记忆、密钥和工作区|仍由应用 local.json 决定，不随构建路径修改|
|实验存档、报告和 API 用量账本|实验脚本 -OutputRoot 指定，不混入源码或正式档案|

Build.ps1、Start.ps1、NewPortablePackage.ps1、RunCognitivePrototype.ps1 和 RunCognitiveModelLab.ps1
使用同一构建路径规则：显式参数优先，其次进程环境变量，最后为源码下 out/build。
相对路径始终基于源码根目录，与当前终端在哪个目录无关。发行包附带 BuildPaths.ps1；
已安装版本的启动器继续显式传递版本内 app 路径，不受开发环境变量干扰。

## 迁移方式

以下 H: 仅为示例，不是已经选定或执行迁移的盘符：

```powershell
$env:HIBERNATION_BUILD_ROOT='H:/AliyatBuild/HibernationAgent'
./scripts/Build.ps1 -Action Configure -Configuration Release -CognitivePrototype
./scripts/Build.ps1 -Action Build -Configuration Release -Targets Hibernation_CognitivePrototype,Hibernation_CognitiveLoopTests
./scripts/RunCognitivePrototype.ps1 -Scenario normal
```

完整应用构建仍可使用 -Targets hibernation-agent,Hibernation_Checks。上例环境变量仅影响当前终端；
其他终端继续默认路径。不要设置示例环境变量后误以为旧主构建已自动搬走。

CMakeCache、build.ninja 和已有增量对象包含绝对路径，不能仅复制旧构建目录然后继续编译。
新位置重新 Configure/Build，验证运行包和依赖后再决定是否退休旧目录。代码仓不需要移动。
Python venv、WebView2 登录资料和程序配置也不是可任意复制改盘符的普通编译缓存。

如果只迁构建，依赖可继续留在现有 out/deps。以后迁应用依赖时：

```powershell
./scripts/Build.ps1 -Action Configure -Configuration Release -BuildRoot H:/AliyatBuild/HibernationAgent -DependencyRoot H:/AliyatDependencies/HibernationAgent
```

目标依赖目录必须先具备已校验 SDK；脚本不自动下载或移动依赖。Python venv 应按新位置重建，
导师/训练插件里显式配置的 pythonPath 也需分别检查。引擎的 vcpkg installed 目录当前仍沿用
引擎已有依赖路径，本轮没有整体迁移引擎依赖仓。

## 运行与性能

本轮没有设置永久环境变量或更改正式启动实例。切换实际运行程序前，先保存退出旧后台，
核对配置与 dataRoot，再显式选择新 BuildRoot。无需把记忆搬进构建目录，也不能让两个后台
同时写同一存档。

机械硬盘上的大量小文件编译、链接和测试可能更慢；保留单一主要构建及有效增量缓存。
默认编译 TEMP/TMP 跟随 BuildRoot，实验有独立输出参数。磁盘容量不足时停止并保留诊断，
不自动删除旧构建、用户文件或生产档案。
