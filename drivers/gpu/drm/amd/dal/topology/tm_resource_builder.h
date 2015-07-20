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


#ifndef __DAL_TM_RESOURCE_BUILDER_H__
#define __DAL_TM_RESOURCE_BUILDER_H__

/* Internal includes */
#include "tm_resource_mgr.h"


/**
 *****************************************************************************
 * TM Resource Builder
 *
 *	TM Resource Builder responsible for creating display paths and
 *	objects for HW resources
 *	Typically this class needed only during initialisation
 *	phase of Topology Manager
 *****************************************************************************
 */

/* structures */
struct tm_resource_builder_init_data {
	struct dal_context *dal_context;
	struct adapter_service *adapter_service;
	struct timing_service *timing_service;
	struct irq_manager *irq_manager;
	struct hw_sequencer *hwss;
	struct tm_resource_mgr *resource_mgr;
	struct topology_mgr *tm;
};

/* functions */
struct tm_resource_builder*
tm_resource_builder_create(
		const struct tm_resource_builder_init_data *init_data);

void tm_resource_builder_destroy(struct tm_resource_builder **tm_rb);

enum tm_result tm_resource_builder_create_gpu_resources(
		struct tm_resource_builder *tm_rb);

enum tm_result tm_resource_builder_build_display_paths(
		struct tm_resource_builder *tm_rb);

enum tm_result tm_resource_builder_add_fake_display_paths(
		struct tm_resource_builder *tm_rb);

enum tm_result tm_resource_builder_add_feature_resources(
		struct tm_resource_builder *tm_rb);

void tm_resource_builder_sort_display_paths(
		struct tm_resource_builder *tm_rb);

uint32_t tm_resource_builder_get_num_of_paths(
		struct tm_resource_builder *tm_rb);

struct display_path *tm_resource_builder_get_path_at(
		struct tm_resource_builder *tm_rb,
		uint32_t index);

#endif /* __DAL_TM_RESOURCE_BUILDER_H__ */
