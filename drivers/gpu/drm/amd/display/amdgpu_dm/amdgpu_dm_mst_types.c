/*
 * Copyright 2012-15 Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 * Authors: AMD
 *
 */

#include <linux/version.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_dp_mst_helper.h>
#include "dm_services.h"
#include "amdgpu.h"
#include "amdgpu_dm.h"
#include "amdgpu_dm_mst_types.h"

#include "dc.h"
#include "dm_helpers.h"

#include "dc_link_ddc.h"

#include "i2caux_interface.h"
#if defined(CONFIG_DEBUG_FS)
#include "amdgpu_dm_debugfs.h"
#endif

#ifdef CONFIG_DRM_AMD_DC_DSC_SUPPORT
#if defined(CONFIG_DRM_AMD_DC_DCN1_0)
#include "dc/dcn20/dcn20_resource.h"
#endif
#endif

static ssize_t dm_dp_aux_transfer(struct drm_dp_aux *aux,
				  struct drm_dp_aux_msg *msg)
{
	ssize_t result = 0;
	struct aux_payload payload;
	enum aux_channel_operation_result operation_result;

	if (WARN_ON(msg->size > 16))
		return -E2BIG;

	payload.address = msg->address;
	payload.data = msg->buffer;
	payload.length = msg->size;
	payload.reply = &msg->reply;
	payload.i2c_over_aux = (msg->request & DP_AUX_NATIVE_WRITE) == 0;
	payload.write = (msg->request & DP_AUX_I2C_READ) == 0;
	payload.mot = (msg->request & DP_AUX_I2C_MOT) != 0;
	payload.defer_delay = 0;

	result = dc_link_aux_transfer_raw(TO_DM_AUX(aux)->ddc_service, &payload,
				      &operation_result);

	if (payload.write)
		result = msg->size;

	if (result < 0)
		switch (operation_result) {
		case AUX_CHANNEL_OPERATION_SUCCEEDED:
			break;
		case AUX_CHANNEL_OPERATION_FAILED_HPD_DISCON:
		case AUX_CHANNEL_OPERATION_FAILED_REASON_UNKNOWN:
			result = -EIO;
			break;
		case AUX_CHANNEL_OPERATION_FAILED_INVALID_REPLY:
		case AUX_CHANNEL_OPERATION_FAILED_ENGINE_ACQUIRE:
			result = -EBUSY;
			break;
		case AUX_CHANNEL_OPERATION_FAILED_TIMEOUT:
			result = -ETIMEDOUT;
			break;
		}

	return result;
}
#ifndef HAVE_DRM_DP_MST_DETECT_PORT_PPPP
static enum drm_connector_status
dm_dp_mst_detect(struct drm_connector *connector, bool force)
{
       struct amdgpu_dm_connector *aconnector = to_amdgpu_dm_connector(connector);
       struct amdgpu_dm_connector *master = aconnector->mst_port;

       enum drm_connector_status status =
               drm_dp_mst_detect_port(
                       connector,
                       &master->mst_mgr,
                       aconnector->port);

       return status;
}
#endif
static void
dm_dp_mst_connector_destroy(struct drm_connector *connector)
{
	struct amdgpu_dm_connector *amdgpu_dm_connector = to_amdgpu_dm_connector(connector);
	struct amdgpu_encoder *amdgpu_encoder = amdgpu_dm_connector->mst_encoder;

	kfree(amdgpu_dm_connector->edid);
	amdgpu_dm_connector->edid = NULL;

	drm_encoder_cleanup(&amdgpu_encoder->base);
	kfree(amdgpu_encoder);
	drm_connector_cleanup(connector);
#if defined(HAVE_DRM_DP_MST_GET_PUT_PORT_MALLOC)
	drm_dp_mst_put_port_malloc(amdgpu_dm_connector->port);
#endif /* HAVE_DRM_DP_MST_GET_PUT_PORT_MALLOC */
	kfree(amdgpu_dm_connector);
}

#if defined(HAVE_DRM_DP_MST_CONNECTOR_LATE_REGISTER)
static int
amdgpu_dm_mst_connector_late_register(struct drm_connector *connector)
{
	struct amdgpu_dm_connector *amdgpu_dm_connector =
		to_amdgpu_dm_connector(connector);
	int r;

	r = drm_dp_mst_connector_late_register(connector,
					       amdgpu_dm_connector->port);
	if (r < 0)
		return r;

#if defined(CONFIG_DEBUG_FS)
	connector_debugfs_init(amdgpu_dm_connector);
#endif

	return 0;
}
#endif /* HAVE_DRM_DP_MST_CONNECTOR_LATE_REGISTER */

