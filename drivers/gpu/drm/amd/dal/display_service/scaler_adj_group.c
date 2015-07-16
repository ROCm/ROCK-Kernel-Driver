/*
 * Copyright 2012-14 Advanced Micro Devices, Inc.
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

#include "include/adjustment_interface.h"
#include "include/hw_sequencer_interface.h"
#include "include/hw_path_mode_set_interface.h"
#include "include/hw_adjustment_types.h"
#include "include/adjustment_types.h"
#include "include/display_service_types.h"
#include "include/logger_interface.h"
#include "include/set_mode_types.h"
#include "include/set_mode_interface.h"
#include "include/dcs_types.h"
#include "include/dcs_interface.h"
#include "include/timing_service_types.h"
#include "include/timing_service_interface.h"
#include "include/display_path_interface.h"

#include "ds_translation.h"
#include "adjustment_container.h"
#include "scaler_adj_group.h"

/*to chk whether underscan can be applied to current timing*/
static bool can_scaling_be_applied(
	struct adj_container *container,
	enum timing_standard timing_std,
	enum timing_source timing_src,
	enum adjustment_id adj_id,
	enum underscan_reason reason)
{
	struct adjustment_info *info = NULL;
	enum signal_type type;

	if (!container)
		return false;

	if (adj_id != ADJ_ID_MULTIMEDIA_PASS_THROUGH) {
		info = dal_adj_info_set_get_adj_info(
			&container->adj_info_set,
			ADJ_ID_MULTIMEDIA_PASS_THROUGH);
		if (info) {
			/*if indeed multimedia pass thru,we don't need apply
			 * underscan*/
			if (info->adj_data.ranged.cur > 0)
				return false;
		}
	}
	type = dal_adj_container_get_signal_type(container);
	if (dal_timing_service_is_ce_timing_standard(timing_std) ||
		(dal_is_embedded_signal(type) && timing_std ==
			TIMING_STANDARD_EXPLICIT)) {
		if (timing_src != TIMING_SOURCE_CUSTOM &&
			reason != UNDERSCAN_REASON_FALL_BACK)
			return true;/*we will do underscan*/
	}
	return false;
}

static bool is_pass_thru_enabled(
	const struct ds_adjustment_scaler *scaler_param,
	const struct ds_underscan_parameter *underscan_param,
	struct adj_container *container,
	enum build_path_set_reason reason)
{
	const struct adjustment_info *info;
	enum underscan_reason reason_for_underscan;

	if (scaler_param->adjust_id == ADJ_ID_MULTIMEDIA_PASS_THROUGH &&
		scaler_param->value > 0)
		return true;

	switch (reason) {
	case BUILD_PATH_SET_REASON_FALLBACK_UNDERSCAN:
		reason_for_underscan = UNDERSCAN_REASON_FALL_BACK;
		break;
	case BUILD_PATH_SET_REASON_SET_MODE:
		reason_for_underscan = UNDERSCAN_REASON_PATCH_TIMING;
		break;
	case BUILD_PATH_SET_REASON_SET_ADJUSTMENT:
	default:
		reason_for_underscan = UNDERSCAN_REASON_SET_ADJUSTMENT;
		break;
	}

	if (can_scaling_be_applied(
		container,
		scaler_param->timing_standard,
		scaler_param->timing_source,
		scaler_param->adjust_id,
		reason_for_underscan)) {
		info = dal_adj_info_set_get_adj_info(
			&container->adj_info_set,
			ADJ_ID_MULTIMEDIA_PASS_THROUGH);
		if (!info)
			return false;
		if (info->adj_data.ranged.cur > 0)
			return true;
	}

	return false;
}

static bool build_base_avi_info_frame_parameter(
	const struct ds_adjustment_scaler *scaler_param,
	const struct ds_underscan_parameter *underscan_param,
	struct adj_container *container,
	const struct hw_path_mode *hw_path_mode,
	enum build_path_set_reason reason,
	enum hw_scale_options *underscan_avi_rule)
{
	struct cea861_support cea861_support = {0};

