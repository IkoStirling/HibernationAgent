# 原生睡眠拖动检查的动效前提

本轮候选的 `Hibernation_CircadianNative` 在“睡眠中仍有物理提起位移”检查失败。原脚本在独立目录重跑也出现相同失败，原失败和重跑均保留；未先降低断言或更改产品抬升实现。

初步只读检查发现可能的首帧竞争：鼠标拖动处理先设 `dragging=true`，`WidgetDragMotion::height(grab_time)` 按设计为 0，之后随 120ms 曲线升高。但局部私有桌面时间探针推翻了“仅首帧竞争解释这次失败”的假设：最多 40 次的采样预算内，实际取得 12 个拖动样本，API 往返计入约 738ms 观察范围，全部 `lift=0` 且 `reduced_motion=true`。

测试原配置没有固定 `respectSystemMotion=false`，因此产品按默认行为尊重系统的减动效偏好。减动效情况下 `grab(..., reduced=true)` 的目标高度本来就是 0。该证据指向测试前提与当前环境不一致，不能称为产品抬升损坏。

另一独立私有桌面仅在插件配置中显式指定 `reducedMotion=false`、`respectSystemMotion=false`，没有改变 Windows 设置。首次观察到约 14.24px 提起，距鼠标移动消息返回约 68.96ms，与已有 120ms 动画设计一致。

## 只修测试

`tests/CircadianNative.ps1` 的正常动效组固定上述配置，并保留首次样本。在 300ms 的硬上限内观测正位移；该上限包含鼠标消息分发和 API 响应完成时间，基于 120ms 动画及有限的 16ms 主循环容忍，不使用无限等待，也不把较晚才取得的正位移记成及时通过。

另以独立测试档案、同样的私有桌面机制验证减动效组：实际拖动仍成立，120ms 之后仍观察到零提起位移，且零拖醒概率下保持睡眠。产品默认行为、Widget/Rhythm 源码和 Windows 动画设置均未修改。

修复后的完整脚本本地 29 项通过，约 28.23 秒。正常组首次位移约 14.56px，在输入组开始后 105.96ms、鼠标移动消息返回后 54.98ms 观察到；减动效组实际取得 4 个样本，全部零位移。主代理的后续统一回归另行记录。

全部证据位于 `artifacts/optimization-3h/20261030-113742/circadian-investigation/`：

- `original-retest/` 与上级 `circadian-native-retest.log`：未修脚本的重复失败。
- `timed-probe/motion-probe.json`：原环境策略下的实际 12 个拖动样本；40 是上限，不是完成样本数。
- `timed-probe-explicit-motion/motion-probe.json`：显式正常动效对照。
- `corrected-fixture/acceptance.json`、`motion-samples.json`：修复测试的完整结果和初始样本。
- `diagnosis.json`：初始假设、反证、修复范围和真实时间差。

测试只向自身未激活桌面的窗口发送鼠标消息，没有移动用户鼠标、切换用户桌面、读取真实屏幕或调用模型。
