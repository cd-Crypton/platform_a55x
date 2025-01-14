// SPDX-License-Identifier: GPL-2.0
/*
 * Samsung Exynos SoC series Pablo driver
 *
 * Copyright (c) 2020 Samsung Electronics Co., Ltd
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include "is-subdev-ctrl.h"

static const struct is_subdev_ops is_subdev_sensor_ops = {
	.bypass			= NULL,
	.cfg			= NULL,
	.tag			= NULL,
};

const struct is_subdev_ops *pablo_get_is_subdev_sensor_ops(void)
{
	return &is_subdev_sensor_ops;
}
KUNIT_EXPORT_SYMBOL(pablo_get_is_subdev_sensor_ops);
