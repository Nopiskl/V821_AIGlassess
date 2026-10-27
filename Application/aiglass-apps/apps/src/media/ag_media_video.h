#ifndef AIGLASS_APP_MEDIA_VIDEO_H
#define AIGLASS_APP_MEDIA_VIDEO_H

#include <stddef.h>

/* Implemented by app/video/take_video.c. Serialized by ag_media.c. */
int ag_media_video_start(const char *path);
int ag_media_video_stop(void);
int ag_media_video_status(void); /* 0: running; 1: stop requested; <0: error. */
void ag_media_video_pcm(const unsigned char *data, int length);
int ag_media_take_jpeg(const char *path, int channel, unsigned width,
                       unsigned height, unsigned quality);

#endif
