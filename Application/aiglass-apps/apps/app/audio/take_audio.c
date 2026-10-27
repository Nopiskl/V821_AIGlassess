/*
* Copyright (c) 2019-2025 Allwinner Technology Co., Ltd. ALL rights reserved.
*
* Allwinner is a trademark of Allwinner Technology Co.,Ltd., registered in
* the the people's Republic of China and other countries.
* All Allwinner Technology Co.,Ltd. trademarks are used with permission.
*
* DISCLAIMER
* THIRD PARTY LICENCES MAY BE REQUIRED TO IMPLEMENT THE SOLUTION/PRODUCT.
* IF YOU NEED TO INTEGRATE THIRD PARTY’S TECHNOLOGY (SONY, DTS, DOLBY, AVS OR MPEGLA, ETC.)
* IN ALLWINNERS’SDK OR PRODUCTS, YOU SHALL BE SOLELY RESPONSIBLE TO OBTAIN
* ALL APPROPRIATELY REQUIRED THIRD PARTY LICENCES.
* ALLWINNER SHALL HAVE NO WARRANTY, INDEMNITY OR OTHER OBLIGATIONS WITH RESPECT TO MATTERS
* COVERED UNDER ANY REQUIRED THIRD PARTY LICENSE.
* YOU ARE SOLELY RESPONSIBLE FOR YOUR USAGE OF THIRD PARTY’S TECHNOLOGY.
*
*
* THIS SOFTWARE IS PROVIDED BY ALLWINNER"AS IS" AND TO THE MAXIMUM EXTENT
* PERMITTED BY LAW, ALLWINNER EXPRESSLY DISCLAIMS ALL WARRANTIES OF ANY KIND,
* WHETHER EXPRESS, IMPLIED OR STATUTORY, INCLUDING WITHOUT LIMITATION REGARDING
* THE TITLE, NON-INFRINGEMENT, ACCURACY, CONDITION, COMPLETENESS, PERFORMANCE
* OR MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE.
* IN NO EVENT SHALL ALLWINNER BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
* SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
* NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
* LOSS OF USE, DATA, OR PROFITS, OR BUSINESS INTERRUPTION)
* HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
* STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
* ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
* OF THE POSSIBILITY OF SUCH DAMAGE.
*/
#include <stdint.h>
#include <stdio.h>
#include <pthread.h>
#include <signal.h>
#include <stdlib.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>
#include "aglink/ag_wrap.h"
#include <aglink/ag_proto.h>
#include <aglink_log.h>
#include <sys/time.h>
#include <unistd.h>

#define TAKE_AUDIO_PATCH "/mnt/UDISK/"
#define TAKE_AUDIO_TXT_FILE "/mnt/UDISK/media.config"
#define WAIT_VIDEO_PATCH_TIMEOUT_MS 10000
#define MAX_FILE_PATH_SIZE  (128)

int audio_dev_id = 0;

typedef enum audio_mode_state {
	AG_AUDIO_CH_CLOSE = 0,
	AG_AUDIO_CH_NEW,
	AG_AUDIO_CH_START,
	AG_AUDIO_CH_RECEIVING,
	AG_AUDIO_CH_END,
	AG_AUDIO_CH_SUCCESS,
	AG_AUDIO_CH_FAILED,
	AG_AUDIO_CH_IDLE,
} audio_mode_state;

typedef struct ag_take_audio
{
	FILE *file;
	char strAudioPath[128];
	char strAudioName[128];
}ag_take_audio;

static ag_take_audio take_audio;
static pthread_t ag_audio_thread;

volatile int audio_app_exit;

volatile audio_mode_state audio_state = AG_AUDIO_CH_NEW;
static int error_code = AG_SUCCESS;

static void aglink_audio_app_start(void)
{
	aglink_tx_data(audio_dev_id, AG_VD_RECORD_AUDIO_START,
			NULL, 0);
}

static void aglink_audio_app_success(void)
{
	aglink_tx_data(audio_dev_id, AG_VD_POWER_OFF, NULL, 0);
}

