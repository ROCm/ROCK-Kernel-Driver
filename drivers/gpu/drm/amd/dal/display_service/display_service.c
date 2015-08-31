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
#include "include/display_service_interface.h"
#include "include/topology_mgr_interface.h"
#include "include/set_mode_interface.h"
#include "include/hw_sequencer_interface.h"
#include "include/link_service_interface.h"
#include "include/adjustment_interface.h"
#include "include/display_path_interface.h"

#include "display_service.h"
#include "ds_dispatch.h"
#include "path_mode_set_with_data.h"

struct display_service {
	struct ds_dispatch *ds_dispatch;
};

/*
 * Local function declaration
 */
/* Initialize display service */
static bool ds_construct(
		struct display_service *ds, const struct ds_init_data *data);

/*
 * dal_display_service_create
 *
 * Create display service
 */
struct display_service *dal_display_service_create(struct ds_init_data *data)
{
	struct display_service *ds;

	ds = dal_alloc(sizeof(struct display_service));

	if (ds == NULL)
		return NULL;

	if (ds_construct(ds, data))
		return ds;

	dal_free(ds);
	BREAK_TO_DEBUGGER();

	return NULL;
}

/*
 * dal_display_service_destroy
 *
 * Destroy display service
 */
void dal_display_service_destroy(struct display_service **ds)
{
	if (!ds || !*ds) {
		BREAK_TO_DEBUGGER();
		return;
	}
	dal_ds_dispatch_cleanup_adjustment((*ds)->ds_dispatch);
	dal_ds_dispatch_destroy(&(*ds)->ds_dispatch);

	dal_free(*ds);
	*ds = NULL;
}

struct ds_dispatch *dal_display_service_get_adjustment_interface(
	struct display_service *ds)
{
	return ds->ds_dispatch;
}

struct ds_overlay *dal_display_service_get_overlay_interface(
	struct display_service *ds)
{
	/*TODO: add implementation*/
	return NULL;
}

struct ds_dispatch *dal_display_service_get_set_mode_interface(
	struct display_service *ds)
{
	if (ds == NULL)
		return NULL;

	return ds->ds_dispatch;
}

struct ds_dispatch *dal_display_service_get_reset_mode_interface(
	struct display_service *ds)
{
	if (ds == NULL)
		return NULL;

	return ds->ds_dispatch;
}

struct ds_synchronization *dal_display_service_get_synchronization_interface(
	struct display_service *ds)
{
	/*TODO: add implementation*/
	return NULL;
}

enum ds_return dal_display_service_notify_v_sync_int_state(
	struct display_service *ds,
	uint32_t display_index,
	bool maintain_v_sync_phase)
{
	/*TODO: add implementation*/
	return DS_ERROR;
}

