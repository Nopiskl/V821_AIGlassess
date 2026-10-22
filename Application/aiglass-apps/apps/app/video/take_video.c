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
#include "aglink/ag_wrap.h"
#include "aglink/aglink.h"
#include <aglink/ag_proto.h>
#include <aglink_log.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <semaphore.h>
#include <sys/types.h>
#include <sys/prctl.h>
#include <sys/time.h>

#include <linux/videodev2.h>
#include <media/sunxi_camera_v2.h>

#include <plat_log.h>
#include <confparser.h>
#include <aenc_sw_lib.h>
#include <mm_comm_venc.h>
#include <media_common_aio.h>
#include <media_common_vcodec.h>
#include <mpi_sys.h>
#include <mpi_mux.h>
#include <mpi_ai.h>
#include <mpi_aenc.h>

#include <AW_VideoInput_API.h>

#include "rt-media/uapi_rt_media.h"

#include <mm_common.h>
#include <mm_comm_sys.h>
#include <mm_comm_mux.h>
#include <mm_comm_aio.h>
#include <mm_comm_aenc.h>
#include <tmessage.h>
#include <RecorderMode.h>
#include <SystemBase.h>


#define MAX_FILE_PATH_SIZE  (128)
#define MAX_DIR_PATH_SIZE  (256)
#define TAKE_VIDEO_PATCH "/mnt/UDISK/"
#define TAKE_VIDEO_DIR_PATCH "video-"
#define TAKE_VIDEO_TXT_FILE "/mnt/UDISK/media.config"
#define TAKE_FRAME_NAME_HEAD "frame-"
#define TAKE_GYRO_NAME_HEAD "gyro-"
#define VIDEO_SUBDIR_NAME_POINT "/proc/video_subdir_name"
#define WAIT_VIDEO_PATCH_TIMEOUT_MS 2000
#define WAIT_MOUNT_UDISK 30000
#define PCM_BITWIDTH (16)
#define PCM_PCMSAMPLERATE (16000)
#define GET_CHANNEL_STATE_CNT_MAX (10)

int aglink_take_video(void);
int aglink_mpp_init(void);

typedef enum
{
	DemoAvAIGlass_Stop = 0,
}DemoAvAIGlassMsgType;

typedef struct DemoAvAIGlassCmdLineParam
{
	char strConfigFilePath[MAX_FILE_PATH_SIZE];
} DemoAvAIGlassCmdLineParam;

typedef struct DemoAvAIGlassConfig
{
	PAYLOAD_TYPE_E eAudioEncodeType;
	int nAudioBitrate;

	char strVideoFile[MAX_FILE_PATH_SIZE];
	char strVideoSubDirName[MAX_FILE_PATH_SIZE];
	char strVideoFilePath[MAX_DIR_PATH_SIZE];
	int nTestDuration; //unit:s
} DemoAvAIGlassConfig;

typedef struct DemoAvAIGlassThumb
{
	char *pJpegBuf;
	int nJpegLen;
}DemoAvAIGlassThumb;

typedef struct DemoAvAIGlassContext
{
	DemoAvAIGlassCmdLineParam stCmdLineParam;
	DemoAvAIGlassConfig stConfig;

	int nChnId;
	PresetRtMediaChnConfig presetChnConfig;
	VideoInputConfig AWVideoInputChnConfig;
	catch_jpeg_config stCatchJpegConfig;
	char strPicturePath[MAX_FILE_PATH_SIZE];

	MPP_SYS_CONF_S stSysConf;
	AENC_CHN nAEncChnId;
	AENC_CHN_ATTR_S stAEncAttr;

	char strVideoFileDir[MAX_FILE_PATH_SIZE];	//tail don't contain '/', e.g.,/mnt/extsd/aiglass
	char strVideoFileName[MAX_FILE_PATH_SIZE];
	MEDIA_FILE_FORMAT_E eFileFormat;
	MUX_CHN nMuxChn;
	MUX_CHN_ATTR_S stMuxChnAttr;
	RecordFileDurationPolicy ePolicy;
	int bMuxChnSetSpsppsFlag;
	pthread_mutex_t MuxChnLock;
	int nMuxVideoStreamId;
	int nMuxAudioStreamId;

	pthread_t StreamDispatchThreadId;
	message_queue_t  StreamDispatchCmdQueue;

	//int max_bitstream_count; //max encoded video frame count.
	int stream_count_0; //count encoded video frame number, for channel0.
	cdx_sem_t finish_sem;
	//int video_finish_flag; //indicate video frame count is finish.
	uint64_t pre_pts; //record previous video frame pts. unit:us
	int pre_pts_in_seconds; //record last video frame pts diff in seconds.
	int cb_stream_cnt_in_seconds; //the stream count between two seconds.
} DemoAvAIGlassContext;

typedef enum video_mode_state {
	AG_VEDIA_CH_CLOSE = 0,
	AG_VEDIA_CH_NEW,
	AG_VEDIA_CH_CONNECTING,
	AG_VEDIA_CH_CONNECTION,
	AG_VEDIA_START,
	AG_VEDIA_AUDIO_IN,
	AG_VEDIA_STOP,
	AG_VEDIA_END,
	AG_VEDIA_CH_FAILED,
	AG_VEDIA_CH_DISCONNECT,
	AG_VEDIA_CH_IDLE,
} video_mode_state;

static int video_dev_id = 0;

static pthread_t ag_video_thread;

volatile int video_app_exit;

volatile video_mode_state video_state = AG_VEDIA_CH_NEW;

static int error_code = AG_SUCCESS;

static int nFrameFd = -1;

static u8 gyro_en = 0;

char time_buf[75];

const char* aglink_video_app_state_to_string(video_mode_state state)
{
	switch (state) {
	case AG_VEDIA_CH_CLOSE:
		return "close";
	case AG_VEDIA_CH_NEW:
		return "new";
	case AG_VEDIA_CH_CONNECTING:
		return "connecting";
	case AG_VEDIA_CH_CONNECTION:
		return "connection";
	case AG_VEDIA_START:
		return "start";
	case AG_VEDIA_AUDIO_IN:
		return "audio in";
	case AG_VEDIA_STOP:
		return "stop";
	case AG_VEDIA_END:
		return "end";
	case AG_VEDIA_CH_FAILED:
		return "failed";
	case AG_VEDIA_CH_DISCONNECT:
		return "disconnect";
	case AG_VEDIA_CH_IDLE:
		return "idle";
	}
	return "no found";
}


#define STATE_CHANGED(curr_state)                               \
	if (video_state != curr_state) {                            \
		AGLINK_INFO("video %s -->%s \n",                        \
				aglink_video_app_state_to_string(video_state),   \
				aglink_video_app_state_to_string(curr_state)); \
		video_state = curr_state;                               \
	}

static void aglink_video_app_start(void);

static void aglink_video_app_send_thumb(DemoAvAIGlassThumb *AiGlassThumb);

static DemoAvAIGlassThumb *aglink_take_thumb(uint16_t width, uint16_t height);

static void video_exit(void);

static void aglink_video_app_send_fail(void);

extern uint64_t get_cur_time_us();

static int aglink_take_gyro(char *node, size_t len)
{
	int videoSubdirFd = open(VIDEO_SUBDIR_NAME_POINT, O_WRONLY);
	if (videoSubdirFd < 0) {
		AGLINK_ERROR("fatal error! failed to open %s\n", VIDEO_SUBDIR_NAME_POINT);
		return -1;
	} else {
		write(videoSubdirFd, node, len);
		close(videoSubdirFd);
		return 0;
	}
}

