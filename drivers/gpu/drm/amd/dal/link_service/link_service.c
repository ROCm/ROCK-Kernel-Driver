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

#include "dal_services.h"
#include "link_service.h"
#include "include/hw_sequencer_interface.h"
#include "include/dpcd_access_service_interface.h"
#include "include/display_path_interface.h"
#include "dpsst_link_service.h"


static bool validate_mode_timing(struct link_service *ls,
			uint32_t display_index,
			const struct hw_crtc_timing *timing,
			struct link_validation_flags flags)
{
	return true;
}

static bool enable_stream(struct link_service *ls,
		uint32_t display_index,
		struct hw_path_mode *path_mode)
{
	enum signal_type signal;

	ASSERT(ls->strm_state == STREAM_STATE_DISABLED ||
		ls->strm_state == STREAM_STATE_OPTIMIZED_READY ||
		ls->strm_state == STREAM_STATE_POWER_SAVE);

	if (ls->strm_state == STREAM_STATE_ACTIVE ||
		ls->strm_state == STREAM_STATE_ENABLED)
		return true;

	if (!ls->link_st.FAKE_CONNECTED) {
		dal_ls_try_enable_stream(ls, path_mode, NULL);
		dal_ls_try_enable_link_base(ls, path_mode, NULL);
	}

	dal_ls_update_stream_features(
		ls,
		path_mode);

	if (ls->strm_state == STREAM_STATE_OPTIMIZED_READY)
		ls->strm_state = STREAM_STATE_ACTIVE;
	else
		ls->strm_state = STREAM_STATE_ENABLED;


	signal = dal_display_path_get_config_signal(
				path_mode->display_path, ls->link_idx);

	if (path_mode->mode.timing.pixel_clock <= TMDS_MAX_PIXEL_CLOCK_IN_KHZ) {
		if (signal == SIGNAL_TYPE_DVI_DUAL_LINK &&
			path_mode->mode.timing.flags.COLOR_DEPTH <
							HW_COLOR_DEPTH_101010) {
			signal = SIGNAL_TYPE_DVI_SINGLE_LINK;
		}
	}

	ls->cur_link_setting.link_rate = LINK_RATE_UNKNOWN;
	ls->cur_link_setting.link_spread = LINK_SPREAD_DISABLED;

	if (signal == SIGNAL_TYPE_DVI_DUAL_LINK)
		ls->cur_link_setting.lane_count = LANE_COUNT_EIGHT;
	else
		ls->cur_link_setting.lane_count = LANE_COUNT_FOUR;


	dal_display_path_set_target_powered_on(
			path_mode->display_path,
			DISPLAY_TRI_STATE_TRUE);

	return true;
}

static bool optimized_enable_stream(struct link_service *ls,
				uint32_t display_index,
				struct display_path *display_path)
{
	ls->cur_link_setting.link_rate = LINK_RATE_HIGH;
	ls->cur_link_setting.link_spread = LINK_SPREAD_DISABLED;
	ls->cur_link_setting.lane_count = LANE_COUNT_FOUR;

	ls->strm_state = STREAM_STATE_OPTIMIZED_READY;

	dal_display_path_set_target_powered_on(display_path,
						DISPLAY_TRI_STATE_TRUE);
	dal_display_path_set_target_blanked(display_path,
						DISPLAY_TRI_STATE_FALSE);

	return true;
}

static void program_drr(
	struct link_service *ls,
	const struct hw_path_mode *path_mode)
{
	dal_hw_sequencer_program_drr(ls->hwss, path_mode);
}

static void update_stream_features(
	struct link_service *ls,
	const struct hw_path_mode *path_mode)
{
	program_drr(ls, path_mode);
}

static void set_link_psr_capabilities(
		struct link_service *ls,
		struct psr_caps *psr_caps)
{
	/*Do nothing. PSR is only supported on DP link.*/
}

