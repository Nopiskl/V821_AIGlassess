/*
* Copyright (c) 2019-2025 Allwinner Technology Co., Ltd. ALL rights reserved.
*
* Allwinner is a trademark of Allwinner Technology Co.,Ltd., registered in
* the the people's Republic of China and other countries.
* All Allwinner Technology Co.,Ltd. trademarks are used with permission.
*
* DISCLAIMER
* THIRD PARTY LICENCES MAY BE REQUIRED TO IMPLEMENT THE SOLUTION/PRODUCT.
* IF YOU NEED TO INTEGRATE THIRD PARTY’S TECHNOLOGY (SONY, DTS, DOLBY, AVS OR MPEGLA, ETC.)
* IN ALLWINNERS’SDK OR PRODUCTS, YOU SHALL BE SOLELY RESPONSIBLE TO OBTAIN
* ALL APPROPRIATELY REQUIRED THIRD PARTY LICENCES.
* ALLWINNER SHALL HAVE NO WARRANTY, INDEMNITY OR OTHER OBLIGATIONS WITH RESPECT TO MATTERS
* COVERED UNDER ANY REQUIRED THIRD PARTY LICENSE.
* YOU ARE SOLELY RESPONSIBLE FOR YOUR USAGE OF THIRD PARTY’S TECHNOLOGY.
*
*
* THIS SOFTWARE IS PROVIDED BY ALLWINNER"AS IS" AND TO THE MAXIMUM EXTENT
* PERMITTED BY LAW, ALLWINNER EXPRESSLY DISCLAIMS ALL WARRANTIES OF ANY KIND,
* WHETHER EXPRESS, IMPLIED OR STATUTORY, INCLUDING WITHOUT LIMITATION REGARDING
* THE TITLE, NON-INFRINGEMENT, ACCURACY, CONDITION, COMPLETENESS, PERFORMANCE
* OR MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE.
* IN NO EVENT SHALL ALLWINNER BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
* SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
* NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
* LOSS OF USE, DATA, OR PROFITS, OR BUSINESS INTERRUPTION)
* HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
* STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
* ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
* OF THE POSSIBILITY OF SUCH DAMAGE.
*/
#include <stdint.h>
#include <stdio.h>
#include <pthread.h>
#include <signal.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include "aglink/ag_wrap.h"
#include <sys/socket.h>
#include <stdbool.h>
#include <sys/un.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <aglink/ag_proto.h>
#include <wifimg.h>
#include <wifi_log.h>
#include <aglink_log.h>
#include <ifaddrs.h>
#include <arpa/inet.h>

#include "firmware.h"

enum {
	AP_INFO,
	P2P_INFO
};

enum {
	INIT = 0,
	WAIT_INFO,

	COMMON_STATE_MAX
};

enum {
	RECVD_AP_INFO = COMMON_STATE_MAX,
	START_AP,
	START_AP_SUCCESS,
	START_AP_FAILED,
	WAIT_STA_CONNECT,
	STA_CONNECTED,
	STOP_AP,

	AP_STATE_MAX
};

enum {
	RECVD_P2P_INFO = COMMON_STATE_MAX,
	START_P2P,
	START_P2P_SUCCESS,
	START_P2P_FAILED,
	DEVICE_START_DISCOVERY,
	DEVICE_DISCOVERY,
	DEVICE_RESTART,
	DEVICE_CONNECT,
	DEVICE_CONNECT_SUCCESS,
	DEVICE_CONNECT_FAILED,
	DEVICE_CONNECTD_GC,
	DEVICE_CONNECTD_GO,
	DEVICE_DISCONNECT,
	STOP_P2P,

	P2P_STATE_MAX
};

enum {
	AP,
	P2P,

	MAX
};

enum {  /* store medium */
	MEDIUM_NAND_AND_NOR = 1,
	SINGLE_ONLY_NAND = 2,
	MEDIUM_ONLY_NOR = 3,
};

char *state_str[MAX][(P2P_STATE_MAX - AP_STATE_MAX > 0 ? P2P_STATE_MAX : AP_STATE_MAX)] =
{
	{
		"INIT",
		"WAIT_INFO",
		"RECVD_AP_INFO",
		"START_AP",
		"START_AP_SUCCESS",
		"START_AP_FAILED",
		"WAIT_STA_CONNECT",
		"STA_CONNECTED",
		"STOP_AP",
	},
	{
		"INIT",
		"WAIT_INFO",
		"RECVD_P2P_INFO",
		"START_P2P",
		"START_P2P_SUCCESS",
		"START_P2P_FAILED",
		"DEVICE_START_DISCOVERY",
		"DEVICE_DISCOVERY",
		"DEVICE_RESTART",
		"DEVICE_CONNECT",
		"DEVICE_CONNECT_SUCCESS",
		"DEVICE_CONNECT_FAILED",
		"DEVICE_CONNECTD_GC",
		"DEVICE_CONNECTD_GO",
		"DEVICE_DISCONNECT",
		"STOP_P2P",
	}
};

typedef enum ota_mode_state {
	AG_OTA_CLOSE = 0,
	AG_OTA_NEW,
	AG_OTA_WAIT_AP_INFO,
	AG_OTA_AP_INFO,
	AG_OTA_AP_SUCCESS,
	AG_OTA_AP_FAILED,
	AG_OTA_NET_SDP,
	AG_OTA_WAIT_START,
	AG_OTA_START,
	AG_OTA_WORKING,
	AG_OTA_SUCCESS,
	AG_OTA_FAILED,
	AG_OTA_IDLE,
} ota_mode_state;

#ifdef CONFIG_SOCKET_PROGRESS_PATH
char *SOCKET_PROGRESS_PATH = (char*)CONFIG_SOCKET_PROGRESS_PATH;
#else
char *SOCKET_PROGRESS_PATH = NULL;
#endif
#define SOCKET_PROGRESS_DEFAULT  "swupdateprog"

