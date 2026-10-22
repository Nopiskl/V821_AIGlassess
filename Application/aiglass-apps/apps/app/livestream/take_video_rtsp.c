/******************************************************************************
  Copyright (C), 2024-2030, Allwinner Tech. Co., Ltd.
 ******************************************************************************
  File Name     :
  Version       : Initial Draft
  Author        : Allwinner PDC-PD5 Team
  Created       : 2024/08/23
  Last Modified :
  Description   :
  Function List :
  History       :
******************************************************************************/

#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>

#include "take_video_rtsp.h"
#include "aw_util.h"
#include "aw_common_isp.h"

#include <linux/videodev2.h>
#include <media/sunxi_camera_v2.h>

#include <ion_mem_alloc.h>
#include "rt_aiservice_pdet.h"
#include "rt_de_hw_scale.h"

#include "rt_motion_pdet.h"
#include "rt_motion_detect_common.h"

#include <aglink_log.h>

// #define DO_ISP_TEST

//#define SUPPORT_PDET

#define PDET_DRAW_ORL_INTERVAL 5

#define PDET_VI_INPUT_W 640
#define PDET_VI_INPUT_H 480

#define PDET_INPUT_W 320
#define PDET_INPUT_H 192
#define PDET_INPUT_C 3
#define PDET_RUN_INTV 5

static struct demo_video_rtsp_context *g_context;
static take_video_rtsp_exit_cb awvideo_exit_cb;

#ifdef DO_ISP_TEST
IspApiTestCtrlConfig mIspTestCfg;
#endif

static double w_ratio_0;
static double h_ratio_0;
static double w_ratio_1;
static double h_ratio_1;

static struct SunxiMemOpsS *pMemops;

static int ion_memory_request(void **vir_addr, unsigned int mem_size)
{
	int fd = -1, ret = 0;
	pMemops = GetMemAdapterOpsS();
	ret = SunxiMemOpen(pMemops);
	if (ret < 0) {
		AGLINK_ERROR("%s(%d): ION Open failed\n", __func__, __LINE__);
		return ret;
	}

	*vir_addr = SunxiMemPalloc(pMemops, mem_size);
	if (*vir_addr == NULL) {
		AGLINK_ERROR("%s(%d): ION Malloc failed\n", __func__, __LINE__);
		return -1;
	}

	fd = SunxiMemGetBufferFd(pMemops, *vir_addr);
	if (fd < 0) {
		AGLINK_ERROR("%s(%d): ION get dmabuf-fd failed\n", __func__, __LINE__);
		return -1;
	}
	AGLINK_INFO("%s(%d): ION_IOC_ALLOC succes, dmabuf-fd = %d, size = %d\n",
				__func__, __LINE__, fd, mem_size);

	return fd;
}

static void ion_memory_release(void *virt_addr)
{
	SunxiMemPfree(pMemops, virt_addr);
	SunxiMemClose(pMemops);
}

static void ion_flush_cache(void *virt_addr, unsigned int mem_size)
{
	AGLINK_DEBUG("IonFlushCache!\n");
	SunxiMemFlushCache(pMemops, virt_addr, mem_size);
}