static void* aglink_video_task(void* data)
{
	DemoAvAIGlassThumb *AiGlassThumb = NULL;

	while (!video_app_exit) {
		switch (video_state) {
		case AG_VEDIA_CH_CLOSE:
		case AG_VEDIA_CH_NEW:
			aglink_mpp_init();
			if (error_code != AG_SUCCESS) {
				STATE_CHANGED(AG_VEDIA_CH_FAILED);
				break;
			}
			aglink_video_app_start();
			STATE_CHANGED(AG_VEDIA_START);
			break;
		case AG_VEDIA_CH_CONNECTING:
			break;
		case AG_VEDIA_CH_CONNECTION:
		case AG_VEDIA_START:{
			struct ag_time *ag_mode_info = aglink_get_mode(video_dev_id);
			if (ag_mode_info->width && ag_mode_info->height)
				AiGlassThumb = aglink_take_thumb(ag_mode_info->width, ag_mode_info->height);
			else
				AGLINK_INFO("width&height is null, no need thumb\n");
			aglink_take_video();
			if (error_code != AG_SUCCESS) {
				if (AiGlassThumb != NULL) {
					free(AiGlassThumb->pJpegBuf);
					free(AiGlassThumb);
				}
				STATE_CHANGED(AG_VEDIA_CH_FAILED);
				break;
			}
			STATE_CHANGED(AG_VEDIA_END);
		}break;
		case AG_VEDIA_AUDIO_IN:
		case AG_VEDIA_STOP:
			STATE_CHANGED(AG_VEDIA_END);
			break;
		case AG_VEDIA_END:
			aglink_take_gyro("NULL", strlen("NULL"));
			//if (gyro_en)
			//	aglink_gyro_license_encrypt(time_buf);
			aglink_video_app_send_thumb(AiGlassThumb);
			if (AiGlassThumb != NULL) {
					free(AiGlassThumb->pJpegBuf);
					free(AiGlassThumb);
			}
			STATE_CHANGED(AG_VEDIA_CH_IDLE);
			break;
		case AG_VEDIA_CH_FAILED:
			aglink_video_app_send_fail();
			STATE_CHANGED(AG_VEDIA_CH_IDLE);
			break;
		case AG_VEDIA_CH_DISCONNECT:
		case AG_VEDIA_CH_IDLE:
		default:
			break;
		}
		usleep(10 * 1000);
	}
	AGLINK_INFO("aglink photo task exit\n");
	pthread_exit(NULL);
}

static void sendPcmData(uint8_t *data, int len);
static int aglink_video_app_rxcb(uint8_t type, uint8_t *data, int len)
{
	AGLINK_DEBUG("type: %s\n", aglink_ad_type_to_string(type));
	switch (type) {
	case AG_AD_AP:
	case AG_AD_STA:
	case AG_AD_AUDIO:
		if (video_state == AG_VEDIA_CH_CONNECTING) {
			STATE_CHANGED(AG_VEDIA_START);
		}
		if ((video_state == AG_VEDIA_START) && (len > 0)) {
			sendPcmData(data, len);
		}
		break;
	case AG_AD_OTA_START:
		break;
	case AG_AD_VIDEO_STOP:
		video_exit();
		STATE_CHANGED(AG_VEDIA_STOP);
	default:
		break;
	}
	return 0;
}

static void aglink_video_app_start(void)
{
	aglink_tx_data(video_dev_id, AG_VD_VIDEO_START,
			NULL, 0);
}

static void aglink_video_app_send_fail(void)
{
	struct ag_generic_rsp error = {
		.mode = AG_MODE_VIDEO,
		.code = error_code,
	};

	aglink_tx_data(video_dev_id, AG_VD_GENERIC_RESPONSE,
			(uint8_t *)&error, sizeof(struct ag_generic_rsp));
	aglink_tx_data(video_dev_id, AG_VD_POWER_OFF, NULL, 0);
}

static void aglink_video_app_mount_error(void)
{
	struct ag_generic_rsp error = {
		.mode = AG_MODE_VIDEO,
		.code = AG_FAILED_MOUNT_DISK,
	};

	aglink_tx_data(video_dev_id, AG_VD_GENERIC_RESPONSE,
			(uint8_t *)&error, sizeof(struct ag_generic_rsp));
}

static void aglink_video_app_send_thumb(DemoAvAIGlassThumb *AiGlassThumb)
{
	char *pJpegBuf = NULL;
	int nJpegLen = 0;

	if (AiGlassThumb != NULL) {
		pJpegBuf = AiGlassThumb->pJpegBuf;
		nJpegLen = AiGlassThumb->nJpegLen;
	}

	aglink_tx_data(video_dev_id, AG_VD_THUMB,
			(uint8_t *)pJpegBuf, nJpegLen);

	aglink_tx_data(video_dev_id, AG_VD_THUMB_END,
			NULL, 0);

	aglink_tx_data(video_dev_id, AG_VD_POWER_OFF, NULL, 0);
}

void aglink_video_app_init(int dev_id)
{
	int ret;

	video_dev_id = dev_id;

	aglink_channel_register_cb(aglink_video_app_rxcb, video_dev_id);

#if 0
	if (aglink_channel_create(video_dev_id))
		return ;
#endif

	pthread_create(&ag_video_thread, NULL, aglink_video_task, NULL);

	video_app_exit = 0;
}

void aglink_video_app_deinit(int dev_id)
{
	video_app_exit = 1;
	pthread_join(ag_video_thread, NULL);
	aglink_channel_delete(video_dev_id);
}

static void get_timestamp_string(char *buf, size_t len)
{
    time_t rawtime;
	struct tm *timeinfo;
	struct timespec ts;

	memset(&rawtime, 0, sizeof(time_t));
	memset(&ts, 0, sizeof(struct timespec));
	time(&rawtime);
	clock_gettime(CLOCK_REALTIME, &ts);
	timeinfo = localtime(&rawtime);
	snprintf(buf, len - 1, "%04d%02d%02d%02d%02d%02d%03ld",
			timeinfo->tm_year + 1900,
			timeinfo->tm_mon + 1,
			timeinfo->tm_mday,
			timeinfo->tm_hour,
			timeinfo->tm_min,
			timeinfo->tm_sec,
			(ts.tv_nsec / 1000000));
}

static DemoAvAIGlassContext *gpDemoAvAIGlassContext = NULL;
static void video_exit(void)
{
    if(gpDemoAvAIGlassContext != NULL) {
        cdx_sem_up(&gpDemoAvAIGlassContext->finish_sem);
    }
}

/**
  create directory. if directory is exist, return success.

  @return
	0: success
	-1: fail
*/
static int CreateFolder(const char* pStrFolderPath)
{
	if(NULL == pStrFolderPath || 0 == strlen(pStrFolderPath)) {
		AGLINK_ERROR("folder path is wrong!");
		return FAILURE;
	}
	//check folder existence
	struct stat sb;
	if (stat(pStrFolderPath, &sb) == 0) {
		if(S_ISDIR(sb.st_mode)) {
			return SUCCESS;
		} else {
			AGLINK_ERROR("fatal error! [%s] is exist, but mode[0x%x] is not directory!", pStrFolderPath, (int)sb.st_mode);
			return FAILURE;
		}
	}
	//create folder if necessary
	int ret = mkdir(pStrFolderPath, S_IRWXU | S_IRWXG | S_IRWXO);
	if (!ret) {
		AGLINK_INFO("create folder[%s] success", pStrFolderPath);
		return SUCCESS;
	} else {
		AGLINK_ERROR("fatal error! create folder[%s] failed!", pStrFolderPath);
		return FAILURE;
	}
}