#define MAX_URL_LEN 1024
static uint8_t *rx_data = NULL;
static int data_len = 0;
static int data_type = 0;
char ota_url[MAX_URL_LEN] = {0};
#define RX_DATA rx_data
#define DATA_LEN data_len
#define GET_RX_DATA(t,d,l) \
do { \
	data_type = (t); \
	data_len = (l); \
	if (((d) != NULL) && ((l) > 0)) { \
		rx_data = malloc((l)); \
		memcpy(rx_data, (d), (l)); \
	} else { \
		AGLINK_INFO("rx data is NULL\n"); \
	} \
} while(0)

#define DEL_RX_DATA() \
do { \
	data_type = 0; \
	data_len = 0; \
	if ((rx_data) != NULL) { \
		free(rx_data); \
	} \
} while(0)

int ota_dev_id = 0;
int g_swuipdata_bar;
extern int ota_boot_stage;

static uint8_t state[MAX] = {INIT, INIT};
static uint8_t state_max[MAX] = {AP_STATE_MAX, P2P_STATE_MAX};
#define GET_STATE() \
({ \
	state[MODE]; \
})

#define CHANGE_SATE(s, m) \
do { \
	uint8_t last_state = state[m]; \
	if ((s) >= state_max[m]) { \
		AGLINK_INFO("UNKOWN STATE: %d, change to -> INIT\n", state[m]); \
		state[m] = INIT; \
		break; \
	} \
	state[m] = (s); \
	AGLINK_INFO("%s -> %s\n", state_str[m][last_state], state_str[m][state[m]]); \
} while(0)

#define DEFAULT_MODE AP
static int wireless_mode = DEFAULT_MODE;
#define MODE wireless_mode
#define SET_MODE(m) \
do { \
	MODE = m; \
} while(0)

#define STATE_CHANGED(curr_state)     \
	if (ota_state != curr_state) {      \
		ota_state = curr_state;         \
		AGLINK_INFO("ota %s -->%s \n",         \
				aglink_ota_app_state_to_string(curr_state), \
				aglink_ota_app_state_to_string(ota_state)); \
	}

#define TASK_SLEEP_PER (50 * 1000) // 50 ms
#define LOOP_PRINT_PER (20) // 1 s
#define DEFAULT_AP_NAME "allwinner-ap"
#define DEFAULT_AP_PSK "Aa123456"
#define DEFAULT_AP_CHAN 6

static uint8_t p2p_flag = 0;
#define LOCK()
#define UNLOCK()
#define FLAG_P2P_RESTART ((uint8_t)1 << 0)

#define SET_P2P_FLAG(f) \
do { \
	LOCK(); \
	p2p_flag |= (f); \
	UNLOCK(); \
} while(0)

#define CHECK_P2P_FLAG(f) \
({ \
	uint8_t _ret = 0; \
	LOCK(); \
	_ret = (p2p_flag & (f) == (f))? 1:0; \
	UNLOCK(); \
	_ret; \
})

#define CLEAR_P2P_FLAG(f) \
do { \
	LOCK(); \
	p2p_flag &= ~(f); \
	UNLOCK(); \
} while(0)

enum p2p_role_type {
	P2P_GO = 0,
	P2P_GC
};
static char p2p_gc_ip[64];
static uint8_t p2p_role = P2P_GO;
#define RECONNECT 1
static int discover_cost_time;
#define DISCOVER_TIME 120 //s

#define P2P_INTENT 1
#define DISCOVER_TIME 120 //s
#define LISTEN_TIME DISCOVER_TIME //s
#define P2P_CONNECT_TIMEOUT (DISCOVER_TIME)

#define DEFAULT_P2P_NUM 5
#define P2P_NAME_MAX_LEN 32
#define DEFAULT_P2P_NAME "allwinner-p2p-demo-dev"
static char p2p_dev_buf[P2P_NAME_MAX_LEN + 1] = {DEFAULT_P2P_NAME};

typedef enum {
	IDLE,
	START,
	RUN,
	SUCCESS,
	FAILURE,
	DOWNLOAD,
	DONE,
	SUBPROCESS,
} RECOVERY_STATUS;

typedef enum {
	SOURCE_UNKNOWN,
	SOURCE_WEBSERVER,
	SOURCE_SURICATTA,
	SOURCE_DOWNLOADER,
	SOURCE_LOCAL
} sourcetype;

struct progress_msg {
	unsigned int	magic;		/* Magic Number */
	RECOVERY_STATUS	status;		/* Update Status (Running, Failure) */
	unsigned int	dwl_percent;	/* % downloaded data */
	unsigned int	nsteps;		/* No. total of steps */
	unsigned int	cur_step;	/* Current step index */
	unsigned int	cur_percent;	/* % in current step */
	char		cur_image[256];	/* Name of image to be installed */
	char		hnd_name[64];	/* Name of running hanler */
	sourcetype	source;		/* Interface that triggered the update */
	unsigned int 	infolen;    	/* Len of data valid in info */
	char		info[2048];   	/* additional information about install */
};

volatile ota_mode_state ota_state = AG_OTA_NEW;
//volatile network_mode_state network_state = AG_NETWORK_START;

static pthread_t ag_ota_thread;
static pthread_t ag_ota_schedule_thread;
static pthread_t ag_ota_get_swuupdata_bar_thread;

volatile int ota_app_exit;
static int error_code = AG_SUCCESS;

static void aglink_ota_app_sync(void)
{

	struct ag_sync sync = {
		.mode = AG_MODE_OTA,
		.str = "U_OK",
	};

	aglink_tx_data(ota_dev_id, AG_VD_SYNC_2,
			(uint8_t *)&sync, sizeof(struct ag_sync));

}

static void aglink_ota_app_send_error(void)
{
	struct ag_generic_rsp error = {
		.mode = AG_MODE_OTA,
		.code = error_code,
	};

	aglink_tx_data(ota_dev_id, AG_VD_GENERIC_RESPONSE,
			(uint8_t *)&error, sizeof(struct ag_generic_rsp));
	aglink_tx_data(ota_dev_id, AG_VD_POWER_OFF, NULL, 0);
}

static void aglink_send_generic_response(int rsp_code)
{
	struct ag_generic_rsp generic_rsp = {
		.mode = AG_MODE_OTA,
		.code = rsp_code,
	};

	aglink_tx_data(ota_dev_id, AG_VD_GENERIC_RESPONSE,
			(uint8_t *)&generic_rsp, sizeof(struct ag_generic_rsp));
}

