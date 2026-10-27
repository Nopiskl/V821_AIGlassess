#define _GNU_SOURCE
#include "vm_media.h"
#include "../aiglass-apps/apps/src/misc/media_owner.h"
#include <errno.h>
#include <linux/videodev2.h> /* VI attribute constants only; no V4L2 device IO. */
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <mpi_sys.h>
#include <mpi_vi.h>
#include <mpi_isp.h>
#include <mpi_venc.h>
#include <mpi_ai.h>
#include <mpi_aenc.h>
#include <mpi_mux.h>

/* V821 sun300iw1 reference: nopiskl_smartIPC_demo (JPEG/AI) and
 * nopiskl_CDR_demo (VI/VENC/AI/AENC/MUX). Do not substitute a V851 ABI. */
struct vm_media {
    vm_media_config_t config;
    vm_media_mode_t mode;
    int owner_fd, file_fd;
    char path[512], partial[520];
    bool system, vipp, isp, vipp_enabled, vi, vi_enabled;
    bool venc, venc_started, ai, ai_enabled, aenc, aenc_started, mux, mux_started;
    bool vi_venc, venc_mux, ai_aenc, aenc_mux, audio_thread_started;
    pthread_t audio_thread;
    atomic_bool stop;
    atomic_int error;
    atomic_uint_fast64_t audio_bytes;
};

static uint64_t monotonic_ms(void)
{
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (uint64_t)now.tv_sec * 1000 + (uint64_t)now.tv_nsec / 1000000;
}

static int mpp_result(const char *operation, ERRORTYPE result)
{
    if (result == SUCCESS) return 0;
    fprintf(stderr, "V821 MPP %s: 0x%x\n", operation, (unsigned)result);
    return -EIO;
}
#define MPI(call) mpp_result(#call, (call))

static void set_error(vm_media_t *m, int error)
{
    int previous = 0;
    atomic_compare_exchange_strong(&m->error, &previous, error);
    atomic_store(&m->stop, true);
}

static ERRORTYPE media_callback(void *cookie, MPP_CHN_S *channel,
                               MPP_EVENT_TYPE event, void *data)
{
    vm_media_t *m = cookie;
    (void)data;
    if (!m || !channel) return SUCCESS;
    if (channel->mModId == MOD_ID_MUX) {
        if (event == MPP_EVENT_WRITE_DISK_ERROR) set_error(m, -EIO);
        else if (event == MPP_EVENT_NEED_NEXT_FD) set_error(m, -EFBIG);
    } else if (channel->mModId == MOD_ID_VENC && event == MPP_EVENT_VENC_TIMEOUT) {
        set_error(m, -ETIMEDOUT);
    }
    return SUCCESS;
}

void vm_media_config_init(vm_media_config_t *c)
{
    if (!c) return;
    *c = (vm_media_config_t){.width = 1920, .height = 1080, .fps = 30,
        .video_bitrate = 8000000, .photo_quality = 85, .sample_rate = 16000,
        .audio_channels = 1, .video_audio = true, .manage_system = true};
}

vm_media_t *vm_media_open(const vm_media_config_t *config)
{
    vm_media_t *m = calloc(1, sizeof(*m));
    if (!m) return NULL;
    m->owner_fd = m->file_fd = -1;
    if (config) m->config = *config; else vm_media_config_init(&m->config);
    const vm_media_config_t *c = &m->config;
    if (c->vipp < 0 || c->vi_channel < 0 || c->isp < 0 || c->venc_channel < 0 ||
        c->mux_channel < 0 || c->ai_device < 0 || c->ai_channel < 0 || c->aenc_channel < 0 ||
        c->width < 32 || c->height < 32 || c->width > 4096 || c->height > 4096 ||
        (c->width % 16) || (c->height % 2) || !c->fps || c->fps > 120 ||
        c->video_bitrate < 64000 || c->video_bitrate > 100000000 ||
        !c->photo_quality || c->photo_quality > 100 ||
        (c->sample_rate != 8000 && c->sample_rate != 16000 && c->sample_rate != 32000 &&
         c->sample_rate != 44100 && c->sample_rate != 48000) ||
        (c->audio_channels != 1 && c->audio_channels != 2)) {
        free(m); errno = EINVAL; return NULL;
    }
    atomic_init(&m->stop, false);
    atomic_init(&m->error, 0);
    atomic_init(&m->audio_bytes, 0);
    m->owner_fd = v821_media_owner_acquire();
    if (m->owner_fd < 0) { int saved = errno; free(m); errno = saved; return NULL; }
    if (c->manage_system) {
        MPP_SYS_CONF_S sys = {0};
        sys.nAlignWidth = 32;
        if (MPI(AW_MPI_SYS_SetConf(&sys)) || MPI(AW_MPI_SYS_Init())) {
            close(m->owner_fd); free(m); errno = EIO; return NULL;
        }
        m->system = true;
    }
    return m;
}