static ERRORTYPE MPPCallbackWrapper(void *cookie, MPP_CHN_S *pChn, MPP_EVENT_TYPE event, void *pEventData)
{
	DemoAvAIGlassContext *pCtx = (DemoAvAIGlassContext*)cookie;
	ERRORTYPE ret;
	if(MOD_ID_AI == pChn->mModId) {
		switch(event) {
			case MPP_EVENT_CAPTURE_AUDIO_DATA: {
				AISendDataInfo * pUserData = (AISendDataInfo *)pEventData;
				break;
			}
			default: {
				AGLINK_ERROR("fatal error! unknown event[0x%x] from channel[0x%x][0x%x][0x%x]!\n", event, pChn->mModId, pChn->mDevId, pChn->mChnId);
				break;
			}
		}
	} else if(MOD_ID_AENC == pChn->mModId) {
		alogw("aencChn[%d] not support notify recorder by AEnc with event(%d)\n", pChn->mChnId, event);
	} else if(MOD_ID_MUX == pChn->mModId) {
		switch(event) {
			case MPP_EVENT_RECORD_DONE: {
				int nMuxChn = *(int*)pEventData;
				AGLINK_INFO("muxChn[%d-%d] record file done.\n", pCtx->nMuxChn, nMuxChn);
				break;
			}
			case MPP_EVENT_NEED_NEXT_FD: {
				int nMuxChn = *(int*)pEventData;
				AGLINK_INFO("muxChn[%d-%d] need next fd.\n", pCtx->nMuxChn, nMuxChn);
				break;
			}
			case MPP_EVENT_BSFRAME_AVAILABLE: {
				AGLINK_INFO("mux bs frame available\n");
				break;
			}
			case MPP_EVENT_WRITE_DISK_ERROR: {
				int nMuxChn = *(int*)pEventData;
				AGLINK_ERROR("muxChn[%d] write disk error [%s]!\n", nMuxChn, strerror(errno));
				if (errno == ENOSPC) {
					error_code = AG_FAILED_DISK_FULL;
					video_exit();
				}
				break;
			}
			default: {
				AGLINK_ERROR("fatal error! unknown event[0x%x]\n", event);
				break;
			}
		}
	} else {
		AGLINK_INFO("unknown mpp module:0x%x\n", pChn->mModId);
	}

	return SUCCESS;
}

static PAYLOAD_TYPE_E map_RT_VENC_CODEC_TYPE_to_PAYLOAD_TYPE_E(RT_VENC_CODEC_TYPE eRtVencType)
{
	PAYLOAD_TYPE_E ePayloadType;
	switch(eRtVencType) {
		case RT_VENC_CODEC_H264: {
			ePayloadType = PT_H264;
			break;
		}
		case RT_VENC_CODEC_H265: {
			ePayloadType = PT_H265;
			break;
		}
		case RT_VENC_CODEC_JPEG: {
			ePayloadType = PT_MJPEG;
			break;
		}
		default: {
			AGLINK_ERROR("fatal error! unknown RT venc type:%d", eRtVencType);
			ePayloadType = PT_H264;
			break;
		}
	}
	return ePayloadType;
}

static int check_access(const char *path, long timeout_ms)
{
	struct timeval start_time, current_time;
	long elapsed_time_ms = 0;

	gettimeofday(&start_time, NULL);

	while (1) {
		if (access(path, W_OK) == 0)
			return (int)elapsed_time_ms;

		gettimeofday(&current_time, NULL);
		elapsed_time_ms = (current_time.tv_sec - start_time.tv_sec) * 1000L +
						  (current_time.tv_usec - start_time.tv_usec) / 1000L;

		if (elapsed_time_ms >= timeout_ms)
			return -1;

		usleep(10 * 1000);
	}
}


static int CreateMuxChn(DemoAvAIGlassContext *pContext)
{
	ERRORTYPE ret;
	char strVideoSubDirPath[128];
	char frameFilePath[MAX_DIR_PATH_SIZE];

	get_timestamp_string(time_buf, sizeof(time_buf));
	if (access(VIDEO_SUBDIR_NAME_POINT, F_OK) == 0)
		gyro_en = 1;
	snprintf(pContext->stConfig.strVideoFile, sizeof(pContext->stConfig.strVideoFile) - 1, "%s.mp4",time_buf);
	if (gyro_en) {
		snprintf(pContext->stConfig.strVideoSubDirName, sizeof(pContext->stConfig.strVideoSubDirName) - 1, "%s%s",
				TAKE_VIDEO_DIR_PATCH, time_buf);
		snprintf(strVideoSubDirPath, sizeof(strVideoSubDirPath) - 1, "%s%s%s/",
				TAKE_VIDEO_PATCH, TAKE_VIDEO_DIR_PATCH, time_buf);
		snprintf(pContext->stConfig.strVideoFilePath, sizeof(pContext->stConfig.strVideoFilePath) - 1, "%s%s%s/%s.mp4",
						TAKE_VIDEO_PATCH, TAKE_VIDEO_DIR_PATCH, time_buf, time_buf);
	} else {
		snprintf(pContext->stConfig.strVideoFilePath, sizeof(pContext->stConfig.strVideoFilePath) - 1, "%s/%s.mp4",
						TAKE_VIDEO_PATCH, time_buf);
	}

	const char *pFilePath = pContext->stConfig.strVideoFilePath;

	if (gyro_en) {
		if (mkdir(strVideoSubDirPath, 0755) != 0) {
			if (errno != EEXIST) {
				AGLINK_ERROR("mkdir %s failed\n", pContext->stConfig.strVideoSubDirName);
				return -errno;
			}
		}
	}

	int nMediaFd = open(pFilePath, O_RDWR | O_CREAT | O_TRUNC | O_NOATIME, 0666);
	AGLINK_INFO("%s\n", pFilePath);
	if (nMediaFd < 0) {
		AGLINK_ERROR("fatal error! failed to open %s\n", pFilePath);
	}

	if (aglink_take_gyro(time_buf, strlen(time_buf)) == 0) {
		snprintf(frameFilePath, sizeof(frameFilePath) - 1, "%s%s%s/%s%s.txt",
					TAKE_VIDEO_PATCH, TAKE_VIDEO_DIR_PATCH, time_buf, TAKE_FRAME_NAME_HEAD, time_buf);
		nFrameFd = open(frameFilePath, O_RDWR | O_CREAT| O_SYNC | O_TRUNC, 0666);
		if (nFrameFd < 0) {
			AGLINK_ERROR("fatal error! failed to open %s\n", frameFilePath);
		}
	}

	pContext->nMuxChn = 0;
	memset(&pContext->stMuxChnAttr, 0, sizeof(MUX_CHN_ATTR_S));
	pContext->stMuxChnAttr.mVideoAttrValidNum = 1;
	pContext->stMuxChnAttr.mVideoAttr[0].mWidth = pContext->AWVideoInputChnConfig.dst_width;
	pContext->stMuxChnAttr.mVideoAttr[0].mHeight = pContext->AWVideoInputChnConfig.dst_height;
	pContext->stMuxChnAttr.mVideoAttr[0].mVideoFrmRate = pContext->AWVideoInputChnConfig.fps*1000;
	pContext->stMuxChnAttr.mVideoAttr[0].mMaxKeyInterval = pContext->AWVideoInputChnConfig.gop;
	pContext->stMuxChnAttr.mVideoAttr[0].mVideoEncodeType = map_RT_VENC_CODEC_TYPE_to_PAYLOAD_TYPE_E(
		pContext->AWVideoInputChnConfig.encodeType);
	pContext->stMuxChnAttr.mVideoAttr[0].mRotateDegree = 0;
	//we can do this, muxer use veChn to link spspps and streamId, muxer don't need to operate veChn.
	//so we can use any number as veChn.
	pContext->stMuxChnAttr.mVideoAttr[0].mVeChn = pContext->AWVideoInputChnConfig.channelId;

	pContext->stMuxChnAttr.mChannels = 1;
	pContext->stMuxChnAttr.mBitsPerSample = PCM_BITWIDTH;
	pContext->stMuxChnAttr.mSamplesPerFrame = MAXDECODESAMPLE;
	pContext->stMuxChnAttr.mSampleRate = PCM_PCMSAMPLERATE;
	pContext->stMuxChnAttr.mAudioEncodeType = pContext->stConfig.eAudioEncodeType;

	pContext->stMuxChnAttr.mTextEncodeType = PT_MAX;

	//pContext->mMuxChnAttr.mMuxerId = 0;
	pContext->stMuxChnAttr.mMediaFileFormat = pContext->eFileFormat;
	pContext->stMuxChnAttr.mMaxFileDuration = 0;
	pContext->stMuxChnAttr.mCallbackOutFlag = FALSE;
	pContext->stMuxChnAttr.mFsWriteMode = FSWRITEMODE_SIMPLECACHE;
	pContext->stMuxChnAttr.mSimpleCacheSize = 64*1024;
	pContext->stMuxChnAttr.mAddRepairInfo = 0;
	pContext->stMuxChnAttr.mMaxFrmsTagInterval = 0;
	ret = AW_MPI_MUX_CreateChn(pContext->nMuxChn, &pContext->stMuxChnAttr, nMediaFd, 0);
	if(ret != SUCCESS) {
		AGLINK_ERROR("fatal error! create muxChn[%d] fail ret[0x%x]!\n", pContext->nMuxChn, ret);
	}
	close(nMediaFd);
	nMediaFd = -1;
	pContext->ePolicy = RecordFileDurationPolicy_MinDuration;
	ret = AW_MPI_MUX_SetSwitchFileDurationPolicy(pContext->nMuxChn, pContext->ePolicy);
	if(ret != SUCCESS) {
		AGLINK_ERROR("fatal error! set file duration policy[%d] to muxChn[%d] fail!\n", pContext->ePolicy, pContext->nMuxChn);
	}
	ret = AW_MPI_MUX_SetVeChnBindStreamId(pContext->nMuxChn, pContext->AWVideoInputChnConfig.channelId, pContext->nMuxVideoStreamId);
	if(ret != SUCCESS) {
		AGLINK_ERROR("fatal error! set VeChnBindStreamId to muxChn[%d] fail!\n", pContext->nMuxChn);
	}
	MPPCallbackInfo cbInfo;
	cbInfo.cookie = (void*)pContext;
	cbInfo.callback = (MPPCallbackFuncType)&MPPCallbackWrapper;
	AW_MPI_MUX_RegisterCallback(pContext->nMuxChn, &cbInfo);
	return 0;
}