	if (hw_path_mode->mode.ds_info.cea_vic != 0)
		if (is_pass_thru_enabled(
			scaler_param, underscan_param, container, reason))
			*underscan_avi_rule = HW_SCALE_OPTION_OVERSCAN;
		else
			*underscan_avi_rule = HW_SCALE_OPTION_UNDERSCAN;
	else {
		if (dal_adj_container_get_cea861_support(container,
			&cea861_support) &&
			cea861_support.features.UNDER_SCAN == 1)
			*underscan_avi_rule = HW_SCALE_OPTION_UNDERSCAN;
		else
			*underscan_avi_rule = HW_SCALE_OPTION_UNKNOWN;
	}
	return true;
}

static bool build_avi_info_frame_parameter(
	const struct ds_adjustment_scaler *scaler_param,
	struct ds_underscan_parameter *underscan_param,
	struct adj_container *container,
	const struct hw_path_mode *hw_path_mode,
	enum build_path_set_reason reason,
	enum hw_scale_options *underscan_avi_rule)
{
	union cea_video_capability_data_block vcdb = { {0} };
	bool result = false;

	if (dal_adj_container_get_cea_video_cap_data_block(container, &vcdb)) {
		if (hw_path_mode->mode.ds_info.DISPLAY_PREFERED_MODE == 1 &&
			(vcdb.bits.S_PT0 != 0 || vcdb.bits.S_PT1 != 0)) {
			if (vcdb.bits.S_PT0 == 1 && vcdb.bits.S_PT1 == 0)
				*underscan_avi_rule = HW_SCALE_OPTION_OVERSCAN;
			else if (vcdb.bits.S_PT0 == 0 && vcdb.bits.S_PT1 == 1)
				*underscan_avi_rule = HW_SCALE_OPTION_UNDERSCAN;
			else
			result = build_base_avi_info_frame_parameter(
				scaler_param,
				underscan_param,
				container,
				hw_path_mode,
				reason,
				underscan_avi_rule);
		} else {
		if (hw_path_mode->mode.ds_info.cea_vic != 0) {
			if (vcdb.bits.S_CE0 == 1 && vcdb.bits.S_CE1 == 0)
				*underscan_avi_rule = HW_SCALE_OPTION_OVERSCAN;
			else if (vcdb.bits.S_CE0 == 0 && vcdb.bits.S_CE1 == 1)
				*underscan_avi_rule = HW_SCALE_OPTION_UNDERSCAN;
			else
				result = build_base_avi_info_frame_parameter(
						scaler_param,
						underscan_param,
						container,
						hw_path_mode,
						reason,
						underscan_avi_rule);
		} else {
			if (vcdb.bits.S_IT0 == 1 && vcdb.bits.S_IT1 == 0)
				*underscan_avi_rule = HW_SCALE_OPTION_OVERSCAN;
			else if (vcdb.bits.S_IT0 == 0 && vcdb.bits.S_IT1 == 1)
				*underscan_avi_rule = HW_SCALE_OPTION_UNDERSCAN;
			else
				result = build_base_avi_info_frame_parameter(
						scaler_param,
						underscan_param,
						container,
						hw_path_mode,
						reason,
						underscan_avi_rule);
		}
	       }
	} else
		result = build_base_avi_info_frame_parameter(
				scaler_param,
				underscan_param,
				container,
				hw_path_mode,
				reason,
				underscan_avi_rule);
	return result;
}

/*we donot enable underscan on DP since architect decision*/
static bool is_dp_underscan_disabled(
	struct display_path *display_path,
	uint32_t idx)
{
	if (display_path) {
		enum signal_type signal =
			dal_display_path_get_query_signal(
				display_path,
				SINK_LINK_INDEX);

		if (dal_is_dp_signal(signal) ||
			dal_is_dp_external_signal(signal))
			return true;
	}
	return false;
}

