#define _GNU_SOURCE
#include "aiglass_internal.h"
#include "aiglass_json.h"
#include "../aiglass-apps/apps/src/misc/media_owner.h"
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <glob.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

static pthread_mutex_t json_mutex = PTHREAD_MUTEX_INITIALIZER;
cJSON *ag_json_parse(const char *text)
{
    const char *end = NULL;
    cJSON *value;
    if (!text) return NULL;
    pthread_mutex_lock(&json_mutex);
    value = cJSON_ParseWithOpts(text, &end, true);
    pthread_mutex_unlock(&json_mutex);
    return value;
}

void ag_copy(char *dst, size_t size, const char *src)
{
    if (size) {
        size_t n;
        if (!src) src = "";
        n = strnlen(src, size - 1);
        if (src[n]) while (n && ((unsigned char)src[n] & 0xc0) == 0x80) --n;
        memcpy(dst, src, n);
        dst[n] = '\0';
    }
}

uint64_t ag_milliseconds(void)
{
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) return 0;
    return (uint64_t)ts.tv_sec * 1000 + (uint64_t)ts.tv_nsec / 1000000;
}

bool ag_cancelled(ag_services_t *s, uint32_t id)
{
    return atomic_load(&s->stopping) || (id && atomic_load(&s->cancelled) == id);
}

void ag_services_config_init(ag_services_config_t *c)
{
    const char *value;
    if (!c) return;
    memset(c, 0, sizeof(*c));
    c->hardware_access = true;
#define ENV(field, name, fallback) \
    ag_copy(c->field, sizeof(c->field), (value = getenv(name)) ? value : (fallback))
    ENV(data_dir, "AIGLASS_DATA_DIR", "./aiglass-data");
    ENV(socket_path, "AIGLASS_SOCKET", "");
    ENV(chat_url, "AIGLASS_CHAT_URL", "");
    ENV(asr_url, "AIGLASS_ASR_URL", "");
    ENV(api_key, "AIGLASS_API_KEY", "");
    ENV(model, "AIGLASS_MODEL", "");
    ENV(asr_model, "AIGLASS_ASR_MODEL", "");
    ENV(asr_mime, "AIGLASS_ASR_MIME", "audio/wav");
    ENV(backlight, "AIGLASS_BACKLIGHT", "");
    ENV(phone_bind, "AIGLASS_PHONE_BIND", "127.0.0.1");
    ENV(phone_token, "AIGLASS_PHONE_TOKEN", "");
#undef ENV
    vm_media_config_init(&c->media);
#define ENV_NUMBER(field, name, maximum) do { \
    value = getenv(name); \
    if (value && *value) { \
        char *end; \
        unsigned long number = strtoul(value, &end, 10); \
        if (!*end && number <= (maximum)) c->media.field = number; \
    } \
} while (0)
    ENV_NUMBER(vipp, "AIGLASS_VIPP", 15);
    ENV_NUMBER(vi_channel, "AIGLASS_VI_CHANNEL", 15);
    ENV_NUMBER(isp, "AIGLASS_ISP", 3);
    ENV_NUMBER(venc_channel, "AIGLASS_VENC_CHANNEL", 15);
    ENV_NUMBER(mux_channel, "AIGLASS_MUX_CHANNEL", 15);
    ENV_NUMBER(width, "AIGLASS_CAPTURE_WIDTH", 4096);
    ENV_NUMBER(height, "AIGLASS_CAPTURE_HEIGHT", 4096);
    ENV_NUMBER(fps, "AIGLASS_CAPTURE_FPS", 120);
    ENV_NUMBER(video_bitrate, "AIGLASS_VIDEO_BITRATE", 100000000);
    ENV_NUMBER(photo_quality, "AIGLASS_PHOTO_QUALITY", 100);
    ENV_NUMBER(ai_device, "AIGLASS_AI_DEVICE", 15);
    ENV_NUMBER(ai_channel, "AIGLASS_AI_CHANNEL", 15);
    ENV_NUMBER(aenc_channel, "AIGLASS_AENC_CHANNEL", 15);
    ENV_NUMBER(sample_rate, "AIGLASS_AUDIO_RATE", 48000);
    ENV_NUMBER(audio_channels, "AIGLASS_AUDIO_CHANNELS", 2);
    ENV_NUMBER(video_audio, "AIGLASS_VIDEO_AUDIO", 1);
    ENV_NUMBER(manage_system, "AIGLASS_MPP_MANAGE_SYS", 1);
