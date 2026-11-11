# 连续认知最小原型

这是 cognitive-continuity-plan.md 的阶段 1：独立可运行的合成闭环，未接入正式桌面、QQ、
生产记忆。默认 Mock；可选第二阶段真实连接见 cognitive-model-experiment.md。普通应用构建和启动默认不启用它，不改变既有 turn-v1。

## 构建与运行

复用现有引擎 RoleState、RecordStore 与 HV2/DPAPI 加密。没有新增生产事件总线或记忆库。
在项目目录、正常 MSVC 环境运行（与 scripts/Build.ps1 相同编译环境）：

```powershell
./scripts/Build.ps1 -Action Configure -BuildRoot out/build -Configuration Release -CognitivePrototype
./scripts/Build.ps1 -Action Build -BuildRoot out/build -Configuration Release -Targets Hibernation_CognitivePrototype,Hibernation_CognitiveLoopTests
ctest --test-dir out/build --output-on-failure -R '^Hibernation_Cognitive(Loop|PrototypeProcess)$'
./scripts/RunCognitivePrototype.ps1 -Scenario normal -OpenReport
```

使用 D:/Software/Cmake/bin 下的 cmake/ctest 或已配置的 PATH。所有实验程序位于主构建的
experiments/cognitive 子目录。不要为这个原型另建几十份构建目录。

场景可选 normal（中断与恢复）、rest（连续精力与暂停）、disappearing（目标消失）、
timeout（缺少完成回执）、attention（焦点竞争）；加 -AttentionOff 运行注意驻留对照。
默认实验输出 E:/Aliyat/Backups/HibernationAgent/cognitive-experiments，-OutputRoot 可改。
每次生成独立目录、events.json、可阅读的 report.html 和 report.jsonl。
报告明文仅含合成诊断；data/archive/records.sqlite 的全部载荷仍使用既有加密。

## 已实现的边界

- 最多 16 个模拟物件、4 个注意候选、一个执行目标和一个被打断的打算。
- 主要焦点默认驻留 2 秒，并使用 0.15 的切换差值；这些是可调整工程参数，不是科学常数。
- 模型只提出 approach_and_eat/rest/silence，程序分解移动与食用，核对实际回执后串行推进。
- accepted 不完成目标；成功必须匹配操作令牌、目标身份/版本及抵达/食用/休息依据。
- 完成、确认无效果的失败、中断、放弃和结果未知分别记录。超时和重启不重试未确认效果。
- 消息打断保留实际位置；对话结束后重新核对目标，30 秒内有效打算可以接续。
- 暂停冻结模拟时间；重启保留身体值、不补算离线活动，取消未确认操作和未重验证打算。
- 事件身份及 SHA-256 指纹持久去重；身份冲突拒绝；已消费的旧物件版本不能复生。
- 模拟模型可选择 normal、silent、malformed、stale，用于验证无更新与错误输出。
- 已接收的有效输入、结果和状态版本通过一个账本事务写入；没有覆盖或删除历史。
  无效外部事件仅返回拒绝诊断，不成为经历；模型无效提案的拒绝仍随有效事件保存。

## 命令行和持久化

```powershell
out/build/experiments/cognitive/Hibernation_CognitivePrototype.exe --root E:/Aliyat/Backups/HibernationAgent/cognitive-experiments/manual-new --script events.json
out/build/experiments/cognitive/Hibernation_CognitivePrototype.exe --root E:/Aliyat/Backups/HibernationAgent/cognitive-experiments/manual-new --status
```

--root 必须显式指定。首次使用须为不存在的目录；已有目录必须持有精确实验标记。
禁止链接路径，独占进程锁；缺失原密钥不会新建密钥替代它。不能将生产 dataRoot 传给此程序。
省略 --script 执行内置合成示例，--script 传 JSON 数组。--status 不推进时间或取消动作。
已有存档的注意配置固定，使用 --attention-off 的存档再次打开时也需同样参数。

脚本事件详见 RunCognitivePrototype.ps1。每项有 event_id 和 type：seed（仅首次）、observe、
remove、advance、message、conversation_done、pause、receipt、model_mode。
advance 的 executor 为 automatic（模拟执行器）或 silent（故意不返回回执）。
receipt 仅代表实验执行器输入，不是角色可伪造的工具结果；未来接入应用时必须由宿主产生。

存档最多接收 4096 项事件，达到上限停止本次实验，不删除旧记录；另开新实验。
trace 只保留最近 64 项工作视图，完整原始记录仍在加密账本。

## 后续仍未实现

已提供可选模型选择与独立审核适配；实际桌面执行、插件间协调、完整记忆检索和预测学习，
仍为后续阶段。第一阶段对照只证明确定性策略影响焦点切换与模拟调用次数，不证明人格自然度、
真实 API 节费或意识。各阶段推广门槛见计划文档。

## 本轮验证结果

18 个状态机单元场景及独立进程测试通过。进程测试核对真实 HV2 载荷、跨进程去重、
恢复不重放、已有非实验目录拒绝，以及注意开关对照。正常示例在对话打断后恢复并仅食用一次；
目标消失记录失败，缺少回执记录未知，休息随运行时间推进且暂停冻结。
固定三个食物的注意对照：开启驻留时切换/模拟调用各 1 次，关闭时各 3 次。
这只是当前合成场景的确定性结果，不外推为真实对话效果或费用节省。
