#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/types.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <ctype.h>
#include <signal.h>
#include <errno.h>
#include <fcntl.h>
#include <aglink_log.h>
#include <time.h>
#include <libgen.h>
#include <sys/stat.h>

//to do 0: after transform end, 821 should recv a downover flag from app. now the downover is not real downover state.
//to do 1: resource path is configurable. now it is not configurable.
//to do 2: to test send big file by sendfile api.

#include "file_ser.h"

static uint8_t task_state = TASK_INIT;

static pthread_mutex_t file_lock;
static pthread_mutex_t bitmap_lock;

static uint8_t file_server_exit = 0;
static uint8_t process_exit = 0;
static uint8_t do_http_open = 0;

static uint32_t sent_files;
static uint32_t total_files;

/* is text file */
static int is_text(FILE *file)
{
    unsigned char c;
    while (fread(&c, 1, 1, file) == 1) {
        /* if th char do not print, think it is obinary file */
        if (!isprint(c) && !isspace(c)) {
            return 0;  /* is not text file */
        }
    }
    return 1;  /* is text file */
}

/* is png file */
static int is_png(FILE *file)
{
    unsigned char header[4];
    fread(header, 1, 4, file);
    return (header[0] == 0x89 && header[1] == 0x50 && header[2] == 0x4E && header[3] == 0x47);
}

/* is jpeg file */
static int is_jpeg(FILE *file)
{
    unsigned char header[3];
    fread(header, 1, 3, file);
    return (header[0] == 0xFF && header[1] == 0xD8 && header[2] == 0xFF);
}

/* is gif file */
static int is_gif(FILE *file)
{
    unsigned char header[4];
    fread(header, 1, 4, file);
    return (header[0] == 0x47 && header[1] == 0x49 && header[2] == 0x46 && header[3] == 0x38);
}

static int is_mp4(FILE *file)
{
    /* read 4 bytes of file's star. magic of mp4 file is "ftyp" */
    char buffer[4];
    size_t bytesRead = fread(buffer, 1, 4, file);

    if (bytesRead < 4) {
        return 0;  /* if file's content < 4 bytes, it meaning the file is not mp4 file */
    }

    /* checking the start of files is "ftyp" */
    if (buffer[0] == 'f' && buffer[1] == 't' && buffer[2] == 'y' && buffer[3] == 'p') {
        return 1;  /* is mp4 file */
    }

    return 0;  /* is not mp4 file */
}

#if CONFIG_FILELIST_CHECK
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
#endif

uint64_t download_bitmap;
#define SET_DOWNLOADING() \
({ \
	int8_t task_id = -1; \
	uint64_t temp_bitmap = download_bitmap; \
	for (int i = 0; i < sizeof(download_bitmap); i ++) { \
		temp_bitmap = download_bitmap; \
		temp_bitmap &= 1 << i; \
		if (temp_bitmap == 0) { \
			task_id = i; \
			download_bitmap |= 1 << task_id; \
			break; \
		} \
	} \
	task_id; \
})

#define CLEAR_DOWNLOADING(BI) \
do { \
	if ((BI) < 0) break; \
	BI=(BI)>63?0:(BI); \
	download_bitmap &= ~(1 << (BI)); \
} while(0)

