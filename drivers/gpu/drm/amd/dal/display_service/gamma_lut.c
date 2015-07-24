/*****************************************************************************\
 *  Module Name:   GammaLUT.cpp
 *  Project:       DAL 2012 Rearchitecture
 *  Device:        EG and later
 *
 *  Description:   Implementation of GammaLUT class
 *
 *  Copyright (c) 2012 Advanced Micro Devices, Inc. (unpublished)
 *
 *  All rights reserved.  This notice is intended as a precaution against
 *  inadvertent publication and does not imply publication or any waiver
 *  of confidentiality.  The year included in the foregoing notice is the
 *  year of creation of the work.
 *
 \*****************************************************************************/

#include "dal_services.h"
#include "display_service/ds_dispatch.h"
#include "display_service/ds_translation.h"
#include "display_service/grph_colors_group.h"
#include "include/hw_sequencer_interface.h"
#include "include/display_path_interface.h"
#include "include/adjustment_interface.h"
#include "gamma_lut.h"

static bool grph_gamma_lut_group_construct(
		struct grph_gamma_lut_group *grph_gamma_adj,
		struct grph_gamma_lut_group_init_data *init_data) {
	if (!init_data)
		return false;

	grph_gamma_adj->ds = init_data->ds;
	grph_gamma_adj->hws = init_data->hws;
	grph_gamma_adj->dal_context = init_data->dal_context;

	return true;
}

struct grph_gamma_lut_group *dal_gamma_adj_group_create(
		struct grph_gamma_lut_group_init_data *init_data) {
	struct grph_gamma_lut_group *grph_gamma_adj = NULL;

	grph_gamma_adj = dal_alloc(sizeof(*grph_gamma_adj));

	if (!grph_gamma_adj)
		return NULL;

	if (grph_gamma_lut_group_construct(grph_gamma_adj, init_data))
		return grph_gamma_adj;

	dal_free(grph_gamma_adj);

	return NULL;
}

static bool update_internal_status(
		struct ds_dispatch *ds,
		enum adjustment_id adj_id,
		const struct raw_gamma_ramp *gamma)
{
	bool ret = false;
	struct ds_adjustment_status *status = NULL;

	if (ds == NULL)
		return ret;

	switch (adj_id) {
	case ADJ_ID_GAMMA_RAMP:
		status = &ds->grph_gamma_adj->status_gamma_ramp;
		break;
	case ADJ_ID_DRIVER_REQUESTED_GAMMA:
	default:
		break;
	}

	if (status != NULL) {
		status->bits.SET_TO_HARDWARE = 1;
		ret = true;
	}

	return ret;
}

enum ds_return dal_grph_gamma_lut_set_adjustment(
		struct ds_dispatch *ds,
		const struct display_path *disp_path,
		const struct path_mode *disp_path_mode,
		enum adjustment_id adj_id,
		const struct raw_gamma_ramp *gamma,
		const struct ds_regamma_lut *regumma_lut) {

	enum ds_return ret = DS_ERROR;
	struct hw_adjustment_gamma_ramp *hw_gamma_ramp = NULL;

	if (gamma == NULL)
		return ret;

	if (ds == NULL)
		return ret;

	do {
		/* TODO validate to compare if this gamma is already set! */
		if (disp_path == NULL)
			break;

		if (!dal_gamma_lut_validate(adj_id, gamma, true))
			break;

		hw_gamma_ramp = dal_alloc(
			sizeof(struct hw_adjustment_gamma_ramp));

		if (hw_gamma_ramp == NULL)
			break;

		if (adj_id == ADJ_ID_GAMMA_RAMP)
			dal_gamma_lut_set_current_gamma(
				ds,
				ADJ_ID_DRIVER_REQUESTED_GAMMA,
				gamma);

		dal_ds_translate_regamma_to_hw(
				regumma_lut,
				&hw_gamma_ramp->regamma);

		if (!dal_gamma_lut_translate_to_hw(
				ds, disp_path_mode,
				disp_path,
				gamma,
				hw_gamma_ramp))
			break;

		hw_gamma_ramp->flag.uint = 0;
		hw_gamma_ramp->flag.bits.config_is_changed = 0;

		if (adj_id == ADJ_ID_GAMMA_RAMP_REGAMMA_UPDATE)
			hw_gamma_ramp->flag.bits.regamma_update = 1;
		else
			hw_gamma_ramp->flag.bits.gamma_update = 1;

		if (dal_hw_sequencer_set_gamma_ramp_adjustment(
				ds->hwss,
				disp_path,
				hw_gamma_ramp) != HWSS_RESULT_OK)
			break;

		if (adj_id == ADJ_ID_GAMMA_RAMP) {
			dal_gamma_lut_set_current_gamma(ds, adj_id, gamma);
			update_internal_status(ds, adj_id, gamma);
		}

		ret = DS_SUCCESS;

	} while (0);

	dal_free(hw_gamma_ramp);
	return ret;
}