#undef ENV_NUMBER
    value = getenv("AIGLASS_PHONE_PORT");
    if (value) {
        char *end;
        unsigned long port = strtoul(value, &end, 10);
        if (*value && !*end && port <= 65535) c->phone_port = (uint16_t)port;
    }
    c->speech_seconds = 4;
    value = getenv("AIGLASS_SPEECH_SECONDS");
    if (value) {
        unsigned seconds = (unsigned)strtoul(value, NULL, 10);
        if (seconds >= 1 && seconds <= 15) c->speech_seconds = seconds;
    }
}

bool ag_leaf_valid(const char *name)
{
    return name && *name && strlen(name) <= 120 && name[0] != '.' &&
           !strchr(name, '/') && !strchr(name, '\\') &&
           !strchr(name, '\n') && !strchr(name, '\r');
}

int ag_folder_open(ag_services_t *s, const char *folder)
{
    if (!folder || (strcmp(folder, "notes") && strcmp(folder, "media") &&
                    strcmp(folder, "inbox"))) {
        errno = EINVAL;
        return -1;
    }
    return openat(s->root_fd, folder, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
}

int ag_file_open(ag_services_t *s, const char *relative, int flags, mode_t mode)
{
    char folder[16];
    const char *slash = relative ? strchr(relative, '/') : NULL;
    int dir, fd, saved;
    struct stat st;
    if (!slash || (size_t)(slash - relative) >= sizeof(folder) ||
        !ag_leaf_valid(slash + 1)) { errno = EINVAL; return -1; }
    memcpy(folder, relative, (size_t)(slash - relative));
    folder[slash - relative] = '\0';
    dir = ag_folder_open(s, folder);
    if (dir < 0) return -1;
    fd = openat(dir, slash + 1, flags | O_NOFOLLOW | O_CLOEXEC | O_NONBLOCK, mode);
    saved = errno;
    close(dir);
    if (fd >= 0 && (fstat(fd, &st) < 0 || !S_ISREG(st.st_mode))) {
        close(fd); errno = EINVAL; return -1;
    }
    errno = saved;
    return fd;
}

int ag_write_all(int fd, const void *data, size_t size)
{
    const unsigned char *p = data;
    while (size) {
        ssize_t n = write(fd, p, size);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) return -(n < 0 ? errno : EIO);
        p += n;
        size -= (size_t)n;
    }
    return 0;
}

int ag_list_files(ag_services_t *s, const char *folder, char *text, size_t size)
{
    int fd = ag_folder_open(s, folder);
    DIR *directory;
    struct dirent *entry;
    cJSON *list;
    unsigned count = 0;
    int result = 0;
    if (fd < 0) return -errno;
    directory = fdopendir(fd);
    if (!directory) { close(fd); return -errno; }
    list = cJSON_CreateArray();
    if (!list) { closedir(directory); return -ENOMEM; }
    while ((entry = readdir(directory)) != NULL && count < 32) {
        struct stat st;
        cJSON *item;
        if (!ag_leaf_valid(entry->d_name) ||
            fstatat(fd, entry->d_name, &st, AT_SYMLINK_NOFOLLOW) < 0 || !S_ISREG(st.st_mode)) continue;
        item = cJSON_CreateObject();
        if (!item || !cJSON_AddStringToObject(item, "name", entry->d_name) ||
            !cJSON_AddNumberToObject(item, "size", (double)st.st_size)) {
            cJSON_Delete(item); result = -ENOMEM; break;
        }
        if (!cJSON_AddItemToArray(list, item)) { cJSON_Delete(item); result = -ENOMEM; break; }
        ++count;
    }
    if (!result && !cJSON_PrintPreallocated(list, text, (int)size, false)) result = -EOVERFLOW;
    cJSON_Delete(list);
    closedir(directory);
    return result;
}

void ag_publish(ag_services_t *s, const ag_result_t *result)
{
    pthread_mutex_lock(&s->mutex);
    while (s->result_count == AG_RESULT_COUNT && result->id && !atomic_load(&s->stopping))
        pthread_cond_wait(&s->wake, &s->mutex);
    if (s->result_count < AG_RESULT_COUNT && !atomic_load(&s->stopping)) {
        s->results[(s->result_read + s->result_count) % AG_RESULT_COUNT] = *result;
        ++s->result_count;
    }
    pthread_mutex_unlock(&s->mutex);
}

