// SPDX-License-Identifier: GPL-2.0
/*
 * Samsung Exynos SoC series Pablo driver
 *
 * Copyright (c) 2021 Samsung Electronics Co., Ltd
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/delay.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/interrupt.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/firmware.h>
#include <linux/dma-mapping.h>
#include <linux/scatterlist.h>
#include <videodev2_exynos_camera.h>
#include <linux/v4l2-mediabus.h>
#include <linux/bug.h>

#include "is-hw.h"
#include "is-core.h"
#include "is-cmd.h"
#include "is-err.h"
#include "is-video.h"
#include "pablo-video.h"

int is_ssxvc_video_probe(void *data, u32 vc)
{
	int ret = 0;
	struct is_device_sensor *device;
	struct is_video *video;
	char* video_name;
	u32 video_id;
	u32 device_id;

	FIMC_BUG(!data);

	device = (struct is_device_sensor *)data;
	if (!device->pdev) {
		probe_err("pdev is NULL");
		ret = -EINVAL;
		goto p_err;
	}

	device_id = device->device_id;
	video = &device->video_ssxvc[vc];

	if (device_id >= IS_SENSOR_COUNT) {
		err("device_id %d was invalid", device_id);
		ret = -EINVAL;
		goto p_err;
	}

	video_name = IS_VIDEO_SSXVC_NAME(device_id, vc);
	video_id = IS_VIDEO_SS0VC0_NUM + vc + (device_id * 4);

	video->group_id = GROUP_ID_SS0 + device_id;
	video->subdev_ofs = offsetof(struct is_device_sensor, ssvc[0][vc]);

	ret = is_video_probe(video,
			video_name,
			video_id,
			VFL_DIR_RX,
			&device->v4l2_dev,
			NULL);
	if (ret)
		dev_err(&device->pdev->dev, "%s is fail(%d)\n", __func__, ret);

p_err:
	return ret;
}