static int filelist_check(const char *filelist_path)
{
	/*This note needs to be improved to consider 
	 * that the temp file cannot be created when 
	 * there is not enough disk space 
	 * */
#if 0
	char file_name[CONFIG_FILE_NAME_LEN] = {0};
	char file_path[CONFIG_FILE_PATH_LEN] = {0};
	char tempfile_path[CONFIG_FILE_PATH_LEN] = {0};
	char *dir = CONFIG_DEFAULT_FILE_DIR;

	FILE *filelist = fopen(filelist_path, "r");
	if (filelist == NULL) {
		perror("open file_list failed.");
		return -1;
	}

	sprintf(tempfile_path, "%s/%s", dir, "temp.txt");
	FILE *temp_file = fopen(tempfile_path, "w");
	if (temp_file == NULL) {
		perror("open file failed.");
		fclose(filelist);
		return -1;
	}

	while (fgets(file_name, sizeof(file_name), filelist)) {
		file_name[strcspn(file_name, "\r")] = 0;
		file_name[strcspn(file_name, "\n")] = 0;
		sprintf(file_path, "%s/%s", dir, file_name);

		if (access(file_path, F_OK) == 0) {
			fprintf(temp_file, "%s\n", file_name);
			//AGLINK_INFO("File exists: %s\n", file_name);
		} else {
			AGLINK_INFO("|File does not exists: %s|\n", file_path);
		}
	}

	fclose(filelist);
	fclose(temp_file);

	if (remove(filelist_path) != 0) {
		perror("remove failed.");
		return -1;
	}

	if (rename(tempfile_path, filelist_path) != 0) {
		perror("rename failed.");
		return -1;
	}

#endif
	return 0;
}

static int filelist_flush(const char *filelist_path)
{
    FILE *file = fopen(filelist_path, "w");
    if (file == NULL) {
        AGLINK_INFO("file path: %s\n", filelist_path);
        perror("Failed to open file");
        return -1;
    }

	fclose(file);

	return 0;
}

/* delete line that contains the specified string in the file */
static int filelist_delete(const char *filelist_path, char *str_to_delete)
{
    FILE *file, *temp_file;
    char tempfile_path[CONFIG_FILE_PATH_LEN] = {0};
    char *dir = CONFIG_DEFAULT_FILE_DIR;

    /* open source file */
    file = fopen(filelist_path, "r");
    if (file == NULL) {
        perror("Failed to open file");
        AGLINK_INFO("file path: %s\n", filelist_path);
		return -1;
    }

    /* open a temp file */
    char randnumber_str[24] = {0};
    unsigned int seed = (unsigned int)time(NULL) + (unsigned int)pthread_self();
    int random_number = rand_r(&seed);

    sprintf(randnumber_str, "%d", random_number);

    sprintf(tempfile_path, "%s/%s_%s", dir, randnumber_str, "temp.txt");
    temp_file = fopen(tempfile_path, "w");
    if (temp_file == NULL) {
        perror("Failed to open file");
        AGLINK_INFO("file path: %s\n", tempfile_path);
        fclose(file);
		return -1;
    }
    AGLINK_INFO("open temp file: %s\n", tempfile_path);

    char line[CONFIG_FILE_NAME_LEN] = {0};
    while (fgets(line, sizeof(line), file)) {
        /* if current line do not contains specified string, writing the line to temp file */
        if (strstr(line, str_to_delete) == NULL) {
            fputs(line, temp_file);
			//AGLINK_INFO("write str(%s) to %s\n", line, tempfile_path);
        }
    }

    /* close file */
    fclose(file);
    fclose(temp_file);

    /* remove source file and rename temp file */
    if (remove(filelist_path) == 0) {
        if (rename(tempfile_path, filelist_path) == 0) {
            AGLINK_INFO("String removed successfully.\n");
        } else {
            perror("Error renaming temporary file");
            AGLINK_INFO("file path: %s\n", tempfile_path);
        }
    } else {
        perror("Error removing original file");
    }

	/* sync */
    file = fopen(filelist_path, "r");
    if (file == NULL) {
        perror("Failed to open file");
        AGLINK_INFO("file path: %s\n", filelist_path);
    } else {
        fflush(file);
        int fd = fileno(file);
        if (fsync(fd) == -1) {
            perror("Failed to sync file");
        }
        fclose(file);
    }

    AGLINK_INFO("delete string(%s) of %s successfully.\n", str_to_delete, filelist_path);

	return 0;
}