static int SetSpsppsToMuxer(DemoAvAIGlassContext *pContext)
{
	AGLINK_INFO("try to get spspps from rtmedia and set to muxChn\n");
	if(RT_VENC_CODEC_H264 == pContext->AWVideoInputChnConfig.encodeType) {
		sps_pps_data_info stSpsPpsInfo;
		int result = AWVideoInput_GetSpsPpsInfo(pContext->AWVideoInputChnConfig.channelId, &stSpsPpsInfo);
		if(0 == result) {
			VencHeaderData stH264SpsPpsInfo;
			memset(&stH264SpsPpsInfo,0,sizeof(stH264SpsPpsInfo));
			stH264SpsPpsInfo.pBuffer = stSpsPpsInfo.buf;
			stH264SpsPpsInfo.nLength = stSpsPpsInfo.size;
			AGLINK_INFO("AWVideoInputChn[%d]: H264SpsPpsInfo.nLength: %d\n", pContext->AWVideoInputChnConfig.channelId,
				stH264SpsPpsInfo.nLength);
			AW_MPI_MUX_SetH264SpsPpsInfo(pContext->nMuxChn, pContext->AWVideoInputChnConfig.channelId, &stH264SpsPpsInfo);
		} else {
			AGLINK_ERROR("fatal error! get spspps from rtmedia channel[%d] fail\n", pContext->AWVideoInputChnConfig.channelId);
		}
	} else if (RT_VENC_CODEC_H265 == pContext->AWVideoInputChnConfig.encodeType) {
		sps_pps_data_info stSpsPpsInfo;
		int result = AWVideoInput_GetSpsPpsInfo(pContext->AWVideoInputChnConfig.channelId, &stSpsPpsInfo);
		if(0 == result) {
			VencHeaderData stH265SpsPpsInfo;
			memset(&stH265SpsPpsInfo,0,sizeof(stH265SpsPpsInfo));
			stH265SpsPpsInfo.pBuffer = stSpsPpsInfo.buf;
			stH265SpsPpsInfo.nLength = stSpsPpsInfo.size;
			AGLINK_INFO("AWVideoInputChn[%d]: H265SpsPpsInfo.nLength: %d\n", pContext->AWVideoInputChnConfig.channelId,
				stH265SpsPpsInfo.nLength);
			AW_MPI_MUX_SetH265SpsPpsInfo(pContext->nMuxChn, pContext->AWVideoInputChnConfig.channelId, &stH265SpsPpsInfo);
		} else {
			AGLINK_ERROR("fatal error! get spspps from AWVideoInputChn[%d] fail\n", pContext->AWVideoInputChnConfig.channelId);
		}
	}
	return 0;
}

static void video_stream_cb(const AWVideoInput_StreamInfo* stream_info)
{
	ERRORTYPE ret;
	static int frame_cnt;
	char frame_txt[32];
	DemoAvAIGlassContext *pContext = gpDemoAvAIGlassContext;
	pthread_mutex_lock(&pContext->MuxChnLock);
	if (0 == pContext->bMuxChnSetSpsppsFlag) {
		SetSpsppsToMuxer(pContext);
		pContext->bMuxChnSetSpsppsFlag = 1;
		AW_MPI_MUX_StartChn(pContext->nMuxChn);
	}
	pthread_mutex_unlock(&pContext->MuxChnLock);

	if (stream_info->data0 != NULL && stream_info->size0 > 0) {
		uint64_t cur_time = get_cur_time_us();

		frame_cnt++;
		uint64_t pts = stream_info->pts; //unit:us
		uint64_t pts_in_seconds = pts/1000/1000;
		uint64_t pts_in_useconds = pts % 1000000;
		uint32_t exposureTime = stream_info->exposureTime; //unit:us
		uint32_t exp_in_mseconds = exposureTime/1000;
		uint32_t exp_in_useconds = exposureTime % 1000;

		if (nFrameFd >= 0) {
			snprintf(frame_txt, sizeof(frame_txt), "%d %llu.%-06llu %d.%-03d\n",
					frame_cnt, pts_in_seconds, pts_in_useconds, exp_in_mseconds, exp_in_useconds);

			write(nFrameFd, frame_txt, strlen(frame_txt));
		}

		if (pContext->stream_count_0%15 == 0) {
			//AGLINK_INFO("*data-len:%p-%d,%p-%d,%p-%d, cnt = %d, kf = %d, pts = %llu us(%llu s), diff = %llu ms,  cur_time = %llu us, time-pts-diff = %llu ms",
				//stream_info->data0, stream_info->size0, stream_info->data1, stream_info->size1, stream_info->data2, stream_info->size2,
				//pContext->stream_count_0, stream_info->keyframe_flag,
				//pts, pts/1000000, (pts - pContext->pre_pts)/1000, cur_time, (cur_time - pts)/1000);
		}

		if (pts_in_seconds != pContext->pre_pts_in_seconds) {
			//AGLINK_INFO("get video stream, fps = %d", pContext->cb_stream_cnt_in_seconds);
			pContext->pre_pts_in_seconds = pts_in_seconds;
			pContext->cb_stream_cnt_in_seconds = 0;
		}
		pContext->cb_stream_cnt_in_seconds++;

		pContext->pre_pts = pts;

		VENC_STREAM_S stVencStream;
		VENC_PACK_S stVencPack;
		memset(&stVencStream, 0, sizeof(stVencStream));
		memset(&stVencPack, 0, sizeof(stVencPack));
		stVencStream.mPackCount = 1;
		stVencStream.mpPack = &stVencPack;

		stVencStream.mpPack[0].mpAddr0 = stream_info->data0;
		stVencStream.mpPack[0].mLen0 = stream_info->size0;
		if (stream_info->size1 > 0) {
			stVencStream.mpPack[0].mpAddr1 = stream_info->data1;
			stVencStream.mpPack[0].mLen1 = stream_info->size1;
		}
		if (stream_info->size2 > 0) {
			stVencStream.mpPack[0].mpAddr2 = stream_info->data2;
			stVencStream.mpPack[0].mLen2 = stream_info->size2;
		}
		stVencStream.mpPack[0].mPTS = pts;
		stVencStream.mpPack[0].mbFrameEnd = TRUE;
		if (RT_VENC_CODEC_H264 == pContext->AWVideoInputChnConfig.encodeType) {
			stVencStream.mpPack[0].mDataType.enH264EType = (stream_info->keyframe_flag) ? H264E_NALU_ISLICE : H264E_NALU_PSLICE;
		} else if (RT_VENC_CODEC_H265 == pContext->AWVideoInputChnConfig.encodeType) {
			stVencStream.mpPack[0].mDataType.enH265EType = (stream_info->keyframe_flag) ? H265E_NALU_ISLICE : H265E_NALU_PSLICE;
		} else if (RT_VENC_CODEC_JPEG == pContext->AWVideoInputChnConfig.encodeType) {
			stVencStream.mpPack[0].mDataType.enJPEGEType = JPEGE_PACK_PIC;
		} else {
			alogw("fatal error! unsupported encodeType:%d", pContext->AWVideoInputChnConfig.encodeType);
		}
		PAYLOAD_TYPE_E ePayloadType = map_RT_VENC_CODEC_TYPE_to_PAYLOAD_TYPE_E(pContext->AWVideoInputChnConfig.encodeType);
		stVencStream.mpPack[0].mDataNum = stream_info->nDataNum;
		for (int i=0; i<stream_info->nDataNum; i++) {
			stVencStream.mpPack[0].mPackInfo[i].mPackType = map_VENC_OUTPUT_PACK_TYPE_to_VENC_DATA_TYPE_U(
				stream_info->stPackInfo[i].uType, ePayloadType);
			stVencStream.mpPack[0].mPackInfo[i].mPackOffset = stream_info->stPackInfo[i].nOffset;
			stVencStream.mpPack[0].mPackInfo[i].mPackLength = stream_info->stPackInfo[i].nLength;
			//AGLINK_INFO("frmcnt[%d]:%d/%d:%d-%d-%d", pContext->stream_count_0, i, stream_info->stream_data->nDataNum,
			//	stVencStream.mpPack[0].mPackInfo[i].mPackType, stVencStream.mpPack[0].mPackInfo[i].mPackOffset,
			//	stVencStream.mpPack[0].mPackInfo[i].mPackLength);
		}
		stVencStream.mSeq = stream_info->id; //pContext->stream_count_0
		ret = AW_MPI_MUX_SendVideoStreamSync(pContext->nMuxChn, &stVencStream, pContext->nMuxVideoStreamId);
		if (ret != SUCCESS) {
			AGLINK_ERROR("fatal error! send video stream to mux fail[0x%x]\n", ret);
		}
	} else {
		AGLINK_ERROR("fatal error! stream data error: data = %p, len = %d, streamCount:%d\n", stream_info->data0, stream_info->size0,
			pContext->stream_count_0);
		return;
	}
	pContext->stream_count_0++;
}