uint32_t ag_services_submit(ag_services_t *s, ag_operation_t op,
                            const char *text, const char *argument)
{
    ag_job_t *job;
    uint32_t id;
    if (!s || op < AG_CHAT || op > AG_VOLUME) { errno = EINVAL; return 0; }
    if ((text && strlen(text) >= AG_INPUT_MAX) ||
        (argument && strlen(argument) >= AG_PATH_MAX)) { errno = E2BIG; return 0; }
    pthread_mutex_lock(&s->mutex);
    if (s->job_count == AG_JOB_COUNT || atomic_load(&s->stopping)) {
        pthread_mutex_unlock(&s->mutex); errno = EBUSY; return 0;
    }
    job = &s->jobs[(s->job_read + s->job_count) % AG_JOB_COUNT];
    memset(job, 0, sizeof(*job));
    id = ++s->next_id;
    if (!id) id = ++s->next_id;
    job->id = id;
    job->operation = op;
    ag_copy(job->text, sizeof(job->text), text);
    ag_copy(job->argument, sizeof(job->argument), argument);
    ++s->job_count;
    pthread_cond_signal(&s->wake);
    pthread_mutex_unlock(&s->mutex);
    return id;
}

void ag_services_cancel(ag_services_t *s, uint32_t id)
{
    if (s && id) atomic_store(&s->cancelled, id);
}

int ag_services_poll(ag_services_t *s, ag_result_t *result)
{
    int available = 0;
    if (!s || !result) return 0;
    pthread_mutex_lock(&s->mutex);
    if (s->result_count) {
        *result = s->results[s->result_read];
        s->result_read = (s->result_read + 1) % AG_RESULT_COUNT;
        --s->result_count;
        available = 1;
        pthread_cond_broadcast(&s->wake);
    }
    pthread_mutex_unlock(&s->mutex);
    return available;
}

void ag_services_status(ag_services_t *s, ag_status_t *status)
{
    if (!status) return;
    memset(status, 0, sizeof(*status));
    status->battery_percent = status->brightness_percent = status->volume_percent = -1;
    if (!s) return;
    pthread_mutex_lock(&s->mutex);
    *status = s->status;
    status->phone_connected = s->status.phone_paired && s->phone_seen && ag_milliseconds() - s->phone_seen < 15000;
    pthread_mutex_unlock(&s->mutex);
}

static int read_integer(const char *path)
{
    char buf[32], *end;
    int fd = open(path, O_RDONLY | O_CLOEXEC);
    ssize_t n;
    long value;
    if (fd < 0) return -1;
    n = read(fd, buf, sizeof(buf) - 1);
    close(fd);
    if (n <= 0) return -1;
    buf[n] = '\0';
    value = strtol(buf, &end, 10);
    if (end == buf || value < 0 || value > 10000000) return -1;
    return (int)value;
}

static void refresh_status(ag_services_t *s)
{
    int battery = -1;
    char address[64] = "";
    struct ifaddrs *addresses = NULL;
    if (s->config.hardware_access) {
        glob_t found = {0};
        if (glob("/sys/class/power_supply/*/capacity", 0, NULL, &found) == 0 && found.gl_pathc)
            battery = read_integer(found.gl_pathv[0]);
        globfree(&found);
    }
    if (getifaddrs(&addresses) == 0) {
        struct ifaddrs *it;
        for (it = addresses; it; it = it->ifa_next) {
            if (it->ifa_addr && it->ifa_addr->sa_family == AF_INET &&
                (it->ifa_flags & IFF_UP) && !(it->ifa_flags & IFF_LOOPBACK)) {
                struct sockaddr_in *addr = (struct sockaddr_in *)it->ifa_addr;
                inet_ntop(AF_INET, &addr->sin_addr, address, sizeof(address));
                break;
            }
        }
        freeifaddrs(addresses);
    }
    pthread_mutex_lock(&s->mutex);
    s->status.battery_percent = battery <= 100 ? battery : -1;
    ag_copy(s->status.address, sizeof(s->status.address), address);
    pthread_mutex_unlock(&s->mutex);
}