static void aglink_ota_p2p_start_success(void)
{
	struct ag_wifi_p2p_success p2p_success_para;

	if (wifi_get_mac(NULL, p2p_success_para.mac) != WMG_STATUS_SUCCESS) {
		AGLINK_INFO("mac addr get failed\n");
	}

	p2p_success_para.mac[5] = p2p_success_para.mac[5] + 1;

	aglink_tx_data(ota_dev_id, AG_VD_P2P_SUCCESS,
			(uint8_t *)&p2p_success_para, sizeof(struct ag_wifi_p2p_success));
}

static void aglink_ota_send_success(void)
{
	if (ota_boot_stage == 0){
		aglink_tx_data(ota_dev_id, AG_VD_POWER_OFF,
			NULL, 0);
		return;
	}
	aglink_tx_data(ota_dev_id, AG_VD_OTA_SUCCESS,
			NULL, 0);

	aglink_tx_data(ota_dev_id, AG_VD_POWER_OFF,
			NULL, 0);
}

static void aglink_ota_ap_start_success(void)
{
	aglink_tx_data(ota_dev_id, AG_VD_AP_SUCCESS,
			NULL, 0);
}

static int get_ifip(const char *if_name, char *ip_str)
{
	struct ifaddrs *ifaddr, *ifa;
	char ip[INET6_ADDRSTRLEN];
	int found = 0;

	if (getifaddrs(&ifaddr) == -1) {
		perror("getifaddrs");
		return -1;
	}

	for (ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next) {
		if (ifa->ifa_addr && strncmp(ifa->ifa_name, if_name, 4) == 0) {
			if (ifa->ifa_addr->sa_family == AF_INET) {
				void *addr = &((struct sockaddr_in *)ifa->ifa_addr)->sin_addr;
				inet_ntop(AF_INET, addr, ip, sizeof(ip));
				AGLINK_INFO("Interface: %s  IPv4 Address: %s\n", ifa->ifa_name, ip);
				found = 1;
			} else if (ifa->ifa_addr->sa_family == AF_INET6) {
				void *addr = &((struct sockaddr_in6 *)ifa->ifa_addr)->sin6_addr;
				inet_ntop(AF_INET6, addr, ip, sizeof(ip));
				AGLINK_INFO("Interface: %s  IPv6 Address: %s\n", ifa->ifa_name, ip);
				found = 1;
			}
		}
	}

	if (!found) {
		AGLINK_INFO("No %s interface found or it does not have an IP address.\n", if_name);
	}

	freeifaddrs(ifaddr);

	memcpy(ip_str, ip, sizeof(ip));

	return 0;
}

static int ipstr_2_hex(char *ip_str, uint8_t *ip)
{
	struct in_addr addr;

	if (inet_pton(AF_INET, ip_str, &addr) != 1) {
		AGLINK_INFO("Invalid IP address.\n");
		return -1;
	}

	unsigned char *byte = (unsigned char *)&addr.s_addr;

	memcpy(ip, byte, 4);

	return 0;
}

static struct timeval start_time;

static inline void check_timeout_start(void)
{
	gettimeofday(&start_time, NULL);
}

static int check_timeout(long timeout_s)
{
	struct timeval current_time;
	long elapsed_time_s = 0;

	gettimeofday(&current_time, NULL);

	elapsed_time_s = (current_time.tv_sec - start_time.tv_sec);

	/* timeout */
	if (elapsed_time_s >= timeout_s) {
		return -1;
	}

	return 0;
}

static void aglink_ota_net_sdp(void)
{
	struct ag_sdp ota_sdp = {
		.ip = {192,168,31,1}
	};

	if (MODE == P2P) {
		switch (p2p_role) {
			case P2P_GO: {
				ota_sdp.ip[0] = 192;
				ota_sdp.ip[1] = 168;
				ota_sdp.ip[2] = 6;
				ota_sdp.ip[3] = 1;
			} break;
			case P2P_GC: {
				ipstr_2_hex(p2p_gc_ip, ota_sdp.ip);
			} break;
			default: break;
		}
	}

	AGLINK_INFO("sdp ip: %x %x %x %x\n", ota_sdp.ip[0], ota_sdp.ip[1], ota_sdp.ip[2], ota_sdp.ip[3]);

	aglink_tx_data(ota_dev_id, AG_VD_SDP,
			(uint8_t *)&ota_sdp, sizeof(struct ag_sdp));

}

static void aglink_ota_send_bar(uint8_t dl_schedule, uint8_t wd_schedule)
{
	struct ag_ota_bar ota_bar = {
		.download_schedule = dl_schedule,
		.write_data_schedule = wd_schedule,
	};
	aglink_tx_data(ota_dev_id, AG_VD_OTA_PG_BAR,
			(uint8_t *)&ota_bar, sizeof(struct ag_ota_bar));
}

static void* aglink_ota_schedule_task(void* data)
{
	uint8_t dl_schedule = 0, wd_schedule = 0;
	while (!ota_app_exit) {
		switch (ota_state)
		{
		case AG_OTA_NEW:
		case AG_OTA_WORKING:
			dl_schedule = get_download_firware_schedule();
			wd_schedule = (uint8_t) g_swuipdata_bar;
			aglink_ota_send_bar(dl_schedule, wd_schedule);
			break;
		default:
			goto exit;
			break;
		}
		usleep(500*1000);
	}
exit:
	AGLINK_INFO("aglink OTA Finish,please reboot now!!!\n");
	pthread_exit(NULL);
}

static char *get_prog_socket(void) {
	if (!SOCKET_PROGRESS_PATH || !strlen(SOCKET_PROGRESS_PATH)) {
		const char *tmpdir = getenv("TMPDIR");
		if (!tmpdir)
			tmpdir = "/tmp";

		if (asprintf(&SOCKET_PROGRESS_PATH, "%s/%s", tmpdir, SOCKET_PROGRESS_DEFAULT) == -1)
			return (char *)"/tmp/"SOCKET_PROGRESS_DEFAULT;
	}

	return SOCKET_PROGRESS_PATH;
}