/**
 * When the TWS (True Wireless Stereo) collects audio and transmits it to the "take_video" application,
 * there is a delay in the received audio, resulting in the audio being ahead of the video content.
 * Here, the compensation value is set as an experimental value.
 * */
#define AUDIO_RX_DELAY (100*1000)
static void sendPcmData(uint8_t *data, int len)
{
	int ret;
	DemoAvAIGlassContext *pContext = gpDemoAvAIGlassContext;

	if (pContext == NULL) {
		return;
	}
	AUDIO_FRAME_S frame;

	frame.mpAddr = data;
	frame.mLen = len;

	frame.mTimeStamp = CDX_GetSysTimeUsMonotonic() + AUDIO_RX_DELAY;
	if (frame.mLen > 0) {
		ret = AW_MPI_AENC_SendFrame(pContext->nAEncChnId, &frame, NULL);
	if (ret) {
			AGLINK_ERROR("aenc send Frame error:%d\n", ret);
		}
	}
}

static void* StreamDispatchThread(void *pThreadData)
{
	DemoAvAIGlassContext *pContext = (DemoAvAIGlassContext *)pThreadData;
	int result = 0;
	ERRORTYPE ret;
	message_t stCmdMsg;
	handle_set rdFds;
	prctl(PR_SET_NAME, (unsigned long)"StrmDispatch", 0, 0, 0);

	int nAeChnFd = AW_MPI_AENC_GetHandle(pContext->nAEncChnId);
	int nMaxFd = nAeChnFd;
	int nReadyCnt;

	while(1) {
	PROCESS_MESSAGE:
		if (get_message(&pContext->StreamDispatchCmdQueue, &stCmdMsg) == 0) {
			alogv("StreamDispatch Thread receive command:%d", stCmdMsg.command);
			if (DemoAvAIGlass_Stop == stCmdMsg.command) {
				// Kill thread
				goto EXIT;
			} else {
				AGLINK_ERROR("fatal error! unknown command:%d\n", stCmdMsg.command);
			}
			//precede to process message
			goto PROCESS_MESSAGE;
		}

		AW_MPI_SYS_HANDLE_ZERO(&rdFds);
		AW_MPI_SYS_HANDLE_SET(nAeChnFd, &rdFds);
		nReadyCnt = AW_MPI_SYS_HANDLE_Select(nMaxFd+1, &rdFds, 200);
		if(nReadyCnt > 0) {
			if(AW_MPI_SYS_HANDLE_ISSET(nAeChnFd, &rdFds)) {
				AUDIO_STREAM_S stAudioStream;
				ret = AW_MPI_AENC_GetStream(pContext->nAEncChnId, &stAudioStream, 0);
				if (SUCCESS == ret) {
					ret = AW_MPI_MUX_SendAudioStreamSync(pContext->nMuxChn, &stAudioStream, pContext->nMuxAudioStreamId);
					if(ret != SUCCESS) {
						AGLINK_ERROR("fatal error! send audio stream to mux fail[0x%x]\n", ret);
					}
					ret = AW_MPI_AENC_ReleaseStream(pContext->nAEncChnId, &stAudioStream);
					if(ret != SUCCESS) {
						AGLINK_ERROR("fatal error! why aenc release stream fail[0x%x]?\n", ret);
					}
				} else {
					AGLINK_ERROR("fatal error! aenc get stream fail[0x%x]!\n", ret);
				}
			} else {
				AGLINK_ERROR("fatal error! IsSet fail?\n");
			}
		} else {
			AGLINK_INFO("handle_select timeout! fd cnts:%d\n", nReadyCnt);
		}
	}

EXIT:
	AGLINK_INFO("StreamDispatch thread exit\n");
	return (void*)result;
}

static DemoAvAIGlassContext *createDemoAvAIGlassContext()
{
	int result;
	DemoAvAIGlassContext *pContext = calloc(1, sizeof(DemoAvAIGlassContext));
	if(NULL == pContext) {
		AGLINK_ERROR("fatal error! malloc fail\n");
		return NULL;
	}
	result = pthread_mutex_init(&pContext->MuxChnLock, NULL);
	if (result!=0) {
		AGLINK_ERROR("fatal error! mutex init fail\n");
	}
	result = message_create(&pContext->StreamDispatchCmdQueue);
	if(result != 0) {
		AGLINK_ERROR("fatal error! message create fail!\n");
	}
	cdx_sem_init(&pContext->finish_sem, 0);
	return pContext;
}

static void deleteDemoAvAIGlassContext(DemoAvAIGlassContext *pContext)
{
	int result;
	cdx_sem_deinit(&pContext->finish_sem);
	message_destroy(&pContext->StreamDispatchCmdQueue);
	result = pthread_mutex_destroy(&pContext->MuxChnLock);
	if (result!=0) {
		AGLINK_ERROR("fatal error! mutex destroy fail\n");
	}
	free(pContext);
}

