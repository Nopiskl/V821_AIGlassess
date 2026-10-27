#define _POSIX_C_SOURCE 200809L
#include "aiglass_internal.h"
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static int ensure_media(ag_services_t *services)
{
    if (!services->config.hardware_access) return -ENOTSUP;
    if (!services->media) services->media = vm_media_open(&services->config.media);
    return services->media ? 0 : -errno;
}

static void set_active(ag_services_t *services, bool active)
{
    pthread_mutex_lock(&services->mutex);
    services->status.media_active = active;
    pthread_mutex_unlock(&services->mutex);
}

static int media_name(ag_services_t *services, const char *extension, char *path, size_t size)
{
    int n = snprintf(path, size, "%s/media/%llu.%s", services->config.data_dir,
                     (unsigned long long)ag_milliseconds(), extension);
    return n < 0 || (size_t)n >= size ? -ENAMETOOLONG : 0;
}

int ag_service_media_start(ag_services_t *services, const ag_job_t *job, ag_result_t *out)
{
    int rc = ensure_media(services);
    vm_media_status_t state;
    char path[AG_PATH_MAX];
    if (rc) return rc;
    vm_media_status(services->media, &state);
    if (state.mode != VM_MEDIA_IDLE) return -EBUSY;
    const char *extension = job->operation == AG_PHOTO ? "jpg" :
                            job->operation == AG_AUDIO_START ? "wav" : "mp4";
    if ((rc = media_name(services, extension, path, sizeof(path))) < 0) return rc;
    if (job->operation == AG_PHOTO) {
        rc = vm_media_photo(services->media, path);
        /* MPP JPEG capture is synchronous: cancellation is applied once the
         * vendor call returns, without freeing a buffer still used by its driver. */
        if (!rc && ag_cancelled(services, job->id)) { unlink(path); return -ECANCELED; }
        if (!rc) ag_copy(out->text, sizeof(out->text), "Photo saved");
    } else {
        rc = vm_media_record_start(services->media,
                job->operation == AG_AUDIO_START ? VM_MEDIA_AUDIO : VM_MEDIA_VIDEO, path);
        if (!rc && ag_cancelled(services, job->id)) {
            (void)vm_media_record_stop(services->media);
            return -ECANCELED;
        }
        if (!rc) {
            ag_copy(services->media_path, sizeof(services->media_path), path);
            set_active(services, true);
            ag_copy(out->text, sizeof(out->text), "Local MPP capture started");
        }
    }
    if (!rc) ag_copy(out->detail, sizeof(out->detail), path);
    return rc;
}

int ag_service_media_stop(ag_services_t *services, ag_result_t *out)
{
    if (!services->media) return -ENOENT;
    int rc = vm_media_record_stop(services->media);
    set_active(services, false);
    if (out) {
        ag_copy(out->text, sizeof(out->text), rc ? strerror(-rc) : "Recording saved");
        ag_copy(out->detail, sizeof(out->detail), services->media_path);
    }
    return rc;
}

void ag_service_media_poll(ag_services_t *services)
{
    vm_media_status_t status;
    vm_media_status(services->media, &status);
    if (status.mode == VM_MEDIA_IDLE || !status.stop_requested) return;
    ag_result_t result = {.operation = AG_MEDIA_STOP};
    result.code = ag_service_media_stop(services, &result);
    if (!result.code && status.error) {
        result.code = status.error;
        ag_copy(result.text, sizeof(result.text), strerror(-status.error));
    }
    ag_publish(services, &result);
}

int ag_capture_speech(ag_services_t *services, uint32_t id, char *path, size_t size)
{
    int rc = ensure_media(services);
    vm_media_status_t state;
    if (rc) return rc;
    vm_media_status(services->media, &state);
    if (state.mode != VM_MEDIA_IDLE) return -EBUSY;
    if ((rc = media_name(services, "speech.wav", path, size)) < 0) return rc;
    rc = vm_media_record_start(services->media, VM_MEDIA_AUDIO, path);
    if (rc) return rc;
    set_active(services, true);
    uint64_t deadline = ag_milliseconds() + services->config.speech_seconds * 1000;
    while (!ag_cancelled(services, id) && ag_milliseconds() < deadline) {
        vm_media_status(services->media, &state);
        if (state.stop_requested) break;
        struct timespec pause = {0, 20000000};
        nanosleep(&pause, NULL);
    }
    rc = vm_media_record_stop(services->media);
    set_active(services, false);
    if (ag_cancelled(services, id)) rc = -ECANCELED;
    if (rc) {
        unlink(path);
        vm_media_status(services->media, &state);
        if (state.mode == VM_MEDIA_IDLE) {
            char partial[AG_PATH_MAX + 8];
            int n = snprintf(partial, sizeof(partial), "%s.part", path);
            if (n > 0 && (size_t)n < sizeof(partial)) unlink(partial);
        }
    }
    return rc;
}

void ag_service_media_close(ag_services_t *services)
{
    int rc = vm_media_close(services->media);
    if (rc) fprintf(stderr, "V821 media close: %s\n", strerror(-rc));
    services->media = NULL;
    set_active(services, false);
}