static int filelist_count(const char *filelist_path)
{
    int valid_line_count = 0;

    const char *keystr[] = {".jpg", ".JPG", ".png", ".PNG", ".mp4", ".MP4", ".opus", ".OPUS"};

    FILE *file = fopen(filelist_path, "r");
    if (file == NULL) {
        perror("Failed to open file");
        return 0;
    }

    /* read file by line */
    char line[CONFIG_FILE_NAME_LEN];
    while (fgets(line, sizeof(line), file)) {
        for (int i = 0; i < sizeof(keystr) / sizeof(keystr[0]); i ++) {
            if (strstr(line, keystr[i]) != NULL) {
                valid_line_count++;
                break;
            }
        }
    }

    AGLINK_INFO("Number of valid lines: %d\n", valid_line_count);

    fclose(file);

    return valid_line_count;
}

static int fileser_read_file_info(const char *file_path, long *file_size, int *file_type)
{
    FILE *file = fopen(file_path, "rb");
    if (!file) {
        perror("Unable to open file");
        AGLINK_INFO("file path: %s\n", file_path);
        return -1;
    }

    if (is_jpeg(file)) {
        *file_type = JPG_FILE;
    } else if (is_mp4(file)) {
        *file_type = MP4_FILE;
    } else {
        *file_type = TEXT_FILE;
    }

    fseek(file, 0, SEEK_END);
    *file_size = ftell(file);
    fseek(file, 0, SEEK_SET);

    const char *file_str[MAX_TYPE] = { "JPG", "TEXT", "MP4" };
    AGLINK_INFO("file path: %s file path_len: %d file size: %ld file type: %s\n",
			file_path, strlen(file_path), *file_size, *file_type < MAX_TYPE? file_str[*file_type] : "UNKOWN");

    return 0;
}

static int fileser_send_file(void *_session, const char *file_path, long file_size)
{
	http_session_t *session = (http_session_t *)_session;

	struct timespec start, end;
	long start_ms = 0, end_ms = 0, cost_ms = 0;

	clock_gettime(CLOCK_MONOTONIC, &start);

#if CONFIG_SEND_FILE_WITH_ZERO_COPY
	{
		int file_fd = open(file_path, O_RDONLY);
		if (file_fd == -1) {
			perror("Unable to open file");
			AGLINK_INFO("file path: %s\n", file_path);
			return -1;
		}

		int bytes_sent = http_sendfile(session, &file_fd, file_size, -1);

		if (bytes_sent < 0 || bytes_sent != file_size) {
			AGLINK_ERROR("send file failed.\n");
			close(file_fd);
			return -2;
		}

		close(file_fd);
	}
#else /* CONFIG_SEND_FILE_WITH_ZERO_COPY */
	{
		FILE *file = fopen(file_path, "rb");
		if (!file) {
			perror("Unable to open file");
			AGLINK_INFO("file path: %s\n", file_path);
			return -1;
		}
		/* save file's content */
		char *file_content = (char *)malloc(file_size + 1);
		if (!file_content) {
			perror("Unable to allocate memory for file content");
			fclose(file);
			return -3;
		}

		fread(file_content, 1, file_size, file);
		file_content[file_size] = '\0';  /* add end char of string */
#if CONFIG_DEBUG_DUMP_FILE_CONTENT
		for (int i = 0; i * 16 < file_size; i ++) {
			for (int j = 0; j < 16; j ++)
				AGLINK_INFO("%2x ", file_content[(i * 16) + j]);
			AGLINK_INFO("\n");
		}
#endif

		char *data = file_content;
		int bytes_sent = 0, total_bytes_sent = 0, offset = 0;
		while (total_bytes_sent < file_size) {
			int data_size = file_size - total_bytes_sent;

			bytes_sent = http_write(session, data + offset, data_size, 15000);

			if (bytes_sent < 0) {
				free(file_content);
				fclose(file);
				return -2;
			}

			total_bytes_sent += bytes_sent;
			offset += bytes_sent;
		}

		free(file_content);
		fclose(file);
	}
#endif /* CONFIG_SEND_FILE_WITH_ZERO_COPY */

	clock_gettime(CLOCK_MONOTONIC, &end);
	start_ms = start.tv_sec * 1000 + start.tv_nsec / 1000000;
	end_ms = end.tv_sec * 1000 + end.tv_nsec / 1000000;
	cost_ms = end_ms - start_ms;
	cost_ms = cost_ms > 1? cost_ms : 1;

	AGLINK_INFO("(%ld) %ldms.  %fKbytes/s\n", file_size, cost_ms, (float)file_size/cost_ms);
	AGLINK_INFO("file(%s) sent successfully.\n", file_path);

    return 0;
}