static bool setup_parameter(
	const struct hw_path_mode *hw_path_mode,
	const struct ds_adjustment_scaler *scaler_param,
	struct ds_underscan_parameter *underscan_param)
{
	if (scaler_param->flags.bits.IS_UNDERSCAN_DESC != 1 || !scaler_param ||
		!hw_path_mode)
		return false;

	if (scaler_param->underscan_desc.width == 0 ||
		scaler_param->underscan_desc.height == 0 ||
		scaler_param->underscan_desc.width <
		scaler_param->underscan_desc.x ||
		scaler_param->underscan_desc.height <
		scaler_param->underscan_desc.y)
		return false;

	if (scaler_param->underscan_desc.width >
	hw_path_mode->mode.timing.h_addressable)
		return false;

	if (scaler_param->underscan_desc.height >
	hw_path_mode->mode.timing.v_addressable)
		return false;

	dal_memset(underscan_param, 0, sizeof(*underscan_param));
	underscan_param->type = DS_UNDERSCAN_TYPE_DIMENTIONS;
	underscan_param->data.dimentions.data.width =
		scaler_param->underscan_desc.width;
	underscan_param->data.dimentions.data.height =
		scaler_param->underscan_desc.height;
	underscan_param->data.dimentions.data.position_x =
		scaler_param->underscan_desc.x;
	underscan_param->data.dimentions.data.position_y =
		scaler_param->underscan_desc.y;
	underscan_param->data.dimentions.modified_boarder_x =
		hw_path_mode->mode.timing.h_addressable -
		scaler_param->underscan_desc.width;
	underscan_param->data.dimentions.modified_boarder_y =
		hw_path_mode->mode.timing.v_addressable -
		scaler_param->underscan_desc.height;

	return true;

}

static void setup_parameters(
	const struct hw_path_mode *hw_path_mode,
	const struct ds_adjustment_scaler *scaler_param,
	struct ds_overscan *overscan,
	struct ds_underscan_parameter *underscan_param,
	struct timing_info_parameter *timing_info)
{
	overscan->left = hw_path_mode->mode.overscan.left;
	overscan->right = hw_path_mode->mode.overscan.right;
	overscan->top = hw_path_mode->mode.overscan.top;
	overscan->bottom = hw_path_mode->mode.overscan.bottom;
	dal_memset(underscan_param, 0, sizeof(*underscan_param));
	underscan_param->type = DS_UNDERSCAN_TYPE_PERCENT;
	underscan_param->data.percent.old_dst_x =
		hw_path_mode->mode.scaling_info.dst.width;
	underscan_param->data.percent.old_dst_y =
		hw_path_mode->mode.scaling_info.dst.height;
	underscan_param->data.percent.percent_x =
		(uint32_t)scaler_param->value;
	underscan_param->data.percent.percent_y =
		(uint32_t)scaler_param->value;

	dal_memset(timing_info, 0, sizeof(*timing_info));
	timing_info->timing = hw_path_mode->mode.timing;
	timing_info->dst_width =
		hw_path_mode->mode.scaling_info.dst.width;
	timing_info->dst_height =
		hw_path_mode->mode.scaling_info.dst.height;

}


static void extract_parameters(
	const struct ds_adjustment_scaler *scaler_param,
	const struct timing_info_parameter *timing_info,
	const struct ds_overscan *overscan,
	enum hw_scale_options underscan_avi_rule,
	struct hw_path_mode *hw_path_mode)
{
	hw_path_mode->mode.overscan.left = overscan->left;
	hw_path_mode->mode.overscan.right = overscan->right;
	hw_path_mode->mode.overscan.top = overscan->top;
	hw_path_mode->mode.overscan.bottom = overscan->bottom;
	hw_path_mode->mode.scaling_info.dst.width =
		timing_info->dst_width;
	hw_path_mode->mode.scaling_info.dst.height =
		timing_info->dst_height;
	hw_path_mode->mode.underscan_rule = underscan_avi_rule;
	hw_path_mode->mode.ds_info.position_x = overscan->left;
	hw_path_mode->mode.ds_info.position_y = overscan->top;
	hw_path_mode->mode.ds_info.TIMING_UNDERSCAN_PATCHED = 1;
}

