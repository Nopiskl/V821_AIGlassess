#ifndef V821_MPP_MEDIA_H
#define V821_MPP_MEDIA_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct vm_media vm_media_t;
typedef enum { VM_MEDIA_IDLE, VM_MEDIA_AUDIO, VM_MEDIA_VIDEO } vm_media_mode_t;
typedef struct {
    int vipp, vi_channel, isp, venc_channel, mux_channel;
    unsigned width, height, fps, video_bitrate, photo_quality;
    int ai_device, ai_channel, aenc_channel;
    unsigned sample_rate, audio_channels;
    bool video_audio; /* false: video-only MP4; speech/recording still use AI. */
    bool manage_system; /* false: host owns AW_MPI_SYS_Init/Exit, e.g. MPP VO. */
} vm_media_config_t;
typedef struct {
    vm_media_mode_t mode;
    bool stop_requested;
    int error; /* negative errno; vendor diagnostics are also written to stderr */
    uint64_t audio_bytes;
} vm_media_status_t;

/* One owner thread calls this API. Internal MPP callbacks/audio worker never
 * call LVGL. open() reserves the local media runtime; it does not open AGLink.
 * Every capture must finish before a new one begins. Files never overwrite.
 * Defaults follow the V821 samples; board/channel routing must be configured.
 * Return 0 / negative errno; open returns NULL and sets errno on failure. */
void vm_media_config_init(vm_media_config_t *config);
vm_media_t *vm_media_open(const vm_media_config_t *config);
int vm_media_photo(vm_media_t *media, const char *path);
int vm_media_record_start(vm_media_t *media, vm_media_mode_t mode, const char *path);
int vm_media_record_stop(vm_media_t *media);
void vm_media_status(vm_media_t *media, vm_media_status_t *status);
/* Returns 0 once released; a final file error is logged. On resource cleanup
 * failure, retains the instance/lock so callbacks cannot use a freed cookie.
 * The host must not start another media runtime in that process. */
int vm_media_close(vm_media_t *media);

#ifdef __cplusplus
}
#endif
#endif