static int _progress_ipc_connect(const char *socketpath, bool reconnect)
{
	struct sockaddr_un servaddr;
	int fd = socket(AF_LOCAL, SOCK_STREAM, 0);
	bzero(&servaddr, sizeof(servaddr));
	servaddr.sun_family = AF_LOCAL;
	strncpy(servaddr.sun_path, socketpath, sizeof(servaddr.sun_path));

	AGLINK_INFO("Trying to connect to SWUpdate...\n");

	do {
		if (connect(fd, (struct sockaddr *) &servaddr, sizeof(servaddr)) == 0) {
			break;
		}
		if (!reconnect) {
			AGLINK_INFO("cannot communicate with SWUpdate via %s\n", socketpath);
			exit(1);
		}

		usleep(10000);
	} while (true);

	AGLINK_INFO("Connected to SWUpdate via %s\n", socketpath);
	return fd;
}

static int progress_ipc_connect(bool reconnect)
{
	return _progress_ipc_connect(get_prog_socket(), reconnect);
}

static int progress_ipc_receive(int *connfd, struct progress_msg *msg) {
	int ret = read(*connfd, msg, sizeof(*msg));
	if (ret != sizeof(*msg)) {
		AGLINK_INFO("Connection closing..\n");
		close(*connfd);
		*connfd = -1;
		return -1;
	}
	return ret;
}

static void* aglink_ota_get_swuupdata_bar_task(void* data) {
	int connfd = -1;
	struct progress_msg msg;
	unsigned int filled_len;
	char bar[60];
	const char *tmpdir = NULL;
	unsigned int curstep = 0;
	unsigned int percent = 0;
	int psplash_ok = 0;
	RECOVERY_STATUS status = IDLE;

	while (!ota_app_exit) {
		if (connfd < 0) {
			AGLINK_INFO("connect to swupdate\n\n");
			connfd = progress_ipc_connect(1);
		} else {
			AGLINK_INFO("Not to connect swupdate\n\n");
		}
		if (progress_ipc_receive(&connfd, &msg) == -1) {
			goto exit;
		}

		if (msg.infolen > 0)
			AGLINK_INFO("INFO : %s\n\n", msg.info);

		if ((msg.cur_step != curstep) && (curstep != 0))
			AGLINK_INFO("\n");

		filled_len = sizeof(bar) * msg.cur_percent / 100;
		if (filled_len > sizeof(bar))
			filled_len = sizeof(bar);

		memset(bar,'=', filled_len);
		memset(&bar[filled_len], '-', sizeof(bar) - filled_len);

		AGLINK_INFO("cur_percent:%d, cur_image:%s, cur_step:%d, tola_step:%d\n", msg.cur_percent, msg.cur_image, msg.cur_step, msg.nsteps);
		g_swuipdata_bar = (msg.cur_percent/msg.nsteps) + ((100/msg.nsteps)*(msg.cur_step - 1));
		AGLINK_INFO("total_percent:%d\n", g_swuipdata_bar);

		switch (msg.status)
		{
		case SUCCESS:
			AGLINK_INFO("ota success!\n");
			break;
		case FAILURE:
			error_code = AG_FAILED_PROCESS;
			AGLINK_ERROR("ota failure!\n");
			break;
		default:
			break;
		}
	}
exit:
	if (connfd > 0)
		close(connfd);
	pthread_exit(NULL);

	return 0;
}

/* 1 NAND+NOR, 2 NAND */
static int aglink_check_nand_or_nandnor(void)
{
	FILE *fp;
	char buffer[BUFFER_SIZE];
	char command[] = "ls /dev/mtdblock0";
	int ret = -1;
	fp = popen(command, "r");
	if (fp == NULL) {
		perror("Failed to execute command");
		return 1;
	}

	if (fgets(buffer, BUFFER_SIZE, fp) != NULL) {
		size_t len = strlen(buffer);
		if (len > 0 && buffer[len - 1] == '\n') {
			buffer[len - 1] = '\0';
			if (strstr(buffer, "mtdblock") && !strstr(buffer,"No such file or directory")) {
				AGLINK_INFO("Start mode is NAND + NOR\n");
				ret = MEDIUM_NAND_AND_NOR;
			}
		} else {
			AGLINK_INFO("Read buffer is NONE. Start mode is ONLY NAND\n");
			ret = SINGLE_ONLY_NAND;
		}
	} else {
		ret = SINGLE_ONLY_NAND;
		AGLINK_INFO("Failed to read command output. Start mode is ONLY NAND\n");
	}
	pclose(fp);
	return ret;
}

static int get_fw_env(void)
{
	FILE *fp;
	char buffer[BUFFER_SIZE];
	char command[] = "fw_printenv -n boot_partition";
	int ret = -1;

	fp = popen(command, "r");
	if (fp == NULL) {
		perror("Failed to execute command");
		return 1;
	}

	if (fgets(buffer, BUFFER_SIZE, fp) != NULL) {
		size_t len = strlen(buffer);
		if (len > 0 && buffer[len - 1] == '\n') {
		buffer[len - 1] = '\0';
		}
		if (strcmp(buffer, "bootA") == 0) {
			AGLINK_INFO("The boot partition is BootA.\n");
			ret = 0;
		} else if (strcmp(buffer, "bootB") == 0) {
			AGLINK_INFO("The boot partition is BootB.\n");
			ret = 1;
		} else {
			AGLINK_INFO("The boot partition is neither BootA nor BootB. It is: %s\n", buffer);
			ret = -1;
		}
	} else {
		ret = -1;
		AGLINK_INFO("Failed to read command output.\n");
	}
	pclose(fp);

	return ret;
}