static int configVideoInputConfigByPresetConfig(VideoInputConfig *pAWVideoInputConfig, PresetRtMediaChnConfig *pPresetConfig)
{
	memset(pAWVideoInputConfig, 0, sizeof(VideoInputConfig));
	pAWVideoInputConfig->channelId = pPresetConfig->ch_id;
	pAWVideoInputConfig->fps = pPresetConfig->fps;
	pAWVideoInputConfig->gop = pPresetConfig->gop;
	pAWVideoInputConfig->mRcMode = (0==pPresetConfig->vbr)?AW_CBR:AW_VBR;
	pAWVideoInputConfig->qp_range.nMinqp = pPresetConfig->qp_range.nMinqp;
	pAWVideoInputConfig->qp_range.nMaxqp = pPresetConfig->qp_range.nMaxqp;
	pAWVideoInputConfig->qp_range.nMinPqp = pPresetConfig->qp_range.nMinPqp;
	pAWVideoInputConfig->qp_range.nMaxPqp = pPresetConfig->qp_range.nMaxPqp;
	pAWVideoInputConfig->qp_range.nQpInit = pPresetConfig->qp_range.nQpInit;
	pAWVideoInputConfig->qp_range.bEnMbQpLimit = pPresetConfig->qp_range.bEnMbQpLimit;
	pAWVideoInputConfig->pixelformat = pPresetConfig->pix_fmt;
	pAWVideoInputConfig->enable_sharp = pPresetConfig->enable_sharp;
	pAWVideoInputConfig->bonline_channel = pPresetConfig->bonline_channel;
	pAWVideoInputConfig->share_buf_num = pPresetConfig->share_buf_num;
	pAWVideoInputConfig->vin_buf_num = pPresetConfig->vin_buf_num;
	pAWVideoInputConfig->breduce_refrecmem = pPresetConfig->reduce_refrec_mem;
	pAWVideoInputConfig->venc_video_signal.video_format = DEFAULT;
	pAWVideoInputConfig->venc_video_signal.full_range_flag = 1;
	pAWVideoInputConfig->venc_video_signal.src_colour_primaries = VENC_BT709;
	pAWVideoInputConfig->venc_video_signal.dst_colour_primaries = VENC_BT709;
	pAWVideoInputConfig->product_mode = pPresetConfig->product_mode;
	pAWVideoInputConfig->output_mode = pPresetConfig->out_mode;
	pAWVideoInputConfig->width = pPresetConfig->res_w;
	pAWVideoInputConfig->height = pPresetConfig->res_h;
	pAWVideoInputConfig->dst_width = pPresetConfig->res_w;
	pAWVideoInputConfig->dst_height = pPresetConfig->res_h;
	pAWVideoInputConfig->bitrate	= pPresetConfig->bit_rate*1024;
	pAWVideoInputConfig->encodeType = pPresetConfig->codec_type;
	pAWVideoInputConfig->drop_frame_num = 0;
	pAWVideoInputConfig->enable_wdr = 0;
	pAWVideoInputConfig->profile = VENC_H264ProfileMain;
	pAWVideoInputConfig->level   = VENC_H264Level51;
	pAWVideoInputConfig->enable_isp2ve_linkage = pPresetConfig->isp2ve_en;
	pAWVideoInputConfig->enable_ve2isp_linkage = pPresetConfig->ve2isp_en;
	pAWVideoInputConfig->vbr_param.nMovingTh = pPresetConfig->vbr_param.nMovingTh;
	pAWVideoInputConfig->vbr_param.nQuality = pPresetConfig->vbr_param.nQuality;
	pAWVideoInputConfig->vbr_param.nIFrmBitsCoef = pPresetConfig->vbr_param.nIFrmBitsCoef;
	pAWVideoInputConfig->vbr_param.nPFrmBitsCoef = pPresetConfig->vbr_param.nPFrmBitsCoef;
	pAWVideoInputConfig->vbr_opt_enable = pPresetConfig->vbr_opt_enable;
	pAWVideoInputConfig->enable_tdm_raw = pPresetConfig->enable_tdm_raw;
	pAWVideoInputConfig->tdm_rxbuf_cnt = pPresetConfig->tdm_rxbuf_cnt;
	pAWVideoInputConfig->dma_merge_mode = pPresetConfig->dma_merge_mode;

	return 0;
}

static void video_stream_cb_null(const AWVideoInput_StreamInfo* stream_info)
{
	AGLINK_ERROR("mount UDISK timeout, drop the data before re-recording\n");
	return;
}

int aglink_check_file(DemoAvAIGlassContext *pContext)
{
	int ret = 0;
	uint16_t get_channel_state_cnt = 0;
	int result = 0;

	ret = check_access(TAKE_VIDEO_PATCH, WAIT_VIDEO_PATCH_TIMEOUT_MS);
	if (ret < 0) {
		AGLINK_ERROR("Error: %s can't access\n", TAKE_VIDEO_PATCH);
		//use preset config to config VideoInputConfig.
		AWVideoInput_CallBack(pContext->AWVideoInputChnConfig.channelId, video_stream_cb_null, 0);
		ret = AWVideoInput_Start(pContext->AWVideoInputChnConfig.channelId, 1);
		if (ret != 0) {
			AGLINK_ERROR("fatal error! AWVideoInputChn[%d] start fail:%d\n", pContext->AWVideoInputChnConfig.channelId, result);
			while (AWVideoInput_Get_channel_state(pContext->AWVideoInputChnConfig.channelId) != VIDEO_INPUT_STATE_EXCUTING) {
				usleep(200 * 1000);  //sleep 200ms
				AGLINK_ERROR("AWVideoInput_Get_channel_state waiting VIDEO_INPUT_STATE_EXCUTING, try again [%d/%d]\n",
					++get_channel_state_cnt, GET_CHANNEL_STATE_CNT_MAX);
				if (get_channel_state_cnt == GET_CHANNEL_STATE_CNT_MAX) {
					AGLINK_ERROR("fatal error, waiting VIDEO_INPUT_STATE_EXCUTING timeout!\n");
				}
			}
		}

		AWVideoInput_Destroy(pContext->nChnId);
		aglink_video_app_mount_error();
		ret = check_access(TAKE_VIDEO_PATCH, WAIT_MOUNT_UDISK);
		if (ret < 0) {
			AGLINK_ERROR("Error: %s can't access more than %d ms\n", TAKE_VIDEO_PATCH, WAIT_MOUNT_UDISK);
			return -1;
		} else {
			//use preset config to config VideoInputConfig.
			configVideoInputConfigByPresetConfig(&pContext->AWVideoInputChnConfig, &pContext->presetChnConfig);
			result = AWVideoInput_Configure(pContext->nChnId, &pContext->AWVideoInputChnConfig);
			if (result != 0) {
				AGLINK_ERROR("fatal error! AWVideoInputChn[%d] configure fail:%d\n", pContext->nChnId, result);
				return -1;
			}
			AGLINK_ERROR("recheck %s wirteable %d ms\n", TAKE_VIDEO_PATCH, ret);
		}
	} else {
		AGLINK_ERROR("wait %s wirteable %d ms\n", TAKE_VIDEO_PATCH, ret);
	}

	return 0;
}


