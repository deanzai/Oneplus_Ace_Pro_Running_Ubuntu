#!/usr/bin/env python3
"""Ace Pro two-stage black-screen EAS scheduler & s2idle Connected Standby daemon.

Stage 1 (both AC and Battery, whenever display is blanked / PowerSaveMode != 0):
- iw dev wlp1s0 set power_save on
- kernel.sched_util_clamp_min = 0, kernel.sched_util_clamp_max = 200
- scaling_max_freq: policy0=1056000, policy4=633600, policy7=787200
- schedutil/rate_limit_us: policy0=10000, policy4=20000, policy7=20000
- vm.dirty_writeback_centisecs = 1500
(Restored immediately when screen turns ON.)

Stage 2 (Battery only: UPower OnBattery=True and usb/wls online==0):
- Defers s2idle if wlp1s0 rx+tx rate > 4096 B/s.
- Enters kernel s2idle directly via race-free /sys/power/wakeup_count + "freeze" > /sys/power/state
  (avoiding NetworkManager PrepareForSleep deauth), after checking login1 ListInhibitors().
- On wake from freeze, compares ath11k_wake / mhi / pmic_pwrkey IRQs:
  - pmic_pwrkey delta == 0 (network/async wake): enforces PowerSaveMode=3 & Stage 1, 15s grace.
  - pmic_pwrkey delta > 0 (power key wake): 45s cooldown.
"""

import subprocess
import time
import gi

gi.require_version("Gio", "2.0")
from gi.repository import Gio, GLib

IDLE_TIMEOUT_SEC = 180.0
POST_WAKE_COOLDOWN_SEC = 45.0
NET_WAKE_GRACE_SEC = 15.0
POLL_INTERVAL_SEC = 5
NET_BUSY_BYTES_PER_SEC = 4096.0

DPMS_PATH = "/sys/class/drm/card1-DSI-1/dpms"
BL_POWER_PATH = "/sys/class/backlight/ae94000.dsi.0/bl_power"
USB_ONLINE_PATH = "/sys/class/power_supply/qcom-battmgr-usb/online"
WLS_ONLINE_PATH = "/sys/class/power_supply/qcom-battmgr-wls/online"
RX_BYTES_PATH = "/sys/class/net/wlp1s0/statistics/rx_bytes"
TX_BYTES_PATH = "/sys/class/net/wlp1s0/statistics/tx_bytes"
WLP1S0_ADDR_PATH = "/sys/class/net/wlp1s0/address"
ROOT_PORT_WAKEUP_PATH = "/sys/bus/pci/devices/0000:00:00.0/power/wakeup"
PCIE_WAKEUP_PATH = "/sys/bus/pci/devices/0000:01:00.0/power/wakeup"


def read_sysfs(path, default=""):
    try:
        with open(path, "r", encoding="utf-8") as f:
            return f.read().strip()
    except (OSError, TypeError, ValueError):
        return default


def write_sysfs(path, value):
    try:
        with open(path, "w", encoding="utf-8") as f:
            f.write(str(value) + "\n")
        return True
    except (OSError, TypeError, ValueError):
        return False


def is_screen_off():
    dpms = read_sysfs(DPMS_PATH)
    bl = read_sysfs(BL_POWER_PATH)
    return dpms == "Off" or bl == "4"


def is_external_power_online(upower_proxy):
    usb = read_sysfs(USB_ONLINE_PATH, "0")
    wls = read_sysfs(WLS_ONLINE_PATH, "0")
    if usb != "0" or wls != "0":
        return True
    try:
        v = upower_proxy.get_cached_property("OnBattery")
        if v is not None and not v.unpack():
            return True
    except Exception:
        pass
    return False


def read_net_bytes():
    try:
        rx = int(read_sysfs(RX_BYTES_PATH, "0"))
        tx = int(read_sysfs(TX_BYTES_PATH, "0"))
        return rx + tx
    except ValueError:
        return 0


def read_irq_total(name):
    total = 0
    try:
        with open("/proc/interrupts", "r", encoding="utf-8") as f:
            for line in f:
                if name in line:
                    parts = line.split()
                    for tok in parts[1:]:
                        if tok.isdigit():
                            total += int(tok)
                        else:
                            break
    except OSError:
        pass
    return total


