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
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>
#include <unistd.h>
#include <stdint.h>
#include <aglink/ag_proto.h>
#include "sys_info.h"
#include <aglink_log.h>

static int g_interrupted = 0;

static void signal_handler(int signal)
{
	//AGLINK_INFO("signal_handler get %d\n", signal);
	switch (signal) {
	case SIGTERM:
		//AGLINK_INFO("SIGTERM\n");
		break;
	default:
		//AGLINK_INFO("Signal %d occurred, do nothing\n", signal);
		break;
	}
	g_interrupted = 1;
}

static uint64_t get_timestamp()
{
	struct timeval tv;
	gettimeofday(&tv, NULL);
	return tv.tv_sec * 1000 + tv.tv_usec / 1000;
}

extern int aglink_channel_create(int dev_id);

extern void aglink_audio_app_init(int dev_id);
extern void aglink_audio_app_deinit(int dev_id);

static int dev_id;
int main(int argc, char *argv[])
{

	struct sigaction act;
	act.sa_handler = signal_handler;
	sigemptyset(&act.sa_mask);
	act.sa_flags = 0;
	int next = 0;

	int ret;

	if (sigaction(SIGTERM, &act, NULL) < 0)
		AGLINK_INFO("install signal error\n");
	if (sigaction(SIGINT, &act, NULL) < 0)
		AGLINK_INFO("install signal error\n");
	if (sigaction(SIGABRT, &act, NULL) < 0)
		AGLINK_INFO("install signal error\n");
	if (sigaction(SIGSEGV, &act, NULL) < 0)
		AGLINK_INFO("install signal error\n");

	dev_id = 0;

	ret = aglink_channel_create(dev_id);
	if (ret < 0) {
		AGLINK_INFO("aglink create failed\n");
		return -1;
	}

	aglink_info_app_init(dev_id);

	aglink_audio_app_init(dev_id);

	while (!g_interrupted) {
		usleep(1000 * 1000);
	}

	aglink_info_app_deinit(dev_id);

	aglink_audio_app_deinit(dev_id);

	AGLINK_INFO("aglink exit\n");

	return 0;
}
