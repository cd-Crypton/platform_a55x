/*
 * Copyright (c) 2021-2023 Qualcomm Innovation Center, Inc. All rights reserved.
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

/*
 * DOC: contains MLO manager roaming related functionality
 */
#include <wlan_cmn.h>
#include <wlan_cm_public_struct.h>
#include <wlan_cm_roam_public_struct.h>
#include "wlan_mlo_mgr_cmn.h"
#include "wlan_mlo_mgr_main.h"
#include "wlan_mlo_mgr_roam.h"
#include "wlan_mlo_mgr_public_structs.h"
#include "wlan_mlo_mgr_sta.h"
#include <../../core/src/wlan_cm_roam_i.h>
#include "wlan_cm_roam_api.h"
#include "wlan_mlme_vdev_mgr_interface.h"
#include <include/wlan_mlme_cmn.h>
#include <wlan_cm_api.h>

#ifdef WLAN_FEATURE_11BE_MLO
static bool
mlo_check_connect_req_bmap(struct wlan_objmgr_vdev *vdev)
{
	struct wlan_mlo_dev_context *mlo_dev_ctx = vdev->mlo_dev_ctx;
	struct wlan_mlo_sta *sta_ctx;
	uint8_t i = 0;

	if (!mlo_dev_ctx)
		return false;

	sta_ctx = mlo_dev_ctx->sta_ctx;

	for (i = 0; i < WLAN_UMAC_MLO_MAX_VDEVS; i++) {
		if (!mlo_dev_ctx->wlan_vdev_list[i])
			continue;

		if (vdev == mlo_dev_ctx->wlan_vdev_list[i])
			return qdf_test_bit(i, sta_ctx->wlan_connect_req_links);
	}

	mlo_err("vdev not found in ml dev ctx list");
	return false;
}

static void
mlo_update_for_multi_link_roam(struct wlan_objmgr_psoc *psoc,
			       uint8_t vdev_id,
			       uint8_t ml_link_vdev_id)
{
	struct wlan_objmgr_vdev *vdev;

	vdev = wlan_objmgr_get_vdev_by_id_from_psoc(psoc,
						    ml_link_vdev_id,
						    WLAN_MLME_SB_ID);
	if (!vdev) {
		mlo_err("VDEV is null");
		return;
	}

	if (vdev_id == ml_link_vdev_id) {
		wlan_vdev_mlme_set_mlo_vdev(vdev);
		goto end;
	}

	wlan_vdev_mlme_set_mlo_vdev(vdev);
	wlan_vdev_mlme_set_mlo_link_vdev(vdev);

	mlo_update_connect_req_links(vdev, true);

end:
	wlan_objmgr_vdev_release_ref(vdev, WLAN_MLME_SB_ID);
}

static void
mlo_cleanup_link(struct wlan_objmgr_vdev *vdev, uint8_t num_setup_links)
{
	if (num_setup_links >= 2 &&
	    wlan_vdev_mlme_is_mlo_link_vdev(vdev)) {
		cm_cleanup_mlo_link(vdev);
	} else if (!num_setup_links || wlan_vdev_mlme_is_mlo_link_vdev(vdev)) {
		wlan_vdev_mlme_clear_mlo_vdev(vdev);
		if (wlan_vdev_mlme_is_mlo_link_vdev(vdev)) {
			cm_cleanup_mlo_link(vdev);
			wlan_vdev_mlme_clear_mlo_link_vdev(vdev);
		}
	}
}

static void
mlo_update_vdev_after_roam(struct wlan_objmgr_psoc *psoc,
			   uint8_t vdev_id, uint8_t num_setup_links)
{
	struct wlan_mlo_dev_context *mlo_dev_ctx;
	uint8_t i;
	struct wlan_objmgr_vdev *vdev, *tmp_vdev;

	vdev = wlan_objmgr_get_vdev_by_id_from_psoc(psoc,
						    vdev_id,
						    WLAN_MLME_SB_ID);
	if (!vdev) {
		mlo_err("VDEV is null");
		return;
	}

	if (!vdev->mlo_dev_ctx)
		goto end;

	mlo_dev_ctx = vdev->mlo_dev_ctx;
	for (i = 0; i < WLAN_UMAC_MLO_MAX_VDEVS; i++) {
		if (!mlo_dev_ctx->wlan_vdev_list[i])
			continue;

		tmp_vdev = mlo_dev_ctx->wlan_vdev_list[i];
		mlo_cleanup_link(tmp_vdev, num_setup_links);
	}

end:
	wlan_objmgr_vdev_release_ref(vdev, WLAN_MLME_SB_ID);
}

static void
mlo_clear_link_bmap(struct wlan_objmgr_psoc *psoc, uint8_t vdev_id)
{
	struct wlan_objmgr_vdev *vdev;

	vdev = wlan_objmgr_get_vdev_by_id_from_psoc(psoc,
						    vdev_id,
						    WLAN_MLME_SB_ID);
	if (!vdev) {
		mlo_err("VDEV is null");
		return;
	}

	mlo_clear_connect_req_links_bmap(vdev);
	wlan_vdev_mlme_clear_mlo_vdev(vdev);
	if (wlan_vdev_mlme_is_mlo_link_vdev(vdev))
		wlan_vdev_mlme_clear_mlo_link_vdev(vdev);

	wlan_objmgr_vdev_release_ref(vdev, WLAN_MLME_SB_ID);
}

static QDF_STATUS
mlo_roam_abort_req(struct wlan_objmgr_psoc *psoc,
		   uint8_t *event, uint8_t vdev_id)
{
	struct roam_offload_synch_ind *sync_ind = NULL;

	sync_ind = (struct roam_offload_synch_ind *)event;

	if (!sync_ind) {
		mlme_err("Roam Sync ind ptr is NULL");
		return QDF_STATUS_E_NULL_VALUE;
	}

	wlan_mlo_roam_abort_on_link(psoc, event, vdev_id);
	cm_roam_stop_req(psoc, sync_ind->roamed_vdev_id,
			 REASON_ROAM_SYNCH_FAILED,
			 NULL, false);

	return QDF_STATUS_SUCCESS;
}
#else
static inline void
mlo_clear_link_bmap(struct wlan_objmgr_psoc *psoc, uint8_t vdev_id)
{}

