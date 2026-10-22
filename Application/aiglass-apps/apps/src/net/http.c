#include "http.h"

HTTP_CTX *http_init(void)
{
	HTTP_CTX *http_ctx = (HTTP_CTX *)http_malloc(sizeof(HTTP_CTX));
	if (http_ctx == NULL) {
		HTTP_LOGE("http_context init, malloc failed.\n");
		return NULL;
	}
	http_memset(http_ctx, 0, sizeof(HTTP_CTX));

	http_ctx->network_ctx = network_init();
	if (http_ctx->network_ctx == NULL) {
		http_free(http_ctx);
		HTTP_LOGE("http network init failed.\n");
		return NULL;
	}

	http_ctx->status = HTTP_STATUS_INIT;

	return http_ctx;
}

int http_config(HTTP_CTX *http_ctx, uint8_t role, uint8_t use_ssl)
{
	HTTP_LOGD("%s <role: %s -  use_ssl: %s>\n",__func__, \
			role==HTTP_CLIENT? "CLIENT":"SERVER", \
			use_ssl?"yes":"no");

	if (use_ssl)
		http_ctx->flag |= HTTP_CONNECT_WITH_SSL;

	if (network_config(http_ctx->network_ctx, use_ssl, \
				role == HTTP_CLIENT? CLIENT:SERVER)) {
		return HTTP_ERROR;
	}

	http_ctx->role = role;

	http_ctx->status = HTTP_STATUS_CONFIG;

	return HTTP_SUCCESS;
}

int http_deinit(HTTP_CTX *http_ctx)
{
	http_ctx->status = HTTP_STATUS_DEINIT;

	network_deinit(http_ctx->network_ctx);

	http_free(http_ctx);

	return HTTP_SUCCESS;
}

static int http_extract_url(const char *url, char *host, int16_t *port, char *path)
{
	if (url == NULL)
		return -1;

	char *protocol_end = strstr(url, "://");
	if (protocol_end == NULL) {
		HTTP_LOGE("url is invalid. : %s\n", url);
		goto err_exit;
	}
	char *a = protocol_end + strlen("://");

	char *b = strchr(a, ':');
	char *c = strchr(a, '/');

	if (c == NULL) {
		HTTP_LOGE("url is invalid. no path : %s\n", url);
		goto err_exit;
	}

	if (b != NULL && b < c) {
		/* http://xxx.com:80/files/ */
		if (host && (b - a) < CONFIG_DEFAULT_MAX_HTTP_HOSTNAME_LEN)
			http_memcpy(host, a, b - a);
		else {
			HTTP_LOGE("hostname is too long.\n");
			goto err_exit;
		}

		if (port) {
			char port_str[8] = {0};
			http_memcpy(port_str, b + 1, c - b - 1);
			*port = atoi(port_str);
		}
	} else {
		/* http://xxx.com/files/ */
		if (host && (c - a) < CONFIG_DEFAULT_MAX_HTTP_HOSTNAME_LEN)
			http_memcpy(host, a, c - a);
		else {
			HTTP_LOGE("hostname is too long.\n");
			goto err_exit;
		}

		if (port) {
			if (strstr(url, "https") || strstr(url, "HTTPS"))
				*port = 443;
			else
				*port = 80;
		}
	}

	if (path) {
		size_t path_len = strlen(c);
		HTTP_LOGE("Extracted path: %s (length: %zu)\n", c, path_len);
		if (path_len < CONFIG_DEFAULT_MAX_HTTP_URL_PATH_LEN) {
			http_memcpy(path, c, path_len);
			path[path_len] = '\0';
		} else {
			HTTP_LOGE("url is too long.\n");
			goto err_exit;
		}
	}

	return 0;

err_exit:
	return -1;
}