int ag_write_note(ag_services_t *s, const char *name, const char *text, bool replace)
{
    int dir, fd, rc;
    if (!ag_leaf_valid(name) || !text || strlen(text) >= AG_INPUT_MAX) return -EINVAL;
    dir = ag_folder_open(s, "notes");
    if (dir < 0) return -errno;
    char temporary[160];
    snprintf(temporary, sizeof(temporary), ".note-%llu-%lu.tmp",
             (unsigned long long)ag_milliseconds(), (unsigned long)pthread_self());
    fd = openat(dir, temporary, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600);
    if (fd < 0) { close(dir); return -errno; }
    rc = ag_write_all(fd, text, strlen(text));
    if (!rc && fsync(fd) < 0) rc = -errno;
    close(fd);
    if (!rc && renameat2(dir, temporary, dir, name, replace ? 0 : RENAME_NOREPLACE) < 0) rc = -errno;
    if (rc) unlinkat(dir, temporary, 0);
    else if (fsync(dir) < 0) rc = -errno;
    close(dir);
    return rc;
}

static int change_brightness(ag_services_t *s, int percent)
{
    if (!s->config.hardware_access) return -ENOTSUP;
    char path[AG_PATH_MAX], buf[32];
    int fd, maximum, rc;
    ag_copy(path, sizeof(path), s->config.backlight);
    if (!path[0]) {
        glob_t found = {0};
        if (glob("/sys/class/backlight/*/brightness", 0, NULL, &found) == 0 && found.gl_pathc)
            ag_copy(path, sizeof(path), found.gl_pathv[0]);
        globfree(&found);
    }
    if (!path[0]) return -ENODEV;
    char max_path[AG_PATH_MAX];
    ag_copy(max_path, sizeof(max_path), path);
    char *slash = strrchr(max_path, '/');
    if (!slash || (size_t)(slash - max_path) + sizeof("/max_brightness") > sizeof(max_path)) return -EINVAL;
    strcpy(slash, "/max_brightness");
    maximum = read_integer(max_path);
    if (maximum <= 0) return -ENODEV;
    snprintf(buf, sizeof(buf), "%d\n", (int)((int64_t)maximum * percent / 100));
    fd = open(path, O_WRONLY | O_CLOEXEC);
    if (fd < 0) return -errno;
    rc = ag_write_all(fd, buf, strlen(buf));
    close(fd);
    if (!rc) {
        pthread_mutex_lock(&s->mutex); s->status.brightness_percent = percent; pthread_mutex_unlock(&s->mutex);
    }
    return rc;
}

static int execute_job(ag_services_t *s, const ag_job_t *job, ag_result_t *out)
{
    int fd, rc;
    switch (job->operation) {
    case AG_CHAT: case AG_TRANSLATE: case AG_VOICE_CHAT: case AG_VOICE_TRANSLATE:
    case AG_DOWNLOAD: case AG_BROWSE:
        return ag_http_job(s, job, out);
    case AG_LIST_FILES:
        return ag_list_files(s, job->argument, out->text, sizeof(out->text));
    case AG_READ_FILE: {
        ssize_t n;
        fd = ag_file_open(s, job->argument, O_RDONLY, 0);
        if (fd < 0) return -errno;
        do { n = read(fd, out->text, sizeof(out->text) - 1); } while (n < 0 && errno == EINTR);
        rc = n < 0 ? -errno : 0;
        close(fd);
        if (n >= 0) {
            /* A full preview may end inside a UTF-8 code point. */
            if ((size_t)n == sizeof(out->text) - 1) {
                size_t start = (size_t)n - 1;
                while (start && ((unsigned char)out->text[start] & 0xc0) == 0x80) --start;
                unsigned char lead = (unsigned char)out->text[start];
                size_t needed = lead < 0x80 ? 1 : lead < 0xe0 ? 2 : lead < 0xf0 ? 3 : 4;
                if (start + needed > (size_t)n) n = (ssize_t)start;
            }
            out->text[n] = '\0';
            for (ssize_t i = 0; i < n; ++i)
                if ((unsigned char)out->text[i] < 32 && out->text[i] != '\n' && out->text[i] != '\r' && out->text[i] != '\t')
                    return -EILSEQ;
        }
        return rc;
    }
    case AG_WRITE_NOTE:
        rc = ag_write_note(s, job->argument[0] ? job->argument : "note.txt", job->text, true);
        if (!rc) ag_copy(out->text, sizeof(out->text), "Saved");
        return rc;
    case AG_DELETE_FILE: {
        char folder[16];
        const char *slash = strchr(job->argument, '/');
        if (!slash || (size_t)(slash - job->argument) >= sizeof(folder) || !ag_leaf_valid(slash + 1)) return -EINVAL;
        memcpy(folder, job->argument, (size_t)(slash - job->argument));
        folder[slash - job->argument] = '\0';
        fd = ag_folder_open(s, folder);
        if (fd < 0) return -errno;
        rc = unlinkat(fd, slash + 1, 0) < 0 ? -errno : 0;
        close(fd);
        if (!rc) ag_copy(out->text, sizeof(out->text), "Deleted");
        return rc;
    }
    case AG_PHOTO: case AG_AUDIO_START: case AG_VIDEO_START:
        return ag_service_media_start(s, job, out);
    case AG_MEDIA_STOP:
        return ag_service_media_stop(s, out);
    case AG_PLAY_AUDIO: case AG_VOLUME:
        ag_copy(out->text, sizeof(out->text), "Local MPP playback and output-volume control are not configured");
        return -ENOTSUP;
    case AG_BRIGHTNESS: {
        char *end;
        long value = strtol(job->text, &end, 10);
        if (!*job->text || *end || value < 0 || value > 100) return -EINVAL;
        rc = change_brightness(s, (int)value);
        if (!rc) ag_copy(out->text, sizeof(out->text), "Updated");
        return rc;
    }
    default: return -ENOTSUP;
    }
}

