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

#include "include/encoder_interface.h"
#include "include/connector_interface.h"
#include "include/controller_interface.h"
#include "include/clock_source_interface.h"
#include "include/audio_interface.h"

#include "tm_resource_mgr.h"
#include "tm_resource.h"

struct tm_resource_private {
	uint32_t ref_counter;
	bool cloned;
};

void tm_res_ref_counter_increment(struct tm_resource *resource)
{
	++resource->res_private->ref_counter;
}

void tm_res_ref_counter_decrement(struct tm_resource *resource)
{
	--resource->res_private->ref_counter;
}

uint32_t tm_res_ref_counter_get(const struct tm_resource *resource)
{
	return resource->res_private->ref_counter;
}

void tm_res_ref_counter_reset(struct tm_resource *resource)
{
	resource->res_private->ref_counter = 0;
}

static bool allocate_tm_resource_private(struct tm_resource_private **output)
{
	struct tm_resource_private *priv;

	priv = dal_alloc(sizeof(*priv));
	if (NULL == priv)
		return false;

	*output = priv;

	return true;
}

static uint32_t clock_source_get_priority(const struct tm_resource *resource)
{
	return dal_clock_source_is_clk_src_with_fixed_freq(
		TO_CLOCK_SOURCE(resource)) ? 0 : 1;
}

static uint32_t encoder_get_priority(const struct tm_resource *resource)
{
	struct encoder_feature_support feature;

	/* Encoder has priority based on internal/external property.
	 * Internal has higher priority - lower value. */

	feature = dal_encoder_get_supported_features(
			TO_ENCODER(resource));

	return feature.flags.bits.EXTERNAL_ENCODER ? 1 : 0;
}

static void connector_destroy(struct tm_resource **resource)
{
	struct tm_resource_connector_info *info;
	struct tm_resource_private *priv;

	info = TO_CONNECTOR_INFO(*resource);
	priv = info->resource.res_private;

	if (!priv->cloned) {
		dal_connector_destroy(&info->connector);
		dal_ddc_service_destroy(&info->ddc_service);
	}

	dal_free(priv);
	dal_free(info);
	*resource = NULL;
}

static void engine_destroy(struct tm_resource **resource)
{
	struct tm_resource_engine_info *info;

	info = TO_ENGINE_INFO(*resource);
	dal_free((*resource)->res_private);
	dal_free(info);
	*resource = NULL;
}

#define TM_RES_DESTROY(type, TYPE)\
static void type##_destroy(struct tm_resource **resource)\
{\
	struct tm_resource_##type##_info *info;\
	struct tm_resource_private *priv;\
	\
	if (!resource || !*resource)\
		return;\
	\
	info = TO_##TYPE##_INFO(*resource);\
	priv = info->resource.res_private; \
	\
	if (!priv->cloned)\
		dal_##type##_destroy(&info->type);\
	\
	dal_free(priv);\
	dal_free(info);\
	*resource = NULL;\
}

#define TM_RES_CLONE(type, TYPE)\
static struct tm_resource *type ## _clone(\
	struct tm_resource *resource)\
{\
	struct tm_resource_ ## type ## _info *info = dal_alloc(sizeof(*info));\
	struct tm_resource_private *priv;\
	\
	if (NULL == info) \
		return NULL; \
	\
	*info = *TO_ ## TYPE ## _INFO(resource);\
	\
	if (!allocate_tm_resource_private(&info->resource.res_private)) { \
		dal_free(info); \
		return NULL; \
	} \
	\
	priv = info->resource.res_private; \
	\
	*priv = *(resource->res_private);\
	priv->ref_counter = 0;\
	priv->cloned = true;\
	return &info->resource;\
}

#define GET_GRPH_ID(type, TYPE)\
static const struct graphics_object_id type##_get_grph_id(\
	const struct tm_resource *resource)\
{\
	return dal_##type##_get_graphics_object_id(TO_##TYPE(resource));\
}

TM_RES_CLONE(connector, CONNECTOR)
TM_RES_CLONE(controller, CONTROLLER)
TM_RES_CLONE(encoder, ENCODER)
TM_RES_CLONE(audio, AUDIO)
TM_RES_CLONE(clock_source, CLOCK_SOURCE)
TM_RES_CLONE(engine, ENGINE)
TM_RES_DESTROY(controller, CONTROLLER)
TM_RES_DESTROY(encoder, ENCODER)
TM_RES_DESTROY(audio, AUDIO)
TM_RES_DESTROY(clock_source, CLOCK_SOURCE)
GET_GRPH_ID(connector, CONNECTOR)
GET_GRPH_ID(controller, CONTROLLER)
GET_GRPH_ID(encoder, ENCODER)
GET_GRPH_ID(audio, AUDIO)
GET_GRPH_ID(clock_source, CLOCK_SOURCE)

static const struct graphics_object_id engine_get_grph_id(
	const struct tm_resource *resource)
{
	return TO_ENGINE_INFO(resource)->id;
}

static void empty_release_hw(struct tm_resource *resource)
{
}

static void encoder_release_hw(struct tm_resource *resource)
{
	dal_encoder_release_hw(TO_ENCODER(resource));
}

static uint32_t empty_get_priority(const struct tm_resource *resource)
{
	return 0;
}

static bool clock_source_is_sharable(const struct tm_resource *resource)
{
	return true;
}

