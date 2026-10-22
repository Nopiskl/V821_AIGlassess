#ifndef __HTTP_SERVER_H
#define __HTTP_SERVER_H

/* config */
#define CONFIG_DEBUG_DUMP_FILE_CONTENT 0
#define CONFIG_DEBUG_DUMP_HTTP_REQUEST_INFO 0

#define CONFIG_HTTP_RESPONSE_HEADER_LEN 1024
#define CONFIG_HTTP_REQUEST_TOTAL_LEN 2048

#define CONFIG_FILE_PATH_LEN 256
#define CONFIG_FILE_NAME_LEN 128

#define CONFIG_FILELIST_CHECK 1
#define CONFIG_SEND_FILE_WITH_ZERO_COPY 1
#define CONFIG_TEST_HTTP_SERVER 0

#define CONFIG_DEFAULT_FILE_DIR "/mnt/UDISK"
#define CONFIG_DEFAULT_FILELIST_NAME "media.config"
#define CONFIG_DEFAULT_BASE_URL "/files/"

#define DIR_GPT_HEAD_LEN 16
#define DIR_GPT_ENTRY_LEN 40
#define DIR_GPT_FILE_NAME_LEN 32

/*********/

#include "http.h"

enum {
    JPG_FILE = 0,
    TEXT_FILE = 1,
    MP4_FILE = 2,

    MAX_TYPE
};

enum {
    TASK_INIT,
    TASK_CREATED,
    TASK_RUNNING,
    TASK_EXITED
};

enum {
	IS_NOACCESS = -1,
	IS_FILES = 0,
	IS_DIR = 1,
	IS_OTHER = 2,
};

/* thread data */
typedef struct {
	void *data1;
} task_data_t;

typedef struct {
	int status_code;
	int content_type;
	long content_length;
} response_info_t;

int  fileser_start(void);
void fileser_stop(void);

#endif /* __HTTP_SERVER_H */