int aglink_mpp_init(void)
{
	int result = 0;
	ERRORTYPE ret;
	char str[] = "ai_glass_take_video";
	uint16_t get_channel_state_cnt = 0;
	GLogConfig stGLogConfig =
	{
		.FLAGS_logtostderr = 1,
		.FLAGS_colorlogtostderr = 1,
		.FLAGS_stderrthreshold = _GLOG_INFO,
		.FLAGS_minloglevel = _GLOG_INFO,
		.FLAGS_logbuflevel = -1,
		.FLAGS_logbufsecs = 0,
		.FLAGS_max_log_size = 1,
		.FLAGS_stop_logging_if_full_disk = 1,
	};
	strcpy(stGLogConfig.LogDir, "/tmp/log");
	strcpy(stGLogConfig.InfoLogFileNameBase, "LOG-");
	strcpy(stGLogConfig.LogFileNameExtension, "IPC-");
	log_init(str, &stGLogConfig);
	AGLINK_INFO("aglink_take_video begin\n");

	DemoAvAIGlassContext *pContext = createDemoAvAIGlassContext();

	pContext->nMuxVideoStreamId = 0;
	pContext->nMuxAudioStreamId = 1;
	gpDemoAvAIGlassContext = pContext;

	pContext->stConfig.eAudioEncodeType = PT_AAC;
	pContext->stConfig.nAudioBitrate = 32000;
	pContext->eFileFormat = MEDIA_FILE_FORMAT_MP4;
	pContext->stConfig.nTestDuration = 0;

	pContext->stream_count_0 = 0;

	pContext->stSysConf.nAlignWidth = 32;
	AW_MPI_SYS_SetConf(&pContext->stSysConf);
	AW_MPI_SYS_Init();
	result = AWVideoInput_Init();
	if (result != 0) {
		AGLINK_ERROR("fatal error! AWVideoInput init fail:%d\n", result);
		error_code = AG_FAILED_PROCESS;
		return result;
	}
	pContext->nChnId = 0;
	result = AWVideoInput_GetPresetConfig(pContext->nChnId, &pContext->presetChnConfig);
	if (result != 0) {
		AGLINK_ERROR("fatal error! get AWVideoInputChn[%d] preset config fail:%d\n", pContext->nChnId, result);
		error_code = AG_FAILED_PROCESS;
		goto _exit;
	}
	if (pContext->nChnId != pContext->presetChnConfig.ch_id) {
		AGLINK_ERROR("fatal error! AWVideoInputChn not match:%d!=%d\n", pContext->nChnId, pContext->presetChnConfig.ch_id);
		error_code = AG_FAILED_PROCESS;
		result = -1;
		goto _exit;
	}
	if (0 == pContext->presetChnConfig.b_get_from_dts) {
		AGLINK_ERROR("fatal error! AWVideoInputChn[%d] not enable[%d]?\n", pContext->nChnId, pContext->presetChnConfig.b_get_from_dts);
		error_code = AG_FAILED_PROCESS;
		result = -1;
		goto _exit;
	}
	//use preset config to config VideoInputConfig.
	configVideoInputConfigByPresetConfig(&pContext->AWVideoInputChnConfig, &pContext->presetChnConfig);
	result = AWVideoInput_Configure(pContext->nChnId, &pContext->AWVideoInputChnConfig);
	if (result != 0) {
		AGLINK_ERROR("fatal error! AWVideoInputChn[%d] configure fail:%d\n", pContext->nChnId, result);
		error_code = AG_FAILED_PROCESS;
		goto _exit;
	}
	//prepare audio stream
	MPPCallbackInfo cbInfo;
	cbInfo.cookie = (void*)pContext;
	cbInfo.callback = (MPPCallbackFuncType)&MPPCallbackWrapper;
	pContext->nAEncChnId = 0;
	pContext->stAEncAttr.AeAttr.Type = pContext->stConfig.eAudioEncodeType;
	pContext->stAEncAttr.AeAttr.channels = 1;
	pContext->stAEncAttr.AeAttr.bitsPerSample = PCM_BITWIDTH;
	pContext->stAEncAttr.AeAttr.sampleRate = PCM_PCMSAMPLERATE;
	pContext->stAEncAttr.AeAttr.bitRate = pContext->stConfig.nAudioBitrate;
	pContext->stAEncAttr.AeAttr.attachAACHeader = 0;
	pContext->stAEncAttr.AeAttr.mInBufSize = 0;
	pContext->stAEncAttr.AeAttr.mOutBufCnt = 0;
	ret = AW_MPI_AENC_CreateChn(pContext->nAEncChnId, &pContext->stAEncAttr);
	if (ret != SUCCESS) {
		AGLINK_ERROR("fatal error! create aenc chn fail\n");
	}
	AW_MPI_AENC_RegisterCallback(pContext->nAEncChnId, &cbInfo);
	MPP_CHN_S stDstAEncChn = {MOD_ID_AENC, 0, pContext->nAEncChnId};
	//prepare mux chn
	if ((aglink_check_file(pContext) < 0) || (pContext->finish_sem.semval > 0)) {
		error_code = AG_FAILED_WRITE_FILE;
		goto _exit;
	}
	CreateMuxChn(pContext);

	//start to record
	AWVideoInput_CallBack(pContext->AWVideoInputChnConfig.channelId, video_stream_cb, 0);
	result = AWVideoInput_Start(pContext->AWVideoInputChnConfig.channelId, 1);
	if (result != 0) {
		alogw("fatal error! AWVideoInputChn[%d] start fail:%d", pContext->AWVideoInputChnConfig.channelId, result);
		while (AWVideoInput_Get_channel_state(pContext->AWVideoInputChnConfig.channelId) != VIDEO_INPUT_STATE_EXCUTING) {
			usleep(200 * 1000);  //sleep 200ms
			alogw("AWVideoInput_Get_channel_state waiting VIDEO_INPUT_STATE_EXCUTING, try again [%d/%d]\n",
				++get_channel_state_cnt, GET_CHANNEL_STATE_CNT_MAX);
			if (get_channel_state_cnt == GET_CHANNEL_STATE_CNT_MAX) {
				alogw("fatal error, waiting VIDEO_INPUT_STATE_EXCUTING timeout!\n");
				error_code = AG_FAILED_PROCESS;
				goto _exit;
			}
		}
	}

	pthread_mutex_lock(&pContext->MuxChnLock);
	if(0 == pContext->bMuxChnSetSpsppsFlag) {
		SetSpsppsToMuxer(pContext);
		pContext->bMuxChnSetSpsppsFlag = 1;
		AW_MPI_MUX_StartChn(pContext->nMuxChn);
	}
	pthread_mutex_unlock(&pContext->MuxChnLock);

	AW_MPI_AENC_StartRecvPcm(pContext->nAEncChnId);
	result = pthread_create(&pContext->StreamDispatchThreadId, NULL, StreamDispatchThread, pContext);
	if (result != 0) {
		AGLINK_ERROR("fatal error! pthread create fail!\n");
		error_code = AG_FAILED_PROCESS;
		goto _exit;
	}
	return result;

_exit:
	AWVideoInput_DeInit();
	AW_MPI_SYS_Exit();
	deleteDemoAvAIGlassContext(pContext);
	AGLINK_INFO("ai_glass_take_video finish!\n");
	log_quit();
	return result;
}

