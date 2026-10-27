#define _POSIX_C_SOURCE 200809L
#include "ag_media.h"
#include "../misc/media_owner.h"
#include "ag_media_video.h"
#include "../aglink/ag_wrap.h"
#include <aglink/ag_proto.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>

struct ag_media {
    ag_media_config_t config;
    ag_media_status_t status;
    int audio_fd, lock_fd, mode_fd;
};

/* AGLink rx_cb_t has no user-data argument. The lock also keeps close() from
 * freeing the instance while the receive callback is writing audio/feeding MPP. */
static pthread_mutex_t receiver_lock = PTHREAD_MUTEX_INITIALIZER;
static ag_media_t *receiver;

static int receive_media(uint8_t type, uint8_t *data, int length)
{
    pthread_mutex_lock(&receiver_lock);
    ag_media_t *media = receiver;
    if (!media) { pthread_mutex_unlock(&receiver_lock); return 0; }
    if (type == AG_AD_AUDIO && data && length > 0 && !media->status.stop_requested) {
        if (media->status.mode == AG_MEDIA_AUDIO && media->audio_fd >= 0) {
            size_t offset = 0;
            while (offset < (size_t)length) {
                ssize_t count = write(media->audio_fd, data + offset, (size_t)length - offset);
                if (count < 0 && errno == EINTR) continue;
                if (count <= 0) {
                    media->status.error = -(count < 0 ? errno : EIO);
                    media->status.stop_requested = true;
                    break;
                }
                offset += (size_t)count;
                media->status.audio_bytes += (uint64_t)count;
            }
        } else if (media->status.mode == AG_MEDIA_VIDEO) {
            ag_media_video_pcm(data, length);
        }
    } else if ((type == AG_AD_RECORD_AUDIO_STOP && media->status.mode == AG_MEDIA_AUDIO) ||
               (type == AG_AD_VIDEO_STOP && media->status.mode == AG_MEDIA_VIDEO)) {
        media->status.stop_requested = true;
    } else if (type == AG_AD_DIS_LINE && media->status.mode != AG_MEDIA_IDLE) {
        media->status.error = -ENOTCONN;
        media->status.stop_requested = true;
    }
    pthread_mutex_unlock(&receiver_lock);
    return 0;
}

void ag_media_config_init(ag_media_config_t *config)
{
    if (!config) return;
    memset(config, 0, sizeof(*config));
    config->photo_channel = 4;
    config->photo_quality = 80;
}

ag_media_t *ag_media_open(const ag_media_config_t *config)
{
    ag_media_t *media = calloc(1, sizeof(*media));
    int rc;
    if (!media) return NULL;
    media->audio_fd = media->lock_fd = media->mode_fd = -1;
    if (config) media->config = *config; else ag_media_config_init(&media->config);
    if (media->config.dev_id < 0 || media->config.dev_id > 255 ||
        media->config.photo_channel < 0 || media->config.photo_channel > 15 ||
        !media->config.photo_quality || media->config.photo_quality > 100 ||
        media->config.photo_width > 8192 || media->config.photo_height > 8192 ||
        (!media->config.photo_width != !media->config.photo_height)) {
        free(media); errno = EINVAL; return NULL;
    }
    /* Shared ownership with local MPP and the controlled standalone apps. */
    media->mode_fd = v821_mode_owner_acquire();
    if (media->mode_fd < 0) { rc = -errno; goto fail; }
    media->lock_fd = v821_media_owner_acquire();
    if (media->lock_fd < 0) { rc = -errno; goto fail; }
    pthread_mutex_lock(&receiver_lock);
    if (receiver) { pthread_mutex_unlock(&receiver_lock); rc = -EBUSY; goto fail; }
    receiver = media;
    pthread_mutex_unlock(&receiver_lock);
    rc = aglink_channel_create(media->config.dev_id);
    if (rc < 0) { rc = -ENODEV; goto fail_receiver; }
    rc = aglink_channel_register_cb(receive_media, media->config.dev_id);
    if (rc < 0) { aglink_channel_delete(media->config.dev_id); rc = -EIO; goto fail_receiver; }
    return media;
fail_receiver:
    pthread_mutex_lock(&receiver_lock);
    receiver = NULL;
    pthread_mutex_unlock(&receiver_lock);
fail:
    if (media->lock_fd >= 0) close(media->lock_fd);
    if (media->mode_fd >= 0) close(media->mode_fd);
    free(media);
    errno = -rc;
    return NULL;
}

