# 小爱动作与动画制作总表

核对日期：2026-10-29。维护对象：HibernationAgent 的角色行为、相关插件，以及爱丝特兰默认动画包。

这是动画制作和后续接线的统一清单。项目新增、修改或移除动作时，同步更新本表；修改爱丝特兰素材时，同步核对现有素材表。以当前源码和 `character.json` 为依据，不读取私人对话或角色记忆。

## 读表方法

- **现成**：已有行为入口，并有相应动画或明确的动画阶段。
- **复用**：已有行为入口，但爱丝特兰正在使用其他动作的素材。
- **逻辑**：工具或业务已实现，尚未自动触发专用身体动画；通常保持原姿态。
- **规划**：列入素材准备范围，所述场景或行为尚未完整实现。
- **P0**：优先补齐日常交互；**P1**：丰富桌面生活；**P2**：依赖后续场景或新接口。

表中“当前表现”专指爱丝特兰预设。小恐龙等其他预设已有部分独立的接收、准备、说话、进食素材，不能据此认定爱丝特兰也有。能力是否真正可用，还取决于插件启用、权限和模型配置。

“建议制作单元”是资源规划。标注 **拟** 的名称尚不是该业务会自动请求的状态；添加到资源包、或能用 `widget.animate` 手动播放，不代表相应工具已经接好了动画。动画本身也不构成吃过、采集成功、读完或完成任务的证明。

## 角色动作表

### 基础姿态、移动与出入

| 编号 | 动作 | 状态／入口 | 爱丝特兰当前表现 | 建议制作单元与衔接 | 优先级 |
| --- | --- | --- | --- | --- | --- |
| BASE-01 | 默认站立待机 | 现成；`idle` | `idle/south`，17 帧 | 安静闭嘴、小幅身体变化；作为各动作归位基准 | P0，已有 |
| BASE-02 | 原地眨眼 | 现成；`blink`、`widget.animate`／队列 `pose` | `eyes-blind/south`，17 帧 | 保留头、脚锚点，眨眼可嵌入待机变体 | P0，已有 |
| BASE-03 | 疲劳时明显呼吸 | 现成；`breathing` | `breathing/south`，8 帧；不是默认待机 | 闭嘴、轻微肩胸变化，保持原设定的平静表情 | P1，已有 |
| BASE-04 | 停留、等待、不移动 | 现成；队列 `wait`、游走后的停留 | 继续 `idle` | 复用待机及低频小动作，不必再生成一套等待动画 | P0，复用 |
| BASE-05 | 原地观察、好奇小动作 | 复用；`curious`／队列 `pose` | 复用 17 帧待机 | 轻微侧头、视线变化；不必大幅表情或持续摇晃 | P0 |
| BASE-06 | 朝右行走 | 现成；`widget.move`／队列 `move_to`、自动游走 | `walk.loop`，`walking/east` 17 帧 | 原地踏步循环，桌面位移由程序推进 | P0，已有 |
| BASE-07 | 朝左行走 | 现成；同 BASE-06 | 右向行走的水平镜像 | 暂可复用镜像；非对称服饰或道具需要独立素材时再扩展朝向接口 | P0，镜像 |
| BASE-08 | 正面转到侧面 | 现成；`walk.start` | `trun/south` 9 帧 | 脚步与躯干共同转向；入口完成后再开始位置移动 | P0，已有 |
| BASE-09 | 停步并转回正面 | 现成；`walk.end` | 上述转身素材倒序，9 帧 | 侧面停步 → 回正 → `idle`，避免直接切成正面站姿 | P0，已有 |
| BASE-10 | 模型选择额外姿态 | 现成；`widget.animate`、队列 `pose` | 可选状态由当前包与工具参数决定 | 自定义状态可作为表情或姿态，但自动业务触发仍需单独接线 | P1，按需 |
| BASE-11 | 启动时人物登场 | 规划；现有启动画面不是人物专属入场动作 | 有程序启动画面；没有爱丝特兰专属入场序列 | **拟** `appear`：单次入场 → 稳定站姿；与应用启动画面分别制作 | P2 |
| BASE-12 | 隐藏、关闭和重新显示角色 | 逻辑；`widget.show`、托盘、管理页 | 控制窗口可见性，没有离场／再登场表演 | **拟** `disappear`／复用入场；先保证隐藏操作响应，不强制补演旧动作 | P2 |

### 对话与鼠标交互

| 编号 | 动作 | 状态／入口 | 爱丝特兰当前表现 | 建议制作单元与衔接 | 优先级 |
| --- | --- | --- | --- | --- | --- |
| TALK-01 | 新消息预备接收 | 复用；`conversation.received` → `receiving` | `receiving → listening`，帧仍为待机 | 短暂抬头、视线转向；之后进入倾听 | P0 |
| TALK-02 | 安静倾听 | 复用；`listening`、注意力变化 | 复用待机 | 闭嘴、视线稳定，可轻微点头；可与接收共用部分帧 | P0 |
| TALK-03 | 准备回复、思考中 | 复用；`preparing`；`thinking` 为兼容回退名 | `preparing → listening`，帧仍为待机 | 独立思考循环，表情轻微，避免持续张嘴或夸张点头 | P0 |
| TALK-04 | 显示对白、讲话 | 复用；`assistant.reply`、语音播放与 `speaking` | 身体回退 `idle`；当前包没有嘴部叠层 | `speaking.loop`＋嘴部叠层；对白／语音驱动时长，不预先画整段长对话 | P0 |
| TALK-05 | 行走、进食时同时说话 | 现成组合逻辑 | 保留主要身体动作；爱丝特兰尚无口型叠层 | 共用 TALK-04 的嘴部层，不再制作每一种身体动作的“说话版本” | P1 |
| TALK-06 | 沉默、忽略或拒绝请求 | 逻辑；协议允许 `silence`，角色可拒绝 | 没有强制的拒绝姿态 | 可保持待机；**拟**小幅摇头／移开视线是可选表现，不规定每次必须做 | P1，可选 |
| TOUCH-01 | 单击角色后的反应 | 复用；点击手势、短反应缓存 | 小幅反应可进入 `curious`；对白另走原对话呈现 | 受触碰的小动作；一次点击不画成被拽起 | P0 |
| TOUCH-02 | 正面被拽起 | 现成位移＋复用入口；超过拖动阈值才触发 | 程序约 120 ms 抬升，进入 `dragged`；无独立正面拽起片段 | `dragged.start` 可补手脚失重过渡，悬空循环另外制作 | P0 |
| TOUCH-03 | 被拖动、悬空保持 | 现成；`dragged.loop` | `drag-hold/south` 17 帧 | 悬空循环，身体相对锚点稳定；位置跟随鼠标 | P0，已有 |
| TOUCH-04 | 从侧面被拽起并转向正面 | 复用；`dragged_side.start` | 复用反向转身 9 帧、压缩到约 495 ms，再接悬空循环 | 优先补专门的侧面失重 → 正面悬空过渡，不能仅把站立转身加速 | P0 |
| TOUCH-05 | 松手下落 | 现成程序位移；无单独动画状态名 | 约 200 ms 回落，接落地片段 | 程序负责下降轨迹；可准备失重到触地的身体形变，不在帧里再叠加整段位移 | P0 |
| TOUCH-06 | 触地缓冲、站稳 | 现成；`landing.start` | 原 `drag-drop` 从第 005 帧起使用 12 帧，再保持站姿 | 触地 → 屈膝／缓冲 → 站稳；不在触地前插入待机 | P0，已有 |
| TOUCH-07 | 中键／菜单打开输入条 | 逻辑；小组件的输入入口 | 显示并聚焦输入条；不是一个新的人物姿态 | 可复用接收／倾听；不需要单独绘制一整套“输入框动作” | P1，复用 |

### 休息与睡眠

| 编号 | 动作 | 状态／入口 | 爱丝特兰当前表现 | 建议制作单元与衔接 | 优先级 |
| --- | --- | --- | --- | --- | --- |
| REST-01 | 从站立躺下 | 现成；`resting.start` | `on-sleep/south` 9 帧 | 站立 → 降低重心 → 接触地面 → 侧卧 | P0，已有 |
| REST-02 | 躺卧、睡眠保持 | 现成静态姿态；`resting.loop` | `poses/sleeping/south.png` 单帧 | 优先补闭嘴、轻微睡眠呼吸循环；不是站着闭眼 | P0 |
| REST-03 | 从躺卧起身 | 现成；`resting.end` | `walk-up/south` 9 帧 | 侧卧 → 支撑 → 起身 → 默认站姿，脚底／地面接触位置保持一致 | P0，已有 |
| REST-04 | 醒来、结束或中断休息 | 现成流程；`rhythm.wake`、输入／拖动打断 | 正常退出可走起身阶段；紧急状态按优先级打断 | 复用 REST-03；拖拽等紧急情况不强制播完缓慢起身 | P0，复用 |
| REST-05 | 完成一段实际休息 | 现成；`rhythm.rest`、队列 `rest` | 确认姿态后计时，保持 `resting` | 复用躺下／睡眠／起身；无需为每种休息时长制作不同动画 | P0，复用 |

### 桌面小物、食材、背包

| 编号 | 动作 | 状态／入口 | 爱丝特兰当前表现 | 建议制作单元与衔接 | 优先级 |
| --- | --- | --- | --- | --- | --- |
| ITEM-01 | 接近一件小物或资源 | 现成；`habitat.navigate` | 行走及转向，与普通移动共用 | 复用 BASE-06～09；靠近后再执行动作，不在远处播放拿取 | P0，复用 |
| ITEM-02 | 查看、观察小物或植物 | 复用；`habitat.act(look)`、`look_nearest`、队列 `interact` | 走近后 `curious`，素材仍为待机 | 低头、侧头或俯身观察，可复用通用观察姿态 | P1 |
| ITEM-03 | 吃掉桌面小物 | 复用；`habitat.act(eat)`、`eat_nearest` | 有 `eating` 状态与实际物件挂接，但身体素材复用待机 | 拿到嘴边 → 小口进食循环 → 放下手；保留持物挂点和前臂遮挡层 | P0 |
| ITEM-04 | 捡起、收集桌面食材 | 复用；`habitat.act(collect)` | 走近后使用 `curious`，完成后物件退场并入包 | **拟** `collecting`：俯身／伸手 → 拿起 → 收好 → 归位 | P0 |
| ITEM-05 | 连续收集附近或指定区域食材 | 现成组合；`habitat.gather`、队列 `gather` | 程序连续选择目标，组合行走和 ITEM-04 | 复用行走、拿取、入包；不是一个很长且不可打断的整段动画 | P0，组合 |
| ITEM-06 | 为某道料理收集缺少的食材 | 现成组合；`recipe_id`、`gather_for_recipe` | 依据真实库存确定缺料，再执行采集 | 复用 ITEM-05；不能用“备料动画”代替真的收集与入库 | P1，组合 |
| ITEM-07 | 种下种子 | 复用；`habitat.act(plant)` | 走近后使用 `curious`，种子实际变为芽苗 | **拟** `planting`：蹲下 → 放种子／覆土 → 收手站起；铲子为可选道具 | P1 |
| ITEM-08 | 挪动、布置已有小物 | 复用；`habitat.arrange`、队列 `arrange` | 角色接近原位置，物件缓慢挪动；没有抱物随行 | 轻推／放置动作；抱起后携带到目的地属于后续接线 | P1 |
| ITEM-09 | 创造、放出虚拟小物 | 逻辑；`habitat.create/place`、队列 `create_drop` | 物件按生命周期出现，没有对应的角色放置动作 | 可复用通用拿放／展开手掌，须与实际创建回执绑定 | P1 |
| BAG-01 | 查看背包、查看仓库 | 逻辑；`canteen.inventory/observe` | 查询真实库存，没有背包窗口或翻包动作 | **拟**查看背包：打开 → 低头查看 → 收好；不必每次查询都演 | P1 |
| BAG-02 | 把背包物资存入仓库 | 逻辑；`canteen.store`、`store_supplies` | 库存原子转移，没有走到实体仓库或开柜动作 | 通用取放动作；实体仓库的接近／柜门／存放锚点待接 | P1 |
| BAG-03 | 从仓库取出物资 | 逻辑；`canteen.move_supply` | 真实物品在容器间移动，没有实体取物动作 | 与 BAG-02 共用拿放素材，按方向和道具改变组合 | P1 |
| BAG-04 | 直接吃背包或仓库中的食物 | 逻辑；`canteen.eat_supply`、`eat_inventory` | 实际扣除食物并记录经历，尚未自动绑定 `eating` 身体姿态 | 接上 ITEM-03；取物 → 进食 → 收手，不新增一套完全重复的吃法 | P0 |
| BAG-05 | 手持／抱着物品走动 | 规划身体表现；库存持有本身已实现 | 没有“背包里有东西就自动抱在手里”的持续表现 | **拟** `carrying`：携物站立／行走；长距离持物和多种道具挂接需业务接线 | P2 |

### 食堂

| 编号 | 动作 | 状态／入口 | 爱丝特兰当前表现 | 建议制作单元与衔接 | 优先级 |
| --- | --- | --- | --- | --- | --- |
| CAFE-01 | 看菜单、检查原料和成品 | 逻辑；`canteen.observe` | 返回实际食堂数据，没有菜单道具动作 | 可复用阅读／查看姿态；菜单是可换道具 | P1 |
| CAFE-02 | 制作面包、布丁或泡茶 | 逻辑；`canteen.cook`、`canteen_cook` | 预留原料并计时制作，没有角色站在料理台前的动画 | **拟** `cooking`：准备 → 搅拌／操作循环 → 收尾；三道料理优先更换道具而非重做整个人物 | P1 |
| CAFE-03 | 等待料理完成 | 现成后台过程 | 食堂独立推进；角色可以去做别的事情 | 通常复用待机或其他活动，不让整个制作时长都强制卡在搅拌循环 | P1，复用 |
| CAFE-04 | 从成品架取餐 | 逻辑；`canteen.take`、`take_meal` | 成品实际发布到桌面，没有端盘到座位的身体流程 | 通用拿取／端盘；需另接携带、餐桌锚点和放盘 | P1 |
| CAFE-05 | 在食堂坐下就餐 | 规划；已有食堂制作和两种食用逻辑 | **没有独立食堂场景、座位或坐姿就餐流程** | 前往座位 → 坐下 → 放盘 → 坐姿进食 → 收盘 → 起身；可先准备坐下／坐姿吃饭／起身素材 | P2 |
| CAFE-06 | 取消制作、丢弃成品 | 逻辑；`canteen.cancel/discard` | 真实取消或丢弃，没有对应身体动作 | 取消通常无须演；丢弃可复用拿放，垃圾桶道具和位置待接 | P2，可选 |

### 阅读、创作、游戏与其他工具

