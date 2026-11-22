# OnePlus Ace Pro (PGP110 / ovaltine) Mainline Linux (Ubuntu / Phosh) Test Port

> [!CAUTION]
> **⚠️ 仅限测试与研究使用，禁止用于正式生产环境（FOR TESTING & RESEARCH ONLY — NOT FOR PRODUCTION USE）**  
> 本仓库整理自一加 Ace Pro (SM8475 / `ovaltine`) 主线 Linux `7.3.0-rc4` 移植实验（P3.10 基线），包含内核配置、设备树、增量补丁、用户态 systemd/udev/显示脚本及工程文档。刷写内核与模块存在变砖风险，操作前请务必保留原厂分区与 Slot `_b` 备份。

---

## 1. 已处理 / 已完成模块清单 (P3.10)

- **UFS 3.1 存储**：`ufs_qcom` + `qcom-qmp-ufs-phy`（已修复时钟引用持续累加）。
- **显示与多刷新率 (DSI / DPU1)**：BOE RM692E5 `1080x2412` @ 60Hz / 90Hz / 120Hz，DSC 1.2，DSI PHY AHB `prepare` 生命周期修复（`dispcc` 熄屏收敛至 `suspended`）。
- **触控 (Synaptics S3910)**：`synaptics_tcm` SPI 驱动，已修复 S2R (`deep` / `s2idle`) 唤醒后异步 `REPORT_IDENTIFY` 覆盖 128 字节 `touch_config` 的问题。
- **GPU / GMU 加速 (Adreno 730)**：`msm` + `a6xx_gpu`（OpenGL ES / Vulkan），已修复 `a6xx_hfi_irq` 掩码清除与内存序缺陷，默认 `power/control=auto` 稳定。
- **S2R 休眠唤醒 (`deep` & `s2idle`)**：`systemctl suspend` 下 3 轮 `deep` + 1 轮 `s2idle` 验证通过，电源键单次唤醒/熄屏、触控、Wi-Fi、GPU/GMU 与音频恢复正常。
- **Wi-Fi 6E / 蓝牙 5.2**：WCN6855 (`ath11k_pci` + `hci_uart`)。
- **USB Gadget & Type-C**：HS-only DTB 裁剪（`gcc_usb3_prim_phy_pipe_clk` 收敛 `0/0`），支持 NCM/RNDIS + ACM（默认 USB 调试网口 `172.16.42.1/24`）。
- **音频与电池**：WCD9385 + 双路 TFA9874 立体声扬声器 + `qcom_battmgr`（已静默 enum 9 中断风暴，支持标准 USB-PD 充电）。

## 2. 未处理 / 暂缓模块与已知限制

根据当前阶段收敛范围，以下模块**暂不作为当前目标 / 尚未适配**：
- **5G 基带调制解调器 (`modem` / `ipa`)**：未启用蜂窝数据与通话。
- **SuperVOOC / UFCS 私有快充协议**：当前仅支持标准 USB-PD 充电。
- **摄像头子系统 (`camss` / IMX766 / OV08D / OV02B)**：未启用拍照与预览。
- **屏下指纹识别**：未适配 TEE/光学指纹。
- **R4 / R5 深度功耗收敛**：PCIe/MHI/`ath11k` 深度 ASPM 与 DDR/AOSS 低功耗计数优化暂缓。

## 3. 敏感信息脱敏说明

仓库内已对所有设备隐私及个人环境信息进行脱敏处理：
- 宿主机工作区绝对路径替换为 `<WORKSPACE>` / `<HOME>`；
- 设备序列号替换为 `0123456789ABCDEF`（`rootfs/usr/local/sbin/acepro-usb-gadget.sh`）或 `<DEVICE_SERIAL>`；
- 蓝牙固定地址替换为本地管理占位地址 `02:00:00:00:00:01`（`rootfs/usr/local/sbin/acepro-bt-addr.sh`，请按实际设备修改）；
- 测试 Wi-Fi SSID、BSSID 及内网 IP 段均已替换为占位符。

## 4. 目录结构

```text
.
├── README.md
├── .gitignore
├── docs/
│   ├── PROJECT_HANDOVER_GUIDE.md   # 脱敏版工程指导手册 (v2.3)
│   └── MILESTONES_SUMMARY.md       # P3.8 / P3.9 / P3.10 里程碑与验收摘要
├── kernel/
│   ├── configs/ovaltine_p3_10.config
│   ├── dts/{sm8475-oneplus-ovaltine.dts,sm8475-ovaltine.dtso}
│   ├── drivers/panel-boe-rm692e5.c
│   └── patches/                    # 按 0001 -> 0005 顺序应用到 Linux 7.3-rc4 (6d6be0ba1)
└── rootfs/
    ├── etc/{phosh/phoc.ini,systemd/system/*.service,udev/rules.d/90-acepro-input.rules}
    └── usr/local/{bin/acepro-display-sync.py,sbin/*.sh}
```

## 5. 内核打补丁与构建指引

1. 准备主线 Linux `7.3-rc4`（基线 commit `6d6be0ba1`），放入新增板级文件：
   ```bash
   cp kernel/dts/sm8475-oneplus-ovaltine.dts kernel/dts/sm8475-ovaltine.dtso <LINUX_SRC>/arch/arm64/boot/dts/qcom/
   cp kernel/drivers/panel-boe-rm692e5.c <LINUX_SRC>/drivers/gpu/drm/panel/
   ```
2. 按编号顺序应用补丁：
   ```bash
   for p in kernel/patches/000*.patch; do git -C <LINUX_SRC> apply "$p"; done
   ```
3. 使用 `kernel/configs/ovaltine_p3_10.config` 作为 `.config`，设置 `ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- LOCALVERSION=` 编译内核镜像、`dtbs` 与 `modules`。

> [!WARNING]
> **双槽位安全红线**：本移植仅在 **Slot `_a`**（`boot_a` / `vendor_boot_a` / `dtbo_a`）调试，**严禁刷写或修改 Slot `_b`**（保留出厂系统作为急救回退）。
