#ifndef AIGLASS_INTERNAL_H
#define AIGLASS_INTERNAL_H
#include "aiglass_services.h"
#include "third_party/cJSON.h"
#include <pthread.h>
#include <stdatomic.h>
#include <sys/types.h>
#define AG_JOB_COUNT 8
#define AG_RESULT_COUNT 16
#define AG_HTTP_MAX (256U * 1024U)
#define AG_TRANSFER_MAX (32U * 1024U * 1024U)
typedef struct {
    uint32_t id;
    ag_operation_t operation;
    char text[AG_INPUT_MAX], argument[AG_PATH_MAX];
} ag_job_t;
struct ag_services {
    ag_services_config_t config;
    pthread_t worker, phone;
    pthread_mutex_t mutex;
    pthread_cond_t wake;
    atomic_bool stopping;
    atomic_uint cancelled;
    ag_job_t jobs[AG_JOB_COUNT];
    unsigned job_read, job_count;
    ag_result_t results[AG_RESULT_COUNT];
    unsigned result_read, result_count;
    uint32_t next_id;
    ag_status_t status;
    uint64_t phone_seen;
    int root_fd, unix_fd, tcp_fd, mode_owner_fd;
    vm_media_t *media; /* Owned exclusively by the services worker. */
    char media_path[AG_PATH_MAX];
    cJSON *history;
    char phone_credential[65];
    unsigned pairing_failures;
    uint64_t pairing_blocked_until;
};
uint64_t ag_milliseconds(void);
void ag_copy(char *destination, size_t size, const char *source);
void ag_publish(ag_services_t *s, const ag_result_t *result);
bool ag_cancelled(ag_services_t *s, uint32_t id);
bool ag_leaf_valid(const char *name);
int ag_folder_open(ag_services_t *s, const char *folder);
int ag_file_open(ag_services_t *s, const char *relative, int flags, mode_t mode);
int ag_write_all(int fd, const void *data, size_t size);
int ag_write_note(ag_services_t *s, const char *name, const char *text, bool replace);
int ag_list_files(ag_services_t *s, const char *folder, char *text, size_t size);
int ag_http_job(ag_services_t *s, const ag_job_t *job, ag_result_t *result);
int ag_capture_speech(ag_services_t *s, uint32_t id, char *path, size_t size);
int ag_service_media_start(ag_services_t *s, const ag_job_t *job, ag_result_t *result);
int ag_service_media_stop(ag_services_t *s, ag_result_t *result);
void ag_service_media_poll(ag_services_t *s);
void ag_service_media_close(ag_services_t *s);
void *ag_phone_thread(void *opaque);
int ag_phone_open(ag_services_t *s);
void ag_phone_close(ag_services_t *s);
int ag_pairing_init(ag_services_t *s);
int ag_pairing_claim(ag_services_t *s, const char *code, char credential[65]);
bool ag_pairing_authorized(ag_services_t *s, const char *credential);
#endif