static bool resources_live(const vm_media_t *m)
{
    return m->vipp || m->isp || m->vi || m->venc || m->ai || m->aenc || m->mux;
}

static int begin_file(vm_media_t *m, const char *path)
{
    if (!path || !*path || strlen(path) >= sizeof(m->path)) return -EINVAL;
    if (m->mode != VM_MEDIA_IDLE || resources_live(m) || m->file_fd >= 0) return -EBUSY;
    struct stat st;
    if (lstat(path, &st) == 0) return -EEXIST;
    if (errno != ENOENT) return -errno;
    snprintf(m->path, sizeof(m->path), "%s", path);
    snprintf(m->partial, sizeof(m->partial), "%s.part", path);
    m->file_fd = open(m->partial, O_RDWR | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600);
    if (m->file_fd < 0) { m->partial[0] = '\0'; return -errno; }
    atomic_store(&m->stop, false);
    atomic_store(&m->error, 0);
    atomic_store(&m->audio_bytes, 0);
    return 0;
}

static int write_all(int fd, const void *data, size_t length)
{
    const unsigned char *p = data;
    while (length) {
        ssize_t n = write(fd, p, length);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) return n < 0 ? -errno : -EIO;
        p += n; length -= (size_t)n;
    }
    return 0;
}

static int finish_file(vm_media_t *m, bool commit, bool preserve)
{
    int rc = 0;
    if (m->file_fd >= 0) {
        if (fsync(m->file_fd) < 0) rc = -errno;
        if (close(m->file_fd) < 0 && !rc) rc = -errno;
        m->file_fd = -1;
    }
    if (m->partial[0]) {
        if (commit && !rc) {
            /* Hard link within one directory publishes without replacing a file. */
            if (link(m->partial, m->path) < 0) rc = -errno;
            else if (unlink(m->partial) < 0) rc = -errno;
        } else if (!preserve) {
            if (unlink(m->partial) < 0 && errno != ENOENT && !rc) rc = -errno;
        }
        m->partial[0] = '\0';
    }
    return rc;
}

static int camera_prepare(vm_media_t *m, bool jpeg)
{
    const vm_media_config_t *c = &m->config;
    VI_ATTR_S vi = {0};
    if (MPI(AW_MPI_VI_CreateVipp(c->vipp))) return -EIO;
    m->vipp = true;
    vi.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    vi.memtype = V4L2_MEMORY_MMAP;
    vi.format.pixelformat = V4L2_PIX_FMT_NV21;
    vi.format.field = V4L2_FIELD_NONE;
    vi.format.colorspace = jpeg ? V4L2_COLORSPACE_JPEG : V4L2_COLORSPACE_REC709;
    vi.format.width = c->width; vi.format.height = c->height;
    vi.nbufs = 4; vi.nplanes = 2; vi.fps = c->fps;
    vi.capturemode = V4L2_MODE_VIDEO;
    /* Offline mode: local VI owns YUV buffers; no RT Media/E907 fastboot owner. */
    vi.mOnlineEnable = 0;
    if (MPI(AW_MPI_VI_SetVippAttr(c->vipp, &vi))) return -EIO;
    if (MPI(AW_MPI_ISP_Run(c->isp))) return -EIO;
    m->isp = true;
    if (MPI(AW_MPI_VI_EnableVipp(c->vipp))) return -EIO;
    m->vipp_enabled = true;
    if (MPI(AW_MPI_VI_CreateVirChn(c->vipp, c->vi_channel, NULL))) return -EIO;
    m->vi = true;
    return 0;
}