static void start_ota_sh(void)
{
	AGLINK_INFO("start OTA\n");
	switch (ota_boot_stage) {  /* 0 nor start, 1 nand start */
	case 0:
		AGLINK_INFO("updata sdnand\n");
		system("swupdate -i /mnt/UDISK/openwrt_v821_aiglass-ab.swu -e stable,sdnand");
		break;
	case 1:
		/* judge medium */
		switch (aglink_check_nand_or_nandnor()) {
		case MEDIUM_NAND_AND_NOR:
			AGLINK_INFO("updata nor\n");
			system("swupdate -i /mnt/UDISK/openwrt_v821_aiglass-ab.swu -e stable,nor");
			sync();
			break;
		default:
			AGLINK_INFO("updata signal sdnand\n");
			switch (get_fw_env()) {
			case 0:
				AGLINK_INFO("now_A_next_B");
				system("swupdate -i /mnt/UDISK/openwrt_v821_aiglass-ab.swu -e stable,now_A_next_B");
				remove(SAVE_PATH);  /* nand only update one times, so need remove ota file*/
				break;
			case 1:
				AGLINK_INFO("now_B_next_A");
				system("swupdate -i /mnt/UDISK/openwrt_v821_aiglass-ab.swu -e stable,now_B_next_A");
				remove(SAVE_PATH);  /* nand only update one times, so need remove ota file*/
				break;
			default:
				AGLINK_INFO("Failed get_fw_env.\n");
				break;
			}
			break;
		}
		break;
	default:
		AGLINK_INFO("Failed get_fw_env.\n");
		break;
	}
}

const char* aglink_ota_app_state_to_string(ota_mode_state state)
{
	switch (state) {
	case AG_OTA_CLOSE:
		return "close";
	case AG_OTA_NEW:
		return "new";
	case AG_OTA_WAIT_AP_INFO:
		return "wait_ap_info";
	case AG_OTA_AP_SUCCESS:
		return "ap_success";
	case AG_OTA_AP_FAILED:
		return "ap_failed";
	case AG_OTA_WAIT_START:
		return "wait_start";
	case AG_OTA_START:
		return "start";
	case AG_OTA_WORKING:
		return "working";
	case AG_OTA_SUCCESS:
		return "success";
	case AG_OTA_FAILED:
		return "failed";
	case AG_OTA_IDLE:
		return "idle";
	}
	return "no found";
}

static void wifi_msg_cb(wifi_msg_data_t *msg)
{
	if(msg->id == WIFI_MSG_ID_AP_STATE_CHANGE) {
		WMG_DEBUG("get ap state:(%d) ", msg->data.ap_state);
		switch(msg->data.ap_event) {
			case WIFI_AP_DISABLED:
			WMG_INFO("ap disable\n");
			break;
			case WIFI_AP_ENABLED:
			WMG_INFO("ap enable\n");
			break;
			case WIFI_AP_STA_DISCONNECTED:
			WMG_INFO("one sta disconnected\n");
			break;
			case WIFI_AP_STA_CONNECTED:
			//CHANGE_SATE(STA_CONNECTED);
			WMG_INFO("one sta connected\n");
			break;
			default:
			WMG_INFO("unknown event\n");
			break;
		}
	}

	if(msg->id == WIFI_MSG_ID_P2P_STATE_CHANGE) {
		WMG_DEBUG("get p2p state:(%d) ", msg->data.p2p_state);
		switch(msg->data.p2p_state) {
			case WIFI_P2P_ENABLE:
			WMG_INFO("p2p state: enable\n");
			break;
			case WIFI_P2P_DISABLE:
			WMG_INFO("p2p state: disable\n");
			break;
			case WIFI_P2P_DISCOVER:
			WMG_INFO("p2p state: discovering...\n");
			break;
			case WIFI_P2P_CONNECTD_GC:
			CHANGE_SATE(DEVICE_CONNECTD_GC, P2P);
			WMG_INFO("p2p state: connectd gc\n");
			break;
			case WIFI_P2P_CONNECTD_GO:
			CHANGE_SATE(DEVICE_CONNECTD_GO, P2P);
			WMG_INFO("p2p state: connectd go\n");
			break;
			case WIFI_P2P_DISCONNECTD:
			CHANGE_SATE(DEVICE_DISCONNECT, P2P);
			STATE_CHANGED(AG_OTA_WAIT_AP_INFO);
			WMG_INFO("p2p state: disconnected\n");
			break;
		}
	}
}

static inline void p2p_start_discover(int discover_time)
{
	check_timeout_start();
	// wifi_p2p_find(discover_time, "social");
	CLEAR_P2P_FLAG(FLAG_P2P_RESTART);
}

int stop_p2p(void)
{
	wifi_off(WIFI_MODE_UNKNOWN);
	wifimanager_deinit();

	return 0;
}

int start_p2p(uint8_t *_data, int len)
{
	(void)len;
	struct ag_wifi_p2p *data = (struct ag_wifi_p2p *)_data;

	if (data == NULL) {
		AGLINK_INFO("data is NULL. invailed param.\n");
		return -1;
	}

	if (data->name[0] != 0) {
		uint8_t name_len = P2P_NAME_MAX_LEN > sizeof(data->name)? \
						   sizeof(data->name) : P2P_NAME_MAX_LEN;
		memcpy(p2p_dev_buf, data->name, name_len);
	}

	wifi_p2p_config_t p2p_config;
	memset(&p2p_config, 0, sizeof(wifi_p2p_config_t));
	p2p_config.dev_name = p2p_dev_buf;
	/* listen xxx second to connect */
	p2p_config.discover_time = 0;
	p2p_config.listen_time = LISTEN_TIME;
	p2p_config.p2p_go_intent = P2P_INTENT;
	p2p_config.auto_connect = WMG_TRUE;

	AGLINK_INFO("Start p2p....\n");
	AGLINK_INFO("P2P info: \n");
	AGLINK_INFO(" dev_name:%s\n", p2p_config.dev_name);
	AGLINK_INFO(" discover_time:%d\n", p2p_config.discover_time);
	AGLINK_INFO(" p2p_go_intent:%d\n", p2p_config.p2p_go_intent);

	/* wifimanager init */
	wifimanager_init();
	/* register callback function */
	if(wifi_register_msg_cb(wifi_msg_cb, NULL)) {
		WMG_ERROR("register msg cb failed\n");
	}

	/* open p2p mode */
	if(wifi_on(WIFI_P2P)) {
		WMG_ERROR("p2p mode: open p2p mode failed.\n");
		goto exit;
	}

	/* enable p2p mode */
	if(wifi_p2p_enable(&p2p_config)) {
		WMG_ERROR("p2p mode: p2p mode enable failed.\n");
		wifi_off(WIFI_MODE_UNKNOWN);
		goto exit;
	}

	return 0;

exit:
	wifimanager_deinit();

	return -1;
}

