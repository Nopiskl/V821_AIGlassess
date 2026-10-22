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
#include "sys_info.h"
#include <sys/statvfs.h>
#include "aglink/ag_wrap.h"
#include <aglink/ag_proto.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include <aglink_log.h>
#include <unistd.h>
#include <sys/time.h>
#include <ftw.h>

typedef enum sys_info_mode_state {
	AG_GET_SYS_VERSION_INFO = 0,
	AG_GET_CHIP_TEMP_INFO,
	AG_GET_STROGE_CAPACITY_INFO,
	AG_GET_INFO_IDLE,
	AG_DELETE_MEDIA,
	AG_GET_MEDIA_NUM,
} sys_info_mode_state;

int info_dev_id = 0;

static pthread_t ag_sys_info_thread;
static pthread_t ag_send_temp_thread;

volatile int sys_info_app_exit;
volatile int send_temp_task_run = 0;

volatile sys_info_mode_state info_state = AG_GET_INFO_IDLE;

static int error_code = AG_SUCCESS;

#define STATE_CHANGED(curr_state)	 \
	if (info_state != curr_state) {	  \
		info_state = curr_state; \
	}

static uint8_t *rx_data = NULL;
static int data_len = 0;
static int data_type = 0;
#define RX_DATA rx_data
#define DATA_LEN data_len
#define DATA_TYPE data_type

#define GET_RX_DATA(t,d,l) \
do { \
	data_type = (t); \
	data_len = (l); \
	if (((d) != NULL) && ((l) > 0)) { \
		rx_data = malloc((l)); \
		memcpy(rx_data, (d), (l)); \
	} else { \
		AGLINK_INFO("rx data is NULL\n"); \
	} \
} while(0)

#define DEL_RX_DATA() \
do { \
	data_type = 0; \
	data_len = 0; \
	if ((rx_data) != NULL) { \
		free(rx_data); \
		rx_data = NULL; \
	} \
} while(0)

static void aglink_send_media_num(uint32_t _image_num, uint32_t _audio_num, uint32_t _video_num)
{
	struct ag_photo_count media_num = {
		.jpg_count = _image_num,
		.mp4_count = _video_num,
		.audio_count = _audio_num,
	};

	aglink_tx_data(info_dev_id, AG_VD_MEDIA_NUM,
			(uint8_t *)&media_num, sizeof(struct ag_photo_count));
}

static void aglink_send_generic_response(void)
{
	struct ag_generic_rsp error = {
		.mode = AG_MODE_NORMAL,
		.code = error_code,
	};

	aglink_tx_data(info_dev_id, AG_VD_GENERIC_RESPONSE,
			(uint8_t *)&error, sizeof(struct ag_generic_rsp));
}

static int get_boot_medium(void)
{
	FILE *fp;
	char *token;
	char line[256];
	char *root_value = NULL;

	fp = fopen("/proc/cmdline", "r");
    if (fp == NULL) {
        perror("Failed to open /proc/cmdline");
        return -1;
    }

    if (fgets(line, 256, fp) == NULL) {
        perror("Failed to read /proc/cmdline");
        fclose(fp);
        return -1;
    }
    fclose(fp);
	AGLINK_INFO("line:%s\n", line);
	if (strstr(line, "mtdblock") != NULL) {
		AGLINK_INFO("boot media nor\n");
		return AG_BOOT_NOR;
	} else if (strstr(line, "mmcblk") != 0) {
		AGLINK_INFO("boot media sdnand\n");
		return AG_BOOT_SDNAND;
	} else {
		AGLINK_ERROR("boot media unknow\n");
		return AG_BOOT_UNKNOW;
	}

	return AG_BOOT_UNKNOW;
}