static int encoder_prepare(vm_media_t *m, bool jpeg)
{
    const vm_media_config_t *c = &m->config;
    VENC_CHN_ATTR_S attr = {0};
    attr.VeAttr.Type = jpeg ? PT_JPEG : PT_H264;
    attr.VeAttr.mVippID = c->vipp;
    attr.VeAttr.SrcPicWidth = c->width; attr.VeAttr.SrcPicHeight = c->height;
    attr.VeAttr.Field = VIDEO_FIELD_FRAME;
    attr.VeAttr.PixelFormat = MM_PIXEL_FORMAT_YVU_SEMIPLANAR_420;
    attr.VeAttr.mColorSpace = jpeg ? V4L2_COLORSPACE_JPEG : V4L2_COLORSPACE_REC709;
    attr.VeAttr.OutputFormat = VENC_OUTPUT_SAME_AS_INPUT;
    attr.VeAttr.MaxKeyInterval = jpeg ? 1 : c->fps;
    if (jpeg) {
        attr.VeAttr.AttrJpeg.BufSize = c->width * c->height * 2;
        attr.VeAttr.AttrJpeg.mThreshSize = c->width * c->height;
        attr.VeAttr.AttrJpeg.bByFrame = TRUE;
        attr.VeAttr.AttrJpeg.PicWidth = c->width;
        attr.VeAttr.AttrJpeg.PicHeight = c->height;
    } else {
        attr.VeAttr.AttrH264e.BufSize = c->video_bitrate / 4;
        attr.VeAttr.AttrH264e.mThreshSize = c->width * c->height / 2;
        if (attr.VeAttr.AttrH264e.BufSize < attr.VeAttr.AttrH264e.mThreshSize * 2)
            attr.VeAttr.AttrH264e.BufSize = attr.VeAttr.AttrH264e.mThreshSize * 2;
        attr.VeAttr.AttrH264e.bByFrame = TRUE;
        attr.VeAttr.AttrH264e.Profile = 1;
        attr.VeAttr.AttrH264e.mLevel = H264_LEVEL_51;
        attr.VeAttr.AttrH264e.PicWidth = c->width;
        attr.VeAttr.AttrH264e.PicHeight = c->height;
        attr.VeAttr.AttrH264e.mbPIntraEnable = TRUE;
        attr.RcAttr.mRcMode = VENC_RC_MODE_H264CBR;
        attr.RcAttr.mAttrH264Cbr.mBitRate = c->video_bitrate;
        attr.RcAttr.mAttrH264Cbr.mSrcFrmRate = c->fps;
        attr.RcAttr.mAttrH264Cbr.mDstFrmRate = c->fps;
        attr.GopAttr.enGopMode = VENC_GOPMODE_NORMALP;
        attr.GopAttr.mGopSize = c->fps;
        attr.EncppAttr.eEncppSharpSetting = VencEncppSharp_FollowISPConfig;
    }
    if (MPI(AW_MPI_VENC_CreateChn(c->venc_channel, &attr))) return -EIO;
    m->venc = true;
    MPPCallbackInfo cb = {.cookie = m, .callback = media_callback};
    if (MPI(AW_MPI_VENC_RegisterCallback(c->venc_channel, &cb))) return -EIO;
    if (jpeg) {
        VENC_PARAM_JPEG_S quality = {0};
        quality.Qfactor = c->photo_quality;
        if (MPI(AW_MPI_VENC_SetJpegParam(c->venc_channel, &quality)) ||
            MPI(AW_MPI_VENC_ForbidDiscardingFrame(c->venc_channel, TRUE))) return -EIO;
    } else {
        VENC_RC_PARAM_S rc = {0};
        rc.ParamH264Cbr.mMaxQp = rc.ParamH264Cbr.mMaxPqp = 50;
        rc.ParamH264Cbr.mMinQp = rc.ParamH264Cbr.mMinPqp = 10;
        rc.ParamH264Cbr.mQpInit = 38;
        rc.ParamH264Cbr.mbEnMbQpLimit = 1;
        VENC_FRAME_RATE_S rate = {.SrcFrmRate = c->fps, .DstFrmRate = c->fps};
        if (MPI(AW_MPI_VENC_SetRcParam(c->venc_channel, &rc)) ||
            MPI(AW_MPI_VENC_SetFrameRate(c->venc_channel, &rate))) return -EIO;
    }
    return 0;
}