int ag_media_photo(ag_media_t *media, const char *path)
{
    if (!media || !path || !*path) return -EINVAL;
    pthread_mutex_lock(&receiver_lock);
    bool busy = media->status.mode != AG_MEDIA_IDLE;
    pthread_mutex_unlock(&receiver_lock);
    if (busy) return -EBUSY;
    int rc = ag_media_take_jpeg(path, media->config.photo_channel, media->config.photo_width,
                                media->config.photo_height, media->config.photo_quality);
    return rc;
}

int ag_media_record_start(ag_media_t *media, ag_media_mode_t mode, const char *path)
{
    int fd = -1, rc;
    if (!media || !path || !*path || (mode != AG_MEDIA_AUDIO && mode != AG_MEDIA_VIDEO)) return -EINVAL;
    pthread_mutex_lock(&receiver_lock);
    bool busy = media->status.mode != AG_MEDIA_IDLE;
    pthread_mutex_unlock(&receiver_lock);
    if (busy) return -EBUSY;
    if (mode == AG_MEDIA_AUDIO) {
        fd = open(path, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600);
        if (fd < 0) return -errno;
    } else {
        rc = ag_media_video_start(path);
        if (rc) return rc;
    }
    pthread_mutex_lock(&receiver_lock);
    media->status = (ag_media_status_t){.mode = mode};
    media->audio_fd = fd;
    pthread_mutex_unlock(&receiver_lock);
    rc = aglink_tx_data(media->config.dev_id,
                        mode == AG_MEDIA_AUDIO ? AG_VD_RECORD_AUDIO_START : AG_VD_VIDEO_START, NULL, 0);
    if (rc < 0) {
        (void)ag_media_record_stop(media);
        unlink(path);
        return -EIO;
    }
    return 0;
}

int ag_media_record_stop(ag_media_t *media)
{
    if (!media) return -EINVAL;
    pthread_mutex_lock(&receiver_lock);
    ag_media_mode_t mode = media->status.mode;
    int fd = media->audio_fd, rc = media->status.error;
    uint64_t bytes = media->status.audio_bytes;
    media->status.mode = AG_MEDIA_IDLE;
    media->audio_fd = -1;
    pthread_mutex_unlock(&receiver_lock);
    if (mode == AG_MEDIA_IDLE) return -ENOENT;
    if (mode == AG_MEDIA_VIDEO) {
        int stopped = ag_media_video_stop();
        if (!rc) rc = stopped;
        if (aglink_tx_data(media->config.dev_id, AG_VD_VIDEO_END, NULL, 0) < 0 && !rc) rc = -EIO;
    } else {
        if (fd >= 0) {
            if (fsync(fd) < 0 && !rc) rc = -errno;
            if (close(fd) < 0 && !rc) rc = -errno;
        }
        if (!bytes && !rc) rc = -ENODATA;
        struct ag_generic_rsp response = {
            .mode = AG_MODE_RECORD_AUDIO,
            .code = rc ? AG_FAILED_WRITE_FILE : AG_SUCCESS
        };
        if (aglink_tx_data(media->config.dev_id, AG_VD_GENERIC_RESPONSE,
                           (uint8_t *)&response, sizeof(response)) < 0 && !rc) rc = -EIO;
    }
    return rc;
}

void ag_media_status(ag_media_t *media, ag_media_status_t *status)
{
    if (!status) return;
    memset(status, 0, sizeof(*status));
    if (!media) return;
    pthread_mutex_lock(&receiver_lock);
    *status = media->status;
    pthread_mutex_unlock(&receiver_lock);
    if (status->mode == AG_MEDIA_VIDEO) {
        int state = ag_media_video_status();
        if (state) status->stop_requested = true;
        if (state < 0 && !status->error) status->error = state;
    }
}

void ag_media_close(ag_media_t *media)
{
    if (!media) return;
    (void)ag_media_record_stop(media);
    pthread_mutex_lock(&receiver_lock);
    receiver = NULL;
    pthread_mutex_unlock(&receiver_lock);
    aglink_channel_delete(media->config.dev_id);
    close(media->lock_fd);
    close(media->mode_fd);
    free(media);
}
