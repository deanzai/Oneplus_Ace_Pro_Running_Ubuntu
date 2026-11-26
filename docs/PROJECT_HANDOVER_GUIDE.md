# 一加 Ace Pro (PGP110 / ovaltine) 主线 Linux 移植完整工程指导手册

> **文档版本**：v2.8 (P3.15 里程碑归档：`s2idle` Connected Standby 下保持 Wi-Fi 连接与来包自动唤醒)  
> **更新时间**：2026-11-27  
> **目标系统**：Ubuntu Desktop / Phosh Mobile Linux (Linux 7.3.0-rc4 mainline)  
> **目标机型**：一加 Ace Pro (OnePlus Ace Pro / PGP110 / 内部代号 `ovaltine` / 项目代号 `21841`)  
> **当前实机运行版本**：**P3.15 基线**（`7.3.0-rc4-ovaltine-p3-7-ufs1` 内核 `p3_15_boot.img` + R3 `vendor_boot_a` DTB + R1+R2 `msm.ko` + P3.10 `synaptics_tcm.ko` + P3.14 `qcom_battmgr.ko` + P3.15 `ath11k.ko`/`ath11k_pci.ko`/`qrtr-mhi.ko`，支持黑屏 `s2idle` Connected Standby、来包经 `PCIe PME (IRQ 184)` 自动唤醒且不影响 `qcom_battmgr` / `lxc-webui` / Stage 1 功耗策略，实机验证 `boot_id`: `5ee0f715-22f6-4e66-9214-cc75628c4fd7`；归档路径：[development/milestones/P3_15/](file://<WORKSPACE>/development/milestones/P3_15/)）

---

## 目录
1. [项目概览与硬件基础信息](#1-项目概览与硬件基础信息)
2. [绝对安全红线与双槽位架构约束](#2-绝对安全红线与双槽位架构约束)
3. [硬件子系统移植状态与引脚矩阵](#3-硬件子系统移植状态与引脚矩阵)
4. [已知缺陷深度复盘与核心根因 (RCA)](#4-已知缺陷深度复盘与核心根因-rca)
5. [内核源码结构与构建打包 SOP](#5-内核源码结构与构建打包-sop)
6. [实机刷入、启动与增量更新 SOP](#6-实机刷入启动与增量更新-sop)
7. [用户空间服务与系统配置清单](#7-用户空间服务与系统配置清单)
8. [AI-Development-Orchestrator 开发治理规范](#8-ai-development-orchestrator-开发治理规范)
9. [P3.6 / P3.7 实测与深度功耗排查（R1→R5 优先级）](#9-p36--p37-实测与深度功耗排查r1r5-优先级)
10. [P3.8 / P3.9 研发进度与里程碑交付](#10-p38--p39-研发进度与里程碑交付)
11. [后续演化路线图与未完成任务](#11-后续演化路线图与未完成任务)

---

## 1. 项目概览与硬件基础信息

### 1.1 设备参数矩阵
* **SoC**：高通骁龙 8+ Gen 1 (SM8475 / 架构代号 `cape`)，4nm 台积电制程
* **CPU**：1x Cortex-X2 @ 3.19GHz + 3x Cortex-A710 @ 2.75GHz + 4x Cortex-A510 @ 1.80GHz
* **GPU**：Adreno 730 @ 900MHz (支持 Vulkan 1.3 / OpenGL ES 3.2，依赖 `a730_zap.mdt`)
* **RAM / ROM**：16GB LPDDR5 + 256GB/512GB UFS 3.1
* **PMIC 阵列**：PM8350 + PM8350B + PM8350C + PM8450 + PMK8350 + PMR735A
* **屏幕**：6.7 英寸 京东方 AMOLED (BOE RM692E5)，分辨率 1080x2412，支持 60Hz / 90Hz / 120Hz，DSC 1.2 压缩
* **触摸屏**：Synaptics S3910 (SPI4 接口，支持多点触控与手势)
* **音频**：
  * 主编解码器：WCD9385 (SoundWire 接口)
  * 双智能功放：双路 NXP TFA9874 (I2C13，地址 `0x34` / `0x35`，Tertiary TDM 接口)
  * DSP 拓扑：高通 AudioReach (`audioreach.bin` + `OnePlus-Ace-Pro-tplg.bin`)
* **无线网络**：WCN6855 (PCIe 接口 Wi-Fi 6E + UART20 蓝牙 5.2)
* **USB / 充电**：Type-C USB 2.0/3.0，NXP PTN3222 eUSB2 Repeater + FSA4480 Mux，支持 150W SuperVOOC (原厂)

### 1.2 关键设备匹配标识 (Device Matching IDs)
ABL (Android Bootloader) 启动时强校验设备树节点的设备识别码：
```dts
model = "OnePlus Ace Pro (PGP110 / ovaltine)";
compatible = "oneplus,ovaltine", "qcom,sm8475", "qcom,sm8450";
qcom,msm-id = <0x213 0x10000 0x21c 0x10000 0x212 0x10000>;
qcom,board-id = <0x10008 0x00>;
oplus,project-id = <0x5552 0x5551>;  /* 21841 */
oplus,region-id = <0x01 0x02>;
```

---

## 2. 绝对安全红线与双槽位架构约束

### 2.1 槽位隔离原则 (Slot Isolation)
> [!CAUTION]
> **绝对红线：任何刷写、抹除、格式化操作严禁触碰 Slot `_b`！**
> - **Slot `_a`**：专属分配给 Linux / Ubuntu 系统开发与调试。
> - **Slot `_b`**：保留原厂出厂 ColorOS 15（基线版本 `PGP110_15.0.0.1901(CN01) H.25`）作为终极急救保底。
> - 若 Slot `_a` 发生不可恢复的启动崩溃，只需在 fastboot 执行 `fastboot --set-active=b` 即可无损回退 ColorOS 官方系统。

### 2.2 全分区出厂镜像冷备份 (Golden Backup)
已完成全量 136 个 UFS 分区的 SHA256 校验归档：
* **备份存储路径**：`<WORKSPACE_ROOT>/pgp110_backup/`
* **校验索引**：`pgp110_backup/SHA256SUMS.txt`
* **关键保留镜像**：
  * `boot_b.img`, `vendor_boot_b.img`, `dtbo_b.img`, `vbmeta_b.img`, `vbmeta_system_b.img`
  * `super.img` (动态分区母盘，包含 system, vendor, odm, product, system_ext)
  * `modemst1`, `modemst2`, `fsg`, `persist` (基带射频校准与出厂唯一标定数据)

### 2.3 分区映射与大小规范 (Slot `_a`)
```text
/dev/block/by-name/boot_a          -> 192 MiB (0xC000000 / 201,326,592 字节，主线内核 Image.gz + initramfs)
/dev/block/by-name/vendor_boot_a   -> 192 MiB (0xC000000 / 201,326,592 字节，DTB: sm8475-oneplus-ovaltine.dtb)
/dev/block/by-name/dtbo_a          -> 24 MB (DTBO: sm8475-ovaltine.dtbo)
/dev/block/by-name/vbmeta_a        -> 8 MB  (AVB 禁用标志)
/dev/block/by-name/userdata        -> 450+ GB (Ubuntu rootfs EXT4, 自动扩展)
```

---

## 3. 硬件子系统移植状态与引脚矩阵

| 子系统 | 状态 | 主线驱动/模块 | 核心引脚 / 总线配置 | 固件依赖 |
| :--- | :---: | :--- | :--- | :--- |
| **UFS 3.1 存储** | **PASS** | `ufs_qcom` + `qcom-qmp-ufs-phy` | Reset: GPIO 210, PHY: `0x01d80000` | 内建 |
| **屏幕显示** | **PASS** | `panel-boe-rm692e5.c` + `msm_drm` | Reset: GPIO 3, TE: GPIO 86, Vout: GPIO 90, VDDR: GPIO 180; Lane rate: 1100 Mbps | 需初始化序列 |
| **多刷新率切换** | **PASS** | `panel-boe-rm692e5.c` (DCS) | 60Hz: `FE 40 / BD 00 / FE 00`<br>90Hz: `FE 40 / BD 06 / FE 00`<br>120Hz: `FE 40 / BD 05 / FE 00` | 驱动原生支持 |
| **电容触控** | **PASS** | `synaptics_tcm_spi.c` | SPI4, CS: 0, IRQ: GPIO 21, Reset: GPIO 20, AVDD: GPIO 97 | 自动识别 |
| **GPU 加速** | **PASS** | `msm` + `a6xx_gpu` (Adreno 730) | `3d00000.gpu`, GMU: `3d6a000.gmu` (PM: auto) | `a730_zap.mdt` |
| **Wi-Fi 6E** | **PASS** | `ath11k_pci` | PCIe 总线, PMIC RF 供电规约 | `qca6390/*` / `wlan/*` |
| **蓝牙 5.2** | **PASS** | `hci_uart` (`qcom,wcn6855-bt`) | UART20, 波特率 3200000 | `hpnv21.b80` 等 |
| **USB Gadget** | **PASS** | `dwc3-qcom` + `configfs` | NXP PTN3222 (I2C14 @ 0x4f), FSA4480 (@ 0x42), eUSB2 PHY | 内建 |
| **双立体声扬声器**| **PASS** | `tfa9874.c` + `snd-sc8280xp` | I2C13 @ `0x34` (L, GPIO 50), `0x35` (R, GPIO 51), Tertiary TDM | `OnePlus-Ace-Pro-tplg.bin` |
| **耳机/麦克风** | **PASS** | `wcd938x` + `soundwire` | SoundWire swr1 / swr2, WCD9385 Reset GPIO 112 | `wcd938x_*.mbn` |
| **电池与充电** | **PASS** | `qcom_battmgr.c` | PMIC GLINK, USB-ID GPIO 174 | ADSP 通信 |
| **容器网络 (LXC)** | **PASS** | `nf_nat`, `nft_nat`, `nft_masq` | `lxcbr0` (10.0.3.1/24), nftables postrouting NAT | 内核模块 |
| **摄像头 (CamX)** | **待开发**| `qcom-camss` | 索尼 IMX766 (主摄) + OV08D (超广角) + OV02B (微距) | 需 ISP/CamX 端口 |
| **基带调制解调器** | **待开发**| `qcom_q6v5_pas` + `ipa` | MPSS / Modem QMI / QRTR 数据面 | `modem.mdt` |
| **屏下指纹识别** | **待开发**| 汇顶科技超薄屏下光学指纹 | SPI 总线 + TZ 安全区通讯 | 依赖 TrustZone / TEE |

---

## 4. 已知缺陷深度复盘与核心根因 (RCA)

### 4.1 屏幕 1/4 处闪屏、撕裂与花屏
* **问题现象**：屏幕顶部约 1/4 区域（前 600 行扫描线）出现横向抖动撕裂与彩色花屏。
* **根因分析**：
  * BOE RM692E5 面板在 1080x2412@120Hz 下需要高像素吞吐，原驱动 DSI Lane Clock 配置过低（~980 Mbps/lane），导致 DSI 接收端与显示时序发生 FIFO 欠载（Underrun）。
* **最终修复方案**：
  * 在 `panel-boe-rm692e5.c` 中将 DSI 速率提升至 **1100 Mbps/lane**：
    * `byte_clk = 137.5 MHz`
    * `pclk = 183.33 MHz`
  * 配合 DPU1 DSC 1.2 压缩时序，花屏与横纹撕裂完全消除。

### 4.2 屏幕画面卡住但后台正常（GPU GMU Watchdog / HFI 假超时死锁，已在 P3.9-R2 根治）
* **问题现象**：系统开启 GPU/GMU Runtime PM 后，运行一段时间或多次息屏/唤醒后触发 `Message HFI_H2F_MSG_GX_BW_PERF_VOTE id ... timed out waiting for response` 与 `Timeout waiting for GMU OOB set GPU_SET: 0x0`，屏幕画面定格，工作队列死锁在 `a6xx_gmu_set_oob` / `a6xx_hfi_wait_for_msg_interrupt`。
* **历史临时规避 (P3.5–P3.8)**：曾通过 `/etc/udev/rules.d/80-gpu-pm.rules` 与 `/etc/tmpfiles.d/gpu-pm.conf` 强制将 `3d00000.gpu` 与 `3d6a000.gmu` 锁定为 `power/control=on`。
* **P3.9-R2 实证根因分析 (`TASK-20261120-010302`)**：
  1. `a6xx_hfi_irq()`（`a6xx_gmu.c`）读取 `REG_A6XX_GMU_GMU2HOST_INTR_INFO` 后未经 `A6XX_HFI_IRQ_MASK` 过滤直接写 `INTR_CLR = status`，误清除了由轮询线程独占消费的 `MSGQ`（`BIT(0)`）与 OOB ACK 位，导致 HFI 应答虽已写入 `rspq`（`rspq_r != rspq_w`）却引发 1 秒假超时及后续 `Unexpected message id` / OOB 超时。
  2. `a6xx_gmu_irq_disable()` 先调用 `disable_irq()` 再屏蔽中断（且缺少 `gmu_read()` 刷出 posted write），`a6xx_gmu_resume()` 在冷/热启前后缺少 `GMU2HOST_INTR_MASK` / `INTR_CLR` 屏蔽与 readback 刷出窗口控制。
  3. `a6xx_hfi_queue_read()` 缺少 `dma_rmb()` 配对写侧 `dma_mb()`，`a6xx_hfi_wait_for_msg_interrupt()` 轮询未兼顾检查 `HFI_RESPONSE_QUEUE` 非空。
* **最终正式修复方案 (P3.9)**：
  * 在 `a6xx_gmu.c` 与 `a6xx_hfi.c` 完成中断掩码过滤（未认领返回 `IRQ_NONE`）、posted-write readback 刷出、`dma_rmb()` 与 `rspq` 非空轮询修复（补丁：`p3_9_r2_gmu_hfi_irq.patch`）。
  * **已正式撤销** `/etc/udev/rules.d/80-gpu-pm.rules` 与 `/etc/tmpfiles.d/gpu-pm.conf`（备份保留于设备端 `/root/p3_8_gpu_pm_backup/`），实机重启后默认 `power/control=auto` 通过 20 轮 + 90s 及自然开机 10 轮 + 60s 压测。

### 4.3 待机温度偏高与 NoC 总线带宽死锁 (P3.4 解锁 `sync_state`)
* **问题现象**：系统待机状态下机身明显温热，CPU 使用率极低但表面测温达 53°C。
* **根因分析**：
  * SM8475 的互联总线驱动 `qnoc-sm8450` 挂载在设备树 `crypto@1dfa000` 节点上。由于该加密引擎在主线缺少特定固件支持，其驱动未正常 probe，导致整机 NoC 互联总线迟迟无法触发 `sync_state`。
  * `llcc_mc` 与 `ebi`（DDR 内存总线）带宽被永久钉死在 `INT_MAX`（2.14 GB/s），内存控制器满频运行产生严重静态功耗发热。
* **最终修复方案**：
  * 在 `sm8475-oneplus-ovaltine.dts` 中显式禁用该节点：
    ```dts
    &crypto {
        status = "disabled";
    };
    ```
  * 禁用后 `qnoc-sm8450` 立即完成 `sync_state()`，空闲内存带宽投票不再钉死 `INT_MAX`（P3.4 后降为 CPU 温度 47~50°C，P3.6/P3.7 受控待机窗口 `cpuss0-thermal` 中位数为 44.80°C；剩余空闲功耗保活链详见第 9 节 R1→R5 排查）。

### 4.4 电池管理器中断风暴与墙充限流回落 (P3.11 完善)
* **历史问题（P3.4 修复）**：`systemd-journald` 占用单核 33% CPU，日志狂刷 `qcom_battmgr` 报错，2 秒内产生约 7000 次中断（原厂 PMIC GLINK 发送私有 enum 9，已静默处理）。
* **P3.11 墙充限流与 VoocPHY 熔断修复 (`TASK-20261123-220715`)**：
  * **问题根因**：Oplus Linux 5.10 ADSP 上报墙插充电器 `adap_type=9` 时，驱动误落入 `SDP 500mA` 默认限流；且当 `DCP` 触发 `ADSP_VPHY_FAST_NOTIFY_COMMU_TIME_OUT` 时，`voocphy_status_worker` 与 `voocphy_recheck` 每 ~5.6 秒无限清零 `vooc_limits_set` 并重发 `USB_OPLUS_VOOCPHY_ENABLE = 1` 导致无法稳定充入。
  * **最终修复**：新增 `qcom_battmgr_map_usb_type()`（映射 `9 -> APPLE_BRICK_ID`、`PD_SDP(17) -> PD`）、提取公共限流配置函数 `qcom_battmgr_oplus_apply_adap_limits()`（为 `case 9 / APPLE_BRICK_ID` 设置 `5V/2.0A` ICL/FCC），并在 `vooc_commu_failures >= 2` 时熔断 `vooc_fallback_active = true` 降级为标准 `5V/2.0A`（真拔线 `usb_offline_count >= 2` 复位）。实机 60s 墙充验证识别 `type=9` (2.0A) 并协商 `PD_PPS` (9V/3A)，`bat.current_now=+666~+675mA`、零 `COMMU_TIME_OUT`、电量 60s 内实涨 `16% -> 17%`。

### 4.5 Merged-usr 软链接覆写死机防护 (Rootfs 损坏规避)
* **问题现象**：向手机解压模块包时，若执行 `tar -xf modules.tar.gz -C /`，会将 Ubuntu 24.04 的 `/lib -> usr/lib` 软链接替换为一个普通实体文件夹，导致系统 `libc.so.6` 找不到，下次重启直接 kernel panic 报找不到 `init`。
* **防护规则**：
  1. 宿主机与增量更新部署时，解压模块必须执行：
     ```bash
     tar -xzf p3_5_modules_fw.tar.gz -C /usr
     ```
  2. Initramfs 自愈脚本已植入 `initramfs/init`，在执行 `switch_root` 之前自动检测 `/newroot/lib` 是否为软链接；若为目录，则安全迁移其内部文件到 `/newroot/usr/lib/` 并重建软链接。

---

## 5. 内核源码结构与构建打包 SOP

### 5.1 目录索引
* 宿主机工作区根目录：`<WORKSPACE>/`
* 传统主线源码树：`src/xec-linux-7.3-rc4/` (Git 分支 `7.3-rc4`, 基线 commit `6d6be0ba1`)
* 平台工具集：`tools/bin/`, `tools/platform-tools/` (包含 `mkbootimg`, `unpack_bootimg`, `dtc`)
* P3.5 全量构建归档：`build_p3_5/imgs/`
* Orchestrator 规范化开发与基线目录：`development/`（P3.7 基线：`TASK-20261118-110042`；P3.8 归档：`development/milestones/P3_8/`；**当前 P3.9 节点产物归档**：[development/milestones/P3_9/](file://<WORKSPACE>/development/milestones/P3_9/)）

### 5.2 核心补丁文件列表
1. `arch/arm64/boot/dts/qcom/sm8475-oneplus-ovaltine.dts`：一加 Ace Pro 专用设备树主文件。
2. `arch/arm64/boot/dts/qcom/sm8475-ovaltine.dtso`：ABL 匹配用 DTBO 源码。
3. `drivers/gpu/drm/panel/panel-boe-rm692e5.c`：京东方 RM692E5 驱动（多刷新率 DCS + 1100Mbps 时钟）。
4. `drivers/input/touchscreen/synaptics_tcm_spi.c`：Synaptics S3910 触控 SPI 驱动。
5. `drivers/power/supply/qcom_battmgr.c`：电池管理与电量驱动（已修复 enum 9 洪水）。
6. `sound/soc/codecs/tfa9872.c`：NXP TFA9874 功放驱动。
7. `sound/soc/qcom/sc8280xp.c`：高通 AudioReach 机器驱动适配。

### 5.3 一键完整编译命令
在宿主机端执行：
```bash
source <WORKSPACE>/tools/env.sh

# 1. 编译内核与模块
cd <WORKSPACE>
make -C src/xec-linux-7.3-rc4 \
    O=build_p3_5/kernel/out \
    ARCH=arm64 \
    CROSS_COMPILE=aarch64-linux-gnu- \
    -j$(nproc) Image.gz modules dtbs

# 2. 编译独立 DTBO 叠加层
dtc -@ -I dts -O dtb \
    -o build_p3_5/dts/sm8475-ovaltine.dtbo \
    src/xec-linux-7.3-rc4/arch/arm64/boot/dts/qcom/sm8475-ovaltine.dtso

# 3. 打包 Boot 镜像 (注意保持 cmdline 规范)
mkbootimg \
    --kernel build_p3_5/kernel/out/arch/arm64/boot/Image.gz \
    --ramdisk build_p3_5/p3_5_initramfs.cpio.gz \
    --pagesize 4096 \
    --cmdline "clk_ignore_unused pd_ignore_unused regulator_ignore_unused pstore.compress=none pstore.kmsg_bytes=200000 console=tty0" \
    --header_version 4 \
    --output build_p3_5/imgs/p3_5_boot.img

# 4. 打包 Vendor Boot 镜像
mkbootimg \
    --dtb build_p3_5/kernel/out/arch/arm64/boot/dts/qcom/sm8475-oneplus-ovaltine.dtb \
    --pagesize 4096 \
    --header_version 4 \
    --output build_p3_5/imgs/p3_5_vendor_boot.img

# 5. 生成 DTBO 与 VBMeta
cp build_p3_5/dts/sm8475-ovaltine.dtbo build_p3_5/imgs/p3_5_dtbo.img
avbtool make_vbmeta_image \
    --flags 2 \
    --output build_p3_5/imgs/p3_5_vbmeta.img
```

---

## 6. 实机刷入、启动与增量更新 SOP

### 6.1 Fastboot 底层刷机流程 (严格限制 Slot `_a`)
将手机按住 `音量下 + 电源键` 进入 Fastboot 模式，连接 USB：
```bash
cd <WORKSPACE>/build_p3_5/imgs

# 确认设备就绪
fastboot devices

# 确保活动槽位为 A
fastboot --set-active=a

# 单独刷入内核、设备树、叠加层与禁用 AVB 验证
fastboot flash boot_a p3_5_boot.img
fastboot flash vendor_boot_a p3_5_vendor_boot.img
fastboot flash dtbo_a p3_5_dtbo.img
fastboot flash vbmeta_a p3_5_vbmeta.img

# 重启进入 Linux
fastboot reboot
```

### 6.2 在线增量更新模块 (无需进 Fastboot)
当仅调整驱动模块或配置文件时，通过 USB RNDIS（`172.16.42.1`）推送到实机：
```bash
# 1. 复制模块压缩包
scp build_p3_5/p3_5_modules_fw.tar.gz root@172.16.42.1:/tmp/

# 2. 登录实机执行解压与 depmod (注意解压至 /usr 规避 merged-usr 破坏)
ssh root@172.16.42.1 "tar -xzf /tmp/p3_5_modules_fw.tar.gz -C /usr && depmod -a && rm /tmp/p3_5_modules_fw.tar.gz"
```

---

## 7. 用户空间服务与系统配置清单

### 7.1 桌面合成器 (Phosh / Phoc)
* 配置文件路径：`/etc/phosh/phoc.ini`
* 当前显示屏配置：
  ```ini
  [output:DSI-1]
  mode = 1080x2412@120.000Hz
  scale = 2.0
  transform = normal
  ```

### 7.2 刷新率自动持久化服务 (`acepro-display-sync`)
* **程序路径**：`/usr/local/bin/acepro-display-sync.py`
* **服务路径**：`/etc/systemd/user/acepro-display-sync.service`
* **工作机制**：通过 DBus 监听 `org.gnome.Mutter.DisplayConfig` 的 `MonitorsChanged` 信号。用户在 GNOME 控制中心点击 60Hz/90Hz/120Hz 后，自动读取新分辨率刷新率并原子写入 `/etc/phosh/phoc.ini`。
* **部署注意**：由于以 `acepro` 用户运行，必须确保执行：
  ```bash
  chown -R acepro:acepro /etc/phosh && chmod 775 /etc/phosh
  ```

### 7.3 容器虚拟化与网络 (`lxc` / `lxcbr0`)
* **组件版本**：LXC 5.0.3 + lxc-templates + lxcfs
* **服务单元**：`lxc-net.service`
* **网桥配置**：`/etc/default/lxc-net`
  * 网桥名称：`lxcbr0`
  * 默认网段：`10.0.3.1/24` (DHCP 地址池 `10.0.3.2` ~ `10.0.3.254`)
  * 转发规则：基于 nftables IP Masquerade，向外网卡（`wlan0` / `rndis0`）提供 NAT 出口。

### 7.4 USB Gadget 网络服务
* **脚本路径**：`/usr/local/bin/acepro-usb-gadget.sh`
* **配置参数**：
  * IP：`172.16.42.1/24`
  * 驱动模块：`libcomposite` (RNDIS + ACM 串口)
  * 宿主机对应静态 IP：`172.16.42.2/24`

---

## 8. AI-Development-Orchestrator 开发治理规范

自 2026-11-17 起，内核与系统演进全面纳入 **AI-Development-Orchestrator** 人工门禁工作流管理，严禁直接在主工作区或未经审批跳过门禁修改/刷写内核。

### 8.1 目录架构
* **调度器源码目录**：`<WORKSPACE_ROOT>/AI-Development-Orchestrator/`
  * 核心程序：`app.py`（Tkinter 桌面审批控制台）、`coordinator.py`（命令行/门禁协调器）、`config/models.json`、角色配置 `profiles/{planner,executor,reviewer}.config.toml`、提示词 `prompts/`、模板 `templates/`、`install.sh`、`run.sh`。
* **Ace Pro 规范化开发目录**：`<WORKSPACE>/development/`
  * `admission/xec-linux-7.3-rc4`：纯净基准仓库（Clean admission repo，base commit `6d6be0ba139e89d843e62f56cbd9f829775901d5`），仅作只读基线，**禁止作为执行修改目录**。
  * `orchestrator/worktrees/xec-linux-7.3-rc4-<TASK-ID>`：每个任务独占的独立 Git worktree（分支 `ai/<task-id>`），实现任务间物理与源码隔离。
  * `orchestrator/projects/3bc162499e3b0a0d/<TASK-ID>/`：各任务审批账本（`state.json`）、`TASK.md`、`PLAN.md`、`EXECUTION.md`、`TEST-RESULTS.md`、`REVIEW.md` 及哈希收据归档。
  * `device-tests/`：实机测试取证归档（如 `P3_6-20261118-EXCEPTION-01`、`THERMAL-20261118-01`、`THERMAL-20261118-UFS-01`）。

### 8.2 Planner / Executor / Reviewer 人工分阶段门禁
所有变更必须经过严格哈希绑定与人工签批，协调器不自动 `commit`、`push` 或 `deploy`：
1. **Plan 阶段（`plan` → `approve-plan`）**：Planner 在只读沙箱生成 `PLAN.md`；人工审核后执行 `approve-plan`，记录 `PLAN.md` 的精确 SHA-256（`plan_hash`）。
2. **Execute 阶段（`execute`）**：Executor 仅在任务专属 worktree 中执行，若 `PLAN.md` 哈希与批准时不一致则拒绝启动。
3. **Review 阶段（`approve-review` → `review`）**：`approve-review` 记录跟踪差异与未跟踪文件内容的源码快照哈希（`source_hash_before_review`）；Reviewer 在只读沙箱执行独立审查并生成 `REVIEW.md`。
4. **Final Gate（`approve-final`）**：仅当 `review_verdict == "PASS"` 且源码快照哈希未发生任何变化时才允许最终批准（`final_approved = true`）。

### 8.3 P3.7 权威基线定位
* **权威基线任务 ID**：`TASK-20261118-110042`
* **Worktree 路径**：`<WORKSPACE>/development/orchestrator/worktrees/xec-linux-7.3-rc4-TASK-20261118-110042`
* **冻结源码快照 SHA-256**：`f2f78d85d0d3303fae7091e212a697882ed16269d1ada4e672288ef2b33f892f`
* **Payload Manifest SHA-256**：`cb15e123128cc6881ae6c2d0c66e0a1560ab6785009e78d4c3940c21a1f269cf`（5,897 项绑定文件一致）
* **任务状态**：`DONE` / `review_verdict: PASS` / `final_approved: true`

---

## 9. P3.6 / P3.7 实测与深度功耗排查（R1→R5 优先级）

### 9.1 P3.6 例外试刷实测 (`P3_6-20261118-EXCEPTION-01`)
* **背景与授权**：原任务 `TASK-20261118-000550` 处于 `REVIEW_NEEDS_CHANGES` / `REQUEST_CHANGES`（`final_approved=false`），经用户明确授权一次性例外试刷后，仅刷写 `boot_a` 并安装独立模块运行 `7.3.0-rc4-ovaltine-p3-6`。
* **LXC 容器与网络实测通过**：
  * 双容器并行（`acepro-p36-r2a`、`acepro-p36-r2b`）完成两轮启停生命周期与 `lxc-net` 重启恢复验证：
  * DHCP 正常分配 IPv4（`10.0.3.15/24`、`10.0.3.16/24`；首轮单容器 smoke 为 `10.0.3.152/24`），PID 与网络命名空间独立，`Seccomp=2`；
  * 容器经 `10.0.3.1` 正常解析 `example.com` IPv4 DNS，同桥互 ping 成功，并经 Wi-Fi（`<GATEWAY>60`）完成 HTTP 外网访问与 SNAT 计数实证（后续 P3.6/P3.7 A/B 对照中经 USB `172.16.42.1` 完成每版 12 次独立 HTTP SNAT 验证）。
* **6080 面板测点性质确认**（`THERMAL-20261118-01`）：
  * 设备内置 6080 面板（`lxc-webui.service`，`/opt/lxc-webui/lxc-webui`，接口 `GET /api/dashboard`）显示的 43–45°C（三轮采样八核范围 `43.4–45.1°C`）**确认为 CPU/SoC 温度**（`stats.cpu_cores[].temp_c`），而非电池（同期 `39.7°C`）或外壳表面温度。

### 9.2 P3.7 实机部署、UFS 修复与温度 A/B 实测
* **实机部署与回退保护**（`P3_7-DEPLOY-UFS-20261118-01`）：
  * 仅刷写 `boot_a`（前 19,566,592 字节 SHA-256 `dfe20c755a4bec1dda6d27d8ed43a3c98f3edbe228a7704038f5bc0c4ff07931`），实机运行 `7.3.0-rc4-ovaltine-p3-7-ufs1`。
  * 保留 192 MiB P3.6 `boot_a` 完整回退备份：`TASK-20261118-110042/tools/acepro/p3_7/out/device-tests/P3_7-PREFLIGHT-20261118-01/current-p3_6-boot_a.img`（`201,326,592` 字节，SHA-256 `6b481798c65a004a13662cf080a1841a6784a88b93eaec71edf7e7347810e14a`）。
* **UFS 20 轮读写验证**（`P3_7-DEPLOY-UFS-20261118-01` & `P3_7-THERMAL-LXC-20261118-01`）：
  * P3.6 在 20 轮 4 KiB direct 只读后，7 路 UFS 时钟引用从 `18/18` 持续累加至 `38/38`。
  * P3.7 部署后 20 轮读写不再出现每轮 `+1` 持续累加：首轮部署 20 轮前后均为 `0/0`；在 A/B 对比测试中，39/40 个快照全为 `0/0`，仅第 17 轮 `after_clocks` 出现瞬时非零（`1/1` 或 `2/2`、`hardware Y`），第 18–20 轮及后续 5 次空闲复核均归零（`0/0`、`hardware N`），因此结论为**带第 17 轮瞬时非零限定的非累加通过**。
* **温度 A/B 对照实测**（`P3_7-THERMAL-LXC-20261118-01`）：
  * 在一致物理条件（DSI Off/disabled、CRTC 全停用、WLAN rfkill blocked，稳定等待 900 秒后采样 61 点/约 600 秒）下，主测点 `cpuss0-thermal`：
    * **A (P3.6)** 中位数：**44.80°C**（范围 `44.00–45.10°C`）
    * **B (P3.7)** 中位数：**44.80°C**（范围 `44.40–45.10°C`）
    * **B − A = +0.00°C**
  * **结论**：单修 UFS 时钟引用累加未带来显著 SoC 降温，必须继续收敛其余硬件保活链。

### 9.3 深度功耗排查与旧结论更正 (`P3_7-THERMAL-DEEPDIVE-20261118-01/DIAGNOSIS.md`)
* **明确更正旧结论（撤回“MDP 持续泄漏”推断）**：
  * 读取完整 `clk_summary` 会临时取得 clock provider 的 runtime PM 引用，此前因此观测到 `disp_cc_mdss_mdp_clk` 为 `172 MHz / 1/1`。
  * 在不读取 `clk_summary` 的 4 个被动采样窗口（120 秒两端与 30 秒确认）中，`MDP`、`MDP src` 与 `PLL0` 的引用计数均稳定为 **`0/0`**，故正式撤回“MDP 持续泄漏”推断。
* **完整修复顺序（R1→R5 优先级，按阻塞证据与实现边界排序）**：
  1. **R1（显示 DSI PHY AHB 长驻 `prepare=1`）**：DSI PHY 驱动（`drivers/gpu/drm/msm/dsi/phy/dsi_phy.c`、`dsi_phy.h`）通过 `pm_clk_add(dev, "iface")` 管理 `DISP_CC_MDSS_AHB_CLK`，`pm_clk_suspend()` 仅执行 `clk_disable()` 而不 `unprepare`，导致 AHB 及其 src 长期维持 `enable=0 / prepare=1`，进而持有 `DISPCC`（`af00000.clock-controller`）的 runtime PM 引用，使 `DISPCC` 持续 `active`、`MMCX` 与 `CX` 维持 `on / perf_state=64`。
  2. **R2（GPU/GMU 双重禁止挂起）**：`/etc/udev/rules.d/80-gpu-pm.rules` 与 `/etc/tmpfiles.d/gpu-pm.conf` 强制将 `3d00000.gpu` 与 `3d6a000.gmu` 设为 `power/control=on`，导致 `gpu_cc_cx_gmu_clk` 保持 `2/2`（`200 MHz`）、GPU CX/GX 域常亮；须先定位并修复 GMU OOB / `0x807` 风险后再撤销两份保活规则。
  3. **R3（USB2 模式下 USB3 PHY 保活）**：运行设备树与 UDC 均限制 `maximum-speed="high-speed"`，但 `phys` 仍保留 `usb3-phy`，使 `88e8000.phy` 默认 `pm_runtime_forbid()`、`PIPE` 时钟维持 `125 MHz / 1/1`，且 USB→DDR 保持 `240000 / 700000 kB/s`（`ALWAYS`）带宽投票。需在核对 Type-C/DP/DISPCC 时钟供给关系后验证仅在 DWC3 消费端裁剪。
  4. **R4（PCIe / `ath11k` / MHI 三层保活）**：即使 `nmcli radio wifi=disabled`，PCIe root port（`0000:00:00.0`）与 endpoint（`0000:01:00.0`）仍处于 `D0 / 8 GT/s x1`、`ASPM Disabled`；原因包括现存 `acepro-pcie-aspm.service` / `90-acepro-pcie-aspm.rules`、`qcom_pcie_probe()` 持有 `pm_runtime_get_sync()` 未释放、以及 `ath11k`/MHI 缺少完整的 runtime PM 静默与恢复回调，同时保持 `984500 kB/s`、`tag=7`（含 `SLEEP`）DDR 带宽请求。
  5. **R5（DDR 下游验收）**：当前 `CXSD` / `DDR` / `AOSD` 低功耗计数起止均为 `0/0`；须在 R1–R4 客户端资源生命周期与睡眠带宽投票收敛后作为下游验收，严禁直接强清 ICC 或写 AOSS 强制降频。

---

## 10. P3.8 / P3.9 研发进度与里程碑交付

P3.8 围绕 **R3（USB HS-only）** 与 **R1（显示 DSI PHY AHB `prepare` 生命周期）** 完成交付并归档于 [development/milestones/P3_8/](file://<WORKSPACE>/development/milestones/P3_8/)；随后 **P3.9 (`TASK-20261120-010302`)** 完成 **R2（GPU/GMU Runtime PM HFI 中断与内存序根治）**、撤销设备端保活规则并归档于 [development/milestones/P3_9/](file://<WORKSPACE>/development/milestones/P3_9/)。

### 10.0-B P3.12 Wi-Fi 静态 IP 固化与 `qcom_battmgr` PD/PPS 优化 (`TASK-20261123-231500`, `PASS`)
* **Wi-Fi 静态 IP 固化 (`<STATIC_IP>/24`)**：通过 `nmcli` 将 `<WIFI_SSID>`（`<NM_UUID>`）设为 `manual` `<STATIC_IP>/24`（网关/DNS `<GATEWAY>`，不改随机 MAC；原配置备份于 `/root/90-NM-<NM_UUID>.yaml.bak`），跨重启验证 MAC 变化后 IP 仍稳定锁定 `<STATIC_IP>/24`。
* **对照原厂 `oplus_battery_sm8450.c` / `oplus-chg-21841.dtsi` 实现与发现**：
  * 新增 `PPS_OPCODE_READ_BUFFER (0x10004)` 成功解码适配器全部 PDO（`5V/3A`, `9V/3A`, `12V/3A`, `15V/3A`, `20V/7A`, `PPS 3.3-21V 6.35A`、`subtype=0, pps_type=1, fcc_lim_max=4500000`）；
  * 处理 `0x58/0x5A/0x78/0x79` ADSP 通知消除 `unknown notification: 0x78`，新增 `adap_cfg_lock` 串行化与阶梯 AICL 消除 VBUS 塌陷；
  * 实测 `BATT_OPLUS_SET_PDO(9000)` 返回 `0` 但当前 ADSP 固件在 `PD_PPS` 且无外部电荷泵驱动下维持 `5V/2.0A` 稳态供电（`4.68V / 1.997A`、`+630mA` 净充入，与 `oplus-chg-21841.dtsi` `qcom,pd_input_current_charger_ma = 2000` 一致），归档至 [development/milestones/P3_12/](file://<WORKSPACE>/development/milestones/P3_12/)。

### 10.0-A P3.11 墙充限流与 VoocPHY 熔断修复 (`TASK-20261123-220715`, `PASS`)
* **修复内容 (`drivers/power/supply/qcom_battmgr.c`)**：映射 `adap_type=9 -> APPLE_BRICK_ID`、`PD_SDP(17) -> PD`，提取公共 `qcom_battmgr_oplus_apply_adap_limits()`，并增加 `vooc_commu_failures >= 2` 熔断回落标准 `5V/2.0A` 状态机，归档至 [development/milestones/P3_11/](file://<WORKSPACE>/development/milestones/P3_11/)。
* **实机 60s 墙充验收 (`boot_id`: `1f42788e-acce-4ddf-8d20-27f8af57d469`)**：识别 `wall brick adapter (type=9), setting 2.0A ICL/FCC` 并顺利协商 `PD_PPS` (`9V/3A`)，`usb.input_current_limit=2000000`，`bat.current_now=+666~+675mA`，零 `COMMU_TIME_OUT`，60 秒内电量 `16% -> 17%`。

### 10.0 P3.10 S2R (`deep` / `s2idle`) 与触控唤醒修复 (`TASK-20261123-000413`, `PASS`)
* **根因与修复 (`drivers/input/touchscreen/synaptics_tcm.c`)**：`synaptics_tcm_resume()` 上电复位后未先执行 `identify_chip` / `get_fw_config`，且 `preserve_touch_report_config()` 未校验 `TCM_STATUS_OK (0x01)` 与排空异步 `REPORT_IDENTIFY`（28 字节），导致唤醒后覆盖开机时的 128 字节 `touch_config` 使触控失灵。修复后新增公共命令应答排空函数 `synaptics_tcm_read_cmd_resp()`、`identify_chip` 长度防护、`suspend()` 释放 `active_slots` 及 `resume()` 完整重初始化序列，构建并归档至 [development/milestones/P3_10/](file://<WORKSPACE>/development/milestones/P3_10/)。
* **实机 4 轮验收 (`boot_id`: `0d115116-2a2e-4642-be35-2f3d8299ba8b`)**：连续完成 3 轮 `deep` (`WIFI=enabled` / `disabled` / `enabled`) + 1 轮 `s2idle` (`WIFI=enabled`) `systemctl suspend` 休眠唤醒（`suspend_stats`: `success=4, fail=0`），单次电源键亮/熄屏、触控滑动解锁（`128 bytes`）、`wlp1s0` 重连、`es2gears_wayland` + GPU/GMU `auto` 挂起与声卡均正常。

### 10.0-C P3.13 未插电黑屏自动 `deep` 休眠与 `qcom_battmgr` 满充容量修复 (`TASK-20261124-000500`, `PASS`)
* **根因定位**：
  1. `/proc/device-tree/hypervisor/compatible`（`qcom,gunyah-hypervisor-1.0`）导致 `systemd-detect-virt` / `systemd1.Manager.Virtualization` 返回 `"vm-other"`，`gsd-power`（`gnome-settings-daemon 46.0`）命中 `"Ignoring sleep timeout with suspend action inside VM"` 从而彻底屏蔽 `sleep-inactive-battery-type='suspend'`，使黑屏整机持续清醒耗电 ~2.6W（4 小时掉电 83%→0%）。
  2. `qcom_battmgr.c` 在 SM8350/SM8550 probe 分支未初始化 `battmgr->unit`（保持 `0=mWh`），导致 `CHARGE_FULL`/`CHARGE_FULL_DESIGN` 恒返回 `-ENODATA`，且 `OEM_BUF_FCC`/`OEM_BUF_CHARGE_COUNTER` 以 `mAh` 上报，致使 `upowerd` 看到 `0 Wh` 并持续输出 `value "nan" ... property 'percentage'`（6 小时消耗 3m10s CPU）。
* **修复方案（[development/milestones/P3_13/](file://<WORKSPACE>/development/milestones/P3_13/)）**：
  1. **内核 (`p3_13_qcom_battmgr_unit_mah.patch`)**：SM8350/SM8550 probe 初始化 `battmgr->unit = QCOM_BATTMGR_UNIT_mAh`，并对 `mAh` 值（`>0 && <20000`）统一换算为 `uAh`（`* 1000`）。
  2. **用户态休眠守护 (`/usr/local/sbin/acepro-idle-suspend.py` + `acepro-idle-suspend.service`)**：保留 DT `/hypervisor` 与 `acepro-pcie-aspm.service`（保护 WCN6855），开启 `sm.puri.phosh enable-suspend=true` 与 `sleep-inactive-battery-timeout=180`；仅在 `UPower.OnBattery=true` 且 `usb/wls online=0`、屏幕熄灭空闲 `>=180s`、无 `block sleep` inhibitor 且过 `45s` 唤醒/亮屏冷却期后调用 `login1.Manager.Suspend(false)` 进入 `deep` 休眠。
  3. **辅助日志降噪 (`/usr/local/bin/acepro-display-sync.py`)**：当目标模式未变化时跳过写 `/etc/phosh/phoc.ini.tmp`，消除 `Permission denied` 报错。
* **实机验收 (`boot_id`: `e3f798d8-e597-41ea-8ce2-4d5d9fd1bccf`)**：`qcom_battmgr.ko` (`8fc2d72a...95e9`) 导出 `charge_full=1687000`、`charge_full_design=4000000`、`charge_counter=1044000`；`upower -d` 正常显示 `energy-full=7.08~7.15 Wh`、`energy-full-design=16.80~16.95 Wh`，`upowerd` `nan` 报错 `0` 条、`acepro-display-sync.py` 权限报错 `0` 条；`systemctl suspend` 实测进入 `PM: suspend entry (deep)` 并通过短按电源键正常唤醒（`suspend_stats/success=1, fail=0`），`PrepareForSleep(false)` 45s 冷却与 `systemd-inhibit --mode=block` 识别自检均通过。

### 10.0-D P3.14 `qcom_battmgr` 异步 OEM 缓存消除 450ms sysfs 阻塞 (`TASK-20261124-004500`, `PASS`)
* **根因定位**：`qcom_battmgr_bat_sm8350_update()` 每次读取任意电池 sysfs 属性时均同步执行 `BATTMGR_BAT_PROPERTY_GET` (~28ms) 与 `qcom_battmgr_oem_read_buffer()`（`0x10000` 256B ADSP 电量计缓冲读取，耗时 ~350–450ms），导致 `status`/`voltage_now`/`current_now`/`capacity`/`charge_full` 单次读取耗时高达 **`356.45–672.11 ms`**，超过 `/opt/lxc-webui/lxc-webui` `readDevicePower` 的 **300 ms** 硬超时阈值并触发路径黑名单（`power_w=0`）。
* **修复方案（[development/milestones/P3_14/](file://<WORKSPACE>/development/milestones/P3_14/)，`p3_14_qcom_battmgr_async_oem_cache.patch`）**：
  1. 引入 `oem_read_work`（`devm_work_autocancel` 托管）与 `oem_last_update`：在 `qcom_battmgr_enable_worker()` 初始化时先拉取一次填满 `oem_read_buf`，后续在 `bat_sm8350_update()` 中当缓存超过 `2000 ms` 时异步 `schedule_work(&battmgr->oem_read_work)` 后台刷新，移除 sysfs 同步读路径上的 450ms 等待。
  2. 新增 `qcom_battmgr_prop_from_oem()`：当 `oem_read_buf_valid` 为真时，对由 `oem_read_buf` 提供的属性（`VOLTAGE_NOW`、`CAPACITY`、`TEMP`、`CYCLE_COUNT`、`CHARGE_FULL`、`CHARGE_COUNTER`、`STATE_OF_HEALTH` 及 `!usb.online` 下的 `CURRENT_NOW`）直接走内存缓存，跳过冗余 `BATTMGR_BAT_PROPERTY_GET`。
* **实机验收 (`boot_id`: `79be0688-0b00-4dd2-b2b7-6327a8dda132`)**：部署 `qcom_battmgr.ko` (`126d536b...11d4`) 后，`qcom-battmgr-bat` 实测单次读取耗时降至 `type=0.10ms`、`status=2.47ms`、`voltage_now=0.06ms`、`current_now=33.02ms`、`capacity=0.04ms`、`charge_full=0.03ms`、`charge_full_design=0.39ms`、`charge_counter=0.05ms`，`qcom-battmgr-usb` `online=32.41ms`、`voltage_now=3.78ms`、`current_now=5.95ms`（全部 **`< 50 ms`**，远低于 `lxc-webui` 300ms 阈值，实测输入功率 `9.362 W`、电池充电功率 `2.062 W`）；`thermal_zone0=44.0°C`、`thermal_zone1=46.7°C`，`vmstat` 稳态 `99% idle`，`UPOWER_NAN_COUNT=0`。

### 10.0-E P3.15 `s2idle` Connected Standby（黑屏保活 Wi-Fi 与来包自动唤醒，`TASK-20261125-001500`, `PASS`)
* **根因定位**：
  1. GICv3-ITS (`irq-gic-v3-its.c`、`irq-gic-its-msi-parent.c`) 缺少 `IRQCHIP_SKIP_SET_WAKE`，导致 PCIe Root Port `PCIe PME (IRQ 184)` 与 `ath11k_wake` 的 `enable_irq_wake()` 失败。
  2. SM8450/SM8475 PCIe 使用 OPP 表（`pcie->use_pm_opp=true`），`qcom_pcie_suspend_noirq()` 在非 S2RAM `s2idle` 下即便 `!pcie->pci->suspended`（链路保持激活）也无条件调用 `dev_pm_opp_set_opp(dev, NULL)`，撤除了 `pcie_noc -> mc_virt` 互联与 `rpmhpd` 最低电压档位，阻塞 EP 上行唤醒报文/MSI。
  3. `ath11k` 在 `D0-WoW` 下通过 HTC wakeup 上报无法穿透，而在 `D3hot + MHI M3` (`d0_wow=false, keep_d0=false`, `pause_iface=0, flags=0x20`) 配合 Root Port `00:00.0` `power/wakeup=enabled` 时，WCN6855 匹配 WoWLAN 规则后触发 `PCIe PME (IRQ 184)` 稳定唤醒全核。
  4. 遇到固件瞬时忙应答 `ATH11K_HTC_MSG_NACK_SUSPEND` 时，原 `ath11k_wow_op_suspend()` 仅重试 3 次并返回正值 `1`，触发 `cfg80211_leave_all()` 主动 `DEAUTH_LEAVING` 断网并下电 MHI 导致恢复超时 `-110`。
* **修复方案（[development/milestones/P3_15/](file://<WORKSPACE>/development/milestones/P3_15/)，`0010-p3_15-ath11k-wowlan-pcie-pme-s2idle.patch`）**：
  1. **`irqchip`**：在 GICv3-ITS / ITS MSI parent 添加 `IRQCHIP_SKIP_SET_WAKE`，允许 `enable_irq_wake(184)` 成功武装。
  2. **`pcie-qcom.c` (`Image.gz`)**：新增 `qcom_pcie_set_min_opp()` 与默认 `keep_min_opp_s2idle=true`，在 `!pcie->pci->suspended` 的非 S2RAM `s2idle` 中维持最低 PCIe OPP。
  3. **`ath11k` (`ath11k.ko`, `ath11k_pci.ko`) & `qrtr-mhi.ko`**：默认 `d0_wow=false, keep_d0=false`、`probe`/`pm_prepare` 无条件使能 `00:00.0` 与 `01:00.0` `wakeup`、`ath11k_wmi_wow_enable()` 在 `!d0_wow` 下默认 `pause_iface=0, flags=0x20`；将 `ATH11K_WOW_RETRY_NUM` 由 `3` 增至 `10`，并在 `ath11k_wow_op_suspend()` 完善逆序回滚且返回 `-EBUSY`（不再返回 `1` 引发断网）。
  4. **用户态 (`/usr/lib/systemd/system-sleep/acepro-wowlan`, `/usr/local/sbin/acepro-idle-suspend.py`)**：动态解析 `wlp1s0` 对应的 `phyX`（不再硬编码 `phy0`），避免重载模块后 WoWLAN patterns 失效。
* **实机验收 (`boot_id`: `5ee0f715-22f6-4e66-9214-cc75628c4fd7`, `p3_15_boot.img` `23efa9cf...`)**：
  - 冷启动零手动覆盖下完成 3 轮连续 `s2idle` 自动唤醒（C1 ~29s ping `202ms`/`0%` 丢包；C2 ~29s 免先 ping 直接 `ssh` `1225ms` 成功；C3 ~33s 连续再休眠 ping `172ms`/`0%` 丢包），`pmic_pwrkey +0`、`184 (PCIe PME)` `+1/+2/+2`、`cpu0..7 state1/s2idle/usage` `0->3`、`suspend_stats success=3, fail=0`、零 `DEAUTH_LEAVING` 与 MHI 超时。
  - Stage 1 (`PowerSaveMode 3<->0`)、`lxc-webui :6080` (`HTTP 200`) 与 `qcom_battmgr` 缓存耗时 (`0.07–60.21 ms`) 均保持正常；回退方式为仅刷写 `P3_8/p3_8_boot.img` 到 `boot_a` 或切换 Slot `_b`。

### 10.1 P3.9-R2 已完成实机验收项 (`TASK-20261120-010302`, `PASS`)
* **根因修复**：在 `a6xx_gmu.c` 与 `a6xx_hfi.c` 修复 `a6xx_hfi_irq()`/`a6xx_gmu_irq()` 掩码过滤与 `IRQ_NONE` 返回、`a6xx_gmu_irq_disable()`/`a6xx_gmu_resume()` posted-write readback 刷新、`a6xx_hfi_queue_read()` `dma_rmb()` 与 `a6xx_hfi_wait_for_msg_interrupt()` `rspq` 非空轮询。
* **20 轮 + 90s 长稳回归**：临时 `auto` 下 20/20 轮 `es2gears_wayland` + 偶轮 `vulkaninfo --summary` + 熄屏 `gpu`/`gmu=suspended` & `gpu_cx_gdsc`/`gpu_gx_gdsc=off-0` 全过，90s 静置与唤醒重渲染正常，`dmesg` 零 HFI/OOB/fault/hang 错误（仅 2 条非致命 `0x807`），`hfi=0, gmu=0`。
* **撤销保活规则与自然 `auto` 收口**：移除 `/etc/udev/rules.d/80-gpu-pm.rules` 与 `/etc/tmpfiles.d/gpu-pm.conf`（备份于 `/root/p3_8_gpu_pm_backup/`），重启后（`boot_id`: `81e72500-1c7a-4036-993f-c6c3fa6e67d5`）自然默认 `power/control=auto`，10/10 轮 + 60s 静置 + 唤醒全过，R1 `dispcc=suspended` 与 R3 `gcc_usb3_prim_phy_pipe_clk=0/0` 保持不回退。

### 10.1 P3.8 已完成实机验收项
1. **R3（`TASK-20261119-233358`：USB HS-only DTS 裁剪，`PASS`）**：
   * 在 `arch/arm64/boot/dts/qcom/sm8475-oneplus-ovaltine.dts` 的 `&usb_1` 添加 `qcom,select-utmi-as-pipe-clk;` 且仅保留 `phys = <&usb_1_hsphy>;` / `phy-names = "usb2-phy";`（完整保留 `&usb_1_qmpphy` `status = "okay"` 与 `usb-role-switch;`）。
   * 仅刷写 `vendor_boot_a` 实机验收通过：`gcc_usb3_prim_phy_pipe_clk` 的 `clk_enable_count / clk_prepare_count` 由 **`1/1` 降至 `0/0`**，USB2 Gadget SSH 与 Type-C/充电均正常。
2. **R1（`TASK-20261119-235522`：DSI PHY `iface` AHB 生命周期修复，`PASS`）**：
   * 在 `drivers/gpu/drm/msm/dsi/phy/dsi_phy.h` 与 `dsi_phy.c` 中彻底移除 `pm_clk` 与 `dsi_phy_pm_ops`（`.pm = NULL`，不在 runtime PM 回调内取 `prepare_lock`），改为 `probe`（围绕 `ops.pll_init` 与 `devm_of_clk_add_hw_provider`）及 `msm_dsi_phy_enable()` / `msm_dsi_phy_disable()` 内成对 `clk_prepare_enable` / `clk_disable_unprepare`。
   * 仅替换 rootfs `msm.ko` 后，10 轮完整亮屏 ↔ 熄屏切换及熄屏稳态 `clk_summary` 读压测全部 `PASS`：`disp_cc_mdss_ahb_clk` 与 `ahb_src` 均由 **`0/1` 降至 `0/0`**，`af00000.clock-controller`（`dispcc`）与 `mmcx` / `mmcx_ao` / `cx` / `lcx` / `mdss_gdsc` 均稳定收敛至 **`suspended` / `off-0`**。
3. **方案 B（180s 熄屏稳态观测）与方案 A（R2 GPU/GMU 内存临时探针，`PASS`）**：
   * **方案 B**：R1+R3 下 180s 熄屏稳态窗口 `dispcc` 100% 挂起（`+180,007 ms`），`mmcx`/`cx`/`mdss_gdsc` 保持 `off-0`；中位温度 `cpuss0=42.0°C`（`41.2–43.6°C`）、`gpu-top=40.0°C`（`39.6–41.2°C`）、`battery=36.8°C`（`36.4–37.4°C`），`dmesg` 零新增（存在跨日、boot age 与充电阶段混杂限制，不单独归因为净温降）。
   * **方案 A**：内存临时将 `3d00000.gpu` 与 `3d6a000.gmu` 设为 `auto` 后完成 2 轮熄屏 10s ↔ 亮屏渲染 5s 短测，`gpu_cx_gdsc` 与 `gpu_gx_gdsc` 双双降至 `off-0`，唤醒渲染正常且 `dmesg` 零新增；但 `gmu` 挂起下 `gpu_cc_cx_gmu_clk` / `hub_cx_int` 仍 `0/1`，且 2 轮短测不足以排除长稳/高负载下的历史 `GMU OOB` 风险，**R2 长稳验证与 R4（`pcie-qcom` / `ath11k_pci` / `mhi` 缺口）仍为后续待解项**。

### 10.2 历史 R1 锁序卡点任务归档（供设计溯源）
1. **`TASK-20261118-200637`（`Ace Pro P3.8：显示 AHB prepare 生命周期最小修复`）**
   * **状态**：`EXECUTED`（`plan_approved: true`，`execution_approved: false`），`EXECUTION.md` 结论为 **`BLOCKED_BEFORE_IMPLEMENTATION`**。
   * **卡点**：静态锁序分析发现跨线程等待环（PHY runtime PM 转换申请 `prepare_lock` vs 持锁等待 PHY PM 转换），两个 PHY 文件保持零改动（该问题随后在 `TASK-20261119-235522` 通过移除 `dsi_phy_pm_ops` 并改由 `probe`/`enable`/`disable` 成对管理得以规避）。
2. **`TASK-20261118-211452`（`Ace Pro P3.8：显示 AHB 修复的 PM/CCF 锁序补充设计`）**
   * **状态**：`DONE` / `review_verdict: PASS` / `final_approved: true`，接受离线停止交付 **`LOCAL_CONTRACT_INCOMPLETE`**（产品设计 `BLOCKED_DESIGN`），遗留设计缺口 **M-01**（DRM 停机路径 `drm_atomic_helper_shutdown()`）与 **M-02**（双输出交错改频与部分失败回滚账本）。
3. **`TASK-20261118-230035`（`Ace Pro P3.8：DRM 停机链与双输出租约合同修订设计`）**
   * **状态**：停滞在 **`DRAFT`**；首次规划被模型服务以 **`cyber_policy`** 中断（`PLANNING_INTERRUPTED_BY_SERVICE`），已归档至 `service-review-01/`、`SERVICE-REVIEW-01.zip`（SHA-256 `dea3947e0474e0db576aa857840bc259173a61c5721f01cb904dea9beafde4f8`）与 `MEDIUM-FINDINGS-INDEX-01.md`。

---

## 11. 后续演化路线图与未完成任务

> [!IMPORTANT]
> **范围收敛声明（v2.3）**：随着 R1（显示 AHB）、R2（GPU/GMU `auto`）、R3（USB HS）以及 **P3.10 S2R (`deep` / `s2idle` 休眠唤醒与触控修复)** 全部实机闭环，当前阶段核心移植目标已达成。根据用户确认，**5G 基带调制解调器、SuperVOOC 快充协议、摄像头子系统以及 R4/R5（PCIe/`ath11k`/MHI 深度功耗与 DDR 下游）均暂不作为当前/近期目标**。

### 11.1 远期暂缓项（非当前目标）
* **摄像头子系统 (Camera Bringup)**：Sony IMX766 / OV08D / OV02B + Spectra 680 ISP（暂不作为近期目标）。
* **5G 基带与移动蜂窝网络 (Cellular / Modem)**：`qcom_q6v5_pas` + IPA + ModemManager（暂不作为近期目标）。
* **快速充电协议补全 (SuperVOOC / UFCS)**：当前维持标准 5V/9V USB-PD 充电，150W SuperVOOC 暂不作为近期目标。
* **屏下指纹与安全增强 (Biometrics)**：汇顶光学屏下指纹 + TrustZone QSEE/TEE。

---

> [!NOTE]
> 本手册与代码仓库变更实时对齐。后续接手的开发者请严格遵循第二章节的**双槽位安全隔离守则**，在开始任何未知底层模块调试前务必验证 Slot `_b` 的可引导性。