#if defined(HAVE_DRM_DP_MST_CONNECTOR_EARLY_UNREGISTER)
static void
amdgpu_dm_mst_connector_early_unregister(struct drm_connector *connector)
{
	struct amdgpu_dm_connector *amdgpu_dm_connector =
		to_amdgpu_dm_connector(connector);
	struct drm_dp_mst_port *port = amdgpu_dm_connector->port;

	drm_dp_mst_connector_early_unregister(connector, port);
}
#endif /* HAVE_DRM_DP_MST_CONNECTOR_EARLY_UNREGISTER */

static const struct drm_connector_funcs dm_dp_mst_connector_funcs = {
/*
 * Need to add support for DRM < 4.14 as DP1.1 does
 * 4.13 DRM uses .set_property hook, while 4.15 doesn't
 */
#ifdef HAVE_DRM_ATOMIC_HELPER_XXX_SET_PROPERTY
	.dpms = drm_atomic_helper_connector_dpms,
	.set_property = drm_atomic_helper_connector_set_property,
#endif
#ifndef HAVE_DRM_DP_MST_DETECT_PORT_PPPP
	.detect = dm_dp_mst_detect,
#endif
	.fill_modes = drm_helper_probe_single_connector_modes,
	.destroy = dm_dp_mst_connector_destroy,
	.reset = amdgpu_dm_connector_funcs_reset,
	.atomic_duplicate_state = amdgpu_dm_connector_atomic_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_connector_destroy_state,
	.atomic_set_property = amdgpu_dm_connector_atomic_set_property,
	.atomic_get_property = amdgpu_dm_connector_atomic_get_property,
#if defined(HAVE_DRM_DP_MST_CONNECTOR_LATE_REGISTER)
	.late_register = amdgpu_dm_mst_connector_late_register,
#endif /* HAVE_DRM_DP_MST_CONNECTOR_LATE_REGISTER */
#if defined(HAVE_DRM_DP_MST_CONNECTOR_EARLY_UNREGISTER)
	.early_unregister = amdgpu_dm_mst_connector_early_unregister,
#endif /* HAVE_DRM_DP_MST_CONNECTOR_EARLY_UNREGISTER */
};

#if defined(CONFIG_DRM_AMD_DC_DCN1_0)
static bool validate_dsc_caps_on_connector(struct amdgpu_dm_connector *aconnector)
{
	struct dc_sink *dc_sink = aconnector->dc_sink;
	struct drm_dp_mst_port *port = aconnector->port;
	u8 dsc_caps[16] = { 0 };

#if defined(HAVE_DRM_DP_MST_DSC_AUX_FOR_PORT)
	aconnector->dsc_aux = drm_dp_mst_dsc_aux_for_port(port);
#endif

	if (!aconnector->dsc_aux)
		return false;

	if (drm_dp_dpcd_read(aconnector->dsc_aux, DP_DSC_SUPPORT, dsc_caps, 16) < 0)
		return false;

#if defined(CONFIG_DRM_AMD_DC_DSC_SUPPORT)
	if (!dc_dsc_parse_dsc_dpcd(aconnector->dc_link->ctx->dc,
				   dsc_caps, NULL,
				   &dc_sink->dsc_caps.dsc_dec_caps))
		return false;
#endif

	return true;
}
#endif

static int dm_dp_mst_get_modes(struct drm_connector *connector)
{
	struct amdgpu_dm_connector *aconnector = to_amdgpu_dm_connector(connector);
	int ret = 0;

	if (!aconnector)
		return drm_add_edid_modes(connector, NULL);

	if (!aconnector->edid) {
		struct edid *edid;
		edid = drm_dp_mst_get_edid(connector, &aconnector->mst_port->mst_mgr, aconnector->port);

		if (!edid) {
			drm_connector_update_edid_property(
				&aconnector->base,
				NULL);
			return ret;
		}

		aconnector->edid = edid;
	}

	if (aconnector->dc_sink && aconnector->dc_sink->sink_signal == SIGNAL_TYPE_VIRTUAL) {
		dc_sink_release(aconnector->dc_sink);
		aconnector->dc_sink = NULL;
	}

	if (!aconnector->dc_sink) {
		struct dc_sink *dc_sink;
		struct dc_sink_init_data init_params = {
				.link = aconnector->dc_link,
				.sink_signal = SIGNAL_TYPE_DISPLAY_PORT_MST };
		dc_sink = dc_link_add_remote_sink(
			aconnector->dc_link,
			(uint8_t *)aconnector->edid,
			(aconnector->edid->extensions + 1) * EDID_LENGTH,
			&init_params);

		dc_sink->priv = aconnector;
		/* dc_link_add_remote_sink returns a new reference */
		aconnector->dc_sink = dc_sink;

		if (aconnector->dc_sink) {
			amdgpu_dm_update_freesync_caps(
					connector, aconnector->edid);

#ifdef CONFIG_DRM_AMD_DC_DSC_SUPPORT
#if defined(CONFIG_DRM_AMD_DC_DCN1_0)
#if defined(HAVE_DRM_DP_MST_DSC_AUX_FOR_PORT)
			if (!validate_dsc_caps_on_connector(aconnector))
				memset(&aconnector->dc_sink->dsc_caps,
				       0, sizeof(aconnector->dc_sink->dsc_caps));
#endif
#endif
#endif
		}
	}

	drm_connector_update_edid_property(
					&aconnector->base, aconnector->edid);

	ret = drm_add_edid_modes(connector, aconnector->edid);

	return ret;
}