int restart_p2p(void)
{
	/* close p2p mode */
	wifi_off(WIFI_MODE_UNKNOWN);

	wifi_p2p_config_t p2p_config;

	memset(&p2p_config, 0, sizeof(wifi_p2p_config_t));

	p2p_config.dev_name = p2p_dev_buf;
	/* listen xxx second to connect */
	p2p_config.discover_time = 0;
	p2p_config.p2p_go_intent = P2P_INTENT;
	p2p_config.auto_connect = WMG_TRUE;

	/* open p2p mode */
	if(wifi_on(WIFI_P2P)) {
		WMG_ERROR("p2p mode: open p2p mode failed.\n");
		goto exit;
	}

	/* enable p2p mode */
	if(wifi_p2p_enable(&p2p_config)) {
		WMG_ERROR("p2p mode: p2p mode enable failed.\n");
		wifi_off(WIFI_MODE_UNKNOWN);
		goto exit;
	}

	return 0;

exit:
	wifimanager_deinit();

	return -1;
}

int stop_ap(void)
{
	wifi_p2p_disconnect(NULL);
	wifi_off(WIFI_MODE_UNKNOWN);
	wifimanager_deinit();

	return 0;
}

static int start_ap(uint8_t *_data, int len)
{
	(void)len;
	char ssid_buf[SSID_MAX_LEN + 1] = {0};
	char psk_buf[PSK_MAX_LEN + 1] = {0};
	uint8_t ip_addr[4] = {192,168,31,1};
	uint8_t gw_addr[4] = {192,168,31,1};
	uint8_t ap_channel = DEFAULT_AP_CHAN;

	struct ag_wifi_ap *data = (struct ag_wifi_ap *)_data;
	if (data == NULL) {
		AGLINK_INFO("data is NULL. invailed param.\n");
		return -1;
	}

	if (data->ssid[0] != 0) {
		uint8_t ssid_len = SSID_MAX_LEN > sizeof(data->ssid)? \
						   sizeof(data->ssid) : SSID_MAX_LEN;
		uint8_t pwd_len = PSK_MAX_LEN > sizeof(data->pwd)? \
						   sizeof(data->pwd) : PSK_MAX_LEN;
		memcpy(ssid_buf, data->ssid, ssid_len);
		memcpy(psk_buf, data->pwd, pwd_len);
	} else {
		memcpy(ssid_buf, DEFAULT_AP_NAME, strlen(DEFAULT_AP_NAME));
		memcpy(psk_buf, DEFAULT_AP_PSK, strlen(DEFAULT_AP_PSK));
	}

	if (data->channel != 0) {
		ap_channel = data->channel;
	} else {
		ap_channel = DEFAULT_AP_CHAN;
	}

	wifi_ap_config_t ap_config;
	memset(&ap_config, 0, sizeof(wifi_ap_config_t));
	ap_config.ssid = ssid_buf;
	ap_config.psk = psk_buf;
	ap_config.sec = WIFI_SEC_WPA2_PSK;
	ap_config.channel = ap_channel;
	memcpy(ap_config.ip_addr, ip_addr, 4);
	memcpy(ap_config.gw_addr, gw_addr, 4);

	AGLINK_INFO("Start ap....\n");

	/* wifimanager init */
	wifimanager_init();

	/* register callback function */
	if(wifi_register_msg_cb(wifi_msg_cb, NULL)) {
		WMG_ERROR("register msg cb failed\n");
	}
	/* open ap mode */
	if(wifi_on(WIFI_AP) == 0) {
		if(wifi_ap_enable(&ap_config) == 0) {
			WMG_INFO("ap enable success, ssid=%s, psk=%s, sec=%d, channel=%d\n", ap_config.ssid,
					ap_config.psk, ap_config.sec, ap_config.channel);
		} else {
			WMG_ERROR("ap enable failed\n");
			goto exit;
		}
	} else {
		WMG_ERROR("wifi on ap failed\n");
		goto exit;
	}

	return 0;

exit:
	wifi_off(WIFI_MODE_UNKNOWN);
	wifimanager_deinit();

	return -1;
}