HTTP_SESSION *http_open(HTTP_CTX *http_ctx, char *url, uint8_t is_blocking)
{
	HTTP_LOGD("%s ...\n", __func__);

	void *conn = NULL;

	if (http_ctx->role == HTTP_CLIENT) {
		HTTP_LOGD("%s as <%s>\n",__func__, "CLIENT");

		int16_t port = 0;
		char hostname[CONFIG_DEFAULT_MAX_HTTP_HOSTNAME_LEN] = { 0 };
		if (http_extract_url(url, hostname, &port, NULL)) {
			HTTP_LOGE("http open: url extract failed.\n");
			goto err_exit;
		}
		HTTP_LOGD("url-> host:%s. port:%d\n", hostname, port);

		conn = network_open(http_ctx->network_ctx, hostname, port, is_blocking);
		if (conn == NULL) {
			if (errno == EAGAIN || errno == EWOULDBLOCK)
				HTTP_LOGD("try again.\n");
			else
				HTTP_LOGE("http open failed.\n");
			goto err_exit;
		}
	} else if (http_ctx->role == HTTP_SERVER) {
		HTTP_LOGD("%s as <%s>\n",__func__, "SERVER");

		conn = network_open(http_ctx->network_ctx, NULL, 0, is_blocking);
		if (conn == NULL) {
			if (errno == EAGAIN || errno == EWOULDBLOCK)
				HTTP_LOGD("try again.\n");
			else
				HTTP_LOGE("http open failed.\n");
			goto err_exit;
		}
	} else {
		HTTP_LOGE("%s UNKOWN role <%d>\n",__func__, http_ctx->role);
		goto err_exit;
	}

	HTTP_SESSION *session = (HTTP_SESSION *)http_malloc(sizeof(HTTP_SESSION));
	if (session == NULL) {
		HTTP_LOGE("http open. malloc(%d) failed.\n", sizeof(HTTP_SESSION));
		return NULL;
	}
	http_memset(session, 0, sizeof(HTTP_SESSION));
	session->conn = conn;
	session->ctx = http_ctx;

	if (url) {
		if (strstr(url, "https") || strstr(url, "HTTPS"))
			session->is_https = 1;
		else
			session->is_https = 0;
	}

	if (http_ctx->role == HTTP_CLIENT) {
		char hostname[CONFIG_DEFAULT_MAX_HTTP_HOSTNAME_LEN] = { 0 };
		char url_path[CONFIG_DEFAULT_MAX_HTTP_URL_PATH_LEN] = { 0 };
		if (http_extract_url(url, hostname, NULL, url_path)) {
			HTTP_LOGE("http open: url extract failed.\n");
			goto err_exit;
		}
		http_memcpy(session->head_info.path, url_path, strlen(url_path));
		http_memcpy(session->head_info.host, hostname, strlen(hostname));
	}

	http_ctx->status = HTTP_STATUS_OPEN;

	HTTP_LOGD("%s success.\n", __func__);

	return session;

err_exit:
	return NULL;
}

int http_sendfile(HTTP_SESSION *session, int *file_fd, int file_size, int tmo_ms)
{
	int bytes_sent = 0;

	HTTP_LOGD("%s to send bytes: %d.\n", __func__, file_size);

	bytes_sent = network_sendfile(session->conn, file_fd, file_size, tmo_ms);

	if (bytes_sent < 0) {
		if (errno == EAGAIN || errno == EWOULDBLOCK)
			return HTTP_EAGAIN;
		else if (bytes_sent == NT_EAGAIN)
			return HTTP_EAGAIN;
		else
			return HTTP_ERROR;
	}

	session->ctx->status = HTTP_STATUS_WRITE;

	HTTP_LOGD("%s bytes sent: %d.\n", __func__, bytes_sent);

	return bytes_sent;
}

int http_write(HTTP_SESSION *session, char *data, int data_size, int tmo_ms)
{
	int bytes_sent = 0;

	HTTP_LOGD("%s to send bytes: %d.\n", __func__, data_size);

	bytes_sent = network_send(session->conn, data, data_size, tmo_ms);

	if (bytes_sent < 0) {
		if (errno == EAGAIN || errno == EWOULDBLOCK)
			return HTTP_EAGAIN;
		else if (bytes_sent == NT_EAGAIN)
			return HTTP_EAGAIN;
		else
			return HTTP_ERROR;
	}

	session->ctx->status = HTTP_STATUS_WRITE;

	HTTP_LOGD("%s bytes sent: %d.\n", __func__, bytes_sent);

	return bytes_sent;
}

int http_read(HTTP_SESSION *session, char *recv_buffer, size_t read_bytes, int tmo_ms)
{
	int bytes_received = 0;

	HTTP_LOGD("%s to read bytes: %d.\n", __func__, read_bytes);

	bytes_received = network_recv(session->conn, recv_buffer, read_bytes, tmo_ms);

	if (bytes_received < 0) {
		if (errno == EAGAIN || errno == EWOULDBLOCK)
			return HTTP_EAGAIN;
		else if (bytes_received == NT_EAGAIN)
			return HTTP_EAGAIN;
		else
			return HTTP_ERROR;
	}

	session->ctx->status = HTTP_STATUS_READ;

	HTTP_LOGD("%s bytes received: %d.\n", __func__, bytes_received);

	return bytes_received;
}

