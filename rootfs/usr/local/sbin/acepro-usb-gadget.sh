#!/bin/sh
set -eu

# Mount configfs if not already mounted
mountpoint -q /sys/kernel/config || mount -t configfs none /sys/kernel/config

# Wait briefly for UDC controller (SM8475 dwc3 @ a600000.usb)
UDC_DEV=""
for _ in $(seq 1 20); do
    UDC_DEV=$(ls /sys/class/udc 2>/dev/null | head -n 1 || true)
    [ -n "$UDC_DEV" ] && break
    sleep 0.5
done

if [ -z "$UDC_DEV" ]; then
    echo "acepro-usb-gadget: no UDC device found" >&2
    exit 0
fi

G=/sys/kernel/config/usb_gadget/g1
if [ ! -d "$G" ]; then
    mkdir -p "$G/strings/0x409" "$G/configs/c.1/strings/0x409"
    echo 0x1d6b > "$G/idVendor"
    echo 0x0104 > "$G/idProduct"
    echo 0x0100 > "$G/bcdDevice"
    echo 0x0200 > "$G/bcdUSB"
    # Placeholder serial number; replace with your device's serial if needed
    echo "0123456789ABCDEF" > "$G/strings/0x409/serialnumber"
    echo "OnePlus-Linux" > "$G/strings/0x409/manufacturer"
    echo "AcePro-Ubuntu" > "$G/strings/0x409/product"
    echo "NCM+ACM" > "$G/configs/c.1/strings/0x409/configuration"
    echo 250 > "$G/configs/c.1/MaxPower"

    if mkdir -p "$G/functions/ncm.usb0" 2>/dev/null; then
        ln -sf "$G/functions/ncm.usb0" "$G/configs/c.1/"
    elif mkdir -p "$G/functions/rndis.usb0" 2>/dev/null; then
        ln -sf "$G/functions/rndis.usb0" "$G/configs/c.1/"
    fi

    if mkdir -p "$G/functions/acm.gs0" 2>/dev/null; then
        ln -sf "$G/functions/acm.gs0" "$G/configs/c.1/"
    fi
fi

CUR_UDC=$(cat "$G/UDC" 2>/dev/null || true)
if [ -z "$CUR_UDC" ]; then
    echo "$UDC_DEV" > "$G/UDC"
fi

# Wait briefly for usb* network interface
NET_IF=""
for _ in $(seq 1 20); do
    NET_IF=$(ls /sys/class/net 2>/dev/null | grep -E '^(usb|rndis)' | head -n 1 || true)
    [ -n "$NET_IF" ] && break
    sleep 0.2
done

if [ -n "$NET_IF" ]; then
    ip link set "$NET_IF" up
    ip addr replace 172.16.42.1/24 dev "$NET_IF"
    if command -v dnsmasq >/dev/null 2>&1; then
        if [ -f /run/acepro-usb-dhcp.pid ]; then
            kill "$(cat /run/acepro-usb-dhcp.pid)" 2>/dev/null || true
            rm -f /run/acepro-usb-dhcp.pid
        fi
        dnsmasq \
            --interface="$NET_IF" \
            --bind-interfaces \
            --port=0 \
            --dhcp-range=172.16.42.10,172.16.42.100,255.255.255.0,12h \
            --dhcp-option=3 \
            --dhcp-option=6 \
            --dhcp-leasefile=/run/acepro-usb-dhcp.leases \
            --pid-file=/run/acepro-usb-dhcp.pid
    fi
fi
