#!/bin/sh
MODULES_DIR="/lib/modules/$(uname -r)"
BOOT_MEDIA="0"
mode=$(cat /sys/kernel/aglink_mode)

echo "Current mode: $mode"
echo "Modules directory: $MODULES_DIR"
ulimit -c unlimited
echo /mnt/UDISK/coredump-%e-%p-%s-%t > /proc/sys/kernel/core_pattern

load_wlan() {
    echo "Loading WLAN module..."
    case "$(sed 's/ /\n/g' /proc/cmdline | awk -F= '/^root=/{print $2}')" in
    /dev/mtdblock*)
        echo "Boot media is nor"
        BOOT_MEDIA="0"
        MODULES_DIR="/mnt/UDISK/data/lib/modules/$(uname -r)"
        ;;
    /dev/mmcblk*)
        echo "Boot media is sdnand"
        BOOT_MEDIA="1"
        MODULES_DIR="/lib/modules/$(uname -r)"
        ;;
    *)
        echo "Boot media is unknow"
        ;;
    esac
    local timeout="3000"
    local cnt="0"
    while [ ! -e "$MODULES_DIR/v821_fmac.ko" ] && [ ! -e "$MODULES_DIR/v821_smac.ko" ]; do
        if [ $cnt -ge $timeout ]; then
            echo "check wireless module timeout"
            return
        fi
        let cnt=cnt+1
        usleep 10000
    done
    local elapsed_time=$(expr $cnt \* 10)
    echo "check wireless module $elapsed_time ms."

    if [ -f "$MODULES_DIR/v821_fmac.ko" ]; then
        echo "Found v821_fmac.ko, checking if it's loaded..."
        lsmod | grep -q 'v821_fmac' || { echo "Loading v821_fmac.ko..."; insmod "$MODULES_DIR/v821_fmac.ko"; }
    fi
    if [ ! -f "$MODULES_DIR/v821_fmac.ko" ] && [ -f "$MODULES_DIR/v821_smac.ko" ]; then
        echo "Found v821_smac.ko, checking if it's loaded..."
        lsmod | grep -q 'v821_smac' || { echo "Loading v821_smac.ko..."; insmod "$MODULES_DIR/v821_smac.ko"; }
    fi
}

execute_if_exists() {
    if [ -f "$1" ]; then
        if [ $# -ge 2 ]; then
            echo "Executing $1 $2..."
            "$1" "$2" &
        else
            echo "Executing $1..."
            "$1" &
        fi
    else
        echo "File $1 does not exist, skipping execution."
    fi
}

case "$mode" in
    0)
        execute_if_exists "/bin/ai_glass_photo"
        ;;
    1)
        execute_if_exists "/bin/ai_glass_video"
        ;;
    2)
        load_wlan  # Loading WLAN module for mode 2
        execute_if_exists "/bin/ai_glass_download"
        ;;
    3)
        load_wlan  # Loading WLAN module for mode 3
        execute_if_exists "/bin/ai_glass_ota" $BOOT_MEDIA
        ;;
    4)
        echo "AI mode no need app."
        ;;
    5|7)
        execute_if_exists "/bin/ai_glass_normal"
        ;;
    6)
        execute_if_exists "/bin/ai_glass_audio"
        ;;
    8)
        load_wlan  # Loading WLAN module for mode 8
		if [ "$BOOT_MEDIA" = "0" ]; then
			execute_if_exists "/mnt/UDISK/data/usr/sbin/ai_glass_livestream"
		else
			execute_if_exists "/bin/ai_glass_livestream"
		fi
        ;;
    *)
        echo "Unknown mode: $mode. No action taken."
        ;;
esac