int http_close(HTTP_SESSION *session)
{
	session->ctx->status = HTTP_STATUS_CLOSE;

	network_close(session->conn);

	http_free(session);

	return HTTP_SUCCESS;
}

static int http_get_remote_header(HTTP_SESSION *session, char *header_buf)
{
	char *ptr = header_buf;

	session->header_len = 0;

	while (1) {
		int bytes_received = network_recv(session->conn, ptr, 1, 5000); /* 5ms too small. use 5000ms */

		if (bytes_received < 0) {
			HTTP_LOGE("get remote header error.\n");
			return HTTP_ERROR;
		} else {
			session->header_len += bytes_received;
			ptr += bytes_received;
			if ((session->header_len > 4) && \
				(http_memcmp(ptr - 4, "\r\n\r\n", 4)) == 0) {
				break;
			}
		}
	}

	return HTTP_SUCCESS;
}

static int http_parse_header(HTTP_SESSION *session, char *header_buf)
{
	char *ptr = header_buf;

	/* parse start line */
	if (!strstr(header_buf, "HTTP")) {
		return HTTP_ERROR;
	}

	/* skip space */
	while (*ptr && isspace((unsigned char)*ptr)) {
		ptr ++;
	}

	if (strstr(ptr, "HTTP/1.0")) {
		session->head_info.version = HTTP_1_0;
	} else if (strstr(ptr, "HTTP/1.1")) {
		session->head_info.version = HTTP_1_1;
	} else {
		session->head_info.version = HTTP_1_1;
	}

	if (strncmp(ptr, "HTTP/", 5) == 0) {
		session->act_type = HTTP_RESPONSE;
	} else {
		session->act_type = HTTP_REQUEST;
	}

	if (session->act_type == HTTP_REQUEST) {
		if (strstr(ptr, "GET")) {
			session->head_info.verb = GET;
		} else if (strstr(ptr, "POST")) {
			session->head_info.verb = POST;
		} else {
			session->head_info.verb = UNKOWN_VERB;
		}

		char *url_path_start = strstr(ptr, "/");
		if (url_path_start != NULL) {
			uint16_t url_path_len = strstr(url_path_start, " ") - url_path_start;
			if (url_path_len > sizeof(session->head_info.path)) {
				HTTP_LOGE("parse http header. url len(%d) is too long.\n", url_path_len);
				return HTTP_ERROR;
			}
			http_memcpy(session->head_info.path, url_path_start, url_path_len);
			session->head_info.path[url_path_len] = '\0';
		}
	} else {
		char status_code[4] = {0};
		char skip_len = 0;
		if (session->head_info.version == HTTP_1_0 || \
				session->head_info.version == HTTP_1_1) {
			skip_len = 9;
		}

		for (int i = 0; i < 3; i ++) {
			status_code[i] = ptr[i + skip_len];
		}

		session->head_info.status_code = atoi(status_code);
	}

	/* parse head */
	int i = 0;
	char *start = NULL, *end = NULL;
	const char *content_type[2] = {"Content-Type:", "content_type:"};
	for (i = 0; i < 2; i ++) {
		start = strstr(ptr, content_type[i]);

		if (start != NULL) {
			start += strlen(content_type[0]);

			end = strstr(start, "\r\n");
			if (end != NULL) {
				int len = end - start;
				if (len > (sizeof(session->head_info.content_type) - 1)) {
					HTTP_LOGE("parse http header. content_type len(%d) is too long.\n", len);
					return HTTP_ERROR;
				}
				http_memcpy(session->head_info.content_type, start, len);
				session->head_info.content_type[len] = '\0';
				break;
			}
		}
	}
	/* default */
	if (i == 2)
		http_memcpy(session->head_info.content_type, "text/plain", strlen("text/plain"));

	const char *connection[2] = {"Connection:", "connection:"};
	for (i = 0; i < 2; i ++) {
		start = strstr(ptr, connection[i]);

		if (start != NULL) {
			start += strlen(connection[0]);

			if (strstr(start, "close")) {
				session->head_info.connection = CLOSE;
			} else if (strstr(start, "keep-alive")) {
				session->head_info.connection = KEEPALIVE;
			} else {
				session->head_info.connection = CLOSE;
			}
			break;
		}
	}
	/* default */
	if (i == 2)
		session->head_info.connection = CLOSE;

	const char *trans_encode[2] = {"Transfer-Encoding:", "transfer-encoding:"};
	for (i = 0; i < 2; i ++) {
		start = strstr(ptr, trans_encode[i]);

		if (start != NULL) {
			start += strlen(trans_encode[0]);
			if (strstr(start, "chunked")) {
				session->head_info.trans_encode = CHUNKED;
				session->head_info.content_length = 0;
				break;
			}
		}
	}
	/* default */
	if (i == 2)
		session->head_info.trans_encode = NOT_CHUNKED;

	const char *content_length[2] = {"Content-Length:", "content-length:"};
	for (i = 0; i < 2; i ++) {
		start = strstr(ptr, content_length[i]);

		if (start != NULL) {
			start += strlen(content_length[0]);

			while (*start && isspace((unsigned char)*start)) {
				start ++;
			}

			session->head_info.content_length = strtol(start, NULL, 10);
			break;
		}
	}
	/* default */
	if (i == 2)
		session->head_info.content_length = 0;

	/* parse over */
	return HTTP_SUCCESS;
}

