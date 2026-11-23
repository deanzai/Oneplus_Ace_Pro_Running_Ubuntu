#!/usr/bin/env python3
"""Ace Pro battery idle auto-suspend daemon.

Compensates for gsd-power skipping automatic suspend when systemd-detect-virt
reports vm-other (due to Qualcomm Gunyah DT hypervisor node).

Guardrails:
- Only triggers suspend when UPower OnBattery is True AND USB/WLS online == 0.
- Only triggers when display is off (DSI-1 dpms == Off or bl_power == 4) for >= 180s.
- Subscribes to login1 PrepareForSleep and screen-on transitions, enforcing a >= 45s
  post-wake/post-screen-on cooldown/debounce before counting idle time.
- Checks login1 ListInhibitors() for active "block" "sleep" inhibitors before suspending.
"""

import os
import sys
import time
import gi

gi.require_version("Gio", "2.0")
from gi.repository import Gio, GLib

IDLE_TIMEOUT_SEC = 180.0
POST_WAKE_COOLDOWN_SEC = 45.0
POLL_INTERVAL_SEC = 5

DPMS_PATH = "/sys/class/drm/card1-DSI-1/dpms"
BL_POWER_PATH = "/sys/class/backlight/ae94000.dsi.0/bl_power"
USB_ONLINE_PATH = "/sys/class/power_supply/qcom-battmgr-usb/online"
WLS_ONLINE_PATH = "/sys/class/power_supply/qcom-battmgr-wls/online"


def read_sysfs(path, default=""):
    try:
        with open(path, "r", encoding="utf-8") as f:
            return f.read().strip()
    except (OSError, TypeError, ValueError):
        return default


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

        self.login1.connect("g-signal", self.on_login1_signal)
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
            print("[acepro-idle-suspend] PrepareForSleep(true): entering sleep", flush=True)
        else:
            self.suspend_in_progress = False
            self.idle_start = None
            self.cooldown_until = now + POST_WAKE_COOLDOWN_SEC
            print(
                f"[acepro-idle-suspend] PrepareForSleep(false): woke up, "
                f"resetting idle timer with {POST_WAKE_COOLDOWN_SEC:.0f}s cooldown",
                flush=True,
            )

    def tick(self):
        now = time.monotonic()
        if self.suspend_in_progress:
            return True

        screen_off = is_screen_off()
        on_battery = not is_external_power_online(self.upower)

        if on_battery != self.prev_on_battery:
            print(f"[acepro-idle-suspend] Power state changed: on_battery={on_battery}", flush=True)
            self.prev_on_battery = on_battery
            self.idle_start = None

        if screen_off != self.prev_screen_off:
            self.prev_screen_off = screen_off
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

        if now < self.cooldown_until:
            self.idle_start = None
            return True

        if self.idle_start is None:
            self.idle_start = now
            print(
                f"[acepro-idle-suspend] Idle timer started on battery "
                f"(will suspend in {IDLE_TIMEOUT_SEC:.0f}s)",
                flush=True,
            )
            return True

        elapsed = now - self.idle_start
        if elapsed >= IDLE_TIMEOUT_SEC:
            blocked, blocker = has_sleep_block_inhibitor(self.login1)
            if blocked:
                print(
                    f"[acepro-idle-suspend] Sleep blocked by inhibitor: {blocker}; skipping",
                    flush=True,
                )
                self.idle_start = now - (IDLE_TIMEOUT_SEC - 30.0)
                return True

            print(
                f"[acepro-idle-suspend] Idle {elapsed:.1f}s >= {IDLE_TIMEOUT_SEC:.0f}s "
                f"on battery; calling login1.Suspend(false)",
                flush=True,
            )
            self.idle_start = None
            try:
                self.login1.call_sync(
                    "Suspend", GLib.Variant("(b)", (False,)), Gio.DBusCallFlags.NONE, -1, None
                )
            except Exception as e:
                print(f"[acepro-idle-suspend] Suspend call failed: {e}", flush=True)
                self.cooldown_until = now + POST_WAKE_COOLDOWN_SEC

        return True


def main():
    daemon = IdleSuspendDaemon()
    GLib.timeout_add_seconds(POLL_INTERVAL_SEC, daemon.tick)
    GLib.MainLoop().run()


if __name__ == "__main__":
    main()