static inline void
mlo_update_vdev_after_roam(struct wlan_objmgr_psoc *psoc,
			   uint8_t vdev_id, uint8_t num_setup_links)
{}

static inline void
mlo_cleanup_link(struct wlan_objmgr_vdev *vdev, uint8_t num_setup_links)
{}

static inline void
mlo_update_for_multi_link_roam(struct wlan_objmgr_psoc *psoc,
			       uint8_t vdev_id,
			       uint8_t ml_link_vdev_id)
{}

static inline bool
mlo_check_connect_req_bmap(struct wlan_objmgr_vdev *vdev)
{
	return false;
}

static inline QDF_STATUS
mlo_roam_abort_req(struct wlan_objmgr_psoc *psoc,
		   uint8_t *event, uint8_t vdev_id)
{
	return QDF_STATUS_E_NOSUPPORT;
}
#endif

static void mlo_roam_update_vdev_macaddr(struct wlan_objmgr_psoc *psoc,
					 uint8_t vdev_id,
					 bool is_non_ml_connection)
{
	struct wlan_objmgr_vdev *vdev;
	struct qdf_mac_addr *mld_mac;

	vdev = wlan_objmgr_get_vdev_by_id_from_psoc(psoc,
						    vdev_id,
						    WLAN_MLO_MGR_ID);
	if (!vdev) {
		mlo_err("VDEV is null");
		return;
	}

	if (is_non_ml_connection) {
		mld_mac = (struct qdf_mac_addr *)wlan_vdev_mlme_get_mldaddr(vdev);
		if (!qdf_is_macaddr_zero(mld_mac))
			wlan_vdev_mlme_set_macaddr(vdev, mld_mac->bytes);
	} else {
		wlan_vdev_mlme_set_macaddr(vdev,
					   wlan_vdev_mlme_get_linkaddr(vdev));
	}

	mlme_debug("vdev_id %d self mac " QDF_MAC_ADDR_FMT,
		   vdev_id,
		   QDF_MAC_ADDR_REF(wlan_vdev_mlme_get_macaddr(vdev)));
	wlan_objmgr_vdev_release_ref(vdev, WLAN_MLO_MGR_ID);
}

QDF_STATUS mlo_fw_roam_sync_req(struct wlan_objmgr_psoc *psoc, uint8_t vdev_id,
				void *event, uint32_t event_data_len)
{
	struct roam_offload_synch_ind *sync_ind;
	QDF_STATUS status;
	uint8_t i;
	bool is_non_mlo_ap = false;

	sync_ind = (struct roam_offload_synch_ind *)event;
	if (!sync_ind)
		return QDF_STATUS_E_FAILURE;

	for (i = 0; i < sync_ind->num_setup_links; i++)
		mlo_update_for_multi_link_roam(psoc, vdev_id,
					       sync_ind->ml_link[i].vdev_id);

	if (!sync_ind->num_setup_links) {
		mlo_debug("MLO_ROAM: Roamed to Legacy");
		is_non_mlo_ap = true;
	} else if (sync_ind->num_setup_links == 1 ||
		sync_ind->auth_status == ROAM_AUTH_STATUS_CONNECTED) {
		mlo_debug("MLO_ROAM: Roamed to single link MLO");
		mlo_set_single_link_ml_roaming(psoc, vdev_id, true);
	} else {
		mlo_debug("MLO_ROAM: Roamed to MLO");
	}

	mlo_roam_update_vdev_macaddr(psoc, vdev_id, is_non_mlo_ap);
	status = cm_fw_roam_sync_req(psoc, vdev_id, event, event_data_len);

	if (QDF_IS_STATUS_ERROR(status))
		mlo_clear_link_bmap(psoc, vdev_id);

	return status;
}

#ifdef WLAN_FEATURE_11BE_MLO_ADV_FEATURE
void mlo_cm_roam_sync_cb(struct wlan_objmgr_vdev *vdev,
			 void *event, uint32_t event_data_len)
{
	QDF_STATUS status;
	struct roam_offload_synch_ind *sync_ind;
	struct wlan_objmgr_psoc *psoc;
	struct wlan_objmgr_vdev *link_vdev = NULL;
	uint8_t i;
	uint8_t vdev_id;

	sync_ind = (struct roam_offload_synch_ind *)event;
	vdev_id = wlan_vdev_get_id(vdev);
	psoc = wlan_vdev_get_psoc(vdev);

	/* Clean up link vdev in following cases
	 * 1. When roamed to legacy, num_setup_links = 0
	 * 2. When roamed to single link, num_setup_links = 1
	 * 3. Roamed to AP with auth_status = ROAMED_AUTH_STATUS_CONNECTED
	 */
	if (sync_ind->num_setup_links < 2 ||
	    sync_ind->auth_status == ROAM_AUTH_STATUS_CONNECTED)
		mlo_update_vdev_after_roam(psoc, vdev_id,
					   sync_ind->num_setup_links);

	/* If EAPOL is offloaded to supplicant, link vdev/s are not up
	 * at FW, in that case complete roam sync on assoc vdev
	 * link vdev will be initialized after set key is complete.
	 */
	if (sync_ind->auth_status == ROAM_AUTH_STATUS_CONNECTED)
		return;

	for (i = 0; i < sync_ind->num_setup_links; i++) {
		if (vdev_id == sync_ind->ml_link[i].vdev_id)
			continue;

		link_vdev = wlan_objmgr_get_vdev_by_id_from_psoc(psoc,
								 sync_ind->ml_link[i].vdev_id,
								 WLAN_MLME_SB_ID);

		if (!link_vdev) {
			mlo_err("Link vdev is null");
			return;
		}

		if (mlo_check_connect_req_bmap(link_vdev)) {
			mlo_update_connect_req_links(link_vdev, false);

			status = cm_fw_roam_sync_req(psoc,
						     sync_ind->ml_link[i].vdev_id,
						     event, event_data_len);
			if (QDF_IS_STATUS_ERROR(status)) {
				mlo_clear_connect_req_links_bmap(link_vdev);
				mlo_roam_abort_req(psoc, event,
						   sync_ind->ml_link[i].vdev_id);
				wlan_objmgr_vdev_release_ref(link_vdev,
							     WLAN_MLME_SB_ID);
				return;
			}
		}
		wlan_objmgr_vdev_release_ref(link_vdev,
					     WLAN_MLME_SB_ID);
	}
}
#endif

