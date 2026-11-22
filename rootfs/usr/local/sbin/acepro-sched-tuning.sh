#!/bin/sh
# Mainline SM8475 EAS + schedutil tuning (keep all CPUs online, full OPP range)
for c in /sys/devices/system/cpu/cpu[0-7]/online; do
    [ -w "$c" ] && echo 1 > "$c" 2>/dev/null || true
done

for p in /sys/devices/system/cpu/cpufreq/policy*; do
    [ -d "$p" ] || continue
    echo schedutil > "$p/scaling_governor" 2>/dev/null || true
    [ -r "$p/cpuinfo_min_freq" ] && cat "$p/cpuinfo_min_freq" > "$p/scaling_min_freq" 2>/dev/null || true
    [ -r "$p/cpuinfo_max_freq" ] && cat "$p/cpuinfo_max_freq" > "$p/scaling_max_freq" 2>/dev/null || true
    if [ -w "$p/schedutil/rate_limit_us" ]; then
        echo 1000 > "$p/schedutil/rate_limit_us" 2>/dev/null || true
    fi
done
exit 0