static void update_underscan_bundle(
	const struct ds_adjustment_scaler *scaler_param,
	const struct underscan_adjustment_group *group,
	const struct timing_info_parameter *timing_info,
	struct ds_underscan_parameter *underscan)
{
	switch (group->id_requested) {
	case ADJ_ID_UNDERSCAN:
		if (scaler_param->flags.bits.IS_TV == 1 &&
			group->current_underscan_auto == 0) {
			underscan->type = DS_UNDERSCAN_TYPE_DIMENTIONS;
			underscan->data.dimentions.data.width =
				group->current_underscan_desc.width;
			underscan->data.dimentions.data.height =
				group->current_underscan_desc.height;
			underscan->data.dimentions.data.position_x =
				group->current_underscan_desc.x;
			underscan->data.dimentions.data.position_y =
				group->current_underscan_desc.y;
			underscan->data.dimentions.modified_boarder_x =
				timing_info->timing.h_addressable -
				group->current_underscan_desc.width;
			underscan->data.dimentions.modified_boarder_y =
				timing_info->timing.v_addressable -
				group->current_underscan_desc.height;
		} else {
			underscan->data.percent.percent_x =
				(uint32_t)group->requested_value;
			underscan->data.percent.percent_y =
				(uint32_t)group->requested_value;
		}
		break;
	case ADJ_ID_UNDERSCAN_TYPE:
			if (group->requested_value == 0) {
				underscan->type = DS_UNDERSCAN_TYPE_DIMENTIONS;
				underscan->data.dimentions.data.width =
					group->current_underscan_desc.width;
				underscan->data.dimentions.data.height =
					group->current_underscan_desc.height;
				underscan->data.dimentions.data.position_x =
					group->current_underscan_desc.x;
				underscan->data.dimentions.data.position_y =
					group->current_underscan_desc.y;
				underscan->data.dimentions.modified_boarder_x =
					timing_info->timing.h_addressable -
					group->current_underscan_desc.width;
				underscan->data.dimentions.modified_boarder_y =
					timing_info->timing.v_addressable -
					group->current_underscan_desc.height;
			} else {
				underscan->data.percent.percent_x =
					(uint32_t)group->current_percent_x;
				underscan->data.percent.percent_y =
					(uint32_t)group->current_percent_y;
			}
			break;
	default:
		break;
	}

}

static bool calculate_underscan(
	const struct ds_underscan_parameter *underscan_param,
	uint32_t *new_dest_x,
	uint32_t *new_dest_y,
	struct ds_overscan *overscan)
{
	uint32_t underscan_x;
	uint32_t underscan_y;
	uint32_t underscan_x2;
	uint32_t underscan_y2;

	if (!underscan_param || !overscan || !new_dest_x || !new_dest_y)
		return false;

	if (underscan_param->type != DS_UNDERSCAN_TYPE_PERCENT &&
		underscan_param->type != DS_UNDERSCAN_TYPE_DIMENTIONS)
		return false;