static int get_partition_num(struct ag_sys_version *sys)
{
	FILE *fp;
	char buffer[128];

	if (!sys) {
		AGLINK_ERROR("Parameter error\n");
		return -1;
	}

	fp = popen("ls /dev/mtdblock* 2>/dev/null | wc -l", "r");
	if (fp == NULL) {
		AGLINK_ERROR("Failed to get nor flash partition num\n");
		return -1;
	}

	if (fgets(buffer, sizeof(buffer), fp) != NULL) {
		sys->nor_partition_num = (uint8_t)atoi(buffer);
	}

	pclose(fp);
	fp = NULL;

	fp = popen("ls /dev/mmcblk0p* 2>/dev/null | wc -l", "r");
	if (fp == NULL) {
		AGLINK_ERROR("Failed to get sdnand partition num\n");
		return -1;
	}

	if (fgets(buffer, sizeof(buffer), fp) != NULL) {
		sys->sdnand_partition_num = (uint8_t)atoi(buffer);
	}

	pclose(fp);
	fp = NULL;

	return 0;
}


static void aglink_send_version(void)
{
	struct ag_sys_version sys_persion = {
		.nor_partition_num = 0,
		.sdnand_partition_num = 0,
	};

	strncpy(sys_persion.version, AGLINK_VERSION, sizeof(sys_persion.version));
	sys_persion.bootmedium = get_boot_medium();
	get_partition_num(&sys_persion);

	aglink_tx_data(info_dev_id, AG_VD_SYS_VERSION,
			(uint8_t *)&sys_persion, sizeof(struct ag_sys_version));
}

static void aglink_send_capacity(void)
{
	struct statvfs stat;
	struct ag_stroge_capacity sys_capac;

	if (statvfs(CAPACITY_INFO_PATH, &stat) != 0) {
		perror("statvfs");
		return;
	}

	uint32_t total_space = (uint32_t)stat.f_frsize * stat.f_blocks;
	uint32_t free_space = (uint32_t)stat.f_frsize * stat.f_bavail;

	sys_capac.used = total_space - free_space;
	sys_capac.free = free_space;

	aglink_tx_data(info_dev_id, AG_VD_STROGE_CAPACITY,
			(uint8_t *)&sys_capac, sizeof(struct ag_stroge_capacity));
}

uint8_t aglink_get_temp(void)
{
	uint8_t temp = 0;
	int fd;
	char buffer[10];
	ssize_t bytes_read;
	int temperature_milli_celsius;

	fd = open(CPU_TEMP_INFO, O_RDONLY);
	if (fd == -1) {
		perror("Failed to open temperature file");
	} else {
		bytes_read = read(fd, buffer, 10 - 1);
		if (bytes_read == -1) {
			perror("Failed to read temperature file");
		}
		close(fd);
	}

	buffer[bytes_read] = '\0';

	temperature_milli_celsius = atoi(buffer);

	temp = (int) (temperature_milli_celsius / 1000);
	AGLINK_INFO("temp: %d\n", temp);
	return temp;
}

static void aglink_send_temp(int temp)
{
	struct ag_chip_temp chip_temp = {
		.temp = temp,
	};
	aglink_tx_data(info_dev_id, AG_VD_CHIP_TEMP,
			(uint8_t *)&chip_temp, sizeof(struct ag_chip_temp));
}

static void* aglink_send_temp_task(void* data)
{
	uint8_t temp = 0;

	while (!sys_info_app_exit) {
		temp = aglink_get_temp();
		aglink_send_temp(temp);

		sleep(3);
	}
	AGLINK_INFO("send temp task exit!!!\n");
	pthread_exit(NULL);
}

/* MEDIA DELETE */
static int is_image_audio_or_video(const char *filename)
{
    const char *image_extensions[] = {".jpg", ".jpeg", ".png", ".gif", ".bmp"};
    const char *video_extensions[] = {".mp4", ".avi", ".mkv", ".mov", ".flv"};
    const char *audio_extensions[] = {".opus", ".mp3"};

    const char *dot = strrchr(filename, '.');
    if (dot == NULL) {
		if (!strncmp("video-", filename, sizeof("video-") - 1))
            return VIDEO;
		return 0;
	}

    for (int i = 0; i < sizeof(image_extensions) / sizeof(image_extensions[0]); i++) {
        if (strcasecmp(dot, image_extensions[i]) == 0) {
            return IMAGE;
        }
    }

    for (int i = 0; i < sizeof(video_extensions) / sizeof(video_extensions[0]); i++) {
        if (strcasecmp(dot, video_extensions[i]) == 0) {
            return VIDEO;
        }
    }

    for (int i = 0; i < sizeof(audio_extensions) / sizeof(audio_extensions[0]); i++) {
        if (strcasecmp(dot, audio_extensions[i]) == 0) {
            return AUDIO;
        }
    }

    return 0;
}

