/*
 * Copyright © 2009 Keith Packard
 *
 * Permission to use, copy, modify, distribute, and sell this software and its
 * documentation for any purpose is hereby granted without fee, provided that
 * the above copyright notice appear in all copies and that both that copyright
 * notice and this permission notice appear in supporting documentation, and
 * that the name of the copyright holders not be used in advertising or
 * publicity pertaining to distribution of the software without specific,
 * written prior permission.  The copyright holders make no representations
 * about the suitability of this software for any purpose.  It is provided "as
 * is" without express or implied warranty.
 *
 * THE COPYRIGHT HOLDERS DISCLAIM ALL WARRANTIES WITH REGARD TO THIS SOFTWARE,
 * INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS, IN NO
 * EVENT SHALL THE COPYRIGHT HOLDERS BE LIABLE FOR ANY SPECIAL, INDIRECT OR
 * CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE,
 * DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER
 * TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE
 * OF THIS SOFTWARE.
 */

#include <kcl/kcl_drm_dp_helper.h>
#include <kcl/kcl_drm_print.h>
#include <drm/display/drm_dp_mst_helper.h>

#ifndef HAVE_DRM_DP_READ_DPCD_CAPS
static int _kcl_drm_dp_read_extended_dpcd_caps(struct drm_dp_aux *aux,
					      u8 dpcd[DP_RECEIVER_CAP_SIZE])
{
	u8 dpcd_ext[DP_RECEIVER_CAP_SIZE];
	int ret;
	struct drm_device *drm_dev = NULL;

	if (aux) {
		struct drm_dp_mst_topology_mgr *mgr =
			container_of(&aux, struct drm_dp_mst_topology_mgr, aux);
		drm_dev = mgr->dev;
	}
	/*
	 * Prior to DP1.3 the bit represented by
	 * DP_EXTENDED_RECEIVER_CAP_FIELD_PRESENT was reserved.
	 * If it is set DP_DPCD_REV at 0000h could be at a value less than
	 * the true capability of the panel. The only way to check is to
	 * then compare 0000h and 2200h.
	 */
	if (!(dpcd[DP_TRAINING_AUX_RD_INTERVAL] &
	      DP_EXTENDED_RECEIVER_CAP_FIELD_PRESENT))
		return 0;

	ret = drm_dp_dpcd_read(aux, DP_DP13_DPCD_REV, &dpcd_ext,
			       sizeof(dpcd_ext));
	if (ret < 0)
		return ret;
	if (ret != sizeof(dpcd_ext))
		return -EIO;

	if (dpcd[DP_DPCD_REV] > dpcd_ext[DP_DPCD_REV]) {
		drm_dbg_kms(
			drm_dev,
			"%s: Extended DPCD rev less than base DPCD rev (%d > %d)\n",
			aux->name, dpcd[DP_DPCD_REV], dpcd_ext[DP_DPCD_REV]);
		return 0;
	}

	if (!memcmp(dpcd, dpcd_ext, sizeof(dpcd_ext)))
		return 0;

	drm_dbg_kms(drm_dev, "%s: Base DPCD: %*ph\n", aux->name,
		    DP_RECEIVER_CAP_SIZE, dpcd);

	memcpy(dpcd, dpcd_ext, sizeof(dpcd_ext));

	return 0;
}

/**
 * drm_dp_read_dpcd_caps() - read DPCD caps and extended DPCD caps if
 * available
 * @aux: DisplayPort AUX channel
 * @dpcd: Buffer to store the resulting DPCD in
 *
 * Attempts to read the base DPCD caps for @aux. Additionally, this function
 * checks for and reads the extended DPRX caps (%DP_DP13_DPCD_REV) if
 * present.
 *
 * Returns: %0 if the DPCD was read successfully, negative error code
 * otherwise.
 */
int _kcl_drm_dp_read_dpcd_caps(struct drm_dp_aux *aux,
			       u8 dpcd[DP_RECEIVER_CAP_SIZE])
{
	int ret;
	struct drm_device *drm_dev = NULL;

	if (aux) {
		struct drm_dp_mst_topology_mgr *mgr =
			container_of(&aux, struct drm_dp_mst_topology_mgr, aux);
		drm_dev = mgr->dev;
	}

	ret = drm_dp_dpcd_read(aux, DP_DPCD_REV, dpcd, DP_RECEIVER_CAP_SIZE);
	if (ret < 0)
		return ret;
	if (ret != DP_RECEIVER_CAP_SIZE || dpcd[DP_DPCD_REV] == 0)
		return -EIO;

	ret = _kcl_drm_dp_read_extended_dpcd_caps(aux, dpcd);
	if (ret < 0)
		return ret;

	drm_dbg_kms(drm_dev, "%s: DPCD: %*ph\n", aux->name,
		    DP_RECEIVER_CAP_SIZE, dpcd);

	return ret;
}
EXPORT_SYMBOL(_kcl_drm_dp_read_dpcd_caps);
#endif