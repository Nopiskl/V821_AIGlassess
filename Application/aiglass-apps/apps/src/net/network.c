#include "network.h"

#if SUPPORT_SSL
void *ssl_init(int role)
{
    const SSL_METHOD *method;
    SSL_CTX *ssl_ctx;

    SSL_library_init();

    SSL_load_error_strings();
    ERR_load_crypto_strings();

    OpenSSL_add_ssl_algorithms();

	if (role == SERVER)
		method = TLS_server_method();
	else
		method = SSLv23_client_method();

    ssl_ctx = SSL_CTX_new(method);
    if (!ssl_ctx) {
        perror("Unable to create SSL context");
        ERR_print_errors_fp(stderr);
		return NULL;
    }

    return (void *)ssl_ctx;
}

typedef int (*verify_callback_t)(int is_ok, X509_STORE_CTX *ctx);

static int verify_callback(int preverify_ok, X509_STORE_CTX *ctx)
{
	if (!preverify_ok) {
		printf("Certificate verification failed!\n");
	}

    return preverify_ok;
}

void ssl_config(void *_ssl_ctx, int verify_mode, verify_callback_t cb,
		const char *cert, const char *key, const char *ca)
{

	SSL_CTX *ssl_ctx = (SSL_CTX *)_ssl_ctx;

    SSL_CTX_set_ecdh_auto(ssl_ctx, 1);

    /* load cert and key */
	if (cert && key) {
		if (SSL_CTX_use_certificate_file(ssl_ctx, cert, SSL_FILETYPE_PEM) <= 0) {
			ERR_print_errors_fp(stderr);
			goto err_exit;
		}

		if (SSL_CTX_use_PrivateKey_file(ssl_ctx, key, SSL_FILETYPE_PEM) <= 0) {
			ERR_print_errors_fp(stderr);
			goto err_exit;
		}

		if (!SSL_CTX_check_private_key(ssl_ctx)) {
			fprintf(stderr, "Private key does not match the public certificate\n");
			goto err_exit;
		}
	}

	if (ca) {
		/* load ca */
		if (SSL_CTX_load_verify_locations(ssl_ctx, ca, NULL) <= 0) {
			ERR_print_errors_fp(stderr);
			goto err_exit;
		}
	}

	if (verify_mode != -1) {
		/* set verify mode */
		SSL_CTX_set_verify(ssl_ctx, verify_mode, cb);
	}

	return 0;

err_exit:
	return -1;
}

void *ssl_open(void *_ssl_ctx, int fd, int role)
{
	SSL_CTX *ssl_ctx = (SSL_CTX *)_ssl_ctx;

	SSL *ssl = SSL_new(ssl_ctx);

	if (!ssl) {
		ERR_print_errors_fp(stderr);
		return NULL;
	}

	SSL_set_fd(ssl, fd);

	if (role == SERVER) {
		if (SSL_accept(ssl) <= 0) {
			ERR_print_errors_fp(stderr);
			SSL_free(ssl);
			return NULL;
		}
	} else if (role == CLIENT) {
		if (SSL_connect(ssl) <= 0) {
			ERR_print_errors_fp(stderr);
			SSL_free(ssl);
			return NULL;
		}
	}

	X509 *peer_cert = SSL_get_peer_certificate(ssl);
	if (!peer_cert) {
		printf("peer certificate not provided!\n");
		SSL_free(ssl);
		return NULL;
	}

	return (void *)ssl;
}

int ssl_close(void *_ssl)
{
	SSL *ssl = (SSL *)_ssl;

	SSL_shutdown(ssl);

	SSL_free(ssl);

	return 0;
}

void ssl_deinit(void *_ssl_ctx)
{
	SSL_CTX *ssl_ctx = (SSL_CTX *)_ssl_ctx;

    SSL_CTX_free(_ssl_ctx);

    EVP_cleanup();
    ERR_free_strings();
}

int ssl_write(void *_ssl, char *data, int data_size)
{
	SSL *ssl = (SSL *)_ssl;

	return SSL_write(ssl, data, data_size);
}