static int aglink_delete_dir_cb(const char *path, const struct stat *sb, int type, struct FTW *ftwb)
{
    if (type == FTW_DP) {
        if (rmdir(path) == -1) {
            perror("rmdir");
            return -1;
        }
    } else if (type == FTW_F || type == FTW_SL) {
        if (remove(path) == -1) {
            perror("remove");
            return -1;
        }
    }
    return 0;
}

static int aglink_delete_dir(const char *path)
{
	return nftw(path, aglink_delete_dir_cb, 64, FTW_DEPTH | FTW_PHYS);
}

static int aglink_delete_files_in_directory(const char *path)
{
    DIR *dir = opendir(path);
    if (dir == NULL) {
        perror("Failed to open directory");
        return -1;
    }

    struct dirent *entry;
    char fullpath[1024];

    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }

        snprintf(fullpath, sizeof(fullpath), "%s/%s", path, entry->d_name);

        struct stat statbuf;
        if (stat(fullpath, &statbuf) == -1) {
            perror("Failed to stat file");
            continue;
        }

        if (is_image_audio_or_video(entry->d_name)) {
			if (S_ISREG(statbuf.st_mode)) {
				if (remove(fullpath))
            	    perror("Failed to delete file");
			} else {
				if (aglink_delete_dir(fullpath))
					perror("Failed to delete dir");
			}
            AGLINK_INFO("Deleted: %s\n", fullpath);
		}
    }

    closedir(dir);
    sync();

    return 0;
}

static int aglink_delete_line_from_file(const char *_filepath, char *_str_to_delete)
{
    FILE *file, *temp_file;
    char *str_to_delete = _str_to_delete;
    char line[1024] = {0};
    char temp_filepath[256] = {0};
    int ret = 0;

    /* open source file */
    const char *filepath = _filepath;
    file = fopen(filepath, "r");
    if (file == NULL) {
        perror("Failed to open file");
        AGLINK_INFO("file path: %s\n", filepath);
        ret = -1;
        goto exit;
    }

    /* open a temp file */
    char randnumber_str[24] = {0};
    unsigned int seed = (unsigned int)time(NULL) + (unsigned int)pthread_self();
    int random_number = rand_r(&seed);
    sprintf(randnumber_str, "%d", random_number);
    sprintf(temp_filepath, "%s/%s_%s", CAPACITY_INFO_PATH, randnumber_str, "temp.txt");
    temp_file = fopen(temp_filepath, "w");
    if (temp_file == NULL) {
        perror("Failed to open temporary file");
        AGLINK_INFO("file path: %s\n", temp_filepath);
        fclose(file);
        ret = -1;
        goto exit;
    }
    AGLINK_INFO("open temp file: %s\n", temp_filepath);

    while (fgets(line, sizeof(line), file)) {
        /* if current line do not contains specified string, writing the line to temp file */
        if (strstr(line, str_to_delete) == NULL) {
            fputs(line, temp_file);
			AGLINK_INFO("write str(%s) to %s\n", str_to_delete, temp_filepath);
        }
    }

    /* close file */
    fclose(file);
    fclose(temp_file);

    /* remove source file and rename temp file */
    if (remove(filepath) == 0) {
        if (rename(temp_filepath, filepath) == 0) {
            AGLINK_INFO("String removed successfully.\n");
        } else {
            perror("Error renaming temporary file");
            AGLINK_INFO("file path: %s\n", temp_filepath);
        }
    } else {
        perror("Error removing original file");
    }
    file = fopen(filepath, "r");
    if (file == NULL) {
        perror("Failed to open file");
        AGLINK_INFO("file path: %s\n", filepath);
    } else {
        fflush(file);
        int fd = fileno(file);
        if (fsync(fd) == -1) {
            perror("Failed to sync file");
        }
        fclose(file);
    }
    AGLINK_INFO("delete string(%s) of %s successfully.\n", str_to_delete, filepath);

exit:

    return ret;
}

