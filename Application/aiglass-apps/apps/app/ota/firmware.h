#ifndef __HTTP_CLINET_H
#define __HTTP_CLIENT_H

#include "http.h"

#define BUFFER_SIZE 4096
#define SAVE_PATH "/mnt/UDISK/openwrt_v821_aiglass-ab.swu"

int start_down_ota_firmware(char *_url);
uint8_t get_download_firware_schedule(void);

#endif