void
mlo_fw_ho_fail_req(struct wlan_objmgr_psoc *psoc,
		   uint8_t vdev_id, struct qdf_mac_addr bssid)
{
	struct wlan_objmgr_vdev *vdev;
	struct wlan_mlo_dev_context *mlo_dev_ctx;
	uint8_t i;

	vdev = wlan_objmgr_get_vdev_by_id_from_psoc(psoc,
						    vdev_id,
						    WLAN_MLME_SB_ID);

	if (!vdev) {
		mlo_err("vdev is null");
		return;
	}

	if (!vdev->mlo_dev_ctx)
		goto end;

	mlo_dev_ctx = vdev->mlo_dev_ctx;

	for (i = 0; i < WLAN_UMAC_MLO_MAX_VDEVS; i++) {
		if (!mlo_dev_ctx->wlan_vdev_list[i] ||
		    mlo_dev_ctx->wlan_vdev_list[i] == vdev)
			continue;
		cm_fw_ho_fail_req(psoc,
				  wlan_vdev_get_id(mlo_dev_ctx->wlan_vdev_list[i]),
				  bssid);
	}

end:
	cm_fw_ho_fail_req(psoc, vdev_id, bssid);
	wlan_objmgr_vdev_release_ref(vdev,
				     WLAN_MLME_SB_ID);
}

QDF_STATUS
mlo_get_sta_link_mac_addr(uint8_t vdev_id,
			  struct roam_offload_synch_ind *sync_ind,
			  struct qdf_mac_addr *link_mac_addr)
{
	QDF_STATUS status = QDF_STATUS_SUCCESS;
	uint8_t i;

	if (!sync_ind || !sync_ind->num_setup_links)
		return QDF_STATUS_E_FAILURE;

	for (i = 0; i < sync_ind->num_setup_links; i++) {
		if (sync_ind->ml_link[i].vdev_id == vdev_id) {
			qdf_copy_macaddr(link_mac_addr,
					 &sync_ind->ml_link[i].link_addr);
			return status;
		}
	}

	if (i == sync_ind->num_setup_links) {
		mlo_err("Link mac addr not found");
		status = QDF_STATUS_E_FAILURE;
	}

	return status;
}

uint32_t
mlo_roam_get_chan_freq(uint8_t vdev_id,
		       struct roam_offload_synch_ind *sync_ind)
{
	uint8_t i;

	if (!sync_ind || !sync_ind->num_setup_links)
		return 0;

	for (i = 0; i < sync_ind->num_setup_links; i++) {
		if (sync_ind->ml_link[i].vdev_id == vdev_id)
			return sync_ind->ml_link[i].channel.mhz;
	}

	return 0;
}

uint32_t
mlo_roam_get_link_id(uint8_t vdev_id,
		     struct roam_offload_synch_ind *sync_ind)
{
	uint8_t i;

	if (!sync_ind || !sync_ind->num_setup_links)
		return 0;

	for (i = 0; i < sync_ind->num_setup_links; i++) {
		if (sync_ind->ml_link[i].vdev_id == vdev_id)
			return sync_ind->ml_link[i].link_id;
	}

	return 0;
}

bool is_multi_link_roam(struct roam_offload_synch_ind *sync_ind)
{
	if (!sync_ind)
		return false;

	if (sync_ind->num_setup_links)
		return true;

	return false;
}

uint32_t
mlo_roam_get_link_freq_from_mac_addr(struct roam_offload_synch_ind *sync_ind,
				     uint8_t *link_mac_addr)
{
	uint8_t i;

	if (!sync_ind || !sync_ind->num_setup_links || !link_mac_addr)
		return 0;

	for (i = 0; i < sync_ind->num_setup_links; i++)
		if (!qdf_mem_cmp(sync_ind->ml_link[i].link_addr.bytes,
				 link_mac_addr,
				 QDF_MAC_ADDR_SIZE))
			return sync_ind->ml_link[i].channel.mhz;

	return 0;
}

QDF_STATUS mlo_enable_rso(struct wlan_objmgr_pdev *pdev,
			  struct wlan_objmgr_vdev *vdev,
			  struct wlan_cm_connect_resp *rsp)
{
	struct wlan_objmgr_vdev *assoc_vdev;
	uint8_t num_partner_links;

	if (!rsp) {
		mlo_err("Connect resp is null");
		return QDF_STATUS_E_NULL_VALUE;
	}

	num_partner_links = rsp->ml_parnter_info.num_partner_links;

	if (wlan_vdev_mlme_is_mlo_link_vdev(vdev) ||
	    !num_partner_links ||
	    num_partner_links == 1) {
		assoc_vdev = wlan_mlo_get_assoc_link_vdev(vdev);
		if (!assoc_vdev) {
			mlo_err("Assoc vdev is null");
			return QDF_STATUS_E_NULL_VALUE;
		}
		cm_roam_start_init_on_connect(pdev,
					      wlan_vdev_get_id(assoc_vdev));
	}

	return QDF_STATUS_SUCCESS;
}

void
mlo_roam_copy_partner_info(struct wlan_cm_connect_resp *connect_rsp,
			   struct roam_offload_synch_ind *sync_ind)
{
	uint8_t i;
	struct mlo_partner_info *partner_info;

	if (!sync_ind)
		return;

	partner_info = &connect_rsp->ml_parnter_info;

