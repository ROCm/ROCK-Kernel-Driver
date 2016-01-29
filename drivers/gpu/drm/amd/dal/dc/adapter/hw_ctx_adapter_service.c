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

#include "dm_services.h"
#include "include/adapter_service_types.h"
#include "include/grph_object_id.h"
#include "hw_ctx_adapter_service.h"

static const struct graphics_object_id invalid_go = {
	0, ENUM_ID_UNKNOWN, OBJECT_TYPE_UNKNOWN
};

static void destroy(
	struct hw_ctx_adapter_service *hw_ctx)
{
	/* Attention!
	 * You must override impl method in derived class */
	BREAK_TO_DEBUGGER();
}

static bool power_up(
	struct hw_ctx_adapter_service *hw_ctx)
{
	/* Attention!
	 * You must override impl method in derived class */
	BREAK_TO_DEBUGGER();

	return false;
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
	/* by default, we only allow one audio */

	if (index > 0)
		return invalid_go;
	else if (hw_ctx->cached_audio_straps == AUDIO_STRAPS_NOT_ALLOWED)
		return invalid_go;
	else
		return dal_graphics_object_id_init(
			AUDIO_ID_INTERNAL_AZALIA,
			ENUM_ID_1,
			OBJECT_TYPE_AUDIO);
}

static void update_audio_connectivity(
	struct hw_ctx_adapter_service *hw_ctx,
	uint32_t number_of_audio_capable_display_path,
	uint32_t number_of_controllers)
{
	/* Attention!
	 * You must override impl method in derived class */
	BREAK_TO_DEBUGGER();
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

bool dal_adapter_service_construct_hw_ctx(
	struct hw_ctx_adapter_service *hw_ctx,
	struct dc_context *ctx)
{

	hw_ctx->ctx = ctx;
	hw_ctx->funcs = &funcs;
	hw_ctx->cached_audio_straps = AUDIO_STRAPS_NOT_ALLOWED;

	return true;
}

union audio_support dal_adapter_service_hw_ctx_get_audio_support(
	const struct hw_ctx_adapter_service *hw_ctx)
{
	union audio_support result;

	result.raw = 0;

	switch (hw_ctx->cached_audio_straps) {
	case AUDIO_STRAPS_DP_HDMI_AUDIO:
		result.bits.HDMI_AUDIO_NATIVE = true;
		/* do not break ! */
	case AUDIO_STRAPS_DP_HDMI_AUDIO_ON_DONGLE:
		result.bits.HDMI_AUDIO_ON_DONGLE = true;
		/* do not break ! */
	case AUDIO_STRAPS_DP_AUDIO_ALLOWED:
		result.bits.DP_AUDIO = true;
		break;
	default:
		break;
	}

	return result;
}

void dal_adapter_service_destruct_hw_ctx(
	struct hw_ctx_adapter_service *hw_ctx)
{
	/* There is nothing to destruct at the moment */
}

void dal_adapter_service_destroy_hw_ctx(
	struct hw_ctx_adapter_service **ptr)
{
	if (!ptr || !*ptr) {
		BREAK_TO_DEBUGGER();
		return;
	}

	(*ptr)->funcs->destroy(*ptr);

	*ptr = NULL;
}