static void p2p_newstate(int ns)
{
	switch (ns) {
		case RECVD_P2P_INFO: {
			AGLINK_INFO("recved p2p info.\n");
			CHANGE_SATE(START_P2P, P2P);
		} break;
		case START_P2P: {
			int ret = -1;
			ret = start_p2p(RX_DATA, DATA_LEN);
			DEL_RX_DATA();
			if (ret) {
				CHANGE_SATE(START_P2P_FAILED, P2P);
				break;
			}
			CHANGE_SATE(START_P2P_SUCCESS, P2P);
		} break;
		case START_P2P_SUCCESS: {
			AGLINK_INFO("start p2p success. inform....\n");
			aglink_ota_p2p_start_success();
			AGLINK_INFO("start p2p success. inform over.\n");
			CHANGE_SATE(DEVICE_START_DISCOVERY, P2P);
		} break;
		case START_P2P_FAILED: {
			AGLINK_INFO("start p2p failed!!! restart.\n");
			CHANGE_SATE(INIT, P2P);
		} break;
		case DEVICE_START_DISCOVERY: {
			p2p_start_discover(DISCOVER_TIME);
			CHANGE_SATE(DEVICE_DISCOVERY, P2P);
		} break;
		case DEVICE_DISCOVERY: {
			static int loop_cnt = 0; int p2p_num = 0;
			wifi_p2p_peers_t p2p_peers[DEFAULT_P2P_NUM] = {0};
			if (!(loop_cnt ++ % LOOP_PRINT_PER)) {
				loop_cnt = 1;
				AGLINK_INFO("device discover.....\n");
				wifi_p2p_get_peers(p2p_peers, &p2p_num, DEFAULT_P2P_NUM);
				if (p2p_num > 0) {
					AGLINK_INFO("get %d p2p peers\n", p2p_num);
					for (int i = 0; i < p2p_num; i++) {
						AGLINK_INFO("p2p dev name: %s mac: %02x:%02x:%02x:%02x:%02x:%02x\n", p2p_peers[i].dev_name,
								p2p_peers[i].mac_addr[0], p2p_peers[i].mac_addr[1] ,p2p_peers[i].mac_addr[2],
								p2p_peers[i].mac_addr[3], p2p_peers[i].mac_addr[4] ,p2p_peers[i].mac_addr[5]);
					}
				}
			}
			/* check restart */
			if (CHECK_P2P_FLAG(FLAG_P2P_RESTART)) {
				CLEAR_P2P_FLAG(FLAG_P2P_RESTART);
				CHANGE_SATE(DEVICE_RESTART, P2P);
			}
			/* check timeout */
			if (check_timeout(P2P_CONNECT_TIMEOUT)) {
				AGLINK_INFO("discover timeout!\n");
				aglink_send_generic_response(AG_CONNECT_TIMEOUT);
				CHANGE_SATE(STOP_P2P, P2P);
			}
		} break;
		case DEVICE_RESTART: {
			if (restart_p2p()) {
				AGLINK_INFO("restart p2p failed!\n");
				CHANGE_SATE(STOP_P2P, P2P);
				break;
			}
			/* set reconnect */
			CHANGE_SATE(DEVICE_START_DISCOVERY, P2P);
		} break;
		case DEVICE_CONNECTD_GC: {
			int ret = 0; static int retry = 0, retry_max = 50;
			AGLINK_INFO("connected! is GC\n");
			p2p_role = P2P_GC;
			retry_get:
			ret = get_ifip("p2p-wlan", p2p_gc_ip);
			if (ret) {
				AGLINK_INFO("retry get ip. %d\n", retry);
				if (retry ++ > retry_max) {
					AGLINK_INFO("get ip failed.\n");
					CHANGE_SATE(DEVICE_DISCONNECT, P2P);
				}
				usleep(100 * 1000);
				goto retry_get;
			}
			STATE_CHANGED(AG_OTA_NET_SDP);
			//CHANGE_SATE(INIT, P2P);
		} break;
		case DEVICE_CONNECTD_GO: {
			AGLINK_INFO("connected! is GO\n");
			p2p_role = P2P_GO;
			STATE_CHANGED(AG_OTA_NET_SDP);
			//CHANGE_SATE(INIT, P2P);
		} break;
		case DEVICE_DISCONNECT: {
			AGLINK_INFO("device disconnect!\n");
#if RECONNECT
			/* notify TWS */
			aglink_send_generic_response(AG_NETWORK_DISCONNECT);
			/* set reconnect */
			CHANGE_SATE(DEVICE_RESTART, P2P);
#else
			CHANGE_SATE(STOP_P2P, P2P);
#endif
		} break;
		case STOP_P2P: {
			stop_p2p();
			AGLINK_INFO("stop p2p.\n");
			CHANGE_SATE(INIT, P2P);
		} break;
		default: AGLINK_INFO("unkown state.\n");break;
	}
}

static void ap_newstate(int ns)
{
	switch (ns) {
		case RECVD_AP_INFO: {
			AGLINK_INFO("recved ap info.\n");
			CHANGE_SATE(START_AP, AP);
		} break;
		case START_AP: {
			int ret = -1;
			ret = start_ap(RX_DATA, DATA_LEN);
			DEL_RX_DATA();
			if (ret) {
				CHANGE_SATE(START_AP_FAILED, AP);
				break;
			}
			CHANGE_SATE(START_AP_SUCCESS, AP);
		} break;
		case START_AP_SUCCESS: {
			AGLINK_INFO("start ap success. inform....\n");
			aglink_ota_ap_start_success();
			AGLINK_INFO("start ap success. inform over.\n");
			CHANGE_SATE(WAIT_STA_CONNECT, AP);
		} break;
		case START_AP_FAILED: {
			AGLINK_INFO("start ap failed!!! restart.\n");
			CHANGE_SATE(INIT, AP);
		} break;
		case WAIT_STA_CONNECT: {
			static int loop_cnt = 0;
			char ap_ssid[SSID_MAX_LEN + 1] = {0};
			char ap_psk[PSK_MAX_LEN + 1] = {0};
			wifi_ap_config_t ap_config;
			ap_config.ssid = ap_ssid;
			ap_config.psk = ap_psk;
			ap_config.dev_list = (wifi_sta_dev_member_t *)malloc(sizeof(wifi_sta_dev_member_t) * STA_MAX_NUM);
			if (ap_config.dev_list != NULL) {
				ap_config.sta_dev_list_num = STA_MAX_NUM;
			} else {
				AGLINK_INFO("ap config malloc failed\n");
				error_code = AG_FAILED_NOMEMORY;
				STATE_CHANGED(AG_OTA_FAILED);
			}
			if (!(loop_cnt ++ % 10000)) {
				wifi_ap_get_config(&ap_config);
				if (ap_config.sta_num > 0) {
					AGLINK_INFO("sta num: %d\n", ap_config.sta_num);
					AGLINK_INFO("ap ip_addr: %d.%d.%d.%d\n", \
						 ap_config.ip_addr[0],
						 ap_config.ip_addr[1],
						 ap_config.ip_addr[2],
						 ap_config.ip_addr[3]);
					AGLINK_INFO("ap gw_addr: %d.%d.%d.%d\n", \
						 ap_config.gw_addr[0],
						 ap_config.gw_addr[1],
						 ap_config.gw_addr[2],
						 ap_config.gw_addr[3]);
					CHANGE_SATE(STA_CONNECTED, AP);
				} else {
					AGLINK_INFO("waiting for sta to connect....\n");
					loop_cnt = 1;
				}
			}
			if (ap_config.dev_list != NULL) {
				free(ap_config.dev_list);
				ap_config.dev_list = NULL;
				ap_config.sta_dev_list_num = 0;
			}
		} break;
		case STA_CONNECTED: {
			// todo . maybe sta is not my aiglassess phone.
			AGLINK_INFO("sta connected!\n");
			STATE_CHANGED(AG_OTA_NET_SDP);
			//CHANGE_SATE(INIT, AP);
		} break;
		case STOP_AP: {
			stop_ap();
			AGLINK_INFO("stop ap.\n");
			CHANGE_SATE(INIT, AP);
		} break;
		default: AGLINK_INFO("unkown state.\n");break;
	}
}