int ssl_read(void *_ssl, char *recv_buffer, size_t read_bytes)
{
	SSL *ssl = (SSL *)_ssl;

	return SSL_read(ssl, recv_buffer, read_bytes);
}
#endif /* SUPPORT_SSL */

static int get_socket_blocking_mode(int sockfd)
{
	int flags = fcntl(sockfd, F_GETFL, 0);
	if (flags == -1) {
		return -1;
	}
	/* 0: nonblocking   1: blocking */
	return (flags & O_NONBLOCK) ? 0 : 1;
}

static int set_socket_nonblocking(int sockfd)
{
    int flags = fcntl(sockfd, F_GETFL, 0);
    if (flags == -1) {
        perror("fcntl F_GETFL");
        goto err_exit;
    }

    if (fcntl(sockfd, F_SETFL, flags | O_NONBLOCK) == -1) {
        perror("fcntl F_SETFL O_NONBLOCK");
        goto err_exit;
    }
	return NT_SUCCESS;

err_exit:
	return NT_ERROR;
}

static int set_socket_blocking(int sockfd)
{
    int flags = fcntl(sockfd, F_GETFL, 0);
    if (flags == -1) {
        perror("fcntl F_GETFL");
        goto err_exit;
    }

    if (fcntl(sockfd, F_SETFL, flags & (~O_NONBLOCK)) == -1) {
        perror("fcntl F_SETFL O_NONBLOCK");
        goto err_exit;
    }
	return NT_SUCCESS;

err_exit:
	return NT_ERROR;
}

static int connect_server(network_context_t *network_ctx, conn_t *conn, \
		char *host_name, int16_t host_port, uint8_t is_blocking)
{
	(void)is_blocking;
	NT_LOGD("not support set blocking or nonblocking.\n");

    int cli_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (cli_fd < 0) {
        perror("socket creation failed");
		return NT_ERROR;
    }

    struct hostent *host;
    host = gethostbyname(host_name);
    if (host == NULL) {
        perror("gethostbyname failed");
        close(cli_fd);
		return NT_ERROR;
    }

    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));

    server_addr.sin_family = AF_INET;

#if SUPPORT_SSL
	if (network_ctx->flag & NETWORK_CONNECT_WITH_SSL)
		server_addr.sin_port = htons(DEFAULT_SERVER_SSL_PORT);
	else
#endif

	server_addr.sin_port = htons(host_port ? host_port : DEFAULT_SERVER_PORT);

	memcpy(&server_addr.sin_addr, host->h_addr, host->h_length);
	if (connect(cli_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
		perror("connect failed");
		close(cli_fd);
		return NT_ERROR;
	}

#if SUPPORT_SSL
	if (network_ctx->flag & NETWORK_CONNECT_WITH_SSL) {
		void *ssl_ctx = network_ctx->ssl_ctx;
		void *ssl = ssl_open(ssl_ctx, cli_fd, network_ctx->role);
		if (!ssl) {
			close(cli_fd);
			return NT_ERROR;
		}
		conn->ssl = (void *)ssl;
	}
#endif
	conn->fd = cli_fd;
	return NT_SUCCESS;
}