	for (i = 0; i < sync_ind->num_setup_links; i++) {
		partner_info->partner_link_info[i].link_id =
			sync_ind->ml_link[i].link_id;
		partner_info->partner_link_info[i].vdev_id =
			sync_ind->ml_link[i].vdev_id;

		qdf_copy_macaddr(
			&partner_info->partner_link_info[i].link_addr,
			&sync_ind->ml_link[i].link_addr);
		partner_info->partner_link_info[i].chan_freq =
				sync_ind->ml_link[i].channel.mhz;
		mlo_debug("vdev_id %d link_id %d freq %d bssid" QDF_MAC_ADDR_FMT,
			  sync_ind->ml_link[i].vdev_id,
			  sync_ind->ml_link[i].link_id,
			  sync_ind->ml_link[i].channel.mhz,
			  QDF_MAC_ADDR_REF(sync_ind->ml_link[i].link_addr.bytes));
	}
	partner_info->num_partner_links = sync_ind->num_setup_links;
	mlo_debug("num_setup_links %d", sync_ind->num_setup_links);
}

void mlo_roam_init_cu_bpcc(struct wlan_objmgr_vdev *vdev,
			   struct roam_offload_synch_ind *sync_ind)
{
	uint8_t i;
	struct wlan_mlo_dev_context *mlo_dev_ctx;

	if (!vdev) {
		mlo_err("vdev is NULL");
		return;
	}

	mlo_dev_ctx = vdev->mlo_dev_ctx;
	if (!mlo_dev_ctx) {
		mlo_err("ML dev ctx is NULL");
		return;
	}

	mlo_clear_cu_bpcc(vdev);
	for (i = 0; i < sync_ind->num_setup_links; i++)
		mlo_init_cu_bpcc(mlo_dev_ctx, sync_ind->ml_link[i].vdev_id);

	mlo_debug("update cu info from roam sync");
}

void
mlo_roam_update_connected_links(struct wlan_objmgr_vdev *vdev,
				struct wlan_cm_connect_resp *connect_rsp)
{
	mlo_clear_connected_links_bmap(vdev);
	mlo_update_connected_links_bmap(vdev->mlo_dev_ctx,
					connect_rsp->ml_parnter_info);
}

QDF_STATUS
wlan_mlo_roam_abort_on_link(struct wlan_objmgr_psoc *psoc,
			    uint8_t *event, uint8_t vdev_id)
{
	uint8_t i;
	QDF_STATUS status;
	struct roam_offload_synch_ind *sync_ind = NULL;

	sync_ind = (struct roam_offload_synch_ind *)event;

	if (!sync_ind) {
		mlo_err("Roam Sync ind ptr is NULL");
		return QDF_STATUS_E_NULL_VALUE;
	}

	for (i = 0; i < sync_ind->num_setup_links; i++) {
		if (sync_ind->ml_link[i].vdev_id != vdev_id) {
			status = cm_fw_roam_abort_req(psoc,
						      sync_ind->ml_link[i].vdev_id);
			if (QDF_IS_STATUS_ERROR(status)) {
				mlo_err("LFR3: Fail to abort roam on vdev: %u",
					sync_ind->ml_link[i].vdev_id);
			}
		}
	}

	return QDF_STATUS_SUCCESS;
}

void
mlo_set_single_link_ml_roaming(struct wlan_objmgr_psoc *psoc,
			       uint8_t vdev_id,
			       bool is_single_link_ml_roaming)
{
	struct wlan_objmgr_vdev *vdev;

	vdev = wlan_objmgr_get_vdev_by_id_from_psoc(psoc,
						    vdev_id,
						    WLAN_MLME_SB_ID);
	if (!vdev) {
		mlo_err("VDEV is null");
		return;
	}

	if (!wlan_vdev_mlme_is_mlo_link_vdev(vdev))
		mlme_set_single_link_mlo_roaming(vdev,
						 is_single_link_ml_roaming);

	wlan_objmgr_vdev_release_ref(vdev, WLAN_MLME_SB_ID);
}

bool
mlo_get_single_link_ml_roaming(struct wlan_objmgr_psoc *psoc,
			       uint8_t vdev_id)
{
	bool is_single_link_ml_roaming = false;
	struct wlan_objmgr_vdev *vdev;

	vdev = wlan_objmgr_get_vdev_by_id_from_psoc(psoc,
						    vdev_id,
						    WLAN_MLME_SB_ID);
	if (!vdev) {
		mlo_err("VDEV is null");
		return is_single_link_ml_roaming;
	}

	is_single_link_ml_roaming = mlme_get_single_link_mlo_roaming(vdev);
	mlo_debug("MLO:is_single_link_ml_roaming %d",
		  is_single_link_ml_roaming);
	wlan_objmgr_vdev_release_ref(vdev, WLAN_MLME_SB_ID);

	return is_single_link_ml_roaming;
}

QDF_STATUS
mlo_roam_get_bssid_chan_for_link(uint8_t vdev_id,
				 struct roam_offload_synch_ind *sync_ind,
				 struct qdf_mac_addr *bssid,
				 wmi_channel *chan)
{
	QDF_STATUS status = QDF_STATUS_SUCCESS;
	uint8_t i;

	if (!sync_ind || !sync_ind->num_setup_links)
		return QDF_STATUS_E_FAILURE;

	for (i = 0; i < sync_ind->num_setup_links; i++) {
		if (vdev_id == sync_ind->ml_link[i].vdev_id) {
			qdf_mem_copy(chan, &sync_ind->ml_link[i].channel,
				     sizeof(wmi_channel));
			qdf_copy_macaddr(bssid,
					 &sync_ind->ml_link[i].link_addr);
			return status;
		}
	}

	if (i == sync_ind->num_setup_links) {
		mlo_err("roam sync info not found for vdev id %d", vdev_id);
		status = QDF_STATUS_E_FAILURE;
	}

	return status;
}