static int draw_orl(SamplePdetInfo *pPdetInfo)
{
	int i;
	int ret = -1;
	static int cnt;
	RTIspOrl isp_orl;
	memset(&isp_orl, 0, sizeof(RTIspOrl));
	isp_orl.orl_cnt = pPdetInfo->pdet_outputs->num;
	isp_orl.orl_width = 2;

	if (pPdetInfo->pdet_num > 0)
	{
		isp_orl.on = 1;

		int x1 = pPdetInfo->mCoordinate->x * w_ratio_0;
		int y1 = pPdetInfo->mCoordinate->y * h_ratio_0;
		int x2 = (pPdetInfo->mCoordinate->x + pPdetInfo->mCoordinate->width) * w_ratio_0;
		int y2 = (pPdetInfo->mCoordinate->y + pPdetInfo->mCoordinate->height) * h_ratio_0;

		for (i = 0; i < isp_orl.orl_cnt; i++)
		{
			isp_orl.orl_win[i].left = (int)(x1 * w_ratio_1);
			isp_orl.orl_win[i].top = (int)(y1 * h_ratio_1);
			isp_orl.orl_win[i].width = (int)((x2 - x1) * w_ratio_1);
			isp_orl.orl_win[i].height = (int)((y2 - y1) * h_ratio_1);
			isp_orl.orl_win[i].rgb_orl = 0xff0000 >> ((i % 3)*8);

			AGLINK_DEBUG("orl [x,y,w,h]: %d, %d, %d, %d\n", isp_orl.orl_win[i].left, isp_orl.orl_win[i].top, \
													isp_orl.orl_win[i].width, isp_orl.orl_win[i].width);

			ret = AWVideoInput_SetIspOrl(0, &isp_orl);
		}
	}
	else
	{
		// Adjust the value of the PDET_DRAW_ORL_INTERVAL to Control the duration of draw_orl.
		if (cnt % PDET_DRAW_ORL_INTERVAL == 0)
		{
			cnt = 0;

			isp_orl.on = 0;
			isp_orl.orl_cnt = 5;
			isp_orl.orl_width = 6;

			ret = AWVideoInput_SetIspOrl(0, &isp_orl);
		}
		cnt++;
	}

	pPdetInfo->pdet_outputs->num = 0;

	return ret;
}

static int runPdet(int channel, VideoInputConfig* config, MotionPdetParam *pMotionPdetParam, int cnt)
{
	SamplePdetInfo *pPdetInfo = pMotionPdetParam->pPdetInfo;
	SampleDewbInfo *pDewbInfo = pMotionPdetParam->pDewbInfo;
	MotionDetectRectInfo *pMotionLastData = &pMotionPdetParam->mMotionLastData;

	int ret = 0;
	VideoYuvFrame mYuvFrame;
	uint64_t time_1 = 0, time_2 = 0, time_3 = 0, time_4 = 0;
	unsigned int pdet_run_intv = PDET_RUN_INTV;
	static int pdet_cnt = 0;

	AGLINK_DEBUG("runPdet begin!\n");


	while(cnt > 0)
	{
		if (AWVideoInput_Check_Wait_Start(channel)) {
			usleep(200*1000);
			AGLINK_DEBUG("wait start");
			continue;
		}

		AGLINK_DEBUG("ret = %d, cnt = %d; frame info: w&h = %d, %d; phyAddr = %p, %p, %p, virAddr = %p, %p, %p time %lld\n",
				ret, cnt, mYuvFrame.widht, mYuvFrame.height,
				mYuvFrame.phyAddr[0], mYuvFrame.phyAddr[1], mYuvFrame.phyAddr[2],
				mYuvFrame.virAddr[0], mYuvFrame.virAddr[1], mYuvFrame.virAddr[2], get_cur_time_us());

		memset(&mYuvFrame, 0, sizeof(VideoYuvFrame));
		ret = AWVideoInput_GetYuvFrame(channel, &mYuvFrame);
		if (ret != 0) {
			AGLINK_ERROR("Getting yuv failed, break!");
			break;
		}
		else if(0 == cnt % pdet_run_intv)
		{
			/* DE use VIN dma_buf */
			pDewbInfo->mInputInfo.mIonFd = mYuvFrame.fd;

			ret = motion_pdet_prepare(pMotionPdetParam, mYuvFrame.virAddr[0]);
			if(ret < 0)
			{
				AGLINK_ERROR("fatal error! motion_pdet_prepare fail! \n");
				AWVideoInput_ReleaseYuvFrame(channel, &mYuvFrame);
				continue;
			}
			AWVideoInput_ReleaseYuvFrame(channel, &mYuvFrame);

			motion_pdet_run(pMotionPdetParam, pMotionLastData);

			/* draw orl */
			draw_orl(pPdetInfo);
		}
		else
		{
			AWVideoInput_ReleaseYuvFrame(channel, &mYuvFrame);
		}

		cnt--;
	}

	return 0;
}