static struct drm_encoder *
dm_mst_atomic_best_encoder(struct drm_connector *connector,
			   struct drm_connector_state *connector_state)
{
	return &to_amdgpu_dm_connector(connector)->mst_encoder->base;
}

#ifdef HAVE_DRM_DP_MST_DETECT_PORT_PPPP
static int
dm_dp_mst_detect(struct drm_connector *connector,
		 struct drm_modeset_acquire_ctx *ctx, bool force)
{
	struct amdgpu_dm_connector *aconnector = to_amdgpu_dm_connector(connector);
	struct amdgpu_dm_connector *master = aconnector->mst_port;

	return drm_dp_mst_detect_port(connector, ctx, &master->mst_mgr,
				      aconnector->port);
}
#endif

#if defined(HAVE_DRM_CONNECTOR_HELPER_FUNCS_ATOMIC_CHECK_ARG_DRM_ATOMIC_STATE)
static int dm_dp_mst_atomic_check(struct drm_connector *connector,
				struct drm_atomic_state *state)
{
	struct drm_connector_state *new_conn_state =
			drm_atomic_get_new_connector_state(state, connector);
	struct drm_connector_state *old_conn_state =
			drm_atomic_get_old_connector_state(state, connector);
	struct amdgpu_dm_connector *aconnector = to_amdgpu_dm_connector(connector);
	struct drm_crtc_state *new_crtc_state;
	struct drm_dp_mst_topology_mgr *mst_mgr;
	struct drm_dp_mst_port *mst_port;

	mst_port = aconnector->port;
	mst_mgr = &aconnector->mst_port->mst_mgr;

	if (!old_conn_state->crtc)
		return 0;

	if (new_conn_state->crtc) {
		new_crtc_state = drm_atomic_get_new_crtc_state(state, new_conn_state->crtc);
		if (!new_crtc_state ||
		    !drm_atomic_crtc_needs_modeset(new_crtc_state) ||
		    new_crtc_state->enable)
			return 0;
		}

	return drm_dp_atomic_release_vcpi_slots(state,
						mst_mgr,
						mst_port);
}
#endif

static const struct drm_connector_helper_funcs dm_dp_mst_connector_helper_funcs = {
	.get_modes = dm_dp_mst_get_modes,
	.mode_valid = amdgpu_dm_connector_mode_valid,
	.atomic_best_encoder = dm_mst_atomic_best_encoder,
#ifdef HAVE_DRM_DP_MST_DETECT_PORT_PPPP
	.detect_ctx = dm_dp_mst_detect,
#endif
#if defined(HAVE_DRM_CONNECTOR_HELPER_FUNCS_ATOMIC_CHECK_ARG_DRM_ATOMIC_STATE)
	.atomic_check = dm_dp_mst_atomic_check,
#endif
};

static void amdgpu_dm_encoder_destroy(struct drm_encoder *encoder)
{
	drm_encoder_cleanup(encoder);
	kfree(encoder);
}

static const struct drm_encoder_funcs amdgpu_dm_encoder_funcs = {
	.destroy = amdgpu_dm_encoder_destroy,
};

