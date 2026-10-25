# 记忆文件加密与旧档案兼容

2026-10-26。新载荷使用 `HV2:` + 十六进制的24字节随机 nonce、XChaCha20-Poly1305
密文与16字节标签。32字节主密钥仍由当前 Windows 用户的 DPAPI 文件保护；不换密钥
文件、不输出密钥。HV2 的实际加密密钥由 HMAC-SHA256(master,
`HibernationAgent/vault/HV2/XChaCha20-Poly1305`) 派生，与旧构造隔离。

加密正文首先绑定 scope、id、revision、schemaVersion、source、correlation、时间，
再保存实际内容。认证失败或绑定不符不返回旧的输出缓冲内容。加密/解密临时明文
退出作用域时清零；运行中的应用和已授权模型仍可访问必要明文。元数据和网络通信
不隐藏；普通用户界面继续只显示对话，不显示记忆操作和内部参数。

旧 HV1 使用引擎原有的8字节 ChaCha20-Poly1305 构造，但封装曾保存12字节 nonce。
后4字节实际不参与旧算法；保留读取其原始行为，绝不把旧密文按新算法解释。新写入
只用 HV2。账本通过显式兼容升级将 codec 身份从
`hibernation.chacha20poly1305.v1` 切换为 `hibernation.xchacha20poly1305.v2`。
所有旧载荷字节、记录版本、来源均不改写。旧程序因身份不匹配在启动时拒绝打开；
不允许手工改回标记来降级。SQLite 账本本身仍为引擎私有格式2。

部署前停止程序并完整备份档案，包含 `archive`、`runtime/memory-key.dpapi` 和必要
配置。密钥丢失/无法由当前 Windows 用户解锁时直接失败，不重新生成密钥覆盖档案。
需要回退时恢复完整匹配备份，不能只替换程序或单个数据库文件。当前保护目标为
文件内容不可直接阅读，不承诺阻止同一 Windows 用户主动调试程序。

验证：应用37项单测通过，包括所有24个 nonce 字节篡改、全部身份字段篡改、错误
主密钥、旧格式混读与不改写历史。真实旧二进制→新二进制→旧版拒绝→新版重开
的11项进程检查通过。独立 Python/系统库检查重新派生密钥、验证每条新旧载荷与
元数据，未输出明文或密钥；仅允许带明确夹具标记的合成档案。

```powershell
pwsh -NoProfile -File tests/VaultUpgrade.ps1 -BuildRoot out/staging `
  -LegacyExecutable <保留的旧版程序绝对路径> -Python <Python绝对路径>
```

引擎 AYCrypto 的变体实现与 AYStorage 的兼容编码身份升级分别有模块测试和文档，
应用不再自行实现另一套加密算法或账本迁移。
