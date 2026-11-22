#!/bin/sh
# Wait up to 20s for hci0 to finish QCA FW setup before setting public BD_ADDR
for i in $(seq 1 20); do
  if [ -e /sys/class/bluetooth/hci0 ]; then
    # Placeholder locally-administered BD_ADDR; replace per device as needed
    OUT=$(timeout 6 /usr/bin/script -q -c "/usr/bin/btmgmt --index 0 public-addr 02:00:00:00:00:01" /dev/null 2>&1 || true)
    if echo "$OUT" | grep -qiE "successfully|succeeded"; then
      break
    fi
    CUR=$(/usr/bin/hciconfig hci0 2>/dev/null | grep -oE "([0-9A-Fa-f]{2}:){5}[0-9A-Fa-f]{2}" | head -n 1)
    if [ -n "$CUR" ] && ! echo "$CUR" | grep -qiE "^(00:00:00:00:5A:AD|00:00:00:00:00:00)$"; then
      break
    fi
  fi
  sleep 1
done
[ -e /sys/class/bluetooth/hci0 ] && /usr/bin/hciconfig hci0 up >/dev/null 2>&1 || true
exit 0