	if (underscan_param->type == DS_UNDERSCAN_TYPE_PERCENT) {
		if (underscan_param->data.percent.old_dst_x == 0 ||
			underscan_param->data.percent.old_dst_y == 0)
			return false;
	underscan_x = underscan_param->data.percent.old_dst_x *
		underscan_param->data.percent.percent_x / 100;
	underscan_y = underscan_param->data.percent.old_dst_y *
		underscan_param->data.percent.percent_y / 100;

	if (underscan_param->data.percent.old_dst_x <= underscan_x ||
		underscan_param->data.percent.old_dst_y <= underscan_y)
		return false;

	*new_dest_x = underscan_param->data.percent.old_dst_x - underscan_x;
	*new_dest_y = underscan_param->data.percent.old_dst_y - underscan_y;
	underscan_x2 = underscan_x>>1;
	underscan_x -= underscan_x2;

	underscan_y2 = underscan_y>>1;
	underscan_y -= underscan_y2;

	overscan->left += underscan_x;
	overscan->right += underscan_x2;
	overscan->bottom += underscan_y;
	overscan->top += underscan_y2;

	}
	/* this underscan is not centered!*/
	else {
		if (underscan_param->data.dimentions.data.width == 0 ||
			underscan_param->data.dimentions.data.height == 0)
			return false;
		overscan->left +=
			underscan_param->data.dimentions.data.position_x;
		overscan->right +=
			underscan_param->data.dimentions.modified_boarder_x;
		overscan->right = overscan->right >=
			underscan_param->data.dimentions.data.position_y ?
			overscan->bottom - underscan_param->
			data.dimentions.data.position_y : 0;
		*new_dest_x = underscan_param->data.dimentions.data.width;
		*new_dest_y = underscan_param->data.dimentions.data.height;
	}
	if (overscan->left & 0x01) {
		overscan->left--;
		overscan->right++;
	}
	/*Top should be even number at interlace mode*/
	if (overscan->top & 0x01) {
		overscan->top--;
		overscan->bottom++;
	}
	return true;
}

bool dal_scaler_adj_group_build_scaler_parameter(
	const struct path_mode *path_mode,
	struct adj_container *container,
	enum build_path_set_reason reason,
	enum adjustment_id adjust_id,
	int32_t value,
	const struct ds_underscan_desc *underscan_desc,
	const struct display_path *display_path,
	struct ds_adjustment_scaler *param)
{
	struct dcs *dcs = dal_display_path_get_dcs(display_path);
	struct dcs_stereo_3d_features feature;

	if (!display_path || !path_mode || !dcs)
		return false;
	dal_memset(param, 0, sizeof(*param));
	param->timing_source = path_mode->mode_timing->
		mode_info.timing_source;
	param->timing_standard = path_mode->mode_timing->crtc_timing.
		timing_standard;
	param->display_index = path_mode->display_path_index;
	if (path_mode->view_3d_format != VIEW_3D_FORMAT_NONE) {
		enum timing_3d_format format =
			path_mode->mode_timing->crtc_timing.timing_3d_format;
		feature = dal_dcs_get_stereo_3d_features(dcs, format);
		if (feature.flags.SUPPORTED && !feature.flags.SCALING)
			return false;
	}

	if (reason == BUILD_PATH_SET_REASON_SET_ADJUSTMENT) {
		if (dal_adj_container_get_default_underscan_allow(container))
			return false;
		param->flags.bits.IS_FOR_SET_MODE = 0;
		param->adjust_id = adjust_id;
		param->value = value;
		if (underscan_desc) {
			param->underscan_desc = *underscan_desc;
			param->flags.bits.IS_UNDERSCAN_DESC = 1;
		}
	} else {
		param->flags.bits.IS_FOR_SET_MODE = 1;
		param->adjust_id = ADJ_ID_UNDERSCAN;
		param->value = 0;
	}
	return true;
}

static bool build_underscan_bundle(
	const struct ds_adjustment_scaler *param,
	struct adj_container *container,
	const struct timing_info_parameter *timing_info,
	struct underscan_adjustment_group *group)
{
	struct adjustment_info *mm_pass_thur;
	struct adjustment_info *underscan;

	dal_memset(group, 0, sizeof(*group));
	group->id_overscan = ADJ_ID_OVERSCAN;
	group->id_underscan = ADJ_ID_UNDERSCAN;
	group->id_underscan_auto = ADJ_ID_UNDERSCAN_TYPE;
	group->id_multi_media_pass_thru = ADJ_ID_MULTIMEDIA_PASS_THROUGH;

	group->id_requested = param->adjust_id;
	group->requested_value = param->value;

	if (!param || !container || !timing_info)
		return false;
	underscan = dal_adj_info_set_get_adj_info(
		&container->adj_info_set,
		group->id_underscan);
	if (!underscan)
		return false;

	mm_pass_thur = dal_adj_info_set_get_adj_info(
			&container->adj_info_set,
			group->id_multi_media_pass_thru);


