#!/usr/bin/env python3
import os
import re
import sys
import time
import subprocess
import configparser
from gi.repository import GLib, Gio

PHOC_INI = "/etc/phosh/phoc.ini"
DSI_SECTION = "output:DSI-1"

def get_current_wlr_mode():
    try:
        env = os.environ.copy()
        if "XDG_RUNTIME_DIR" not in env:
            env["XDG_RUNTIME_DIR"] = "/run/user/1001"
        if "WAYLAND_DISPLAY" not in env:
            env["WAYLAND_DISPLAY"] = "wayland-0"
        
        out = subprocess.check_output(
            ["wlr-randr", "--output", "DSI-1"],
            env=env,
            stderr=subprocess.DEVNULL,
            text=True
        )
        # Look for current mode line, e.g. "  1080x2412 px, 60.000000 Hz (current)"
        for line in out.splitlines():
            if "current" in line:
                m = re.search(r"(\d+x\d+)\s+px,\s+([0-9.]+)\s+Hz", line)
                if m:
                    res = m.group(1)
                    hz = round(float(m.group(2)))
                    return f"{res}@{hz}Hz"
    except Exception as e:
        print(f"Error querying wlr-randr: {e}", file=sys.stderr)
    return None

def update_phoc_ini(mode_str):
    if not mode_str or not os.path.exists(PHOC_INI):
        return False
    
    try:
        with open(PHOC_INI, "r") as f:
            lines = f.readlines()
        
        in_dsi = False
        mode_set = False
        new_lines = []
        for line in lines:
            stripped = line.strip()
            if stripped.startswith("[") and stripped.endswith("]"):
                if in_dsi and not mode_set:
                    new_lines.append(f"mode = {mode_str}\n")
                    mode_set = True
                in_dsi = (stripped == f"[{DSI_SECTION}]")
            elif in_dsi and stripped.startswith("mode"):
                new_lines.append(f"mode = {mode_str}\n")
                mode_set = True
                continue
            new_lines.append(line)
        
        if in_dsi and not mode_set:
            new_lines.append(f"mode = {mode_str}\n")
            mode_set = True
        
        if new_lines == lines:
            return True

        tmp_file = PHOC_INI + ".tmp"
        with open(tmp_file, "w") as f:
            f.writelines(new_lines)
        os.replace(tmp_file, PHOC_INI)
        # Keep ownership and permissions accessible
        os.chmod(PHOC_INI, 0o664)
        print(f"[display-sync] Persisted mode {mode_str} to {PHOC_INI}")
        return True
    except Exception as e:
        print(f"[display-sync] Error updating {PHOC_INI}: {e}", file=sys.stderr)
        return False

def on_monitors_changed(proxy, sender_name, signal_name, parameters):
    print("[display-sync] Received MonitorsChanged signal")
    # Small sleep to allow wlr-randr state to stabilize
    time.sleep(0.5)
    mode = get_current_wlr_mode()
    if mode:
        print(f"[display-sync] Current mode detected: {mode}")
        update_phoc_ini(mode)

def main():
    print("[display-sync] Starting OnePlus Ace Pro display persistence daemon...")
    # Initial check on daemon startup
    initial_mode = get_current_wlr_mode()
    if initial_mode:
        print(f"[display-sync] Initial active mode: {initial_mode}")
        update_phoc_ini(initial_mode)
    
    # Connect to Mutter DisplayConfig DBus signal
    try:
        bus = Gio.bus_get_sync(Gio.BusType.SESSION, None)
        proxy = Gio.DBusProxy.new_sync(
            bus,
            Gio.DBusProxyFlags.NONE,
            None,
            "org.gnome.Mutter.DisplayConfig",
            "/org/gnome/Mutter/DisplayConfig",
            "org.gnome.Mutter.DisplayConfig",
            None
        )
        proxy.connect("g-signal", on_monitors_changed)
        print("[display-sync] Subscribed to org.gnome.Mutter.DisplayConfig signals")
    except Exception as e:
        print(f"[display-sync] DBus subscribe failed: {e}, falling back to poll", file=sys.stderr)

    loop = GLib.MainLoop()
    loop.run()

if __name__ == "__main__":
    main()
