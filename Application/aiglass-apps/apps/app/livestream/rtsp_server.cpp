/******************************************************************************
  Copyright (C), 2001-2016, Allwinner Tech. Co., Ltd.
 ******************************************************************************
  File Name     :
  Version       : Initial Draft
  Author        : Allwinner BU3-PD2 Team
  Created       : 2016/11/4
  Last Modified :
  Description   :
  Function List :
  History       :
******************************************************************************/

//#include <time.h>
//#include <sys/types.h>
//#include <sys/stat.h>
//#include <fcntl.h>

#include <unistd.h>
#include <signal.h>
#include <time.h>
#include <string.h>

#include <arpa/inet.h>
#include <sys/ioctl.h>
#include <net/if.h>
#include <ifaddrs.h>
#include "MediaStream.h"
#include "TinyServer.h"

#include "rtsp_server.h"

#include <aglink_log.h>

#define RTSP_SERVER_CHANNEL_NUM  (4)

static TinyServer *gpRtspContext[RTSP_SERVER_CHANNEL_NUM];
static MediaStream *gpRtspStream[RTSP_SERVER_CHANNEL_NUM];

static RtspUrl RtspUrlParam;

void rtsp_url_get(RtspUrl *url)
{
	url->ip = RtspUrlParam.ip;
	url->port = RtspUrlParam.port;
	memcpy(url->path, RtspUrlParam.path, sizeof(RtspUrlParam.path));
}

static int get_net_dev_ip(const char *netdev_name, char *ip)
{
    int            fd     = -1;
    unsigned int   u32ip  =  0;
    struct ifreq   ifr;
    struct in_addr sin_addr;

    if (NULL == netdev_name || NULL == ip) {
        AGLINK_ERROR("%s,%d: Input netdev_name or ip is NULL!\n", __func__, __LINE__);
        return -1;
    }

    fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd <= 0) {
        AGLINK_ERROR("%s,%d: Fail to create socket! errno[%d] errinfo[%s]\n", __func__, __LINE__,
                errno, strerror(errno));
        return -1;
    }

    memset(&ifr, 0, sizeof(struct ifreq));
    strncpy(ifr.ifr_name, netdev_name, sizeof(ifr.ifr_name));
    ifr.ifr_addr.sa_family = AF_INET;
    if (ioctl(fd, SIOCGIFADDR, &ifr) < 0) {
        AGLINK_ERROR("%s,%d: Fail to ioctl SIOCGIFADDR. devname[%s] errno[%d] errinfo[%s]\n", __func__, __LINE__,
                netdev_name, errno, strerror(errno));
        close(fd);
        return -1;
    }

    close(fd);

    u32ip = ntohl(((struct sockaddr_in *)&ifr.ifr_addr)->sin_addr.s_addr);

	RtspUrlParam.ip = ((u32ip & 0xff) << 24) |
						((u32ip & 0xff00) << 8) |
						((u32ip & 0xff0000) >> 8) |
						((u32ip & 0xff000000) >> 24) ;

    sin_addr.s_addr = htonl(u32ip);
    sprintf(ip, "%s", inet_ntoa(sin_addr));

    return 0;
}

static int ipstr_2_hex(char *ip_str, uint32_t *ip)
{
	struct in_addr addr;

	if (inet_pton(AF_INET, ip_str, &addr) != 1) {
		AGLINK_INFO("Invalid IP address.\n");
		return -1;
	}

	*ip = (uint32_t)addr.s_addr;

	return 0;
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

	ipstr_2_hex(ip_str, &RtspUrlParam.ip);

	return 0;
}