enum ds_return dal_display_service_target_power_control(
	struct display_service *ds,
	uint32_t display_index,
	bool power_on)
{
	/* Handles DPMS states of all displaypaths
	 * by event and Manages the DPMS state of
	 * given DisplayPath by powerOn
	 */
	/*TODO: Depends on AdjustmentContainer.cpp, should
	 * be uncommented once adjustmentContainer is
	 * implemented.
	 * ManageDPMSState(displayPathIndex, powerOn);*/

	enum hwss_result hwss_ret = HWSS_RESULT_OK;
	struct hw_path_mode hw_path_mode;
	struct display_path *display_path = NULL;

	if (dal_tm_is_hw_state_valid(ds->ds_dispatch->tm)) {
		uint32_t link_cnt = 0;
		int32_t i = 0;
		struct active_path_data *path_data =
			dal_pms_with_data_get_path_data_for_display_index(
				ds->ds_dispatch->set,
				display_index);

		if (!path_data)
			return DS_ERROR;

		path_data->flags.bits.TURN_OFF_BACK_END_AND_RX =
			power_on ? 0 : 1;

		/* Create hw_path_mode for this display path*/
		if (!dal_ds_dispatch_build_hw_path_mode_for_adjustment(
			ds->ds_dispatch,
			&hw_path_mode,
			display_index,
			NULL))
			/* DisplayIndex requested not in
			 * currently active PathModeSet,
			 * therefore invalid parameter
			 */
			return DS_ERROR;

		/*Signal EventID_DisplayPhyAccessBegin
		 * event to allow access to Display output HW
		 */
		/*TODO: Event eventPhyAccessBegin(
		 * EventID_DisplayPhyAccessBegin);
		 * getEM()->SendEvent(this, &eventPhyAccessBegin);
		 */

		display_path = dal_tm_display_index_to_display_path(
			ds->ds_dispatch->tm,
			display_index);
		link_cnt = dal_display_path_get_number_of_links(
					display_path);

		if (power_on) {
			/* Call EnableAllowSelfRefresh
			 * when not resuming from S3 or S4. */
			/*TODO: Remove the below
			 * comments once stutter mode
			 * is implemented.
			if (dal_tm_get_current_power_state(
			ds->ds_dispatch->tm) ==
					VIDEO_POWER_ON &&
			dal_tm_get_previous_power_state(
			ds->ds_dispatch->tm) >
				VIDEO_POWER_ON &&
			dal_tm_get_previous_power_state(
			ds->ds_dispatch->tm) <
				VIDEO_POWER_SHUTDOWN)
			hwss_ret =
			dal_hw_sequencer_enable_allow_self_refresh(
				&hw_path_mode,
				false);
			*/
			/* turn off DPMS light sleep - power on memories*/
			/*TODO: Mainly used for DCE 10 & DCE11
			 * dal_tm_toggle_dpms_light_sleep(false);
			 */

			/* move stream to Enabled from Power Off state*/
			for (i = 0; i < link_cnt; i++) {

				struct link_service *ls =
				dal_display_path_get_link_config_interface(
					display_path, i);

				ASSERT_CRITICAL(ls != NULL);
				dal_ls_power_on_stream(
					ls,
					display_index,
					&hw_path_mode);
			}



			if (dal_adapter_service_is_feature_supported(
				FEATURE_DPMS_AUDIO_ENDPOINT_CONTROL))
				/* Enable audio device*/
				dal_hw_sequencer_enable_azalia_audio_jack_presence(
					ds->ds_dispatch->hwss,
					display_path);
			else
				dal_hw_sequencer_mute_audio_endpoint(
					ds->ds_dispatch->hwss,
					display_path,
					false);

			/* move stream from Enabled to Active*/
			for (i = 0; i < link_cnt; i++) {

				struct link_service *ls =
				dal_display_path_get_link_config_interface(
					display_path, i);

				ASSERT_CRITICAL(ls != NULL);

				dal_ls_unblank_stream(
					ls,
					display_index,
					&hw_path_mode);
			}

			dal_hw_sequencer_mute_audio_endpoint(
				ds->ds_dispatch->hwss,
				display_path,
				false);

			/* Make sure internal states are updated properly*/
			path_data->display_state.OUTPUT_ENABLED = 1;
			path_data->display_state.OUTPUT_BLANKED = 0;

			/*
			 * Re-enable PSR if display is not in blanked state
			 */
			if (dal_display_path_is_source_blanked(display_path) &&
				dal_display_path_is_psr_supported(display_path))
				dal_hw_sequencer_psr_enable(
					ds->ds_dispatch->hwss,
					display_path);

			/* Use m_numDisplaysConnected to mark the
			 * end of wake-up/resume by setting TM's
			 * current power state to On
			 */
			/*TODO: Depends on AdjustmentContainer.cpp, should be
			 * uncommented once AdjustmentContainer is implemented
			 * if (m_numDisplaysDPMSOn == m_numDisplaysConnected)
			{
			getTM()->SetCurrentPowerState(VideoPowerOn);
			}*/
		} else {
			/* disable PSR*/
			if (dal_display_path_is_psr_supported(display_path))
				dal_hw_sequencer_psr_disable(
					ds->ds_dispatch->hwss,
					display_path);

			/* move stream from Active to Enable state*/
			for (i = link_cnt - 1; i >= 0; i--) {

				struct link_service *ls =
				dal_display_path_get_link_config_interface(
					display_path, i);

				ASSERT_CRITICAL(ls != NULL);
				dal_ls_blank_stream(
					ls,
					display_index,
					&hw_path_mode);
				dal_hw_sequencer_mute_audio_endpoint(
					ds->ds_dispatch->hwss,
					hw_path_mode.display_path,
					true);
			}

			/* move stream from Enable to Power Off state*/
			for (i = link_cnt - 1; i >= 0; i--) {

				struct link_service *ls =
				dal_display_path_get_link_config_interface(
					display_path, i);

				ASSERT_CRITICAL(ls != NULL);

				dal_ls_power_off_stream(
					ls,
					display_index,
					&hw_path_mode);
			}

			/* blank CRTC and enable stutter
			 * mode allow_self_Refresh*/
			 /*TODO: Remove comments after implementing
			 * stutter mode
			 */
			/* hwssRet = getHWSS()->
			 * EnableAllowSelfRefresh(
			 * &hwPathMode,
			 * true);

			 force SW controlled memories
			 into light sleep state
			getTM()->ToggleDPMSLightSleep(true);
			*/
			/* Make sure internal states
			 * are updated properly*/
			path_data->display_state.OUTPUT_ENABLED = 0;
			path_data->display_state.OUTPUT_BLANKED = 1;
		}

		/*TODO: PPLib Notification
		 Logical state of display might
		 change so we need to update PPlib cache
		m_pDSDispatch->
		NotifySingleDisplayConfig(
		displayPathIndex,
		true);

		 //Signal EventID_DisplayPhyAccessEnd
		 event to inform that Display
		 output configuration has changed
		Event eventPhyAccessEnd(
		EventID_DisplayPhyAccessEnd);
		getEM()->SendEvent(
		this, &eventPhyAccessEnd);*/
	}

	/*TODO: PPLib Notification
	NotifyETW(DAL_NOTIFYPPLIBSCREENSTATUSCHANGE_ENTER);

	notify PPLib only once -
	when first display DPMS on or
	last display DPMS off, pplib will program
	NBMCU based on this.
	if (powerOn && m_numDisplaysDPMSOn == 1)
	{
	getEC()->NotifyScreenStatusChange(true);
	}
	else if (!powerOn && m_numDisplaysDPMSOn == 0)
	{
	getEC()->NotifyScreenStatusChange(false);
	//Clear m_numDisplaysConnected
	m_numDisplaysConnected = 0;
	}

	NotifyETW(DAL_NOTIFYPPLIBSCREENSTATUSCHANGE_EXIT);
	*/
	/* No adjustments expected be allocated
	 * no need to destroy adjustments
	 */
	return (hwss_ret == HWSS_RESULT_OK ? DS_SUCCESS : DS_ERROR);
}

