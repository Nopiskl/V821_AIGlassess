#define _POSIX_C_SOURCE 200809L
#include "aiglass_internal.h"
#include "aiglass_json.h"
#include <curl/curl.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

static pthread_once_t curl_once = PTHREAD_ONCE_INIT;
static CURLcode curl_init_result;
static void initialize_curl(void) { curl_init_result = curl_global_init(CURL_GLOBAL_DEFAULT); }

typedef struct {
    ag_services_t *services;
    uint32_t id;
    char *data;
    size_t length, limit;
    int fd, error;
} transfer_t;

static size_t receive_data(char *data, size_t size, size_t count, void *opaque)
{
    transfer_t *t = opaque;
    size_t bytes;
    if (size && count > SIZE_MAX / size) { t->error = -EOVERFLOW; return 0; }
    bytes = size * count;
    if (bytes > t->limit - t->length) { t->error = -EFBIG; return 0; }
    if (t->fd >= 0) {
        t->error = ag_write_all(t->fd, data, bytes);
        if (t->error) return 0;
    } else {
        char *next = realloc(t->data, t->length + bytes + 1);
        if (!next) { t->error = -ENOMEM; return 0; }
        t->data = next;
        memcpy(next + t->length, data, bytes);
        next[t->length + bytes] = '\0';
    }
    t->length += bytes;
    return bytes;
}

static int transfer_progress(void *opaque, curl_off_t total_down, curl_off_t down,
                             curl_off_t total_up, curl_off_t up)
{
    transfer_t *t = opaque;
    (void)total_down; (void)down; (void)total_up; (void)up;
    return ag_cancelled(t->services, t->id) ? 1 : 0;
}

static bool web_url(const char *url)
{
    return url && (!strncmp(url, "https://", 8) || !strncmp(url, "http://", 7));
}

/* No redirect of bearer tokens; TLS verification uses libcurl's system CA store. */
static CURL *prepare_transfer(transfer_t *t, const char *url)
{
    CURL *curl;
    if (!web_url(url)) { t->error = -EINVAL; return NULL; }
    pthread_once(&curl_once, initialize_curl);
    if (curl_init_result != CURLE_OK) { t->error = -EIO; return NULL; }
    curl = curl_easy_init();
    if (!curl) { t->error = -ENOMEM; return NULL; }
#define OPTION(option, value) do { if (curl_easy_setopt(curl, option, value) != CURLE_OK) goto fail; } while (0)
    OPTION(CURLOPT_URL, url);
    OPTION(CURLOPT_NOSIGNAL, 1L);
    OPTION(CURLOPT_CONNECTTIMEOUT_MS, 5000L);
    OPTION(CURLOPT_TIMEOUT_MS, 30000L);
    OPTION(CURLOPT_LOW_SPEED_LIMIT, 16L);
    OPTION(CURLOPT_LOW_SPEED_TIME, 10L);
    OPTION(CURLOPT_FOLLOWLOCATION, 0L);
    OPTION(CURLOPT_SSL_VERIFYPEER, 1L);
    OPTION(CURLOPT_SSL_VERIFYHOST, 2L);
#if LIBCURL_VERSION_NUM >= 0x075500
    CURLcode protocols_rc = curl_easy_setopt(curl, CURLOPT_PROTOCOLS_STR, "http,https");
    /* New headers can be used with an older ABI-compatible libcurl runtime. */
    if (protocols_rc == CURLE_UNKNOWN_OPTION) {
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
        protocols_rc = curl_easy_setopt(curl, CURLOPT_PROTOCOLS, (long)(CURLPROTO_HTTP | CURLPROTO_HTTPS));
#pragma GCC diagnostic pop
    }
    if (protocols_rc != CURLE_OK) goto fail;
#else
    OPTION(CURLOPT_PROTOCOLS, (long)(CURLPROTO_HTTP | CURLPROTO_HTTPS));
#endif
    OPTION(CURLOPT_WRITEFUNCTION, receive_data);
    OPTION(CURLOPT_WRITEDATA, t);
    OPTION(CURLOPT_NOPROGRESS, 0L);
    OPTION(CURLOPT_XFERINFOFUNCTION, transfer_progress);
    OPTION(CURLOPT_XFERINFODATA, t);
    OPTION(CURLOPT_USERAGENT, "V821-AIGlass/1.0");
#undef OPTION
    return curl;
fail:
    curl_easy_cleanup(curl);
    t->error = -EIO;
    return NULL;
}

