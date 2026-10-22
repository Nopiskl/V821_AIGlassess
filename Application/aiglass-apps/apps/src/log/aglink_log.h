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
#ifndef __AGLINK_LOG_H__
#define __AGLINK_LOG_H__

#if __cplusplus
extern "C" {
#endif

#include <log_core.h>

#define AGLINK_LOG_TAG "AG"

enum aglink_log_level {
    AGLINK_MSG_NONE = 0,
    AGLINK_MSG_ERROR,
    AGLINK_MSG_WARNING,
    AGLINK_MSG_INFO,
    AGLINK_MSG_DEBUG,
    AGLINK_MSG_MSGDUMP,
    AGLINK_MSG_EXCESSIVE
};

#define AGLINK_DEBUG(fmt, arg...)                                                                    \
    aglink_print(AGLINK_MSG_DEBUG, "[%s:%u]:" fmt, __func__, __LINE__, ##arg)

#define AGLINK_INFO(fmt, arg...)                                                                     \
    aglink_print(AGLINK_MSG_INFO, "[%s:%u]:" fmt, __func__, __LINE__, ##arg)

#define AGLINK_WARNG(fmt, arg...)                                                                    \
    aglink_print(AGLINK_MSG_WARNING, "[%s:%u]:" fmt, __func__, __LINE__, ##arg)

#define AGLINK_ERROR(fmt, arg...)                                                                    \
    aglink_print(AGLINK_MSG_ERROR, "[%s:%u]:" fmt, __func__, __LINE__, ##arg)

#define AGLINK_DUMP(fmt, arg...)                                                                     \
    aglink_print(AGLINK_MSG_MSGDUMP, "[%s:%u]:" fmt, __func__, __LINE__, ##arg)

#define AGLINK_EXCESSIVE(fmt, arg...)                                                                \
    aglink_print(AGLINK_MSG_EXCESSIVE, "[%s:%u]" fmt, __func__, __LINE__, ##arg)

#define aglink_print(level, fmt, arg...) log_print(AGLINK_LOG_TAG, level, fmt, ##arg)

int aglink_set_debug_level(int level);

int aglink_set_debug_file_path(char *file_path);

int aglink_stop_debug_file_log(void);

int aglink_stop_log(void);
#if __cplusplus
}; // extern "C"
#endif

#endif