enum ds_return dal_display_service_power_down_active_hw(
	struct display_service *ds,
	enum dal_video_power_state state)
{
	/*TODO: add implementation*/
	return DS_ERROR;
}

enum ds_return dal_display_service_mem_request_control(
	struct display_service *ds,
	uint32_t display_index,
	bool blank)
{
	if (dal_tm_is_hw_state_valid(ds->ds_dispatch->tm)) {
		struct display_path *display_path;
		enum signal_type signal;
		struct hw_path_mode mode;

		if (!dal_ds_dispatch_build_hw_path_mode_for_adjustment(
				ds->ds_dispatch, &mode, display_index, NULL))
			return DS_ERROR;

		display_path = dal_tm_create_resource_context_for_display_index(
			ds->ds_dispatch->tm, display_index);

		signal = dal_display_path_get_query_signal(
				display_path, SINK_LINK_INDEX);

		dal_tm_destroy_resource_context_for_display_path(
				ds->ds_dispatch->tm, display_path);

		if (!blank) {
			dal_hw_sequencer_enable_memory_requests(
					ds->ds_dispatch->hwss,
					&mode);

			if (signal == SIGNAL_TYPE_WIRELESS)
				dal_hw_sequencer_enable_wireless_idle_detection
						(ds->ds_dispatch->hwss,	true);
		} else {
			dal_hw_sequencer_disable_memory_requests(
					ds->ds_dispatch->hwss,
					&mode);
			if (signal == SIGNAL_TYPE_WIRELESS)
				dal_hw_sequencer_enable_wireless_idle_detection
						(ds->ds_dispatch->hwss,	false);
		}
	}

	return DS_SUCCESS;
}

enum ds_return dal_display_service_set_multimedia_pass_through_mode(
	struct display_service *ds,
	uint32_t display_index,
	bool passThrough)
{
	/*TODO: add implementation*/
	return DS_ERROR;
}