	group->current_percent_x = underscan->adj_data.ranged.cur;
	group->current_percent_y = underscan->adj_data.ranged.cur;

	if (mm_pass_thur)
		group->current_multi_media_pass_thru =
			mm_pass_thur->adj_data.ranged.cur;
	else
		group->current_multi_media_pass_thru = 0;
	if (param->flags.bits.IS_FOR_SET_MODE == 1)
		group->requested_value = group->current_percent_x;

	return true;
}

static bool build_underscan_parameters(
	const struct ds_adjustment_scaler *param,
	struct adj_container *container,
	enum build_path_set_reason reason,
	struct ds_underscan_parameter *underscan_param,
	struct timing_info_parameter *timing_info,
	struct ds_overscan *overscan)
{
	struct underscan_adjustment_group group;

	if (!build_underscan_bundle(
		param,
		container,
		timing_info,
		&group))
		return false;
	/*update parameter if required*/
	update_underscan_bundle(
		param,
		&group,
		timing_info,
		underscan_param);
	/*calculate underscan and new destination*/
	if (!calculate_underscan(
		underscan_param,
		&timing_info->dst_width,
		&timing_info->dst_height,
		overscan))
		return false;

	return true;
}

static struct hw_path_mode *find_hw_path_mode(
	const struct display_path *display_path,
	struct hw_path_mode_set *hw_pms)
{
	uint32_t i;
	uint32_t num_of_path;
	struct hw_path_mode *mode = NULL;
	struct hw_path_mode *local_mode;

	num_of_path = dal_hw_path_mode_set_get_paths_number(hw_pms);
	for (i = 0; i < num_of_path; i++) {
		local_mode = dal_hw_path_mode_set_get_path_by_index(hw_pms, i);
		if (local_mode && local_mode->display_path == display_path) {
			mode = local_mode;
			break;
		}
	}
	return mode;
}

static bool build_hw_path_set_for_adjustment(
	struct ds_dispatch *ds,
	const struct ds_adjustment_scaler *param,
	const struct display_path *display_path,
	uint32_t disp_index,
	struct hw_path_mode_set *hw_pms)
{
	struct adjustment_params adj_param;

	if (!param || !display_path || !hw_pms)
		return false;

	dal_memset(&adj_param, 0, sizeof(struct adjustment_params));
	adj_param.affected_path = display_path;
	adj_param.action = ADJUSTMENT_ACTION_SET_ADJUSTMENT;
	adj_param.params.type = ADJUSTMENT_PAR_TYPE_TIMING;
	adj_param.params.timings.ajd_id = param->adjust_id;
	adj_param.params.timings.adj_id_hw = HW_ADJUSTMENT_ID_OVERSCAN;
	if (param->adjust_id == ADJ_ID_UNDERSCAN_TYPE)
		adj_param.params.timings.ajd_id = ADJ_ID_UNDERSCAN;

	return dal_ds_dispatch_build_hw_path_set_for_adjustment(
		ds,
		hw_pms,
		&adj_param);
}

bool dal_scaler_adj_group_apply_scaling(
	const struct ds_adjustment_scaler *param,
	struct adj_container *container,
	enum build_path_set_reason reason,
	struct hw_path_mode *hw_path_mode)
{
	struct ds_overscan overscan;
	struct ds_underscan_parameter parameter;
	struct timing_info_parameter timing_info;
	enum hw_scale_options underscan_avi_rule = HW_SCALE_OPTION_UNKNOWN;
	enum underscan_reason reason_for_underscan;

	build_avi_info_frame_parameter(
		param,
		NULL,
		container,
		hw_path_mode,
		reason,
		&hw_path_mode->mode.underscan_rule);

	switch (reason) {
	case BUILD_PATH_SET_REASON_FALLBACK_UNDERSCAN:
		reason_for_underscan = UNDERSCAN_REASON_FALL_BACK;
		break;
	case BUILD_PATH_SET_REASON_SET_MODE:
		reason_for_underscan = UNDERSCAN_REASON_PATCH_TIMING;
		break;
	case BUILD_PATH_SET_REASON_SET_ADJUSTMENT:
	default:
		reason_for_underscan = UNDERSCAN_REASON_SET_ADJUSTMENT;
		break;
	}

