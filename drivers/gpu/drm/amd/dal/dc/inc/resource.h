/*
 * Copyright 2015 Advanced Micro Devices, Inc.
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
 */

#ifndef DRIVERS_GPU_DRM_AMD_DAL_DEV_DC_INC_RESOURCE_H_
#define DRIVERS_GPU_DRM_AMD_DAL_DEV_DC_INC_RESOURCE_H_

#include "core_types.h"
#include "core_status.h"
#include "core_dc.h"

/* TODO unhardcode, 4 for CZ*/
#define MEMORY_TYPE_MULTIPLIER 4

bool dc_construct_resource_pool(struct adapter_service *adapter_serv,
				struct dc *dc,
				uint8_t num_virtual_links);

void build_scaling_params(
	const struct dc_surface *surface,
	struct core_stream *stream);

void build_scaling_params_for_context(
	const struct dc *dc,
	struct validate_context *context);

void unreference_clock_source(
		struct resource_context *res_ctx,
		struct clock_source *clock_source);

void reference_clock_source(
		struct resource_context *res_ctx,
		struct clock_source *clock_source);

bool is_same_timing(
	const struct dc_crtc_timing *timing1,
	const struct dc_crtc_timing *timing2);

struct clock_source *find_used_clk_src_for_sharing(
		struct validate_context *context,
		struct core_stream *stream);

bool logical_attach_surfaces_to_target(
		struct dc_surface *surfaces[],
		uint8_t surface_count,
		struct dc_target *dc_target);

void pplib_apply_safe_state(const struct dc *dc);

void pplib_apply_display_requirements(
	const struct dc *dc,
	const struct validate_context *context);

void build_info_frame(struct core_stream *stream);

enum dc_status map_resources(
	const struct dc *dc,
	struct validate_context *context);

#endif /* DRIVERS_GPU_DRM_AMD_DAL_DEV_DC_INC_RESOURCE_H_ */