static struct amdgpu_encoder *
dm_dp_create_fake_mst_encoder(struct amdgpu_dm_connector *connector)
{
	struct drm_device *dev = connector->base.dev;
	struct amdgpu_device *adev = dev->dev_private;
	struct amdgpu_encoder *amdgpu_encoder;
	struct drm_encoder *encoder;

	amdgpu_encoder = kzalloc(sizeof(*amdgpu_encoder), GFP_KERNEL);
	if (!amdgpu_encoder)
		return NULL;

	encoder = &amdgpu_encoder->base;
	encoder->possible_crtcs = amdgpu_dm_get_encoder_crtc_mask(adev);

	drm_encoder_init(
		dev,
		&amdgpu_encoder->base,
		&amdgpu_dm_encoder_funcs,
		DRM_MODE_ENCODER_DPMST,
		NULL);

	drm_encoder_helper_add(encoder, &amdgpu_dm_encoder_helper_funcs);

	return amdgpu_encoder;
}

static struct drm_connector *
dm_dp_add_mst_connector(struct drm_dp_mst_topology_mgr *mgr,
			struct drm_dp_mst_port *port,
			const char *pathprop)
{
	struct amdgpu_dm_connector *master = container_of(mgr, struct amdgpu_dm_connector, mst_mgr);
	struct drm_device *dev = master->base.dev;
	struct amdgpu_device *adev = dev->dev_private;
	struct amdgpu_dm_connector *aconnector;
	struct drm_connector *connector;

#if DRM_VERSION_CODE < DRM_VERSION(4, 7, 0)
	drm_modeset_lock(&dev->mode_config.connection_mutex, NULL);
	list_for_each_entry(connector, &dev->mode_config.connector_list, head) {
		aconnector = to_amdgpu_dm_connector(connector);
		if (aconnector->mst_port == master
				&& !aconnector->port) {
			DRM_INFO("DM_MST: reusing connector: %p [id: %d] [master: %p]\n",
						aconnector, connector->base.id, aconnector->mst_port);
			aconnector->port = port;
			drm_mode_connector_set_path_property(connector, pathprop);
			drm_modeset_unlock(&dev->mode_config.connection_mutex);
			aconnector->mst_connected = true;
			return &aconnector->base;
		}
	}
	drm_modeset_unlock(&dev->mode_config.connection_mutex);
#endif

	aconnector = kzalloc(sizeof(*aconnector), GFP_KERNEL);
	if (!aconnector)
		return NULL;

	connector = &aconnector->base;
	aconnector->port = port;
	aconnector->mst_port = master;

	if (drm_connector_init(
		dev,
		connector,
		&dm_dp_mst_connector_funcs,
		DRM_MODE_CONNECTOR_DisplayPort)) {
		kfree(aconnector);
		return NULL;
	}
	drm_connector_helper_add(connector, &dm_dp_mst_connector_helper_funcs);

	amdgpu_dm_connector_init_helper(
		&adev->dm,
		aconnector,
		DRM_MODE_CONNECTOR_DisplayPort,
		master->dc_link,
		master->connector_id);

	aconnector->mst_encoder = dm_dp_create_fake_mst_encoder(master);
	drm_connector_attach_encoder(&aconnector->base,
				     &aconnector->mst_encoder->base);

#ifdef HAVE_DRM_CONNECTOR_PROPERTY_MAX_BPC
	connector->max_bpc_property = master->base.max_bpc_property;
	if (connector->max_bpc_property)
		drm_connector_attach_max_bpc_property(connector, 8, 16);
#else
	drm_object_attach_property(&aconnector->base.base,
				adev->mode_info.max_bpc_property,
				0);
#endif

#ifdef HAVE_DRM_VRR_SUPPORTED
	connector->vrr_capable_property = master->base.vrr_capable_property;
	if (connector->vrr_capable_property)
		drm_connector_attach_vrr_capable_property(connector);
#else
	drm_object_attach_property(&connector->base,
			adev->mode_info.vrr_capable_property, 0);
#endif

	drm_object_attach_property(
		&connector->base,
		dev->mode_config.path_property,
		0);
	drm_object_attach_property(
		&connector->base,
		dev->mode_config.tile_property,
		0);

	drm_connector_set_path_property(connector, pathprop);

	/*
	 * Initialize connector state before adding the connectror to drm and
	 * framebuffer lists
	 */
	amdgpu_dm_connector_funcs_reset(connector);

	DRM_INFO("DM_MST: added connector: %p [id: %d] [master: %p]\n",
		 aconnector, connector->base.id, aconnector->mst_port);

#if defined(HAVE_DRM_DP_MST_GET_PUT_PORT_MALLOC)
	drm_dp_mst_get_port_malloc(port);
#endif /* HAVE_DRM_DP_MST_GET_PUT_PORT_MALLOC */

	DRM_DEBUG_KMS(":%d\n", connector->base.id);

	return connector;
}

