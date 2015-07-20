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
 * Authors: Najeeb Ahmed
 *
 */

#ifndef __DAL_TM_SUBSETS_CACHE__
#define __DAL_TM_SUBSETS_CACHE__

/* internal consts*/
enum {
	MAPPING_NOT_SET = 0xFFFF
};


enum cache_query_result {
	CQR_UNKNOWN = 0,
	CQR_NOT_SUPPORTED,
	CQR_SUPPORTED,
	CQR_DP_MAPPING_NOT_VALID,
};

/* forward declarations */
struct dal_context;

struct tm_subsets_cache {
	struct dal_context *dal_context;
	uint32_t *cofunc_cache;
	uint32_t *dp2_cache_mapping;
	uint32_t *cache_2dp_mapping;
	uint32_t cofunc_cache_single;
	uint32_t cofunc_cache_single_valid;
	uint32_t connected;
	uint32_t num_connected;
	uint32_t num_cur_cached_paths;
	/* for robustness purposes to assure
	 * computed index isn't out of bounds
	 */
	uint32_t max_num_combinations;
	enum cache_query_result all_connected_supported;

	/* these two are board specific,
	 * should not change during executable runtime
	 */
	uint32_t num_display_paths;
	uint32_t max_num_cofunc_targets;

	/* performance enhancing helper*/
	uint32_t *binom_coeffs;

};

struct tm_subsets_cache *dal_tm_subsets_cache_create(
	struct dal_context *dal_context,
	uint32_t num_of_display_paths,
	uint32_t max_num_of_cofunc_paths,
	uint32_t num_of_func_controllers);

void dal_tm_subsets_cache_destroy(
	struct tm_subsets_cache **tm_subsets_cache);

uint32_t dal_get_num_of_combinations(
	struct tm_subsets_cache *tm_subsets_cache);

void dal_invalidate_subsets_cache(
	struct tm_subsets_cache *tm_subset_cache,
	bool singles_too);

enum cache_query_result dal_is_subset_supported(
	struct tm_subsets_cache *tm_subset_cache,
	const uint32_t *displays,
	uint32_t array_size);

void dal_set_subset_supported(
	struct tm_subsets_cache *tm_subset_cache,
	const uint32_t *displays,
	uint32_t array_size,
	bool supported);

void dal_update_display_mapping(
	struct tm_subsets_cache *tm_subset_cache,
	const uint32_t display_index,
	bool connected);

#endif /*__DAL_TM_SUBSETS_CACHE_H__*/