static int fileser_set_http_header(char *response_header, response_info_t *response_info)
{
	if (response_info == NULL || response_header == NULL)
		return -1;

	int status_code = response_info->status_code;
	char *status_code_str = NULL;
	switch (status_code) {
		case 200: status_code_str = "200 OK"; break;
		case 400: status_code_str = "400 Bad Request"; break;
		case 404: status_code_str = "404 Not Found"; break;
		default: status_code_str = "200 OK"; break;
	}

	int content_type = response_info->content_type;
	char *content_type_str = NULL;
    switch (content_type) {
        case JPG_FILE: content_type_str = "image/jpeg"; break;
        case TEXT_FILE: content_type_str = "text/plain"; break;
        case MP4_FILE: content_type_str = "video/mp4"; break;
        default: content_type_str = "text/plain"; break;
    }

	long content_length = response_info->content_length;

    snprintf(response_header, CONFIG_HTTP_RESPONSE_HEADER_LEN,
             "HTTP/1.1 %s\r\n"
             "Content-Type: %s\r\n"
             "Content-Length: %ld\r\n"
             "Connection: close\r\n"
             "Access-Control-Allow-Origin: *\r\n"
             "\r\n", \
             status_code_str, content_type_str, content_length);

	return 0;
}

static int check_path(const char *path)
{
    struct stat path_stat;

    if (stat(path, &path_stat) == 0) {
        if (S_ISDIR(path_stat.st_mode)) {
			return IS_DIR;
        } else if (S_ISREG(path_stat.st_mode)) {
			return IS_FILES;
        } else {
			return IS_OTHER;
        }
    } else {
        perror("is not found");
		return IS_NOACCESS;
    }
}

static int fileser_send_one_file(char *file_path, void *session)
{
	long file_size = 0; int file_type = 0;
	int ret = fileser_read_file_info(file_path, &file_size, &file_type);
	if (ret == -1) {
		return ret;
	}

	response_info_t response_info = {
		.status_code = 200,
		.content_type = file_type,
		.content_length = file_size,
	};
	char response_header[CONFIG_HTTP_RESPONSE_HEADER_LEN] = {0};
	fileser_set_http_header(response_header, &response_info);

	http_write(session, response_header, strlen(response_header), 5000);

	ret = fileser_send_file(session, file_path, file_size);
	return ret;
}

typedef struct dir_node {
    char name[64];
    char path[192];
	long file_size;
    struct dir_node *next;
} dir_node;