static void *worker_thread(void *opaque)
{
    ag_services_t *s = opaque;
    uint64_t last_status = 0;
    while (!atomic_load(&s->stopping)) {
        ag_job_t job = {0};
        bool have_job = false;
        pthread_mutex_lock(&s->mutex);
        if (!s->job_count) {
            struct timespec deadline;
            clock_gettime(CLOCK_REALTIME, &deadline);
            deadline.tv_nsec += 100000000;
            if (deadline.tv_nsec >= 1000000000) { ++deadline.tv_sec; deadline.tv_nsec -= 1000000000; }
            pthread_cond_timedwait(&s->wake, &s->mutex, &deadline);
        }
        if (s->job_count) {
            job = s->jobs[s->job_read];
            s->job_read = (s->job_read + 1) % AG_JOB_COUNT;
            --s->job_count;
            have_job = true;
        }
        pthread_mutex_unlock(&s->mutex);
        if (have_job && !atomic_load(&s->stopping)) {
            ag_result_t result = {.id = job.id, .operation = job.operation};
            ag_copy(result.detail, sizeof(result.detail), job.argument);
            result.code = ag_cancelled(s, job.id) ? -ECANCELED : execute_job(s, &job, &result);
            if (result.code && !result.text[0]) ag_copy(result.text, sizeof(result.text), strerror(-result.code));
            ag_publish(s, &result);
        }
        ag_service_media_poll(s);
        if (ag_milliseconds() - last_status > 1000) { refresh_status(s); last_status = ag_milliseconds(); }
    }
    ag_service_media_close(s);
    return NULL;
}

ag_services_t *ag_services_open(const ag_services_config_t *config)
{
    ag_services_t *s = calloc(1, sizeof(*s));
    int rc;
    char *absolute;
    struct stat st;
    if (!s) return NULL;
    s->root_fd = s->unix_fd = s->tcp_fd = s->mode_owner_fd = -1;
    if (config) s->config = *config; else ag_services_config_init(&s->config);
    if (s->config.hardware_access) {
        s->mode_owner_fd = v821_mode_owner_acquire();
        if (s->mode_owner_fd < 0) goto fail;
    }
#define CHECK_STRING(field) do { \
    if (!memchr(s->config.field, '\0', sizeof(s->config.field))) { errno = EINVAL; goto fail; } \
} while (0)
    CHECK_STRING(data_dir); CHECK_STRING(socket_path);
    CHECK_STRING(chat_url); CHECK_STRING(asr_url); CHECK_STRING(api_key);
    CHECK_STRING(model); CHECK_STRING(asr_model); CHECK_STRING(asr_mime);
    CHECK_STRING(backlight); CHECK_STRING(phone_bind); CHECK_STRING(phone_token);
