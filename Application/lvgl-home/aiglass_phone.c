#define _GNU_SOURCE
#include "aiglass_internal.h"
#include "aiglass_json.h"
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

#define PHONE_LINE_MAX 16384
#define PHONE_CLIENTS 4
#define PHONE_CHUNK 4096

typedef struct {
    int fd;
    bool tcp;
    size_t used;
    uint64_t touched;
    char line[PHONE_LINE_MAX];
} phone_client_t;

static const char *string_value(cJSON *root, const char *key)
{
    cJSON *item = cJSON_GetObjectItemCaseSensitive(root, key);
    return cJSON_IsString(item) ? item->valuestring : "";
}

static bool same_token(const char *a, const char *b)
{
    size_t al = strlen(a), bl = strlen(b);
    unsigned different = (unsigned)(al ^ bl);
    for (size_t i = 0; i < bl; ++i) different |= (unsigned char)(i < al ? a[i] : 0) ^ (unsigned char)b[i];
    return different == 0;
}

static int json_offset(cJSON *root, size_t *offset)
{
    cJSON *value = cJSON_GetObjectItemCaseSensitive(root, "offset");
    if (!cJSON_IsNumber(value) || !isfinite(value->valuedouble) || value->valuedouble < 0 ||
        value->valuedouble > AG_TRANSFER_MAX) return -EINVAL;
    *offset = (size_t)value->valuedouble;
    return (double)*offset == value->valuedouble ? 0 : -EINVAL;
}

static int hex_digit(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static int upload_chunk(ag_services_t *s, cJSON *request, cJSON *reply)
{
    const char *name = string_value(request, "name");
    const char *hex = string_value(request, "data_hex");
    unsigned char bytes[PHONE_CHUNK];
    char part[160];
    size_t offset, length = strlen(hex);
    struct stat st;
    int dir, fd, rc;
    if (!ag_leaf_valid(name) || length % 2 || length > sizeof(bytes) * 2 || json_offset(request, &offset)) return -EINVAL;
    length /= 2;
    if (offset + length > AG_TRANSFER_MAX) return -EFBIG;
    for (size_t i = 0; i < length; ++i) {
        int high = hex_digit(hex[i * 2]), low = hex_digit(hex[i * 2 + 1]);
        if (high < 0 || low < 0) return -EINVAL;
        bytes[i] = (unsigned char)((high << 4) | low);
    }
    snprintf(part, sizeof(part), ".incoming-%s.part", name);
    dir = ag_folder_open(s, "inbox");
    if (dir < 0) return -errno;
    fd = openat(dir, part, O_WRONLY | O_NOFOLLOW | O_CLOEXEC | O_NONBLOCK |
                (offset ? 0 : O_CREAT | O_TRUNC), 0600);
    if (fd < 0) { close(dir); return -errno; }
    rc = fstat(fd, &st) < 0 ? -errno : 0;
    if (!rc && (!S_ISREG(st.st_mode) || (uint64_t)st.st_size != offset)) rc = -EINVAL;
    if (!rc && lseek(fd, (off_t)offset, SEEK_SET) < 0) rc = -errno;
    if (!rc) rc = ag_write_all(fd, bytes, length);
    bool final = cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(request, "final"));
    if (!rc && final && fsync(fd) < 0) rc = -errno;
    close(fd);
    if (!rc && final) {
        if (linkat(dir, part, dir, name, 0) < 0) rc = -errno;
        else unlinkat(dir, part, 0);
        if (!rc && fsync(dir) < 0) rc = -errno;
    }
    close(dir);
    if (!rc) {
        pthread_mutex_lock(&s->mutex); s->status.bytes_received += length; pthread_mutex_unlock(&s->mutex);
        cJSON_AddNumberToObject(reply, "offset", (double)(offset + length));
        cJSON_AddBoolToObject(reply, "final", final);
        if (final) {
            ag_result_t event = {.operation = AG_PHONE_MESSAGE};
            snprintf(event.text, sizeof(event.text), "File received: %s", name);
            snprintf(event.detail, sizeof(event.detail), "inbox/%s", name);
            ag_publish(s, &event);
        }
    }
    return rc;
}