static int http_reset_header(HTTP_SESSION *session)
{
	memset(session->header_buf, 0, sizeof(session->header_buf));

	session->write_ptr = session->header_buf;

	return HTTP_SUCCESS;
}

static int http_add_header(HTTP_SESSION *session , char *header_name, char *header_data)
{
	char *ptr = session->write_ptr;

	int name_len = strlen(header_name);
	int data_len = strlen(header_data);

	http_memcpy(ptr, header_name, name_len);
	ptr += name_len;
	http_memcpy(ptr, ": ", 2);
	ptr += 2;
	http_memcpy(ptr, header_data, data_len);
	ptr += data_len;
	http_memcpy(ptr, "\r\n", 2);
	ptr += 2;

	session->write_ptr = ptr;

	return HTTP_SUCCESS;
}

static int http_set_head_startline(HTTP_SESSION *session)
{
	char act_type = session->act_type;

	char *ver_str = "HTTP/1.1";
	switch (session->head_info.version) {
		case HTTP_1_0: ver_str = "HTTP/1.0"; break;
		case HTTP_1_1: ver_str = "HTTP/1.1"; break;
	}

	switch (act_type) {
		case HTTP_RESPONSE: {
			char *status_code_str = "200 OK";
			switch (session->head_info.status_code) {
				case HTTP_200_OK: status_code_str = "200 OK"; break;
				case HTTP_301_MOV: status_code_str = "301 Moved Permanently"; break;
				case HTTP_302_FOUND: status_code_str = "302 Found"; break;
				case HTTP_400_BAD: status_code_str = "400 Bad Request"; break;
				case HTTP_401_UNAUTH: status_code_str = "401 Unauthorized"; break;
				case HTTP_404_NOT_FOUND: status_code_str = "404 Not Found"; break;
				default: HTTP_LOGE("status code not support!\n"); return HTTP_ERROR;
			}

			sprintf(session->header_buf, "%s %s\r\n", ver_str, status_code_str);
			session->write_ptr += strlen(ver_str) + 1 + strlen(status_code_str) + strlen("\r\n");
		} break;
		case HTTP_REQUEST: {
			char *verb_str = "GET";
			switch (session->head_info.verb) {
				case GET: verb_str = "GET"; break;
				case POST: verb_str = "POST"; break;
				default: HTTP_LOGE("verb not support!\n"); return HTTP_ERROR;
			}

			sprintf(session->header_buf, "%s %s %s\r\n", verb_str, session->head_info.path, ver_str);
			session->write_ptr += strlen(verb_str) + 1 + strlen(session->head_info.path) + 1 \
								  + strlen(ver_str) + strlen("\r\n");
		} break;
	}

	return HTTP_SUCCESS;
}

/* key: value\r\nkey1: value1\r\nkey2: value2\r\n */
static int http_set_headextention(HTTP_SESSION *session, char *ext_h, int len)
{
	uint32_t remain_size = sizeof(session->header_buf) - (session->write_ptr - session->header_buf);

	if (remain_size > len) {
		http_memcpy(session->write_ptr, ext_h, len);
		session->write_ptr += len;
	} else {
		HTTP_LOGE("extension header is too long %d >= %d\n", len, remain_size);
		return HTTP_ERROR;
	}

	return HTTP_SUCCESS;
}