static int complete_transfer(CURL *curl, transfer_t *t, ag_result_t *out)
{
    CURLcode code = curl_easy_perform(curl);
    long status = 0;
    if (ag_cancelled(t->services, t->id)) return -ECANCELED;
    if (t->error) return t->error;
    if (code != CURLE_OK) {
        ag_copy(out->text, sizeof(out->text), curl_easy_strerror(code));
        return code == CURLE_OPERATION_TIMEDOUT ? -ETIMEDOUT : -EIO;
    }
    if (curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status) != CURLE_OK) return -EIO;
    if (status < 200 || status >= 300) {
        snprintf(out->text, sizeof(out->text), "HTTP %ld", status);
        return -EPROTO;
    }
    return 0;
}

static const char *json_string(cJSON *object, const char *key)
{
    cJSON *value = cJSON_GetObjectItemCaseSensitive(object, key);
    return cJSON_IsString(value) ? value->valuestring : NULL;
}

static int response_text(const char *data, char *out, size_t size)
{
    cJSON *root = ag_json_parse(data ? data : "");
    const char *text;
    if (!root) return -EPROTO;
    text = json_string(root, "text");
    if (!text) {
        cJSON *choices = cJSON_GetObjectItemCaseSensitive(root, "choices");
        cJSON *message = cJSON_GetObjectItemCaseSensitive(cJSON_GetArrayItem(choices, 0), "message");
        text = json_string(message, "content");
    }
    if (text && *text) ag_copy(out, size, text);
    else { cJSON_Delete(root); return -EPROTO; }
    cJSON_Delete(root);
    return 0;
}

static struct curl_slist *request_headers(ag_services_t *s, bool json)
{
    struct curl_slist *headers = NULL, *next;
    char authorization[300];
    headers = curl_slist_append(NULL, "Accept: application/json");
    if (!headers) return NULL;
    if (json) {
        next = curl_slist_append(headers, "Content-Type: application/json");
        if (!next) goto fail;
        headers = next;
    }
    if (s->config.api_key[0]) {
        snprintf(authorization, sizeof(authorization), "Authorization: Bearer %s", s->config.api_key);
        next = curl_slist_append(headers, authorization);
        if (!next) goto fail;
        headers = next;
    }
    return headers;
fail:
    curl_slist_free_all(headers);
    return NULL;
}

static bool add_message(cJSON *messages, const char *role, const char *text)
{
    cJSON *item = cJSON_CreateObject();
    if (!item || !cJSON_AddStringToObject(item, "role", role) ||
        !cJSON_AddStringToObject(item, "content", text) || !cJSON_AddItemToArray(messages, item)) {
        cJSON_Delete(item); return false;
    }
    return true;
}