static int fileser_send_dir(const char *file_path, void *session)
{
	int ret = 0;
	int file_cnt = 0;
	long file_size = 0;
	char *file_gpt = NULL;
	dir_node *head = NULL;
	dir_node *cur = NULL;
	uint32_t file_gpt_size = 0;
	int file_gpt_offset = 0;
	char file_tmp[32 + 1] = {0};

	struct dirent **entries;
	int count = scandir(file_path, &entries, NULL, versionsort);
	if (count < 0) {
	    perror("scandir failed");
	    return -1;
	}

	for (int i = 0; i < count; i++) {
        if (strcmp(entries[i]->d_name, ".") == 0 || strcmp(entries[i]->d_name, "..") == 0) {
			free(entries[i]);
            continue;
		}

		if (ret) {
			free(entries[i]);
			continue;
		}

		dir_node *new = (dir_node*)malloc(sizeof(dir_node));
		if (!new) {
			AGLINK_ERROR("alloc node for %s failed\n", entries[i]->d_name);
			free(entries[i]);
			ret = -1;
            continue;
		}
		new->next = NULL;
		memcpy(new->name, entries[i]->d_name, strlen(entries[i]->d_name));
        snprintf(new->path, sizeof(new->path), "%s/%s", file_path, new->name);
		free(entries[i]);

		FILE *file = fopen(new->path, "rb");
		fseek(file, 0, SEEK_END);
    	new->file_size = ftell(file);
    	fseek(file, 0, SEEK_SET);
		file_size += new->file_size;
		if (head == NULL) {
			head = new;
		} else {
			cur = head;
			while (cur->next != NULL) {
				cur = cur->next;
			}
			cur->next = new;
		}
		file_cnt++;
    }

	free(entries);
	if (ret == 0) {
		file_gpt_size = file_cnt * DIR_GPT_ENTRY_LEN + DIR_GPT_HEAD_LEN;
		file_size += file_gpt_size;
		file_gpt_size += 1;
		file_gpt = (char *)malloc(file_gpt_size);
		file_cnt = 0;
		AGLINK_INFO("send dir all file size:%ld\n", file_size);

		response_info_t response_info = {
			.status_code = 200,
			.content_type = TEXT_FILE,
			.content_length = file_size,
		};
		char response_header[CONFIG_HTTP_RESPONSE_HEADER_LEN] = {0};
		fileser_set_http_header(response_header, &response_info);

		if (http_write(session, response_header, strlen(response_header), 5000) < 0) {
			ret = -2;
			AGLINK_ERROR("http_write head failed\n");
		}
	}

	cur = head;
	while (cur != NULL) {
		if (ret >= 0) {
			ret = fileser_send_file(session, cur->path, cur->file_size);
			if (ret == 0) {
				snprintf(file_tmp, DIR_GPT_FILE_NAME_LEN + 1, "%-*s", DIR_GPT_FILE_NAME_LEN, cur->name);
				for (int i = 0; i < DIR_GPT_FILE_NAME_LEN; i++) {
    			    if (file_tmp[i] == ' ' || file_tmp[i] == '\0') file_tmp[i] = '0';
    			}
				file_gpt_offset += snprintf(file_gpt + file_gpt_offset, file_gpt_size - file_gpt_offset, "%-s", file_tmp);
				file_gpt_offset += snprintf(file_gpt + file_gpt_offset, file_gpt_size - file_gpt_offset, "%08lx", cur->file_size);
				file_cnt++;
			} else {
				AGLINK_ERROR("send %s failed\n", cur->path);
			}
		}
		dir_node *tmp = cur;
		cur = cur->next;
		free(tmp);
	}

	if (file_gpt) {
		snprintf(file_gpt + file_gpt_offset, file_gpt_size - file_gpt_offset, "%08x%s", file_cnt, "Thisadir");
		if (http_write(session, file_gpt, strlen(file_gpt), 5000) < 0) {
			ret = -2;
			AGLINK_ERROR("http_write gpt failed\n");
		}
		free(file_gpt);
	}

	return ret;
}