static int audio_prepare(vm_media_t *m, bool encoded)
{
    const vm_media_config_t *c = &m->config;
    AIO_ATTR_S attr = {0};
    attr.enSamplerate = (AUDIO_SAMPLE_RATE_E)c->sample_rate;
    attr.enBitwidth = AUDIO_BIT_WIDTH_16;
    attr.enSoundmode = c->audio_channels == 1 ? AUDIO_SOUND_MODE_MONO : AUDIO_SOUND_MODE_STEREO;
    attr.mMicNum = 1; attr.mChnCnt = c->audio_channels;
    AI_CHN_ATTR_S channel = {0};
    channel.nFrameSize = 1024;
    if (MPI(AW_MPI_AI_SetPubAttr(c->ai_device, &attr)) ||
        MPI(AW_MPI_AI_CreateChn(c->ai_device, c->ai_channel, &channel))) return -EIO;
    m->ai = true;
    if (encoded) {
        AENC_CHN_ATTR_S enc = {0};
        enc.AeAttr.Type = PT_AAC;
        enc.AeAttr.channels = c->audio_channels;
        enc.AeAttr.bitsPerSample = 16;
        enc.AeAttr.sampleRate = c->sample_rate;
        enc.AeAttr.bitRate = 64000;
        if (MPI(AW_MPI_AENC_CreateChn(c->aenc_channel, &enc))) return -EIO;
        m->aenc = true;
    }
    return 0;
}

static int release_capture(vm_media_t *m)
{
    const vm_media_config_t *c = &m->config;
    int rc = 0;
    atomic_store(&m->stop, true);
    if (m->audio_thread_started) {
        int joined = pthread_join(m->audio_thread, NULL);
        if (joined) return -joined; /* Never destroy a channel used by the worker. */
        m->audio_thread_started = false;
    }
#define RELEASE(flag, call) do { if (m->flag) { \
    int result = MPI(call); if (result) { if (!rc) rc = result; } else m->flag = false; \
} } while (0)
    RELEASE(vi_enabled, AW_MPI_VI_DisableVirChn(c->vipp, c->vi_channel));
    RELEASE(ai_enabled, AW_MPI_AI_DisableChn(c->ai_device, c->ai_channel));
    RELEASE(venc_started, AW_MPI_VENC_StopRecvPic(c->venc_channel));
    RELEASE(aenc_started, AW_MPI_AENC_StopRecvPcm(c->aenc_channel));
    RELEASE(mux_started, AW_MPI_MUX_StopChn(c->mux_channel, FALSE));
    /* If stopping a producer/consumer fails, retain its bindings and cookie. */
    if (rc) return rc;
    MPP_CHN_S vi = {MOD_ID_VIU, c->vipp, c->vi_channel};
    MPP_CHN_S ve = {MOD_ID_VENC, 0, c->venc_channel};
    MPP_CHN_S ai = {MOD_ID_AI, c->ai_device, c->ai_channel};
    MPP_CHN_S ae = {MOD_ID_AENC, 0, c->aenc_channel};
    MPP_CHN_S mux = {MOD_ID_MUX, 0, c->mux_channel};
    RELEASE(aenc_mux, AW_MPI_SYS_UnBind(&ae, &mux));
    RELEASE(venc_mux, AW_MPI_SYS_UnBind(&ve, &mux));
    RELEASE(ai_aenc, AW_MPI_SYS_UnBind(&ai, &ae));
    RELEASE(vi_venc, AW_MPI_SYS_UnBind(&vi, &ve));
    if (rc) return rc;
    RELEASE(mux, AW_MPI_MUX_DestroyChn(c->mux_channel));
    RELEASE(aenc, AW_MPI_AENC_DestroyChn(c->aenc_channel));
    RELEASE(venc, AW_MPI_VENC_DestroyChn(c->venc_channel));
    RELEASE(ai, AW_MPI_AI_DestroyChn(c->ai_device, c->ai_channel));
    RELEASE(vi, AW_MPI_VI_DestroyVirChn(c->vipp, c->vi_channel));
    if (rc) return rc;
    RELEASE(vipp_enabled, AW_MPI_VI_DisableVipp(c->vipp));
    RELEASE(isp, AW_MPI_ISP_Stop(c->isp));
    if (rc) return rc;
    RELEASE(vipp, AW_MPI_VI_DestroyVipp(c->vipp));