static int http_set_headgeneral(HTTP_SESSION *session)
{
	if (session->head_info.connection) {
		char *connection_str = "close";
		switch (session->head_info.connection) {
			case CLOSE: connection_str = "close"; break;
			case KEEPALIVE: connection_str = "keep-alive"; break;
		}
		http_add_header(session, "Connection", connection_str);
	}

	if (session->head_info.trans_encode == CHUNKED) {
		http_add_header(session, "Transfer-Encoding", "chunked");
	} else {
		char content_length_str[24] = {0};
		snprintf(content_length_str, sizeof(content_length_str), "%d", \
				session->head_info.content_length);
		http_add_header(session, "Content-Length", content_length_str);
	}

	if (strlen(session->head_info.host)) {
		http_add_header(session, "Host", session->head_info.host);
	}

	http_add_header(session, "Content-Type", session->head_info.content_type);

	http_add_header(session, "Access-Control-Allow-Origin", "*");

	/* add user header */
	if (session->head_info.ext_h && strlen(session->head_info.ext_h) > 0) {
		char *http_header = NULL;
		uint32_t header_len = 0;

		http_header = session->head_info.ext_h;
		if(http_header)
			header_len = strlen(http_header);

		if (http_set_headextention(session, http_header, header_len)) {
			return HTTP_ERROR;
		}
	}

	return HTTP_SUCCESS;
}

static int http_set_headend(HTTP_SESSION *session)
{
	uint32_t remain_size = sizeof(session->header_buf) - (session->write_ptr - session->header_buf);

	uint32_t len = strlen("\r\n\r\n");

	if (remain_size > len) {
		http_memcpy(session->write_ptr, "\r\n\r\n", len);
		session->write_ptr += len;
	} else {
		HTTP_LOGE("extension header is too long %d >= %d\n", len, remain_size);
		return HTTP_ERROR;
	}

	return HTTP_SUCCESS;
}

static int http_intrn_send(HTTP_SESSION *session)
{
	char *data = session->header_buf;
	int data_len = session->write_ptr - session->header_buf;
	HTTP_LOGD("%s request data:\n%s", __func__, data);
	int ret = network_send(session->conn, data, data_len, 5000);
	if (ret)
		return HTTP_ERROR;

	data = session->body;
	data_len = session->body_len;

	if (data && data_len > 0) {
		ret = network_send(session->conn, data, data_len, 5000);
		if (ret)
			return HTTP_ERROR;
	}

	return HTTP_SUCCESS;
}

int http_recv_response(HTTP_SESSION *session)
{
	int ret = 0;
	// memset(session, 0, sizeof(HTTP_SESSION));  /* will reset session->conn */

	ret = http_get_remote_header(session, session->header_buf);
	if (ret != HTTP_SUCCESS) {
		return HTTP_ERROR;
	}

	ret = http_parse_header(session, session->header_buf);
	if (ret != HTTP_SUCCESS) {
		return HTTP_ERROR;
	}

	session->ctx->status = HTTP_STATUS_RECV_RESPONSE;

	return HTTP_SUCCESS;
}

int http_send_request(HTTP_SESSION *session)
{
	/* reset head buf */
	http_reset_header(session);

	/* add start line */
	http_set_head_startline(session);

	/* add general header */
	http_set_headgeneral(session);

	/* add \r\n\r\n */
	http_set_headend(session);

	/* send */
	http_intrn_send(session);

	session->ctx->status = HTTP_STATUS_SEND_REQUEST;

	return HTTP_SUCCESS;
}

int http_recv_request(HTTP_SESSION *session)
{
	int ret = 0;

	memset(session, 0, sizeof(HTTP_SESSION));

	ret = http_get_remote_header(session, session->header_buf);
	if (ret != HTTP_SUCCESS) {
		return HTTP_ERROR;
	}

	ret = http_parse_header(session, session->header_buf);
	if (ret != HTTP_SUCCESS) {
		return HTTP_ERROR;
	}

	session->ctx->status = HTTP_STATUS_RECV_REQUEST;

	return HTTP_SUCCESS;
}

