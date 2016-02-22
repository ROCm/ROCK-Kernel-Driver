/*
 * Copyright 2012-16 Advanced Micro Devices, Inc.
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

/* FPGA Diagnostics version of AS HW CTX. */

#include "dm_services.h"

#include "../hw_ctx_adapter_service.h"

#include "hw_ctx_adapter_service_diag.h"

#include "include/logger_interface.h"
#include "include/grph_object_id.h"

static const struct graphics_object_id invalid_go = {
	0, ENUM_ID_UNKNOWN, OBJECT_TYPE_UNKNOWN
};

static void destroy(
	struct hw_ctx_adapter_service *hw_ctx)
{
}

static bool power_up(
	struct hw_ctx_adapter_service *hw_ctx)
{
	return true;
}

static struct graphics_object_id enum_fake_path_resource(
	const struct hw_ctx_adapter_service *hw_ctx,
	uint32_t index)
{
	return invalid_go;
}

static struct graphics_object_id enum_stereo_sync_object(
	const struct hw_ctx_adapter_service *hw_ctx,
	uint32_t index)
{
	return invalid_go;
}

static struct graphics_object_id enum_sync_output_object(
	const struct hw_ctx_adapter_service *hw_ctx,
	uint32_t index)
{
	return invalid_go;
}

static struct graphics_object_id enum_audio_object(
	const struct hw_ctx_adapter_service *hw_ctx,
	uint32_t index)
{
	return invalid_go;
}

static void update_audio_connectivity(
	struct hw_ctx_adapter_service *hw_ctx,
	uint32_t number_of_audio_capable_display_path,
	uint32_t number_of_controllers)
{
}

static const struct hw_ctx_adapter_service_funcs funcs = {
	destroy,
	power_up,
	enum_fake_path_resource,
	enum_stereo_sync_object,
	enum_sync_output_object,
	enum_audio_object,
	update_audio_connectivity
};

static bool construct(
	struct hw_ctx_adapter_service *hw_ctx,
	struct dc_context *ctx)

{
	if (!dal_adapter_service_construct_hw_ctx(hw_ctx, ctx)) {
		ASSERT_CRITICAL(false);
		return false;
	}

	hw_ctx->funcs = &funcs;

	return true;
}

struct hw_ctx_adapter_service *dal_adapter_service_create_hw_ctx_diag(
	struct dc_context *ctx)
{
	struct hw_ctx_adapter_service *hw_ctx = dm_alloc(sizeof(*hw_ctx));

	if (!hw_ctx) {
		ASSERT_CRITICAL(false);
		return NULL;
	}

	if (construct(hw_ctx, ctx))
		return hw_ctx;

	ASSERT_CRITICAL(false);

	dm_free(hw_ctx);

	return NULL;
}

/*****************************************************************************/