#undef RELEASE
    return rc;
}

static void put16(unsigned char *p, unsigned value)
{
    p[0] = value & 255; p[1] = (value >> 8) & 255;
}
static void put32(unsigned char *p, uint32_t value)
{
    put16(p, value & 65535); put16(p + 2, value >> 16);
}
static int wav_header(vm_media_t *m)
{
    uint64_t bytes = atomic_load(&m->audio_bytes);
    if (bytes > UINT32_MAX - 36) return -EFBIG;
    unsigned char header[44] = {0};
    memcpy(header, "RIFF", 4); put32(header + 4, (uint32_t)bytes + 36);
    memcpy(header + 8, "WAVEfmt ", 8); put32(header + 16, 16);
    put16(header + 20, 1); put16(header + 22, m->config.audio_channels);
    put32(header + 24, m->config.sample_rate);
    put32(header + 28, m->config.sample_rate * m->config.audio_channels * 2);
    put16(header + 32, m->config.audio_channels * 2); put16(header + 34, 16);
    memcpy(header + 36, "data", 4); put32(header + 40, (uint32_t)bytes);
    if (lseek(m->file_fd, 0, SEEK_SET) < 0) return -errno;
    return write_all(m->file_fd, header, sizeof(header));
}

static void *capture_audio(void *opaque)
{
    vm_media_t *m = opaque;
    uint64_t last_frame = monotonic_ms();
    while (!atomic_load(&m->stop)) {
        AUDIO_FRAME_S frame = {0};
        ERRORTYPE got = AW_MPI_AI_GetFrame(m->config.ai_device, m->config.ai_channel, &frame, NULL, 100);
        if (got != SUCCESS) {
            if (monotonic_ms() - last_frame > 5000) { set_error(m, -ETIMEDOUT); break; }
            struct timespec delay = {0, 10000000};
            nanosleep(&delay, NULL);
            continue;
        }
        last_frame = monotonic_ms();
        int rc = 0;
        if (!frame.mpAddr || frame.mLen <= 0 || frame.mLen % (m->config.audio_channels * 2)) rc = -EIO;
        else if (atomic_load(&m->audio_bytes) + (uint64_t)frame.mLen > UINT32_MAX - 36) rc = -EFBIG;
        else {
            rc = write_all(m->file_fd, frame.mpAddr, frame.mLen);
            if (!rc) atomic_fetch_add(&m->audio_bytes, frame.mLen);
        }
        int released = MPI(AW_MPI_AI_ReleaseFrame(m->config.ai_device, m->config.ai_channel, &frame, NULL));
        if (!rc) rc = released;
        if (rc) { set_error(m, rc); break; }
    }
    return NULL;
}

