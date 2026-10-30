# 小食堂与 BSimmer 共享料理

食堂插件 0.3 直接使用 `BSimmerCuisine::Kitchen`，不再内置另一个厨房实现。共用 AliyatEngine 库存、BSimmer 背包、菜谱、加工链和存档规则。目录现有 120 种原料、164 道成品、30 种中间加工；详见相邻 BSimmer 项目的 `docs/shared-cuisine.md` 和由数据导出的 `docs/cuisine-cookbook.md`。

## 模型工具流程

1. `canteen.menu` 按 query、cuisine、kind 搜索／分页，一次最多 12 条。观察快照只有少量预览，不能据此断言只有三道菜。
2. `canteen.recipe` 查询实际编号的定量材料、工序、器具、产物、来源与改编标记。
3. `canteen.plan` 按当前仓库计算完整加工链、缺料和缺器具；只读，不申请食材。
4. 可以用 `habitat.gather`／队列 `gather_for_recipe` 按完整链缺料去收集实际桌面物件。它不创造食材，找不到时如实结束；最多八件。
5. 也可查询 `canteen.supplies` 后 `canteen.request_supplies`。仅程序虚拟原料补给，每种 1–4 份、最多八种／16份，30 模拟秒冷却；受配置开关控制，不是现实采购。
6. `canteen.cook` 开始一条完整加工链，当前步骤预留引擎库存。可以同时继续聊天或其他活动；accepted 不等于完成。取消只释放未消费材料。
7. 真正成品出现在 shelf 并有 `canteen.ready`。`canteen.take` 请求桌面发布，实际物件确认后才 `canteen.taken`。之后可以食用或收集入背包／仓库。

`canteen.inventory/store/move_supply/eat_supply/cancel/discard` 沿用已有受控工具入口。记忆中的有限工具摘要保留菜谱、任务编号、具体缺料、虚拟来源和完成标记；不会把开始制作解释为已经吃过。

队列 `canteen_cook`、`take_meal` 与 `gather_for_recipe` 已解除三道旧菜枚举；真实配方编号由 BSimmer 校验。制作等待期限根据完整计划时长加余量，最多 30 分钟。目录／规划超过支持边界时返回明确错误，不靠扩大模型调用轮数伪装任务推进。

## 配置

```json
{
  "plugins": {
    "habitat": { "enabled": true },
    "canteen": {
      "enabled": true,
      "automatic": true,
      "allowSupplyOrders": true,
      "automaticIntervalSeconds": 900,
      "supplyIntervalSeconds": 3600,
      "shelfLifeSeconds": 1800
    }
  }
}
```

`allowSupplyOrders=false` 禁止新的申请，不清空已有原料，也不关闭原本的基础补给。高级配置 `equipment` 是器具 ID 数组，可只授予部分器具；省略表示目录全套。任务活动中更改器具集会被拒绝。原料补给开关和食材来源进入实际操作回执，模型不能用正文绕过。

JSON 配方在编译时嵌入 DLL，改菜谱需要重建并更新食堂及依赖该目录的桌面插件，然后重启后台；只刷新管理页不会更新 DLL。用户设置仍沿原管理页保存／应用流程。

## 表现与存档边界

食物在桌面沿用已有图标模板，但名字、可食用性、营养、配方身份来自共享目录。例如新的面食可复用 bread 图标，不会因此被当作旧 bread 的营养值。没有新画 164 份菜品图标或自动接入专用烹调身体动画；真实制作也不要求持续播放搅拌。

口感、营养与过敏原为游戏属性；sensory.basis 始终为程序模拟，不证明真实身体感觉，也不强制角色夸赞或复述味道。共享目录不改写小爱人设、阶段、小说或原始记忆。

厨房 v1 到 v2 的旧任务、原料预留和交付 ticket 经合成旧档检查；正式档案仍由原有加密库保存。验证从不复制私人聊天。源码同步、候选验证和正式部署分别记录，不能把其中一个当作另一个。

## 进程验证

```powershell
./tests/CuisineProcess.ps1 -ProjectRoot <应用源码> -BuildRoot <已构建目录> -ArtifactRoot <独立验证目录>
```

使用 Mock 与不可见的独立桌面，独立数据目录。检查新工具发现、分页、复杂链缺料／补料、制作中重启、取消、实际计时、桌面发布、采集入仓、消费和再次重启；不访问正式存档、QQ、麦克风或真实模型。