static void get_link_psr_capabilities(
		struct link_service *ls,
		struct psr_caps *psr_caps)
{
	/*PSR is only supported on DP link.
	 * Report not supported by setting version field to 0.
	 * Typically, this function should not be called on non-DP link.*/
	psr_caps->psr_version = 0;
}

bool dal_link_service_blank_stream(struct link_service *ls,
				uint32_t display_index,
				struct hw_path_mode *path_mode)
{
	struct blank_stream_param blank_param = {0};

	ASSERT(ls->strm_state == STREAM_STATE_ACTIVE ||
			ls->strm_state == STREAM_STATE_OPTIMIZED_READY ||
			ls->strm_state == STREAM_STATE_POWER_SAVE  ||
			ls->strm_state == STREAM_STATE_DISABLED);

	if (ls->strm_state != STREAM_STATE_ACTIVE &&
		ls->strm_state != STREAM_STATE_OPTIMIZED_READY) {
		return true;
	}
	blank_param.link_idx = ls->link_idx;
	blank_param.display_path = path_mode->display_path;
	dal_hw_sequencer_blank_stream(ls->hwss, &blank_param);

	ls->strm_state = STREAM_STATE_ENABLED;

	dal_display_path_set_target_blanked(
					path_mode->display_path,
					DISPLAY_TRI_STATE_TRUE);

	return true;

}

bool dal_link_service_unblank_stream(struct link_service *ls,
			uint32_t display_index,
			struct hw_path_mode *path_mode)
{
	struct blank_stream_param blank_param = {0};

	ASSERT(ls->strm_state == STREAM_STATE_ENABLED ||
			ls->strm_state == STREAM_STATE_OPTIMIZED_READY ||
			ls->strm_state == STREAM_STATE_ACTIVE);

	if (ls->strm_state == STREAM_STATE_ACTIVE ||
			ls->strm_state == STREAM_STATE_OPTIMIZED_READY)
		return true;

	if (!ls->link_st.FAKE_CONNECTED) {
		blank_param.link_idx = ls->link_idx;
		blank_param.display_path = path_mode->display_path;
		blank_param.timing = path_mode->mode.timing;
		dal_ls_get_current_link_setting(
			ls, &blank_param.link_settings);
		dal_hw_sequencer_unblank_stream(ls->hwss, &blank_param);
	}

	ls->strm_state = STREAM_STATE_ACTIVE;

	dal_display_path_set_target_blanked(path_mode->display_path,
				DISPLAY_TRI_STATE_FALSE);

	return true;

}

bool dal_link_service_pre_mode_change(struct link_service *ls,
			uint32_t display_index,
			struct hw_path_mode *path_mode)
{
	ASSERT(ls->strm_state == STREAM_STATE_ENABLED ||
			ls->strm_state == STREAM_STATE_OPTIMIZED_READY ||
			ls->strm_state == STREAM_STATE_POWER_SAVE);

	if (ls->strm_state == STREAM_STATE_DISABLED ||
			ls->strm_state == STREAM_STATE_POWER_SAVE)
		return true;

	ls->funcs->disable_stream(ls, display_index, path_mode);

	ls->strm_state = STREAM_STATE_MODE_CHANGE_SAFE;

	return true;
}

bool dal_link_service_post_mode_change(struct link_service *ls,
			uint32_t display_index,
			struct hw_path_mode *path_mode)
{
	ASSERT(ls->strm_state == STREAM_STATE_MODE_CHANGE_SAFE);

	ls->strm_state = STREAM_STATE_DISABLED;
	ls->funcs->enable_stream(ls, display_index, path_mode);

	return true;
}

bool dal_link_service_power_on_stream(struct link_service *ls,
			uint32_t display_index,
			struct hw_path_mode *path_mode)
{
	ASSERT(ls->strm_state == STREAM_STATE_POWER_SAVE ||
			ls->strm_state == STREAM_STATE_OPTIMIZED_READY ||
			ls->strm_state == STREAM_STATE_ACTIVE);

	if (ls->strm_state == STREAM_STATE_ACTIVE ||
			ls->strm_state == STREAM_STATE_ENABLED)
		return true;