int vm_media_photo(vm_media_t *m, const char *path)
{
    if (!m) return -EINVAL;
    int rc = begin_file(m, path);
    if (rc) return rc;
    const vm_media_config_t *c = &m->config;
    rc = camera_prepare(m, true);
    if (!rc) rc = encoder_prepare(m, true);
    if (!rc) {
        rc = MPI(AW_MPI_VENC_StartRecvPic(c->venc_channel));
        if (!rc) m->venc_started = true;
    }
    if (!rc) {
        rc = MPI(AW_MPI_VI_EnableVirChn(c->vipp, c->vi_channel));
        if (!rc) m->vi_enabled = true;
    }
    if (!rc) {
        VIDEO_FRAME_INFO_S frame = {0};
        rc = MPI(AW_MPI_VI_GetFrame(c->vipp, c->vi_channel, &frame, 2000));
        if (!rc) {
            /* V821 sample uses the synchronous variant so VI owns the buffer
             * until the encoder has consumed it. */
            rc = MPI(AW_MPI_VENC_SendFrameSync(c->venc_channel, &frame, 10000));
            int released = MPI(AW_MPI_VI_ReleaseFrame(c->vipp, c->vi_channel, &frame));
            if (!rc) rc = released;
        }
    }
    if (!rc) {
        VENC_PACK_S pack = {0};
        VENC_STREAM_S stream = {.mpPack = &pack, .mPackCount = 1};
        rc = MPI(AW_MPI_VENC_GetStream(c->venc_channel, &stream, 2000));
        if (!rc) {
            if (!pack.mLen0 && !pack.mLen1 && !pack.mLen2) rc = -ENODATA;
            if (!rc && pack.mLen0) rc = pack.mpAddr0 ? write_all(m->file_fd, pack.mpAddr0, pack.mLen0) : -EIO;
            if (!rc && pack.mLen1) rc = pack.mpAddr1 ? write_all(m->file_fd, pack.mpAddr1, pack.mLen1) : -EIO;
            if (!rc && pack.mLen2) rc = pack.mpAddr2 ? write_all(m->file_fd, pack.mpAddr2, pack.mLen2) : -EIO;
            int released = MPI(AW_MPI_VENC_ReleaseStream(c->venc_channel, &stream));
            if (!rc) rc = released;
        }
    }
    int released = release_capture(m);
    if (!rc) rc = released;
    if (!rc) rc = atomic_load(&m->error);
    int saved = finish_file(m, !rc, false);
    return rc ? rc : saved;
}

static int video_prepare(vm_media_t *m)
{
    const vm_media_config_t *c = &m->config;
    int rc = camera_prepare(m, false);
    if (!rc) rc = encoder_prepare(m, false);
    if (!rc && c->video_audio) rc = audio_prepare(m, true);
    if (rc) return rc;
    MUX_CHN_ATTR_S attr = {0};
    attr.mVideoAttrValidNum = 1;
    attr.mVideoAttr[0].mWidth = c->width; attr.mVideoAttr[0].mHeight = c->height;
    attr.mVideoAttr[0].mVideoFrmRate = c->fps * 1000;
    attr.mVideoAttr[0].mVideoEncodeType = PT_H264;
    attr.mVideoAttr[0].mVeChn = c->venc_channel;
    attr.mAudioEncodeType = c->video_audio ? PT_AAC : PT_MAX;
    attr.mTextEncodeType = PT_MAX;
    if (c->video_audio) {
        attr.mChannels = c->audio_channels; attr.mBitsPerSample = 16;
        attr.mSampleRate = c->sample_rate; attr.mSamplesPerFrame = 1024;
    }
    attr.mMediaFileFormat = MEDIA_FILE_FORMAT_MP4;
    attr.mFsWriteMode = FSWRITEMODE_SIMPLECACHE;
    attr.mSimpleCacheSize = 256 * 1024;
    if (MPI(AW_MPI_MUX_CreateChn(c->mux_channel, &attr, m->file_fd, 0))) return -EIO;
    m->mux = true;
    MPPCallbackInfo cb = {.cookie = m, .callback = media_callback};
    if (MPI(AW_MPI_MUX_RegisterCallback(c->mux_channel, &cb))) return -EIO;
    MPP_CHN_S vi = {MOD_ID_VIU, c->vipp, c->vi_channel};
    MPP_CHN_S ve = {MOD_ID_VENC, 0, c->venc_channel};
    MPP_CHN_S ai = {MOD_ID_AI, c->ai_device, c->ai_channel};
    MPP_CHN_S ae = {MOD_ID_AENC, 0, c->aenc_channel};
    MPP_CHN_S mux = {MOD_ID_MUX, 0, c->mux_channel};
    if (MPI(AW_MPI_SYS_Bind(&vi, &ve))) return -EIO;
    m->vi_venc = true;
    if (MPI(AW_MPI_SYS_Bind(&ve, &mux))) return -EIO;
    m->venc_mux = true;
    if (c->video_audio) {
        if (MPI(AW_MPI_SYS_Bind(&ai, &ae))) return -EIO;
        m->ai_aenc = true;
        if (MPI(AW_MPI_SYS_Bind(&ae, &mux))) return -EIO;
        m->aenc_mux = true;
    }
    VencHeaderData header = {0};
    if (MPI(AW_MPI_VENC_GetH264SpsPpsInfo(c->venc_channel, &header)) ||
        !header.pBuffer || !header.nLength ||
        MPI(AW_MPI_MUX_SetH264SpsPpsInfo(c->mux_channel, c->venc_channel, &header))) return -EIO;
    if (MPI(AW_MPI_MUX_StartChn(c->mux_channel))) return -EIO;
    m->mux_started = true;
    if (MPI(AW_MPI_VENC_StartRecvPic(c->venc_channel))) return -EIO;
    m->venc_started = true;
    if (c->video_audio) {
        if (MPI(AW_MPI_AENC_StartRecvPcm(c->aenc_channel))) return -EIO;
        m->aenc_started = true;
        if (MPI(AW_MPI_AI_EnableChn(c->ai_device, c->ai_channel))) return -EIO;
        m->ai_enabled = true;
    }
    if (MPI(AW_MPI_VI_EnableVirChn(c->vipp, c->vi_channel))) return -EIO;
    m->vi_enabled = true;
    return 0;
}