static int chat_request(ag_services_t *s, const ag_job_t *job, const char *input,
                        bool translation, ag_result_t *out)
{
    cJSON *root = NULL, *messages = NULL;
    char *body = NULL;
    char instruction[256];
    struct curl_slist *headers = NULL;
    transfer_t transfer = {.services = s, .id = job->id, .limit = AG_HTTP_MAX, .fd = -1};
    CURL *curl = NULL;
    int rc = -ENOMEM;
    if (!s->config.chat_url[0]) { ag_copy(out->text, sizeof(out->text), "AI service is not configured"); return -ENOTCONN; }
    if (!input || !*input) return -EINVAL;
    root = cJSON_CreateObject();
    if (!root) goto done;
    messages = cJSON_AddArrayToObject(root, "messages");
    if (!messages || !cJSON_AddBoolToObject(root, "stream", false)) goto done;
    if (s->config.model[0] && !cJSON_AddStringToObject(root, "model", s->config.model)) goto done;
    if (translation) {
        const char *language = !strcmp(job->argument, "zh") ? "Simplified Chinese" :
                               !strcmp(job->argument, "ja") ? "Japanese" : "English";
        snprintf(instruction, sizeof(instruction), "Translate the user text into %s. Return only the translation, with no explanation.", language);
    } else ag_copy(instruction, sizeof(instruction), "You are an assistant on smart glasses. Reply concisely in the user's language.");
    if (!add_message(messages, "system", instruction)) goto done;
    if (!translation) {
        cJSON *item;
        cJSON_ArrayForEach(item, s->history) {
            cJSON *copy = cJSON_Duplicate(item, true);
            if (!copy || !cJSON_AddItemToArray(messages, copy)) { cJSON_Delete(copy); goto done; }
        }
    }
    if (!add_message(messages, "user", input)) goto done;
    body = cJSON_PrintUnformatted(root);
    headers = request_headers(s, true);
    if (!body || !headers) goto done;
    curl = prepare_transfer(&transfer, s->config.chat_url);
    if (!curl) { rc = transfer.error; goto done; }
    if (curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers) != CURLE_OK ||
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body) != CURLE_OK) { rc = -EIO; goto done; }
    rc = complete_transfer(curl, &transfer, out);
    if (!rc) rc = response_text(transfer.data, out->text, sizeof(out->text));
    if (!rc && !translation) {
        /* A failed allocation clears history rather than retaining an unmatched turn. */
        if (!add_message(s->history, "user", input) || !add_message(s->history, "assistant", out->text)) {
            while (cJSON_GetArraySize(s->history)) cJSON_DeleteItemFromArray(s->history, 0);
        }
        while (cJSON_GetArraySize(s->history) > 8) cJSON_DeleteItemFromArray(s->history, 0);
    }
done:
    if (curl) curl_easy_cleanup(curl);
    curl_slist_free_all(headers);
    free(transfer.data);
    free(body);
    cJSON_Delete(root);
    return rc;
}

static int speech_request(ag_services_t *s, const ag_job_t *job, ag_result_t *out)
{
    char path[AG_PATH_MAX] = "", transcript[AG_INPUT_MAX];
    CURL *curl = NULL;
    curl_mime *mime = NULL;
    struct curl_slist *headers = NULL;
    transfer_t transfer = {.services = s, .id = job->id, .limit = AG_HTTP_MAX, .fd = -1};
    int rc;
    if (!s->config.asr_url[0] || !s->config.chat_url[0]) {
        ag_copy(out->text, sizeof(out->text), "Speech/AI service is not configured"); return -ENOTCONN;
    }
    rc = ag_capture_speech(s, job->id, path, sizeof(path));
    if (rc) return rc;
    curl = prepare_transfer(&transfer, s->config.asr_url);
    if (!curl) { rc = transfer.error; goto done; }
    mime = curl_mime_init(curl);
    headers = request_headers(s, false);
    if (!mime || !headers) { rc = -ENOMEM; goto done; }
    curl_mimepart *part = curl_mime_addpart(mime);
    if (!part || curl_mime_name(part, "file") != CURLE_OK ||
        curl_mime_filedata(part, path) != CURLE_OK || curl_mime_type(part, s->config.asr_mime) != CURLE_OK) { rc = -EIO; goto done; }
    if (s->config.asr_model[0]) {
        part = curl_mime_addpart(mime);
        if (!part || curl_mime_name(part, "model") != CURLE_OK ||
            curl_mime_data(part, s->config.asr_model, CURL_ZERO_TERMINATED) != CURLE_OK) { rc = -EIO; goto done; }
    }
    if (curl_easy_setopt(curl, CURLOPT_MIMEPOST, mime) != CURLE_OK ||
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers) != CURLE_OK) { rc = -EIO; goto done; }
    rc = complete_transfer(curl, &transfer, out);
    if (!rc) rc = response_text(transfer.data, transcript, sizeof(transcript));
    if (!rc) {
        rc = chat_request(s, job, transcript, job->operation == AG_VOICE_TRANSLATE, out);
        if (!rc) ag_copy(out->detail, sizeof(out->detail), transcript);
    }