bool dal_gamma_lut_validate(
		enum adjustment_id adj_id,
		const struct raw_gamma_ramp *gamma,
		bool validate_all) {
	if (adj_id != ADJ_ID_DRIVER_REQUESTED_GAMMA
			&& adj_id != ADJ_ID_GAMMA_RAMP
			&& adj_id != ADJ_ID_GAMMA_RAMP_REGAMMA_UPDATE)
		return false;

	if (!validate_all)
		return true;

	if (gamma == NULL)
		return false;

	if (gamma->type != GAMMA_RAMP_TYPE_RGB256
			&& gamma->type != GAMMA_RAMP_TYPE_FIXED_POINT)
		return false;

	if (gamma->type == GAMMA_RAMP_TYPE_RGB256
			&& gamma->size != sizeof(gamma->rgb_256))
		return false;

	return true;
}

bool dal_gamma_lut_translate_to_hw(
		struct ds_dispatch *ds,
		const struct path_mode *disp_path_mode,
		const struct display_path *disp_path,
		const struct raw_gamma_ramp *gamma_in,
		struct hw_adjustment_gamma_ramp *gamma_out) {
	unsigned int i;
	enum pixel_format pix_format = disp_path_mode->pixel_format;
	enum ds_color_space color_space = DS_COLOR_SPACE_UNKNOWN;

	uint32_t display_index;
	struct adj_container *adj_container = NULL;

	if (!disp_path)
		return false;

	display_index = dal_display_path_get_display_index(disp_path);

	adj_container = dal_ds_dispatch_get_adj_container_for_path(ds,
		display_index);

	if (gamma_in == NULL)
		return false;

	/* translate the PixelFormat */
	gamma_out->surface_pixel_format = pix_format;

	if (gamma_in->type != GAMMA_RAMP_TYPE_RGB256)
		return false;

	gamma_out->type = HW_GAMMA_RAMP_RBG_256x3x16;
	gamma_out->size = sizeof(gamma_out->gamma_ramp_rgb256x3x16);

	/* copy the rgb */
	for (i = 0; i < NUM_OF_RAW_GAMMA_RAMP_RGB_256; i++) {
		gamma_out->gamma_ramp_rgb256x3x16.red[i] =
				(unsigned short) (gamma_in->rgb_256[i].red);
		gamma_out->gamma_ramp_rgb256x3x16.green[i] =
				(unsigned short) (gamma_in->rgb_256[i].green);
		gamma_out->gamma_ramp_rgb256x3x16.blue[i] =
				(unsigned short) (gamma_in->rgb_256[i].blue);
	}

	/*
	 * logic below builds the color space and it is used for color
	 * adjustments also
	 */
	color_space = dal_grph_colors_group_get_color_space(
			ds->grph_colors_adj,
			&disp_path_mode->mode_timing->crtc_timing,
			disp_path,
			adj_container);

	gamma_out->color_space =
		dal_ds_translation_hw_color_space_from_color_space(color_space);

	return true;
}

static bool get_parameters(
		struct ds_dispatch *ds,
		enum adjustment_id adj_id,
		struct ds_adjustment_status **adjustment_status,
		struct raw_gamma_ramp **gamma)
{
	struct ds_adjustment_status *status = NULL;
	struct raw_gamma_ramp *ramp = NULL;

	if (ds == NULL)
		return false;

	if (ds->grph_gamma_adj == NULL)
		return false;

