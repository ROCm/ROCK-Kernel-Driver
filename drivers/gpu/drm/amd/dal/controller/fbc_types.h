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

#ifndef __DAL_FBC_TYPES_H__
#define __DAL_FBC_TYPES_H__

enum fbc_compress_ratio {
	FBC_COMPRESS_RATIO_INVALID = 0,
	FBC_COMPRESS_RATIO_1TO1 = 1,
	FBC_COMPRESS_RATIO_2TO1 = 2,
	FBC_COMPRESS_RATIO_4TO1 = 4,
	FBC_COMPRESS_RATIO_8TO1 = 8,
};

union fbc_physical_address {
	struct {
		uint32_t low_part;
		int32_t high_part;
	} addr;
	int64_t quad_part;
};

struct compr_addr_and_pitch_params {
	enum controller_id controller_id;
	uint32_t source_view_width;
	uint32_t source_view_height;
};

struct fbc_lpt_config {
	uint32_t mem_channels_num;
	uint32_t banks_num;
	uint32_t chan_interleave_size;
	uint32_t row_size;
};

struct fbc_input_info {
	bool dynamic_fbc_buffer_alloc;
	uint32_t source_view_width;
	uint32_t source_view_height;
	uint32_t active_targets_num;
	struct fbc_lpt_config lpt_config;
};

struct fbc_requested_compressed_size {
	uint32_t prefered_size;
	uint32_t prefered_size_alignment;
	uint32_t min_size;
	uint32_t min_size_alignment;
	union {
		struct {
			/*Above prefered_size must be allocated in FB pool */
			uint32_t PREFERED_MUST_BE_FRAME_BUFFER_POOL:1;
			/*Above min_size must be allocated in FB pool */
			uint32_t MIN_MUST_BE_FRAME_BUFFER_POOL:1;
		} flags;
		uint32_t bits;
	};
};

struct fbc_compressed_surface_info {
	union fbc_physical_address compressed_surface_address;
	uint32_t allocated_size;
	union {
		struct {
			uint32_t FB_POOL:1; /*Allocated in FB Pool */
			uint32_t DYNAMIC_ALLOC:1; /*Dynamic allocation */
		} allocation_flags;
		uint32_t bits;
	};
};

enum fbc_hw_max_resolution_supported {
	FBC_MAX_X = 3840,
	FBC_MAX_Y = 2400
};

struct fbc_max_resolution_supported {
	uint32_t source_view_width;
	uint32_t source_view_height;
};

#endif
