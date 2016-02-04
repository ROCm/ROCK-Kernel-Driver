/*
 * gamma_calcs.h
 *
 *  Created on: Feb 9, 2016
 *      Author: yonsun
 */

#ifndef DRIVERS_GPU_DRM_AMD_DAL_DEV_DC_INC_GAMMA_CALCS_H_
#define DRIVERS_GPU_DRM_AMD_DAL_DEV_DC_INC_GAMMA_CALCS_H_

#include "opp.h"
#include "core_types.h"
#include "dc.h"

struct temp_params {
	struct hw_x_point coordinates_x[256 + 3];
	struct pwl_float_data rgb_user[FLOAT_GAMMA_RAMP_MAX + 3];
	struct pwl_float_data_ex rgb_regamma[256 + 3];
	struct pwl_float_data rgb_oem[FLOAT_GAMMA_RAMP_MAX + 3];
	struct gamma_pixel axix_x_256[256];
	struct pixel_gamma_point coeff128_oem[256 + 3];
	struct pixel_gamma_point coeff128[256 + 3];

};

void calculate_regamma_params(struct regamma_params *params,
		struct temp_params *temp_params,
		const struct core_gamma *ramp,
		const struct core_surface *surface);

#endif /* DRIVERS_GPU_DRM_AMD_DAL_DEV_DC_INC_GAMMA_CALCS_H_ */