bool
mlo_check_if_all_links_up(struct wlan_objmgr_vdev *vdev)
{
	struct wlan_mlo_dev_context *mlo_dev_ctx;
	struct wlan_mlo_sta *sta_ctx;
	uint8_t i;

	if (!vdev || !vdev->mlo_dev_ctx) {
		mlo_err("Vdev is null");
		return false;
	}

	mlo_dev_ctx = vdev->mlo_dev_ctx;
	sta_ctx = mlo_dev_ctx->sta_ctx;
	for (i = 0; i < WLAN_UMAC_MLO_MAX_VDEVS; i++) {
		if (!mlo_dev_ctx->wlan_vdev_list[i])
			continue;

		if (qdf_test_bit(i, sta_ctx->wlan_connected_links) &&
		    !wlan_cm_is_vdev_connected(mlo_dev_ctx->wlan_vdev_list[i])) {
			mlo_debug("Vdev id %d is not in connected state",
				  wlan_vdev_get_id(mlo_dev_ctx->wlan_vdev_list[i]));
			return false;
		}
	}

	if (i == WLAN_UMAC_MLO_MAX_VDEVS) {
		mlo_debug("all links are up");
		return true;
	}

	return false;
}

void
mlo_roam_set_link_id(struct wlan_objmgr_vdev *vdev,
		     struct roam_offload_synch_ind *sync_ind)
{
	uint8_t i;

	for (i = 0; i < sync_ind->num_setup_links; i++) {
		if (sync_ind->ml_link[i].vdev_id == wlan_vdev_get_id(vdev)) {
			wlan_vdev_set_link_id(vdev,
					      sync_ind->ml_link[i].link_id);
			mlme_debug("Set link for vdev id %d link id %d",
				   wlan_vdev_get_id(vdev),
				   sync_ind->ml_link[i].link_id);
		}
	}
}

QDF_STATUS
mlo_get_link_mac_addr_from_reassoc_rsp(struct wlan_objmgr_vdev *vdev,
				       struct qdf_mac_addr *link_mac_addr)
{
	uint8_t i;
	struct wlan_mlo_sta *sta_ctx;
	struct wlan_cm_connect_resp *rsp;
	struct mlo_partner_info parnter_info;
	uint8_t vdev_id;

	if (!vdev)
		return QDF_STATUS_E_NULL_VALUE;

	vdev_id = wlan_vdev_get_id(vdev);

	if (!vdev->mlo_dev_ctx) {
		mlo_err("mlo dev ctx is null, vdev id %d", vdev_id);
		return QDF_STATUS_E_NULL_VALUE;
	}

	sta_ctx = vdev->mlo_dev_ctx->sta_ctx;
	if (!sta_ctx || !sta_ctx->copied_reassoc_rsp ||
	    !sta_ctx->copied_reassoc_rsp->roaming_info) {
		mlo_debug("sta ctx or copied reassoc rsp is null for vdev id %d", vdev_id);
		return QDF_STATUS_E_NULL_VALUE;
	}

	rsp = sta_ctx->copied_reassoc_rsp;
	if (rsp->roaming_info->auth_status != ROAM_AUTH_STATUS_CONNECTED) {
		mlo_debug("Roam auth status is not connected");
		return QDF_STATUS_E_FAILURE;
	}

	parnter_info = rsp->ml_parnter_info;
	for (i = 0; i < parnter_info.num_partner_links; i++) {
		if (parnter_info.partner_link_info[i].vdev_id == vdev_id) {
			qdf_copy_macaddr(link_mac_addr,
					 &parnter_info.partner_link_info[i].link_addr);
			return QDF_STATUS_SUCCESS;
		}
	}

	if (i == parnter_info.num_partner_links) {
		mlo_debug("Link mac addr not found");
		return QDF_STATUS_E_FAILURE;
	}

	return QDF_STATUS_SUCCESS;
}

QDF_STATUS
mlo_roam_copy_reassoc_rsp(struct wlan_objmgr_vdev *vdev,
			  struct wlan_cm_connect_resp *reassoc_rsp)
{
	struct wlan_mlo_dev_context *mlo_dev_ctx;
	struct wlan_mlo_sta *sta_ctx;
	struct wlan_connect_rsp_ies *connect_ies;

	if (!vdev)
		return QDF_STATUS_E_NULL_VALUE;

	if (!reassoc_rsp)
		return QDF_STATUS_E_NULL_VALUE;

	/* Store reassoc rsp only if roamed to 2 link AP */
	if (reassoc_rsp->ml_parnter_info.num_partner_links < 2)
		return QDF_STATUS_E_INVAL;

	mlo_dev_ctx = vdev->mlo_dev_ctx;
	if (!mlo_dev_ctx)
		return QDF_STATUS_E_NULL_VALUE;

	sta_ctx = mlo_dev_ctx->sta_ctx;
	if (!sta_ctx)
		return QDF_STATUS_E_NULL_VALUE;

	if (sta_ctx) {
		sta_ctx->copied_reassoc_rsp = qdf_mem_malloc(
				sizeof(struct wlan_cm_connect_resp));
		if (!sta_ctx->copied_reassoc_rsp)
			return QDF_STATUS_E_NOMEM;

		qdf_mem_copy(sta_ctx->copied_reassoc_rsp, reassoc_rsp,
			     sizeof(struct wlan_cm_connect_resp));

		sta_ctx->copied_reassoc_rsp->roaming_info = qdf_mem_malloc(
				sizeof(struct wlan_roam_sync_info));

		if (!sta_ctx->copied_reassoc_rsp->roaming_info)
			return QDF_STATUS_E_NOMEM;

		qdf_mem_copy(sta_ctx->copied_reassoc_rsp->roaming_info,
			     reassoc_rsp->roaming_info,
			     sizeof(struct wlan_roam_sync_info));

		connect_ies = &sta_ctx->copied_reassoc_rsp->connect_ies;

		connect_ies->assoc_rsp.len =
			reassoc_rsp->connect_ies.assoc_rsp.len;

		connect_ies->assoc_rsp.ptr = qdf_mem_malloc(
				connect_ies->assoc_rsp.len);

		if (!connect_ies->assoc_rsp.ptr)
			return QDF_STATUS_E_NOMEM;

		qdf_mem_copy(connect_ies->assoc_rsp.ptr,
			     reassoc_rsp->connect_ies.assoc_rsp.ptr,
			     reassoc_rsp->connect_ies.assoc_rsp.len);

		connect_ies->assoc_req.len = 0;
		connect_ies->assoc_req.ptr = NULL;
		connect_ies->bcn_probe_rsp.len = 0;
		connect_ies->bcn_probe_rsp.ptr = NULL;
		connect_ies->link_bcn_probe_rsp.len = 0;
		connect_ies->link_bcn_probe_rsp.ptr = NULL;
		connect_ies->fils_ie = NULL;

		mlo_debug("Copied reassoc response");
	}

