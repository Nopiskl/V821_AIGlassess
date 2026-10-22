#ifndef __HTTP_H
#define __HTTP_H

#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <ctype.h>
#include <stdio.h>

#include "network.h"

#define http_malloc malloc
#define http_free   free
#define http_memset memset
#define http_memcpy memcpy
#define http_memcmp memcmp

/*********************/
#define HTTP_DEBUG 0
#if HTTP_DEBUG > 0
#define HTTP_LOGD(fmt, arg...) printf("[HTTP][DBG]"fmt, ##arg)
#else
#define HTTP_LOGD(fmt, arg...)
#endif
#define HTTP_LOGI(fmt, arg...) printf("[HTTP][INF]"fmt, ##arg)
#define HTTP_LOGE(fmt, arg...) printf("[HTTP][ERR]"fmt, ##arg)
/*********************/

#define BIT(I) (1 << I)
#define HTTP_CONNECT_WITH_SSL BIT(0)

#define CONFIG_DEFAULT_MAX_HTTP_URL_PATH_LEN 256
#define CONFIG_DEFAULT_MAX_HTTP_HOSTNAME_LEN 64
#define CONFIG_DEFAULT_MAX_HTTP_CONTENT_TYPE_LEN 64
#define CONFIG_DEFAULT_MAX_HTTP_EXTH_LEN 256
#define CONFIG_DEFAULT_MAX_HTTP_HEAD_LEN 1024

enum http_ret_code {
	HTTP_SUCCESS = 0,
	HTTP_ERROR = -1,
	HTTP_MEM_ERROR = -2,
	HTTP_EAGAIN = -3,
};

enum http_act_type {
	HTTP_REQUEST = 0,
	HTTP_RESPONSE = 1,
};

enum http_verb_type {
	GET = 0,
	POST = 1,

	UNKOWN_VERB
};

enum http_version {
	HTTP_1_0 = 0X10,
	HTTP_1_1 = 0X11,
};

enum http_conn_type {
	CLOSE = 1,
	KEEPALIVE = 2,
};

enum http_trans_ed_type {
	CHUNKED = 0,
	NOT_CHUNKED = 1,
};

enum http_role {
	HTTP_CLIENT = CLIENT,
	HTTP_SERVER = SERVER
};

enum http_status_code {
	HTTP_200_OK = 200,
	HTTP_301_MOV = 301,
	HTTP_302_FOUND = 302,
	HTTP_400_BAD = 400,
	HTTP_401_UNAUTH = 401,
	HTTP_404_NOT_FOUND = 404
};

enum http_status {
	HTTP_STATUS_INIT = 0,
	HTTP_STATUS_CONFIG,
	HTTP_STATUS_DEINIT,
	HTTP_STATUS_OPEN,
	HTTP_STATUS_CLOSE,
	HTTP_STATUS_WRITE,
	HTTP_STATUS_READ,
	HTTP_STATUS_RECV_RESPONSE,
	HTTP_STATUS_SEND_REQUEST,
	HTTP_STATUS_RECV_REQUEST,
	HTTP_STATUS_SEND_RESPONSE,
};

typedef void *(*network_init_t)(void);
typedef int (*network_connect_t)(void *network_ctx, void **conn, uint8_t is_blocking);
typedef int (*network_disconnect_t)(void *network_ctx, void *conn);
typedef int (*network_send_t)(void *network_ctx, void *conn, char *data, int data_size);
typedef int (*network_recv_t)(void *network_ctx, void *conn, char *recv_buffer, size_t read_bytes);
typedef int (*network_deinit_t)(void *network_ctx);

typedef struct {
	network_init_t init;
	network_deinit_t deinit;
	network_connect_t connect;
	network_disconnect_t disconnect;
	network_send_t send;
	network_recv_t recv;
	void *network_context;
} network_interface_t;

typedef struct {
	void *network_ctx;
	uint8_t flag;
	uint8_t	role;
	uint8_t	status;
} http_context_t;

typedef struct {
	char	verb;
	char	version;
	int		status_code;
	char	path[CONFIG_DEFAULT_MAX_HTTP_URL_PATH_LEN];
	char    host[CONFIG_DEFAULT_MAX_HTTP_HOSTNAME_LEN];
	char	connection;
	char	trans_encode;
	size_t	content_length;
	char	content_type[CONFIG_DEFAULT_MAX_HTTP_CONTENT_TYPE_LEN];
	char	ext_h[CONFIG_DEFAULT_MAX_HTTP_EXTH_LEN];
} http_head_info_t;

typedef struct {
	char	act_type;
	char	is_https;
	void *conn;
	char *write_ptr;
	char *read_ptr;
	char	header_buf[CONFIG_DEFAULT_MAX_HTTP_HEAD_LEN];
	size_t	header_len;
	char *body;
	size_t	body_len;
	http_head_info_t head_info;
	http_context_t *ctx;
} http_session_t;

typedef http_context_t HTTP_CTX;
typedef http_session_t HTTP_SESSION;

typedef struct {
	char	verb;
	char *ext_h;
	char *body;
	size_t	body_len;
} httpc_request_t;

typedef struct {
	int		status_code;
	char	trans_encode;
	char	content_type[CONFIG_DEFAULT_MAX_HTTP_CONTENT_TYPE_LEN];
	size_t	content_length;
} httpc_response_t;

HTTP_CTX *http_init(void);
int http_config(HTTP_CTX *http_ctx, uint8_t role, uint8_t use_ssl);
int http_deinit(HTTP_CTX *http_ctx);
HTTP_SESSION *http_open(HTTP_CTX *http_ctx, char *url, uint8_t is_blocking);
int http_close(HTTP_SESSION *session);
int http_sendfile(HTTP_SESSION *session, int *file_fd, int file_size, int tmo_ms);
int http_write(HTTP_SESSION *session, char *data, int data_size, int tmo_ms);
int http_read(HTTP_SESSION *session, char *recv_buffer, size_t read_bytes, int tmo_ms);
/* client */
int http_send_request(HTTP_SESSION *session);
int http_recv_response(HTTP_SESSION *session);
HTTP_SESSION *httpc_open(char *url);
int httpc_get(HTTP_SESSION *session, char *ext_h, httpc_response_t *response);
int httpc_post(HTTP_SESSION *session, char *ext_h, char *post_data, size_t size, httpc_response_t *response);
int httpc_read(HTTP_SESSION *session, char *recv_buffer, int read_bytes, int tmo_ms);
int httpc_close(HTTP_SESSION *session);
/* server */
int http_recv_request(HTTP_SESSION *session);
int http_send_response(HTTP_SESSION *session);
#endif /* __HTTP_H */
