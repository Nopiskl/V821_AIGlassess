#define _GNU_SOURCE
#include "aiglass_internal.h"
#include "aiglass_json.h"
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/random.h>
#include <sys/stat.h>
#include <unistd.h>

#define BINDING_FILE ".phone-binding.json"

static int random_bytes(void *buffer, size_t size)
{
    unsigned char *out = buffer;
    while (size) {
        ssize_t n = getrandom(out, size, 0);
        if (n < 0) { if (errno == EINTR) continue; return -errno; }
        if (!n) return -EIO;
        out += n;
        size -= (size_t)n;
    }
    return 0;
}

static int random_hex(char *out, size_t bytes)
{
    unsigned char raw[32];
    if (bytes > sizeof(raw)) return -EINVAL;
    int rc = random_bytes(raw, bytes);
    if (rc) return rc;
    for (size_t i = 0; i < bytes; ++i) snprintf(out + i * 2, 3, "%02x", raw[i]);
    return 0;
}

static int new_code(char code[7])
{
    uint32_t number;
    int rc = random_bytes(&number, sizeof(number));
    if (!rc) snprintf(code, 7, "%06u", number % 1000000U);
    return rc;
}

static bool valid_hex(const char *text, size_t length)
{
    return text && strlen(text) == length && strspn(text, "0123456789abcdef") == length;
}

/* Called during initialization or with the mutex held; publish with one rename. */
static int save_binding(ag_services_t *s, const char *credential)
{
    char content[192], temporary[96];
    int n = snprintf(content, sizeof(content), "{\"device_id\":\"%s\",\"credential\":\"%s\"}\n",
                     s->status.device_id, credential);
    if (n < 0 || n >= (int)sizeof(content)) return -EOVERFLOW;
    snprintf(temporary, sizeof(temporary), ".binding-%ld-%llu.tmp", (long)getpid(),
             (unsigned long long)ag_milliseconds());
    int fd = openat(s->root_fd, temporary, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600);
    if (fd < 0) return -errno;
    int rc = ag_write_all(fd, content, (size_t)n);
    if (!rc && fsync(fd) < 0) rc = -errno;
    close(fd);
    if (!rc && renameat(s->root_fd, temporary, s->root_fd, BINDING_FILE) < 0) rc = -errno;
    if (rc) unlinkat(s->root_fd, temporary, 0);
    else if (fsync(s->root_fd) < 0) rc = -errno;
    return rc;
}

int ag_pairing_init(ag_services_t *s)
{
    int fd = openat(s->root_fd, BINDING_FILE, O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
    if (fd < 0 && errno != ENOENT) return -errno;
    if (fd >= 0) {
        struct stat st;
        char text[256];
        if (fstat(fd, &st) < 0) { int rc = -errno; close(fd); return rc; }
        if (!S_ISREG(st.st_mode) || st.st_uid != geteuid() || (st.st_mode & 0077) || st.st_size >= (off_t)sizeof(text)) {
            close(fd); return -EACCES;
        }
        size_t used = 0;
        while (used < (size_t)st.st_size) {
            ssize_t n = read(fd, text + used, (size_t)st.st_size - used);
            if (n < 0 && errno == EINTR) continue;
            if (n <= 0) {
                int rc = n < 0 ? -errno : -EIO;
                close(fd);
                return rc;
            }
            used += (size_t)n;
        }
        close(fd);
        text[used] = '\0';
        cJSON *record = ag_json_parse(text);
        cJSON *id = cJSON_GetObjectItemCaseSensitive(record, "device_id");
        cJSON *credential = cJSON_GetObjectItemCaseSensitive(record, "credential");
        bool valid = cJSON_IsString(id) && valid_hex(id->valuestring, 32) && cJSON_IsString(credential) &&
                     (!credential->valuestring[0] || valid_hex(credential->valuestring, 64));
        if (valid) {
            ag_copy(s->status.device_id, sizeof(s->status.device_id), id->valuestring);
            ag_copy(s->phone_credential, sizeof(s->phone_credential), credential->valuestring);
        }
        cJSON_Delete(record);
        if (!valid) return -EINVAL;
    } else {
        int rc = random_hex(s->status.device_id, 16);
        if (!rc) rc = save_binding(s, "");
        if (rc) return rc;
    }
    s->status.phone_paired = s->phone_credential[0] != '\0';
    return s->status.phone_paired ? 0 : new_code(s->status.pairing_code);
}

bool ag_pairing_authorized(ag_services_t *s, const char *credential)
{
    if (!credential || strlen(credential) != 64) return false;
    unsigned difference = 0;
    pthread_mutex_lock(&s->mutex);
    for (unsigned i = 0; i < 64; ++i) difference |= (unsigned char)credential[i] ^ (unsigned char)s->phone_credential[i];
    bool valid = s->status.phone_paired && !difference;
    pthread_mutex_unlock(&s->mutex);
    return valid;
}

int ag_pairing_claim(ag_services_t *s, const char *code, char credential[65])
{
    int rc = 0;
    pthread_mutex_lock(&s->mutex);
    if (s->status.phone_paired) rc = -EALREADY;
    else if (ag_milliseconds() < s->pairing_blocked_until) rc = -EAGAIN;
    else if (!code || strlen(code) != 6 || strcmp(code, s->status.pairing_code)) {
        rc = -EACCES;
        if (++s->pairing_failures >= 5) {
            s->pairing_blocked_until = ag_milliseconds() + 30000;
            s->pairing_failures = 0;
        }
    } else {
        char token[65];
        rc = random_hex(token, 32);
        if (!rc) rc = save_binding(s, token);
        if (!rc) {
            ag_copy(s->phone_credential, sizeof(s->phone_credential), token);
            ag_copy(credential, 65, token);
            s->status.phone_paired = true;
            s->status.pairing_code[0] = '\0';
            s->phone_seen = ag_milliseconds();
            s->pairing_failures = 0;
        }
    }
    pthread_mutex_unlock(&s->mutex);
    return rc;
}

int ag_services_unpair(ag_services_t *s)
{
    if (!s) return -EINVAL;
    char code[7];
    int rc = new_code(code);
    if (rc) return rc;
    pthread_mutex_lock(&s->mutex);
    rc = save_binding(s, "");
    if (!rc) {
        memset(s->phone_credential, 0, sizeof(s->phone_credential));
        s->status.phone_paired = false;
        s->phone_seen = 0;
        s->status.last_message[0] = '\0';
        ++s->status.message_sequence;
        s->pairing_failures = 0;
        s->pairing_blocked_until = 0;
        ag_copy(s->status.pairing_code, sizeof(s->status.pairing_code), code);
    }
    pthread_mutex_unlock(&s->mutex);
    return rc;
}