def set_phoc_power_save_mode(mode):
    try:
        subprocess.run(
            [
                "sudo",
                "-u",
                "acepro",
                "DBUS_SESSION_BUS_ADDRESS=unix:path=/run/user/1001/bus",
                "gdbus",
                "call",
                "--session",
                "--dest",
                "org.gnome.Mutter.DisplayConfig",
                "--object-path",
                "/org/gnome/Mutter/DisplayConfig",
                "--method",
                "org.freedesktop.DBus.Properties.Set",
                "org.gnome.Mutter.DisplayConfig",
                "PowerSaveMode",
                f"<int32 {int(mode)}>",
            ],
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
            timeout=3,
            check=False,
        )
    except Exception:
        pass


def get_wlp1s0_phy():
    phy = read_sysfs("/sys/class/net/wlp1s0/phy80211/name")
    if phy:
        return phy
    try:
        out = subprocess.check_output(["iw", "dev", "wlp1s0", "info"], text=True, stderr=subprocess.DEVNULL)
        for line in out.splitlines():
            parts = line.strip().split()
            if len(parts) >= 2 and parts[0] == "wiphy":
                return f"phy{parts[1]}"
    except Exception:
        pass
    return "phy0"


def configure_wowlan_live():
    mac = read_sysfs(WLP1S0_ADDR_PATH)
    phy = get_wlp1s0_phy()
    if mac and phy:
        subprocess.run(
            [
                "iw",
                phy,
                "wowlan",
                "enable",
                "disconnect",
                "magic-packet",
                "gtk-rekey-failure",
                "patterns",
                f"{mac}:-:-:-:-:-:-:08:00",
                f"{mac}:-:-:-:-:-:-:86:dd",
            ],
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
            check=False,
        )
    write_sysfs(ROOT_PORT_WAKEUP_PATH, "enabled")
    write_sysfs(PCIE_WAKEUP_PATH, "enabled")


def apply_stage1_profile(screen_off):
    if screen_off:
        subprocess.run(
            ["iw", "dev", "wlp1s0", "set", "power_save", "on"],
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
            check=False,
        )
        write_sysfs("/proc/sys/kernel/sched_util_clamp_min", "0")
        write_sysfs("/proc/sys/kernel/sched_util_clamp_max", "200")
        write_sysfs("/sys/devices/system/cpu/cpufreq/policy0/scaling_max_freq", "1056000")
        write_sysfs("/sys/devices/system/cpu/cpufreq/policy4/scaling_max_freq", "633600")
        write_sysfs("/sys/devices/system/cpu/cpufreq/policy7/scaling_max_freq", "787200")
        write_sysfs("/sys/devices/system/cpu/cpufreq/policy0/schedutil/rate_limit_us", "10000")
        write_sysfs("/sys/devices/system/cpu/cpufreq/policy4/schedutil/rate_limit_us", "20000")
        write_sysfs("/sys/devices/system/cpu/cpufreq/policy7/schedutil/rate_limit_us", "20000")
        write_sysfs("/proc/sys/vm/dirty_writeback_centisecs", "1500")
        print("[acepro-idle-suspend] Stage 1 APPLIED (screen off: power_save=on, uclamp_max=200)", flush=True)
    else:
        subprocess.run(
            ["iw", "dev", "wlp1s0", "set", "power_save", "off"],
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
            check=False,
        )
        write_sysfs("/proc/sys/kernel/sched_util_clamp_max", "1024")
        write_sysfs("/sys/devices/system/cpu/cpufreq/policy0/scaling_max_freq", "1804800")
        write_sysfs("/sys/devices/system/cpu/cpufreq/policy4/scaling_max_freq", "2745600")
        write_sysfs("/sys/devices/system/cpu/cpufreq/policy7/scaling_max_freq", "3187200")
        write_sysfs("/sys/devices/system/cpu/cpufreq/policy0/schedutil/rate_limit_us", "1000")
        write_sysfs("/sys/devices/system/cpu/cpufreq/policy4/schedutil/rate_limit_us", "1000")
        write_sysfs("/sys/devices/system/cpu/cpufreq/policy7/schedutil/rate_limit_us", "1000")
        write_sysfs("/proc/sys/vm/dirty_writeback_centisecs", "500")
        print("[acepro-idle-suspend] Stage 1 RESTORED (screen on: power_save=off, uclamp_max=1024)", flush=True)