static void *fileser_handle_request(void *arg)
{
    task_data_t *task_data = (task_data_t *)arg;
    void *session = task_data->data1;
	free(task_data);

	int8_t tid = -1;

	pthread_mutex_lock(&bitmap_lock);
	tid = SET_DOWNLOADING();
	pthread_mutex_unlock(&bitmap_lock);

	signal(SIGPIPE, SIG_IGN); // tcp reset result to this process exiting. fix it.

    /* recv client request information, requestion size must be less than buf_size*/
    char buffer[CONFIG_HTTP_REQUEST_TOTAL_LEN] = {0};
	size_t buf_size = sizeof(buffer);

	int bytes_received = http_read(session, buffer, buf_size - 1, 5000);

    if (bytes_received < 0) {
		AGLINK_ERROR("recv error.\n");
        goto exit;
    }
    buffer[bytes_received] = '\0';
#if CONFIG_DEBUG_DUMP_HTTP_REQUEST_INFO
    AGLINK_INFO("client http request: %s\n", buffer);
#endif

    char file_name[CONFIG_FILE_NAME_LEN] = {0};
    /* parse http request */
	const char *key_str = "GET /files/";
	char *request = strstr(buffer, key_str);
    if (request) {
        /* file's name, file's name behind with "/files/" */
        char *file_name_start = buffer + strlen(key_str);
        char *file_name_end = NULL;
        uint8_t file_name_len = 0;

        file_name_end = strchr(file_name_start,' ');
        file_name_len = file_name_end - file_name_start;
		if (file_name_len > CONFIG_FILE_NAME_LEN) {
			AGLINK_ERROR("file name len of requestion: %d > %d.\n", file_name_len, sizeof(file_name));
            goto exit;
		}
        memcpy(file_name, file_name_start, file_name_len);

	} else {
        /* invalid request */
        AGLINK_ERROR("invalid http request.\n");
        AGLINK_ERROR("client http request: \n%s\n\n", buffer);
        char *error_message = "HTTP/1.1 400 Bad Request\r\n"
                                    "Content-Type: text/plain\r\n\r\n"
                                    "Invalid Request.";
        http_write(session, error_message, strlen(error_message), 5000);
		AGLINK_ERROR("send responese: \n%s\n", error_message);
		goto exit;
	}

    char file_path[CONFIG_FILE_PATH_LEN] = {0};
    snprintf(file_path, sizeof(file_path), "%s/%s", CONFIG_DEFAULT_FILE_DIR, file_name);

	/* prepare */
#if CONFIG_FILELIST_CHECK
#define WAIT_TIMEOUT_MS 5000
	/* the file is filelist */
	if (!strcmp(file_name, CONFIG_DEFAULT_FILELIST_NAME)) {
		int ret = check_access(file_path, WAIT_TIMEOUT_MS);
		if (ret < 0) {
			AGLINK_ERROR("Error: %s can't access", file_path);
			goto exit;
		} else {
			AGLINK_INFO("wait %s wirteable %d ms", file_path, ret);
		}

		pthread_mutex_lock(&file_lock);
		ret = filelist_check(file_path);
		pthread_mutex_unlock(&file_lock);
		if (ret	!= 0) {
			AGLINK_INFO("check file list failed.\n");
		}
	}
#endif

	/* send response */
	int ret;
	switch (check_path(file_path)) {
		case IS_DIR:
			AGLINK_ERROR("send dir: %s\n", file_path);
			ret = fileser_send_dir(file_path, session);
		break;
		case IS_FILES:
			AGLINK_ERROR("send file: %s\n", file_path);
			ret = fileser_send_one_file(file_path, session);
		break;
		case IS_OTHER:
		case IS_NOACCESS:
			AGLINK_ERROR("%s no found\n", file_path);
		goto not_find_files;
	}
	if (ret == 0) goto send_files_success;
	else if (ret == -1) goto not_find_files;
	else if (ret == -2) goto send_files_failed;
	else goto exit;

not_find_files:
	{
		/* handle file failed. */
		char *error_message = "HTTP/1.1 404 Not Found\r\n"
									"Content-Type: text/plain\r\n\r\n"
									"File not found.";
        http_write(session, error_message, strlen(error_message), 5000);
		AGLINK_ERROR("send %s\n", error_message);
		goto send_exit;
	}

send_files_success:
	{
		/* the file is not file_list */
		if (strcmp(file_name, CONFIG_DEFAULT_FILELIST_NAME)) {
			char *str_to_delete = file_name;

			memset(file_path, '\0', sizeof(file_path));
			snprintf(file_path, sizeof(file_path), "%s/%s", CONFIG_DEFAULT_FILE_DIR,
				CONFIG_DEFAULT_FILELIST_NAME);
			pthread_mutex_lock(&file_lock);
			filelist_delete(file_path, str_to_delete);
			pthread_mutex_unlock(&file_lock);
			sent_files ++;
		} else {
			total_files = filelist_count(file_path);
			sent_files = 0;
		}
		AGLINK_INFO("HTTP SEND FILES (%d/%d)\n", sent_files, total_files);
		goto send_exit;
	}

send_files_failed:
	goto send_exit;

send_exit:
    if (sent_files == total_files) {
        if (total_files == 0) {
            AGLINK_INFO("no files sent\n");
        } else {
            extern void download_over(void *);
            download_over(NULL);
        }
    }

exit:
	pthread_mutex_lock(&bitmap_lock);
	CLEAR_DOWNLOADING(tid);
	pthread_mutex_unlock(&bitmap_lock);

	http_close(session);

    return NULL;
}