static struct demo_video_rtsp_context *create_context(void)
{
    struct demo_video_rtsp_context *context = (struct demo_video_rtsp_context *)malloc(sizeof(struct demo_video_rtsp_context));
    if (context) {
        memset(context, 0, sizeof(struct demo_video_rtsp_context));
        return context;
    } else
        return NULL;
}

static void destroy_context(struct demo_video_rtsp_context *context)
{
    if (context) {
        free(context);
        context = NULL;
        g_context = NULL;
    }
}

static void init_obj_config(struct demo_video_rtsp_context *context, RTSP_NET_ROLE role)
{
    struct demo_video_rtsp_config *config;

    config = &context->obj[0].config;
    config->rtsp_id = 0;
    config->id = 0;
    config->rtsp_net_type = role ? RTSP_NET_TYPE_WLAN0 : RTSP_NET_TYPE_P2P_WLAN;

    config = &context->obj[1].config;
    config->rtsp_id = 1;
    config->id = 1;
    config->rtsp_net_type = role ? RTSP_NET_TYPE_WLAN0 : RTSP_NET_TYPE_P2P_WLAN;

    config = &context->obj[2].config;
    config->enable = 0;
    config->rtsp_id = -1;
    config->id = 2;
    config->rtsp_net_type = role ? RTSP_NET_TYPE_WLAN0 : RTSP_NET_TYPE_P2P_WLAN;

    config = &context->obj[3].config;
    config->enable = 0;
    config->rtsp_id = -1;
    config->id = 3;
    config->rtsp_net_type = role ? RTSP_NET_TYPE_WLAN0 : RTSP_NET_TYPE_P2P_WLAN;
}

static void video_stream_cb(const AWVideoInput_StreamInfo* stream_info)
{
    struct demo_video_rtsp_obj *obj = &g_context->obj[stream_info->channel_id];

    if (obj->buf_len < (stream_info->sps_pps_size+stream_info->size0+stream_info->size1+stream_info->size2)) {
        AGLINK_DEBUG("obj %d buf len %d less than stream len %d!\n", obj->config.id, obj->buf_len,
            stream_info->sps_pps_size+stream_info->size0+stream_info->size1+stream_info->size2);
        return;
    }

    obj->data_len = 0;
    if (stream_info->keyframe_flag) {
        memcpy(obj->buf, stream_info->sps_pps_buf, stream_info->sps_pps_size);
        obj->data_len += stream_info->sps_pps_size;
    }
    if (stream_info->data0 && stream_info->size0) {
        memcpy(obj->buf + obj->data_len, stream_info->data0, stream_info->size0);
        obj->data_len += stream_info->size0;
    }
    if (stream_info->data1 && stream_info->size1) {
        memcpy(obj->buf + obj->data_len, stream_info->data1, stream_info->size1);
        obj->data_len += stream_info->size1;
    }
    if (stream_info->data2 && stream_info->size2) {
        memcpy(obj->buf + obj->data_len, stream_info->data2, stream_info->size2);
        obj->data_len += stream_info->size2;
    }

    if (obj->config.rtsp_id >= 0)
    {
        RtspSendDataParam rtsp_send_data;
        memset(&rtsp_send_data, 0, sizeof(rtsp_send_data));
        rtsp_send_data.buf = obj->buf;
        rtsp_send_data.size = obj->data_len;
        rtsp_send_data.frame_type = (stream_info->keyframe_flag ? RTSP_FRAME_DATA_TYPE_I : RTSP_FRAME_DATA_TYPE_P);
        rtsp_send_data.pts = stream_info->pts;
        rtsp_sendData(obj->config.rtsp_id, &rtsp_send_data);
    }
}

static void init_color_space(VencH264VideoSignal *pvenc_video_signal)
{
	pvenc_video_signal->video_format = DEFAULT;
	pvenc_video_signal->full_range_flag = 1;
	pvenc_video_signal->src_colour_primaries = VENC_BT709;
	pvenc_video_signal->dst_colour_primaries = VENC_BT709;
	return ;
}

void AWVideoInput_Channel_Thread_exit(int channel)
{
	if (awvideo_exit_cb)
		awvideo_exit_cb();
}