static void dm_dp_destroy_mst_connector(struct drm_dp_mst_topology_mgr *mgr,
					struct drm_connector *connector)
{
#if DRM_VERSION_CODE >= DRM_VERSION(4, 7, 0)
	struct amdgpu_dm_connector *master = container_of(mgr, struct amdgpu_dm_connector, mst_mgr);
	struct drm_device *dev = master->base.dev;
	struct amdgpu_device *adev = dev->dev_private;
#endif
	struct amdgpu_dm_connector *aconnector = to_amdgpu_dm_connector(connector);

	DRM_INFO("DM_MST: Disabling connector: %p [id: %d] [master: %p]\n",
		 aconnector, connector->base.id, aconnector->mst_port);

	if (aconnector->dc_sink) {
		amdgpu_dm_update_freesync_caps(connector, NULL);
		dc_link_remove_remote_sink(aconnector->dc_link,
					   aconnector->dc_sink);
		dc_sink_release(aconnector->dc_sink);
		aconnector->dc_sink = NULL;
		aconnector->dc_link->cur_link_settings.lane_count = 0;
	}
#if defined(HAVE_DRM_CONNECTOR_PUT) || defined(HAVE_FREE_CB_IN_STRUCT_DRM_MODE_OBJECT)
	drm_connector_unregister(connector);
#ifdef HAVE_DRM_FB_HELPER_ADD_REMOVE_CONNECTORS
	if (adev->mode_info.rfbdev)
		drm_fb_helper_remove_one_connector(&adev->mode_info.rfbdev->helper, connector);
#endif
	drm_connector_put(connector);
#endif
}

#if defined(HAVE_DRM_DP_MST_TOPOLOGY_CBS_HOTPLUG)
static void dm_dp_mst_hotplug(struct drm_dp_mst_topology_mgr *mgr)
{
	struct amdgpu_dm_connector *master = container_of(mgr, struct amdgpu_dm_connector, mst_mgr);
	struct drm_device *dev = master->base.dev;

	drm_kms_helper_hotplug_event(dev);
}
#endif

#ifdef HAVE_DRM_DP_MST_TOPOLOGY_CBS_REGISTER_CONNECTOR
static void dm_dp_mst_register_connector(struct drm_connector *connector)
{
	struct drm_device *dev = connector->dev;
	struct amdgpu_device *adev = dev->dev_private;

#if DRM_VERSION_CODE < DRM_VERSION(4, 14, 0)
	drm_modeset_lock_all(dev);
#endif
	if (adev->mode_info.rfbdev)
		drm_fb_helper_add_one_connector(&adev->mode_info.rfbdev->helper, connector);
	else
		DRM_ERROR("adev->mode_info.rfbdev is NULL\n");

#if DRM_VERSION_CODE < DRM_VERSION(4, 14, 0)
	drm_modeset_unlock_all(dev);
#endif

	drm_connector_register(connector);
}
#endif

static const struct drm_dp_mst_topology_cbs dm_mst_cbs = {
	.add_connector = dm_dp_add_mst_connector,
	.destroy_connector = dm_dp_destroy_mst_connector,
#if defined(HAVE_DRM_DP_MST_TOPOLOGY_CBS_HOTPLUG)
	.hotplug = dm_dp_mst_hotplug,
#endif
#ifdef HAVE_DRM_DP_MST_TOPOLOGY_CBS_REGISTER_CONNECTOR
	.register_connector = dm_dp_mst_register_connector
#endif
};

void amdgpu_dm_initialize_dp_connector(struct amdgpu_display_manager *dm,
				       struct amdgpu_dm_connector *aconnector,
				       int link_index)
{
	aconnector->dm_dp_aux.aux.name =
		kasprintf(GFP_KERNEL, "AMDGPU DM aux hw bus %d",
			  link_index);
	aconnector->dm_dp_aux.aux.transfer = dm_dp_aux_transfer;
	aconnector->dm_dp_aux.ddc_service = aconnector->dc_link->ddc;
#if defined(HAVE_DRM_CONNECTOR_FUNCS_REGISTER)
	drm_dp_aux_init(&aconnector->dm_dp_aux.aux);
#endif
	drm_dp_cec_register_connector(&aconnector->dm_dp_aux.aux,
				      &aconnector->base);

	if (aconnector->base.connector_type == DRM_MODE_CONNECTOR_eDP)
		return;

	aconnector->mst_mgr.cbs = &dm_mst_cbs;
	drm_dp_mst_topology_mgr_init(
		&aconnector->mst_mgr,
#if DRM_VERSION_CODE < DRM_VERSION(4, 11, 0)
		dm->adev->dev,
#else
		dm->adev->ddev,
#endif
		&aconnector->dm_dp_aux.aux,
		16,
		4,
		aconnector->connector_id);
}