	return QDF_STATUS_SUCCESS;
}

static void
mlo_roam_free_connect_rsp(struct wlan_cm_connect_resp *rsp)
{
	struct wlan_connect_rsp_ies *connect_ie =
						&rsp->connect_ies;

	if (connect_ie->assoc_req.ptr) {
		qdf_mem_free(connect_ie->assoc_req.ptr);
		connect_ie->assoc_req.ptr = NULL;
	}

	if (connect_ie->bcn_probe_rsp.ptr) {
		qdf_mem_free(connect_ie->bcn_probe_rsp.ptr);
		connect_ie->bcn_probe_rsp.ptr = NULL;
	}

	if (connect_ie->link_bcn_probe_rsp.ptr) {
		qdf_mem_free(connect_ie->link_bcn_probe_rsp.ptr);
		connect_ie->link_bcn_probe_rsp.ptr = NULL;
	}

	if (connect_ie->assoc_rsp.ptr) {
		qdf_mem_free(connect_ie->assoc_rsp.ptr);
		connect_ie->assoc_rsp.ptr = NULL;
	}

	if (connect_ie->fils_ie && connect_ie->fils_ie->fils_pmk) {
		qdf_mem_zero(connect_ie->fils_ie->fils_pmk,
			     connect_ie->fils_ie->fils_pmk_len);
		qdf_mem_free(connect_ie->fils_ie->fils_pmk);
	}

	if (connect_ie->fils_ie) {
		qdf_mem_zero(connect_ie->fils_ie, sizeof(*connect_ie->fils_ie));
		qdf_mem_free(connect_ie->fils_ie);
	}

	if (rsp->roaming_info) {
		qdf_mem_free(rsp->roaming_info);
		rsp->roaming_info = NULL;
	}

	qdf_mem_zero(rsp, sizeof(*rsp));
	qdf_mem_free(rsp);
}

static bool
mlo_roam_is_internal_disconnect(struct wlan_objmgr_vdev *link_vdev)
{
	struct wlan_cm_vdev_discon_req *disconn_req;

	if (wlan_vdev_mlme_is_mlo_link_vdev(link_vdev) &&
	    wlan_cm_is_vdev_disconnecting(link_vdev)) {
		mlo_debug("Disconnect is ongoing on vdev %d",
			  wlan_vdev_get_id(link_vdev));

		disconn_req = qdf_mem_malloc(sizeof(*disconn_req));
		if (!disconn_req) {
			mlme_err("Malloc failed for disconnect req");
			return false;
		}

		if (!wlan_cm_get_active_disconnect_req(link_vdev,
						       disconn_req)) {
			mlme_err("vdev: %d: Active disconnect not found",
				 wlan_vdev_get_id(link_vdev));
			qdf_mem_free(disconn_req);
			return false;
		}

		mlo_debug("Disconnect source %d", disconn_req->req.source);

		if (disconn_req->req.source == CM_MLO_ROAM_INTERNAL_DISCONNECT) {
			qdf_mem_free(disconn_req);
			return true;
		}

		qdf_mem_free(disconn_req);
	}
	/* Disconnect is not ongoing */
	return true;
}

static QDF_STATUS
mlo_roam_validate_req(struct wlan_objmgr_vdev *vdev,
		      struct wlan_objmgr_vdev *link_vdev,
		      struct wlan_cm_connect_resp *rsp)
{
	struct wlan_mlo_dev_context *mlo_dev_ctx;
	struct wlan_mlo_sta *sta_ctx;

	if (!vdev) {
		mlo_debug_rl("vdev is NULL");
		return QDF_STATUS_E_NULL_VALUE;
	}

	mlo_dev_ctx = vdev->mlo_dev_ctx;
	if (!mlo_dev_ctx) {
		mlo_debug_rl("mlo_dev_ctx is NULL");
		return QDF_STATUS_E_NULL_VALUE;
	}

	sta_ctx = mlo_dev_ctx->sta_ctx;
	if (sta_ctx && sta_ctx->disconn_req) {
		mlo_debug("Handle pending disconnect for vdev %d",
			  wlan_vdev_get_id(vdev));
		mlo_handle_pending_disconnect(vdev);
		return QDF_STATUS_E_FAILURE;
	}

	if (wlan_cm_is_vdev_disconnected(vdev) ||
	    (wlan_vdev_mlme_is_mlo_link_vdev(link_vdev) &&
	     (wlan_cm_is_vdev_connecting(link_vdev) ||
	      !mlo_roam_is_internal_disconnect(link_vdev)))) {
		if (sta_ctx) {
			if (sta_ctx->copied_reassoc_rsp) {
				mlo_roam_free_connect_rsp(sta_ctx->copied_reassoc_rsp);
				sta_ctx->copied_reassoc_rsp = NULL;
			}
			copied_conn_req_lock_acquire(sta_ctx);
			if (sta_ctx->copied_conn_req) {
				mlo_free_connect_ies(sta_ctx->copied_conn_req);
				qdf_mem_free(sta_ctx->copied_conn_req);
				sta_ctx->copied_conn_req = NULL;
			}
			copied_conn_req_lock_release(sta_ctx);
		}
	}

	if (wlan_vdev_mlme_is_mlo_vdev(vdev)) {
		mlo_debug("Vdev: %d", wlan_vdev_get_id(vdev));
		if (wlan_cm_is_vdev_disconnected(vdev)) {
			mlo_handle_sta_link_connect_failure(vdev, rsp);
			return QDF_STATUS_E_FAILURE;
		} else if (!wlan_cm_is_vdev_connected(vdev)) {
			/* If vdev is not in disconnected or connected state,
			 * then the event is received due to connect req being
			 * flushed. Hence, ignore this event
			 */
			if (sta_ctx && sta_ctx->copied_reassoc_rsp) {
				mlo_roam_free_connect_rsp(sta_ctx->copied_reassoc_rsp);
				sta_ctx->copied_reassoc_rsp = NULL;
			}
			return QDF_STATUS_E_FAILURE;
		}
	}

