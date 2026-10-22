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
#ifndef __TAKE_VIDEO_RTSP_H__
#define __TAKE_VIDEO_RTSP_H__

#include <semaphore.h>
#include <sys/time.h>

#include "AW_VideoInput_API.h"
#include "rtsp_server.h"

#define VE_VBV_CACHE_TIME                 (4)  // unit:seconds, exp:1,2,3,4...
#define DEMO_VIDEO_RTSP_MAX_OBJ_NUM       (4)
#define DEMO_VIDEO_RTSP_VBV_THRESH_SIZE   (200*1024)


struct demo_video_rtsp_config {
    int enable;
    int rtsp_id;
    int id;
    RTSP_NET_TYPE rtsp_net_type;
};

struct demo_video_rtsp_obj {
    unsigned char *buf;
    int buf_len;
    int data_len;
    struct demo_video_rtsp_config config;
    void (*send_to_rtsp)(void *thiz, const AWVideoInput_StreamInfo* stream_info);
};

struct demo_video_rtsp_context {
    sem_t wait_sem;
    struct demo_video_rtsp_obj obj[DEMO_VIDEO_RTSP_MAX_OBJ_NUM];
};

typedef void (*take_video_rtsp_exit_cb)(void);

int take_video_rtsp_start(RTSP_NET_ROLE role);
void take_video_rtsp_stop(void);
void take_video_rtsp_exit_callback_set(take_video_rtsp_exit_cb cb);

#endif