enum ds_return dal_display_service_set_palette(
	struct display_service *ds,
	uint32_t display_index,
	const struct ds_devclut *palette,
	const uint32_t start,
	const uint32_t length)
{
	/*TODO: add implementation*/
	return DS_ERROR;
}

enum ds_return dal_display_service_apply_pix_clk_range(
	struct display_service *ds,
	uint32_t display_index,
	struct pixel_clock_safe_range *range)
{
	/*TODO: add implementation*/
	return DS_ERROR;
}

enum ds_return dal_display_service_get_safe_pix_clk(
	struct display_service *ds,
	uint32_t display_index,
	uint32_t *pix_clk_khz)
{
	/*TODO: add implementation*/
	return DS_ERROR;
}

enum ds_return dal_display_service_apply_refreshrate_adjustment(
	struct display_service *ds,
	uint32_t display_index,
	enum ds_refreshrate_adjust_action action,
	struct ds_refreshrate *refreshrate)
{
	/*TODO: add implementation*/
	return DS_ERROR;
}

enum ds_return dal_display_service_pre_ddc(
	struct display_service *ds,
	uint32_t display_index)
{
	/*TODO: add implementation*/
	return DS_ERROR;
}

enum ds_return dal_display_service_post_ddc(
	struct display_service *ds,
	uint32_t display_index)
{
	/*TODO: add implementation*/
	return DS_ERROR;
}

enum ds_return dal_display_service_backlight_control(
	struct display_service *ds,
	uint32_t display_index,
	bool enable)
{
	/*TODO: add implementation*/
	return DS_ERROR;
}

enum ds_return dal_display_service_get_backlight_user_level(
	struct display_service *ds,
	uint32_t display_index,
	uint32_t *level)
{
	/*TODO: add implementation*/
	return DS_ERROR;
}

enum ds_return dal_display_service_get_backlight_effective_level(
	struct display_service *ds,
	uint32_t display_index,
	uint32_t *level)
{
	/*TODO: add implementation*/
	return DS_ERROR;
}

enum ds_return dal_display_service_enable_hpd(
	struct display_service *ds,
	uint32_t display_index)
{
	/*TODO: add implementation*/
	return DS_ERROR;
}

enum ds_return dal_display_service_disable_hpd(
	struct display_service *ds,
	uint32_t display_index)
{
	/*TODO: add implementation*/
	return DS_ERROR;
}

enum ds_return dal_display_service_get_min_mem_channels(
		struct display_service *ds,
		const struct path_mode_set *path_mode_set,
		uint32_t mem_channels_num,
		uint32_t *min_mem_channels_num)
{
	/*TODO: add implementation*/
	return DS_ERROR;
}

enum ds_return dal_display_service_enable_advanced_request(
	struct display_service *ds,
	bool enable)
{
	/*TODO: add implementation*/
	return DS_ERROR;
}

/*Audio related*/

enum ds_return dal_display_service_enable_audio_endpoint(
	struct display_service *ds,
	uint32_t display_index,
	bool enable)
{
	/*TODO: add implementation*/
	return DS_ERROR;
}

enum ds_return dal_display_service_mute_audio_endpoint(
	struct display_service *ds,
	uint32_t display_index,
	bool mute)
{
	/*TODO: add implementation*/
	return DS_ERROR;
}

bool dal_display_service_calc_view_port_for_wide_display(
	struct display_service *ds,
	uint32_t display_index,
	const struct ds_view_port *set_view_port,
	struct ds_get_view_port *get_view_port)
{
	/*TODO: add implementation*/
	return false;
}

/*
 * Local function definition
 */

/*
 * ds_construct
 *
 * Initialize display service
 */
static bool ds_construct(
		struct display_service *ds, const struct ds_init_data *data)
{
	struct ds_dispatch_init_data dispatch_data;

	if (data == NULL)
		return false;

	dispatch_data.dal_context = data->dal_context;
	dispatch_data.as = data->as;
	dispatch_data.hwss = data->hwss;
	dispatch_data.tm = data->tm;
	dispatch_data.ts = data->ts;

	ds->ds_dispatch = dal_ds_dispatch_create(&dispatch_data);

	if (ds->ds_dispatch == NULL)
		return false;

	return true;
}