	if (ls->strm_state == STREAM_STATE_POWER_SAVE)
		ls->strm_state = STREAM_STATE_DISABLED;

	ls->funcs->enable_stream(ls, display_index, path_mode);
	return true;
}

bool dal_link_service_power_off_stream(struct link_service *ls,
			uint32_t display_index,
			struct hw_path_mode *path_mode)
{
	ASSERT(ls->strm_state == STREAM_STATE_ENABLED ||
			ls->strm_state == STREAM_STATE_OPTIMIZED_READY);

	ls->funcs->disable_stream(ls, display_index, path_mode);

	ls->strm_state = STREAM_STATE_POWER_SAVE;

	return true;

}

static void connect_link(struct link_service *ls,
			const struct display_path *display_path,
			bool initial_detection)
{
	/*TO DO: these flags are important for MSTMGR,
	 * this function will probably be called
	 * from mstmgr:: connectlink*/
	ls->link_st.CONNECTED = 1;
	ls->link_st.INVALIDATED = 0;
}

static void disconnect_link(struct link_service *ls)
{
	/* TO DO: these flags are important for MSTMGR,
	 * this function will probably be called from
	 * mstmgr:: connectlink*/
	ls->link_st.CONNECTED = 0;
}

static bool associate_link(struct link_service *ls,
			uint32_t display_index,
			uint32_t link_idx,
			bool is_internal_link)
{
	ls->link_idx = link_idx;
	ls->link_notification_display_index = display_index;
	ls->link_prop.INTERNAL = is_internal_link;

	return true;
}

uint32_t dal_link_service_get_notification_display_idx(
		struct link_service *ls)
{
	return ls->link_notification_display_index;
}

void dal_link_service_invalidate_downstream_devices(
		struct link_service *ls)
{
	ls->link_st.INVALIDATED = 1;
}

static void release_hw(struct link_service *ls)
{

}

bool dal_link_service_get_mst_sink_info(
		struct link_service *ls,
		uint32_t display_index,
		struct mst_sink_info *sink_info)
{
	return false;
}

bool dal_link_service_is_mst_network_present(
		struct link_service *ls)
{
	return false;
}

bool dal_link_service_are_mst_displays_cofunctional(
				struct link_service *ls,
				const uint32_t *array_display_index,
				uint32_t len)
{
	return true;
}

bool dal_link_service_is_sink_present_at_display_index(
				struct link_service *ls,
				uint32_t display_index)
{
	return false;
}

struct ddc_service *dal_link_service_obtain_mst_ddc_service(
				struct link_service *ls,
				uint32_t display_index)
{
	return NULL;
}

void dal_link_service_release_mst_ddc_service(
				struct link_service *ls,
				struct ddc_service *ddc_service)
{

}

static bool get_gtc_sync_status(
				struct link_service *ls)
{
	return false;
}

static bool is_stream_drr_supported(struct link_service *ls)
{
	/* For eDP/DP there is a requirement that Sink reports it is able to
	 * render video frame without valid MSA params. For other display types
	 * there is no such requirement, so return true for those cases.*/
	return true;
}

static bool is_link_psr_supported(struct link_service *ls)
{
	/* Only eDP panels support PSR currently. Should return false for all
	 * other sink types. */
	return false;
}

static void destroy(struct link_service **ls)
{
	dal_free(*ls);
	*ls = NULL;

}