static void* aglink_ota_task(void* data)
{
	while (!ota_app_exit) {
		switch (ota_state) {
			case AG_OTA_CLOSE:
				break;
			case AG_OTA_NEW:
				aglink_ota_app_sync();
				if (ota_boot_stage == 1) {
					if (access(SAVE_PATH, F_OK) == 0) {
						pthread_create(&ag_ota_schedule_thread, NULL, aglink_ota_schedule_task, NULL);
						pthread_create(&ag_ota_get_swuupdata_bar_thread, NULL, aglink_ota_get_swuupdata_bar_task, NULL);
						AGLINK_INFO("aiglass ota BOOT SDNAND\n");
						start_ota_sh();
						if (error_code == AG_SUCCESS) {
							STATE_CHANGED(AG_OTA_SUCCESS);
						} else {
							STATE_CHANGED(AG_OTA_FAILED);
						}
						remove(SAVE_PATH);
						sync();
						break;
					}
					AGLINK_INFO("swu package not exist, redownload\n");
				}
				STATE_CHANGED(AG_OTA_WAIT_AP_INFO);
				break;
			case AG_OTA_WAIT_AP_INFO:{
				switch (GET_STATE()) {
				case INIT: {
					CHANGE_SATE(WAIT_INFO, MODE);
				} break;
				case WAIT_INFO: {
					static int loop_cnt = 0;
					if (!(loop_cnt ++ % 10000)) {
						AGLINK_INFO("wait info.....\n");
						loop_cnt = 1;
					}
				} break;
				default: {
					switch (MODE) {
						case AP:
							  ap_newstate(GET_STATE());
						break;
						case P2P:
							  p2p_newstate(GET_STATE());
						break;
						default:
							  AGLINK_INFO("unkown wireless mode.\n");
						break;
					}
				} break;
				}
			}
			break;
			case AG_OTA_NET_SDP:
				aglink_ota_net_sdp();
				STATE_CHANGED(AG_OTA_WAIT_START);
				break;
			case AG_OTA_WAIT_START:{
					static int loop_cnt = 0;
					if (!(loop_cnt ++ % 10000)) {
						AGLINK_INFO("wait OTA start cmd\n");
						loop_cnt = 1;
					}
				} break;
			case AG_OTA_START:
				AGLINK_INFO("ota start\n");
				STATE_CHANGED(AG_OTA_WORKING);
				break;
			case AG_OTA_WORKING:{
				pthread_create(&ag_ota_schedule_thread, NULL, aglink_ota_schedule_task, NULL);
				pthread_create(&ag_ota_get_swuupdata_bar_thread, NULL, aglink_ota_get_swuupdata_bar_task, NULL);
				error_code = start_down_ota_firmware(ota_url);
				if (error_code != 0) {
					STATE_CHANGED(AG_OTA_FAILED);
					if (access(SAVE_PATH, F_OK) == 0) {
						remove(SAVE_PATH);
						sync();
					}
					break;
				} else {
					start_ota_sh();
					if (error_code == AG_SUCCESS) {
						STATE_CHANGED(AG_OTA_SUCCESS);
					} else {
						STATE_CHANGED(AG_OTA_FAILED);
					}
					break;
				}
			}
			case AG_OTA_SUCCESS:
				aglink_ota_send_success();
				STATE_CHANGED(AG_OTA_IDLE);
				break;
			case AG_OTA_FAILED:
				aglink_ota_app_send_error();
				STATE_CHANGED(AG_OTA_IDLE);
				break;
			case AG_OTA_IDLE:
				break;
		}
		usleep(100);
	}
	AGLINK_INFO("aglink photo task exit\n");
	pthread_exit(NULL);
}

static int aglink_ota_app_rxcb(uint8_t type, uint8_t *data, int len)
{
	AGLINK_INFO("type: %s, len:%d\n", aglink_ad_type_to_string(type), len);
	switch (type) {
	case AG_AD_TEST:
		break;
	case AG_AD_TIME:
		break;
	case AG_AD_AP:
		SET_MODE(AP);
		GET_RX_DATA(type, data, len);
		CHANGE_SATE(RECVD_AP_INFO, AP);
		break;
	case AG_AD_P2P:
		SET_MODE(P2P);
		GET_RX_DATA(type, data, len);
		CHANGE_SATE(RECVD_P2P_INFO, P2P);
		break;
	case AG_AD_P2P_RESTART: {
			SET_P2P_FLAG(FLAG_P2P_RESTART);
		} break;
	case AG_AD_STA:
		break;
	case AG_AD_AUDIO:
		break;
	case AG_AD_OTA_START:
		if (len < MAX_URL_LEN) {
			memcpy(ota_url, data, len);
			AGLINK_INFO("get ota url %s\n", ota_url);
			STATE_CHANGED(AG_OTA_START);
		} else {
			AGLINK_INFO("ERROR:url len too long.\n");
			error_code = AG_FAILED_PARAMETER;
			STATE_CHANGED(AG_OTA_FAILED);
		}
		break;
	case AG_AD_VIDEO_STOP:
		break;
	case AG_AD_DIS_LINE:
	case AG_AD_PAUSE:
		break;
	case AG_AD_RESUME:
		break;
	default:
		break;
	}
	return 0;
}

void aglink_ota_app_init(int dev_id)
{
	int ret;

	ota_dev_id = dev_id;

	aglink_channel_register_cb(aglink_ota_app_rxcb, ota_dev_id);

	pthread_create(&ag_ota_thread, NULL, aglink_ota_task, NULL);

	ota_app_exit = 0;
}

void aglink_ota_app_deinit(int dev_id)
{
	ota_app_exit = 1;
	pthread_join(ag_ota_thread, NULL);
	aglink_channel_delete(ota_dev_id);
}