static void aglink_audio_app_fail(void)
{
		struct ag_generic_rsp error = {
		.mode = AG_MODE_RECORD_AUDIO,
		.code = error_code,
	};

	aglink_tx_data(audio_dev_id, AG_VD_GENERIC_RESPONSE,
			(uint8_t *)&error, sizeof(struct ag_generic_rsp));
	aglink_tx_data(audio_dev_id, AG_VD_POWER_OFF, NULL, 0);
}

const char* aglink_audio_app_state_to_string(audio_mode_state state)
{
	switch (state) {
	case AG_AUDIO_CH_CLOSE:
		return "close";
	case AG_AUDIO_CH_NEW:
		return "new";
	case AG_AUDIO_CH_START:
		return "start";
	case AG_AUDIO_CH_RECEIVING:
		return "receiving";
	case AG_AUDIO_CH_END:
		return "end";
	case AG_AUDIO_CH_SUCCESS:
		return "success";
	case AG_AUDIO_CH_FAILED:
		return "failed";
    case AG_AUDIO_CH_IDLE:
		return "idle";
	}
	return "no found";
}

#define STATE_CHANGED(curr_state)                               \
	if (audio_state != curr_state) {                            \
		AGLINK_INFO("audio %s -->%s \n",                        \
				aglink_audio_app_state_to_string(audio_state),  \
				aglink_audio_app_state_to_string(curr_state));  \
		audio_state = curr_state;                               \
	}

static int check_access(const char *path, long timeout_ms)
{
	struct timeval start_time, current_time;
	long elapsed_time_ms = 0;

	gettimeofday(&start_time, NULL);

	while (1) {
		if (access(path, W_OK) == 0)
			return (int)elapsed_time_ms;

		gettimeofday(&current_time, NULL);
		elapsed_time_ms = (current_time.tv_sec - start_time.tv_sec) * 1000L +
						  (current_time.tv_usec - start_time.tv_usec) / 1000L;

		if (elapsed_time_ms >= timeout_ms)
			return -1;

		usleep(10 * 1000);
	}
}

static void* aglink_audio_task(void* data)
{
	while (!audio_app_exit) {
		switch (audio_state) {
			case AG_AUDIO_CH_CLOSE:
				break;
			case AG_AUDIO_CH_NEW:{
				int ret = check_access(TAKE_AUDIO_PATCH, WAIT_VIDEO_PATCH_TIMEOUT_MS);
				if (ret < 0) {
					AGLINK_ERROR("Error: %s can't access\n", TAKE_AUDIO_PATCH);
					error_code = AG_FAILED_WRITE_FILE;
					STATE_CHANGED(AG_AUDIO_CH_FAILED);
					break;
				} else {
					AGLINK_INFO("wait %s wirteable %d ms\n", TAKE_AUDIO_PATCH, ret);
				}
				aglink_audio_app_start();
				STATE_CHANGED(AG_AUDIO_CH_START);
			}break;
			case AG_AUDIO_CH_START:
				break;
			case AG_AUDIO_CH_RECEIVING:
				break;
			case AG_AUDIO_CH_END:
				break;
			case AG_AUDIO_CH_SUCCESS:
				aglink_audio_app_success();
				STATE_CHANGED(AG_AUDIO_CH_IDLE);
				break;
			case AG_AUDIO_CH_FAILED:
				aglink_audio_app_fail();
				STATE_CHANGED(AG_AUDIO_CH_IDLE);
				break;
            case AG_AUDIO_CH_IDLE:
				break;
		}
		usleep(1 * 1000);
	}
	AGLINK_INFO("aglink photo task exit\n");
	pthread_exit(NULL);
}