int rtsp_open(int id, RtspServerAttr *rtsp_attr)
{
    int ret  = 0;
    int port = 8554;
    char net_name[32] = {0};
    char ip[64] = {0};
    std::string ip_addr;

    if (id >= RTSP_SERVER_CHANNEL_NUM)
    {
        AGLINK_ERROR("%s,%d: invalid id %d >= %d\n", __func__,__LINE__, id, RTSP_SERVER_CHANNEL_NUM);
        return -1;
    }

    if (RTSP_NET_TYPE_LO == rtsp_attr->net_type)
        strncpy(net_name, "lo", 31);
    else if (RTSP_NET_TYPE_ETH0 == rtsp_attr->net_type)
        strncpy(net_name, "eth0", 31);
    else if (RTSP_NET_TYPE_BR0 == rtsp_attr->net_type)
        strncpy(net_name, "br0", 31);
    else if (RTSP_NET_TYPE_WLAN0 == rtsp_attr->net_type)
        strncpy(net_name, "wlan0", 31);
    else if (RTSP_NET_TYPE_P2P_WLAN == rtsp_attr->net_type)
        strncpy(net_name, "p2p-wlan", 31);
    else
        AGLINK_ERROR("%s,%d: invalid net type %d\n", __func__,__LINE__, rtsp_attr->net_type);

    // get ip addr and create server
	if (RTSP_NET_TYPE_P2P_WLAN == rtsp_attr->net_type)
		ret = get_ifip(net_name, ip);
	else
		ret = get_net_dev_ip(net_name, ip);
    if (ret)
    {
        AGLINK_ERROR("%s get_net_dev_ip fail! ret:%d\n", net_name, ret);
        return -1;
    }
    AGLINK_DEBUG("%s ip:%s \n", net_name, ip);

    ip_addr = ip;
    TinyServer *rtsp = TinyServer::createServer(ip_addr, port);
    if (NULL == rtsp)
    {
        AGLINK_ERROR("%s,%d: rtsp create server failed!\n", __func__,__LINE__);
        return -1;
    }
     gpRtspContext[id] = rtsp;

    // create Media Stream
    MediaStream::MediaStreamAttr attr;

    if (RTSP_VIDEO_TYPE_H264 == rtsp_attr->video_type)
        attr.videoType  = MediaStream::MediaStreamAttr::VIDEO_TYPE_H264;
    else if (RTSP_VIDEO_TYPE_H265 == rtsp_attr->video_type)
        attr.videoType  = MediaStream::MediaStreamAttr::VIDEO_TYPE_H265;
    else
        AGLINK_DEBUG("%s,%d: invalid video type %d\n", __func__,__LINE__, rtsp_attr->video_type);

    if (RTSP_AUDIO_TYPE_AAC == rtsp_attr->audio_type)
        attr.audioType  = MediaStream::MediaStreamAttr::AUDIO_TYPE_AAC;
    else
        AGLINK_DEBUG("%s,%d: invalid audio type %d\n", __func__,__LINE__, rtsp_attr->audio_type);

    if (RTSP_STREAM_TYPE_UNICAST == rtsp_attr->stream_type)
        attr.streamType = MediaStream::MediaStreamAttr::STREAM_TYPE_UNICAST;
    else if (RTSP_STREAM_TYPE_MULTICAST == rtsp_attr->stream_type)
        attr.streamType = MediaStream::MediaStreamAttr::STREAM_TYPE_MULTICAST;
    else
        AGLINK_DEBUG("%s,%d: invalid stream type %d\n", __func__,__LINE__, rtsp_attr->stream_type);

    char ch_str[32] = {0};
    sprintf(ch_str, "ch%d", id);
    MediaStream *stream = rtsp->createMediaStream(ch_str, attr);
    if (NULL == stream)
    {
        AGLINK_ERROR("%s,%d: rtsp create media stream failed!\n", __func__,__LINE__);
        return -1;
    }
    gpRtspStream[id] = stream;

    stream->setVideoFrameRate(rtsp_attr->frame_rate);

	RtspUrlParam.port = port;
	memcpy(RtspUrlParam.path, ch_str, sizeof(RtspUrlParam.path));

    printf("============================================================\n");
    printf("   rtsp://%s:%d/%s  \n", ip_addr.c_str(), port, ch_str);
    printf("============================================================\n");

    return 0;
}

void rtsp_close(int id)
{
    if (id >= RTSP_SERVER_CHANNEL_NUM)
    {
        AGLINK_ERROR("%s,%d: invalid id %d >= %d\n", __func__,__LINE__, id, RTSP_SERVER_CHANNEL_NUM);
        return;
    }
    AGLINK_DEBUG("close rtsp id %d\n", id);
    if (gpRtspStream[id])
    {
        delete gpRtspStream[id];
        gpRtspStream[id] = NULL;
    }
    if (gpRtspContext[id])
    {
        delete gpRtspContext[id];
        gpRtspContext[id] = NULL;
    }
}

void rtsp_start(int id)
{
    if (id >= RTSP_SERVER_CHANNEL_NUM)
    {
        AGLINK_ERROR("%s,%d: invalid id %d >= %d\n", __func__,__LINE__, id, RTSP_SERVER_CHANNEL_NUM);
        return;
    }
    TinyServer *rtsp = gpRtspContext[id];
    MediaStream *stream = gpRtspStream[id];

    std::string url_ = stream->streamURL();
    rtsp->runWithNewThread();
}

void rtsp_stop(int id)
{
    if (id >= RTSP_SERVER_CHANNEL_NUM)
    {
        AGLINK_ERROR("%s,%d: invalid id %d >= %d\n", __func__,__LINE__, id, RTSP_SERVER_CHANNEL_NUM);
        return;
    }
    TinyServer *rtsp = gpRtspContext[id];
    rtsp->stop();
}

void rtsp_sendData(int id, RtspSendDataParam *param)
{
    if (id >= RTSP_SERVER_CHANNEL_NUM)
    {
        AGLINK_ERROR("%s,%d: invalid id %d >= %d\n", __func__,__LINE__, id, RTSP_SERVER_CHANNEL_NUM);
        return;
    }
    if (NULL == param)
    {
        AGLINK_ERROR("%s,%d: invalid param!\n", __func__,__LINE__);
        return;
    }

    MediaStream *stream = gpRtspStream[id];
    if (stream != NULL)
    {
        if (RTSP_FRAME_DATA_TYPE_I == param->frame_type)
        {
            stream->appendVideoData(param->buf, param->size, param->pts, MediaStream::FRAME_DATA_TYPE_I);
        }
        else if (RTSP_FRAME_DATA_TYPE_P == param->frame_type)
        {
            stream->appendVideoData(param->buf, param->size, param->pts, MediaStream::FRAME_DATA_TYPE_P);
        }
        else
        {
            AGLINK_DEBUG("rtsp id %d, frame_type %d is NOT support!\n", id, param->frame_type);
        }
    }
}