static const struct link_service_funcs funcs = {
	.destroy = destroy,
	.validate_mode_timing = validate_mode_timing,
	.get_gtc_sync_status = get_gtc_sync_status,
	.enable_stream = enable_stream,
	.disable_stream = dal_ls_disable_stream_base,
	.optimized_enable_stream = optimized_enable_stream,
	.update_stream_features = update_stream_features,
	.blank_stream = dal_link_service_blank_stream,
	.unblank_stream = dal_link_service_unblank_stream,
	.pre_mode_change = dal_link_service_pre_mode_change,
	.post_mode_change = dal_link_service_post_mode_change,
	.power_on_stream = dal_link_service_power_on_stream,
	.power_off_stream = dal_link_service_power_off_stream,
	.connect_link = connect_link,
	.disconnect_link = disconnect_link,
	.associate_link = associate_link,
	.get_notification_display_idx =
		dal_link_service_get_notification_display_idx,
	.invalidate_downstream_devices =
		dal_link_service_invalidate_downstream_devices,
	.are_mst_displays_cofunctional =
		dal_link_service_are_mst_displays_cofunctional,
	.get_mst_sink_info =
		dal_link_service_get_mst_sink_info,
	.is_mst_network_present = dal_link_service_is_mst_network_present,
	.is_sink_present_at_display_index =
		dal_link_service_is_sink_present_at_display_index,
	.is_link_psr_supported = is_link_psr_supported,
	.is_stream_drr_supported = is_stream_drr_supported,
	.set_link_psr_capabilities =
		set_link_psr_capabilities,
	.get_link_psr_capabilities =
		get_link_psr_capabilities,
	.obtain_mst_ddc_service =
		dal_link_service_obtain_mst_ddc_service,
	.release_mst_ddc_service =
		dal_link_service_release_mst_ddc_service,
	.release_hw = release_hw,
};

bool dal_link_service_construct(struct link_service *link_service,
			struct link_service_init_data *init_data)
{
	link_service->hwss = init_data->hwss;
	link_service->link_srv_type = init_data->link_type;
	link_service->adapter_service = init_data->adapter_service;
	link_service->connector_id = init_data->connector_id;
	link_service->connector_enum_id = init_data->connector_enum_id;
	link_service->dal_context = init_data->dal_context;
	link_service->tm = init_data->tm;
	link_service->funcs = &funcs;
	return true;
}

static struct link_service *create_legacy_link_service(
				struct link_service_init_data *init_data)
{
	struct link_service *link_service = NULL;

	link_service = dal_alloc(sizeof(struct link_service));

	if (link_service == NULL)
		return NULL;

	if (dal_link_service_construct(link_service, init_data))
		return link_service;

	dal_free(link_service);
	return NULL;
}


struct link_service *dal_link_service_create(
		struct link_service_init_data *init_data)
{
	struct link_service *link_service = NULL;

	switch (init_data->link_type) {

	case LINK_SERVICE_TYPE_LEGACY:
		link_service = create_legacy_link_service(init_data);
		break;

	case LINK_SERVICE_TYPE_DP_SST:
		link_service = dal_dpsst_ls_create(init_data);
		break;

	case LINK_SERVICE_TYPE_DP_MST:
		break;

	default:
		break;

	}

	return link_service;
}

void dal_link_service_destroy(
		struct link_service **ls)
{
	if (ls == NULL || *ls == NULL)
			return;

	(*ls)->funcs->destroy(ls);
}

enum link_service_type dal_ls_get_link_service_type(
	struct link_service *ls)
{
	return ls->link_srv_type;
}

bool dal_ls_validate_mode_timing(
		struct link_service *ls,
		uint32_t display_index,
		const struct hw_crtc_timing *timing,
		struct link_validation_flags flags)
{
	return ls->funcs->validate_mode_timing(
			ls, display_index, timing, flags);
}

void dal_ls_try_enable_stream(
				struct link_service *ls,
				const struct hw_path_mode *path_mode,
				const struct link_settings *link_setting)
{
	struct enable_stream_param stream_param = {0};

	stream_param.display_path = path_mode->display_path;
	stream_param.link_idx = ls->link_idx;
	stream_param.timing = path_mode->mode.timing;
	stream_param.path_mode = path_mode;

	if (link_setting != NULL)
		stream_param.link_settings = *link_setting;

	dal_hw_sequencer_enable_stream(ls->hwss, &stream_param);

}

bool dal_ls_enable_stream(
		struct link_service *ls,
		uint32_t display_index,
		struct hw_path_mode *path_mode)
{
	return ls->funcs->enable_stream(
			ls, display_index, path_mode);
}

