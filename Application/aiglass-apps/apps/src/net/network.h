#ifndef __NETWORK_H
#define __NETWORK_H

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <netinet/in.h>
#include <unistd.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/sendfile.h>
#include <netdb.h>

/*********************/
#define NETWORK_DEBUG 0
#if NETWORK_DEBUG > 0
#define NT_LOGD(fmt, arg...) printf("[NETWORK][DBG]%s %d "fmt, __func__, __LINE__, ##arg)
#else
#define NT_LOGD(fmt, arg...)
#endif
#define NT_LOGI(fmt, arg...) printf("[NETWORK][INF]%s %d "fmt, __func__, __LINE__, ##arg)
#define NT_LOGE(fmt, arg...) printf("[NETWORK][ERR]%s %d "fmt, __func__, __LINE__, ##arg)
/*********************/

/*********************/
#define nt_malloc malloc
#define nt_memset memset
#define nt_memcpy memcpy
#define nt_free   free
/********************/

#define SUPPORT_SSL 0

#define BIT(I) (1 << I)
#define NETWORK_CONNECT_WITH_SSL BIT(0)
#define NETWORK_SOCKCTX_WITH_NONBLOCK BIT(1)

#define CLIENT 0
#define SERVER 1

#if SUPPORT_SSL
#include <openssl/ssl.h>
#include <openssl/err.h>
#define DEFAULT_SERVER_SSL_PORT 443
#define SERVER_CERT ("/server.crt")
#define SERVER_KEY ("/server.key")
#define CLIENT_CERT ("/client.crt")
#define CLIENT_KEY ("/client.key")
#define CA ("/ca.crt")
#endif
#define DEFAULT_SERVER_PORT 80

#define DEFAULT_SERVER_LISTEN_SYN_NUM 64

enum nt_ret_code {
	NT_SUCCESS = 0,
	NT_ERROR = -1,
	NT_MEM_ERROR = -2,
	NT_EAGAIN = -3,
};

typedef struct {
	int fd;
#if SUPPORT_SSL
	void *ssl;
#endif
	void *ctx;
} conn_t;

typedef struct {
	int sock_ctx;
#if SUPPORT_SSL
	void *ssl_ctx;
#endif
	uint32_t flag;
	uint8_t role;
} network_context_t;

#if SUPPORT_SSL
void *ssl_init(int role);
void ssl_config(void *_ssl_ctx, int verify_mode, verify_callback_t cb,
		const char *cert, const char *key, const char *ca);
void *ssl_open(void *_ssl_ctx, int fd, int role);
int ssl_close(void *_ssl);
void ssl_deinit(void *_ssl_ctx);
int ssl_write(void *_ssl, char *data, int data_size);
int ssl_read(void *_ssl, char *recv_buffer, size_t read_bytes);
#endif /* SUPPORT_SSL */

void *network_init(void);
int network_config(void *_network_ctx, uint8_t use_ssl, uint8_t role);
int network_deinit(void *_network_ctx);
void *network_open(void *_network_ctx, char *host_name, int16_t host_port, \
		uint8_t is_blocking);
int network_close(void *_conn);
int network_sendfile(void *_conn, int *_file_fd, int file_size, int tmo_ms);
int network_send(void *_conn, char *data, int data_size, int tmo_ms);
int network_recv(void *_conn, char *recv_buffer, size_t read_bytes, int tmo_ms);
#endif /* __NETWORK_H */