#undef CHECK_STRING
    if (s->config.speech_seconds < 1 || s->config.speech_seconds > 15) s->config.speech_seconds = 4;
    if (strpbrk(s->config.api_key, "\r\n") || strpbrk(s->config.asr_mime, "\r\n") || !s->config.asr_mime[0]) { errno = EINVAL; goto fail; }
    if (mkdir(s->config.data_dir, 0700) < 0 && errno != EEXIST) goto fail;
    s->root_fd = open(s->config.data_dir, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    if (s->root_fd < 0) goto fail;
    if (fstat(s->root_fd, &st) < 0) goto fail;
    if (st.st_uid != geteuid() || (st.st_mode & 0022)) { errno = EACCES; goto fail; }
    absolute = realpath(s->config.data_dir, NULL);
    if (!absolute) goto fail;
    if (strlen(absolute) + 140 >= sizeof(s->config.data_dir)) { free(absolute); errno = ENAMETOOLONG; goto fail; }
    ag_copy(s->config.data_dir, sizeof(s->config.data_dir), absolute);
    free(absolute);
    const char *folders[] = {"notes", "media", "inbox"};
    for (unsigned i = 0; i < sizeof(folders) / sizeof(folders[0]); ++i) {
        if (mkdirat(s->root_fd, folders[i], 0700) < 0 && errno != EEXIST) goto fail;
        int fd = ag_folder_open(s, folders[i]);
        if (fd < 0) goto fail;
        rc = fstat(fd, &st);
        close(fd);
        if (rc < 0) goto fail;
        if (st.st_uid != geteuid() || (st.st_mode & 0022)) { errno = EACCES; goto fail; }
    }
    if (!s->config.socket_path[0]) {
        rc = snprintf(s->config.socket_path, sizeof(s->config.socket_path), "%s/bridge.sock", s->config.data_dir);
        if (rc < 0 || rc >= (int)sizeof(s->config.socket_path)) { errno = ENAMETOOLONG; goto fail; }
    }
    rc = pthread_mutex_init(&s->mutex, NULL);
    if (rc) { errno = rc; goto fail; }
    rc = pthread_cond_init(&s->wake, NULL);
    if (rc) { pthread_mutex_destroy(&s->mutex); errno = rc; goto fail; }
    atomic_init(&s->stopping, false);
    atomic_init(&s->cancelled, 0);
    s->status.battery_percent = s->status.volume_percent = s->status.brightness_percent = -1;
    s->status.hardware_available = s->config.hardware_access;
    s->history = cJSON_CreateArray();
    rc = s->history ? ag_pairing_init(s) : -ENOMEM;
    if (!rc) rc = ag_phone_open(s);
    if (rc) goto fail_sync;
    rc = pthread_create(&s->worker, NULL, worker_thread, s);
    if (rc) { rc = -rc; goto fail_phone; }
    rc = pthread_create(&s->phone, NULL, ag_phone_thread, s);
    if (rc) {
        atomic_store(&s->stopping, true);
        pthread_cond_broadcast(&s->wake);
        pthread_join(s->worker, NULL);
        rc = -rc;
        goto fail_phone;
    }
    return s;
fail_phone:
    ag_phone_close(s);
fail_sync:
    cJSON_Delete(s->history);
    pthread_cond_destroy(&s->wake);
    pthread_mutex_destroy(&s->mutex);
    errno = -rc;
fail:
    rc = errno;
    if (s->root_fd >= 0) close(s->root_fd);
    if (s->mode_owner_fd >= 0) close(s->mode_owner_fd);
    free(s);
    errno = rc;
    return NULL;
}

void ag_services_close(ag_services_t *s)
{
    if (!s) return;
    atomic_store(&s->stopping, true);
    pthread_mutex_lock(&s->mutex);
    pthread_cond_broadcast(&s->wake);
    pthread_mutex_unlock(&s->mutex);
    pthread_join(s->worker, NULL);
    pthread_join(s->phone, NULL);
    ag_phone_close(s);
    cJSON_Delete(s->history);
    close(s->root_fd);
    if (s->mode_owner_fd >= 0) close(s->mode_owner_fd);
    pthread_cond_destroy(&s->wake);
    pthread_mutex_destroy(&s->mutex);
    memset(s->config.api_key, 0, sizeof(s->config.api_key));
    free(s);
}