static int accept_client(network_context_t *network_ctx, conn_t *conn, uint8_t is_blocking)
{
	int ser_fd = network_ctx->sock_ctx;

	NT_LOGD("%s fd(%d) set %s\n", __func__, ser_fd, is_blocking? "block":"nonblock");
	/* set socket */
	if (!is_blocking) {
		if (set_socket_nonblocking(ser_fd)) {
			NT_LOGE("set socket nonblocking failed.\n");
			close(ser_fd);
			return NT_ERROR;
		}
		network_ctx->flag |= NETWORK_SOCKCTX_WITH_NONBLOCK;
	} else {
		if (set_socket_blocking(ser_fd)) {
			NT_LOGE("set socket blocking failed.\n");
			close(ser_fd);
			return NT_ERROR;
		}
		network_ctx->flag &= ~NETWORK_SOCKCTX_WITH_NONBLOCK;
	}

	/* accept client */
	struct sockaddr_in client_addr;
	socklen_t client_len = sizeof(client_addr);
	int cli_fd = -1;

	cli_fd = accept(ser_fd, (struct sockaddr *)&client_addr, &client_len);

	if (cli_fd == -1) {
		if (errno == EAGAIN || errno == EWOULDBLOCK) {
			NT_LOGD("plase retry. waiting client to connect.\n");
			return NT_EAGAIN;
		} else {
			NT_LOGE("accept client failed. errno: %d\n", errno);
			perror("accept() failed");
			return NT_ERROR;
		}
	} else {
		NT_LOGD("accept successfully. cli_fd: %d\n", cli_fd);
		conn->fd = cli_fd;

#if SUPPORT_SSL
		if (network_ctx->flag & NETWORK_CONNECT_WITH_SSL) {
			void *ssl_ctx = network_ctx->ssl_ctx;
			void *ssl = ssl_open(ssl_ctx, cli_fd, network_ctx->role);
			if (!ssl) {
				close(cli_fd);
				return NT_ERROR;
			}
			conn->ssl = (void *)ssl;
		}
#endif /* SUPPORT_SSL */
	}

	return NT_SUCCESS;
}

void *network_init(void)
{
	network_context_t *network_context = (network_context_t *)nt_malloc(sizeof(network_context_t));
	if (network_context == NULL) {
		NT_LOGE("network_context init, malloc failed.\n");
		return NULL;
	}
	nt_memset(network_context, 0, sizeof(network_context_t));

	return (void *)network_context;
}