| 编号 | 动作 | 状态／入口 | 爱丝特兰当前表现 | 建议制作单元与衔接 | 优先级 |
| --- | --- | --- | --- | --- | --- |
| READ-01 | 搜索感兴趣的资料 | 逻辑；`reading.search`、`reference_search` | 搜索结果有真实来源，没有检索身体动作 | 可复用思考／观察；不为一次搜索另做完整动作 | P1，复用 |
| READ-02 | 阅读资料、继续读取正文片段 | 逻辑；`reading.open`、`reference_read` | 实际取得有限文字片段，不自动切身体姿态 | **拟** `reading`：拿书／展开材料 → 阅读／偶尔翻页 → 收好；文字读取成功不等于已读完整本书 | P1 |
| READ-03 | 在阅读角看书 | 规划场景；读取文字能力已存在 | **没有阅读角、书架、座位、走到阅读角的完整流程** | 走近 → 取书 → 坐下 → 阅读循环 → 合书 → 放回 → 起身；复用 READ-02 加坐姿和场景道具 | P2 |
| ART-01 | 写短文、整理草稿、接写作品 | 逻辑；`atelier.write/append`、`creative_note/creative_continue` | 实际保存受限工作区草稿，没有书写身体动画 | **拟** `writing`：拿笔 → 短书写循环 → 停笔；允许停顿，不要求一直高速写字 | P1 |
| ART-02 | 重读自己的作品 | 逻辑；`atelier.read`、`creative_read` | 读取真实作品的当前内容，没有读稿身体姿态 | 复用 READ-02，道具换成纸张／笔记本 | P1，复用 |
| ART-03 | 画像素画、修改颜色、制作作品动画 | 逻辑；`pixel.create/edit/palette/frame/undo/redo` | 能修改真实画布，没有角色在画桌前作画的姿态 | **拟** `drawing`：拿工具 → 小幅绘画循环 → 看一眼作品 → 收手；不按每个像素播放一次 | P1 |
| ART-04 | 查看、导出、展示自己的作品 | 逻辑／物体显示已实现；`pixel.read/preview/export/show/list` | 可查看真实作品、导出、显示独立桌面摆件；无人物举画动作 | 可选举起／展示小物的通用动作，预览和保存通常不必额外演 | P2，可选 |
| PLAY-01 | 开始、观察、操作或放下九盏灯 | 逻辑；`pocket.start/observe/press/leave`、`puzzle_start/puzzle_press/puzzle_leave` | 有真实棋盘和版本进度，**没有额外棋盘窗口或专用身体动作** | **拟** `playing`：低头看小棋盘／手指轻点／停顿思考；胜利反应可选，不必夸张庆祝 | P1 |
| MEDIA-01 | 找影片、打开小电视或平台页面 | 逻辑；`television.search/sources/platforms/open/open_page` | 小电视可真实打开内容，没有人物使用遥控器的动作 | 可复用伸手／操作道具；遥控器为可选素材 | P1 |
| MEDIA-02 | 看视频、查看当前画面或连续采样帧 | 逻辑；`television.observe/observe_sequence` | 能按能力取得画面，没有自动走到电视前或观影姿态；不是连续视觉直播 | **拟** `watching`：朝电视方向安静观看；坐姿、注意力变化待接，不能用循环动画冒充一直看懂影片 | P1 |
| MEDIA-03 | 播放、暂停、停止、跳转、收起电视 | 逻辑；`television.control` | 控制实际播放器，没有对应身体手势 | 复用 MEDIA-01，不必为每个播放按钮制作一套动作 | P2，复用 |
| TOOL-01 | 看当前桌面、截图、列目录、打开授权程序 | 逻辑；`computer.screenshot/list_directory/open_application` | 按权限实际执行，没有持续眼球追踪、指屏或操作键盘的身体动画 | 通用观察／思考姿态；只有取得真实图像和视觉能力时才能据图表现 | P2，复用 |
| TOOL-02 | 查看天气 | 逻辑；`weather.current` | 天气数据影响既有连续状态，没有自动撑伞、淋雨或外出场景 | 可选抬头看天气／窗外的小动作，不强制把天气映射成固定表情 | P2，可选 |
| TOOL-03 | 闲时整理、形成打算、执行连续任务、调用 Skill 或咨询导师 | 逻辑；既有独处、任务与工具系统 | 行为取决于具体工具／队列，不存在统一的“正在工作”身体动作 | 复用思考、待机和实际子动作；只为新的可见身体行为新增资源，不为每种工具名新增动画 | P1，组合 |

## 爱丝特兰现有资源对照

以下是已核对的实际配置，不是建议帧数。资源根目录为 [`plugins/widget/characters/asterlan`](../plugins/widget/characters/asterlan/character.json)。`start`／`loop`／`end` 是同一状态的进入、保持和离开片段。

| 状态键 | 进入 start | 保持 loop | 离开 end | 实际素材与缺口 |
| --- | --- | --- | --- | --- |
| `idle` | 无 | 17 帧／2833 ms | 无 | `frames/idle/south`；默认待机 |
| `breathing` | 无 | 8 帧／1333 ms | 无 | `frames/breathing/south`；仅作疲劳呼吸 |
| `blink` | 无 | 17 帧／2833 ms | 无 | `frames/eyes-blind/south` |
| `walk` | 9 帧／1500 ms | 17 帧／2833 ms | 9 帧／1500 ms | `trun/south` → `walking/east` → 倒序 `trun/south`；左右通过镜像处理 |
| `dragged` | 无 | 17 帧／2833 ms | 无 | `frames/drag-hold/south`；缺正面拽起专门入口 |
| `dragged_side` | 9 帧／495 ms | 17 帧／2833 ms | 无 | 入口复用反向转身，循环复用拖动；缺专门侧面拽起素材 |
| `landing` | 12 帧／2000 ms | 单帧／200 ms | 无 | 从 `frames/drag-drop/south/005.png` 开始，接 `poses/standing/south.png` |
| `resting` | 9 帧／1500 ms | 单帧／1200 ms | 9 帧／1500 ms | `on-sleep/south` → `poses/sleeping/south.png` → `walk-up/south`；缺睡眠呼吸循环 |
| `listening` | 无 | 17 帧／2833 ms | 无 | 直接复用 `idle/south`，并非独立倾听素材 |
| `eating` | 无 | 17 帧／2833 ms | 无 | 直接复用 `idle/south`，并非独立进食素材 |
| `curious` | 无 | 17 帧／2833 ms | 无 | 直接复用 `idle/south`，并非独立观察素材 |
| `receiving` | — | 回退 `listening` | — | 当前没有独立状态素材 |
| `preparing` | — | 回退 `listening` | — | 当前没有独立状态素材 |
| `thinking` | — | 回退 `preparing` | — | 兼容回退名，不是另一个已绘制的思考动作 |
| `speaking` | — | 回退 `idle` | — | 当前没有独立说话素材；包中也没有 `mouth_open` |

转身原始第 2 帧已恢复使用。落地则仍有意跳过原导出开头五帧站立准备；不要把这两件事混为一条“跳帧规则”。

## 少量环境与物件素材

| 编号 | 物体／环境 | 当前实现 | 可制作的素材 | 接入说明 |
| --- | --- | --- | --- | --- |
| ENV-01 | 食材和掉落物出现、到期淡出 | 已有程序透明度变化与像素模板 | 少量出生／落下／消失小特效 | 不把淡出画成固定实底背景；自定义物件序列接口仍需另接 |
| ENV-02 | 食材被拿起、被吃掉 | 已有真实移除和短退场效果 | 小幅被拿起、碎屑或收纳效果 | 必须与实际完成结果同步，不能先演没了再回报失败 |
| ENV-03 | 种下后的芽苗生长 | 已有新芽、叶子、花苞、开花四种表现 | 四阶段像素形态及可选短过渡 | 当前不是完整种植收获系统，不能据此默认长出可采的苹果或其他食材 |
| ENV-04 | 食堂道具 | 料理可在成品架保存、取出后显示到桌面；无独立食堂布景 | 面包／布丁／茶、盘子／杯子、搅拌器皿、桌椅 | 容器数量与料理种类可替换道具；角色动画和场景锚点需要接线 |
| ENV-05 | 阅读角、背包、仓库道具 | 数据能力部分已具备；这些实体布景尚未实现 | 一本可开合的书、纸张、背包、柜门、椅子 | 优先制作通用少量道具；书架和仓库场景不属于现成角色状态 |
| ENV-06 | 浮动小电视 | 已有电视边框与实际视频／网页内屏 | 可选电视外壳、遥控器、状态灯 | 视频内容由播放器呈现，不制作成角色动画包的整段帧 |
| ENV-07 | 像素创作台作品 | 可显示独立桌面作品及最多 8 帧的作品动画 | 自制画作、小灯、装饰等 | 作品动画属于 `pixel-canvas`，不会自动变成食材或替换角色身体动画 |

## 制作与接入约定

1. **先补当前缺口。** 建议优先制作 ITEM-03 进食、ITEM-04 拿取入包、TALK-01～04 接收／倾听／准备／说话、REST-02 睡眠呼吸，以及 TOUCH-04 侧面被拽起。现有待机、行走、躺下和起身可以沿用，不必全部重做。
2. **按可复用动作单元生产。** 批量采集、备料、阅读角看书和食堂就餐都是组合流程。拿取、收纳、坐下、起身、阅读、进食及道具尽量复用；不要把完整流程画成一条几分钟的序列。
3. **先保持现有规格。** 爱丝特兰包为 168×168 逻辑画布，锚点 `[84,144]`，当前导入基准为 6 FPS。新增帧可按各自 `ms` 调整；循环末帧应能回到首帧。保持地面接触点和头身比例，避免帧间缩放、透明留白变化或重心跳动。
4. **方向先满足实际渲染。** 当前常用正面和右侧面，左侧采用镜像；尚未自动按八方向切换角色帧。非对称衣饰、文字或道具不要默认镜像无差异。
5. **业务时长与动画分开核对。** 当前接近后的食用约 1.6 s、收集／观察约 0.8 s、种植约 2.2 s、摆放约 2 s。仅增加图片帧数不会自动延长这些业务时长；新动作较长时要同步调整程序衔接或动作完成判定。
6. **手持物分层。** 已有帧内 `hand` 挂点与 `front` 前景层支持。优先把手、身体与食物分开，食材替换道具；持续抱物、书本、工具等通用挂接仍需业务适配。当前只有开闭嘴叠层入口；五元音口型可以预备素材，但音素驱动尚未接入。
7. **资源名不是接线证明。** `collecting`、`planting`、`reading`、`writing`、`drawing`、`cooking`、`watching`、`carrying` 等均为本表建议名。决定采用时再核对状态注册、工具／事件触发、退出条件和回退。不要只添加 PNG 和状态名就把表改成“现成”。
8. **不要一行注册一个状态。** 当前包上限为 32 个状态，每状态最多 8 个变体；每个片段最多 256 帧，整包最多 4096 帧、512 个 PNG 文件。过渡优先放进现有状态的 `start/end`，表情小变化放变体，必要时复用图集。详细限制见[序列帧规范](pixel-sequences.md)。
9. **保持响应与事实一致。** 新输入、拖拽等紧急操作允许中断；不排队补演过时反应。吃、采、做饭等结果以业务回执为准，不能用动画播放完成代替真实执行。多种动作重叠时要说明身体、口型、手持物和气泡的优先级。

## PixelLab 制作索引：State 与 Animation 分开

本节新增于 2026-10-29。前面的“当前实现”仍然有效；以下是**制作提示词和素材衔接设计**，不表示这些新素材已经生成或自动接线。每项动作都列出起止 State、播放方式和可复制的英文提示词；复用提示词也展开显示，避免制作时来回找。

