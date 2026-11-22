# 里程碑交付摘要（P3.8 / P3.9 / P3.10）

> [!CAUTION]
> **仅限测试与研究使用，不作正式环境使用（FOR TESTING & RESEARCH ONLY — NOT FOR PRODUCTION USE）**。

## 1. 里程碑演进与关键模块 SHA-256

| 里程碑 | 核心目标 | 关键补丁 / 模块 | SHA-256（模块 / 补丁） |
|---|---|---|---|
| **P3.8** | R3 USB HS-only DTB + R1 显示 DSI PHY AHB `prepare` 生命周期修复 | `0002-p3_9-r3-usb-hs-only-dts.patch`<br>`0003-p3_9-r1-dsi-phy-ahb-lifecycle.patch` | `46f3e08d50219db1b13ad32234c27335824c9c202a3eb7b6897843b2c3064fb6`<br>`10937844d732cac903d6e654f85c6a70e401896f5f597517cbb3241e5cb01a61` |
| **P3.9** | R2 GPU/GMU HFI IRQ 掩码过滤、posted-write readback 与 `dma_rmb()` 修复；撤销系统侧保活规则，恢复默认 `PM: auto` | `0004-p3_9-r2-gmu-hfi-irq-memory-order.patch`<br>`msm.ko` | `260da0675a2732bc78e54658753eee6037e30a3aac43943562002a5770093f05`<br>`344a525b701afcb0a4f7765e93bbedb45db591e26b0d4f979b0d5f7f508990c2` |
| **P3.10** | S2R (`deep` / `s2idle`) 休眠唤醒后 `synaptics_tcm` 触控重初始化与命令应答排空修复 | `0005-p3_10-s2r-synaptics-tcm-resume.patch`<br>`synaptics_tcm.ko` | `f883399c84be0cdbd7403ab31d2465e6f063a8ac1f53203b16a860cc82b737e3`<br>`4ad831e30716429a28d4d1f8c3d8fe20521f380ba63a0ed7db6097be488b6eba` |

## 2. P3.10 S2R 4 轮实机验收矩阵 (`PASS`)

| 轮次 | 模式 / 触发方式 | `WIFI` 状态 | `suspend_stats` | 触控 (`synaptics-tcm`) | 显示 / GPU / 外设 |
|---|---|---|---|---|---|
| **Cycle 1** | `deep` / `systemd` | `enabled` | `success=1, fail=0` | `128 bytes` 配置保留，滑动解锁正常 | 单次电源键亮/熄屏，`es2gears` + GPU/GMU `auto` 挂起，`card 0` 在线，`pstore=0` |
| **Cycle 2** | `deep` / `systemd` | `disabled` (`D0`) | `success=2, fail=0` | `128 bytes` 配置保留，触控正常 | 唤醒后 `nmcli radio wifi on` 正常重连 |
| **Cycle 3** | `deep` / `systemd` | `enabled` | `success=3, fail=0` | `128 bytes` 配置保留，零异常 | `es2gears` + GPU/GMU `suspended` (`gmu` IRQ `0`)，`card 0` 在线，`systemctl --failed=0` |
| **Cycle 4** | `s2idle` / `systemd` | `enabled` | `success=4, fail=0` | 亮屏与触控滑动正常 | `mem_sleep` 恢复 `s2idle [deep]`，`wlp1s0` 在线，`gpu/gmu=suspended` |