static int start_obj(struct demo_video_rtsp_obj *obj, VideoInputConfig *config)
{
    int result = 0;

    obj->buf_len = DEMO_VIDEO_RTSP_VBV_THRESH_SIZE;
    obj->buf = malloc(obj->buf_len);
    if (!obj->buf)
    {
        AGLINK_ERROR("malloc buf len %d fail!\n", obj->buf_len);
        return -1;
    }

    if (obj->config.rtsp_id >= 0)
    {
        RtspServerAttr rtsp_attr;
        memset(&rtsp_attr, 0, sizeof(rtsp_attr));
        rtsp_attr.net_type = obj->config.rtsp_net_type;
        rtsp_attr.stream_type = RTSP_STREAM_TYPE_UNICAST;
        rtsp_attr.video_type =
            (config->encodeType == RT_VENC_CODEC_H264) ? RTSP_VIDEO_TYPE_H264 : RTSP_VIDEO_TYPE_H265;;
        rtsp_attr.frame_rate = config->dst_fps;
        result = rtsp_open(obj->config.rtsp_id, &rtsp_attr);
        if (result)
        {
            AGLINK_ERROR("Do rtsp_open fail! ret:%d \n", result);
            free(obj->buf);
            return -1;
        }
    }

	AWVideoInput_SetChannelThreadExitCb(obj->config.id, AWVideoInput_Channel_Thread_exit);

    AWVideoInput_Configure(obj->config.id, config);

    AWVideoInput_CallBack(obj->config.id, video_stream_cb, 1);

    if (config->encodeType == RT_VENC_CODEC_H264) {
        VencH264VideoTiming timing;
        memset(&timing, 0, sizeof(timing));
        timing.fixed_frame_rate_flag = 1;
        timing.num_units_in_tick = 1000;
        timing.time_scale = timing.num_units_in_tick * config->dst_fps * 2;
        AWVideoInput_SetH264VideoTiming(obj->config.id, &timing);
    } else if (config->encodeType == RT_VENC_CODEC_H265) {
        VencH265TimingS timing;
        memset(&timing, 0, sizeof(timing));
        timing.timing_info_present_flag = 1;
        timing.num_units_in_tick = 1000;
        timing.time_scale = timing.num_units_in_tick * config->dst_fps;
        timing.num_ticks_poc_diff_one = timing.num_units_in_tick;
        AWVideoInput_SetH265VideoTiming(obj->config.id, &timing);
    }

    AWVideoInput_Start(obj->config.id, 1);

#ifdef DO_ISP_TEST
	memset(&mIspTestCfg, 0, sizeof(IspApiTestCtrlConfig));
	mIspTestCfg.mTetstIspChannelId = obj->config.id;
	mIspTestCfg.mTestIntervalMs = 1000;
	mIspTestCfg.mIspApiTask.mtaskSchedule = TEST_ISP_ALL;
	mIspTestCfg.mTestAeCfg.mRes.src_width = config->width;
	mIspTestCfg.mTestAeCfg.mRes.src_height = config->height;
	ispApiTestInit(&mIspTestCfg);
#endif

    if (obj->config.rtsp_id >= 0)
    {
        rtsp_start(obj->config.rtsp_id);
    }
    obj->config.enable = 1;

    return 0;
}

static void stop_obj(struct demo_video_rtsp_obj *obj)
{
    if (!obj->config.enable)
        return;
#ifdef DO_ISP_TEST
	ispApiTestExit(&mIspTestCfg);
#endif
    AWVideoInput_Start(obj->config.id, 0);
    if (obj->config.rtsp_id >= 0)
    {
        rtsp_stop(obj->config.rtsp_id);
        rtsp_close(obj->config.rtsp_id);
    }
    if (obj->buf)
        free(obj->buf);
}

void take_video_rtsp_exit_callback_set(take_video_rtsp_exit_cb cb)
{
	awvideo_exit_cb = cb;
}