	switch (adj_id) {
	case ADJ_ID_GAMMA_RAMP:
		status = &ds->grph_gamma_adj->status_gamma_ramp;
		ramp = &ds->grph_gamma_adj->gamma_ramp;
		break;
	case ADJ_ID_DRIVER_REQUESTED_GAMMA:
		status = &ds->grph_gamma_adj->status_original_ramp;
		ramp = &ds->grph_gamma_adj->oroginal_ramp;
		break;

	default:
		break;
	}

	if (status != NULL && gamma != NULL) {
		if (adjustment_status != NULL)
			*adjustment_status = status;

		if (gamma != NULL)
			*gamma = ramp;

		return true;
	}

	return false;
}

static bool generated_default_gamma_ramp(
		struct ds_dispatch *ds,
		enum adjustment_id adj_id)
{
	bool ret = false;
	unsigned int i;

	struct raw_gamma_ramp_rgb *rgb256 = NULL;
	struct ds_adjustment_status *status = NULL;
	struct raw_gamma_ramp *gamma = NULL;

	if (ds == NULL)
		return false;

	switch (adj_id) {
	case ADJ_ID_GAMMA_RAMP:
		gamma = &ds->grph_gamma_adj->gamma_ramp;
		status = &ds->grph_gamma_adj->status_gamma_ramp;
		rgb256 = ds->grph_gamma_adj->gamma_ramp.rgb_256;
		ret = true;
		break;
	case ADJ_ID_DRIVER_REQUESTED_GAMMA:
		gamma = &ds->grph_gamma_adj->oroginal_ramp;
		status = &ds->grph_gamma_adj->status_original_ramp;
		rgb256 = ds->grph_gamma_adj->oroginal_ramp.rgb_256;
		ret = true;
		break;
	default:
		break;
	}

	if (ret) {
		for (i = 0; i < NUM_OF_RAW_GAMMA_RAMP_RGB_256; ++i) {
			rgb256[i].red = i << 8;
			rgb256[i].green = i << 8;
			rgb256[i].blue = i << 8;
		}
		status->val = 0;
		status->bits.SET_TO_DEFAULT = 1;
		gamma->type = GAMMA_RAMP_TYPE_RGB256;
		gamma->size = sizeof(struct raw_gamma_ramp_rgb)
				* NUM_OF_RAW_GAMMA_RAMP_RGB_256;
	}
	return ret;
}

const struct raw_gamma_ramp *dal_gamma_lut_get_current_gamma(
		struct ds_dispatch *ds,
		enum adjustment_id adj_id) {
	struct ds_adjustment_status *adjustment_status = NULL;
	struct raw_gamma_ramp *gamma = NULL;

	if (ds == NULL)
		return NULL;

	if (!dal_gamma_lut_validate(adj_id, gamma, false))
		return NULL;

	if (get_parameters(ds, adj_id, &adjustment_status, &gamma))
		return gamma;

	if (adjustment_status->bits.SET_FROM_EXTERNAL == 0)
		if (generated_default_gamma_ramp(ds, adj_id))
			return gamma;

	return gamma;
}

bool dal_gamma_lut_set_current_gamma(struct ds_dispatch *ds,
		enum adjustment_id adj_id,
		const struct raw_gamma_ramp *gamma)
{
	struct ds_adjustment_status *adjustment_status = NULL;
	struct raw_gamma_ramp *ramp = NULL;

	if (!dal_gamma_lut_validate(adj_id, gamma, true))
		return false;

	if (!get_parameters(ds, adj_id, &adjustment_status, &ramp))
		return false;

	dal_memmove(ramp, gamma, sizeof(struct raw_gamma_ramp));

	/* new external gamma was set , reset to 0 default flag */
	adjustment_status->bits.SET_TO_DEFAULT = 0;
	/* new external gamma was set , raise this flag */
	adjustment_status->bits.SET_FROM_EXTERNAL = 1;
	/* new external gamma was set , raise this flag */
	adjustment_status->bits.SET_TO_HARDWARE = 0;

	return true;

}
static void destruct(struct grph_gamma_lut_group *gamma_adj)
{
}

void dal_grph_gamma_adj_group_destroy(
		struct grph_gamma_lut_group **grph_gamma_adj) {
	if (grph_gamma_adj == NULL || *grph_gamma_adj == NULL)
		return;

	destruct(*grph_gamma_adj);
	dal_free(*grph_gamma_adj);
	*grph_gamma_adj = NULL;
}