static int download_chunk(ag_services_t *s, cJSON *request, cJSON *reply)
{
    unsigned char data[PHONE_CHUNK];
    char hex[PHONE_CHUNK * 2 + 1];
    static const char digits[] = "0123456789abcdef";
    size_t offset;
    struct stat st;
    int fd;
    ssize_t n;
    if (json_offset(request, &offset)) return -EINVAL;
    fd = ag_file_open(s, string_value(request, "path"), O_RDONLY, 0);
    if (fd < 0) return -errno;
    if (fstat(fd, &st) < 0) { int error = errno; close(fd); return -error; }
    if (offset > (uint64_t)st.st_size || (uint64_t)st.st_size > AG_TRANSFER_MAX) { close(fd); return -EFBIG; }
    n = pread(fd, data, sizeof(data), (off_t)offset);
    close(fd);
    if (n < 0) return -errno;
    for (ssize_t i = 0; i < n; ++i) { hex[i * 2] = digits[data[i] >> 4]; hex[i * 2 + 1] = digits[data[i] & 15]; }
    hex[n * 2] = '\0';
    if (!cJSON_AddStringToObject(reply, "data_hex", hex)) return -ENOMEM;
    cJSON_AddNumberToObject(reply, "offset", (double)(offset + (size_t)n));
    cJSON_AddBoolToObject(reply, "final", offset + (size_t)n >= (uint64_t)st.st_size);
    pthread_mutex_lock(&s->mutex); s->status.bytes_sent += (uint64_t)n; pthread_mutex_unlock(&s->mutex);
    return 0;
}

/* Mutations use the same flat directories and filename checks as the LVGL UI. */
static int mutate_file(ag_services_t *s, cJSON *request, bool rename_file)
{
    const char *path = string_value(request, "path");
    int check = ag_file_open(s, path, O_RDONLY, 0);
    if (check < 0) return -errno;
    close(check);
    const char *slash = strchr(path, '/');
    char folder[16];
    size_t length = (size_t)(slash - path);
    memcpy(folder, path, length); folder[length] = '\0';
    int dir = ag_folder_open(s, folder);
    if (dir < 0) return -errno;
    int rc = 0;
    if (rename_file) {
        const char *name = string_value(request, "name");
        if (!ag_leaf_valid(name)) rc = -EINVAL;
        else if (renameat2(dir, slash + 1, dir, name, RENAME_NOREPLACE) < 0) rc = -errno;
    } else if (unlinkat(dir, slash + 1, 0) < 0) rc = -errno;
    if (!rc && fsync(dir) < 0) rc = -errno;
    close(dir);
    return rc;
}

static int enqueue_message(ag_services_t *s, const ag_result_t *event)
{
    int rc = 0;
    pthread_mutex_lock(&s->mutex);
    if (s->result_count == AG_RESULT_COUNT) rc = -EAGAIN;
    else {
        s->results[(s->result_read + s->result_count) % AG_RESULT_COUNT] = *event;
        ++s->result_count;
    }
    pthread_mutex_unlock(&s->mutex);
    return rc;
}

