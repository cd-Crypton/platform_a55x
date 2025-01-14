// SPDX-License-Identifier: GPL-2.0
/*
 * Samsung Exynos SoC series Pablo driver
 *
 * Exynos Pablo image subsystem functions
 *
 * Copyright (c) 2022 Samsung Electronics Co., Ltd
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include "pablo-hw-chain-info.h"
#include "is-core.h"
#include "pablo-device-iommu-group.h"

static bool pablo_hw_chain_info_check_valid_lib(struct is_hardware *hw, u32 hw_id)
{
	switch(hw_id) {
	case DEV_HW_3AA0:
	case DEV_HW_3AA1:
	case DEV_HW_3AA2:
		return IS_ENABLED(TAA_DDK_LIB_CALL);
	case DEV_HW_ORB0:
		return IS_ENABLED(ORBMCH_DDK_LIB_CALL);
	case DEV_HW_ISP0:
		return IS_ENABLED(ISP_DDK_LIB_CALL);
	default:
		err_hw("invalid hw_id(%d)", hw_id);
		break;
	}

	return false;
}

static int pablo_hw_chain_info_get_hw_slot_id(struct is_hardware *hw, u32 hw_id)
{
	int slot_id = -1;

	switch (hw_id) {
	case DEV_HW_PAF0:
		slot_id = 0;
		break;
	case DEV_HW_PAF1:
		slot_id = 1;
		break;
	case DEV_HW_PAF2:
		slot_id = 2;
		break;
	case DEV_HW_3AA0:
		slot_id = 3;
		break;
	case DEV_HW_3AA1:
		slot_id = 4;
		break;
	case DEV_HW_3AA2:
		slot_id = 5;
		break;
	case DEV_HW_ORB0:
		slot_id = 6;
		break;
	case DEV_HW_ISP0:
		slot_id = 7;
		break;
	case DEV_HW_MCSC0:
		slot_id = 8;
		break;
	default:
		err("Invalid hw id(%d)", hw_id);
		break;
	}

	return slot_id;
}

static struct is_hw_ip *pablo_hw_chain_info_get_hw_ip(struct is_hardware *hw, u32 hw_id)
{
	int hw_slot = pablo_hw_chain_info_get_hw_slot_id(hw, hw_id);

	if (!valid_hw_slot_id(hw_slot)) {
		err("invalid slot (id:%d, slot:%d)", hw_id, hw_slot);
		return NULL;
	}

	return &hw->hw_ip[hw_slot];
}

static void pablo_hw_chain_info_query_cap(struct is_hardware *hw, u32 hw_id, void *cap_data)
{
	switch (hw_id) {
	case DEV_HW_MCSC0:
		{
			struct is_hw_mcsc_cap *cap = (struct is_hw_mcsc_cap *)cap_data;

			cap->hw_ver = HW_SET_VERSION(10, 0, 20, 0);
			cap->max_output = 3;
			cap->max_djag = 1;
			cap->max_cac = 0;
			cap->hwfc = MCSC_CAP_SUPPORT;
			cap->in_otf = MCSC_CAP_SUPPORT;
			cap->in_dma = MCSC_CAP_SUPPORT;
			cap->out_dma[0] = MCSC_CAP_SUPPORT;
			cap->out_dma[1] = MCSC_CAP_SUPPORT;
			cap->out_dma[2] = MCSC_CAP_SUPPORT;
			cap->out_dma[3] = MCSC_CAP_NOT_SUPPORT;
			cap->out_dma[4] = MCSC_CAP_NOT_SUPPORT;
			cap->out_dma[5] = MCSC_CAP_NOT_SUPPORT;
			cap->out_otf[0] = MCSC_CAP_NOT_SUPPORT;
			cap->out_otf[1] = MCSC_CAP_NOT_SUPPORT;
			cap->out_otf[2] = MCSC_CAP_NOT_SUPPORT;
			cap->out_otf[3] = MCSC_CAP_NOT_SUPPORT;
			cap->out_otf[4] = MCSC_CAP_NOT_SUPPORT;
			cap->out_otf[5] = MCSC_CAP_NOT_SUPPORT;
			cap->out_hwfc[1] = MCSC_CAP_SUPPORT;
			cap->out_hwfc[2] = MCSC_CAP_SUPPORT;
			cap->out_post[0] = MCSC_CAP_SUPPORT;
			cap->out_post[1] = MCSC_CAP_SUPPORT;
			cap->out_post[2] = MCSC_CAP_SUPPORT;
			cap->out_post[3] = MCSC_CAP_NOT_SUPPORT;
			cap->out_post[4] = MCSC_CAP_NOT_SUPPORT;
			cap->out_post[5] = MCSC_CAP_NOT_SUPPORT;
			cap->enable_shared_output = false;
			cap->djag = MCSC_CAP_SUPPORT;
			cap->cac = MCSC_CAP_NOT_SUPPORT;
			cap->ysum = MCSC_CAP_NOT_SUPPORT;
			cap->ds_vra = MCSC_CAP_NOT_SUPPORT;
		}
		break;
	default:
		break;
	}
}

static struct is_mem *pablo_hw_get_iommu_mem(struct is_hardware *hw, u32 gid)
{
	struct is_core *core = is_get_is_core();
	struct pablo_device_iommu_group *iommu_group;

	switch (gid) {
	case GROUP_ID_ISP0:
	case GROUP_ID_ORB0:
	case GROUP_ID_MCS0:
		iommu_group = pablo_iommu_group_get(1);
		return &iommu_group->mem;
	default:
		return &core->resourcemgr.mem;
	}
}

static const struct pablo_hw_chain_info_ops phci_ops = {
	.check_valid_lib = pablo_hw_chain_info_check_valid_lib,
	.get_hw_slot_id = pablo_hw_chain_info_get_hw_slot_id,
	.get_hw_ip = pablo_hw_chain_info_get_hw_ip,
	.query_cap = pablo_hw_chain_info_query_cap,
	.get_ctrl = pablo_hw_cmn_get_ctrl,
	.get_iommu_mem = pablo_hw_get_iommu_mem,
};

int pablo_hw_chain_info_probe(struct is_hardware *hw)
{
	hw->chain_info_ops = &phci_ops;

	return 0;
}
EXPORT_SYMBOL_GPL(pablo_hw_chain_info_probe);
