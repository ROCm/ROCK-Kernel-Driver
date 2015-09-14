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
 */

#ifndef __DSAT__H
#define __DSAT__H
#if !defined(BUILD_DC_CORE)
#include <linux/types.h>

#include "amdgpu_dm.h"
#include "logger_interface.h"

void dsat_test_fill_data(struct amdgpu_display_manager *dm,
		struct dsat_test_in *test_in,
		struct dsat_test_out *test_out);

uint32_t dsat_logger_get_buffer_size(
		struct amdgpu_display_manager *dm);

uint32_t dsat_logger_set_buffer_size(
		struct amdgpu_display_manager *dm,
		uint32_t buffer_size);

uint32_t dsat_logger_get_flags(struct amdgpu_display_manager *dm);

void dsat_logger_set_flags(
		struct amdgpu_display_manager *dm,
		uint32_t flags);

uint32_t dsat_logger_get_mask(
		struct amdgpu_display_manager *dm,
		uint32_t lvl_major,
		uint32_t lvl_minor);

uint32_t dsat_logger_set_mask(
		struct amdgpu_display_manager *dm,
		uint32_t lvl_major,
		uint32_t lvl_minor);

uint32_t dsat_logger_get_masks(
		struct amdgpu_display_manager *dm,
		uint32_t lvl_major);

void dsat_logger_set_masks(
		struct amdgpu_display_manager *dm,
		uint32_t lvl_major,
		uint32_t mask);

uint32_t dsat_logger_unset_mask(
		struct amdgpu_display_manager *dm,
		uint32_t lvl_major,
		uint32_t lvl_minor);

uint32_t dsat_logger_read(struct amdgpu_display_manager *dm,
		uint32_t output_buffer_size, /* <[in] */
		char *output_buffer, /* >[out] */
		uint32_t *bytes_read, /* >[out] */
		bool single_line);

uint32_t dsat_logger_enum_major_info(
		struct amdgpu_display_manager *dm,
		void *info,
		uint32_t enum_index);

uint32_t dsat_logger_enum_minor_info(
		struct amdgpu_display_manager *dm,
		void *info,
		uint32_t major,
		uint32_t enum_index);

uint32_t dsat_read_hw_reg(
		struct amdgpu_display_manager *dm,
		uint32_t address);

void dsat_write_hw_reg(
		struct amdgpu_display_manager *dm,
		uint32_t address,
		uint32_t value);

uint32_t dsat_get_adapters_count(struct amdgpu_display_manager *dm);

void dsat_get_adapters_info(
		struct amdgpu_display_manager *dm,
		uint32_t adapter_index,
		struct dsat_adapter_info *adapter_info_data);

uint32_t dsat_get_displays_count(
		struct amdgpu_display_manager *dm,
		uint32_t adapter_index);

uint32_t dsat_get_displays_info(
		struct amdgpu_display_manager *dm,
		uint32_t adapter_index,
		uint32_t buffer_size,
		struct dsat_display_info *display_info_data);

uint32_t dsat_display_get_edid(
		struct amdgpu_display_manager *dm,
		uint32_t adapter_index,
		uint32_t display_index,
		uint32_t *buffer_size,
		uint8_t *edid_data);

void dsat_override_edid(
		struct amdgpu_display_manager *dm,
		uint32_t adapter_index,
		uint32_t display_index,
		struct dsat_display_edid_data *edid_data);

void dsat_get_adjustment_info(
		struct amdgpu_display_manager *dm,
		uint32_t adapter_index,
		uint32_t display_index,
		enum adjustment_id adjust_id,
		struct adjustment_info *adjust_info);

void dsat_set_saturation(
	struct amdgpu_display_manager *dm,
	uint32_t adapter_index,
	uint32_t display_index,
	uint32_t value);

void dsat_set_backlight(
	struct amdgpu_display_manager *dm,
	uint32_t adapter_index,
	uint32_t display_index,
	uint32_t value);

void dsat_set_bit_depth_reduction(
	struct amdgpu_display_manager *dm,
	uint32_t adapter_index,
	uint32_t display_index,
	uint32_t value);

void dsat_set_underscan(
	struct amdgpu_display_manager *dm,
	uint32_t adapter_index,
	uint32_t display_index,
	uint32_t value);

void dsat_get_saturation(
	struct amdgpu_display_manager *dm,
	uint32_t adapter_index,
	uint32_t display_index,
	struct dsat_adjustment_data *adj_value);

void dsat_get_backlight(
	struct amdgpu_display_manager *dm,
	uint32_t adapter_index,
	uint32_t display_index,
	struct dsat_adjustment_data *adj_value);

void dsat_get_bit_depth_reduction(
	struct amdgpu_display_manager *dm,
	uint32_t adapter_index,
	uint32_t display_index,
	struct dsat_adjustment_data *adj_value);

void dsat_get_underscan(
	struct amdgpu_display_manager *dm,
	uint32_t adapter_index,
	uint32_t display_index,
	struct dsat_adjustment_data *adj_value);

uint32_t dsat_display_mode_timing_get_count(
		struct amdgpu_display_manager *dm,
		uint32_t adapter_index,
		uint32_t display_index);

bool dsat_display_mode_timing_get_list(
		struct amdgpu_display_manager *dm,
		uint32_t adapter_index,
		uint32_t display_index,
		uint32_t buffer_size,
		struct dsat_mode_timing *mode_timing);

#endif

#endif /* __DSAT__H */