static int dispatch(ag_services_t *s, cJSON *request, cJSON *reply)
{
    const char *op = string_value(request, "op");
    const char *text = string_value(request, "text");
    if (strcmp(op, "hello") && strcmp(op, "pair") &&
        !ag_pairing_authorized(s, string_value(request, "credential"))) return -EACCES;
    if (!strcmp(op, "pair")) {
        char credential[65];
        int rc = ag_pairing_claim(s, string_value(request, "code"), credential);
        if (!rc) cJSON_AddStringToObject(reply, "credential", credential);
        return rc;
    }
    if (!strcmp(op, "unpair")) return ag_services_unpair(s);
    if (!strcmp(op, "hello") || !strcmp(op, "ping") || !strcmp(op, "status")) {
        ag_status_t status;
        ag_services_status(s, &status);
        cJSON_AddStringToObject(reply, "device", "V821 AI Glass");
        cJSON_AddNumberToObject(reply, "version", 2);
        cJSON_AddStringToObject(reply, "device_id", status.device_id);
        cJSON_AddBoolToObject(reply, "paired", status.phone_paired);
        cJSON_AddNumberToObject(reply, "battery", status.battery_percent);
        cJSON_AddStringToObject(reply, "runtime", status.hardware_available ? "v821" : "linux-host");
        cJSON_AddStringToObject(reply, "address", status.address);
        cJSON_AddNumberToObject(reply, "bytes_received", (double)status.bytes_received);
        cJSON_AddNumberToObject(reply, "bytes_sent", (double)status.bytes_sent);
        cJSON_AddStringToObject(reply, "navigation", status.navigation);
        cJSON *capabilities = cJSON_AddObjectToObject(reply, "capabilities");
        cJSON_AddBoolToObject(capabilities, "files", true);
        cJSON_AddBoolToObject(capabilities, "notes", true);
        cJSON_AddBoolToObject(capabilities, "rename", true);
        cJSON_AddBoolToObject(capabilities, "delete", true);
        cJSON_AddBoolToObject(capabilities, "media_control", false);
        cJSON_AddBoolToObject(capabilities, "wifi_provisioning", false);
    } else if (!strcmp(op, "notify") || !strcmp(op, "input")) {
        ag_result_t event = {.operation = AG_PHONE_MESSAGE};
        if (!*text || strlen(text) >= AG_INPUT_MAX) return -E2BIG;
        ag_copy(event.text, sizeof(event.text), text);
        if (!strcmp(op, "input")) {
            const char *target = string_value(request, "target");
            if (strcmp(target, "chat") && strcmp(target, "translate")) return -EINVAL;
            ag_copy(event.detail, sizeof(event.detail), target);
        }
        int rc = enqueue_message(s, &event);
        if (!rc) {
            pthread_mutex_lock(&s->mutex);
            ag_copy(s->status.last_message, sizeof(s->status.last_message), text);
            ++s->status.message_sequence;
            pthread_mutex_unlock(&s->mutex);
        }
        return rc;
    } else if (!strcmp(op, "navigation")) {
        cJSON *speed = cJSON_GetObjectItemCaseSensitive(request, "speed_kmh");
        cJSON *distance = cJSON_GetObjectItemCaseSensitive(request, "distance_km");
        if (!cJSON_IsNumber(speed) || !cJSON_IsNumber(distance) ||
            !isfinite(speed->valuedouble) || !isfinite(distance->valuedouble) ||
            speed->valuedouble < 0 || speed->valuedouble > 1000 ||
            distance->valuedouble < 0 || distance->valuedouble > 10000000 || strlen(text) >= 256) return -EINVAL;
        pthread_mutex_lock(&s->mutex);
        s->status.speed_kmh = speed->valuedouble;
        s->status.distance_km = distance->valuedouble;
        ag_copy(s->status.navigation, sizeof(s->status.navigation), text);
        pthread_mutex_unlock(&s->mutex);
    } else if (!strcmp(op, "list")) {
        char text_buffer[AG_TEXT_MAX];
        int rc = ag_list_files(s, string_value(request, "folder"), text_buffer, sizeof(text_buffer));
        if (rc) return rc;
        cJSON *items = ag_json_parse(text_buffer);
        if (!items || !cJSON_AddItemToObject(reply, "files", items)) { cJSON_Delete(items); return -ENOMEM; }
    } else if (!strcmp(op, "note")) {
        const char *name = string_value(request, "name");
        if (!*text) return -EINVAL;
        int rc = ag_write_note(s, name, text, false);
        if (!rc) {
            ag_result_t event = {.operation = AG_PHONE_MESSAGE};
            snprintf(event.text, sizeof(event.text), "Note received: %s", name);
            snprintf(event.detail, sizeof(event.detail), "notes/%s", name);
            ag_publish(s, &event);
            cJSON_AddStringToObject(reply, "path", event.detail);
        }
        return rc;
    } else if (!strcmp(op, "rename") || !strcmp(op, "delete")) {
        int rc = mutate_file(s, request, !strcmp(op, "rename"));
        if (!rc) {
            ag_result_t event = {.operation = AG_PHONE_MESSAGE};
            ag_copy(event.detail, sizeof(event.detail), string_value(request, "path"));
            ag_copy(event.text, sizeof(event.text), "Phone updated files");
            ag_publish(s, &event);
        }
        return rc;
    } else if (!strcmp(op, "abort_put")) {
        const char *name = string_value(request, "name");
        if (!ag_leaf_valid(name)) return -EINVAL;
        char part[160]; snprintf(part, sizeof(part), ".incoming-%s.part", name);
        int dir = ag_folder_open(s, "inbox");
        if (dir < 0) return -errno;
        int rc = unlinkat(dir, part, 0) < 0 && errno != ENOENT ? -errno : 0;
        close(dir); return rc;
    } else if (!strcmp(op, "put")) return upload_chunk(s, request, reply);
    else if (!strcmp(op, "get")) return download_chunk(s, request, reply);
    else return -ENOTSUP;
    return 0;
}

