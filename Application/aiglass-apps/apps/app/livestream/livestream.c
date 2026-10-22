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
#include <stdlib.h>
#include <string.h>

#include <pthread.h>
#include <signal.h>
#include <dirent.h>
#include <sys/types.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <ctype.h>

#include "wifimg.h"
#include <wifi_log.h>
#include "aglink/ag_wrap.h"
#include <aglink/ag_proto.h>
#include <sys/stat.h>
#include <aglink_log.h>
#include "take_video_rtsp.h"

typedef enum livestream_mode_state {
	AG_LIVESTREAM_CH_CLOSE = 0,
	AG_LIVESTREAM_CH_NEW,
	AG_LIVESTREAM_CH_CONNECTING,
	AG_LIVESTREAM_CH_CONNECTION,
	AG_LIVESTREAM_MEDIA_NEW,
	AG_LIVESTREAM_MEDIA_INFO_SYNC,
	AG_LIVESTREAM_MEDIA_CONNECTION,
	AG_LIVESTREAM_MEDIA_ALIVE,
	AG_LIVESTREAM_MEDIA_STOP,
	AG_LIVESTREAM_CH_FAILED,
	AG_LIVESTREAM_CH_DISCONNECT,
	AG_LIVESTREAM_CH_IDLE,
} livestream_mode_state;

const char* aglink_livestream_app_state_to_string(livestream_mode_state state);

static void aglink_send_generic_response(int rsp_code);
static void aglink_livestream_md_end(void);
static void aglink_livestream_md_sdp(void);
static void aglink_livestream_app_sync(void);
static void aglink_livestream_ap_start_success(void);
static void aglink_livestream_p2p_start_success(void);

static int dl_dev_id = 0;

static pthread_t ag_livestream_thread;

volatile int livestream_app_exit;

volatile livestream_mode_state livestream_state = AG_LIVESTREAM_CH_NEW;

#define STATE_CHANGED(curr_state)								          \
do {                                                                      \
	livestream_mode_state last_livestream_state = livestream_state;             \
	if (livestream_state != curr_state) {                                   \
		livestream_state = curr_state;                                      \
		AGLINK_INFO("livestream %s -->%s \n",                               \
				aglink_livestream_app_state_to_string(last_livestream_state), \
				aglink_livestream_app_state_to_string(livestream_state));     \
	}                                                                     \
} while(0)

/*=======================================*/
#define TASK_SLEEP_PER (5 * 1000) // 5 ms
#define LOOP_PRINT_PER (1000) // 5 s

#define DEFAULT_AP_NAME "AiGlass"
#define DEFAULT_AP_PSK "Aa123456"
#define DEFAULT_AP_CHAN 6

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

char *state_str[MAX][(P2P_STATE_MAX-AP_STATE_MAX>0? P2P_STATE_MAX : AP_STATE_MAX)] =
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
		"DEVICE_CONNECT",
		"DEVICE_CONNECT_SUCCESS",
		"DEVICE_CONNECT_FAILED",
		"DEVICE_CONNECTD_GC",
		"DEVICE_CONNECTD_GO",
		"DEVICE_DISCONNECT",
		"STOP_P2P",
	}
};

#define DEFAULT_MODE AP
static int wireless_mode = DEFAULT_MODE;
#define MODE wireless_mode
#define SET_MODE(m) \
do { \
	MODE = m; \
} while(0)

static uint8_t *rx_data = NULL;
static int data_len = 0;
static int data_type = 0;

#define RX_DATA rx_data
#define DATA_LEN data_len
#define DATA_TYPE data_type

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

static uint8_t state[MAX] = {INIT, INIT};
static uint8_t state_max[MAX] = {AP_STATE_MAX, P2P_STATE_MAX};
#define GET_STATE() \
({ \
	state[MODE]; \
})

#define CHANGE_SATE(s, m) \
do { \
	uint8_t last_state = state[m]; \
	if (last_state != (s)) { \
		if ((s) >= state_max[m]) { \
			AGLINK_INFO("UNKOWN STATE: %d, change to -> INIT\n", state[m]); \
			state[m] = INIT; \
			break; \
		} \
		state[m] = (s); \
		AGLINK_INFO("%s -> %s\n", state_str[m][last_state], state_str[m][state[m]]); \
	} \
} while(0)

