#!/bin/sh

LOG_DIR="/mnt/UDISK/log"
LOG_PREFIX="aglink"
LOG_LIST="/mnt/UDISK/log/log.list"

MAX_LOG_FILES=20

LOG_SOURCES="/tmp/aglink_tmp.log /mnt/UDISK/aglink.log dmesg"

mkdir -p "$LOG_DIR" || {
    echo "create dir failed $LOG_DIR" >&2
    exit 1
}
timestamp=$(date +%Y%m%d%H%M%S%3N)
output_file="${LOG_DIR}/${LOG_PREFIX}_${timestamp}.log"

clean_logs() {
	if [ -f "$LOG_LIST" ]; then
	    total=$(wc -l < "$LOG_LIST")
	    if [ "$total" -ge "$MAX_LOG_FILES" ]; then
	        old_file=$(sed -n '1p' "$LOG_LIST")
	        rm -f "$LOG_DIR/$old_file" && echo "clean file $LOG_DIR/$old_file"
	        sed -i '1d' "$LOG_LIST"
	    fi
	fi
}

clean_logs

merge_logs() {
	for source in $LOG_SOURCES; do
        case "$source" in
            "dmesg")
                dmesg >> "$output_file"
                ;;
            *)
                if [ -r "$source" ]; then
                    cat "$source" >> "$output_file"
                else
                    echo "[warning] File does not exist $source"
                fi
                ;;
        esac
    done
	rm -rf /tmp/aglink_tmp.log
	rm -rf /mnt/UDISK/aglink.log
	echo -e "${LOG_PREFIX}_${timestamp}.log" >> ${LOG_LIST}
	echo "save log success: $output_file"
	sync
}

merge_logs

exit 0