快速跳转：[角色 State](#pixellab-character-states) · [物件 State](#pixellab-object-states) · [逐项动作提示词](#pixellab-animations)。例如休息为 `S01→S07` 躺下（单次）、`S07→S07` 睡眠（循环）、`S07→S01` 起身（单次），三段分别制作。

### 如何填写

1. **Create State 只做静态姿态。** 把下面“角色 State 通用前缀”和该行的静态提示词合并填写；不要把“先走过去再坐下”这种流程填进 State。已有正确站姿、侧卧姿态直接复用，无须重做。这里的 S 编号是制作索引，不是程序新增的状态键。
2. **Animation 用自定义动作描述。** 从起点 State 的对应朝向开始，复制动作行的英文提示词，逐段生成，不选预设动作。A→B 为一次过渡；A→A 可能是循环，也可能是一次眨眼／点按，以“播放”列为准。循环的末尾应回到起始姿态和运动相位。
3. **终点参考不是口头保证。** 本表列的两个 State 是制作所需的起点和终点关键姿态。若当前页面支持终帧图片，使用终点 State 的同尺寸、同朝向图片；没有该控件时，用描述说明终点，生成后检查末帧，必要时换支持起止帧的生成入口。不能仅因输入了两个名称就认为已经匹配了首末帧。官方将角色 State 定义为姿态／服装等变体，将动画单独提供；其文本动画 v3 公开支持起始帧和可选终帧，具体网页入口和模式以实际控件为准。[PixelLab 官方功能说明](https://www.pixellab.ai/pixellab-api)
4. **提示词格式。** 每行动画提示词已包含保持像素风格、比例和固定镜头的要求，可单独复制。State 名和编号用于查表，不要把整张 Markdown 表一起粘贴进描述框。物件 O 编号是独立对象素材，用物件生成／物件动画入口，不混入人物 Create State。
5. **先统一基准。** 使用已有爱丝特兰参考，保持约两头身、原来的耳朵、面部和衣服；不重新设计。当前导入画布 168×168、6 FPS。静态图与动画保持同画布和对齐，角色站姿脚底对齐 `[84,144]`，卧姿沿同一地面线；悬空姿态保留相对基准。先试一段检查像素边缘和脚底，不要逐帧自动裁切／放大。建议普通小循环先试 8 帧、短过渡先试 8～12 帧，这是制作起点而非网站通用限制；现有 9／17 帧素材不用为此重做。
6. **一次只制作一段。** 表内多个片段按编号分别生成。组合动作的循环段可按业务时长重复；点击、按键、放种子等 A→A 单次片段不得自动连播。出现／消失、角色位移和道具真实消耗由程序处理，不生成一个“空白角色 State”。
7. **分层与朝向。** 食物／书／笔／遥控器等可在生成时用简洁占位物约束手势，接入前拆成身体、实际道具、手部遮挡层并核对挂点，避免画出两个物件。右行使用 east，正面使用 south，左行现阶段整体镜像。下表说“朝右”均指屏幕右侧，不是角色自身右手方向。
8. **资源优先级。** 先制作现有 P0 缺口：进食、拿取入包、对话接收／准备、睡眠保持、侧面拽起。书桌、食堂座位、仓库等属于素材储备，仍需场景和事件接线。下列 State 是按需素材库，不应全部直接注册为运行时状态；运行时仍受 32 状态等限制。

<a id="pixellab-character-states"></a>

### 角色 State 提示词（静态，单独制作）

下面每行 = **本段通用前缀＋该行英文文本**。通用前缀不包含动作过程，适用于从已有爱丝特兰角色创建静态变体；朝向按素材行选择。需要匹配现有睡姿或手势时，以已修复参考为准。

```text
Keep the exact reference character identity, approximately two-head-tall proportions, pointed elf ears, hair, face, layered butler dress, colors and crisp pixel-art clusters. One full-body character in one static pose, fixed camera, unchanged pixel scale and canvas alignment, transparent background. No cat ears, animal tail, extra characters, panels, sprite-sheet grid, text or scenery. Preserve the calm slightly blank AI-like expression unless closed eyes are specified. Do not redesign the character.
```

| State 编号 | 名称／用途 | 静态提示词：接在通用前缀后 | 制作说明 |
| --- | --- | --- | --- |
| `S01` | `front_idle｜正面默认站姿` | `Facing south, standing upright with feet grounded and hands resting naturally in front, eyes open, mouth closed, a calm slightly blank expression.` | 复用现有 standing／idle 基准。 |
| `S02` | `right_stand｜右侧站姿` | `Facing east in a clear right-side profile, standing still with feet grounded and arms relaxed, mouth closed.` | 可取行走侧面基准帧，检查是否处于双脚稳定支撑相位。 |
| `S03` | `front_suspended｜正面悬空` | `Facing south, suspended as if gently held from above, shoulders slightly lifted, arms and legs hanging loosely, feet off the ground, mouth closed. Do not draw a hand, string or hook.` | 复用现有 drag-hold 合适基准。 |
| `S04` | `right_suspended｜侧面悬空` | `Facing east in profile, suspended as if gently held from above, loose arms and dangling feet, mouth closed. Do not draw a hand, string or hook.` | 按需新建；尚非已接线动作。道具为占位时需拆层。 |
| `S05` | `fall_ready｜正面落下准备` | `Facing south, feet pointing down with knees slightly bent, arms slightly out for balance, ready to touch the ground, mouth closed. Keep the body in the reference canvas, without a drawn ground or motion trail.` | 按需新建；尚非已接线动作。道具为占位时需拆层。 |
| `S06` | `landing_crouch｜触地缓冲` | `Facing south, both feet on the baseline, knees bent in a shallow landing crouch, torso slightly lowered and hands balancing beside the body, mouth closed.` | 按需新建；尚非已接线动作。道具为占位时需拆层。 |
| `S07` | `sleeping_side｜侧卧睡姿` | `Lying fully on one side on the same ground baseline, head resting near folded hands, knees gently bent, eyes closed and mouth closed. The whole body is horizontal; this is not standing with closed eyes.` | 复用现有 sleeping 侧卧图，禁止用站着闭眼的预设代替。 |
| `S08` | `ground_reach｜蹲下伸手` | `Facing south, crouching with knees bent and one hand reaching toward a small object near the feet, the other hand balancing the body, eyes looking down, mouth closed.` | 按需新建；尚非已接线动作。道具为占位时需拆层。 |
| `S09` | `hold_front｜胸腹前持物` | `Facing south, standing upright with both hands cupped at waist height as if holding one small item, elbows relaxed, eyes open and mouth closed. Keep the hands clearly readable; no large prop or bag covering the body.` | 按需新建；尚非已接线动作。道具为占位时需拆层。 |
| `S10` | `food_near_mouth｜站姿食物靠嘴` | `Facing south, standing upright, one hand holding a small plain food placeholder just below the lips, the other hand relaxed, mouth gently closed. No wide open mouth and no inflated cheeks.` | 按需新建；尚非已接线动作。道具为占位时需拆层。 |
| `S11` | `bag_open｜查看随身包` | `Facing south, standing with a small plain open pouch at waist height, one hand holding its rim, the other near the opening, eyes looking down, mouth closed.` | 按需新建；尚非已接线动作。道具为占位时需拆层。 |
| `S12` | `storage_reach｜平伸手取放` | `Facing south, standing upright with one arm reaching forward at waist height and the other hand ready to support an item, mouth closed. Do not draw a cupboard, room or background.` | 按需新建；尚非已接线动作。道具为占位时需拆层。 |
| `S13` | `carry_right｜右侧持物站姿` | `Facing east in profile, standing upright with both hands supporting one small plain item at waist height, elbows close to the torso, feet grounded, mouth closed.` | 按需新建；尚非已接线动作。道具为占位时需拆层。 |
| `S14` | `palms_open｜摊掌展示` | `Facing south, standing upright with one palm open at waist height and the other hand relaxed, eyes open and mouth closed. Keep the palm empty and readable.` | 按需新建；尚非已接线动作。道具为占位时需拆层。 |
| `S15` | `cooking_ready｜料理操作准备` | `Facing south, standing with both forearms raised toward an imaginary work surface at waist height, one hand ready to stir and the other ready to steady a bowl, mouth closed. No table, kitchen or background.` | 按需新建；尚非已接线动作。道具为占位时需拆层。 |
| `S16` | `chair_sitting｜椅上坐姿` | `Facing south, sitting upright on an invisible chair with thighs supported horizontally and lower legs pointing down, hands resting near the lap, mouth closed. Do not draw the chair or floor.` | 按需新建；尚非已接线动作。道具为占位时需拆层。 |
| `S17` | `seated_food｜坐姿食物靠嘴` | `Facing south, seated upright on an invisible chair, one hand holding a small plain food placeholder below the lips and the other near the lap, mouth gently closed. Keep exactly the same seat height as the seated reference; no chair or background.` | 按需新建；尚非已接线动作。道具为占位时需拆层。 |
| `S18` | `book_closed｜站姿合书` | `Facing south, standing upright while holding one small plain closed book at waist height with both hands, mouth closed. No writing or symbols on the cover.` | 按需新建；尚非已接线动作。道具为占位时需拆层。 |
| `S19` | `book_open｜站姿开书` | `Facing south, standing upright while holding one small plain open book at chest-to-waist height, eyes looking at its pages, mouth closed. No legible text on the pages.` | 按需新建；尚非已接线动作。道具为占位时需拆层。 |
| `S20` | `seated_reading｜坐姿开书` | `Facing south, seated upright on an invisible chair, holding one small plain open book above the lap, eyes looking down at the pages, mouth closed. Keep the same seat height as the seated reference; no chair or background.` | 按需新建；尚非已接线动作。道具为占位时需拆层。 |
| `S21` | `writing_ready｜书写准备` | `Facing south, standing with one hand holding a small plain pen above an imaginary writing surface at waist height and the other hand steadying a small sheet, eyes lowered, mouth closed. No desk or background.` | 按需新建；尚非已接线动作。道具为占位时需拆层。 |
| `S22` | `drawing_ready｜绘画准备` | `Facing south, standing with one hand holding a small plain stylus above an imaginary drawing surface at waist height and the other hand near its edge, eyes lowered, mouth closed. No desk or background.` | 按需新建；尚非已接线动作。道具为占位时需拆层。 |
| `S23` | `show_art｜举起作品` | `Facing south, standing upright and holding a small plain rectangular picture panel at chest height with both hands, eyes open and mouth closed. Keep the panel below the face; leave its surface blank.` | 按需新建；尚非已接线动作。道具为占位时需拆层。 |
| `S24` | `puzzle_ready｜小棋盘准备` | `Facing south, standing with a small plain board supported at waist height by one hand and the other index finger hovering above it, eyes looking down, mouth closed.` | 按需新建；尚非已接线动作。道具为占位时需拆层。 |
| `S25` | `remote_ready｜遥控操作准备` | `Facing south, standing with one hand holding a small plain remote at waist height, thumb resting on top, the other arm relaxed, mouth closed.` | 按需新建；尚非已接线动作。道具为占位时需拆层。 |
| `S26` | `watch_right｜朝右观看` | `Facing east with a slight three-quarter turn so the face remains readable, standing still with arms relaxed, eyes directed toward a screen to the right, mouth closed. No television or background.` | 按需新建；尚非已接线动作。道具为占位时需拆层。 |
| `S27` | `attentive｜正面注意` | `Facing south, standing with hands relaxed, head lifted very slightly and eyes looking forward attentively, mouth closed. Keep a calm slightly blank expression, without a smile or surprise.` | 按需新建；尚非已接线动作。道具为占位时需拆层。 |
| `S28` | `thinking｜正面思考` | `Facing south, standing with one hand resting lightly near the chin, gaze slightly lowered, mouth closed. Keep the expression quiet and slightly blank, not worried or exaggerated.` | 按需新建；尚非已接线动作。道具为占位时需拆层。 |
| `S29` | `curious｜正面观察` | `Facing south, standing with a very slight head tilt and eyes directed a little downward, hands relaxed, mouth closed. Keep both feet on the same baseline and the expression restrained.` | 按需新建；尚非已接线动作。道具为占位时需拆层。 |

<a id="pixellab-object-states"></a>

### 物件 State／参考形态提示词（独立对象）

物件制作通用前缀：`One isolated pixel-art object, crisp pixel clusters matching the character palette, fixed camera and unchanged pixel scale, transparent background, no text, no scene or sprite-sheet grid. Preserve the same object identity and anchor across variants.` 然后接该行文本。O 编号不是人物姿态，不占用人物运行时状态名。O01 苹果、O06 面包、O13 月灯只是明确的制作样例；换物种或作品时从它自己的参考图生成，不能把苹果变面包当作常规过渡。

| State 编号 | 名称／用途 | 静态提示词：接在物件前缀后 |
| --- | --- | --- |
| `O01` | `drop_rest｜小物稳定` | `One small plain apple ingredient, resting upright, compact pixel silhouette, no face, no character, no ground shadow baked into a background.` |
| `O02` | `sprout｜新芽` | `One tiny planted sprout with two small cotyledons and a short stem, isolated without a pot, keep its root base at the reference anchor.` |
| `O03` | `leaves｜长叶` | `The same plant with a slightly taller stem and several small leaves, same root base and scale, no flower yet, no pot.` |
| `O04` | `bud｜花苞` | `The same leafy plant bearing one closed flower bud, same root base and scale, no open petals, no pot.` |
| `O05` | `flower｜开花` | `The same plant with one modest open flower, same root base and scale, no fruit, no pot.` |
| `O06` | `meal｜成品料理` | `One small plain bread roll on a simple plate, readable at desktop sprite size, no steam cloud covering the food, no character or table.` |
| `O07` | `bowl｜料理器皿` | `One small plain mixing bowl with a spoon resting inside, front view at the character hand height reference, no character, table or background.` |
| `O08` | `book_closed｜合上的书` | `One small plain closed book, front three-quarter view, simple cover without writing, preserve the reference binding edge and footprint.` |
| `O09` | `book_open｜打开的书` | `The same book opened at the middle, small blank pages, same binding anchor and pixel scale, no readable text.` |
| `O10` | `bag_closed｜合上的包` | `One small plain fabric pouch with its opening closed, simple silhouette, no logos, no character or background.` |
| `O11` | `bag_open｜打开的包` | `The same small pouch with its opening parted, same base anchor and width, no loose items, character or background.` |
| `O12` | `tv_shell｜电视外壳` | `One small pixel television frame, front view, a clean empty rectangular inner screen reserved for the application video layer, short feet and a small status light, no video image or character.` |
| `O13` | `art_lamp｜桌面作品示例` | `One small pixel moon lantern with a compact frame, a pale light inside and a tiny crescent ornament, isolated, no character, text or background.` |

<a id="pixellab-animations"></a>

### 每个动作的 Animation 提示词与起止 State

相同的“片段键”表示同一份可复用资源，提示词仍逐行动作展开。片段键是制作建议名，尚未自动成为 `character.json` 的新状态。两列 State 分别对应**第一张关键姿态→最后一张关键姿态**；循环只重复当前行，不把整条组合行为无条件重复。**单次 A→A 不是持续循环。** 表中正常串行流程的相邻 State 应衔接；明确写“不同素材／非串行”的动作按说明选择分支。

嘴部样片 `mouth` 用正面静止基准生成后只保留嘴部区域；不会直接替换身体状态。侧脸嘴层须另按侧面参考制作对齐。动画提示词描述小幅运动，不保证生成精确锚点；导出时仍检查首末帧、脚底、脸部像素与透明边缘。

#### BASE-01 默认站立待机

沿用现有素材即可。

| 顺序／片段键 | 起点 State | 终点 State | 播放 | 自定义 Animation 提示词（可直接复制） |
| --- | --- | --- | --- | --- |
| 1. `idle` | `S01` | `S01` | 循环 | `Same reference character, crisp pixel art, fixed camera and unchanged proportions. Stand quietly in place with only tiny natural settling of the hair and sleeves. Keep the mouth closed, eyes calm, feet planted and head height nearly constant. Return exactly to the starting pose; no deep breathing or swaying.` |

#### BASE-02 原地眨眼

单次眨眼，间隔由程序决定，不连续快速重复。

| 顺序／片段键 | 起点 State | 终点 State | 播放 | 自定义 Animation 提示词（可直接复制） |
| --- | --- | --- | --- | --- |
| 1. `blink` | `S01` | `S01` | 单次 | `Same reference character, crisp pixel art, fixed camera and unchanged proportions. Close both eyelids gently once, hold briefly, then reopen them to the original calm gaze. Keep the head, hands, body and feet still; mouth closed throughout.` |

#### BASE-03 疲劳时明显呼吸

仅疲劳明显时使用；不可替换普通待机。

| 顺序／片段键 | 起点 State | 终点 State | 播放 | 自定义 Animation 提示词（可直接复制） |
| --- | --- | --- | --- | --- |
| 1. `fatigue` | `S01` | `S01` | 循环 | `Same reference character, crisp pixel art, fixed camera and unchanged proportions. Show a small tired breathing cycle through a subtle shoulder lift and release. Keep the lips sealed throughout, no panting, no mouth opening and no large chest motion. Return to the same standing pose.` |

#### BASE-04 停留、等待、不移动

复用 BASE-01；不生成一条很长的等待片段。

| 顺序／片段键 | 起点 State | 终点 State | 播放 | 自定义 Animation 提示词（可直接复制） |
| --- | --- | --- | --- | --- |
| 1. `idle` | `S01` | `S01` | 循环 | `Same reference character, crisp pixel art, fixed camera and unchanged proportions. Stand quietly in place with only tiny natural settling of the hair and sleeves. Keep the mouth closed, eyes calm, feet planted and head height nearly constant. Return exactly to the starting pose; no deep breathing or swaying.` |

#### BASE-05 原地观察、好奇小动作

小幅观察，可只播入口与出口。

| 顺序／片段键 | 起点 State | 终点 State | 播放 | 自定义 Animation 提示词（可直接复制） |
| --- | --- | --- | --- | --- |
| 1. `curious_in` | `S01` | `S29` | 单次 | `Same reference character, crisp pixel art, fixed camera and unchanged proportions. Make one small head tilt and lower the gaze toward a nearby object, ending in the curious reference pose. Do not step, smile or open the mouth.` |
| 2. `curious_loop` | `S29` | `S29` | 循环 | `Same reference character, crisp pixel art, fixed camera and unchanged proportions. Hold the small head tilt and quietly inspect a point below, with a tiny eye movement and a brief pause. Keep both feet planted and the mouth closed. Return to the same curious pose.` |
| 3. `curious_out` | `S29` | `S01` | 单次 | `Same reference character, crisp pixel art, fixed camera and unchanged proportions. Bring the head and eyes calmly back to the default front-facing standing pose, hands relaxed and mouth closed. No extra gesture.` |

#### BASE-06 朝右行走

行走循环时由程序推进坐标；同右朝向续走无需重复转身。

| 顺序／片段键 | 起点 State | 终点 State | 播放 | 自定义 Animation 提示词（可直接复制） |
| --- | --- | --- | --- | --- |
| 1. `turn_right` | `S01` | `S02` | 单次 | `Same reference character, crisp pixel art, fixed camera and unchanged proportions. Turn the whole body from facing front to facing right using two small repositioning steps. Lift the leading foot, pivot the hips with the torso, then bring the trailing foot around. End in the right-facing standing pose; do not twist the upper body over stationary legs.` |
| 2. `walk` | `S02` | `S02` | 循环 | `Same reference character, crisp pixel art, fixed camera and unchanged proportions. Walk in place facing right through one complete alternating step cycle. Lift and place each foot clearly, coordinate the arms and skirt, and keep the torso facing east. No sideways sliding, root translation or camera movement; return to the same gait phase.` |
| 3. `turn_front` | `S02` | `S01` | 单次 | `Same reference character, crisp pixel art, fixed camera and unchanged proportions. From the right-facing standing pose, take two small repositioning steps to turn the hips, torso and head toward the viewer. End in the front-facing idle pose with both feet planted. No upper-body-only twist.` |

#### BASE-07 朝左行走

上述三段均水平镜像，得到正面→左侧、左行循环、左侧→正面；不直接拿右向素材当左向播放。非对称道具另画左向。

| 顺序／片段键 | 起点 State | 终点 State | 播放 | 自定义 Animation 提示词（可直接复制） |
| --- | --- | --- | --- | --- |
| 1. `turn_right` | `S01` | `S02` | 单次 | `Same reference character, crisp pixel art, fixed camera and unchanged proportions. Turn the whole body from facing front to facing right using two small repositioning steps. Lift the leading foot, pivot the hips with the torso, then bring the trailing foot around. End in the right-facing standing pose; do not twist the upper body over stationary legs.` |
| 2. `walk` | `S02` | `S02` | 循环 | `Same reference character, crisp pixel art, fixed camera and unchanged proportions. Walk in place facing right through one complete alternating step cycle. Lift and place each foot clearly, coordinate the arms and skirt, and keep the torso facing east. No sideways sliding, root translation or camera movement; return to the same gait phase.` |
| 3. `turn_front` | `S02` | `S01` | 单次 | `Same reference character, crisp pixel art, fixed camera and unchanged proportions. From the right-facing standing pose, take two small repositioning steps to turn the hips, torso and head toward the viewer. End in the front-facing idle pose with both feet planted. No upper-body-only twist.` |

#### BASE-08 正面转到侧面

朝左时使用镜像；结束再推进桌面位移。

| 顺序／片段键 | 起点 State | 终点 State | 播放 | 自定义 Animation 提示词（可直接复制） |
| --- | --- | --- | --- | --- |
| 1. `turn_right` | `S01` | `S02` | 单次 | `Same reference character, crisp pixel art, fixed camera and unchanged proportions. Turn the whole body from facing front to facing right using two small repositioning steps. Lift the leading foot, pivot the hips with the torso, then bring the trailing foot around. End in the right-facing standing pose; do not twist the upper body over stationary legs.` |

#### BASE-09 停步并转回正面

朝左停步时使用镜像；不要先切 idle 再转身。

| 顺序／片段键 | 起点 State | 终点 State | 播放 | 自定义 Animation 提示词（可直接复制） |
| --- | --- | --- | --- | --- |
| 1. `turn_front` | `S02` | `S01` | 单次 | `Same reference character, crisp pixel art, fixed camera and unchanged proportions. From the right-facing standing pose, take two small repositioning steps to turn the hips, torso and head toward the viewer. End in the front-facing idle pose with both feet planted. No upper-body-only twist.` |

#### BASE-10 模型选择额外姿态

额外姿态先选此观察样例；新增具体姿态时单独登记其 State 和提示词，不存在通吃所有姿态的提示词。

| 顺序／片段键 | 起点 State | 终点 State | 播放 | 自定义 Animation 提示词（可直接复制） |
| --- | --- | --- | --- | --- |
| 1. `curious_in` | `S01` | `S29` | 单次 | `Same reference character, crisp pixel art, fixed camera and unchanged proportions. Make one small head tilt and lower the gaze toward a nearby object, ending in the curious reference pose. Do not step, smile or open the mouth.` |
| 2. `curious_loop` | `S29` | `S29` | 循环 | `Same reference character, crisp pixel art, fixed camera and unchanged proportions. Hold the small head tilt and quietly inspect a point below, with a tiny eye movement and a brief pause. Keep both feet planted and the mouth closed. Return to the same curious pose.` |
| 3. `curious_out` | `S29` | `S01` | 单次 | `Same reference character, crisp pixel art, fixed camera and unchanged proportions. Bring the head and eyes calmly back to the default front-facing standing pose, hands relaxed and mouth closed. No extra gesture.` |

#### BASE-11 启动时人物登场

规划：先用轻量注意动作配合程序淡入，不生成透明人→实体人的角色变形。

| 顺序／片段键 | 起点 State | 终点 State | 播放 | 自定义 Animation 提示词（可直接复制） |
| --- | --- | --- | --- | --- |
| 1. `attend_in` | `S01` | `S27` | 单次 | `Same reference character, crisp pixel art, fixed camera and unchanged proportions. Notice a new message with a slight lift of the head and a small forward eye movement, then settle into the attentive pose. Keep the mouth closed, feet fixed and reaction restrained.` |
| 2. `attend_out` | `S27` | `S01` | 单次 | `Same reference character, crisp pixel art, fixed camera and unchanged proportions. Relax the small attentive head lift and return to the default standing pose, mouth closed and hands still.` |

#### BASE-12 隐藏、关闭和重新显示角色

窗口隐藏／显示由程序立即控制；本行待机只是可选显示基底，不要求生成离场动画，不延后关闭。

| 顺序／片段键 | 起点 State | 终点 State | 播放 | 自定义 Animation 提示词（可直接复制） |
| --- | --- | --- | --- | --- |
| 1. `idle` | `S01` | `S01` | 循环 | `Same reference character, crisp pixel art, fixed camera and unchanged proportions. Stand quietly in place with only tiny natural settling of the hair and sleeves. Keep the mouth closed, eyes calm, feet planted and head height nearly constant. Return exactly to the starting pose; no deep breathing or swaying.` |

#### TALK-01 新消息预备接收

接收入口只播一次，再接 TALK-02。

| 顺序／片段键 | 起点 State | 终点 State | 播放 | 自定义 Animation 提示词（可直接复制） |
| --- | --- | --- | --- | --- |
| 1. `attend_in` | `S01` | `S27` | 单次 | `Same reference character, crisp pixel art, fixed camera and unchanged proportions. Notice a new message with a slight lift of the head and a small forward eye movement, then settle into the attentive pose. Keep the mouth closed, feet fixed and reaction restrained.` |

#### TALK-02 安静倾听

需要保持时循环第一段，离开倾听时播放第二段。

| 顺序／片段键 | 起点 State | 终点 State | 播放 | 自定义 Animation 提示词（可直接复制） |
| --- | --- | --- | --- | --- |
| 1. `attend_loop` | `S27` | `S27` | 循环 | `Same reference character, crisp pixel art, fixed camera and unchanged proportions. Listen quietly with a steady forward gaze, one almost imperceptible nod and a long still pause. Mouth closed, no repeated bobbing or exaggerated expression. Return to the attentive pose.` |
| 2. `attend_out` | `S27` | `S01` | 单次 | `Same reference character, crisp pixel art, fixed camera and unchanged proportions. Relax the small attentive head lift and return to the default standing pose, mouth closed and hands still.` |

#### TALK-03 准备回复、思考中

入口从 S27 倾听出发；若从待机开始，先接 TALK-01。

| 顺序／片段键 | 起点 State | 终点 State | 播放 | 自定义 Animation 提示词（可直接复制） |
| --- | --- | --- | --- | --- |
| 1. `think_in` | `S27` | `S28` | 单次 | `Same reference character, crisp pixel art, fixed camera and unchanged proportions. Lower the gaze slightly and bring one hand lightly toward the chin, ending in the quiet thinking pose. No worried frown, mouth movement or stepping.` |
| 2. `think_loop` | `S28` | `S28` | 循环 | `Same reference character, crisp pixel art, fixed camera and unchanged proportions. Pause thoughtfully with the hand near the chin, make one tiny eye movement, then remain still. Mouth closed, feet fixed, no repeated head shaking. Return to the same thinking pose.` |
| 3. `think_out` | `S28` | `S01` | 单次 | `Same reference character, crisp pixel art, fixed camera and unchanged proportions. Lower the hand from the chin, lift the gaze gently and settle into the default front-facing idle pose, mouth closed.` |

#### TALK-04 显示对白、讲话

两行不是先后串行：身体循环与嘴部样片分层。mouth 仅用于提取嘴部区域；当前仅有开闭嘴接口，五元音驱动未实现。

| 顺序／片段键 | 起点 State | 终点 State | 播放 | 自定义 Animation 提示词（可直接复制） |
| --- | --- | --- | --- | --- |
| 1. `talk_body` | `S01` | `S01` | 循环 | `Same reference character, crisp pixel art, fixed camera and unchanged proportions. Hold a calm front-facing conversational posture with a tiny restrained hand gesture and a short still pause. Keep the lips closed for a separate mouth layer, avoid large head movement and return to the same pose.` |
| 2. `mouth` | `S01` | `S01` | 单次 | `Same reference character, crisp pixel art, fixed camera and unchanged proportions. Keep the entire reference character perfectly still. Change only the lips from closed to a small natural open speaking mouth and back to closed, with identical face position. No head movement, teeth display, smile or wide gasp.` |

#### TALK-05 行走、进食时同时说话

三行不是连续流程：选行走或进食作为身体基底，口型复用 TALK-04 并另做朝向对齐。进食咬合时优先进食口型，暂停说话口型；不能把正面嘴片直接贴到侧脸。

| 顺序／片段键 | 起点 State | 终点 State | 播放 | 自定义 Animation 提示词（可直接复制） |
| --- | --- | --- | --- | --- |
| 1. `walk` | `S02` | `S02` | 循环 | `Same reference character, crisp pixel art, fixed camera and unchanged proportions. Walk in place facing right through one complete alternating step cycle. Lift and place each foot clearly, coordinate the arms and skirt, and keep the torso facing east. No sideways sliding, root translation or camera movement; return to the same gait phase.` |
| 2. `eat` | `S10` | `S10` | 循环 | `Same reference character, crisp pixel art, fixed camera and unchanged proportions. Take one tiny bite with a small hand movement toward the lips, close the lips and make a barely visible chewing pause, then return to the same food-near-mouth pose. No exaggerated jaw movement, puffed cheeks or disappearing whole prop.` |
| 3. `mouth` | `S01` | `S01` | 单次 | `Same reference character, crisp pixel art, fixed camera and unchanged proportions. Keep the entire reference character perfectly still. Change only the lips from closed to a small natural open speaking mouth and back to closed, with identical face position. No head movement, teeth display, smile or wide gasp.` |

#### TALK-06 沉默、忽略或拒绝请求

可选小幅拒绝；沉默本身保持 BASE-01，不强制做此动作。

| 顺序／片段键 | 起点 State | 终点 State | 播放 | 自定义 Animation 提示词（可直接复制） |
| --- | --- | --- | --- | --- |
| 1. `refuse` | `S01` | `S01` | 单次 | `Same reference character, crisp pixel art, fixed camera and unchanged proportions. Make one very small slow head shake, look aside briefly, then return to the original neutral forward gaze. Mouth closed, no anger, no crossed arms or theatrical refusal.` |

#### TOUCH-01 单击角色后的反应

只在点击判定后单次播放；不触发抬升。

| 顺序／片段键 | 起点 State | 终点 State | 播放 | 自定义 Animation 提示词（可直接复制） |
| --- | --- | --- | --- | --- |
| 1. `touch` | `S01` | `S01` | 单次 | `Same reference character, crisp pixel art, fixed camera and unchanged proportions. React to one light tap with a tiny shoulder recoil and a brief glance toward the touch, then settle back. Keep the feet grounded; no jumping, hanging, lifting or startled open mouth.` |

#### TOUCH-02 正面被拽起

仅超过拖动阈值后进入；程序负责整体上移，片段只改变肢体姿态。

| 顺序／片段键 | 起点 State | 终点 State | 播放 | 自定义 Animation 提示词（可直接复制） |
| --- | --- | --- | --- | --- |
| 1. `lift_front` | `S01` | `S03` | 单次 | `Same reference character, crisp pixel art, fixed camera and unchanged proportions. As if gently picked up from above, let the shoulders rise slightly while the feet lose support, knees loosen and arms hang down. End in the front suspended pose. Animate body posture only; do not move the whole sprite upward inside the canvas.` |

#### TOUCH-03 被拖动、悬空保持

循环到释放鼠标；整体位置跟随鼠标，不能在图内重复位移。

| 顺序／片段键 | 起点 State | 终点 State | 播放 | 自定义 Animation 提示词（可直接复制） |
| --- | --- | --- | --- | --- |
| 1. `hang` | `S03` | `S03` | 循环 | `Same reference character, crisp pixel art, fixed camera and unchanged proportions. Remain suspended facing front with a very small loose leg sway and subtle sleeve settling. Mouth closed, no walking, broad swinging or vertical root motion. Return to the same suspended pose.` |

#### TOUCH-04 从侧面被拽起并转向正面

新增专用素材规划：先侧面悬空再转正，不先强制回站立正面；左侧入口整体镜像。

| 顺序／片段键 | 起点 State | 终点 State | 播放 | 自定义 Animation 提示词（可直接复制） |
| --- | --- | --- | --- | --- |
| 1. `lift_side` | `S02` | `S04` | 单次 | `Same reference character, crisp pixel art, fixed camera and unchanged proportions. From a right-facing stand, lose foot support as if gently picked up from above. Let both legs dangle while preserving the right-side orientation. End in the side suspended pose; no whole-sprite upward translation.` |
| 2. `hang_turn` | `S04` | `S03` | 单次 | `Same reference character, crisp pixel art, fixed camera and unchanged proportions. While already suspended in a right-side pose, gently rotate the hips, torso, head and loose legs toward the viewer. End facing front in the suspended pose. The feet remain dangling and never step on an invisible floor.` |
| 3. `hang` | `S03` | `S03` | 循环 | `Same reference character, crisp pixel art, fixed camera and unchanged proportions. Remain suspended facing front with a very small loose leg sway and subtle sleeve settling. Mouth closed, no walking, broad swinging or vertical root motion. Return to the same suspended pose.` |

#### TOUCH-05 松手下落

S05 是接触地面前的结束姿态；程序下降轨迹与片段协调，随后立即接 TOUCH-06。

| 顺序／片段键 | 起点 State | 终点 State | 播放 | 自定义 Animation 提示词（可直接复制） |
| --- | --- | --- | --- | --- |
| 1. `release` | `S03` | `S05` | 单次 | `Same reference character, crisp pixel art, fixed camera and unchanged proportions. On release from suspension, point the feet downward, bend the knees slightly and spread the hands a little for balance. End ready to land. Animate posture only; no full-sprite falling translation or impact yet.` |

#### TOUCH-06 触地缓冲、站稳

S05→S06→S01，两段各单次，禁止中途插入 idle；现有落地素材可继续沿用。

| 顺序／片段键 | 起点 State | 终点 State | 播放 | 自定义 Animation 提示词（可直接复制） |
| --- | --- | --- | --- | --- |
| 1. `impact` | `S05` | `S06` | 单次 | `Same reference character, crisp pixel art, fixed camera and unchanged proportions. Touch down with both feet and absorb the impact through a small knee bend and lowered hips. End in the shallow landing crouch. Do not insert an upright idle frame before contact; no bounce back into the air.` |
| 2. `land_stand` | `S06` | `S01` | 单次 | `Same reference character, crisp pixel art, fixed camera and unchanged proportions. From the landing crouch, straighten the knees and settle the arms into the normal front-facing idle pose. Keep both feet anchored and avoid a second hop or pause in a different pose.` |

#### TOUCH-07 中键／菜单打开输入条

可选复用倾听；输入条本身由 UI 显示与聚焦，无须生成 UI 像素动画。

| 顺序／片段键 | 起点 State | 终点 State | 播放 | 自定义 Animation 提示词（可直接复制） |
| --- | --- | --- | --- | --- |
| 1. `attend_in` | `S01` | `S27` | 单次 | `Same reference character, crisp pixel art, fixed camera and unchanged proportions. Notice a new message with a slight lift of the head and a small forward eye movement, then settle into the attentive pose. Keep the mouth closed, feet fixed and reaction restrained.` |
| 2. `attend_loop` | `S27` | `S27` | 循环 | `Same reference character, crisp pixel art, fixed camera and unchanged proportions. Listen quietly with a steady forward gaze, one almost imperceptible nod and a long still pause. Mouth closed, no repeated bobbing or exaggerated expression. Return to the attentive pose.` |
| 3. `attend_out` | `S27` | `S01` | 单次 | `Same reference character, crisp pixel art, fixed camera and unchanged proportions. Relax the small attentive head lift and return to the default standing pose, mouth closed and hands still.` |

#### REST-01 从站立躺下

末帧必须是完整侧卧 S07，不是站着闭眼。

| 顺序／片段键 | 起点 State | 终点 State | 播放 | 自定义 Animation 提示词（可直接复制） |
| --- | --- | --- | --- | --- |
| 1. `lie_down` | `S01` | `S07` | 单次 | `Same reference character, crisp pixel art, fixed camera and unchanged proportions. Lower the body by bending both knees, place one hand on the ground, sit briefly, then roll gently onto one side with the head near folded hands. Close the eyes and finish fully lying down. Keep ground contact consistent; do not merely close the eyes while standing.` |

#### REST-02 躺卧、睡眠保持

保持侧卧循环，呼吸不能张嘴。

| 顺序／片段键 | 起点 State | 终点 State | 播放 | 自定义 Animation 提示词（可直接复制） |
| --- | --- | --- | --- | --- |
| 1. `sleep` | `S07` | `S07` | 循环 | `Same reference character, crisp pixel art, fixed camera and unchanged proportions. Sleep quietly while fully lying on one side, with one tiny shoulder rise and release and minimal hair settling. Eyes and mouth remain closed. Keep the head on the hands and return to the same lying pose; no rolling or sitting up.` |

#### REST-03 从躺卧起身

直接从真实侧卧出发回站姿，不另插静态站立。

| 顺序／片段键 | 起点 State | 终点 State | 播放 | 自定义 Animation 提示词（可直接复制） |
| --- | --- | --- | --- | --- |
| 1. `wake` | `S07` | `S01` | 单次 | `Same reference character, crisp pixel art, fixed camera and unchanged proportions. From lying on one side, gently open the eyes, press one hand against the ground, lift the torso, bring the knees underneath and stand up. End in the normal front-facing idle pose. Show the supported rise rather than rotating the whole body as a rigid image.` |

#### REST-04 醒来、结束或中断休息

正常唤醒复用 REST-03；紧急拖动允许打断，不能等整个片段播完。

| 顺序／片段键 | 起点 State | 终点 State | 播放 | 自定义 Animation 提示词（可直接复制） |
| --- | --- | --- | --- | --- |
| 1. `wake` | `S07` | `S01` | 单次 | `Same reference character, crisp pixel art, fixed camera and unchanged proportions. From lying on one side, gently open the eyes, press one hand against the ground, lift the torso, bring the knees underneath and stand up. End in the normal front-facing idle pose. Show the supported rise rather than rotating the whole body as a rigid image.` |

#### REST-05 完成一段实际休息

进入→按实际休息时长保持→退出，不一次生成几分钟素材。

| 顺序／片段键 | 起点 State | 终点 State | 播放 | 自定义 Animation 提示词（可直接复制） |
| --- | --- | --- | --- | --- |
| 1. `lie_down` | `S01` | `S07` | 单次 | `Same reference character, crisp pixel art, fixed camera and unchanged proportions. Lower the body by bending both knees, place one hand on the ground, sit briefly, then roll gently onto one side with the head near folded hands. Close the eyes and finish fully lying down. Keep ground contact consistent; do not merely close the eyes while standing.` |
| 2. `sleep` | `S07` | `S07` | 循环 | `Same reference character, crisp pixel art, fixed camera and unchanged proportions. Sleep quietly while fully lying on one side, with one tiny shoulder rise and release and minimal hair settling. Eyes and mouth remain closed. Keep the head on the hands and return to the same lying pose; no rolling or sitting up.` |
| 3. `wake` | `S07` | `S01` | 单次 | `Same reference character, crisp pixel art, fixed camera and unchanged proportions. From lying on one side, gently open the eyes, press one hand against the ground, lift the torso, bring the knees underneath and stand up. End in the normal front-facing idle pose. Show the supported rise rather than rotating the whole body as a rigid image.` |

#### ITEM-01 接近一件小物或资源

程序选方向与位置，到达目标附近再进入下一动作；左向使用镜像。

| 顺序／片段键 | 起点 State | 终点 State | 播放 | 自定义 Animation 提示词（可直接复制） |
| --- | --- | --- | --- | --- |
| 1. `turn_right` | `S01` | `S02` | 单次 | `Same reference character, crisp pixel art, fixed camera and unchanged proportions. Turn the whole body from facing front to facing right using two small repositioning steps. Lift the leading foot, pivot the hips with the torso, then bring the trailing foot around. End in the right-facing standing pose; do not twist the upper body over stationary legs.` |
| 2. `walk` | `S02` | `S02` | 循环 | `Same reference character, crisp pixel art, fixed camera and unchanged proportions. Walk in place facing right through one complete alternating step cycle. Lift and place each foot clearly, coordinate the arms and skirt, and keep the torso facing east. No sideways sliding, root translation or camera movement; return to the same gait phase.` |
| 3. `turn_front` | `S02` | `S01` | 单次 | `Same reference character, crisp pixel art, fixed camera and unchanged proportions. From the right-facing standing pose, take two small repositioning steps to turn the hips, torso and head toward the viewer. End in the front-facing idle pose with both feet planted. No upper-body-only twist.` |

#### ITEM-02 查看、观察小物或植物

先接近实际对象；近地面物件也可后续增加 crouch 变体。

| 顺序／片段键 | 起点 State | 终点 State | 播放 | 自定义 Animation 提示词（可直接复制） |
| --- | --- | --- | --- | --- |
| 1. `curious_in` | `S01` | `S29` | 单次 | `Same reference character, crisp pixel art, fixed camera and unchanged proportions. Make one small head tilt and lower the gaze toward a nearby object, ending in the curious reference pose. Do not step, smile or open the mouth.` |
| 2. `curious_loop` | `S29` | `S29` | 循环 | `Same reference character, crisp pixel art, fixed camera and unchanged proportions. Hold the small head tilt and quietly inspect a point below, with a tiny eye movement and a brief pause. Keep both feet planted and the mouth closed. Return to the same curious pose.` |
| 3. `curious_out` | `S29` | `S01` | 单次 | `Same reference character, crisp pixel art, fixed camera and unchanged proportions. Bring the head and eyes calmly back to the default front-facing standing pose, hands relaxed and mouth closed. No extra gesture.` |

#### ITEM-03 吃掉桌面小物

先走近；拿到食物后才挂接道具，食用结果以回执为准。若已持物，从 S09 开始。每段独立生成，非整条长动画。

| 顺序／片段键 | 起点 State | 终点 State | 播放 | 自定义 Animation 提示词（可直接复制） |
| --- | --- | --- | --- | --- |
| 1. `crouch` | `S01` | `S08` | 单次 | `Same reference character, crisp pixel art, fixed camera and unchanged proportions. Bend the knees and lower the hips toward a nearby ground item, reaching one hand down while the other balances the body. End in the crouching reach pose. Keep the feet planted and do not stretch the arm unnaturally.` |
| 2. `pickup` | `S08` | `S09` | 单次 | `Same reference character, crisp pixel art, fixed camera and unchanged proportions. From the crouching reach pose, close the reaching hand around one small item, lift it with the hand, then straighten the knees and bring both hands to waist height. End in the front holding pose. Keep the item aligned with the hand; no distant item teleportation.` |
| 3. `food_up` | `S09` | `S10` | 单次 | `Same reference character, crisp pixel art, fixed camera and unchanged proportions. Raise one small held food item from waist height to just below the lips, keeping the elbow relaxed and the head nearly still. End in the food-near-mouth pose. Do not swallow the entire item or open the mouth widely.` |
| 4. `eat` | `S10` | `S10` | 循环 | `Same reference character, crisp pixel art, fixed camera and unchanged proportions. Take one tiny bite with a small hand movement toward the lips, close the lips and make a barely visible chewing pause, then return to the same food-near-mouth pose. No exaggerated jaw movement, puffed cheeks or disappearing whole prop.` |
| 5. `food_down` | `S10` | `S09` | 单次 | `Same reference character, crisp pixel art, fixed camera and unchanged proportions. Lower the hand from the mouth to waist height and return to the front holding pose, keeping the lips gently closed and expression calm. Do not create a new food item.` |
| 6. `hands_down` | `S09` | `S01` | 单次 | `Same reference character, crisp pixel art, fixed camera and unchanged proportions. Lower the cupped hands from waist height into the default relaxed pose after the application has removed or transferred the held item. Mouth closed; do not make a second item appear or throw anything.` |

#### ITEM-04 捡起、收集桌面食材

收纳成功才让道具退出并计入背包；失败时保留实际物件，可从当前姿态退回。

| 顺序／片段键 | 起点 State | 终点 State | 播放 | 自定义 Animation 提示词（可直接复制） |
| --- | --- | --- | --- | --- |
| 1. `crouch` | `S01` | `S08` | 单次 | `Same reference character, crisp pixel art, fixed camera and unchanged proportions. Bend the knees and lower the hips toward a nearby ground item, reaching one hand down while the other balances the body. End in the crouching reach pose. Keep the feet planted and do not stretch the arm unnaturally.` |
| 2. `pickup` | `S08` | `S09` | 单次 | `Same reference character, crisp pixel art, fixed camera and unchanged proportions. From the crouching reach pose, close the reaching hand around one small item, lift it with the hand, then straighten the knees and bring both hands to waist height. End in the front holding pose. Keep the item aligned with the hand; no distant item teleportation.` |
| 3. `bag_in` | `S09` | `S11` | 单次 | `Same reference character, crisp pixel art, fixed camera and unchanged proportions. Bring the held item toward a small pouch at the waist and gently part its opening with one hand. Finish with the pouch open and the other hand near its opening. Keep the item transfer localized; do not grow a large backpack over the body.` |
| 4. `bag_store` | `S11` | `S11` | 单次 | `Same reference character, crisp pixel art, fixed camera and unchanged proportions. Move one hand into the small open pouch once as if stowing the held item, then withdraw it to the same open-pouch pose. Keep the pouch steady; actual item removal is handled separately. No repeated rummaging.` |
| 5. `bag_close` | `S11` | `S01` | 单次 | `Same reference character, crisp pixel art, fixed camera and unchanged proportions. Close the small pouch, lower the hands naturally and return to the normal front-facing idle pose. Do not leave a second pouch or loose item behind.` |

#### ITEM-05 连续收集附近或指定区域食材

每一目标复用一轮；是否还有目标、入包数量由业务判断，不循环播放整条采集流程。

| 顺序／片段键 | 起点 State | 终点 State | 播放 | 自定义 Animation 提示词（可直接复制） |
| --- | --- | --- | --- | --- |
| 1. `turn_right` | `S01` | `S02` | 单次 | `Same reference character, crisp pixel art, fixed camera and unchanged proportions. Turn the whole body from facing front to facing right using two small repositioning steps. Lift the leading foot, pivot the hips with the torso, then bring the trailing foot around. End in the right-facing standing pose; do not twist the upper body over stationary legs.` |
| 2. `walk` | `S02` | `S02` | 循环 | `Same reference character, crisp pixel art, fixed camera and unchanged proportions. Walk in place facing right through one complete alternating step cycle. Lift and place each foot clearly, coordinate the arms and skirt, and keep the torso facing east. No sideways sliding, root translation or camera movement; return to the same gait phase.` |
| 3. `turn_front` | `S02` | `S01` | 单次 | `Same reference character, crisp pixel art, fixed camera and unchanged proportions. From the right-facing standing pose, take two small repositioning steps to turn the hips, torso and head toward the viewer. End in the front-facing idle pose with both feet planted. No upper-body-only twist.` |
| 4. `crouch` | `S01` | `S08` | 单次 | `Same reference character, crisp pixel art, fixed camera and unchanged proportions. Bend the knees and lower the hips toward a nearby ground item, reaching one hand down while the other balances the body. End in the crouching reach pose. Keep the feet planted and do not stretch the arm unnaturally.` |
| 5. `pickup` | `S08` | `S09` | 单次 | `Same reference character, crisp pixel art, fixed camera and unchanged proportions. From the crouching reach pose, close the reaching hand around one small item, lift it with the hand, then straighten the knees and bring both hands to waist height. End in the front holding pose. Keep the item aligned with the hand; no distant item teleportation.` |
| 6. `bag_in` | `S09` | `S11` | 单次 | `Same reference character, crisp pixel art, fixed camera and unchanged proportions. Bring the held item toward a small pouch at the waist and gently part its opening with one hand. Finish with the pouch open and the other hand near its opening. Keep the item transfer localized; do not grow a large backpack over the body.` |
| 7. `bag_store` | `S11` | `S11` | 单次 | `Same reference character, crisp pixel art, fixed camera and unchanged proportions. Move one hand into the small open pouch once as if stowing the held item, then withdraw it to the same open-pouch pose. Keep the pouch steady; actual item removal is handled separately. No repeated rummaging.` |
| 8. `bag_close` | `S11` | `S01` | 单次 | `Same reference character, crisp pixel art, fixed camera and unchanged proportions. Close the small pouch, lower the hands naturally and return to the normal front-facing idle pose. Do not leave a second pouch or loose item behind.` |

#### ITEM-06 为某道料理收集缺少的食材

素材同 ITEM-05；需要何种食材由真实缺料与库存决定，不新增“缺料”人体姿态。

| 顺序／片段键 | 起点 State | 终点 State | 播放 | 自定义 Animation 提示词（可直接复制） |
| --- | --- | --- | --- | --- |
| 1. `turn_right` | `S01` | `S02` | 单次 | `Same reference character, crisp pixel art, fixed camera and unchanged proportions. Turn the whole body from facing front to facing right using two small repositioning steps. Lift the leading foot, pivot the hips with the torso, then bring the trailing foot around. End in the right-facing standing pose; do not twist the upper body over stationary legs.` |
| 2. `walk` | `S02` | `S02` | 循环 | `Same reference character, crisp pixel art, fixed camera and unchanged proportions. Walk in place facing right through one complete alternating step cycle. Lift and place each foot clearly, coordinate the arms and skirt, and keep the torso facing east. No sideways sliding, root translation or camera movement; return to the same gait phase.` |
| 3. `turn_front` | `S02` | `S01` | 单次 | `Same reference character, crisp pixel art, fixed camera and unchanged proportions. From the right-facing standing pose, take two small repositioning steps to turn the hips, torso and head toward the viewer. End in the front-facing idle pose with both feet planted. No upper-body-only twist.` |
| 4. `crouch` | `S01` | `S08` | 单次 | `Same reference character, crisp pixel art, fixed camera and unchanged proportions. Bend the knees and lower the hips toward a nearby ground item, reaching one hand down while the other balances the body. End in the crouching reach pose. Keep the feet planted and do not stretch the arm unnaturally.` |
| 5. `pickup` | `S08` | `S09` | 单次 | `Same reference character, crisp pixel art, fixed camera and unchanged proportions. From the crouching reach pose, close the reaching hand around one small item, lift it with the hand, then straighten the knees and bring both hands to waist height. End in the front holding pose. Keep the item aligned with the hand; no distant item teleportation.` |
| 6. `bag_in` | `S09` | `S11` | 单次 | `Same reference character, crisp pixel art, fixed camera and unchanged proportions. Bring the held item toward a small pouch at the waist and gently part its opening with one hand. Finish with the pouch open and the other hand near its opening. Keep the item transfer localized; do not grow a large backpack over the body.` |
| 7. `bag_store` | `S11` | `S11` | 单次 | `Same reference character, crisp pixel art, fixed camera and unchanged proportions. Move one hand into the small open pouch once as if stowing the held item, then withdraw it to the same open-pouch pose. Keep the pouch steady; actual item removal is handled separately. No repeated rummaging.` |
| 8. `bag_close` | `S11` | `S01` | 单次 | `Same reference character, crisp pixel art, fixed camera and unchanged proportions. Close the small pouch, lower the hands naturally and return to the normal front-facing idle pose. Do not leave a second pouch or loose item behind.` |

#### ITEM-07 种下种子

种子与芽苗通过 ENV-03 管理；plant 是单次操作，自身到自身也不代表要反复播。

| 顺序／片段键 | 起点 State | 终点 State | 播放 | 自定义 Animation 提示词（可直接复制） |
| --- | --- | --- | --- | --- |
| 1. `crouch` | `S01` | `S08` | 单次 | `Same reference character, crisp pixel art, fixed camera and unchanged proportions. Bend the knees and lower the hips toward a nearby ground item, reaching one hand down while the other balances the body. End in the crouching reach pose. Keep the feet planted and do not stretch the arm unnaturally.` |
| 2. `plant` | `S08` | `S08` | 单次 | `Same reference character, crisp pixel art, fixed camera and unchanged proportions. While crouched, place one tiny seed at the feet with the fingertips, make one small covering motion and withdraw the hand to the same crouching reach pose. Keep the head, feet and scale stable. No instantly growing plant or large tool.` |
| 3. `crouch_out` | `S08` | `S01` | 单次 | `Same reference character, crisp pixel art, fixed camera and unchanged proportions. From the crouching reach pose, draw the hand back and straighten the knees into the default standing pose. Keep the feet anchored and mouth closed.` |

#### ITEM-08 挪动、布置已有小物

本版本对应轻推摆放；携物跨区域流程需 BAG-05 接线后另接。

| 顺序／片段键 | 起点 State | 终点 State | 播放 | 自定义 Animation 提示词（可直接复制） |
| --- | --- | --- | --- | --- |
| 1. `crouch` | `S01` | `S08` | 单次 | `Same reference character, crisp pixel art, fixed camera and unchanged proportions. Bend the knees and lower the hips toward a nearby ground item, reaching one hand down while the other balances the body. End in the crouching reach pose. Keep the feet planted and do not stretch the arm unnaturally.` |
| 2. `push` | `S08` | `S08` | 单次 | `Same reference character, crisp pixel art, fixed camera and unchanged proportions. While crouched, gently push a nearby small item a short distance with one hand, then draw the hand back to the same reaching pose. Do not lift or carry it. Whole-object movement is controlled separately.` |
| 3. `crouch_out` | `S08` | `S01` | 单次 | `Same reference character, crisp pixel art, fixed camera and unchanged proportions. From the crouching reach pose, draw the hand back and straighten the knees into the default standing pose. Keep the feet anchored and mouth closed.` |

#### ITEM-09 创造、放出虚拟小物

创建成功回执后再让 ENV-01 淡入；动作不能虚构创建成功。

| 顺序／片段键 | 起点 State | 终点 State | 播放 | 自定义 Animation 提示词（可直接复制） |
| --- | --- | --- | --- | --- |
| 1. `palm_in` | `S01` | `S14` | 单次 | `Same reference character, crisp pixel art, fixed camera and unchanged proportions. Raise one hand and open the palm gently at waist height as if offering a place for a small object. End with the palm open, mouth closed and expression calm. No magical explosion or item appearing by itself.` |
| 2. `palm_out` | `S14` | `S01` | 单次 | `Same reference character, crisp pixel art, fixed camera and unchanged proportions. Lower the open hand quietly into the normal relaxed standing pose. Keep the face calm and mouth closed, without tossing an object.` |

#### BAG-01 查看背包、查看仓库

库存查询可不表演；这是拟接入的可选翻包表现，不等于已存在实体仓库。

| 顺序／片段键 | 起点 State | 终点 State | 播放 | 自定义 Animation 提示词（可直接复制） |
| --- | --- | --- | --- | --- |
| 1. `bag_open` | `S01` | `S11` | 单次 | `Same reference character, crisp pixel art, fixed camera and unchanged proportions. Bring a small pouch to waist height, part the opening gently and lower the gaze toward it. Finish in the open-pouch pose. Keep body scale unchanged; no oversized backpack or dramatic flourish.` |
| 2. `bag_look` | `S11` | `S11` | 循环 | `Same reference character, crisp pixel art, fixed camera and unchanged proportions. Look into the small open pouch with one subtle eye movement and a short still pause. Keep the hands on its rim, feet planted and mouth closed. Return to the same pose without producing items.` |
| 3. `bag_close` | `S11` | `S01` | 单次 | `Same reference character, crisp pixel art, fixed camera and unchanged proportions. Close the small pouch, lower the hands naturally and return to the normal front-facing idle pose. Do not leave a second pouch or loose item behind.` |

#### BAG-02 把背包物资存入仓库

拟实体存放表现；现有数据转移仍可直接执行，柜子位置与物件层需另接。

| 顺序／片段键 | 起点 State | 终点 State | 播放 | 自定义 Animation 提示词（可直接复制） |
| --- | --- | --- | --- | --- |
| 1. `reach` | `S01` | `S12` | 单次 | `Same reference character, crisp pixel art, fixed camera and unchanged proportions. Extend one hand forward at waist height toward a nearby storage surface while the other hand comes up to support an item. End in the storage-reaching pose, feet planted and mouth closed. No generated cupboard or background.` |
| 2. `store` | `S12` | `S12` | 单次 | `Same reference character, crisp pixel art, fixed camera and unchanged proportions. Make one short gentle forward placing movement, release the fingers, then return to the same reaching pose. Keep the support hand steady. Item visibility and storage success are controlled separately.` |
| 3. `reach_out` | `S12` | `S01` | 单次 | `Same reference character, crisp pixel art, fixed camera and unchanged proportions. Retract the extended arm and lower both hands into the normal relaxed standing pose, mouth closed and feet fixed.` |

#### BAG-03 从仓库取出物资

物品取出成功才显示手持物；末段前由程序决定物件归背包或留手中。

| 顺序／片段键 | 起点 State | 终点 State | 播放 | 自定义 Animation 提示词（可直接复制） |
| --- | --- | --- | --- | --- |
| 1. `reach` | `S01` | `S12` | 单次 | `Same reference character, crisp pixel art, fixed camera and unchanged proportions. Extend one hand forward at waist height toward a nearby storage surface while the other hand comes up to support an item. End in the storage-reaching pose, feet planted and mouth closed. No generated cupboard or background.` |
| 2. `retrieve` | `S12` | `S09` | 单次 | `Same reference character, crisp pixel art, fixed camera and unchanged proportions. Close the reaching hand around one small item on a nearby surface, draw it back and support it at waist height with both hands. End in the front holding pose without stepping or inventing extra items.` |
| 3. `hands_down` | `S09` | `S01` | 单次 | `Same reference character, crisp pixel art, fixed camera and unchanged proportions. Lower the cupped hands from waist height into the default relaxed pose after the application has removed or transferred the held item. Mouth closed; do not make a second item appear or throw anything.` |

#### BAG-04 直接吃背包或仓库中的食物

直接吃库存的流程尚未自动绑定身体素材；食物成功取用后再进食。

| 顺序／片段键 | 起点 State | 终点 State | 播放 | 自定义 Animation 提示词（可直接复制） |
| --- | --- | --- | --- | --- |
| 1. `reach` | `S01` | `S12` | 单次 | `Same reference character, crisp pixel art, fixed camera and unchanged proportions. Extend one hand forward at waist height toward a nearby storage surface while the other hand comes up to support an item. End in the storage-reaching pose, feet planted and mouth closed. No generated cupboard or background.` |
| 2. `retrieve` | `S12` | `S09` | 单次 | `Same reference character, crisp pixel art, fixed camera and unchanged proportions. Close the reaching hand around one small item on a nearby surface, draw it back and support it at waist height with both hands. End in the front holding pose without stepping or inventing extra items.` |
| 3. `food_up` | `S09` | `S10` | 单次 | `Same reference character, crisp pixel art, fixed camera and unchanged proportions. Raise one small held food item from waist height to just below the lips, keeping the elbow relaxed and the head nearly still. End in the food-near-mouth pose. Do not swallow the entire item or open the mouth widely.` |
| 4. `eat` | `S10` | `S10` | 循环 | `Same reference character, crisp pixel art, fixed camera and unchanged proportions. Take one tiny bite with a small hand movement toward the lips, close the lips and make a barely visible chewing pause, then return to the same food-near-mouth pose. No exaggerated jaw movement, puffed cheeks or disappearing whole prop.` |
| 5. `food_down` | `S10` | `S09` | 单次 | `Same reference character, crisp pixel art, fixed camera and unchanged proportions. Lower the hand from the mouth to waist height and return to the front holding pose, keeping the lips gently closed and expression calm. Do not create a new food item.` |
| 6. `hands_down` | `S09` | `S01` | 单次 | `Same reference character, crisp pixel art, fixed camera and unchanged proportions. Lower the cupped hands from waist height into the default relaxed pose after the application has removed or transferred the held item. Mouth closed; do not make a second item appear or throw anything.` |

#### BAG-05 手持／抱着物品走动

规划：先通过取物到 S09，再携物转身／行走／回正；需要接持物挂点，不能把背包里所有物资都画在手上。

| 顺序／片段键 | 起点 State | 终点 State | 播放 | 自定义 Animation 提示词（可直接复制） |
| --- | --- | --- | --- | --- |
| 1. `carry_turn` | `S09` | `S13` | 单次 | `Same reference character, crisp pixel art, fixed camera and unchanged proportions. While supporting one small item at waist height, turn toward the right using two small repositioning steps. Keep both hands aligned with the item and end in the right-facing carrying pose.` |
| 2. `carry_walk` | `S13` | `S13` | 循环 | `Same reference character, crisp pixel art, fixed camera and unchanged proportions. Walk in place facing right while holding a small item steadily at waist height. Alternate both feet through a complete gait cycle, keep the hands stable and avoid root translation. Return to the same carrying gait phase.` |
| 3. `carry_front` | `S13` | `S09` | 单次 | `Same reference character, crisp pixel art, fixed camera and unchanged proportions. Stop in the right-facing carrying pose, then turn toward the viewer using two small steps. Keep the item supported by both hands and end in the front holding pose.` |

#### CAFE-01 看菜单、检查原料和成品

拟菜单道具表现；将书替换成统一尺寸菜单，道具层复用，不画菜单文字。纯数据查询允许不演。

| 顺序／片段键 | 起点 State | 终点 State | 播放 | 自定义 Animation 提示词（可直接复制） |
| --- | --- | --- | --- | --- |
| 1. `book_take` | `S01` | `S18` | 单次 | `Same reference character, crisp pixel art, fixed camera and unchanged proportions. Reach toward a nearby book, bring it back with both hands and hold it closed at waist height. End in the closed-book standing pose. No book appearing from empty air or generated shelf.` |
| 2. `book_open` | `S18` | `S19` | 单次 | `Same reference character, crisp pixel art, fixed camera and unchanged proportions. Open the small book gently with both hands and lower the gaze to the pages, ending in the standing open-book pose. Keep the binding edge anchored and the page count stable.` |
| 3. `read` | `S19` | `S19` | 循环 | `Same reference character, crisp pixel art, fixed camera and unchanged proportions. Read the open book quietly, move the eyes subtly along the pages, turn one page gently and pause. Return to the same open-book pose. Mouth closed, no page text, body rocking or changing book size.` |
| 4. `book_close` | `S19` | `S18` | 单次 | `Same reference character, crisp pixel art, fixed camera and unchanged proportions. Close the open book gently with both hands, keeping the binding edge steady, and end holding the closed book at waist height. Mouth closed.` |
| 5. `book_put` | `S18` | `S01` | 单次 | `Same reference character, crisp pixel art, fixed camera and unchanged proportions. Reach a short distance forward to place the closed book on a nearby surface, then lower the hands into the default idle pose. No book vanishing halfway through the hand movement; removal is handled separately.` |

#### CAFE-02 制作面包、布丁或泡茶

面包／布丁／茶先共用料理身体动作并换道具；搅拌只是可选局部表现，不能假装所有配方都须一直搅拌。真实制作仍由后台计时。

| 顺序／片段键 | 起点 State | 终点 State | 播放 | 自定义 Animation 提示词（可直接复制） |
| --- | --- | --- | --- | --- |
| 1. `cook_in` | `S01` | `S15` | 单次 | `Same reference character, crisp pixel art, fixed camera and unchanged proportions. Lift both forearms toward a waist-high work surface, placing one hand ready to stir and the other ready to steady a bowl. End in the cooking-ready pose. Do not generate a kitchen or change the outfit.` |
| 2. `cook` | `S15` | `S15` | 循环 | `Same reference character, crisp pixel art, fixed camera and unchanged proportions. Make two small slow circular stirring motions with one wrist while the other hand steadies a small bowl, pause briefly, then return to the cooking-ready pose. Keep the torso and feet stable, mouth closed and tools consistent.` |
| 3. `cook_out` | `S15` | `S01` | 单次 | `Same reference character, crisp pixel art, fixed camera and unchanged proportions. Stop stirring, set the tool down with a small localized hand movement and lower the arms into the default idle pose. No finished meal magically appearing in the hands.` |

#### CAFE-03 等待料理完成

等待后台料理完成可做别的事；此待机只是在没有其他活动时的基底，不强制固定在料理台。

| 顺序／片段键 | 起点 State | 终点 State | 播放 | 自定义 Animation 提示词（可直接复制） |
| --- | --- | --- | --- | --- |
| 1. `idle` | `S01` | `S01` | 循环 | `Same reference character, crisp pixel art, fixed camera and unchanged proportions. Stand quietly in place with only tiny natural settling of the hair and sleeves. Keep the mouth closed, eyes calm, feet planted and head height nearly constant. Return exactly to the starting pose; no deep breathing or swaying.` |

#### CAFE-04 从成品架取餐

拟取餐动作；未来需要端盘走动时接 BAG-05，再在目的地放下；当前 take 实际发布到桌面，不会自动变成端盘场景。

| 顺序／片段键 | 起点 State | 终点 State | 播放 | 自定义 Animation 提示词（可直接复制） |
| --- | --- | --- | --- | --- |
| 1. `reach` | `S01` | `S12` | 单次 | `Same reference character, crisp pixel art, fixed camera and unchanged proportions. Extend one hand forward at waist height toward a nearby storage surface while the other hand comes up to support an item. End in the storage-reaching pose, feet planted and mouth closed. No generated cupboard or background.` |
| 2. `retrieve` | `S12` | `S09` | 单次 | `Same reference character, crisp pixel art, fixed camera and unchanged proportions. Close the reaching hand around one small item on a nearby surface, draw it back and support it at waist height with both hands. End in the front holding pose without stepping or inventing extra items.` |
| 3. `hands_down` | `S09` | `S01` | 单次 | `Same reference character, crisp pixel art, fixed camera and unchanged proportions. Lower the cupped hands from waist height into the default relaxed pose after the application has removed or transferred the held item. Mouth closed; do not make a second item appear or throw anything.` |

#### CAFE-05 在食堂坐下就餐

规划：先由位置与桌椅系统把角色带到座位。放盘／收盘作为独立道具事件；第一版可用手持小食，不让生成器临时画桌椅。

| 顺序／片段键 | 起点 State | 终点 State | 播放 | 自定义 Animation 提示词（可直接复制） |
| --- | --- | --- | --- | --- |
| 1. `sit` | `S01` | `S16` | 单次 | `Same reference character, crisp pixel art, fixed camera and unchanged proportions. Bend the knees and lower the hips onto a fixed seat behind the body, using a small hand movement for balance. End sitting upright with thighs supported and feet lowered. No chair generation, sliding seat or change of scale.` |
| 2. `seat_food_up` | `S16` | `S17` | 单次 | `Same reference character, crisp pixel art, fixed camera and unchanged proportions. While remaining seated, lift one small food item from lap height to just below the lips. Keep the torso upright and seat height unchanged. End in the seated food-near-mouth pose; no wide mouth opening.` |
| 3. `seat_eat` | `S17` | `S17` | 循环 | `Same reference character, crisp pixel art, fixed camera and unchanged proportions. Remain seated and take one tiny bite with a subtle hand motion, then close the lips for a brief chewing pause. Return to the same seated food pose. No puffed cheeks, wide jaw movement or moving chair.` |
| 4. `seat_food_down` | `S17` | `S16` | 单次 | `Same reference character, crisp pixel art, fixed camera and unchanged proportions. Lower the food hand toward the lap and settle both hands near the lap, ending in the quiet seated pose. Keep seat height unchanged; prop removal is handled separately.` |
| 5. `stand` | `S16` | `S01` | 单次 | `Same reference character, crisp pixel art, fixed camera and unchanged proportions. Lean forward slightly from the fixed seat, place weight through both feet, straighten the knees and stand upright. End in the normal front-facing idle pose. No jump, chair movement or resizing.` |

#### CAFE-06 取消制作、丢弃成品

取消制作通常不表演；可选丢弃复用短放置动作，需指定真实垃圾容器与回执，不默认已有场景。

| 顺序／片段键 | 起点 State | 终点 State | 播放 | 自定义 Animation 提示词（可直接复制） |
| --- | --- | --- | --- | --- |
| 1. `reach` | `S01` | `S12` | 单次 | `Same reference character, crisp pixel art, fixed camera and unchanged proportions. Extend one hand forward at waist height toward a nearby storage surface while the other hand comes up to support an item. End in the storage-reaching pose, feet planted and mouth closed. No generated cupboard or background.` |
| 2. `store` | `S12` | `S12` | 单次 | `Same reference character, crisp pixel art, fixed camera and unchanged proportions. Make one short gentle forward placing movement, release the fingers, then return to the same reaching pose. Keep the support hand steady. Item visibility and storage success are controlled separately.` |
| 3. `reach_out` | `S12` | `S01` | 单次 | `Same reference character, crisp pixel art, fixed camera and unchanged proportions. Retract the extended arm and lower both hands into the normal relaxed standing pose, mouth closed and feet fixed.` |

#### READ-01 搜索感兴趣的资料

搜索时可选择思考表现，也可保持待机；不生成一次网络搜索的完整叙事动画。

| 顺序／片段键 | 起点 State | 终点 State | 播放 | 自定义 Animation 提示词（可直接复制） |
| --- | --- | --- | --- | --- |
| 1. `attend_in` | `S01` | `S27` | 单次 | `Same reference character, crisp pixel art, fixed camera and unchanged proportions. Notice a new message with a slight lift of the head and a small forward eye movement, then settle into the attentive pose. Keep the mouth closed, feet fixed and reaction restrained.` |
| 2. `think_in` | `S27` | `S28` | 单次 | `Same reference character, crisp pixel art, fixed camera and unchanged proportions. Lower the gaze slightly and bring one hand lightly toward the chin, ending in the quiet thinking pose. No worried frown, mouth movement or stepping.` |
| 3. `think_loop` | `S28` | `S28` | 循环 | `Same reference character, crisp pixel art, fixed camera and unchanged proportions. Pause thoughtfully with the hand near the chin, make one tiny eye movement, then remain still. Mouth closed, feet fixed, no repeated head shaking. Return to the same thinking pose.` |
| 4. `think_out` | `S28` | `S01` | 单次 | `Same reference character, crisp pixel art, fixed camera and unchanged proportions. Lower the hand from the chin, lift the gaze gently and settle into the default front-facing idle pose, mouth closed.` |

#### READ-02 阅读资料、继续读取正文片段

现有文字读取未绑定身体；本表是拟接素材流程，纸张也可替换书本道具。

| 顺序／片段键 | 起点 State | 终点 State | 播放 | 自定义 Animation 提示词（可直接复制） |
| --- | --- | --- | --- | --- |
| 1. `book_take` | `S01` | `S18` | 单次 | `Same reference character, crisp pixel art, fixed camera and unchanged proportions. Reach toward a nearby book, bring it back with both hands and hold it closed at waist height. End in the closed-book standing pose. No book appearing from empty air or generated shelf.` |
| 2. `book_open` | `S18` | `S19` | 单次 | `Same reference character, crisp pixel art, fixed camera and unchanged proportions. Open the small book gently with both hands and lower the gaze to the pages, ending in the standing open-book pose. Keep the binding edge anchored and the page count stable.` |
| 3. `read` | `S19` | `S19` | 循环 | `Same reference character, crisp pixel art, fixed camera and unchanged proportions. Read the open book quietly, move the eyes subtly along the pages, turn one page gently and pause. Return to the same open-book pose. Mouth closed, no page text, body rocking or changing book size.` |
| 4. `book_close` | `S19` | `S18` | 单次 | `Same reference character, crisp pixel art, fixed camera and unchanged proportions. Close the open book gently with both hands, keeping the binding edge steady, and end holding the closed book at waist height. Mouth closed.` |
| 5. `book_put` | `S18` | `S01` | 单次 | `Same reference character, crisp pixel art, fixed camera and unchanged proportions. Reach a short distance forward to place the closed book on a nearby surface, then lower the hands into the default idle pose. No book vanishing halfway through the hand movement; removal is handled separately.` |

#### READ-03 在阅读角看书

规划：走近书架／座位用 BASE-06～09；第一版先站起再合书，避免额外新建坐姿合书 State。

| 顺序／片段键 | 起点 State | 终点 State | 播放 | 自定义 Animation 提示词（可直接复制） |
| --- | --- | --- | --- | --- |
| 1. `book_take` | `S01` | `S18` | 单次 | `Same reference character, crisp pixel art, fixed camera and unchanged proportions. Reach toward a nearby book, bring it back with both hands and hold it closed at waist height. End in the closed-book standing pose. No book appearing from empty air or generated shelf.` |
| 2. `book_open` | `S18` | `S19` | 单次 | `Same reference character, crisp pixel art, fixed camera and unchanged proportions. Open the small book gently with both hands and lower the gaze to the pages, ending in the standing open-book pose. Keep the binding edge anchored and the page count stable.` |
| 3. `book_sit` | `S19` | `S20` | 单次 | `Same reference character, crisp pixel art, fixed camera and unchanged proportions. While holding the book open, carefully bend the knees and sit on a fixed seat behind the body, lowering the book above the lap. End seated with the open book. Keep its size and grip stable; no generated chair.` |
| 4. `seat_read` | `S20` | `S20` | 循环 | `Same reference character, crisp pixel art, fixed camera and unchanged proportions. Sit quietly with the open book above the lap, make a tiny reading eye movement, turn one page gently and pause. Return to the same seated reading pose. Keep seat height fixed and mouth closed.` |
| 5. `book_stand` | `S20` | `S19` | 单次 | `Same reference character, crisp pixel art, fixed camera and unchanged proportions. While supporting the open book with both hands, lean forward slightly and rise from the fixed seat using both feet. End standing with the book still open. No book flipping, resizing or chair generation.` |
| 6. `book_close` | `S19` | `S18` | 单次 | `Same reference character, crisp pixel art, fixed camera and unchanged proportions. Close the open book gently with both hands, keeping the binding edge steady, and end holding the closed book at waist height. Mouth closed.` |
| 7. `book_put` | `S18` | `S01` | 单次 | `Same reference character, crisp pixel art, fixed camera and unchanged proportions. Reach a short distance forward to place the closed book on a nearby surface, then lower the hands into the default idle pose. No book vanishing halfway through the hand movement; removal is handled separately.` |

#### ART-01 写短文、整理草稿、接写作品

笔尖动作循环包含停顿；真实写入与保存由创作工具完成。

| 顺序／片段键 | 起点 State | 终点 State | 播放 | 自定义 Animation 提示词（可直接复制） |
| --- | --- | --- | --- | --- |
| 1. `write_in` | `S01` | `S21` | 单次 | `Same reference character, crisp pixel art, fixed camera and unchanged proportions. Reach for a small pen, bring its tip above a waist-high writing surface and steady a sheet with the other hand. End in the writing-ready pose. Do not generate a desk, readable text or background.` |
| 2. `write` | `S21` | `S21` | 循环 | `Same reference character, crisp pixel art, fixed camera and unchanged proportions. Make a few small deliberate pen strokes, pause with the tip hovering, then return to the writing-ready hand position. Keep the paper hand and body steady, mouth closed, with no legible text appearing.` |
| 3. `write_out` | `S21` | `S01` | 单次 | `Same reference character, crisp pixel art, fixed camera and unchanged proportions. Lift the pen tip from the sheet, set the pen down with one small movement and lower the hands into the default idle pose. Do not generate new paper or background.` |

#### ART-02 重读自己的作品

复用 READ-02，道具替换成自己的稿纸或笔记本；不在生成图内渲染作品正文。

| 顺序／片段键 | 起点 State | 终点 State | 播放 | 自定义 Animation 提示词（可直接复制） |
| --- | --- | --- | --- | --- |
| 1. `book_take` | `S01` | `S18` | 单次 | `Same reference character, crisp pixel art, fixed camera and unchanged proportions. Reach toward a nearby book, bring it back with both hands and hold it closed at waist height. End in the closed-book standing pose. No book appearing from empty air or generated shelf.` |
| 2. `book_open` | `S18` | `S19` | 单次 | `Same reference character, crisp pixel art, fixed camera and unchanged proportions. Open the small book gently with both hands and lower the gaze to the pages, ending in the standing open-book pose. Keep the binding edge anchored and the page count stable.` |
| 3. `read` | `S19` | `S19` | 循环 | `Same reference character, crisp pixel art, fixed camera and unchanged proportions. Read the open book quietly, move the eyes subtly along the pages, turn one page gently and pause. Return to the same open-book pose. Mouth closed, no page text, body rocking or changing book size.` |
| 4. `book_close` | `S19` | `S18` | 单次 | `Same reference character, crisp pixel art, fixed camera and unchanged proportions. Close the open book gently with both hands, keeping the binding edge steady, and end holding the closed book at waist height. Mouth closed.` |
| 5. `book_put` | `S18` | `S01` | 单次 | `Same reference character, crisp pixel art, fixed camera and unchanged proportions. Reach a short distance forward to place the closed book on a nearby surface, then lower the hands into the default idle pose. No book vanishing halfway through the hand movement; removal is handled separately.` |

#### ART-03 画像素画、修改颜色、制作作品动画

按一次创作活动播放，不按每个像素／撤销命令重复整套动画；实际画布另层显示。

| 顺序／片段键 | 起点 State | 终点 State | 播放 | 自定义 Animation 提示词（可直接复制） |
| --- | --- | --- | --- | --- |
| 1. `draw_in` | `S01` | `S22` | 单次 | `Same reference character, crisp pixel art, fixed camera and unchanged proportions. Reach for a small stylus and position its tip over a waist-high drawing surface, steadying the edge with the other hand. End in the drawing-ready pose. No large canvas, desk or background.` |
| 2. `draw` | `S22` | `S22` | 循环 | `Same reference character, crisp pixel art, fixed camera and unchanged proportions. Make a few short controlled stylus marks, briefly inspect the drawing and return to the drawing-ready position. Keep the body, surface anchor and mouth still; no large arm scribbling or whole-canvas repaint effect.` |
| 3. `draw_out` | `S22` | `S01` | 单次 | `Same reference character, crisp pixel art, fixed camera and unchanged proportions. Lift the stylus, set it down gently and lower both hands into the default idle pose. Keep the body scale fixed; do not create a finished painting.` |

#### ART-04 查看、导出、展示自己的作品

拟可选举画动作；画作是可替换图层，数据导出／保存本身无需人体动画。

| 顺序／片段键 | 起点 State | 终点 State | 播放 | 自定义 Animation 提示词（可直接复制） |
| --- | --- | --- | --- | --- |
| 1. `art_up` | `S01` | `S23` | 单次 | `Same reference character, crisp pixel art, fixed camera and unchanged proportions. Pick up a small picture panel with both hands and raise it to chest height without covering the face. End in the artwork-showing pose. Leave the panel blank for a separate artwork layer.` |
| 2. `art_hold` | `S23` | `S23` | 循环 | `Same reference character, crisp pixel art, fixed camera and unchanged proportions. Hold the small picture panel steadily below the face, with one tiny grip adjustment and a still pause. Keep its corners fixed, mouth closed and expression restrained. Return to the same showing pose.` |
| 3. `art_down` | `S23` | `S01` | 单次 | `Same reference character, crisp pixel art, fixed camera and unchanged proportions. Lower the small picture panel and place it on a nearby surface with both hands, then return to the default idle pose. Keep the panel rigid; do not erase the picture as an effect.` |

#### PLAY-01 开始、观察、操作或放下九盏灯

观察循环和点按单次分开调度，不固定“观察后一定点按”；棋盘灯状态来自真实工具，当前未绑定道具窗口。

| 顺序／片段键 | 起点 State | 终点 State | 播放 | 自定义 Animation 提示词（可直接复制） |
| --- | --- | --- | --- | --- |
| 1. `puzzle_in` | `S01` | `S24` | 单次 | `Same reference character, crisp pixel art, fixed camera and unchanged proportions. Bring a small board to waist height supported by one hand and hover the other index finger above it, lowering the gaze. End in the puzzle-ready pose without generating readable symbols.` |
| 2. `puzzle_look` | `S24` | `S24` | 循环 | `Same reference character, crisp pixel art, fixed camera and unchanged proportions. Study the small board quietly with a tiny eye movement and a thoughtful still pause. Keep the hovering finger still and the mouth closed. Return to the same pose without pressing any button.` |
| 3. `puzzle_tap` | `S24` | `S24` | 单次 | `Same reference character, crisp pixel art, fixed camera and unchanged proportions. Tap one position on the small board once with the hovering index finger, then lift it back to the original position. Keep the board steady; no automatic light changes or victory gesture.` |
| 4. `puzzle_out` | `S24` | `S01` | 单次 | `Same reference character, crisp pixel art, fixed camera and unchanged proportions. Lower the small board, place it on a nearby surface and return the hands to the default idle pose. No throw, celebration or exaggerated disappointment.` |

#### MEDIA-01 找影片、打开小电视或平台页面

可选遥控器表现；搜索本身可只保持待机或思考。电视显示由插件完成，不画进角色序列。

| 顺序／片段键 | 起点 State | 终点 State | 播放 | 自定义 Animation 提示词（可直接复制） |
| --- | --- | --- | --- | --- |
| 1. `remote_in` | `S01` | `S25` | 单次 | `Same reference character, crisp pixel art, fixed camera and unchanged proportions. Pick up a small remote with one hand and hold it at waist height, thumb ready above a button. End in the remote-ready pose, mouth closed and feet planted.` |
| 2. `remote_tap` | `S25` | `S25` | 单次 | `Same reference character, crisp pixel art, fixed camera and unchanged proportions. Press one small remote button once with the thumb, pause briefly, then return to the same remote-ready pose. Keep the arm stable; do not animate a screen or change the outfit.` |
| 3. `remote_out` | `S25` | `S01` | 单次 | `Same reference character, crisp pixel art, fixed camera and unchanged proportions. Lower and set aside the small remote, then return the hand to the default idle pose. Mouth closed; no television appearing in the sprite.` |

#### MEDIA-02 看视频、查看当前画面或连续采样帧

拟朝右观看，左侧镜像；位置与电视朝向需业务接线，观看循环不等于连续识图已开启。

| 顺序／片段键 | 起点 State | 终点 State | 播放 | 自定义 Animation 提示词（可直接复制） |
| --- | --- | --- | --- | --- |
| 1. `watch_in` | `S01` | `S26` | 单次 | `Same reference character, crisp pixel art, fixed camera and unchanged proportions. Take one small repositioning step and turn the body partly toward the right, directing the gaze at a screen to the right. End in the watching reference pose. Turn the hips and feet with the torso; no generated screen.` |
| 2. `watch` | `S26` | `S26` | 循环 | `Same reference character, crisp pixel art, fixed camera and unchanged proportions. Watch a fixed point to the right quietly, with a tiny eye adjustment and a long still pause. Mouth closed, no automatic emotional reaction or continuous head tracking. Return to the same watching pose.` |
| 3. `watch_out` | `S26` | `S01` | 单次 | `Same reference character, crisp pixel art, fixed camera and unchanged proportions. Turn the head, torso, hips and feet gently back toward the viewer with one small step. End in the normal front-facing idle pose. No abrupt upper-body-only twist.` |

#### MEDIA-03 播放、暂停、停止、跳转、收起电视

复用遥控器；按钮动作单次，播放器实际控制成功后更新状态。

| 顺序／片段键 | 起点 State | 终点 State | 播放 | 自定义 Animation 提示词（可直接复制） |
| --- | --- | --- | --- | --- |
| 1. `remote_in` | `S01` | `S25` | 单次 | `Same reference character, crisp pixel art, fixed camera and unchanged proportions. Pick up a small remote with one hand and hold it at waist height, thumb ready above a button. End in the remote-ready pose, mouth closed and feet planted.` |
| 2. `remote_tap` | `S25` | `S25` | 单次 | `Same reference character, crisp pixel art, fixed camera and unchanged proportions. Press one small remote button once with the thumb, pause briefly, then return to the same remote-ready pose. Keep the arm stable; do not animate a screen or change the outfit.` |
| 3. `remote_out` | `S25` | `S01` | 单次 | `Same reference character, crisp pixel art, fixed camera and unchanged proportions. Lower and set aside the small remote, then return the hand to the default idle pose. Mouth closed; no television appearing in the sprite.` |

#### TOOL-01 看当前桌面、截图、列目录、打开授权程序

可选通用观察；无真实截图时不据图做反应，权限工具照旧由程序执行。

| 顺序／片段键 | 起点 State | 终点 State | 播放 | 自定义 Animation 提示词（可直接复制） |
| --- | --- | --- | --- | --- |
| 1. `curious_in` | `S01` | `S29` | 单次 | `Same reference character, crisp pixel art, fixed camera and unchanged proportions. Make one small head tilt and lower the gaze toward a nearby object, ending in the curious reference pose. Do not step, smile or open the mouth.` |
| 2. `curious_loop` | `S29` | `S29` | 循环 | `Same reference character, crisp pixel art, fixed camera and unchanged proportions. Hold the small head tilt and quietly inspect a point below, with a tiny eye movement and a brief pause. Keep both feet planted and the mouth closed. Return to the same curious pose.` |
| 3. `curious_out` | `S29` | `S01` | 单次 | `Same reference character, crisp pixel art, fixed camera and unchanged proportions. Bring the head and eyes calmly back to the default front-facing standing pose, hands relaxed and mouth closed. No extra gesture.` |

#### TOOL-02 查看天气

可选一次抬眼，天气不强制绑定情绪，不生成不存在的雨景或雨伞。

| 顺序／片段键 | 起点 State | 终点 State | 播放 | 自定义 Animation 提示词（可直接复制） |
| --- | --- | --- | --- | --- |
| 1. `weather` | `S01` | `S01` | 单次 | `Same reference character, crisp pixel art, fixed camera and unchanged proportions. Glance slightly upward and to one side as if checking outside, pause quietly, then return to the original forward gaze. Keep the mouth closed and feet grounded. No rain, umbrella or fixed happy or sad expression.` |

#### TOOL-03 闲时整理、形成打算、执行连续任务、调用 Skill 或咨询导师

通用思考只是可选占位；实际连续任务按各子动作组合，不把所有工具统一包装成重复点头。

| 顺序／片段键 | 起点 State | 终点 State | 播放 | 自定义 Animation 提示词（可直接复制） |
| --- | --- | --- | --- | --- |
| 1. `attend_in` | `S01` | `S27` | 单次 | `Same reference character, crisp pixel art, fixed camera and unchanged proportions. Notice a new message with a slight lift of the head and a small forward eye movement, then settle into the attentive pose. Keep the mouth closed, feet fixed and reaction restrained.` |
| 2. `think_in` | `S27` | `S28` | 单次 | `Same reference character, crisp pixel art, fixed camera and unchanged proportions. Lower the gaze slightly and bring one hand lightly toward the chin, ending in the quiet thinking pose. No worried frown, mouth movement or stepping.` |
| 3. `think_loop` | `S28` | `S28` | 循环 | `Same reference character, crisp pixel art, fixed camera and unchanged proportions. Pause thoughtfully with the hand near the chin, make one tiny eye movement, then remain still. Mouth closed, feet fixed, no repeated head shaking. Return to the same thinking pose.` |
| 4. `think_out` | `S28` | `S01` | 单次 | `Same reference character, crisp pixel art, fixed camera and unchanged proportions. Lower the hand from the chin, lift the gaze gently and settle into the default front-facing idle pose, mouth closed.` |

#### ENV-01 食材和掉落物出现、到期淡出

以苹果作样例；其他掉落物换其单独参考图。出生／下落轨迹／到期淡出由程序处理，不创建“透明空白 State”；自身→自身的 settle 为单次。

| 顺序／片段键 | 起点 State | 终点 State | 播放 | 自定义 Animation 提示词（可直接复制） |
| --- | --- | --- | --- | --- |
| 1. `drop_settle` | `O01` | `O01` | 单次 | `Same reference object, crisp pixel art, fixed camera and unchanged scale. Make one tiny soft settling squash and recovery, ending in exactly the original resting item pose. Keep the item anchor fixed; do not generate a character, floor, duplicates or a disappearance effect.` |

#### ENV-02 食材被拿起、被吃掉

物件被拿起时挂接手部或入包，吃完时按回执隐藏；单次轻反馈不重复播放，不用随机形变假装完整消耗。

| 顺序／片段键 | 起点 State | 终点 State | 播放 | 自定义 Animation 提示词（可直接复制） |
| --- | --- | --- | --- | --- |
| 1. `drop_taken` | `O01` | `O01` | 单次 | `Same reference object, crisp pixel art, fixed camera and unchanged scale. Make one tiny upward anticipation wobble and return to the same item shape and scale. Keep the root anchor fixed; the application will attach or remove the item afterward. Do not duplicate or dissolve it.` |

#### ENV-03 种下后的芽苗生长

前三段按真实生长阶段各播一次，开花保持循环可选；没有自动结果或收获。

| 顺序／片段键 | 起点 State | 终点 State | 播放 | 自定义 Animation 提示词（可直接复制） |
| --- | --- | --- | --- | --- |
| 1. `grow_leaves` | `O02` | `O03` | 单次 | `Same reference object, crisp pixel art, fixed camera and unchanged scale. Let the small sprout slowly extend its stem and unfold a few leaves, ending in the leafy reference plant. Keep the root anchored, pixel scale and colors consistent. No flower, fruit, pot or duplicate stems.` |
| 2. `grow_bud` | `O03` | `O04` | 单次 | `Same reference object, crisp pixel art, fixed camera and unchanged scale. Let one small closed bud emerge at the top of the same leafy plant. End in the closed-bud reference pose with the root fixed. Do not open petals or grow fruit.` |
| 3. `grow_flower` | `O04` | `O05` | 单次 | `Same reference object, crisp pixel art, fixed camera and unchanged scale. Open the one closed flower bud gently into the reference blossom. Keep the stem and root fixed, preserve the leaf shapes and finish with a still open flower. No fruit or seed explosion.` |
| 4. `plant_idle` | `O05` | `O05` | 循环 | `Same reference object, crisp pixel art, fixed camera and unchanged scale. Make a barely visible gentle leaf and petal sway and return to the same open-flower pose. Keep the root fixed, flower count unchanged and pixel scale stable.` |

#### ENV-04 食堂道具

两行是成品与器皿的不同素材，不是前后串行。面包可换同尺寸布丁／茶；静态盘杯桌椅无需为了凑循环强行动画。

| 顺序／片段键 | 起点 State | 终点 State | 播放 | 自定义 Animation 提示词（可直接复制） |
| --- | --- | --- | --- | --- |
| 1. `meal_idle` | `O06` | `O06` | 循环 | `Same reference object, crisp pixel art, fixed camera and unchanged scale. Keep the plate and bread still with only one tiny highlight variation that returns to the original palette. Do not breathe, grow a face, change food type or generate a background.` |
| 2. `bowl_stir` | `O07` | `O07` | 循环 | `Same reference object, crisp pixel art, fixed camera and unchanged scale. Move the spoon through one small circular stirring cycle inside the bowl, then return it to the original angle. Keep the bowl footprint fixed and its contents contained. No hands, character or new food appearing.` |

#### ENV-05 阅读角、背包、仓库道具

书本与背包是两个独立对象的可选开合，不是连续四段；柜门椅子先静态制作，实体场景待接。

| 顺序／片段键 | 起点 State | 终点 State | 播放 | 自定义 Animation 提示词（可直接复制） |
| --- | --- | --- | --- | --- |
| 1. `obj_book_open` | `O08` | `O09` | 单次 | `Same reference object, crisp pixel art, fixed camera and unchanged scale. Open the same small book gently around its binding into the reference open-book shape. Keep its binding anchor and pixel scale fixed, pages blank, with no extra book or background.` |
| 2. `obj_book_close` | `O09` | `O08` | 单次 | `Same reference object, crisp pixel art, fixed camera and unchanged scale. Close the same small book gently around its binding into the reference closed-book shape. Keep its binding anchor and pixel scale fixed, with no extra book or background.` |
| 3. `obj_bag_open` | `O10` | `O11` | 单次 | `Same reference object, crisp pixel art, fixed camera and unchanged scale. Part the small pouch opening gently while keeping the base and side seams in place. End in the open-pouch reference shape. No items, hands or character appearing.` |
| 4. `obj_bag_close` | `O11` | `O10` | 单次 | `Same reference object, crisp pixel art, fixed camera and unchanged scale. Close the small pouch opening gently into the reference closed shape, keeping the base anchor and pixel scale fixed. No items, hands or character appearing.` |

#### ENV-06 浮动小电视

仅可选外壳指示灯；电视画面与音画同步继续使用实际播放器，不用 PixelLab 合成视频内容。

| 顺序／片段键 | 起点 State | 终点 State | 播放 | 自定义 Animation 提示词（可直接复制） |
| --- | --- | --- | --- | --- |
| 1. `tv_light` | `O12` | `O12` | 循环 | `Same reference object, crisp pixel art, fixed camera and unchanged scale. Pulse only the tiny status light softly once and return it to the original brightness. Keep the television frame and empty screen rectangle perfectly fixed. Do not generate video content, screen noise, characters or camera movement.` |

#### ENV-07 像素创作台作品

月灯仅作为可选桌面作品示例，不会自动添加到正式档案。现有作品动画最多 8 帧，不能套用角色包的片段上限。

| 顺序／片段键 | 起点 State | 终点 State | 播放 | 自定义 Animation 提示词（可直接复制） |
| --- | --- | --- | --- | --- |
| 1. `art_glow` | `O13` | `O13` | 循环 | `Same reference object, crisp pixel art, fixed camera and unchanged scale. Make the small lantern glow brighten and dim gently once, returning to its original brightness. Keep its frame, crescent ornament and base completely still, with no particles, text or background.` |

### 接入前逐项核对

- 起止帧对上本表对应 State 的实际 PNG，且循环段回到同一运动相位；A→B 过渡播一次后切换到 B 的保持段。
- 持物接线前，检查画面里是否同时留下了生成器占位物和程序挂接道具；手指遮挡层与 `hand` 坐标逐帧核对。
- 待机闭嘴、疲劳呼吸闭嘴、睡眠为侧卧、转身有脚步、点击不抬升、落地不先插待机、行走无图内整体平移。
- 新增进入／退出片段时仍需提供非空保持 `loop`；单次动作结束可接末帧保持并由程序退出，不能把空循环写进资源包。
- 程序隐藏／淡入淡出／坐标移动、道具消耗、食堂计时、真实工具回执单独验证；制作提示词不改变这些规则。
- 本表覆盖的是素材生产路径。`S16` 等不可见椅子坐姿需要真实场景座位锚点，`S15/S21/S22` 的操作面需要实际道具层；未接场景时不可把角色切进悬空坐姿来冒充实现。

## 持续维护要求

每次新增或修改角色动作、插件工具对应的可见行为、交互方式、场景或动画包时，在同一改动中：

- 新增或更新稳定动作编号、中文含义、业务入口和状态标记；重命名不复用旧编号，移除动作保留“已停用”记录。
- 说明是独立动画、现有动画复用、纯逻辑操作还是尚未实现的场景，并写出默认爱丝特兰的实际表现。
- 核对进入／循环／退出、朝向、道具、能否中断、实际完成回执与回退；涉及组合行为时链接已有动作编号。
- 同步维护单独的 PixelLab 静态 State 提示词，以及每个动作的自定义 Animation 提示词、起点 State、终点 State 和单次／循环标记。新增 State 保留稳定 S／O 编号，组合动作拆成可复用片段；纯 UI／逻辑动作明确无须新生成，不能漏列或虚构已接线。
- 改变 `character.json` 或导入素材后，重新统计上面的帧数、时长和回退。素材缺失时明确标记，不用建议状态名冒充已有实现。
- 逻辑接线与素材分别确认；只完成其中一项时，不把另一项标成完成。不因更新本表自动新增人物性格、阶段或小说正式设定。

本表是唯一人工维护的动作总表。README 和序列帧文档只保留入口链接；以后如需 CSV／表格文件，从本表导出，不另建一份会独立漂移的手工清单。

## 核对入口

- [角色状态、手势和姿态工具](../plugins/widget/Widget.cpp)、[小物事件到角色姿态的映射](../plugins/widget/WorldEvents.inc)、[休息姿态事件](../plugins/widget/RhythmEvents.inc)。
- [自主队列命令定义](../src/ActivityPlan.cpp)、[实际调度与回执处理](../src/ActivityRuntime.inc)、[小物业务时长与完成条件](../include/Hibernation/SmallWorld.h)。
- [区域与采集](../plugins/habitat/HabitatForaging.inc)、[食堂工具](../plugins/canteen/Canteen.cpp)、[库存及制作流程](../include/Hibernation/CanteenKitchen.h)。
- [资料阅读](../plugins/reading/Reading.cpp)、[文字创作](../plugins/atelier/Atelier.cpp)、[像素创作](../plugins/pixel-canvas/PixelCanvas.cpp)、[九盏灯](../plugins/pocket/Pocket.cpp)。
- [小电视](../plugins/television/Television.cpp)、[安静节律说明](quiet-rhythm.md)、[桌面物资说明](desktop-supplies.md)、[序列帧与资源规范](pixel-sequences.md)。