int aglink_take_video(void)
{
	DemoAvAIGlassContext *pContext = gpDemoAvAIGlassContext;
	ERRORTYPE ret;
	int result = 0;
	int fd = -1;

	if(gpDemoAvAIGlassContext == NULL) {
        error_code = AG_FAILED_PROCESS;
		return -1;
    }
	if (pContext->stConfig.nTestDuration > 0) {
		cdx_sem_down_timedwait(&pContext->finish_sem, pContext->stConfig.nTestDuration*1000);
	} else {
		cdx_sem_down(&pContext->finish_sem);
	}
	AGLINK_INFO("app will stop and exit now.\n");
	//AWVideoInput_Start(pContext->config_0.channelId, 0);
	result = AWVideoInput_Destroy(pContext->AWVideoInputChnConfig.channelId);
	if (result != 0) {
		AGLINK_ERROR("fatal error! AWVideoInputChn[%d] destroy fail:%d\n", pContext->AWVideoInputChnConfig.channelId, result);
	}

	message_t stMsg;
	InitMessage(&stMsg);
	stMsg.command = DemoAvAIGlass_Stop;
	put_message(&pContext->StreamDispatchCmdQueue, &stMsg);
	int StreamDispatchResult;
	result = pthread_join(pContext->StreamDispatchThreadId, (void**) &StreamDispatchResult);
	if (result != 0) {
		AGLINK_ERROR("fatal error! pthread join fail[%d]\n", result);
	}
	//stop
	ret = AW_MPI_AENC_StopRecvPcm(pContext->nAEncChnId);
	if (ret != SUCCESS) {
		AGLINK_ERROR("fatal error! aenc stop chn fail[0x%x]\n", ret);
	}

	ret = AW_MPI_MUX_StopChn(pContext->nMuxChn, FALSE);
	if (ret != SUCCESS) {
		AGLINK_ERROR("fatal error! muxChn stop fail[0x%x]\n", ret);
	}
	//destroy
	ret = AW_MPI_MUX_DestroyChn(pContext->nMuxChn);
	if (ret != SUCCESS) {
		AGLINK_ERROR("fatal error! muxChn destroy fail[0x%x]\n", ret);
	}
	ret = AW_MPI_AENC_DestroyChn(pContext->nAEncChnId);
	if (ret != SUCCESS) {
		AGLINK_ERROR("fatal error! aenc destroy chn fail[0x%x]\n", ret);
	}

	if (access(pContext->stConfig.strVideoFilePath, F_OK) != -1) {
		fd = open(TAKE_VIDEO_TXT_FILE, O_WRONLY | O_APPEND | O_SYNC | O_CREAT, 0644);
		if (fd == -1) {
			AGLINK_ERROR("Error opening file\n");
		} else {
			if (gyro_en) {
				write(fd, pContext->stConfig.strVideoSubDirName, strlen(pContext->stConfig.strVideoSubDirName));
			} else {
				write(fd, pContext->stConfig.strVideoFile, strlen(pContext->stConfig.strVideoFile));
			}
			write(fd, "\n", strlen("\n"));
			close(fd);
		}
	} else {
		result = -1;
		error_code = AG_FAILED_WRITE_FILE;
	}

	if (nFrameFd >= 0) {
		close(nFrameFd);
		nFrameFd = -1;
	}
	sync();

_exit:
	AWVideoInput_DeInit();
	AW_MPI_SYS_Exit();
	deleteDemoAvAIGlassContext(pContext);
	AGLINK_INFO("ai_glass_take_video finish!\n");
	log_quit();
	return result;

_err0:
	deleteDemoAvAIGlassContext(pContext);
	return result;
}

static DemoAvAIGlassThumb *aglink_take_thumb(uint16_t width, uint16_t height)
{
	int nJpegSize = width * height / 2;
	int nJpegLen = nJpegSize;
	int result = 0;
	int nThumbChn = 4;
	DemoAvAIGlassThumb *AiGlassThumb = NULL;

	PresetRtMediaChnConfig stPresetThumbChnConfig;
	result = AWVideoInput_GetPresetConfig(nThumbChn, &stPresetThumbChnConfig);
	if (result != 0) {
		AGLINK_ERROR("fatal error! get AWVideoInputChn[%d] preset config fail:%d\n", nThumbChn, result);
	}
	if (stPresetThumbChnConfig.b_get_from_dts) {
		if (nThumbChn != stPresetThumbChnConfig.ch_id){
			AGLINK_ERROR("fatal error! AWVideoInputChn not match:%d!=%d\n", nThumbChn, stPresetThumbChnConfig.ch_id);
		}
		if (stPresetThumbChnConfig.out_mode != OUTPUT_MODE_YUV) {
			AGLINK_ERROR("fatal error! In aiglass picture mode, but AWVideoInputChn[%d] preset config output_mode[%d] is wrong!\n",
				nThumbChn, stPresetThumbChnConfig.out_mode);
		}
		AGLINK_INFO("take thumb: AWVideoInputChn[%d] output_mode[%d], jpg[%d-%dx%d-%d]\n", nThumbChn,
			stPresetThumbChnConfig.out_mode, stPresetThumbChnConfig.catchjpg_en, stPresetThumbChnConfig.jpg_w,
			stPresetThumbChnConfig.jpg_h, stPresetThumbChnConfig.jpg_qp);
        //use preset config to config VideoInputConfig.
		VideoInputConfig stThumbChnConfig;
		configVideoInputConfigByPresetConfig(&stThumbChnConfig, &stPresetThumbChnConfig);
		result = AWVideoInput_Configure(nThumbChn, &stThumbChnConfig);
		if (result != 0) {
			AGLINK_ERROR("fatal error! AWVideoInputChn[%d] configure fail:%d\n", nThumbChn, result);
		}
		result = AWVideoInput_Start(nThumbChn, 1);
	}

	AiGlassThumb = (DemoAvAIGlassThumb*)malloc(sizeof(DemoAvAIGlassThumb));
	if (AiGlassThumb == NULL) {
		AGLINK_ERROR("fatal error! AiGlassThumb malloc %d bytes fail\n", sizeof(DemoAvAIGlassThumb));
		return NULL;
	}
	AiGlassThumb->pJpegBuf = (char *)malloc(nJpegSize);
	if (AiGlassThumb->pJpegBuf == NULL) {
		AGLINK_ERROR("fatal error!pJpegBuf malloc %d bytes fail\n", nJpegSize);
		free(AiGlassThumb);
		return NULL;
	}
	catch_jpeg_config stCatchJpegConfig;
	memset(&stCatchJpegConfig, 0, sizeof(stCatchJpegConfig));
	stCatchJpegConfig.channel_id = nThumbChn;
	stCatchJpegConfig.width = width;
	stCatchJpegConfig.height = height;
	stCatchJpegConfig.qp = 80;
	stCatchJpegConfig.rotate_angle = 0;
	result = AWVideoInput_CatchJpegConfig(&stCatchJpegConfig);
	if (result != 0) {
		AGLINK_ERROR("fatal error! AWChannel[%d] catch jpeg config fail[%d]\n", stCatchJpegConfig.channel_id, result);
		free(AiGlassThumb->pJpegBuf);
		free(AiGlassThumb);
		return NULL;
	}
	result = AWVideoInput_CatchJpeg(AiGlassThumb->pJpegBuf, &nJpegLen, stCatchJpegConfig.channel_id);
	if (0 == result) {
		AGLINK_INFO("AWChannel[0]: JpegLen[%d]\n", nJpegLen);
		AiGlassThumb->nJpegLen = nJpegLen;
#ifdef SAVE_TAKE_VIDEO_THUMB
		char time_buf[75];
		int fd = -1;
		char strJpegPath[128] = {0};
		char *filename = NULL;
		get_timestamp_string(time_buf, sizeof(time_buf));
		snprintf(strJpegPath, sizeof(strJpegPath) - 1, "%s%s.jpg", TAKE_VIDEO_PATCH, time_buf);
		FILE* pJpegFile = fopen(strJpegPath, "wb");
		int nJpegFd = open(strJpegPath, O_RDWR | O_CREAT| O_SYNC | O_TRUNC, 0666);
		if (pJpegFile != NULL) {
			if (write(nJpegFd, AiGlassThumb->pJpegBuf, AiGlassThumb->nJpegLen) <=0)
				perror("video save thumb failed");
			close(nJpegFd);
			AGLINK_INFO("store to file:%s\n", strJpegPath);
			filename = strrchr(strJpegPath, '/');
			filename++;
			fd = open(TAKE_VIDEO_TXT_FILE, O_WRONLY | O_APPEND | O_SYNC | O_CREAT, 0644);
			write(fd, filename, strlen(filename));
			write(fd, "\n", strlen("\n"));
			close(fd);
		}
#endif
	} else {
		AGLINK_ERROR("fatal error! why AWChannel[0] catch jpeg fail[%d]?\n", result);
		free(AiGlassThumb->pJpegBuf);
		free(AiGlassThumb);
		return NULL;
	}
	result = AWVideoInput_Destroy(nThumbChn);
    if (result != 0) {
        AGLINK_ERROR("fatal error! AWVideoInputChn[%d] destroy fail:%d\n", nThumbChn, result);
    }
	return AiGlassThumb;
}