void __attribute__((weak)) download_over(void *arg)
{
    (void)arg;

    AGLINK_INFO("download over.\n");
}

static int fileser_init(HTTP_CTX **_http_ctx)
{
	*_http_ctx = NULL;

	*_http_ctx = (HTTP_CTX *)http_init();
	if (*_http_ctx == NULL) {
		return -1;
	}

	if (http_config(*_http_ctx, HTTP_SERVER, 0)) {
		return -1;
	}

	return 0;
}

static int fileser_deinit(HTTP_CTX *http_ctx)
{
	if (http_ctx == NULL)
		return -1;

	http_deinit(http_ctx);

	return 0;
}

static void *fileser_looper(void* arg)
{
    (void)arg;

    AGLINK_INFO("file server version: %s\n", "v0.0.3");

    task_state = TASK_RUNNING;

	HTTP_CTX *http_ctx = NULL;
	if (fileser_init(&http_ctx)) {
		AGLINK_ERROR("file server init failed.\n");
		goto exit;
	}

    do_http_open = 1;
    while (!file_server_exit) {
        if (!do_http_open) AGLINK_INFO("waiting file server exit...\n");
        else AGLINK_INFO("Server listening...\n");
        /* accept client */
		void *session = NULL;
        while (do_http_open) {
			int ret;
			session = http_open(http_ctx, NULL, 0);
			if (session) {
                AGLINK_INFO("Client connect successfully.\n");
                goto process_request;
			} else {
				if (errno == EAGAIN || errno == EWOULDBLOCK) {
					usleep(50 * 1000);
					continue;
				} else {
					AGLINK_INFO("Client connect failed.\n");
					usleep(50 * 1000);
					continue;
				}
			}
        }
        if (!do_http_open) continue;

process_request:
		{
			/* process client request */
			task_data_t *task_data = (task_data_t *)malloc(sizeof(task_data_t));

			task_data->data1 = session;

			pthread_t thread;
			pthread_create(&thread, NULL, fileser_handle_request, (void *)task_data);
			pthread_detach(thread);
		}
    }

exit:
	fileser_deinit(http_ctx);

    pthread_mutex_destroy(&file_lock);
    pthread_mutex_destroy(&bitmap_lock);

    task_state = TASK_EXITED;

    AGLINK_INFO("http server looper exit.\n");

    file_server_exit = 2;

    return NULL;
}

int fileser_start(void)
{
    int ret = 0;

    if (task_state == TASK_INIT || task_state == TASK_EXITED) {
		pthread_t file_server_thread;
        ret = pthread_create(&file_server_thread, NULL, fileser_looper, NULL);
        ret = pthread_detach(file_server_thread);
        if (!ret) {
            pthread_mutex_init(&file_lock, NULL);
            pthread_mutex_init(&bitmap_lock, NULL);
            task_state = TASK_CREATED;
            AGLINK_INFO("create file server successfully.\n");
        } else {
            AGLINK_INFO("create file server failed!\n");
        }
    }

    return ret;
}

void fileser_stop(void)
{
    do_http_open = 0;
    file_server_exit = 1;
}

#if CONFIG_TEST_FILE_SERVER
static void sigint_handler(int signum)
{
    AGLINK_INFO("received signal %d.\n", signum);

    do_http_open = 0;
    file_server_exit = 1;
}

int main(int argc, char *argv[])
{
    (void)argc;
    (void)argv;

    if (signal(SIGINT, sigint_handler) == SIG_ERR) {
        perror("setting signal handler");
        exit(1);
    }

    file_server_exit = 0;
    fileser_start();

    process_exit = 0;
    while (!process_exit) {
        usleep(100 * 1000);
        if (file_server_exit == 2) {
            process_exit = 1;
        }
    }
    AGLINK_INFO("main task exit.\n");

    exit(0);

    return 0;
}
#endif