	if (!can_scaling_be_applied(
		container,
		param->timing_standard,
		param->timing_source,
		param->adjust_id,
		reason_for_underscan))
		return false;

	if (param->flags.bits.IS_UNDERSCAN_DESC == 0) {
		if (param->flags.bits.IS_FOR_SET_MODE == 1 &&
			param->adjust_id != ADJ_ID_UNDERSCAN)
			return false;

		if (is_dp_underscan_disabled(
			hw_path_mode->display_path,
			param->display_index))
			return false;

		setup_parameters(
			hw_path_mode,
			param,
			&overscan,
			&parameter,
			&timing_info);
		if (!build_underscan_parameters(
			param,
			container,
			reason,
			&parameter,
			&timing_info,
			&overscan))
			return false;
	} else {
		dal_memset(
			&parameter,
			0,
			sizeof(struct ds_underscan_parameter));
		dal_memset(
			&timing_info,
			0,
			sizeof(struct timing_info_parameter));
		dal_memset(&overscan, 0, sizeof(struct ds_overscan));
		timing_info.timing = hw_path_mode->mode.timing;
		if (!setup_parameter(
			hw_path_mode,
			param,
			&parameter))
			return false;

		if (!calculate_underscan(
			&parameter,
			&timing_info.dst_width,
			&timing_info.dst_height,
			&overscan))
			return false;
	}

	build_avi_info_frame_parameter(
		param,
		&parameter,
		container,
		hw_path_mode,
		reason,
		&underscan_avi_rule);

	extract_parameters(
		param,
		&timing_info,
		&overscan,
		underscan_avi_rule,
		hw_path_mode);

	return true;
}

static bool prepare_underscan(
	struct ds_dispatch *ds,
	const struct path_mode *path_mode,
	const struct ds_adjustment_scaler *param,
	struct adj_container *container,
	const struct display_path *display_path,
	struct hw_underscan_adjustment_data **hw_underscan_data,
	struct hw_path_mode_set *hw_path_set)
{
	struct hw_underscan_adjustment hw_underscan_adj = { {0} };
	struct hw_path_mode *hw_path_mode;
	enum build_path_set_reason reason =
		BUILD_PATH_SET_REASON_SET_ADJUSTMENT;
	hw_path_set = dal_hw_path_mode_set_create();
	if (!hw_path_set)
		return false;
	if (!build_hw_path_set_for_adjustment(
		ds,
		param,
		display_path,
		path_mode->display_path_index,
		hw_path_set))
		return false;


	hw_path_mode = find_hw_path_mode(display_path, hw_path_set);
	if (!hw_path_mode)
		return false;

	if (!dal_scaler_adj_group_apply_scaling(
		param,
		container,
		reason,
		hw_path_mode))
		return false;
	dal_ds_dispatch_setup_info_frame(ds, path_mode, hw_path_mode);
	dal_memset(&hw_underscan_adj,
		0,
		sizeof(struct hw_underscan_adjustment));
	/* no need call it build_deflicker_adjustment */
	hw_underscan_adj.hw_overscan = hw_path_mode->mode.overscan;
	/*just create struct hw_underscan_adjustment_data is enough to use,
	 * no need a HWAdjustment */
	(*hw_underscan_data)->hw_adj_id = HW_ADJUSTMENT_ID_OVERSCAN;
	(*hw_underscan_data)->hw_underscan_adj = hw_underscan_adj;
	return true;
}

static enum ds_return set_underscan_adjustment(
	struct ds_dispatch *ds,
	struct display_path *display_path,
	enum adjustment_id adjust_id,
	int32_t value,
	const struct path_mode *path_mode,
	struct adj_container *container)
{
	enum ds_return result = DS_ERROR;
	struct hw_path_mode_set *hw_path_set = NULL;
	struct ds_adjustment_scaler scaler;
	struct hw_underscan_adjustment_data data = { 0 };
	struct hw_underscan_adjustment_data *hw_underscan_data = &data;