int network_config(void *_network_ctx, uint8_t use_ssl, uint8_t role)
{
	network_context_t *network_ctx = (network_context_t *)_network_ctx;

	if (role == SERVER) {
		/* init server socket */
		int fd = -1;
		if ((fd = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
			perror("socket() failed");
			return NT_ERROR;
		}

		struct sockaddr_in server_addr;
		server_addr.sin_family = AF_INET;
		server_addr.sin_addr.s_addr = INADDR_ANY;

#if SUPPORT_SSL
		if (use_ssl)
			server_addr.sin_port = htons(DEFAULT_SERVER_SSL_PORT);
		else
#endif
		server_addr.sin_port = htons(DEFAULT_SERVER_PORT);

		NT_LOGD("%s bind port:%d\n",__func__, server_addr.sin_port);
		/* bind ip addr and port */
		if (bind(fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) == -1) {
			perror("bind() failed");
			close(fd);
			return NT_ERROR;
		}

		NT_LOGD("%s listen. syn num:%d\n",__func__, DEFAULT_SERVER_LISTEN_SYN_NUM);
		/* listen port */
		if (listen(fd, DEFAULT_SERVER_LISTEN_SYN_NUM) == -1) {
			perror("listen() failed");
			close(fd);
			return NT_ERROR;
		}

		network_ctx->sock_ctx = fd;

#if SUPPORT_SSL
		if (use_ssl) {
			void *ssl_ctx = ssl_init(role);
			if (!ssl_ctx)
				return NT_ERROR;

			if (ssl_config(ssl_ctx, SSL_VERIFY_NONE, verify_callback, \
					SERVER_CERT, SERVER_KEY, CA))
				return NT_ERROR;

			network_ctx->ssl_ctx = (void *)ssl_ctx;
			network_ctx->flag |= NETWORK_CONNECT_WITH_SSL;
		} else
			network_ctx->flag &= ~NETWORK_CONNECT_WITH_SSL;
#endif/* SUPPORT_SSL */
		network_ctx->role = SERVER;
	} else {
		network_ctx->sock_ctx = -1;

#if SUPPORT_SSL
		if (use_ssl) {
			void *ssl_ctx = ssl_init(role);
			if (!ssl_ctx)
				return NT_ERROR;

			if (ssl_config(ssl_ctx, SSL_VERIFY_NONE, verify_callback, \
					CLIENT_CERT, CLIENT_KEY, CA))
				return NT_ERROR;

			network_ctx->ssl_ctx = (void *)ssl_ctx;
			network_ctx->flag |= NETWORK_CONNECT_WITH_SSL;
		} else
			network_ctx->flag &= ~NETWORK_CONNECT_WITH_SSL;
#endif/* SUPPORT_SSL */
		network_ctx->role = CLIENT;
	}

	return NT_SUCCESS;
}

int network_deinit(void *_network_ctx)
{
	if (_network_ctx == NULL)
		return NT_ERROR;

	network_context_t *network_ctx = (network_context_t *)_network_ctx;

	int ser_fd = network_ctx->sock_ctx;

#if SUPPORT_SSL
	if (network_ctx->flag & NETWORK_CONNECT_WITH_SSL) {
		void *ssl_ctx = network_ctx->ssl_ctx;
		if (ssl_ctx)
			ssl_deinit(ssl_ctx);
	}
#endif

    close(ser_fd);

	nt_free(network_ctx);

	return NT_SUCCESS;
}

void *network_open(void *_network_ctx, char *host_name, int16_t host_port, uint8_t is_blocking)
{
	if (_network_ctx == NULL)
		return NULL;
	network_context_t *network_ctx = (network_context_t *)_network_ctx;
	uint8_t role = network_ctx->role;
	int ret = -1;
	conn_t _conn, *conn = NULL;

	NT_LOGD("%s role is %s.\n", __func__, role == CLIENT ? "CLIENT" : "SERVER");

	switch (role) {
		case CLIENT:
			ret = connect_server(network_ctx, &_conn, host_name, host_port, is_blocking);
			break;
		case SERVER:
			ret = accept_client(network_ctx, &_conn, is_blocking);
			break;
		default: break;
	}
	if (ret == NT_EAGAIN) {
		if (errno != EAGAIN && errno != EWOULDBLOCK)
			errno = EAGAIN;
	}

	if (ret == NT_SUCCESS) {
		conn = (conn_t *)nt_malloc(sizeof(conn_t));
		if (conn == NULL) {
			perror("malloc failed");
			NT_LOGE("malloc(%d) failed.\n", sizeof(conn_t));
		}
		nt_memset(conn, 0, sizeof(conn_t));
		nt_memcpy(conn, &_conn, sizeof(conn_t));

		conn->ctx = (void *)network_ctx;
		NT_LOGD("%s success.\n", __func__);
	}
	return (ret == NT_SUCCESS? conn : NULL);
}

int network_close(void *_conn)
{
	if (_conn == NULL)
		return NT_ERROR;

	conn_t *conn = (conn_t *)_conn;

	int cli_fd = conn->fd;

#if SUPPORT_SSL
	if (conn->ctx->flag & NETWORK_CONNECT_WITH_SSL) {

		void *ssl = conn->ssl;

		if (ssl) {
			ssl_close(ssl);
		}
	}
#endif

    close(cli_fd);

	nt_free(conn);

	return NT_SUCCESS;
}

int network_sendfile(void *_conn, int *_file_fd, int file_size, int tmo_ms)
{
	int bytes_sent = 0;

	if (_conn == NULL)
		return NT_ERROR;

	conn_t *conn = (conn_t *)_conn;
	network_context_t *network_ctx = (network_context_t *)conn->ctx;

	NT_LOGD("%s send bytes(%d) to SOCKET(%d).\n", __func__, file_size, conn->fd);

	int new_size = 1 * 1024 * 1024;  // 1 MB
	if (setsockopt(conn->fd, SOL_SOCKET, SO_SNDBUF, &new_size, sizeof(new_size)) == -1) {
		perror("setsockopt failed");
	} else {
		NT_LOGD("Send buffer size set to %d\n", new_size);
	}

	int sndbuf_size;
	socklen_t optlen = sizeof(sndbuf_size);
	if (getsockopt(conn->fd, SOL_SOCKET, SO_SNDBUF, &sndbuf_size, &optlen) == -1) {
		perror("getsockopt failed");
	} else {
		NT_LOGD("Send buffer size: %d\n", sndbuf_size);
	}

	int file_fd = *_file_fd;

	if (errno) errno = 0;

	if (tmo_ms < 0) {
		goto send_without_tmo;
	} else if (tmo_ms >= 0) {
		goto send_with_tmo;
	}

send_without_tmo:
	{
		int old_blocking = get_socket_blocking_mode(conn->fd);

		if (!old_blocking) set_socket_blocking(conn->fd);

		off_t offset = 0;
		while (offset < file_size) {
			int ret = sendfile(conn->fd, file_fd, &offset, 1024 * 1024);
			/* NT_LOGI("sent .... %d errno: %d %s\n", ret, errno, strerror(errno)); */

			if (ret < 0) {
				/* conn is reseted, errno is EPIPE */
				if (errno) perror("errno");
				if (errno != EWOULDBLOCK && errno != EAGAIN) {
					return NT_ERROR;
				}
			}

			if (ret == 0)
				break;
		}

		if (!old_blocking) set_socket_nonblocking(conn->fd);
	}
	goto exit;
send_with_tmo:
	{
		int old_blocking = get_socket_blocking_mode(conn->fd);

		if (old_blocking) set_socket_nonblocking(conn->fd);

		/* socket select */
		fd_set writefds, errfds;

		FD_ZERO(&writefds); FD_SET(conn->fd, &writefds);
		FD_ZERO(&errfds); FD_SET(conn->fd, &errfds);

		struct timeval timeout = {tmo_ms / 1000, (tmo_ms % 1000) * 1000};

		off_t offset = 0;
		while (offset < file_size) {
			int ret = select(conn->fd + 1, NULL, &writefds, &errfds, &timeout);
			if (ret <= 0) {
				if (ret == 0) NT_LOGE("select timeout.\n");
				else NT_LOGE("select failed.\n");
				return NT_ERROR;
			}
			if (FD_ISSET(conn->fd, &errfds)) {
				NT_LOGE("socket error.\n");
				return NT_ERROR;
			}
			if (FD_ISSET(conn->fd, &writefds)) {
				ret = sendfile(conn->fd, file_fd, &offset, 1024 * 1024);
				/* NT_LOGI("sent .... %d errno: %d %s\n", ret, errno, strerror(errno)); */
			}

			if (ret < 0) {
				/* conn is reseted, errno is EPIPE */
				if (errno) perror("errno");
				if (errno != EWOULDBLOCK && errno != EAGAIN) {
					return NT_ERROR;
				}
			}

			if (ret == 0)
				break;
		}

		if (old_blocking) set_socket_blocking(conn->fd);
	}
	goto exit;

exit:
	bytes_sent = file_size;

	NT_LOGD("%s %d = sendfile(%d, ., ., %d).\n", __func__, bytes_sent, conn->fd, data_size);

	return bytes_sent;
}

int network_send(void *_conn, char *data, int data_size, int tmo_ms)
{
	int bytes_sent = 0;

	if (_conn == NULL)
		return NT_ERROR;

	conn_t *conn = (conn_t *)_conn;
	network_context_t *network_ctx = (network_context_t *)conn->ctx;

	NT_LOGD("%s send bytes(%d) to SOCKET(%d).\n", __func__, data_size, conn->fd);

	/* blocking tmo or nonblocking */
	int old_blocking = get_socket_blocking_mode(conn->fd);
	if (tmo_ms < 0) {
		if (!old_blocking) set_socket_blocking(conn->fd);
	} else if (tmo_ms == 0) {
		if (old_blocking) set_socket_nonblocking(conn->fd);
	} else {
		/* socket select */
		fd_set writefds, errfds;
		FD_ZERO(&writefds); FD_SET(conn->fd, &writefds);
		FD_ZERO(&errfds); FD_SET(conn->fd, &errfds);

		struct timeval timeout = {tmo_ms / 1000, (tmo_ms % 1000) * 1000};
		int ret = select(conn->fd + 1, NULL, &writefds, &errfds, &timeout);
		if (ret <= 0) {
			if (ret == 0) NT_LOGE("select timeout.\n");
			else NT_LOGE("select failed.\n");
			return NT_ERROR;
		}
		if (FD_ISSET(conn->fd, &errfds)) {
			NT_LOGE("socket error.\n");
			return NT_ERROR;
		}
		if (FD_ISSET(conn->fd, &writefds)) {
			goto send;
		}
	}
send:
#if SUPPORT_SSL
	if (network_ctx->flag & NETWORK_CONNECT_WITH_SSL) {
		void *ssl = conn->ssl;
		bytes_sent = ssl_write(ssl, data, data_size);
	} else
#endif
	{
		int cli_fd = conn->fd;
		bytes_sent = send(cli_fd, data, data_size, 0);

		NT_LOGD("%s %d = send(%d, ., %d, .).\n", __func__, bytes_sent, cli_fd, data_size);
	}

	if (old_blocking) set_socket_blocking(conn->fd);
	else set_socket_nonblocking(conn->fd);

    if (bytes_sent < 0) {
		if (errno == EAGAIN || errno == EWOULDBLOCK)
			return NT_EAGAIN;
        perror("send() failed");
        return NT_ERROR;
    }

	NT_LOGD("%s bytes sent(%d) to SOCKET(%d).\n", __func__, bytes_sent, conn->fd);

	return bytes_sent;
}

int network_recv(void *_conn, char *recv_buffer, size_t read_bytes, int tmo_ms)
{
	int bytes_received = 0;

	if (_conn == NULL) {
		NT_LOGE("Don't connect network, please try again.\n");
		return NT_ERROR;
	}

	conn_t *conn = (conn_t *)_conn;
	network_context_t *network_ctx = (network_context_t *)conn->ctx;

	/* blocking tmo or nonblocking */
	int old_blocking = get_socket_blocking_mode(conn->fd);
	if (tmo_ms < 0) {
		if (!old_blocking) set_socket_blocking(conn->fd);
	} else if (tmo_ms == 0) {
		if (old_blocking) set_socket_nonblocking(conn->fd);
	} else {
		/* socket select */
		fd_set readfds, errfds;
		FD_ZERO(&readfds); FD_SET(conn->fd, &readfds);
		FD_ZERO(&errfds); FD_SET(conn->fd, &errfds);

		struct timeval timeout = {tmo_ms / 1000, (tmo_ms % 1000) * 1000};
		int ret = select(conn->fd + 1, &readfds, NULL, &errfds, &timeout);
		if (ret <= 0) {
			if (ret == 0) NT_LOGE("select timeout.\n");
			else NT_LOGE("select failed.\n");
			return NT_ERROR;
		}
		if (FD_ISSET(conn->fd, &errfds)) {
			NT_LOGE("socket error.\n");
			return NT_ERROR;
		}
		if (FD_ISSET(conn->fd, &readfds)) {
			int bytes_available = 0;
			if (ioctl(conn->fd, FIONREAD, &bytes_available) < 0) {
				perror("ioctl failed");
				return NT_ERROR;
			}
			NT_LOGD("Bytes available in receive buffer: %d\n", bytes_available);
			goto recv;
		}
	}
recv:
#if SUPPORT_SSL
	if (network_ctx->flag & NETWORK_CONNECT_WITH_SSL) {
		NT_LOGD("%s read bytes from SSL.\n", __func__);

		void *ssl = conn->ssl;
		bytes_received = ssl_read(ssl, recv_buffer, read_bytes);
	} else
#endif
	{
		int cli_fd = conn->fd;

		NT_LOGD("%s read bytes(%d) from SOCKET(%d).\n", __func__, read_bytes, cli_fd);

		bytes_received = recv(cli_fd, recv_buffer, read_bytes, 0);

		NT_LOGD("%s bytes received(%d) from SOCKET(%d).\n", __func__, bytes_received, cli_fd);
	}

	if (old_blocking) set_socket_blocking(conn->fd);
	else set_socket_nonblocking(conn->fd);

	if (bytes_received < 0) {
		if (errno == EAGAIN || errno == EWOULDBLOCK)
			return NT_EAGAIN;
		perror("recv() failed");
		return NT_ERROR;
	}

	return bytes_received;
}

