# 一次回复中的多条消息

模型可以自主选择一条完整长回复，或一次返回多条相邻消息。协议仍为 `version: 1`，旧的单条 `text` 完全保留，不自动按句号或长度拆条。QQ 可以偏向较短的多条交谈，但条界是回复结构，不是角色人格或权限规则。

## v1 格式

旧格式继续有效：

```json
{"version":1,"kind":"reply","text":"这是一条完整回复。","calls":[]}
```

新增可选 `messages`：

```json
{"version":1,"kind":"reply","text":null,"messages":["先说这一点。","还有另一点。"],"calls":[]}
```

校验后保留数组，并生成用于完整审核、会话上下文和兼容消费者的 canonical 文本：

```json
{"version":1,"kind":"reply","text":"先说这一点。\n\n还有另一点。","messages":["先说这一点。","还有另一点。"],"calls":[]}
```

`text` 键仍必填；有有效 `messages` 时可为 null，或与 canonical 文本逐字节一致。若同时给出另一份文字，整体拒绝，不悄悄选其中之一。`messages: null` 按省略处理，兼容要求所有字段出现的结构化服务。`call` 和 `silence` 不接受非 null 的 `messages`，不能将对白藏在操作或沉默中。

## 上限与文本规则

- 1–6 条非空字符串，每条最多 2000 个 Unicode 码点。
- 以两个换行连接后的 canonical 文本最多 6000 个码点，分隔符也计入总量。例如三条分别为 2000、2000、1996 个码点时恰好 6000。
- 不把 UTF-8 字节数或 UTF-16 单元数当作字数；常见中文和单个四字节 emoji 均按一个码点计数。这不是“视觉字形数”：组合字符仍按各自码点计算。
- 拒绝全空白、非法 UTF-8、NUL、C0/C1 控制字符及 DEL。普通制表、回车和换行保留，前后空格不自动删改；不会规范化 Unicode 或替换关键词。
- 任一消息无效，整组拒绝；不发送已通过的前半组，也不退回另一个 `text` 掩盖冲突。

旧兼容后台输出仍可显式允许空的单条文字；用户消息下 `validateOutput` 原有的空回复限制继续执行。允许后台空文字不表示允许空的 `messages` 项。

## 共用接口

实现为 header-only `include/Hibernation/ReplyMessages.h`：

```cpp
struct ReplyMessagesValidation {
    bool ok;
    std::string error;
    nlohmann::json value;
};
nlohmann::json replyMessagesSchema();
ReplyMessagesValidation normalizeReplyMessages(
    nlohmann::json value,
    std::string_view textField = "text",
    bool allowEmptyText = false);
std::vector<std::string> replyMessageParts(
    const nlohmann::json& value,
    std::string_view textField = "text");
```

归一化按值返回，不修改输入。成功后读取 `checked.value`；失败时仅含错误码，不附上私有原文。其他字段保留，来源、工具、权限及完整输出 Schema 仍由既有入口校验。

`replyMessageParts` 先验证完整 bundle，再返回模型原定条界；无 `messages` 时仅返回旧单条，空后台文本返回空 vector。非法 bundle 抛 `std::invalid_argument`，错误码不含回复正文。

`TurnProtocol`、严格服务端 Schema 和 `ha_turn_reply` 标准函数均支持该格式。标准函数依然一次提交完整最终回复，不与实际操作函数混批。经典兼容格式使用同一 `messages`，将 `textField` 传为 `"visible_reply"`：

```json
{"visible_reply":null,"messages":["第一条。","第二条。"],"actions":[],"memories":[],"state_update":{},"stage_proposal":null}
```

## 审核、归档与发送边界

先整体规范化和审核，再落各条记录或顺序发送。审核理解整组完整意思，不能将前一句的指代与后一句拆开审核，也不因分条本身要求改写。单条或多条的选择不修改人物设定、长期记忆权限、工具授权或操作回执。

会话投影缓存保留原定 `messages` 条界，并将同组每一条独立档案 ID 绑定为来源依赖。任一条来源失效或权限撤销时，必须重新构建授权上下文，不能只绑定首条却复用含全部正文的 canonical 缓存。隐藏来源不会删除原始档案或改变所有者历史展示。

归档应保留一个共同 output/group 标识及各条序号；桌面展示与 QQ 顺序发送由对应消费方接入，不能仅因本协议已校验就假定所有条目已送达。多条实际发送出现部分成功时，发送方需要保留各条真实回执，不能重发已经确认成功的消息或把部分完成记为整组完成。

## 同轮可选 feeling

最终 `reply` 或 `silence` 可携带 `feeling`，与整组对白一起提交和审核，不需要再发起模型轮次；`call` 不接受该字段。它仅提出有来源的有限刺激，不直接设置当前数值：

```json
{"version":1,"kind":"reply","text":null,"messages":["我现在挺开心的。","也想再聊一会儿。"],"calls":[],"feeling":{"source_id":"真实经历id","stimulus":{"emotions":{"joy":0.2},"desires":{"company":0.1}}}}
```

情感仅允许 `joy/anger/sadness/worry/affection/disgust/surprise`，愿望仅允许 `rest/explore/company/express/play/replenish`，每轴为 `[-1,1]` 的有符号刺激。来源真实性、授权范围、去重、最多三轴及绝对刺激合计不超过三由情绪核心执行；本格式校验不会取代这些检查。`feeling: null` 表示省略；严格服务为稀疏可选轴返回的已知 null 会在本地 Schema 校验前去除，未知轴、必需字段 null 或缺失仍拒绝。Schema 不使用 AYSerializer 未支持的 `maxProperties`。

标准函数和普通 v1 入口都在解析前保留原始 JSON 重复键检查，不能借 null 归一化吞掉重复轴或冲突文本。经典 `visible_reply` 兼容格式不新增 `feeling` 权限。

## 验证

`tests/ReplyMessages.cpp` 包含 helper、v1、严格 Schema、标准函数与经典兼容格式的专项单元。重点覆盖相同文本、冲突文本、原样条界、Unicode 上限、含分隔总量、控制字/坏编码、call/silence 混条、空后台兼容及无效整组不降级。新增文件需由构建目标登记；单文件编译通过不等于整组端到端发送已验收。此功能不需要额外真实模型连接，合成协议与发送夹具可独立验证。

独立进程专项 `tests/MultiReplyProcess.ps1` 使用完全合成角色、独立存档与本机 provider，覆盖整组审核前不显示、独立归档与 history/request 顺序、坏数组整组拒绝、旧长单条及保存后重启不重复。脚本未实际跑通时不能作为验收完成依据。
`tests/MultiReplyAccess.ps1` 单独检查上述缓存依赖：实际保留来源逐条绑定、缓存条界、派生记忆撤销第二条来源后 actor/reviewer 输入排除其正文、授权重建及原档保留。