#define WAIT_TIMEOUT_MS 8000

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


static int aglink_process_del_media(uint8_t *_data, int len)
{

	uint8_t *data = (uint8_t *)malloc(len);
	if (data == NULL) {
		perror("malloc del_media_data failed.\n");
		goto error_exit;
	}
	memset(data, 0, len);
	memcpy(data, _data, len);

	struct ag_del_media *del_media_data = (struct ag_del_media *)data;
	uint8_t del_ploy = del_media_data->type;
	char *file_name = del_media_data->name;

	if (del_ploy == DM_SINGLE) {
		char filepath[1024] = {0};
		const char *filename = file_name;
		sprintf(filepath, "%s/%s", CAPACITY_INFO_PATH, filename);

		AGLINK_INFO("remove file: %s\n", filepath);
        struct stat statbuf;
        if (stat(filepath, &statbuf) == -1) {
            perror("Failed to stat file");
			goto error_exit;
        }
        if (S_ISREG(statbuf.st_mode)) {
			if (remove(filepath) != 0) {
				AGLINK_INFO("remove file: %s failed.\n", filepath);
				perror("remove file failed.");
				goto error_exit;
			}
		} else if (S_ISDIR(statbuf.st_mode)) {
			if (aglink_delete_dir(filepath)) {
				AGLINK_INFO("remove dir: %s failed.\n", filepath);
				perror("failed to delete dir");
				goto error_exit;
			}
		} else {
			perror("Failed to delete dir");
			goto error_exit;
		}
        char *str_to_delete = file_name;
        aglink_delete_line_from_file(MEDIA_CONFIG_PATH, str_to_delete);
	} else if (del_ploy == DM_ALL) {
		/* del all picture and vedio file */
		AGLINK_INFO("delete file in dir: %s\n", CAPACITY_INFO_PATH);
		if(aglink_delete_files_in_directory(CAPACITY_INFO_PATH) != 0) {
			AGLINK_INFO("del dir: %s failed.\n", CAPACITY_INFO_PATH);
			perror("del dir failed.");
			goto error_exit;
		}
		if (access(MEDIA_CONFIG_PATH, F_OK) == 0) {
			if (remove(MEDIA_CONFIG_PATH) != 0) {
				AGLINK_INFO("remove file: %s failed.\n", MEDIA_CONFIG_PATH);
				perror("remove file failed.");
				goto error_exit;
			}
		} else {
			AGLINK_INFO("delete %s can not access\n", MEDIA_CONFIG_PATH);
			goto error_exit;
		}
	}
	sync();
	free(data);
	return 0;

error_exit:
	free(data);
	return -1;
}
/* MEDIA DELETE */

/* MDEIA STAT */
static int filelist_check(const char *file_list_path)
{
	/*This note needs to be improved to consider 
	 * that the temp file cannot be created when 
	 * there is not enough disk space 
	 * */
#if 0
	FILE *input_file = fopen(file_list_path, "r");
	if (input_file == NULL) {
		perror("open file_list failed.");
		return -1;
	}

	FILE *temp_file = fopen((CAPACITY_INFO_PATH"/temp.txt"), "w");
	if (temp_file == NULL) {
		perror("open file_list failed.");
		fclose(input_file);
		return -1;
	}

	char file_name[128] = {0};
	char file_path[256] = {0};
	while (fgets(file_name, sizeof(file_name), input_file)) {
		file_name[strcspn(file_name, "\r")] = 0;
		file_name[strcspn(file_name, "\n")] = 0;
		sprintf(file_path, "%s/%s", CAPACITY_INFO_PATH, file_name);

		if (access(file_path, F_OK) == 0) {
			fprintf(temp_file, "%s\n", file_name);
			//AGLINK_INFO("File exists: %s\n", file_name);
		} else {
			AGLINK_INFO("|File does not exists: %s|\n", file_path);
		}
	}

	fclose(input_file);
	fclose(temp_file);

	if (remove(file_list_path) != 0) {
		perror("remove failed.");
		return -1;
	}

	if (rename((CAPACITY_INFO_PATH"/temp.txt"), file_list_path) != 0) {
		perror("rename failed.");
		return -1;
	}

	sync();

#endif
	return 0;
}