void livestream_over(void)
{
	AGLINK_INFO("livestream over.\n");
	/* wait for deleting file */
	//STATE_CHANGED(AG_LIVESTREAM_CH_IDLE);
}

//Callback functions cannot do high-load and blocking actions
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
			STATE_CHANGED(AG_LIVESTREAM_CH_DISCONNECT);
			CHANGE_SATE(DEVICE_DISCONNECT, P2P);
			WMG_INFO("p2p state: disconnected\n");
			break;
		}
	}

	if (msg->id == WIFI_MSG_ID_P2P_CN_EVENT) {
		WMG_DEBUG("get p2p event:(%d) ", msg->data.p2p_event);
		switch(msg->data.p2p_event) {
			case WIFI_P2P_GROUP_REMOVED:
			STATE_CHANGED(AG_LIVESTREAM_CH_DISCONNECT);
			CHANGE_SATE(DEVICE_DISCONNECT, P2P);
			WMG_INFO("p2p event: disconnected\n");
			break;
		}
	}
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

#define RECONNECT 1
enum p2p_role_type {
	P2P_GO = 0,
	P2P_GC
};
static uint8_t p2p_role = P2P_GO;
#define P2P_INTENT 1
#define DISCOVER_TIME 120 //s
#define P2P_CONNECT_TIMEOUT (DISCOVER_TIME)

#define DEFAULT_P2P_NUM 5
#define P2P_NAME_MAX_LEN 32
#define DEFAULT_P2P_NAME "allwinner-p2p-demo-dev"
static char p2p_dev_buf[P2P_NAME_MAX_LEN + 1] = {DEFAULT_P2P_NAME};

