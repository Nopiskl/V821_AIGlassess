/* Host-only link replacement. No camera, microphone, MPP or device node access. */
#include "vm_media.h"
#include <errno.h>
#include <string.h>
void vm_media_config_init(vm_media_config_t *c)
{
    memset(c, 0, sizeof(*c));
    c->width = 1920; c->height = 1080; c->fps = 30;
    c->video_bitrate = 8000000; c->photo_quality = 85;
    c->sample_rate = 16000; c->audio_channels = 1;
}
vm_media_t *vm_media_open(const vm_media_config_t *c) { (void)c; errno = ENOTSUP; return NULL; }
int vm_media_photo(vm_media_t *m, const char *p) { (void)m; (void)p; return -ENOTSUP; }
int vm_media_record_start(vm_media_t *m, vm_media_mode_t mode, const char *p) { (void)m; (void)mode; (void)p; return -ENOTSUP; }
int vm_media_record_stop(vm_media_t *m) { (void)m; return -ENOTSUP; }
void vm_media_status(vm_media_t *m, vm_media_status_t *s) { (void)m; if (s) memset(s, 0, sizeof(*s)); }
int vm_media_close(vm_media_t *m) { (void)m; return 0; }