static int aglink_audio_app_rxcb(uint8_t type, uint8_t *data, int len)
{
	AGLINK_DEBUG("type: %s, len:%d\n", aglink_ad_type_to_string(type), len);
	switch (type) {
	case AG_AD_AUDIO:
			if (!data || len <= 0) break;
		if (audio_state != AG_AUDIO_CH_START) {
			AGLINK_ERROR("AUDIO has no START!,please wait\n");
			break;
		}

		if (take_audio.file == NULL) {
			time_t rawtime;
			struct tm *timeinfo;
			struct timespec ts;
			memset(&rawtime, 0, sizeof(time_t));
			memset(&ts, 0, sizeof(struct timespec));
			time(&rawtime);
			clock_gettime(CLOCK_REALTIME, &ts);
			timeinfo = localtime(&rawtime);
			snprintf(take_audio.strAudioPath, sizeof(take_audio.strAudioPath)-1, "%s%04d%02d%02d%02d%02d%02d%03ld.opus",
						TAKE_AUDIO_PATCH, timeinfo->tm_year + 1900, timeinfo->tm_mon + 1, timeinfo->tm_mday, timeinfo->tm_hour,
						timeinfo->tm_min, timeinfo->tm_sec, (ts.tv_nsec / 1000000));
			snprintf(take_audio.strAudioName, sizeof(take_audio.strAudioName)-1, "%04d%02d%02d%02d%02d%02d%03ld.opus",
						timeinfo->tm_year + 1900, timeinfo->tm_mon + 1, timeinfo->tm_mday, timeinfo->tm_hour, timeinfo->tm_min,
						timeinfo->tm_sec, (ts.tv_nsec / 1000000));
			take_audio.file = fopen(take_audio.strAudioPath, "w");
			if (take_audio.file == NULL) {
				perror("Failed to open temporary file");
				error_code = AG_FAILED_WRITE_FILE;
				STATE_CHANGED(AG_AUDIO_CH_FAILED);
				break;
			}
			AGLINK_INFO("%s\n", take_audio.strAudioPath);
		}

		if (take_audio.file != NULL) {
			int writelen;
			writelen = fwrite(data, 1, len, take_audio.file);
			if (writelen < len) {
				AGLINK_ERROR("write audio fail [%d/%d][%s]\n", writelen, len, strerror(errno));
				if (errno == ENOSPC) {
					fflush(take_audio.file);
					int fd = fileno(take_audio.file);
					if (fsync(fd) == -1) {
						perror("Failed to sync file");
					}
					fclose(take_audio.file);
					take_audio.file = NULL;
					error_code = AG_FAILED_DISK_FULL;
					STATE_CHANGED(AG_AUDIO_CH_FAILED);
				}
			}
		}
		break;
	case AG_AD_OTA_START:
		break;
	case AG_AD_RECORD_AUDIO_STOP:
		if (take_audio.file != NULL) {
			fflush(take_audio.file);
			int fd = fileno(take_audio.file);
			if (fsync(fd) == -1) {
				perror("Failed to sync file");
			}
			fclose(take_audio.file);
			take_audio.file = NULL;
			fd = open(TAKE_AUDIO_TXT_FILE, O_WRONLY | O_APPEND | O_SYNC | O_CREAT, 0644);
			if (fd == -1) {
				perror("Error opening file");
			} else {
				write(fd, take_audio.strAudioName, strlen(take_audio.strAudioName));
				write(fd, "\n", strlen("\n"));
				close(fd);
			}
		} else {
			AGLINK_INFO("file has been close\n");
		}
		STATE_CHANGED(AG_AUDIO_CH_SUCCESS);
		break;
	case AG_AD_DIS_LINE:
	case AG_AD_PAUSE:
	case AG_AD_RESUME:
	default:
		break;
	}
	return 0;
}

void aglink_audio_app_init(int dev_id)
{
	int ret;

	audio_dev_id = dev_id;

	aglink_channel_register_cb(aglink_audio_app_rxcb, audio_dev_id);

	audio_app_exit = 0;
	pthread_create(&ag_audio_thread, NULL, aglink_audio_task, NULL);
}

void aglink_audio_app_deinit(int dev_id)
{
	audio_app_exit = 1;
	pthread_join(ag_audio_thread, NULL);
	aglink_channel_delete(audio_dev_id);
	if (take_audio.file) {
		fflush(take_audio.file);
		if (fsync(fileno(take_audio.file)) < 0) perror("sync interrupted audio");
		fclose(take_audio.file);
		take_audio.file = NULL;
	}
}