static int send_reply(ag_services_t *s, int fd, const char *data)
{
    size_t size = strlen(data), sent = 0;
    uint64_t deadline = ag_milliseconds() + 2000;
    while (sent < size) {
        ssize_t n = send(fd, data + sent, size - sent, MSG_NOSIGNAL | MSG_DONTWAIT);
        if (n > 0) { sent += (size_t)n; continue; }
        if (n < 0 && errno != EINTR && errno != EAGAIN && errno != EWOULDBLOCK) return -errno;
        if (atomic_load(&s->stopping) || ag_milliseconds() >= deadline) return -ETIMEDOUT;
        struct pollfd pollfd = {.fd = fd, .events = POLLOUT};
        poll(&pollfd, 1, 50);
    }
    return send(fd, "\n", 1, MSG_NOSIGNAL | MSG_DONTWAIT) == 1 ? 0 : -EIO;
}

static int process_line(ag_services_t *s, phone_client_t *client)
{
    cJSON *request = ag_json_parse(client->line), *reply = cJSON_CreateObject();
    int rc = -EINVAL, sent = -ENOMEM;
    if (!reply) { cJSON_Delete(request); return -ENOMEM; }
    if (cJSON_IsObject(request)) {
        cJSON *id = cJSON_GetObjectItemCaseSensitive(request, "id");
        if (cJSON_IsNumber(id) || cJSON_IsString(id)) cJSON_AddItemToObject(reply, "id", cJSON_Duplicate(id, true));
        if (client->tcp && !same_token(string_value(request, "token"), s->config.phone_token)) rc = -EACCES;
        else {
            rc = dispatch(s, request, reply);
            if (!rc && strcmp(string_value(request, "op"), "unpair") &&
                ag_pairing_authorized(s, string_value(request, "credential"))) {
                pthread_mutex_lock(&s->mutex);
                s->phone_seen = ag_milliseconds();
                pthread_mutex_unlock(&s->mutex);
            }
        }
    }
    cJSON_AddBoolToObject(reply, "ok", rc == 0);
    if (rc) { cJSON_AddNumberToObject(reply, "code", rc); cJSON_AddStringToObject(reply, "error", strerror(-rc)); }
    char *serialized = cJSON_PrintUnformatted(reply);
    if (serialized) { sent = send_reply(s, client->fd, serialized); free(serialized); }
    cJSON_Delete(request);
    cJSON_Delete(reply);
    return sent;
}

int ag_phone_open(ag_services_t *s)
{
    struct sockaddr_un local = {.sun_family = AF_UNIX};
    int fd, rc;
    if (strlen(s->config.socket_path) >= sizeof(local.sun_path)) return -ENAMETOOLONG;
    if (s->config.phone_port && strlen(s->config.phone_token) < 16) return -EINVAL;
    fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);
    if (fd < 0) return -errno;
    ag_copy(local.sun_path, sizeof(local.sun_path), s->config.socket_path);
    if (bind(fd, (struct sockaddr *)&local, sizeof(local)) < 0) { rc = -errno; close(fd); return rc; }
    s->unix_fd = fd;
    if (chmod(local.sun_path, 0600) < 0 || listen(fd, PHONE_CLIENTS) < 0) { rc = -errno; ag_phone_close(s); return rc; }
    if (s->config.phone_port) {
        struct sockaddr_in address = {.sin_family = AF_INET, .sin_port = htons(s->config.phone_port)};
        if (inet_pton(AF_INET, s->config.phone_bind, &address.sin_addr) != 1) { ag_phone_close(s); return -EINVAL; }
        fd = socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);
        if (fd < 0) { rc = -errno; ag_phone_close(s); return rc; }
        s->tcp_fd = fd;
        if (bind(fd, (struct sockaddr *)&address, sizeof(address)) < 0 || listen(fd, PHONE_CLIENTS) < 0) {
            rc = -errno; ag_phone_close(s); return rc;
        }
    }
    return 0;
}