static int aglink_stat_files_in_filelist(const char *path, media_stat_t *media_stat)
{
	uint32_t image_num = 0, audio_num = 0, video_num = 0;

	filelist_check(path);

	FILE *input_file = fopen(path, "r");
	if (input_file == NULL) {
		AGLINK_INFO("open (%s) file failed.\n", path);
		perror("open file_list failed.");
		return -1;
	}

	char file_name[128] = {0};
	while (fgets(file_name, sizeof(file_name), input_file)) {
		file_name[strcspn(file_name, "\r")] = 0;
		file_name[strcspn(file_name, "\n")] = 0;

		uint8_t media_type = is_image_audio_or_video(file_name);
		switch (media_type) {
			case IMAGE: image_num ++; break;
			case AUDIO: audio_num ++; break;
			case VIDEO: video_num ++; break;
			default: break;
		}
	}

	media_stat->image_num = image_num;
	media_stat->audio_num = audio_num;
	media_stat->video_num = video_num;

	AGLINK_INFO("image num: %d audio num: %d video num: %d \n", image_num, audio_num, video_num);

	fclose(input_file);

	return 0;
}

static int aglink_stat_files_in_directory(const char *path, media_stat_t *media_stat)
{
    DIR *dir = opendir(path);
    if (dir == NULL) {
		AGLINK_INFO("open (%s) file failed.\n", path);
        perror("Failed to open directory");
        return -1;
    }

    struct dirent *entry;
    char fullpath[1024];

	uint32_t image_num = 0, audio_num = 0, video_num = 0;
    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }

        snprintf(fullpath, sizeof(fullpath), "%s/%s", path, entry->d_name);

        struct stat statbuf;
        if (stat(fullpath, &statbuf) == -1) {
            perror("Failed to stat file");
            continue;
        }

        if (S_ISREG(statbuf.st_mode)) {
			uint8_t media_type = is_image_audio_or_video(entry->d_name);
			switch (media_type) {
				case IMAGE: image_num ++; break;
				case AUDIO: audio_num ++; break;
				case VIDEO: video_num ++; break;
				default: break;
			}
        }
    }
	media_stat->image_num = image_num;
	media_stat->audio_num = audio_num;
	media_stat->video_num = video_num;

	AGLINK_INFO("image num: %d audio num: %d video num: %d \n", image_num, audio_num, video_num);

	closedir(dir);

    return 0;
}

static int aglink_process_get_media_num(uint8_t *_data, int len, media_stat_t *media_stat)
{
	(void)_data;
	(void)len;

	int stat_method = 0;

	switch (stat_method) {
		case 0: {
			if (aglink_stat_files_in_filelist(MEDIA_CONFIG_PATH, media_stat)) {
				AGLINK_INFO("stat media num failed.\n");
				return -1;
			}
		} break;
		case 1: {
			if (aglink_stat_files_in_directory(CAPACITY_INFO_PATH, media_stat)) {
				AGLINK_INFO("stat media num failed.\n");
				return -1;
			}
		} break;
	}

	return 0;
}

