# 本地发行、升级与回退

发行脚本把 Release 构建、插件、角色资源、管理页面和经过校验的工具复制成独立目录。它不复制生产存档、`local.json`、DPAPI 密钥、API 凭据和测试程序。默认配置只保留 Mock 连接，关闭插件及电脑操作；真实模型与插件由使用者配置。

```powershell
./scripts/NewPortablePackage.ps1 -BuildRoot ./out/daily-runtime-20261028 -OutputRoot E:/Aliyat/Packages -Zip
./scripts/InstallPackage.ps1 -PackageRoot E:/Aliyat/Packages/<包名> -InstallRoot E:/Aliyat/Applications/XiaoAi
```

需要 Windows x64、PowerShell 7 和相应运行依赖。生成清单记录每个文件的 SHA-256，并检查可执行文件和 DLL 的普通导入表是否引用开发版 CRT。发现这类依赖时仍保留诊断包，但 `portable_ready` 为 false；加 `-RequirePortable` 会拒绝作为便携包发布。这不是完整依赖闭包检查，也没有代码签名，不能把目录校验当作发行者身份认证。当前视频 SDK 的开发版依赖需要替换成经过验证的 Release SDK 后，再在干净电脑上验证。

安装布局：

```text
XiaoAi/
  Start.cmd / Stop.cmd / Dashboard.cmd
  current.json
  versions/<包名>/       # 不存用户数据，保留完整清单
  config/local.json      # 跨版本保留
  data/                  # 跨版本保留
  installation-backups/ # 配置、指针与配置预检日志
```

安装完成不自动启动。`Start.cmd` 打开桌面角色程序，`Dashboard.cmd` 打开管理页面，`Stop.cmd` 保存退出。安装脚本要求该安装位置的后台已经退出，防止同时更新运行中的文件。启动时根据安装指针校验清单指纹，再校验版本内所有文件；配置与数据独立保存，不参与版本文件清单。

升级时再次执行安装命令。原有连接、插件设置、工作区和档案路径保留，只更新仍由安装器管理的默认资源路径。自定义资源路径保持原值。新程序必须通过已有配置检查后才切换版本指针。失败会恢复配置、版本指针与启动文件；失败版本目录保留供检查。这是程序级故障恢复，不保证在任意断电点完成跨文件事务。

```powershell
./scripts/InstallPackage.ps1 -Action Rollback -InstallRoot E:/Aliyat/Applications/XiaoAi
```

回退切换到上一个已校验版本，保留当前用户配置和全部档案；旧版本无法接受当前配置时拒绝切换。它不会自动降级数据库格式。清单不允许未列文件、路径穿越、Windows 文件别名或目录链接。不要直接修改已安装版本目录内的 UI 或插件；开发时使用源码启动目录。

密钥跨机迁移见 [存档恢复说明](vault-portability.md)。版本回退与存档恢复是两项独立操作：安装器不迁移、删除或覆盖记忆。

`tests/PortablePackage.ps1` 使用全新目录测试打包、移动目录、实际 Mock 交谈、保存退出、升级、配置与存档保留、回退、文件篡改和切换失败恢复。`tests/PackageIntegrity.ps1` 使用小型合成清单检查路径与文件完整性边界。均不读取生产存档或调用真实模型。