done:
    if (mime) curl_mime_free(mime);
    if (curl) curl_easy_cleanup(curl);
    curl_slist_free_all(headers);
    free(transfer.data);
    if (path[0]) unlink(path);
    return rc;
}

/* A deliberately text-only reader; no scripts, embedded resources or HTML layout. */
static void html_text(const char *input, char *output, size_t size)
{
    size_t used = 0;
    bool tag = false, skip_script = false, skip_style = false;
    const char *p = input;
    while (*p && used + 1 < size) {
        if (*p == '<') {
            if (!strncasecmp(p, "<script", 7)) skip_script = true;
            if (!strncasecmp(p, "<style", 6)) skip_style = true;
            if (!strncasecmp(p, "</script", 8)) skip_script = false;
            if (!strncasecmp(p, "</style", 7)) skip_style = false;
            tag = true;
            if (used && output[used - 1] != '\n') output[used++] = '\n';
        } else if (*p == '>') tag = false;
        else if (!tag && !skip_script && !skip_style) {
            if (*p == '&') {
                struct entity { const char *name; char value; };
                static const struct entity entities[] = {{"&amp;", '&'}, {"&lt;", '<'}, {"&gt;", '>'}, {"&quot;", '"'}, {"&nbsp;", ' '}};
                bool found = false;
                for (unsigned i = 0; i < sizeof(entities) / sizeof(entities[0]); ++i) {
                    size_t length = strlen(entities[i].name);
                    if (!strncmp(p, entities[i].name, length)) { output[used++] = entities[i].value; p += length; found = true; break; }
                }
                if (found) continue;
            }
            if (!isspace((unsigned char)*p) || !used || !isspace((unsigned char)output[used - 1])) output[used++] = *p;
        }
        ++p;
    }
    output[used] = '\0';
}

static int fetch_document(ag_services_t *s, const ag_job_t *job, ag_result_t *out)
{
    bool download = job->operation == AG_DOWNLOAD;
    transfer_t transfer = {.services = s, .id = job->id,
        .limit = download ? AG_TRANSFER_MAX : AG_HTTP_MAX, .fd = -1};
    CURL *curl = NULL;
    int dir = -1, rc = 0;
    char temporary[64];
    if (!web_url(job->text)) return -EINVAL;
    if (download) {
        if (!ag_leaf_valid(job->argument)) return -EINVAL;
        dir = ag_folder_open(s, "inbox");
        if (dir < 0) return -errno;
        snprintf(temporary, sizeof(temporary), ".download-%llu-%u.tmp",
                 (unsigned long long)ag_milliseconds(), job->id);
        transfer.fd = openat(dir, temporary, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600);
        if (transfer.fd < 0) { close(dir); return -errno; }
    }
    curl = prepare_transfer(&transfer, job->text);
    rc = curl ? complete_transfer(curl, &transfer, out) : transfer.error;
    if (download) {
        if (!rc && fsync(transfer.fd) < 0) rc = -errno;
        close(transfer.fd);
        /* Atomic publication, without replacing existing user files. */
        if (!rc && linkat(dir, temporary, dir, job->argument, 0) < 0) rc = -errno;
        unlinkat(dir, temporary, 0);
        if (!rc && fsync(dir) < 0) rc = -errno;
        close(dir);
        if (!rc) {
            snprintf(out->text, sizeof(out->text), "Downloaded %zu bytes", transfer.length);
            snprintf(out->detail, sizeof(out->detail), "inbox/%.120s", job->argument);
        }
    } else if (!rc) html_text(transfer.data ? transfer.data : "", out->text, sizeof(out->text));
    if (curl) curl_easy_cleanup(curl);
    free(transfer.data);
    return rc;
}

int ag_http_job(ag_services_t *s, const ag_job_t *job, ag_result_t *out)
{
    switch (job->operation) {
    case AG_CHAT: return chat_request(s, job, job->text, false, out);
    case AG_TRANSLATE: return chat_request(s, job, job->text, true, out);
    case AG_VOICE_CHAT: case AG_VOICE_TRANSLATE: return speech_request(s, job, out);
    case AG_DOWNLOAD: case AG_BROWSE: return fetch_document(s, job, out);
    default: return -ENOTSUP;
    }
}