def has_sleep_block_inhibitor(login1_proxy):
    try:
        res = login1_proxy.call_sync(
            "ListInhibitors", None, Gio.DBusCallFlags.NONE, -1, None
        )
        for what, who, why, mode, uid, pid in res.unpack()[0]:
            if mode == "block" and "sleep" in what.split(":"):
                return True, f"{who} ({why})"
    except Exception as e:
        print(f"[acepro-idle-suspend] ListInhibitors error: {e}", flush=True)
    return False, None


class IdleSuspendDaemon:
    def __init__(self):
        self.bus = Gio.bus_get_sync(Gio.BusType.SYSTEM, None)
        self.login1 = Gio.DBusProxy.new_sync(
            self.bus,
            Gio.DBusProxyFlags.NONE,
            None,
            "org.freedesktop.login1",
            "/org/freedesktop/login1",
            "org.freedesktop.login1.Manager",
            None,
        )
        self.upower = Gio.DBusProxy.new_sync(
            self.bus,
            Gio.DBusProxyFlags.NONE,
            None,
            "org.freedesktop.UPower",
            "/org/freedesktop/UPower",
            "org.freedesktop.UPower",
            None,
        )
        self.idle_start = None
        self.cooldown_until = time.monotonic() + POST_WAKE_COOLDOWN_SEC
        self.prev_screen_off = is_screen_off()
        self.prev_on_battery = not is_external_power_online(self.upower)
        self.suspend_in_progress = False
        self.last_net_bytes = read_net_bytes()
        self.last_net_time = time.monotonic()
        self.pre_sleep_wake_irq = 0
        self.pre_sleep_pwr_irq = 0

        self.login1.connect("g-signal", self.on_login1_signal)
        apply_stage1_profile(self.prev_screen_off)
        print(
            f"[acepro-idle-suspend] Started (timeout={IDLE_TIMEOUT_SEC:.0f}s, "
            f"cooldown={POST_WAKE_COOLDOWN_SEC:.0f}s, on_battery={self.prev_on_battery})",
            flush=True,
        )

    def on_login1_signal(self, proxy, sender, signal_name, params):
        if signal_name != "PrepareForSleep":
            return
        active = params.unpack()[0]
        now = time.monotonic()
        if active:
            self.suspend_in_progress = True
            self.idle_start = None
            self.pre_sleep_wake_irq = read_irq_total("ath11k_wake")
            self.pre_sleep_pwr_irq = read_irq_total("pmic_pwrkey")
        else:
            self.suspend_in_progress = False
            self.idle_start = None
            self.cooldown_until = now + POST_WAKE_COOLDOWN_SEC
            self.last_net_bytes = read_net_bytes()
            self.last_net_time = now

    def enter_connected_s2idle(self):
        blocked, blocker = has_sleep_block_inhibitor(self.login1)
        if blocked:
            print(
                f"[acepro-idle-suspend] Sleep blocked by inhibitor: {blocker}; skipping",
                flush=True,
            )
            self.idle_start = time.monotonic() - (IDLE_TIMEOUT_SEC - 30.0)
            return

        configure_wowlan_live()

        pre_pme = read_irq_total("PCIe PME")
        pre_wake = read_irq_total("ath11k_wake")
        pre_mhi = read_irq_total("mhi")
        pre_pwr = read_irq_total("pmic_pwrkey")

        wc = read_sysfs("/sys/power/wakeup_count")
        if not wc or not write_sysfs("/sys/power/wakeup_count", wc):
            print("[acepro-idle-suspend] wakeup_count race detected; deferring freeze", flush=True)
            return

        print(
            f"[acepro-idle-suspend] Entering kernel s2idle (freeze) "
            f"[pcie_pme={pre_pme}, ath11k_wake={pre_wake}, mhi={pre_mhi}, pmic_pwrkey={pre_pwr}]",
            flush=True,
        )
        self.suspend_in_progress = True
        self.idle_start = None
        ok = write_sysfs("/sys/power/state", "freeze")
        self.suspend_in_progress = False

        now = time.monotonic()
        self.last_net_bytes = read_net_bytes()
        self.last_net_time = now

        post_pme = read_irq_total("PCIe PME")
        post_wake = read_irq_total("ath11k_wake")
        post_mhi = read_irq_total("mhi")
        post_pwr = read_irq_total("pmic_pwrkey")
        pme_delta = post_pme - pre_pme
        wake_delta = post_wake - pre_wake
        mhi_delta = post_mhi - pre_mhi
        pwr_delta = post_pwr - pre_pwr

        if not ok:
            print("[acepro-idle-suspend] freeze write aborted/failed; cooling down", flush=True)
            self.cooldown_until = now + NET_WAKE_GRACE_SEC
            return

        if pwr_delta == 0:
            set_phoc_power_save_mode(3)
            self.prev_screen_off = True
            apply_stage1_profile(True)
            self.cooldown_until = now + NET_WAKE_GRACE_SEC
            print(
                f"[acepro-idle-suspend] Silent wake from freeze "
                f"(pcie_pme +{pme_delta}, ath11k_wake +{wake_delta}, mhi +{mhi_delta}, pmic_pwrkey +0); "
                f"kept PowerSaveMode=3 & Stage 1, grace={NET_WAKE_GRACE_SEC:.0f}s",
                flush=True,
            )
        else:
            self.cooldown_until = now + POST_WAKE_COOLDOWN_SEC
            print(
                f"[acepro-idle-suspend] Power-key wake from freeze "
                f"(pcie_pme +{pme_delta}, ath11k_wake +{wake_delta}, mhi +{mhi_delta}, pmic_pwrkey +{pwr_delta}); "
                f"cooldown={POST_WAKE_COOLDOWN_SEC:.0f}s",
                flush=True,
            )

    def tick(self):
        now = time.monotonic()
        if self.suspend_in_progress:
            return True

        cur_bytes = read_net_bytes()
        dt = max(now - self.last_net_time, 0.1)
        net_rate = max(0.0, (cur_bytes - self.last_net_bytes) / dt)
        self.last_net_bytes = cur_bytes
        self.last_net_time = now

        screen_off = is_screen_off()
        on_battery = not is_external_power_online(self.upower)

        if on_battery != self.prev_on_battery:
            print(f"[acepro-idle-suspend] Power state changed: on_battery={on_battery}", flush=True)
            self.prev_on_battery = on_battery
            self.idle_start = None

        if screen_off != self.prev_screen_off:
            self.prev_screen_off = screen_off
            apply_stage1_profile(screen_off)
            if not screen_off:
                self.idle_start = None
                self.cooldown_until = max(self.cooldown_until, now + POST_WAKE_COOLDOWN_SEC)
                print(
                    f"[acepro-idle-suspend] Screen turned ON; reset idle timer "
                    f"(cooldown={POST_WAKE_COOLDOWN_SEC:.0f}s)",
                    flush=True,
                )
            else:
                print("[acepro-idle-suspend] Screen turned OFF", flush=True)

        if not on_battery or not screen_off:
            self.idle_start = None
            return True

        if net_rate > NET_BUSY_BYTES_PER_SEC:
            if self.idle_start is not None:
                print(
                    f"[acepro-idle-suspend] Traffic {net_rate:.0f} B/s > {NET_BUSY_BYTES_PER_SEC:.0f} B/s; "
                    f"deferring s2idle",
                    flush=True,
                )
            self.idle_start = None
            return True

        if now < self.cooldown_until:
            self.idle_start = None
            return True

        if self.idle_start is None:
            self.idle_start = now
            print(
                f"[acepro-idle-suspend] Stage 2 idle timer started on battery "
                f"(will enter s2idle in {IDLE_TIMEOUT_SEC:.0f}s)",
                flush=True,
            )
            return True

        elapsed = now - self.idle_start
        if elapsed >= IDLE_TIMEOUT_SEC:
            self.enter_connected_s2idle()

        return True


def main():
    daemon = IdleSuspendDaemon()
    GLib.timeout_add_seconds(POLL_INTERVAL_SEC, daemon.tick)
    GLib.MainLoop().run()


if __name__ == "__main__":
    main()