bool dal_ls_disable_stream(
		struct link_service *ls,
		uint32_t display_index,
		struct hw_path_mode *path_mode)
{
	return ls->funcs->disable_stream(
			ls, display_index, path_mode);
}

bool dal_ls_optimized_enable_stream(
		struct link_service *ls,
		uint32_t display_index,
		struct display_path *display_path)
{
	return ls->funcs->optimized_enable_stream(
			ls, display_index, display_path);
}

bool dal_ls_blank_stream(
		struct link_service *ls,
		uint32_t display_index,
		struct hw_path_mode *path_mode)
{
	return ls->funcs->blank_stream(
			ls, display_index, path_mode);
}

bool dal_ls_unblank_stream(
		struct link_service *ls,
		uint32_t display_index,
		struct hw_path_mode *path_mode)
{
	return ls->funcs->unblank_stream(
			ls, display_index, path_mode);
}

bool dal_ls_pre_mode_change(
		struct link_service *ls,
		uint32_t display_index,
		struct hw_path_mode *path_mode)
{
	return ls->funcs->pre_mode_change(
			ls, display_index, path_mode);
}

bool dal_ls_post_mode_change(
		struct link_service *ls,
		uint32_t display_index,
		struct hw_path_mode *path_mode)
{
	return ls->funcs->post_mode_change(
			ls, display_index, path_mode);
}

bool dal_ls_power_on_stream(
		struct link_service *ls,
		uint32_t display_index,
		struct hw_path_mode *path_mode)
{
	return ls->funcs->power_on_stream(
			ls, display_index, path_mode);
}

bool dal_ls_power_off_stream(
		struct link_service *ls,
		uint32_t display_index,
		struct hw_path_mode *path_mode)
{
	return ls->funcs->power_off_stream(
			ls, display_index, path_mode);
}

void dal_ls_retrain_link(
		struct link_service *ls,
		struct hw_path_mode_set *path_set)
{
	ls->funcs->retrain_link(ls, path_set);
}

bool dal_ls_try_enable_link_base(
		struct link_service *ls,
		const struct hw_path_mode *path_mode,
		const struct link_settings *link_setting)
{
	enum hwss_result result = HWSS_RESULT_UNKNOWN;
	struct enable_link_param link_param = {0};

	link_param.display_path = path_mode->display_path;
	link_param.link_idx = ls->link_idx;
	link_param.optimized_programming =
			(ls->strm_state == STREAM_STATE_OPTIMIZED_READY);
	link_param.timing = path_mode->mode.timing;
	link_param.path_mode = path_mode;

	if (link_setting != NULL)
		link_param.link_settings = *link_setting;

	result = dal_hw_sequencer_enable_link(ls->hwss, &link_param);
	return (result == HWSS_RESULT_OK);
}

bool dal_ls_get_current_link_setting(
		struct link_service *ls,
		struct link_settings *link_settings)
{
	*link_settings = ls->cur_link_setting;
	return true;
}

void dal_ls_connect_link(
		struct link_service *ls,
		const struct display_path *display_path,
		bool initial_detection)
{
	ls->funcs->connect_link(
			ls, display_path, initial_detection);
}

void dal_ls_disconnect_link(
		struct link_service *ls)
{
	ls->funcs->disconnect_link(ls);
}

void dal_ls_invalidate_down_stream_devices(
		struct link_service *ls)
{
	ls->funcs->invalidate_downstream_devices(ls);
}

void dal_ls_release_hw(
		struct link_service *ls)
{
	ls->funcs->release_hw(ls);
}

bool dal_ls_associate_link(
		struct link_service *ls,
		uint32_t display_index,
		uint32_t link_index,
		bool is_internal_link)
{
	return ls->funcs->associate_link(
			ls, display_index,
			link_index, is_internal_link);

}

bool dal_ls_get_mst_sink_info(
		struct link_service *ls,
		uint32_t display_index,
		struct mst_sink_info *sink_info)
{
	return ls->funcs->get_mst_sink_info(
			ls, display_index, sink_info);
}