int http_send_response(HTTP_SESSION *session)
{
	/* reset head buf */
	http_reset_header(session);

	/* add start line */
	http_set_head_startline(session);

	/* add general header */
	http_set_headgeneral(session);

	/* add \r\n\r\n */
	http_set_headend(session);

	/* send */
	http_intrn_send(session);

	session->ctx->status = HTTP_STATUS_SEND_RESPONSE;

	return HTTP_SUCCESS;
}

HTTP_SESSION *httpc_open(char *url)
{
	HTTP_CTX *ctx = http_init();
	if (ctx == NULL) {
		HTTP_LOGE("http init failed.\n");
		goto exit0;
	}

	if (url) {
		if (strstr(url, "https") || strstr(url, "HTTPS"))
			http_config(ctx, HTTP_CLIENT, 1);
		else
			http_config(ctx, HTTP_CLIENT, 0);
	}

	HTTP_SESSION *session = http_open(ctx, url, 1);
	if (session == NULL) {
		HTTP_LOGE("http open failed.\n");
		goto exit1;
	}

	return session;
exit1:
	http_deinit(ctx);
exit0:
	return NULL;
}

static int httpc_transaction(HTTP_SESSION *session, httpc_request_t *request, \
		httpc_response_t *response)
{
	session->act_type			= HTTP_REQUEST;
	session->body				= request->body;
	session->body_len			= request->body_len;
	session->head_info.verb			= request->verb;
	session->head_info.version		= HTTP_1_1;
	session->head_info.connection		= CLOSE;
	session->head_info.trans_encode 	= NOT_CHUNKED;

	if (request->ext_h) {
		if (strlen(request->ext_h) < sizeof(session->head_info.ext_h)) {
			http_memcpy(session->head_info.ext_h, request->ext_h, \
				strlen(request->ext_h));
		} else {
			HTTP_LOGE("ext_h is too long > %d, fix it\n", sizeof(session->head_info.ext_h));
			return -1;
		}
	}

	int ret = http_send_request(session);
	if (ret) {
		HTTP_LOGE("http send request failed.\n");
		goto exit1;
	}
	http_memset(&session->head_info, 0, sizeof(session->head_info));
	ret = http_recv_response(session);
	if (ret) {
		HTTP_LOGE("http recv responese failed.\n");
		goto exit1;
	}
	response->status_code = session->head_info.status_code;
	response->trans_encode = session->head_info.trans_encode;
	response->content_length = session->head_info.content_length;
	http_memcpy(response->content_type, session->head_info.content_type, \
			sizeof(response->content_type));

	return HTTP_SUCCESS;
exit1:
	http_close(session);
exit0:
	return HTTP_ERROR;
}

int httpc_get(HTTP_SESSION *session, char *ext_h, httpc_response_t *response)
{
	httpc_request_t *request = (httpc_request_t *)http_malloc(sizeof(httpc_request_t));
	if (request == NULL) {
		HTTP_LOGE("malloc(%d) failed.\n", sizeof(httpc_request_t));
		return HTTP_ERROR;
	}
	http_memset(request, 0, sizeof(httpc_request_t));

	request->verb = GET;

	if(ext_h)
		request->ext_h = ext_h;

	httpc_transaction(session, request, response);

	http_free(request);

	return HTTP_SUCCESS;
}

int httpc_post(HTTP_SESSION *session, char *ext_h, char *post_data, \
		size_t size, httpc_response_t *response)
{
	httpc_request_t *request = (httpc_request_t *)http_malloc(sizeof(httpc_request_t));
	if (request == NULL) {
		HTTP_LOGE("malloc(%d) failed.\n", sizeof(httpc_request_t));
		return HTTP_ERROR;
	}

	request->verb = POST;
	request->body = post_data;
	request->body_len = size;

	if (ext_h)
		request->ext_h = ext_h;

	httpc_transaction(session, request, response);

	http_free(request);

	return HTTP_SUCCESS;
}

int httpc_read(HTTP_SESSION *session, char *recv_buffer, int read_bytes, int tmo_ms)
{
	int bytes_received = 0;

	if (read_bytes > 0) {
		bytes_received = http_read(session, recv_buffer, read_bytes, tmo_ms);
	}

	return bytes_received;
}

int httpc_close(HTTP_SESSION *session)
{
	http_deinit(session->ctx);

	http_close(session);

	return HTTP_SUCCESS;
}
