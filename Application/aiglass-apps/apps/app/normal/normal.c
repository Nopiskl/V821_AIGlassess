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
#include "aglink/ag_wrap.h"
#include <aglink/ag_proto.h>
#include <aglink_log.h>

static int normal_dev_id = 0;
volatile static int normal_app_exit;
static pthread_t ag_normal_thread;

static void aglink_normal_app_sync(void);
static void* aglink_normal_task(void* data)
{
	aglink_normal_app_sync();
	while (!normal_app_exit) {
		usleep(100);
	}
	AGLINK_INFO("aglink normal task exit\n");
	pthread_exit(NULL);
}


static int aglink_normal_app_rxcb(uint8_t type, uint8_t *data, int len)
{
	AGLINK_INFO("type: %s, len:%d\n", aglink_ad_type_to_string(type), len);
	switch (type) {
	default:
		break;
	}
	return 0;
}

static void aglink_normal_app_sync(void)
{

	struct ag_sync sync = {
		.mode = AG_MODE_NORMAL,
		.str = "U_OK",
	};

	aglink_tx_data(normal_dev_id, AG_VD_SYNC_2,
			(uint8_t *)&sync, sizeof(struct ag_sync));

}

void aglink_normal_app_init(int dev_id)
{
	int ret;

	normal_dev_id = dev_id;

	aglink_channel_register_cb(aglink_normal_app_rxcb, normal_dev_id);

	pthread_create(&ag_normal_thread, NULL, aglink_normal_task, NULL);

	normal_app_exit = 0;
}

void aglink_normal_app_deinit(int dev_id)
{
	normal_app_exit = 1;
	pthread_join(ag_normal_thread, NULL);
	aglink_channel_delete(normal_dev_id);
}