int vm_media_record_start(vm_media_t *m, vm_media_mode_t mode, const char *path)
{
    if (!m || (mode != VM_MEDIA_AUDIO && mode != VM_MEDIA_VIDEO)) return -EINVAL;
    int rc = begin_file(m, path);
    if (rc) return rc;
    if (mode == VM_MEDIA_VIDEO) rc = video_prepare(m);
    else {
        rc = wav_header(m);
        if (!rc) rc = audio_prepare(m, false);
        if (!rc) {
            rc = MPI(AW_MPI_AI_EnableChn(m->config.ai_device, m->config.ai_channel));
            if (!rc) m->ai_enabled = true;
        }
        if (!rc) {
            int started = pthread_create(&m->audio_thread, NULL, capture_audio, m);
            if (started) rc = -started;
            else m->audio_thread_started = true;
        }
    }
    if (!rc) { m->mode = mode; return 0; }
    int released = release_capture(m);
    /* A failed MUX stop may still use its fd. Preserve it with the live context. */
    if (!released) (void)finish_file(m, false, false);
    set_error(m, rc);
    return rc;
}

int vm_media_record_stop(vm_media_t *m)
{
    if (!m) return -EINVAL;
    if (m->mode == VM_MEDIA_IDLE && !resources_live(m)) return -ENOENT;
    vm_media_mode_t mode = m->mode;
    int rc = release_capture(m);
    if (rc) { set_error(m, rc); return rc; }
    rc = atomic_load(&m->error);
    if (mode == VM_MEDIA_AUDIO) {
        int header = wav_header(m);
        if (!rc) rc = header;
        if (!rc && !atomic_load(&m->audio_bytes)) rc = -ENODATA;
    }
    struct stat st;
    if (!rc && (fstat(m->file_fd, &st) < 0 || st.st_size <= 0)) rc = -EIO;
    int saved = finish_file(m, !rc, true);
    if (!rc) rc = saved;
    m->mode = VM_MEDIA_IDLE;
    return rc;
}

void vm_media_status(vm_media_t *m, vm_media_status_t *status)
{
    if (!status) return;
    *status = (vm_media_status_t){0};
    if (!m) return;
    status->mode = m->mode;
    status->stop_requested = atomic_load(&m->stop);
    status->error = atomic_load(&m->error);
    status->audio_bytes = atomic_load(&m->audio_bytes);
}

int vm_media_close(vm_media_t *m)
{
    if (!m) return 0;
    int rc = 0;
    if (m->mode != VM_MEDIA_IDLE || resources_live(m)) rc = vm_media_record_stop(m);
    if (resources_live(m)) return rc ? rc : -EBUSY;
    if (m->file_fd >= 0) { int saved = finish_file(m, false, true); if (!rc) rc = saved; }
    if (m->system && MPI(AW_MPI_SYS_Exit())) return -EIO;
    if (rc) fprintf(stderr, "V821 media finalized with error: %s\n", strerror(-rc));
    close(m->owner_fd);
    free(m);
    return 0;
}