	hw_path_set = dal_hw_path_mode_set_create();
	if (!hw_path_set)
		return DS_ERROR;

	if (!dal_scaler_adj_group_build_scaler_parameter(
		path_mode,
		container,
		BUILD_PATH_SET_REASON_SET_ADJUSTMENT,
		adjust_id,
		value,
		NULL,
		display_path,
		&scaler)) {
		result = DS_ERROR;
		goto fail;
	}

	if (!prepare_underscan(
		ds,
		path_mode,
		&scaler,
		container,
		display_path,
		&hw_underscan_data,
		hw_path_set)) {
		result = DS_ERROR;
		goto fail;
	}

	if (dal_hw_sequencer_set_overscan_adj(
		ds->hwss,
		hw_path_set,
		hw_underscan_data) == HWSS_RESULT_OK)
		result = DS_SUCCESS;
	else if (dal_adj_info_set_update_cur_value(
			&container->adj_info_set,
			adjust_id,
			value))
		result = DS_SUCCESS;
	else
		result = DS_ERROR;
fail:
	dal_hw_path_mode_set_destroy(&hw_path_set);
	return result;
}

enum ds_return dal_scaler_adj_group_set_adjustment(
	struct ds_dispatch *ds,
	const uint32_t display_index,
	struct display_path *display_path,
	enum adjustment_id adjust_id,
	int32_t value)
{
	struct adj_container *container;
	struct path_mode_set_with_data *pms_wd;
	const struct path_mode *path_mode;
	enum ds_return result = DS_ERROR;
	const struct adjustment_info *adj_info;

	pms_wd = dal_ds_dispatch_get_active_pms_with_data(ds);
	if (NULL == pms_wd)
		return DS_ERROR;
	container = dal_ds_dispatch_get_adj_container_for_path(
		ds, display_index);
	if (NULL == container)
		return DS_ERROR;

	path_mode =
		dal_pms_with_data_get_path_mode_for_display_index(
			pms_wd,
			display_index);

	if (NULL == path_mode)
		return DS_ERROR;

	adj_info = dal_adj_info_set_get_adj_info(
		&container->adj_info_set,
		adjust_id);
	if (NULL == adj_info)
		return DS_ERROR;

	if (adj_info->adj_data.ranged.cur == value) {
		if (dal_adj_container_is_adjustment_committed(
			container,
			adjust_id))
			/* we have set the same adjustment, do nothing, bypass
			 */
			return DS_SUCCESS;
		else if (ADJ_ID_UNDERSCAN == adjust_id) {
			if (value == adj_info->adj_data.ranged.def &&
				value == adj_info->adj_data.ranged.def) {
				/* this is the initial call  since our setmode's
				 * default: no underscan. We don't need any 0
				 * underscan adjustment after setmode. only set
				 * committed.
				 */
				dal_adj_container_commit_adj(
					container,
					adjust_id);
				return DS_SUCCESS;
			}
		}
	}

	if (adj_info->adj_data.ranged.max < value ||
		adj_info->adj_data.ranged.min > value)
		return DS_ERROR;
	if (!dal_adj_info_set_update_cur_value(
		&container->adj_info_set,
		adjust_id,
		value))
		return DS_ERROR;
	if (adjust_id == ADJ_ID_UNDERSCAN || adjust_id == ADJ_ID_UNDERSCAN_TYPE)
		result = set_underscan_adjustment(
				ds,
				display_path,
				adjust_id,
				value,
				path_mode,
				container);
	else {
		dal_logger_write(ds->dal_context->logger,
			LOG_MAJOR_ERROR,
			LOG_MINOR_COMPONENT_DISPLAY_SERVICE,
			"adj_id isn't for scaler_adj_group");
		return DS_ERROR;
	}
	if (result != DS_ERROR)
		dal_adj_container_commit_adj(container, adjust_id);

	return result;
}

