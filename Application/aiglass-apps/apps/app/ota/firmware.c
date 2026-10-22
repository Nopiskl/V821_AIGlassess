#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <aglink_log.h>
#include <aglink/ag_proto.h>

#include "firmware.h"

size_t downloaded_bytes = 0;
size_t total_bytes = 0;

static void extract_host_port_path(const char *url, char *host, int *port, char *path)
{
	const char *start_host = strstr(url, "://");
	if (start_host == NULL) {
		strcpy(host, "");
		*port = 80;
		strcpy(path, "");
		return;
	}
	start_host += 3;

	const char *start_port = strchr(start_host, ':');
	const char *start_path = strchr(start_host, '/');

	if (start_port == NULL) {
		if (start_path == NULL) {
			strcpy(host, start_host);
			strcpy(path, "");
			*port = 80;
		} else {
			size_t host_length = start_path - start_host;
			strncpy(host, start_host, host_length);
			host[host_length] = '\0';
			strcpy(path, start_path);
			*port = 80;
		}
	} else {
		if (start_path == NULL) {
			size_t host_length = start_port - start_host;
			strncpy(host, start_host, host_length);
			host[host_length] = '\0';
			sscanf(start_port + 1, "%d", port);
			strcpy(path, "");
		} else {
			size_t host_length = start_port - start_host;
			strncpy(host, start_host, host_length);
			host[host_length] = '\0';
			sscanf(start_port + 1, "%d", port);
			strcpy(path, start_path);
		}
	}
}

uint8_t get_download_firware_schedule(void)
{
	uint8_t schedule = 0;
	if (total_bytes)
		schedule = (downloaded_bytes * 100) / total_bytes;
	return schedule;
}

int start_down_ota_firmware(char *_url)
{
	int ret = AG_SUCCESS;

	char url_host[128] = { 0 }, url_path[128] = { 0 };
	int  url_port;

	extract_host_port_path(_url, url_host, &url_port, url_path);
	AGLINK_INFO("get path:%s, host:%s, port:%d\n", url_path, url_host, url_port);

	HTTP_SESSION *session = httpc_open(_url);
	if (!session)
		AGLINK_ERROR("httpc_open failed\n");

	httpc_response_t response;
	if (httpc_get(session, NULL, &response)) {
		ret = AG_FAILED_WRITE_FILE;
		goto exit;
	}

	FILE *fp = fopen(SAVE_PATH, "wb");
	if (fp == NULL) {
		perror("Failed to open file for writing");
		ret = AG_FAILED_WRITE_FILE;
		goto exit;
	}
	AGLINK_INFO("fopen  %s success\n", SAVE_PATH);
	int retry = 0;
	char buffer[BUFFER_SIZE * 10] = { 0 };

	downloaded_bytes = 0;
	total_bytes = response.content_length;
	if (total_bytes == 0) {  /* no ota file */
		AGLINK_ERROR("Failed to find ota file!\n");
		ret = AG_FAILED_PROCESS;
		goto exit;
	}
	while (total_bytes && response.status_code == HTTP_200_OK) {
		int bytes_received = httpc_read(session, buffer, sizeof(buffer), 15000);
		if (bytes_received < 0) {
			perror("Network Error try again...");
			AGLINK_INFO("read bytes:%d\n", bytes_received);
			if (retry > 3) {
				ret = AG_FAILED_NETWORK;
				perror("Network timeout please check network...");
				goto exit;
			}
			retry++;
			continue;
		} else if (bytes_received == 0) {
			if (total_bytes && (total_bytes == downloaded_bytes)) {
				AGLINK_INFO("total_bytes equal downloaded_bytes\n");
			}
			break;
		} else {
			retry = 0;
		}
		if (fwrite(buffer, 1, bytes_received, fp) < bytes_received) {
			perror("Error writing to file");
			ret = AG_FAILED_WRITE_FILE;
			goto exit;
		}
		downloaded_bytes += bytes_received;
	}
exit:
	if (fp) {
		fclose(fp);
	}
	/* close */
	httpc_close(session);
	return ret;
}