int aglink_info_app_rxcb(uint8_t type, uint8_t *data, int len)
{
	AGLINK_INFO("type: %s, len:%d\n", aglink_ad_type_to_string(type), len);
	switch (type) {
	case AG_AD_GET_SYS_VERSION:
		STATE_CHANGED(AG_GET_SYS_VERSION_INFO);
		break;
	case AG_AD_GET_CHIP_TEMP:
		STATE_CHANGED(AG_GET_CHIP_TEMP_INFO);
		break;
	case AG_AD_GET_STROGE_CAPACITY:
		STATE_CHANGED(AG_GET_STROGE_CAPACITY_INFO);
		break;
	case AG_AD_DELETE_MEDIA: {
			GET_RX_DATA(type, data, len);
			STATE_CHANGED(AG_DELETE_MEDIA);
		} break;
	case AG_AD_GET_MEDIA_NUM: {
			GET_RX_DATA(type, data, len);
			STATE_CHANGED(AG_GET_MEDIA_NUM);
		} break;
	default:
		break;
	}
	return 0;
}

static void* aglink_info_task(void* data)
{
	while (!sys_info_app_exit) {
		switch (info_state) {
		case AG_GET_SYS_VERSION_INFO:
			aglink_send_version();
			STATE_CHANGED(AG_GET_INFO_IDLE);
			break;
		case AG_GET_CHIP_TEMP_INFO:
			if (send_temp_task_run) {
				STATE_CHANGED(AG_GET_INFO_IDLE);
				break;
			} else if (pthread_create(&ag_send_temp_thread, NULL, aglink_send_temp_task, NULL) == 0) {
				send_temp_task_run = 1;
			}
			STATE_CHANGED(AG_GET_INFO_IDLE);
			break;
		case AG_GET_STROGE_CAPACITY_INFO:
			aglink_send_capacity();
			STATE_CHANGED(AG_GET_INFO_IDLE);
			break;
		case AG_DELETE_MEDIA: {
				int ret;
				ret = check_access(CAPACITY_INFO_PATH, WAIT_TIMEOUT_MS);
				if (ret < 0) {
					AGLINK_ERROR("Error: access %s failed\n", CAPACITY_INFO_PATH);
					error_code = AG_FAILED_WRITE_FILE;
				} else {
					AGLINK_INFO("wait %s wirteable %d ms\n", CAPACITY_INFO_PATH, ret);
					ret = aglink_process_del_media(RX_DATA, DATA_LEN);
					DEL_RX_DATA();
					if (ret) {
						error_code = AG_FAILED_PROCESS;
					} else {
						error_code = AG_SUCCESS;
					}
				}
				aglink_send_generic_response();
				STATE_CHANGED(AG_GET_INFO_IDLE);
			} break;
		case AG_GET_MEDIA_NUM: {
				int ret;
				media_stat_t media_stat = {0};
				ret = check_access(CAPACITY_INFO_PATH, WAIT_TIMEOUT_MS);
				if (ret < 0) {
					AGLINK_ERROR("Error: access %s failed\n", CAPACITY_INFO_PATH);
					error_code = AG_FAILED_WRITE_FILE;
				} else {
					AGLINK_INFO("wait %s wirteable %d ms\n", CAPACITY_INFO_PATH, ret);
					ret = aglink_process_get_media_num(RX_DATA, DATA_LEN, &media_stat);
					DEL_RX_DATA();
					if (ret) {
						error_code = AG_FAILED_PROCESS;
					} else {
						error_code = AG_SUCCESS;
					}
				}
				aglink_send_media_num(media_stat.image_num, \
						media_stat.audio_num, media_stat.video_num);
				STATE_CHANGED(AG_GET_INFO_IDLE);
			} break;
		case AG_GET_INFO_IDLE:
			break;
		default:
			break;
		}
		usleep(50 * 1000);
	}
	AGLINK_INFO("aglink take info task exit\n");
	pthread_exit(NULL);
}

void aglink_info_app_init(int dev_id)
{
	int ret;
	info_dev_id = dev_id;

	AGLINK_INFO("AGLINK_VERSION:%s\n", AGLINK_VERSION);
	aglink_sys_info_register_cb(aglink_info_app_rxcb);
	pthread_create(&ag_sys_info_thread, NULL, aglink_info_task, NULL);
	sys_info_app_exit = 0;
}

void aglink_info_app_deinit(int dev_id)
{
	sys_info_app_exit = 1;
	pthread_join(ag_sys_info_thread, NULL);
	pthread_join(ag_send_temp_thread, NULL);
}
