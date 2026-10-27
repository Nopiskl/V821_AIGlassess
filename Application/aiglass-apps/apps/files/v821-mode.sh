#!/bin/sh
# Install into /etc/media. The BSP init service supplies the boot role/mode.
# Keep exec so signals reach the actual application; do not run both roles.
set -eu

usage() {
    echo 'Usage: v821-mode.sh ui /absolute/path/to/lvgl-host [args...]' >&2
    echo '       v821-mode.sh controlled normal|photo|audio|video|download|ota|livestream' >&2
    echo '       controlled also accepts AG_MODE values: 0,1,2,3,5,6,8' >&2
    exit 2
}

[ "$#" -ge 1 ] || usage
v821_role=$1
shift
case "$v821_role" in
    ui)
        [ "$#" -ge 1 ] || usage
        case "$1" in /*) ;; *) usage ;; esac
        [ -x "$1" ] || { echo 'LVGL host is not executable' >&2; exit 1; }
        export V821_RUNTIME_MODE=ui
        exec "$@"
        ;;
    controlled)
        [ "$#" -eq 1 ] || usage
        case "$1" in
            0|photo) v821_app=photo ;;
            1|video) v821_app=video ;;
            2|download) v821_app=download ;;
            3|ota) v821_app=ota ;;
            5|normal) v821_app=normal ;;
            6|audio) v821_app=audio ;;
            8|livestream) v821_app=livestream ;;
            *) usage ;;
        esac
        export V821_RUNTIME_MODE=controlled
        v821_binary="/bin/ai_glass_${v821_app}"
        [ -x "$v821_binary" ] || { echo "Missing $v821_binary" >&2; exit 1; }
        exec "$v821_binary"
        ;;
    *) usage ;;
esac