static bool empty_is_sharable(const struct tm_resource *resource)
{
	return false;
}

static void encoder_set_multi_path(
	struct tm_resource *resource,
	bool is_multi_path)
{
	dal_encoder_set_multi_path(TO_ENCODER(resource), is_multi_path);
}

static void empty_set_multi_path(
	struct tm_resource *resource,
	bool is_multi_path)
{

}

static const struct tm_resource_funcs connector_funcs = {
	.destroy = connector_destroy,
	.get_grph_id = connector_get_grph_id,
	.release_hw = empty_release_hw,
	.clone = connector_clone,
	.get_priority = empty_get_priority,
	.set_multi_path = empty_set_multi_path,
	.is_sharable = empty_is_sharable
};

static const struct tm_resource_funcs controller_funcs = {
	.destroy = controller_destroy,
	.get_grph_id = controller_get_grph_id,
	.release_hw = empty_release_hw,
	.clone = controller_clone,
	.get_priority = empty_get_priority,
	.set_multi_path = empty_set_multi_path,
	.is_sharable = empty_is_sharable
};

static const struct tm_resource_funcs encoder_funcs = {
	.destroy = encoder_destroy,
	.get_grph_id = encoder_get_grph_id,
	.release_hw = encoder_release_hw,
	.clone = encoder_clone,
	.get_priority = encoder_get_priority,
	.set_multi_path = encoder_set_multi_path,
	.is_sharable = empty_is_sharable
};

static const struct tm_resource_funcs clock_source_funcs = {
	.destroy = clock_source_destroy,
	.get_grph_id = clock_source_get_grph_id,
	.release_hw = empty_release_hw,
	.clone = clock_source_clone,
	.get_priority = clock_source_get_priority,
	.set_multi_path = empty_set_multi_path,
	.is_sharable = clock_source_is_sharable
};

static const struct tm_resource_funcs audio_funcs = {
	.destroy = audio_destroy,
	.get_grph_id = audio_get_grph_id,
	.release_hw = empty_release_hw,
	.clone = audio_clone,
	.get_priority = empty_get_priority,
	.set_multi_path = empty_set_multi_path,
	.is_sharable = empty_is_sharable
};

static const struct tm_resource_funcs engine_funcs = {
	.destroy = engine_destroy,
	.get_grph_id = engine_get_grph_id,
	.release_hw = empty_release_hw,
	.clone = engine_clone,
	.get_priority = empty_get_priority,
	.set_multi_path = empty_set_multi_path,
	.is_sharable = empty_is_sharable
};

struct tm_resource *dal_tm_resource_encoder_create(struct encoder *enc)
{
	struct tm_resource_encoder_info *info = dal_alloc(sizeof(*info));

	if (NULL == info)
		return NULL;

	if (!allocate_tm_resource_private(&info->resource.res_private)) {
		dal_free(info);
		return NULL;
	}

	info->paired_encoder_index = RESOURCE_INVALID_INDEX;
	info->encoder = enc;
	info->resource.funcs = &encoder_funcs;
	return &info->resource;
}

struct tm_resource *dal_tm_resource_controller_create(struct controller *cntl)
{
	struct tm_resource_controller_info *info = dal_alloc(sizeof(*info));

	if (NULL == info)
		return NULL;

	if (!allocate_tm_resource_private(&info->resource.res_private)) {
		dal_free(info);
		return NULL;
	}

	info->power_gating_state = TM_POWER_GATE_STATE_NONE;
	info->controller = cntl;
	info->resource.funcs = &controller_funcs;
	return &info->resource;
}

struct tm_resource *dal_tm_resource_connector_create(struct connector *conn)
{
	struct tm_resource_connector_info *info = dal_alloc(sizeof(*info));

	if (NULL == info)
		return NULL;

	if (!allocate_tm_resource_private(&info->resource.res_private)) {
		dal_free(info);
		return NULL;
	}

	info->connector = conn;
	info->resource.funcs = &connector_funcs;
	return &info->resource;
}

struct tm_resource *dal_tm_resource_clock_source_create(struct clock_source *cs)
{
	struct tm_resource_clock_source_info *info = dal_alloc(sizeof(*info));

	if (NULL == info)
		return NULL;

	if (!allocate_tm_resource_private(&info->resource.res_private)) {
		dal_free(info);
		return NULL;
	}

	info->clk_sharing_group = CLOCK_SHARING_GROUP_EXCLUSIVE;
	info->clock_source = cs;
	info->resource.funcs = &clock_source_funcs;
	return &info->resource;
}

struct tm_resource *dal_tm_resource_engine_create(struct graphics_object_id id)
{
	struct tm_resource_engine_info *info = dal_alloc(sizeof(*info));

	if (NULL == info)
		return NULL;

	if (!allocate_tm_resource_private(&info->resource.res_private)) {
		dal_free(info);
		return NULL;
	}

	info->id = id;
	info->resource.funcs = &engine_funcs;
	return &info->resource;
}

struct tm_resource *dal_tm_resource_audio_create(struct audio *audio)
{
	struct tm_resource_audio_info *info = dal_alloc(sizeof(*info));

	if (NULL == info)
		return NULL;

	if (!allocate_tm_resource_private(&info->resource.res_private)) {
		dal_free(info);
		return NULL;
	}

	info->audio = audio;
	info->resource.funcs = &audio_funcs;
	return &info->resource;
}