int dm_mst_get_pbn_divider(struct dc_link *link)
{
	if (!link)
		return 0;

	return dc_link_bandwidth_kbps(link,
			dc_link_get_link_cap(link)) / (8 * 1000 * 54);
}

#if defined(CONFIG_DRM_AMD_DC_DSC_SUPPORT)
#if defined(CONFIG_DRM_AMD_DC_DCN1_0) && defined(HAVE_DRM_DP_MST_ATOMIC_CHECK)
struct dsc_mst_fairness_params {
	struct dc_crtc_timing *timing;
	struct dc_sink *sink;
	struct dc_dsc_bw_range bw_range;
	bool compression_possible;
	struct drm_dp_mst_port *port;
};

struct dsc_mst_fairness_vars {
	int pbn;
	bool dsc_enabled;
	int bpp_x16;
};

static int kbps_to_peak_pbn(int kbps)
{
	u64 peak_kbps = kbps;

	peak_kbps *= 1006;
	peak_kbps = div_u64(peak_kbps, 1000);
	return (int) DIV64_U64_ROUND_UP(peak_kbps * 64, (54 * 8 * 1000));
}

static void set_dsc_configs_from_fairness_vars(struct dsc_mst_fairness_params *params,
		struct dsc_mst_fairness_vars *vars,
		int count)
{
	int i;

	for (i = 0; i < count; i++) {
		memset(&params[i].timing->dsc_cfg, 0, sizeof(params[i].timing->dsc_cfg));
		if (vars[i].dsc_enabled && dc_dsc_compute_config(
					params[i].sink->ctx->dc->res_pool->dscs[0],
					&params[i].sink->dsc_caps.dsc_dec_caps,
					params[i].sink->ctx->dc->debug.dsc_min_slice_height_override,
					0,
					params[i].timing,
					&params[i].timing->dsc_cfg)) {
			params[i].timing->flags.DSC = 1;
			params[i].timing->dsc_cfg.bits_per_pixel = vars[i].bpp_x16;
		} else {
			params[i].timing->flags.DSC = 0;
		}
	}
}

static int bpp_x16_from_pbn(struct dsc_mst_fairness_params param, int pbn)
{
	struct dc_dsc_config dsc_config;
	u64 kbps;

	kbps = div_u64((u64)pbn * 994 * 8 * 54, 64);
	dc_dsc_compute_config(
			param.sink->ctx->dc->res_pool->dscs[0],
			&param.sink->dsc_caps.dsc_dec_caps,
			param.sink->ctx->dc->debug.dsc_min_slice_height_override,
			(int) kbps, param.timing, &dsc_config);

	return dsc_config.bits_per_pixel;
}