void ag_phone_close(ag_services_t *s)
{
    if (s->unix_fd >= 0) { close(s->unix_fd); s->unix_fd = -1; unlink(s->config.socket_path); }
    if (s->tcp_fd >= 0) { close(s->tcp_fd); s->tcp_fd = -1; }
}

void *ag_phone_thread(void *opaque)
{
    ag_services_t *s = opaque;
    phone_client_t *clients = calloc(PHONE_CLIENTS, sizeof(*clients));
    if (!clients) {
        ag_result_t event = {.operation = AG_PHONE_STATUS, .code = -ENOMEM};
        ag_copy(event.text, sizeof(event.text), "Phone service: out of memory");
        ag_publish(s, &event);
        return NULL;
    }
    for (unsigned i = 0; i < PHONE_CLIENTS; ++i) clients[i].fd = -1;
    while (!atomic_load(&s->stopping)) {
        struct pollfd fds[PHONE_CLIENTS + 2] = {
            {.fd = s->unix_fd, .events = POLLIN}, {.fd = s->tcp_fd, .events = POLLIN}
        };
        for (unsigned i = 0; i < PHONE_CLIENTS; ++i) { fds[i + 2].fd = clients[i].fd; fds[i + 2].events = POLLIN; }
        int ready = poll(fds, PHONE_CLIENTS + 2, 100);
        if (ready < 0) { if (errno == EINTR) continue; break; }
        for (unsigned listener = 0; listener < 2; ++listener) {
            if (!(fds[listener].revents & POLLIN)) continue;
            int fd = accept4(fds[listener].fd, NULL, NULL, SOCK_CLOEXEC | SOCK_NONBLOCK);
            if (fd < 0) continue;
            if (!listener) {
                struct ucred credentials;
                socklen_t length = sizeof(credentials);
                if (getsockopt(fd, SOL_SOCKET, SO_PEERCRED, &credentials, &length) < 0 || credentials.uid != geteuid()) { close(fd); continue; }
            }
            unsigned slot;
            for (slot = 0; slot < PHONE_CLIENTS && clients[slot].fd >= 0; ++slot) {}
            if (slot == PHONE_CLIENTS) { close(fd); continue; }
            clients[slot].fd = fd;
            clients[slot].tcp = listener != 0;
            clients[slot].used = 0;
            clients[slot].touched = ag_milliseconds();
        }
        for (unsigned i = 0; i < PHONE_CLIENTS; ++i) {
            phone_client_t *client = &clients[i];
            if (client->fd < 0) continue;
            bool close_client = false;
            if (fds[i + 2].revents & POLLIN) {
                char chunk[2048];
                ssize_t n = recv(client->fd, chunk, sizeof(chunk), 0);
                if (n <= 0) close_client = n == 0 || (errno != EINTR && errno != EAGAIN && errno != EWOULDBLOCK);
                for (ssize_t j = 0; j < n && !close_client; ++j) {
                    if (!chunk[j] || client->used + 1 >= sizeof(client->line)) { close_client = true; break; }
                    if (chunk[j] == '\n') {
                        client->line[client->used] = '\0';
                        close_client = process_line(s, client) < 0;
                        client->used = 0;
                    } else client->line[client->used++] = chunk[j];
                }
                if (n > 0) client->touched = ag_milliseconds();
            }
            if ((fds[i + 2].revents & (POLLERR | POLLHUP | POLLNVAL)) ||
                ag_milliseconds() - client->touched > 30000) close_client = true;
            if (close_client) { close(client->fd); client->fd = -1; }
        }
    }
    for (unsigned i = 0; i < PHONE_CLIENTS; ++i) if (clients[i].fd >= 0) close(clients[i].fd);
    free(clients);
    return NULL;
}
