> [!CAUTION]
> **仅限测试与研究使用，不作正式环境使用（FOR TESTING & RESEARCH ONLY — NOT FOR PRODUCTION USE）**。  
> 本文档及仓库内的路径、序列号、MAC 地址与内网 IP 均已做脱敏处理。

# 一加 Ace Pro (PGP110 / ovaltine) 主线 Linux 移植完整工程指导手册

> **文档版本**：v2.3 (P3.10 里程碑归档：P3.9 基线 + S2R `deep`/`s2idle` `synaptics_tcm` 唤醒触控修复)  
> **更新时间**：2026-11-23  
> **目标系统**：Ubuntu Desktop / Phosh Mobile Linux (Linux 7.3.0-rc4 mainline)  
> **目标机型**：一加 Ace Pro (OnePlus Ace Pro / PGP110 / 内部代号 `ovaltine` / 项目代号 `21841`)  
> **当前实机运行版本**：**P3.10 基线**（`7.3.0-rc4-ovaltine-p3-7-ufs1` 内核 + R3 `vendor_boot_a` DTB + R1+R2 `msm.ko` + P3.10 `synaptics_tcm.ko`，默认 `mem_sleep=deep`、GPU/GMU `power/control=auto`，实机验证 `boot_id`: `0d115116-2a2e-4642-be35-2f3d8299ba8b`；归档路径：[development/milestones/P3_10/](development/milestones/P3_10/)）

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
* **备份存储路径**：`<BACKUP_DIR>/`
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

### 4.4 电池管理器中断风暴 (CPU 占满与日志刷屏)
* **问题现象**：`systemd-journald` 占用单核 33% CPU，日志狂刷 `qcom_battmgr` 报错，2 秒内产生约 7000 次中断。
* **根因分析**：
  * 原厂 PMIC GLINK 发送的电池事件响应中包含 enum 9（私有快充状态枚举），主线驱动 `drivers/power/supply/qcom_battmgr.c` 缺少对该枚举的映射，落入 default 分支反复抛出错误信息。
* **最终修复方案**：
  * 在 `qcom_battmgr.c` 中补齐 enum 9 的静默处理，中断频率骤降至 3 次/2秒，CPU 占用恢复归零。

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
* Orchestrator 规范化开发与基线目录：`development/`（P3.7 基线：`TASK-20261118-110042`；P3.8 归档：`development/milestones/P3_8/`；**当前 P3.9 节点产物归档**：[development/milestones/P3_9/](development/milestones/P3_9/)）

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
* **调度器源码目录**：`<HOME>/文档/macos/AI-Development-Orchestrator/`
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
  * 容器经 `10.0.3.1` 正常解析 `example.com` IPv4 DNS，同桥互 ping 成功，并经 Wi-Fi（`192.168.x.160`）完成 HTTP 外网访问与 SNAT 计数实证（后续 P3.6/P3.7 A/B 对照中经 USB `172.16.42.1` 完成每版 12 次独立 HTTP SNAT 验证）。
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

P3.8 围绕 **R3（USB HS-only）** 与 **R1（显示 DSI PHY AHB `prepare` 生命周期）** 完成交付并归档于 [development/milestones/P3_8/](development/milestones/P3_8/)；随后 **P3.9 (`TASK-20261120-010302`)** 完成 **R2（GPU/GMU Runtime PM HFI 中断与内存序根治）**、撤销设备端保活规则并归档于 [development/milestones/P3_9/](development/milestones/P3_9/)。

### 10.0 P3.10 S2R (`deep` / `s2idle`) 与触控唤醒修复 (`TASK-20261123-000413`, `PASS`)
* **根因与修复 (`drivers/input/touchscreen/synaptics_tcm.c`)**：`synaptics_tcm_resume()` 上电复位后未先执行 `identify_chip` / `get_fw_config`，且 `preserve_touch_report_config()` 未校验 `TCM_STATUS_OK (0x01)` 与排空异步 `REPORT_IDENTIFY`（28 字节），导致唤醒后覆盖开机时的 128 字节 `touch_config` 使触控失灵。修复后新增公共命令应答排空函数 `synaptics_tcm_read_cmd_resp()`、`identify_chip` 长度防护、`suspend()` 释放 `active_slots` 及 `resume()` 完整重初始化序列，构建并归档至 [development/milestones/P3_10/](development/milestones/P3_10/)。
* **实机 4 轮验收 (`boot_id`: `0d115116-2a2e-4642-be35-2f3d8299ba8b`)**：连续完成 3 轮 `deep` (`WIFI=enabled` / `disabled` / `enabled`) + 1 轮 `s2idle` (`WIFI=enabled`) `systemctl suspend` 休眠唤醒（`suspend_stats`: `success=4, fail=0`），单次电源键亮/熄屏、触控滑动解锁（`128 bytes`）、`wlp1s0` 重连、`es2gears_wayland` + GPU/GMU `auto` 挂起与声卡均正常。

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