int take_video_rtsp_start(RTSP_NET_ROLE role)
{
    int ret = 0;

    struct demo_video_rtsp_context *context = create_context();
    int i = 0;
    if (!context)
    {
        AGLINK_ERROR("create demo video rtsp context fail!");
        return -1;
    }

	demo_video_param mparam;
    memset(&mparam, 0, sizeof(demo_video_param));
    mparam.encoder_num = 1500;
    mparam.c0_src_w = 1920;
    mparam.c0_src_h = 1080;
    mparam.c0_dst_w = 1920;
    mparam.c0_dst_h = 1080;
    mparam.c0_encoder_format = 0;
	mparam.c1_encoder_format = -1;
	mparam.pixelformat = 12;
	mparam.pixelformat_1 = RT_PIXEL_NUM;
	mparam.pixelformat_2 = RT_PIXEL_NUM;
	mparam.use_vipp_num = 0;
	mparam.use_vipp_num_1 = 1;
	mparam.use_vipp_num_2 = 4;
	mparam.share_buf_num = 2;
	mparam.share_buf_num_1 = 2;
	mparam.enable_sharp = 1;
	mparam.c0_bitrate = 1500000;
	mparam.color_space = 3;
	mparam.vin_buf_num = 4;
	mparam.enable_tdm_raw = MAIN_CHANNEL_TDM_RAW_ENABLE;
	mparam.tdm_rxbuf_cnt = MAIN_CHANNEL_TDM_RXBUF_CNT;
	mparam.src_fps = 30;
    mparam.dst_fps = 30;
	mparam.product_mode = MAIN_CHANNEL_PRODUCT_MODE;

	check_param(&mparam);

    AGLINK_DEBUG("*demo param: c0: w&h = %d, %d; encode_format = %d, bitrate = %d",
			mparam.c0_src_w, mparam.c0_src_h, mparam.c0_encoder_format, mparam.c0_bitrate);
	AGLINK_DEBUG("*demo param: c1: w&h = %d, %d; encode_format = %d, bitrate = %d",
			mparam.c1_src_w, mparam.c1_src_h, mparam.c1_encoder_format, mparam.c1_bitrate);

    VideoInputConfig config_0;
    memset(&config_0, 0, sizeof(VideoInputConfig));
    config_0.channelId = mparam.use_vipp_num;
	config_0.fps     = mparam.src_fps;
	config_0.dst_fps = mparam.dst_fps;
	config_0.gop     = MAIN_CHANNEL_GOP;
	config_0.product_mode = mparam.product_mode;
	config_0.mRcMode = mparam.rc_mode;
	if (config_0.mRcMode == AW_VBR)
    {
		config_0.vbr_param.uMaxBitRate = mparam.c0_bitrate/1024; //* kb
		config_0.vbr_param.nMovingTh = 20;
		config_0.vbr_param.nQuality = 10;
		config_0.vbr_param.nIFrmBitsCoef = 10;
		config_0.vbr_param.nPFrmBitsCoef = 10;
	}
    config_0.qp_range.nMinqp = MAIN_CHANNEL_I_MIN_QP;
	config_0.qp_range.nMaxqp = MAIN_CHANNEL_I_MAX_QP;
	config_0.qp_range.nMinPqp = MAIN_CHANNEL_P_MIN_QP;
	config_0.qp_range.nMaxPqp = MAIN_CHANNEL_P_MAX_QP;
	config_0.qp_range.nQpInit = MAIN_CHANNEL_INIT_QP;
	config_0.profile = VENC_H264ProfileMain;
	config_0.level   = VENC_H264Level51;
    config_0.pixelformat = mparam.pixelformat;
    config_0.enable_sharp = mparam.enable_sharp;
	config_0.bonline_channel = mparam.bonline_channel;
	config_0.share_buf_num = mparam.share_buf_num;
	config_0.en_16_align_fill_data = 0;
	config_0.breduce_refrecmem = MAIN_CHANNEL_REDUCE_REFREC_MEM;
	config_0.enable_tdm_raw = mparam.enable_tdm_raw;
	config_0.tdm_rxbuf_cnt = mparam.tdm_rxbuf_cnt;
    AGLINK_DEBUG("ch%d enable_tdm_raw:%d, tdm_rxbuf_cnt:%d", config_0.channelId,
		config_0.enable_tdm_raw, config_0.tdm_rxbuf_cnt);
	init_color_space(&config_0.venc_video_signal);
    config_0.output_mode = OUTPUT_MODE_STREAM;
    config_0.width      = mparam.c0_src_w;
	config_0.height     = mparam.c0_src_h;
	config_0.dst_width      = mparam.c0_dst_w;
	config_0.dst_height 	= mparam.c0_dst_h;
	config_0.bitrate	= mparam.c0_bitrate;
	if (config_0.fps)
	{
		config_0.vbv_thresh_size = config_0.bitrate / 8 / config_0.fps * 15;
	}
	config_0.vbv_buf_size = config_0.bitrate / 8 * VE_VBV_CACHE_TIME + config_0.vbv_thresh_size;
	config_0.encodeType = mparam.c0_encoder_format;
	config_0.drop_frame_num = 0;
	config_0.enable_wdr = 0;
#if ENABLE_TEST_SET_CAMERA_MOVE_STATUS
	config_0.enable_ve2isp_linkage = 1;
#else
	config_0.enable_ve2isp_linkage = 0;
#endif
    if (config_0.encodeType == 1)   //jpg encode
    {
		config_0.jpg_quality = 80;
		if (mparam.encoder_num > 1)
        {
			config_0.jpg_mode = mparam.jpg_mode = 1;
			config_0.bitrate = 12*1024*1024;
			config_0.bit_rate_range.bitRateMin = 10*1024*1024;
			config_0.bit_rate_range.bitRateMax = 14*1024*1024;
		}
	}

    g_context = context;
    init_obj_config(context, role);

    AWVideoInput_Init();
    if (0 != start_obj(&context->obj[0], &config_0))
        goto _exit1;

	VideoInputConfig config_1;
	memset(&config_1, 0, sizeof(VideoInputConfig));
    if (mparam.en_second_channel)
    {
		config_1.channelId = mparam.use_vipp_num_1;
		config_1.fps     = mparam.src_fps;
		config_1.gop     = mparam.dst_fps;
		config_1.product_mode = mparam.product_mode;;
		config_1.mRcMode     = mparam.rc_mode;;
		if (config_1.mRcMode == AW_VBR)
        {
			config_1.vbr_param.uMaxBitRate = mparam.c1_bitrate/1024; //* kb
			config_1.vbr_param.nMovingTh = 20;
			config_1.vbr_param.nQuality = 10;
			config_1.vbr_param.nIFrmBitsCoef = 10;
			config_1.vbr_param.nPFrmBitsCoef = 10;
		}
		config_1.qp_range.nMinqp = MAIN_CHANNEL_I_MIN_QP;
		config_1.qp_range.nMaxqp = MAIN_CHANNEL_I_MAX_QP;
		config_1.qp_range.nMinPqp = MAIN_CHANNEL_P_MIN_QP;
		config_1.qp_range.nMaxPqp = MAIN_CHANNEL_P_MAX_QP;
		config_1.qp_range.nQpInit = MAIN_CHANNEL_INIT_QP;
		config_1.profile = VENC_H264ProfileMain;
		config_1.level   = VENC_H264Level51;

		init_color_space(&config_1.venc_video_signal);
		config_1.output_mode = OUTPUT_MODE_STREAM;
		config_1.width      = mparam.c1_src_w;
		config_1.height 	= mparam.c1_src_h;
		config_1.bitrate	= mparam.c1_bitrate;
		if (config_1.fps)
		{
			config_1.vbv_thresh_size = config_1.bitrate / 8 / config_1.fps * 15;
		}
		config_1.vbv_buf_size = config_1.bitrate / 8 * VE_VBV_CACHE_TIME + config_1.vbv_thresh_size;
		config_1.encodeType = mparam.c1_encoder_format;
		config_1.pixelformat = mparam.pixelformat_1;
		config_1.enable_sharp = 1;
		config_1.bonline_channel = mparam.bonline_channel_1;
		config_1.share_buf_num = mparam.share_buf_num_1;
		config_1.vin_buf_num = mparam.vin_buf_num;//for example, not set, kernel will set to 3
		config_1.breduce_refrecmem = 1;
#if ENABLE_TEST_SET_CAMERA_MOVE_STATUS
		config_1.enable_ve2isp_linkage = 1;
#else
		config_1.enable_ve2isp_linkage = 0;
#endif
        if (config_1.encodeType == 1)   //jpg encode
        {
            config_1.jpg_quality = 80;
            if (mparam.encoder_num > 1)
            {
                config_1.jpg_mode = mparam.jpg_mode = 1;
                config_1.bitrate = 12*1024*1024;
                config_1.bit_rate_range.bitRateMin = 10*1024*1024;
                config_1.bit_rate_range.bitRateMax = 14*1024*1024;
            }
        }
        if (0 != start_obj(&context->obj[1], &config_1))
            goto _exit0;
    }

#ifdef SUPPORT_PDET
    // pdet
	w_ratio_0 =  (double)PDET_VI_INPUT_W / PDET_INPUT_W;
	h_ratio_0 =  (double)PDET_VI_INPUT_H / PDET_INPUT_H;

	w_ratio_1 =  (double)mparam.c0_src_w / PDET_VI_INPUT_W;
	h_ratio_1 =  (double)mparam.c0_src_h / PDET_VI_INPUT_H;

    VideoInputConfig config_2;
	memset(&config_2, 0, sizeof(VideoInputConfig));
	config_2.channelId = mparam.use_vipp_num_2;
	config_2.gop     = 30;
    config_2.pixelformat = 14;

	config_2.venc_video_signal.video_format = DEFAULT;
	switch(mparam.color_space)
	{
		case V4L2_COLORSPACE_JPEG:
		{
			config_2.venc_video_signal.full_range_flag = 1;
			config_2.venc_video_signal.src_colour_primaries = VENC_YCC;
			config_2.venc_video_signal.dst_colour_primaries = VENC_YCC;
			break;
		}
		case V4L2_COLORSPACE_REC709:
		{
			config_2.venc_video_signal.full_range_flag = 1;
			config_2.venc_video_signal.src_colour_primaries = VENC_BT709;
			config_2.venc_video_signal.dst_colour_primaries = VENC_BT709;
			break;
		}
		case V4L2_COLORSPACE_REC709_PART_RANGE:
		{
			config_2.venc_video_signal.full_range_flag = 0;
			config_2.venc_video_signal.src_colour_primaries = VENC_BT709;
			config_2.venc_video_signal.dst_colour_primaries = VENC_BT709;
			break;
		}
		default:
		{
			config_2.venc_video_signal.full_range_flag = 1;
			config_2.venc_video_signal.src_colour_primaries = VENC_BT709;
			config_2.venc_video_signal.dst_colour_primaries = VENC_BT709;
			break;
		}
	}
	config_2.output_mode = OUTPUT_MODE_YUV;
	config_2.width      = PDET_VI_INPUT_W;
	config_2.height     = PDET_VI_INPUT_H;
	config_2.bitrate    = 1024; //* kb
	config_2.encodeType = 0;	//0:h264 1:mjpeg 2:h265

	SampleDewbInfo mDewbInfo;
	memset(&mDewbInfo, 0, sizeof(SampleDewbInfo));
	mDewbInfo.mInputInfo.mWidth = config_2.width;
	mDewbInfo.mInputInfo.mHeight = config_2.height;
	mDewbInfo.mOutputInfo.mWidth = PDET_INPUT_W;
	mDewbInfo.mOutputInfo.mHeight = PDET_INPUT_H;

	SamplePdetInfo mPdetInfo;
	memset(&mPdetInfo, 0, sizeof(SamplePdetInfo));
	mPdetInfo.pdet_conf_thres = 0.3;
	mPdetInfo.pdet_input_w = PDET_INPUT_W;
	mPdetInfo.pdet_input_h = PDET_INPUT_H;
	mPdetInfo.pdet_input_c = PDET_INPUT_C;

	MotionPdetParam *pMotionPdetParam = (MotionPdetParam *)malloc(sizeof(MotionPdetParam));
	if (!pMotionPdetParam)
	{
		AGLINK_ERROR("malloc pMotionPdetParam fail\n");
		return -1;
	}

	MotionDetectContext *pMotionDetectContext = &pMotionPdetParam->mMotionDetectContext;

	pMotionPdetParam->pDewbInfo = &mDewbInfo;
	pMotionPdetParam->pPdetInfo = &mPdetInfo;

	pMotionDetectContext->mMdStaCfg.nPicWidth = ALIGN_XXB(16, config_2.width);
	pMotionDetectContext->mMdStaCfg.nPicStride = ALIGN_XXB(16, config_2.width);
	pMotionDetectContext->mMdStaCfg.nPicHeight = ALIGN_XXB(16, config_2.height);
	pMotionDetectContext->mMdDynCfg.nHorRegionNum = pMotionDetectContext->mMdStaCfg.nPicWidth / 16;
	pMotionDetectContext->mMdDynCfg.nVerRegionNum = pMotionDetectContext->mMdStaCfg.nPicHeight / 16;
	pMotionDetectContext->mMdDynCfg.bDefaultCfgDis = 1;
	pMotionDetectContext->mMdDynCfg.bMorphologEn = 0;
	pMotionDetectContext->mMdDynCfg.nLargeMadTh = 10;
	pMotionDetectContext->mMdDynCfg.nBackgroundWeight = 0;
	pMotionDetectContext->mMdDynCfg.fLargeMadRatioTh = 10.0;

	pMotionDetectContext->mJudgeMotionNum = 50;
	pMotionDetectContext->mMotionDynamicThres = 0.43;
	pMotionDetectContext->mMotionPdetThres = 0.4;

	motion_pdet_set_sensitivity(pMotionPdetParam, MOTION_PDET_SENSITIVITY_MIDDLE);

	AGLINK_DEBUG("mDewbInfo, in w:%d, h:%d, out w:%d, h:%d\n", mDewbInfo.mInputInfo.mWidth, mDewbInfo.mInputInfo.mHeight,
				mDewbInfo.mOutputInfo.mWidth, mDewbInfo.mOutputInfo.mHeight);
	AGLINK_DEBUG("mPdetInfo, w:%d h:%d\n", mPdetInfo.pdet_input_w, mPdetInfo.pdet_input_h);

	motion_pdet_init(pMotionPdetParam);

	AWVideoInput_Init();
	if(AWVideoInput_Configure(config_2.channelId, &config_2))
	{
		AGLINK_ERROR("config err, exit!");
		goto _exit1;
	}

	AWVideoInput_Start(config_2.channelId, 1);

	if (runPdet(config_2.channelId, &config_2, pMotionPdetParam, mparam.encoder_num) != 0) {
		goto _exit1;
	}

	AWVideoInput_Start(mparam.use_vipp_num, 0);
#endif
    return 0;

_exit0:
    for (i = 0; i < DEMO_VIDEO_RTSP_MAX_OBJ_NUM; i++)
        stop_obj(&context->obj[i]);
_exit1:
#ifdef SUPPORT_PDET
    motion_pdet_deinit(pMotionPdetParam);

	if (pMotionPdetParam)
	{
		free(pMotionPdetParam);
		pMotionPdetParam = NULL;
	}
#endif
    AWVideoInput_DeInit();
    destroy_context(context);

    return -1;
}

void take_video_rtsp_stop(void)
{
    int i = 0;
	if (!g_context)
		return;
    for (i = 0; i < DEMO_VIDEO_RTSP_MAX_OBJ_NUM; i++)
        stop_obj(&g_context->obj[i]);
#ifdef SUPPORT_PDET
    motion_pdet_deinit(pMotionPdetParam);

	if (pMotionPdetParam)
	{
		free(pMotionPdetParam);
		pMotionPdetParam = NULL;
	}
#endif
    AWVideoInput_DeInit();
    destroy_context(g_context);
}