bool dal_ls_is_mst_network_present(
		struct link_service *ls)
{
	return ls->funcs->is_mst_network_present(ls);
}

bool dal_ls_are_mst_displays_cofunctional(
		struct link_service *ls,
		const uint32_t *array_display_index,
		uint32_t len)
{
	return ls->funcs->are_mst_displays_cofunctional(
			ls, array_display_index, len);
}

bool dal_ls_is_sink_present_at_display_index(
		struct link_service *ls,
		uint32_t display_index)
{
	return ls->funcs->is_sink_present_at_display_index(
			ls, display_index);
}

struct ddc_service *dal_ls_obtain_mst_ddc_service(
		struct link_service *ls,
		uint32_t display_index)
{
	return ls->funcs->obtain_mst_ddc_service(
			ls, display_index);
}

void dal_ls_release_mst_ddc_service(
		struct link_service *ls,
		struct ddc_service *ddc_service)
{
	ls->funcs->release_mst_ddc_service(ls, ddc_service);
}

bool dal_ls_get_gtc_sync_status(
		struct link_service *ls)
{
	return ls->funcs->get_gtc_sync_status(ls);
}

bool dal_ls_should_send_notification(struct link_service *ls)
{
	return ls->funcs->should_send_notification(ls);
}

void dal_ls_disable_link(struct link_service *ls,
				const struct hw_path_mode *path_mode)
{
	struct enable_link_param link_param = {0};

	link_param.display_path = path_mode->display_path;
	link_param.link_idx = ls->link_idx;
	link_param.path_mode = path_mode;

	dal_hw_sequencer_disable_link(ls->hwss, &link_param);
}

bool dal_ls_disable_stream_base(
	struct link_service *ls,
	uint32_t display_index,
	struct hw_path_mode *path_mode)
{
	struct enable_stream_param stream_param = {0};

	dal_memset(&ls->cur_link_setting, '\0', sizeof(ls->cur_link_setting));

	/* When we returning from S3/S4 we want to remain in
	 * PowerSave until DPMS coming*/
	ASSERT(ls->strm_state == STREAM_STATE_ENABLED ||
			ls->strm_state == STREAM_STATE_OPTIMIZED_READY ||
			ls->strm_state == STREAM_STATE_POWER_SAVE);

	if (ls->strm_state == STREAM_STATE_DISABLED ||
			ls->strm_state == STREAM_STATE_POWER_SAVE)
			/*@todo fix upper layer sequence to prevent this*/
		return true;
	dal_ls_disable_link(ls, path_mode);
	stream_param.display_path = path_mode->display_path;
	stream_param.link_idx = ls->link_idx;
	stream_param.path_mode = path_mode;
	dal_hw_sequencer_disable_stream(ls->hwss, &stream_param);

	ls->strm_state = STREAM_STATE_DISABLED;

	dal_display_path_set_target_powered_on(
					path_mode->display_path,
					DISPLAY_TRI_STATE_FALSE);

	return true;
}

uint32_t dal_ls_get_notification_display_index(
	struct link_service *ls)
{
	return ls->funcs->get_notification_display_idx(ls);
}

void dal_ls_update_stream_features(
	struct link_service *ls,
	const struct hw_path_mode *path_mode)
{
	ls->funcs->update_stream_features(ls, path_mode);
}

bool dal_ls_is_link_psr_supported(
	struct link_service *ls)
{
	return ls->funcs->is_link_psr_supported(ls);
}

bool dal_ls_is_stream_drr_supported(
	struct link_service *ls)
{
	return ls->funcs->is_stream_drr_supported(ls);
}

void dal_ls_set_link_psr_capabilities(
		struct link_service *ls,
		struct psr_caps *psr_caps)
{
	ls->funcs->set_link_psr_capabilities(ls, psr_caps);
}

void dal_ls_get_link_psr_capabilities(
		struct link_service *ls,
		struct psr_caps *psr_caps)
{
	ls->funcs->get_link_psr_capabilities(ls, psr_caps);
}