	if (wlan_vdev_mlme_is_mlo_link_vdev(link_vdev) &&
	    (wlan_cm_is_vdev_connecting(link_vdev) ||
	     !mlo_roam_is_internal_disconnect(link_vdev))) {
		return QDF_STATUS_E_FAILURE;
	}

	if (sta_ctx && !wlan_vdev_mlme_is_mlo_link_vdev(vdev)) {
		if (sta_ctx->assoc_rsp.ptr) {
			qdf_mem_free(sta_ctx->assoc_rsp.ptr);
			sta_ctx->assoc_rsp.ptr = NULL;
		}
		sta_ctx->assoc_rsp.len = rsp->connect_ies.assoc_rsp.len;
		sta_ctx->assoc_rsp.ptr =
			qdf_mem_malloc(rsp->connect_ies.assoc_rsp.len);
		if (!sta_ctx->assoc_rsp.ptr)
			return QDF_STATUS_E_FAILURE;
		if (rsp->connect_ies.assoc_rsp.ptr)
			qdf_mem_copy(sta_ctx->assoc_rsp.ptr,
				     rsp->connect_ies.assoc_rsp.ptr,
				     rsp->connect_ies.assoc_rsp.len);
		/* Update connected_links_bmap for all vdev taking
		 * part in association
		 */
		mlo_update_connected_links(vdev, 1);
		mlo_update_connected_links_bmap(mlo_dev_ctx,
						rsp->ml_parnter_info);
	}

	return QDF_STATUS_SUCCESS;
}

static QDF_STATUS
mlo_roam_prepare_and_send_link_connect_req(struct wlan_objmgr_vdev *assoc_vdev,
					   struct wlan_objmgr_vdev *link_vdev,
					   struct wlan_cm_connect_resp *rsp,
					   struct qdf_mac_addr *link_addr,
					   uint16_t chan_freq)
{
	struct wlan_mlo_sta *sta_ctx;
	struct wlan_cm_connect_req req = {0};
	struct wlan_ssid ssid = {0};
	struct rso_config *rso_cfg;
	QDF_STATUS status = QDF_STATUS_SUCCESS;

	if (!assoc_vdev || !link_vdev || !rsp)
		return QDF_STATUS_E_FAILURE;

	if (!assoc_vdev->mlo_dev_ctx || !assoc_vdev->mlo_dev_ctx->sta_ctx)
		return QDF_STATUS_E_FAILURE;

	sta_ctx = assoc_vdev->mlo_dev_ctx->sta_ctx;

	wlan_vdev_mlme_get_ssid(assoc_vdev, ssid.ssid,
				&ssid.length);

	rso_cfg = wlan_cm_get_rso_config(assoc_vdev);
	req.vdev_id = wlan_vdev_get_id(link_vdev);
	req.source = CM_MLO_LINK_VDEV_CONNECT;
	qdf_mem_copy(&req.bssid.bytes,
		     link_addr->bytes,
		     QDF_MAC_ADDR_SIZE);
	req.ssid.length = ssid.length;
	qdf_mem_copy(&req.ssid.ssid, &ssid.ssid, ssid.length);
	req.chan_freq = chan_freq;

	req.ml_parnter_info = rsp->ml_parnter_info;
	if (rso_cfg) {
		req.crypto.rsn_caps = rso_cfg->orig_sec_info.rsn_caps;
		req.crypto.auth_type = rso_cfg->orig_sec_info.authmodeset;
		req.crypto.ciphers_pairwise = rso_cfg->orig_sec_info.ucastcipherset;
		req.crypto.group_cipher = rso_cfg->orig_sec_info.mcastcipherset;
		req.crypto.akm_suites = rso_cfg->orig_sec_info.key_mgmt;
		req.assoc_ie.len = rso_cfg->assoc_ie.len;
		if (rso_cfg->assoc_ie.len)
			qdf_mem_copy(&req.assoc_ie.ptr, &rso_cfg->assoc_ie.ptr,
				     rso_cfg->assoc_ie.len);
	}

	mlo_debug("vdev_id %d, chan_freq %d, mac_addr " QDF_MAC_ADDR_FMT,
		  req.vdev_id, req.chan_freq,
		  QDF_MAC_ADDR_REF(link_addr->bytes));

	mlme_cm_osif_roam_get_scan_params(assoc_vdev, &req.scan_ie,
					  &req.dot11mode_filter);

	copied_conn_req_lock_acquire(sta_ctx);
	if (!sta_ctx->copied_conn_req)
		sta_ctx->copied_conn_req = qdf_mem_malloc(
				sizeof(struct wlan_cm_connect_req));
	else
		mlo_free_connect_ies(sta_ctx->copied_conn_req);

	mlo_debug("MLO_ROAM: storing from roam connect rsp to connect req");
	if (sta_ctx->copied_conn_req) {
		qdf_mem_copy(sta_ctx->copied_conn_req, &req,
			     sizeof(struct wlan_cm_connect_req));
		mlo_allocate_and_copy_ies(sta_ctx->copied_conn_req,
					  &req);
		copied_conn_req_lock_release(sta_ctx);
	} else {
		mlo_err("MLO_ROAM: Failed to allocate connect req");
		copied_conn_req_lock_release(sta_ctx);
		return QDF_STATUS_E_NOMEM;
	}

	status = mlo_roam_validate_req(assoc_vdev, link_vdev, rsp);
	if (QDF_IS_STATUS_ERROR(status))
		return status;

	mlo_debug("MLO_ROAM: Partner link connect mac:" QDF_MAC_ADDR_FMT " vdev_id:%d",
		  QDF_MAC_ADDR_REF(req.bssid.bytes),
		  req.vdev_id);
	status = wlan_cm_start_connect(link_vdev, &req);
	if (QDF_IS_STATUS_ERROR(status))
		return status;

	mlo_update_connected_links(link_vdev, 1);
	return status;
}

