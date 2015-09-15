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

#ifndef __DAL_TM_RESOURCE_H__
#define __DAL_TM_RESOURCE_H__

/* Generic structure to hold interface to
 * HW resource + TM proprietary info + resource-specific info */
struct tm_resource_flags {
	uint8_t resource_active:1;
	uint8_t display_path_resource:1;
	uint8_t mst_resource:1;
	uint8_t multi_path:1;
};

struct tm_resource;

/* These functions perform operations SPECIFIC for a type of resource.  */
struct tm_resource_funcs {
	void (*destroy)(struct tm_resource **resource);
	struct tm_resource * (*clone)(struct tm_resource *resource);
	void (*release_hw)(struct tm_resource *resource);
	const struct graphics_object_id (*get_grph_id)(
		const struct tm_resource *resource);
	uint32_t (*get_priority)(const struct tm_resource *resource);
	void (*set_multi_path)(
		struct tm_resource *resource,
		bool is_multi_path);
	bool (*is_sharable)(const struct tm_resource *resource);
};

/* Reference count functions - these operations IDENTICAL for all types of
 * resources. */
void tm_res_ref_counter_increment(struct tm_resource *resource);
void tm_res_ref_counter_decrement(struct tm_resource *resource);
uint32_t tm_res_ref_counter_get(const struct tm_resource *resource);
void tm_res_ref_counter_reset(struct tm_resource *resource);
/**** end of reference count functions ****/


struct tm_resource_private;

struct tm_resource {
	const struct tm_resource_funcs *funcs;

	/* private data of tm_resource */
	struct tm_resource_private *res_private;

	struct tm_resource_flags flags;
};


struct tm_resource_connector_info {
	struct tm_resource resource;
	struct ddc_service *ddc_service;
	struct connector *connector;
};

struct tm_resource_encoder_info {
	struct tm_resource resource;
	struct encoder *encoder;
	uint32_t paired_encoder_index;
};

struct tm_resource_controller_info {
	struct tm_resource resource;
	struct controller *controller;
	enum tm_power_gate_state power_gating_state;
};

struct tm_resource_clock_source_info {
	struct tm_resource resource;
	struct clock_source *clock_source;
	enum clock_sharing_group clk_sharing_group;
};

struct tm_resource_engine_info {
	struct tm_resource resource;
	enum tm_engine_priority priority;
	struct graphics_object_id id;
};

struct tm_resource_audio_info {
	struct tm_resource resource;
	struct audio *audio;
};

struct tm_resource *dal_tm_resource_encoder_create(struct encoder *enc);
struct tm_resource *dal_tm_resource_audio_create(struct audio *audio);
struct tm_resource *dal_tm_resource_engine_create(struct graphics_object_id id);
struct tm_resource *dal_tm_resource_clock_source_create(
	struct clock_source *cs);
struct tm_resource *dal_tm_resource_connector_create(struct connector *conn);
struct tm_resource *dal_tm_resource_controller_create(struct controller *cntl);

#define GRPH_ID(resource) ((resource)->funcs->get_grph_id((resource)))

#define TO_CONNECTOR_INFO(tm_resource)\
	(container_of((tm_resource),\
		struct tm_resource_connector_info, resource))

#define TO_CONNECTOR(tm_resource) (TO_CONNECTOR_INFO(tm_resource)->connector)

#define TO_CONTROLLER_INFO(tm_resource)\
	(container_of((tm_resource),\
		struct tm_resource_controller_info, resource))

#define TO_CONTROLLER(tm_resource) (TO_CONTROLLER_INFO(tm_resource)->controller)

#define TO_ENCODER_INFO(tm_resource)\
	(container_of((tm_resource),\
		struct tm_resource_encoder_info, resource))

#define TO_ENCODER(tm_resource) (TO_ENCODER_INFO(tm_resource)->encoder)

#define TO_CLOCK_SOURCE_INFO(tm_resource)\
	(container_of((tm_resource),\
		struct tm_resource_clock_source_info, resource))
#define TO_CLOCK_SOURCE(tm_resource)\
	(TO_CLOCK_SOURCE_INFO(tm_resource)->clock_source)

#define TO_ENGINE_INFO(tm_resource)\
	(container_of((tm_resource),\
		struct tm_resource_engine_info, resource))

#define TO_AUDIO_INFO(tm_resource)\
	(container_of((tm_resource),\
		struct tm_resource_audio_info, resource))
#define TO_AUDIO(tm_resource) (TO_AUDIO_INFO(tm_resource)->audio)


#define TM_RES_REF_CNT_INCREMENT(resource) \
	(tm_res_ref_counter_increment(resource))
#define TM_RES_REF_CNT_DECREMENT(resource) \
	(tm_res_ref_counter_decrement(resource))
#define TM_RES_REF_CNT_GET(resource) \
	(tm_res_ref_counter_get(resource))
#define TM_RES_REF_CNT_RESET(resource) \
	(tm_res_ref_counter_reset(resource))

#endif
