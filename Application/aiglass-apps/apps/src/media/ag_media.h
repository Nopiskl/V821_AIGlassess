#ifndef AIGLASS_APP_MEDIA_H
#define AIGLASS_APP_MEDIA_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ag_media ag_media_t;
typedef enum { AG_MEDIA_IDLE, AG_MEDIA_AUDIO, AG_MEDIA_VIDEO } ag_media_mode_t;
typedef struct {
    int dev_id;          /* AGLink device, normally 0. One owner per process. */
    int photo_channel;   /* RT Media JPEG/YUV channel, normally 4. */
    unsigned photo_width, photo_height; /* Both 0: use the DTS JPEG dimensions. */
    unsigned photo_quality; /* 1..100, default 80. */
} ag_media_config_t;
typedef struct {
    ag_media_mode_t mode;
    bool stop_requested; /* Controller stop, storage error or MPP event. */
    int error;          /* Negative errno. */
    uint64_t audio_bytes;
} ag_media_status_t;

/* All calls except the internal AGLink callback belong to one owner thread.
 * This API owns the AGLink application callback. Do not run another ai_glass_*
 * application on the same device at the same time. It never sends POWER_OFF.
 * Return values are 0 / negative errno, or NULL with errno for open(). */
void ag_media_config_init(ag_media_config_t *config);
ag_media_t *ag_media_open(const ag_media_config_t *config);
void ag_media_close(ag_media_t *media);
int ag_media_photo(ag_media_t *media, const char *path);
int ag_media_record_start(ag_media_t *media, ag_media_mode_t mode, const char *path);
int ag_media_record_stop(ag_media_t *media);
void ag_media_status(ag_media_t *media, ag_media_status_t *status);

/* Audio files preserve AG_AD_AUDIO payloads, as app/audio/take_audio.c does.
 * No PCM/Opus conversion or Ogg muxing is assumed. The current AGLink UAPI has
 * no VD_RECORD_AUDIO_STOP command: local stop closes the file and reports a
 * generic response; the controller must stop its sender separately. */

#ifdef __cplusplus
}
#endif
#endif