static inline void p2p_start_discover(int discover_time)
{
	check_timeout_start();
	//wifi_p2p_find(discover_time);
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
	p2p_config.discover_time = DISCOVER_TIME;
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
	p2p_config.discover_time = DISCOVER_TIME;
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

//////============
const char* aglink_livestream_app_state_to_string(livestream_mode_state state)
{
	switch (state) {
	case AG_LIVESTREAM_CH_CLOSE:
		return "close";
	case AG_LIVESTREAM_CH_NEW:
		return "new";
	case AG_LIVESTREAM_CH_CONNECTING:
		return "connecting";
	case AG_LIVESTREAM_CH_CONNECTION:
		return "connection";
	case AG_LIVESTREAM_MEDIA_NEW:
		return "media new";
	case AG_LIVESTREAM_MEDIA_INFO_SYNC:
		return "media sync";
	case AG_LIVESTREAM_MEDIA_CONNECTION:
		return "media connection";
	case AG_LIVESTREAM_MEDIA_ALIVE:
		return "media keepalive";
	case AG_LIVESTREAM_CH_FAILED:
		return "failed";
	case AG_LIVESTREAM_MEDIA_STOP:
		return "media stop";
	case AG_LIVESTREAM_CH_DISCONNECT:
		return "disconnect";
	case AG_LIVESTREAM_CH_IDLE:
		return "idle";
	}
	return "no found";
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
			aglink_livestream_p2p_start_success();
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
			if (check_timeout(P2P_CONNECT_TIMEOUT)) {
				AGLINK_INFO("discover timeout!\n");
				aglink_send_generic_response(AG_LIVESTREAM_TIMEOUT);
				CHANGE_SATE(STOP_P2P, P2P);
			}
		} break;
		case DEVICE_CONNECTD_GC: {
			AGLINK_INFO("connected! is GC\n");
			p2p_role = P2P_GC;
			STATE_CHANGED(AG_LIVESTREAM_CH_CONNECTION);
			//CHANGE_SATE(INIT, P2P);
		} break;
		case DEVICE_CONNECTD_GO: {
			AGLINK_INFO("connected! is GO\n");
			p2p_role = P2P_GO;
			STATE_CHANGED(AG_LIVESTREAM_CH_CONNECTION);
			//CHANGE_SATE(INIT, P2P);
		} break;
		case DEVICE_DISCONNECT: {
			AGLINK_INFO("device disconnect!\n");
#if RECONNECT
			if (restart_p2p()) {
				AGLINK_INFO("restart p2p failed!\n");
				CHANGE_SATE(STOP_P2P, P2P);
				break;
			}
			/* set reconnect */
			CHANGE_SATE(DEVICE_START_DISCOVERY, P2P);
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
			aglink_livestream_ap_start_success();
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
				CHANGE_SATE(AG_LIVESTREAM_CH_CLOSE, AP);
			}

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
				if (!(loop_cnt ++ % LOOP_PRINT_PER)) {
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
			STATE_CHANGED(AG_LIVESTREAM_CH_CONNECTION);
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

static void* aglink_livestream_task(void* data)
{
	AGLINK_INFO("livestream task start.\n");
	while (!livestream_app_exit) {
		switch (livestream_state) {
		case AG_LIVESTREAM_CH_CLOSE:
			break;
		case AG_LIVESTREAM_CH_NEW:
			aglink_livestream_app_sync();
			STATE_CHANGED(AG_LIVESTREAM_CH_CONNECTING);
			break;
		case AG_LIVESTREAM_CH_CONNECTING:
			switch (GET_STATE()) {
				case INIT: {
					CHANGE_SATE(WAIT_INFO, MODE);
				} break;
				case WAIT_INFO: {
					static int loop_cnt = 0;
					if (!(loop_cnt ++ % LOOP_PRINT_PER)) {
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
			break;
		case AG_LIVESTREAM_CH_CONNECTION:
			STATE_CHANGED(AG_LIVESTREAM_MEDIA_NEW);
			break;
		case AG_LIVESTREAM_MEDIA_NEW:
			retry_start: {
					int ret;
					static uint8_t retry = 0, retry_max = 5;
					if (MODE == P2P && p2p_role == P2P_GC)
						ret = take_video_rtsp_start(RTSP_IS_P2P_GC);
					else
						ret = take_video_rtsp_start(RTSP_IS_NOT_P2P_GC);
					if (ret) { //rtsp server start failed.
						AGLINK_INFO("rtsp server start failed!."\
								"restart rtsp server, retry: %d\n", retry);
						usleep(100 * 1000); // 100 ms
						if (retry < retry_max)
							goto retry_start;
					} else { //rtsp server start success.
						retry = 0;
						STATE_CHANGED(AG_LIVESTREAM_MEDIA_INFO_SYNC);
					}
				}
			break;
		case AG_LIVESTREAM_MEDIA_INFO_SYNC: {
				aglink_livestream_md_sdp();
				STATE_CHANGED(AG_LIVESTREAM_MEDIA_CONNECTION);
			}
			break;
		case AG_LIVESTREAM_MEDIA_CONNECTION: {
				STATE_CHANGED(AG_LIVESTREAM_MEDIA_ALIVE);
			} break;
		case AG_LIVESTREAM_MEDIA_ALIVE: {
				static int loop_cnt = 0;
				if (!(loop_cnt ++ % LOOP_PRINT_PER)) {
					//AGLINK_INFO("keepalive.\n");
					aglink_send_generic_response(AG_LIVESTREAM_KEEPALIVE);
					loop_cnt = 1;
				}
			} break;
		case AG_LIVESTREAM_MEDIA_STOP:
			take_video_rtsp_stop();
			switch (MODE) {
				case AP: {
					CHANGE_SATE(STOP_AP, AP);
					ap_newstate(GET_STATE());
				} break;
				case P2P: {
					CHANGE_SATE(STOP_P2P, P2P);
					p2p_newstate(GET_STATE());
				} break;
				default : AGLINK_INFO("unkown mode: %d\n", MODE); break;
			}
			STATE_CHANGED(AG_LIVESTREAM_CH_IDLE);
			break;
		case AG_LIVESTREAM_CH_FAILED:
			aglink_send_generic_response(AG_LIVESTREAM_NOSTREAM);
			STATE_CHANGED(AG_LIVESTREAM_MEDIA_STOP);
			break;
		case AG_LIVESTREAM_CH_DISCONNECT: {
				STATE_CHANGED(AG_LIVESTREAM_CH_CONNECTING);
			}
			break;
		case AG_LIVESTREAM_CH_IDLE: {
 				system("sync");
				aglink_livestream_md_end();
				STATE_CHANGED(AG_LIVESTREAM_CH_CONNECTING);
			} break;
		default:
			break;
		}
		usleep(TASK_SLEEP_PER);
	}
	AGLINK_INFO("aglink livestream task exit\n");
	pthread_exit(NULL);
}

static int aglink_livestream_app_rxcb(uint8_t type, uint8_t *data, int len)
{
	AGLINK_INFO("type: %s\n", aglink_ad_type_to_string(type));
	int i;
	switch (type) {
	case AG_AD_TEST:
		break;
	case AG_AD_AP: {
			SET_MODE(AP);
			GET_RX_DATA(type, data, len);
			CHANGE_SATE(RECVD_AP_INFO, AP);
		} break;
	case AG_AD_P2P: {
			SET_MODE(P2P);
			GET_RX_DATA(type, data, len);
			CHANGE_SATE(RECVD_P2P_INFO, P2P);
		} break;
	case AG_AD_STA:
		break;
	case AG_AD_AUDIO:
		break;
	case AG_AD_OTA_START:
		break;
	case AG_AD_VIDEO_STOP:
		break;
	case AG_AD_RT_MEDIA_STOP:
		STATE_CHANGED(AG_LIVESTREAM_MEDIA_STOP);
		break;
	case AG_AD_DIS_LINE:
		break;
	case AG_AD_TIME:
		break;
	case AG_AD_PAUSE:
		break;
	case AG_AD_RESUME:
		break;
	default:
		break;
	}
	return 0;
}

static void aglink_send_generic_response(int rsp_code)
{
	struct ag_generic_rsp generic_rsp = {
		.mode = AG_MODE_LIVESTREAM,
		.code = rsp_code,
	};

	aglink_tx_data(dl_dev_id, AG_VD_GENERIC_RESPONSE,
			(uint8_t *)&generic_rsp, sizeof(struct ag_generic_rsp));
}

static void aglink_livestream_md_end(void)
{
	aglink_tx_data(dl_dev_id, AG_VD_POWER_OFF,
			NULL, 0);
}

static void aglink_livestream_md_sdp(void)
{
	RtspUrl url;
	struct ag_sdp md_sdp;

	rtsp_url_get(&url);

	memcpy(md_sdp.ip, (uint8_t *)&url.ip, sizeof(url.ip));
	memcpy(md_sdp.path, url.path, sizeof(url.path));
	md_sdp.port = url.port;
	md_sdp.protocol = AG_PROTO_RTSP;

	AGLINK_INFO("sdp ip: %d.%d.%d.%d\n", md_sdp.ip[0], md_sdp.ip[1], md_sdp.ip[2], md_sdp.ip[3]);

	aglink_tx_data(dl_dev_id, AG_VD_SDP,
			(uint8_t *)&md_sdp, sizeof(struct ag_sdp));
}

static void aglink_livestream_p2p_start_success(void)
{
	struct ag_wifi_p2p_success p2p_success_para;

	if (wifi_get_mac(NULL, p2p_success_para.mac) != WMG_STATUS_SUCCESS) {
		AGLINK_INFO("mac addr get failed\n");
	}

	p2p_success_para.mac[5] = p2p_success_para.mac[5] + 1;

	aglink_tx_data(dl_dev_id, AG_VD_P2P_SUCCESS,
			(uint8_t *)&p2p_success_para, sizeof(struct ag_wifi_p2p_success));
}

static void aglink_livestream_ap_start_success(void)
{
	aglink_tx_data(dl_dev_id, AG_VD_AP_SUCCESS,
			NULL, 0);
}

static void aglink_livestream_app_sync(void)
{

	struct ag_sync sync = {
		.mode = AG_MODE_LIVESTREAM,
		.str = "U_OK",
	};

	aglink_tx_data(dl_dev_id, AG_VD_SYNC_2,
			(uint8_t *)&sync, sizeof(struct ag_sync));

}

void aglink_livestream_exit_cb(void)
{
	STATE_CHANGED(AG_LIVESTREAM_CH_FAILED);
}

void aglink_livestream_app_init(int dev_id)
{
	int ret;

	dl_dev_id = dev_id;

	take_video_rtsp_exit_callback_set(aglink_livestream_exit_cb);

	aglink_channel_register_cb(aglink_livestream_app_rxcb, dl_dev_id);

#if 0
	if (aglink_channel_create(dl_dev_id))
		return ;
#endif

	pthread_create(&ag_livestream_thread, NULL, aglink_livestream_task, NULL);

	livestream_app_exit = 0;
}

void aglink_livestream_app_deinit(void)
{
	livestream_app_exit = 1;
	pthread_join(ag_livestream_thread, NULL);
	take_video_rtsp_stop();
	aglink_channel_delete(dl_dev_id);
}