static void increase_dsc_bpp(struct drm_atomic_state *state,
			     struct dc_link *dc_link,
			     struct dsc_mst_fairness_params *params,
			     struct dsc_mst_fairness_vars *vars,
			     int count)
{
	int i;
	bool bpp_increased[MAX_PIPES];
	int initial_slack[MAX_PIPES];
	int min_initial_slack;
	int next_index;
	int remaining_to_increase = 0;
	int pbn_per_timeslot;
	int link_timeslots_used;
	int fair_pbn_alloc;

	for (i = 0; i < count; i++) {
		if (vars[i].dsc_enabled) {
			initial_slack[i] = kbps_to_peak_pbn(params[i].bw_range.max_kbps) - vars[i].pbn;
			bpp_increased[i] = false;
			remaining_to_increase += 1;
		} else {
			initial_slack[i] = 0;
			bpp_increased[i] = true;
		}
	}

	pbn_per_timeslot = dc_link_bandwidth_kbps(dc_link,
			dc_link_get_link_cap(dc_link)) / (8 * 1000 * 54);

	while (remaining_to_increase) {
		next_index = -1;
		min_initial_slack = -1;
		for (i = 0; i < count; i++) {
			if (!bpp_increased[i]) {
				if (min_initial_slack == -1 || min_initial_slack > initial_slack[i]) {
					min_initial_slack = initial_slack[i];
					next_index = i;
				}
			}
		}

		if (next_index == -1)
			break;

		link_timeslots_used = 0;

		for (i = 0; i < count; i++)
			link_timeslots_used += DIV_ROUND_UP(vars[i].pbn, pbn_per_timeslot);

		fair_pbn_alloc = (63 - link_timeslots_used) / remaining_to_increase * pbn_per_timeslot;

		if (initial_slack[next_index] > fair_pbn_alloc) {
			vars[next_index].pbn += fair_pbn_alloc;
			if (drm_dp_atomic_find_vcpi_slots(state,
							  params[next_index].port->mgr,
							  params[next_index].port,
							  vars[next_index].pbn,
							  dm_mst_get_pbn_divider(dc_link)) < 0)
				return;
			if (!drm_dp_mst_atomic_check(state)) {
				vars[next_index].bpp_x16 = bpp_x16_from_pbn(params[next_index], vars[next_index].pbn);
			} else {
				vars[next_index].pbn -= fair_pbn_alloc;
				if (drm_dp_atomic_find_vcpi_slots(state,
								  params[next_index].port->mgr,
								  params[next_index].port,
								  vars[next_index].pbn,
								  dm_mst_get_pbn_divider(dc_link)) < 0)
					return;
			}
		} else {
			vars[next_index].pbn += initial_slack[next_index];
			if (drm_dp_atomic_find_vcpi_slots(state,
							  params[next_index].port->mgr,
							  params[next_index].port,
							  vars[next_index].pbn,
							  dm_mst_get_pbn_divider(dc_link)) < 0)
				return;
			if (!drm_dp_mst_atomic_check(state)) {
				vars[next_index].bpp_x16 = params[next_index].bw_range.max_target_bpp_x16;
			} else {
				vars[next_index].pbn -= initial_slack[next_index];
				if (drm_dp_atomic_find_vcpi_slots(state,
								  params[next_index].port->mgr,
								  params[next_index].port,
								  vars[next_index].pbn,
								  dm_mst_get_pbn_divider(dc_link)) < 0)
					return;
			}
		}

		bpp_increased[next_index] = true;
		remaining_to_increase--;
	}
}

static void try_disable_dsc(struct drm_atomic_state *state,
			    struct dc_link *dc_link,
			    struct dsc_mst_fairness_params *params,
			    struct dsc_mst_fairness_vars *vars,
			    int count)
{
	int i;
	bool tried[MAX_PIPES];
	int kbps_increase[MAX_PIPES];
	int max_kbps_increase;
	int next_index;
	int remaining_to_try = 0;

	for (i = 0; i < count; i++) {
		if (vars[i].dsc_enabled && vars[i].bpp_x16 == params[i].bw_range.max_target_bpp_x16) {
			kbps_increase[i] = params[i].bw_range.stream_kbps - params[i].bw_range.max_kbps;
			tried[i] = false;
			remaining_to_try += 1;
		} else {
			kbps_increase[i] = 0;
			tried[i] = true;
		}
	}

	while (remaining_to_try) {
		next_index = -1;
		max_kbps_increase = -1;
		for (i = 0; i < count; i++) {
			if (!tried[i]) {
				if (max_kbps_increase == -1 || max_kbps_increase < kbps_increase[i]) {
					max_kbps_increase = kbps_increase[i];
					next_index = i;
				}
			}
		}

		if (next_index == -1)
			break;

		vars[next_index].pbn = kbps_to_peak_pbn(params[next_index].bw_range.stream_kbps);
		if (drm_dp_atomic_find_vcpi_slots(state,
						  params[next_index].port->mgr,
						  params[next_index].port,
						  vars[next_index].pbn,
						  0) < 0)
			return;

		if (!drm_dp_mst_atomic_check(state)) {
			vars[next_index].dsc_enabled = false;
			vars[next_index].bpp_x16 = 0;
		} else {
			vars[next_index].pbn = kbps_to_peak_pbn(params[next_index].bw_range.max_kbps);
			if (drm_dp_atomic_find_vcpi_slots(state,
							  params[next_index].port->mgr,
							  params[next_index].port,
							  vars[next_index].pbn,
							  dm_mst_get_pbn_divider(dc_link)) < 0)
				return;
		}

		tried[next_index] = true;
		remaining_to_try--;
	}
}

static bool compute_mst_dsc_configs_for_link(struct drm_atomic_state *state,
					     struct dc_state *dc_state,
					     struct dc_link *dc_link)
{
	int i;
	struct dc_stream_state *stream;
	struct dsc_mst_fairness_params params[MAX_PIPES];
	struct dsc_mst_fairness_vars vars[MAX_PIPES];
	struct amdgpu_dm_connector *aconnector;
	int count = 0;

	memset(params, 0, sizeof(params));