void mlo_roam_connect_complete(struct wlan_objmgr_psoc *psoc,
			       struct wlan_objmgr_pdev *pdev,
			       struct wlan_objmgr_vdev *vdev,
			       struct wlan_cm_connect_resp *rsp)
{
	struct wlan_mlo_sta *sta_ctx;
	uint8_t auth_status;

	if (!vdev)
		return;

	if (!wlan_vdev_mlme_is_mlo_link_vdev(vdev))
		return;

	if (!vdev->mlo_dev_ctx)
		return;

	sta_ctx = vdev->mlo_dev_ctx->sta_ctx;
	if (!sta_ctx || !sta_ctx->copied_reassoc_rsp ||
	    !sta_ctx->copied_reassoc_rsp->roaming_info)
		return;

	auth_status = sta_ctx->copied_reassoc_rsp->roaming_info->auth_status;
	if (!mlo_check_connect_req_bmap(vdev) &&
	    auth_status == ROAM_AUTH_STATUS_CONNECTED) {
		mlo_roam_free_connect_rsp(sta_ctx->copied_reassoc_rsp);
		sta_ctx->copied_reassoc_rsp = NULL;
	}
}

bool
mlo_roam_is_auth_status_connected(struct wlan_objmgr_psoc *psoc, uint8_t vdev_id)
{
	bool status = false;
	struct wlan_mlo_sta *sta_ctx;
	struct wlan_cm_connect_resp *rsp;
	struct wlan_objmgr_vdev *vdev;

	if (!psoc)
		return status;

	vdev = wlan_objmgr_get_vdev_by_id_from_psoc(psoc, vdev_id,
						    WLAN_MLME_SB_ID);
	if (!vdev)
		return status;

	if (!vdev->mlo_dev_ctx)
		goto end;

	sta_ctx = vdev->mlo_dev_ctx->sta_ctx;
	if (!sta_ctx || !sta_ctx->copied_reassoc_rsp ||
	    !sta_ctx->copied_reassoc_rsp->roaming_info)
		goto end;

	rsp = sta_ctx->copied_reassoc_rsp;
	if (rsp->roaming_info->auth_status == ROAM_AUTH_STATUS_CONNECTED)
		status = true;

end:
	wlan_objmgr_vdev_release_ref(vdev, WLAN_MLME_SB_ID);
	return status;
}

QDF_STATUS
mlo_roam_link_connect_notify(struct wlan_objmgr_psoc *psoc, uint8_t vdev_id)
{
	struct wlan_mlo_sta *sta_ctx = NULL;
	struct wlan_cm_connect_resp *rsp;
	struct wlan_objmgr_vdev *assoc_vdev;
	struct wlan_objmgr_vdev *link_vdev = NULL;
	struct wlan_objmgr_vdev *vdev;
	struct mlo_partner_info partner_info;
	QDF_STATUS status = QDF_STATUS_SUCCESS;
	uint8_t i;
	uint8_t assoc_vdev_id;
	uint8_t link_vdev_id;

	if (!psoc)
		return QDF_STATUS_E_NULL_VALUE;

	vdev = wlan_objmgr_get_vdev_by_id_from_psoc(psoc, vdev_id,
						    WLAN_MLME_SB_ID);
	if (!vdev)
		return QDF_STATUS_E_NULL_VALUE;

	if (!vdev->mlo_dev_ctx) {
		mlo_err("mlo dev ctx is null");
		status = QDF_STATUS_E_FAILURE;
		goto err;
	}

	sta_ctx = vdev->mlo_dev_ctx->sta_ctx;
	if (!wlan_vdev_mlme_is_mlo_vdev(vdev)) {
		mlo_debug("MLO_ROAM: Ignore if not mlo vdev");
		status = QDF_STATUS_E_FAILURE;
		goto err;
	}

	assoc_vdev = wlan_mlo_get_assoc_link_vdev(vdev);
	if (!assoc_vdev) {
		status =  QDF_STATUS_E_NULL_VALUE;
		goto err;
	}

	assoc_vdev_id = wlan_vdev_get_id(assoc_vdev);
	if (!sta_ctx || !sta_ctx->copied_reassoc_rsp) {
		status = QDF_STATUS_E_NULL_VALUE;
		goto err;
	}

	rsp = sta_ctx->copied_reassoc_rsp;
	partner_info = rsp->ml_parnter_info;
	mlo_debug("partner links %d", partner_info.num_partner_links);

	for (i = 0; i < partner_info.num_partner_links; i++) {
		link_vdev_id = partner_info.partner_link_info[i].vdev_id;
		if (assoc_vdev_id == link_vdev_id)
			continue;
		link_vdev = wlan_objmgr_get_vdev_by_id_from_psoc(psoc,
								 link_vdev_id,
								 WLAN_MLME_SB_ID);
		if (!link_vdev) {
			mlo_err("Link vdev is null");
			status = QDF_STATUS_E_NULL_VALUE;
			goto err;
		}

		if (mlo_check_connect_req_bmap(link_vdev)) {
			mlo_update_connect_req_links(link_vdev, false);
			status = mlo_roam_prepare_and_send_link_connect_req(assoc_vdev,
							link_vdev,
							rsp,
							&partner_info.partner_link_info[i].link_addr,
							partner_info.partner_link_info[i].chan_freq);
			if (QDF_IS_STATUS_ERROR(status))
				goto err;
			else
				goto end;
		}
	}
err:
	if (link_vdev)
		mlo_clear_connect_req_links_bmap(link_vdev);
	if (sta_ctx && sta_ctx->copied_reassoc_rsp) {
		mlo_roam_free_connect_rsp(sta_ctx->copied_reassoc_rsp);
		sta_ctx->copied_reassoc_rsp = NULL;
	}
end:
	if (link_vdev)
		wlan_objmgr_vdev_release_ref(link_vdev, WLAN_MLME_SB_ID);
	wlan_objmgr_vdev_release_ref(vdev, WLAN_MLME_SB_ID);
	return status;
}
