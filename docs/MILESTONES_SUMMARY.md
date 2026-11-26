# 里程碑交付摘要（P3.8 / P3.9 / P3.10 / P3.11 / P3.12 / P3.13 / P3.14 / P3.15）

> [!CAUTION]
> **仅限测试与研究使用，不作正式环境使用（FOR TESTING & RESEARCH ONLY — NOT FOR PRODUCTION USE）**。

## 1. 里程碑演进与关键模块 SHA-256

| 里程碑 | 核心目标 | 关键补丁 / 模块 | SHA-256（模块 / 补丁） |
|---|---|---|---|
| **P3.8** | R3 USB HS-only DTB + R1 显示 DSI PHY AHB `prepare` 生命周期修复 | `0002-p3_9-r3-usb-hs-only-dts.patch`<br>`0003-p3_9-r1-dsi-phy-ahb-lifecycle.patch` | `46f3e08d50219db1b13ad32234c27335824c9c202a3eb7b6897843b2c3064fb6`<br>`10937844d732cac903d6e654f85c6a70e401896f5f597517cbb3241e5cb01a61` |
| **P3.9** | R2 GPU/GMU HFI IRQ 掩码过滤、posted-write readback 与 `dma_rmb()` 修复；撤销系统侧保活规则，恢复默认 `PM: auto` | `0004-p3_9-r2-gmu-hfi-irq-memory-order.patch`<br>`msm.ko` | `260da0675a2732bc78e54658753eee6037e30a3aac43943562002a5770093f05`<br>`344a525b701afcb0a4f7765e93bbedb45db591e26b0d4f979b0d5f7f508990c2` |
| **P3.10** | S2R (`deep` / `s2idle`) 休眠唤醒后 `synaptics_tcm` 触控重初始化与命令应答排空修复 | `0005-p3_10-s2r-synaptics-tcm-resume.patch`<br>`synaptics_tcm.ko` | `f883399c84be0cdbd7403ab31d2465e6f063a8ac1f53203b16a860cc82b737e3`<br>`4ad831e30716429a28d4d1f8c3d8fe20521f380ba63a0ed7db6097be488b6eba` |
| **P3.11** | `qcom_battmgr` 墙充 `type=9` 限流映射与 `COMMU_TIME_OUT >= 2` VoocPHY 熔断回落标准 `5V/2A` 修复 | `0006-p3_11-qcom-battmgr-brick-vooc-fix.patch`<br>`qcom_battmgr.ko` | `27e72dfe04b569887156ef1a58ae900a18417bb07c16b084c0439f7ed817783a` (`qcom_battmgr.ko`) |
| **P3.12** | Wi-Fi 固定静态 IP (`<STATIC_IP>/24`) + `qcom_battmgr` `0x10004` PDO 解码、`0x58/0x5A/0x78/0x79` 通知、阶梯 AICL 消除塌陷 | `0007-p3_12-qcom-battmgr-pd-pps-aicl.patch`<br>`qcom_battmgr.ko` | `dbc0114d058e443c2c499899f120ac4ba379ec5190eb4e18f75e2935ed259599`<br>`9dc1b4cb12682d73359364bb6662bd8189438a740116c220f212bd941929ab1c` |
| **P3.13** | 未插电黑屏 180s 自动 `deep` 休眠 (`acepro-idle-suspend.service`) + `qcom_battmgr` `mAh`/`uAh` 满充容量导出修复（消除 `upowerd` `nan`） | `0008-p3_13-qcom-battmgr-unit-mah.patch`<br>`qcom_battmgr.ko` | `8fc2d72a39fb3925d186aa33ed4103705e407eac01d1080e3baad7c35cc595e9` (`qcom_battmgr.ko`) |
| **P3.14** | `qcom_battmgr` 256B OEM 电量计缓冲异步刷新与缓存直读（消除 450ms sysfs 阻塞，读取降至 `0.03~33ms`，恢复 `lxc-webui` 功率采集） | `0009-p3_14-qcom-battmgr-async-oem-cache.patch`<br>`qcom_battmgr.ko` | `126d536bbd324bd5114ca220c3eede4041ea939d74d354570d11066d33e211d4` (`qcom_battmgr.ko`) |
| **P3.15** | WCN6855 `s2idle` Connected Standby 保活与来包自动唤醒（`d0_wow=N, keep_d0=N` 走 D3hot+MHI M3 经 Root Port `PCIe PME` 唤醒、`pcie-qcom` 保持最低 OPP、`wow` NACK `-EBUSY` 回滚防断链、动态 `wiphy` WoWLAN 钩子） | `0010-p3_15-ath11k-wowlan-pcie-pme-s2idle.patch`<br>`Image.gz` / `ath11k.ko` / `ath11k_pci.ko` / `qrtr-mhi.ko`<br>`acepro-wowlan` / `acepro-idle-suspend.py` | `1f37d83044ceed92869ef5f99c68b49fc2f99c15f193cc680c42daf6bc282e91455` (`Image.gz`)<br>`4a6d12e5b538982c3990ab242f55bc80185fc1512fc6dfc12b95176c6788b62a` (`ath11k.ko`)<br>`f08dc2b65f1543de14dae78c28c683a75dfef20ef85444632d6293300ae42035` (`ath11k_pci.ko`)<br>`9de8a0af84206f2f78b3ca106a0e0168f5f6a7f923c121f8d88265e2497cb78d` (`qrtr-mhi.ko`) |

## 2. P3.10 S2R 4 轮实机验收矩阵 (`PASS`)

| 轮次 | 模式 / 触发方式 | `WIFI` 状态 | `suspend_stats` | 触控 (`synaptics-tcm`) | 显示 / GPU / 外设 |
|---|---|---|---|---|---|
| **Cycle 1** | `deep` / `systemd` | `enabled` | `success=1, fail=0` | `128 bytes` 配置保留，滑动解锁正常 | 单次电源键亮/熄屏，`es2gears` + GPU/GMU `auto` 挂起，`card 0` 在线，`pstore=0` |
| **Cycle 2** | `deep` / `systemd` | `disabled` (`D0`) | `success=2, fail=0` | `128 bytes` 配置保留，触控正常 | 唤醒后 `nmcli radio wifi on` 正常重连 |
| **Cycle 3** | `deep` / `systemd` | `enabled` | `success=3, fail=0` | `128 bytes` 配置保留，零异常 | `es2gears` + GPU/GMU `suspended` (`gmu` IRQ `0`)，`card 0` 在线，`systemctl --failed=0` |
| **Cycle 4** | `s2idle` / `systemd` | `enabled` | `success=4, fail=0` | 亮屏与触控滑动正常 | `mem_sleep` 恢复 `s2idle [deep]`，`wlp1s0` 在线，`gpu/gmu=suspended` |