	/* Set up params */
	for (i = 0; i < dc_state->stream_count; i++) {
		struct dc_dsc_policy dsc_policy = {0};

		stream = dc_state->streams[i];

		if (stream->link != dc_link)
			continue;

		stream->timing.flags.DSC = 0;

		params[count].timing = &stream->timing;
		params[count].sink = stream->sink;
		aconnector = (struct amdgpu_dm_connector *)stream->dm_stream_context;
		params[count].port = aconnector->port;
		params[count].compression_possible = stream->sink->dsc_caps.dsc_dec_caps.is_dsc_supported;
		dc_dsc_get_policy_for_timing(params[count].timing, &dsc_policy);
		if (!dc_dsc_compute_bandwidth_range(
				stream->sink->ctx->dc->res_pool->dscs[0],
				stream->sink->ctx->dc->debug.dsc_min_slice_height_override,
				dsc_policy.min_target_bpp,
				dsc_policy.max_target_bpp,
				&stream->sink->dsc_caps.dsc_dec_caps,
				&stream->timing, &params[count].bw_range))
			params[count].bw_range.stream_kbps = dc_bandwidth_in_kbps_from_timing(&stream->timing);

		count++;
	}
	/* Try no compression */
	for (i = 0; i < count; i++) {
		vars[i].pbn = kbps_to_peak_pbn(params[i].bw_range.stream_kbps);
		vars[i].dsc_enabled = false;
		vars[i].bpp_x16 = 0;
		if (drm_dp_atomic_find_vcpi_slots(state,
						 params[i].port->mgr,
						 params[i].port,
						 vars[i].pbn,
						 0) < 0)
			return false;
	}
	if (!drm_dp_mst_atomic_check(state)) {
		set_dsc_configs_from_fairness_vars(params, vars, count);
		return true;
	}

	/* Try max compression */
	for (i = 0; i < count; i++) {
		if (params[i].compression_possible) {
			vars[i].pbn = kbps_to_peak_pbn(params[i].bw_range.min_kbps);
			vars[i].dsc_enabled = true;
			vars[i].bpp_x16 = params[i].bw_range.min_target_bpp_x16;
			if (drm_dp_atomic_find_vcpi_slots(state,
							  params[i].port->mgr,
							  params[i].port,
							  vars[i].pbn,
							  dm_mst_get_pbn_divider(dc_link)) < 0)
				return false;
		} else {
			vars[i].pbn = kbps_to_peak_pbn(params[i].bw_range.stream_kbps);
			vars[i].dsc_enabled = false;
			vars[i].bpp_x16 = 0;
			if (drm_dp_atomic_find_vcpi_slots(state,
							  params[i].port->mgr,
							  params[i].port,
							  vars[i].pbn,
							  0) < 0)
				return false;
		}
	}
	if (drm_dp_mst_atomic_check(state))
		return false;

	/* Optimize degree of compression */
	increase_dsc_bpp(state, dc_link, params, vars, count);

	try_disable_dsc(state, dc_link, params, vars, count);

	set_dsc_configs_from_fairness_vars(params, vars, count);

	return true;
}

bool compute_mst_dsc_configs_for_state(struct drm_atomic_state *state,
				       struct dc_state *dc_state)
{
	int i, j;
	struct dc_stream_state *stream;
	bool computed_streams[MAX_PIPES];
	struct amdgpu_dm_connector *aconnector;

	for (i = 0; i < dc_state->stream_count; i++)
		computed_streams[i] = false;

	for (i = 0; i < dc_state->stream_count; i++) {
		stream = dc_state->streams[i];

		if (stream->signal != SIGNAL_TYPE_DISPLAY_PORT_MST)
			continue;

		aconnector = (struct amdgpu_dm_connector *)stream->dm_stream_context;

		if (!aconnector || !aconnector->dc_sink)
			continue;

		if (!aconnector->dc_sink->dsc_caps.dsc_dec_caps.is_dsc_supported)
			continue;

		if (computed_streams[i])
			continue;

		mutex_lock(&aconnector->mst_mgr.lock);
		if (!compute_mst_dsc_configs_for_link(state, dc_state, stream->link)) {
			mutex_unlock(&aconnector->mst_mgr.lock);
			return false;
		}
		mutex_unlock(&aconnector->mst_mgr.lock);

		for (j = 0; j < dc_state->stream_count; j++) {
			if (dc_state->streams[j]->link == stream->link)
				computed_streams[j] = true;
		}
	}

	for (i = 0; i < dc_state->stream_count; i++) {
		stream = dc_state->streams[i];

		if (stream->timing.flags.DSC == 1)
			dcn20_add_dsc_to_stream_resource(stream->ctx->dc, dc_state, stream);
	}

	return true;
}

#endif
#endif
