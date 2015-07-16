/*
* Copyright 2012-14 Advanced Micro Devices, Inc.
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

#include "dal_services.h"
#include "tm_subsets_cache.h"
#include "tm_utils.h"
#include "include/logger_interface.h"

/**
*	Returns (n choose k).
*	For k<=3 we do direct computation, and for
*	k>3 we use the cached values stored in
*	m_pBinomCoeffs and created in
*	computeBinomCoeffs at class initialization
*
*	\param [in] n
*	\param [in] k
*
*	\return
*	(n choose k)
*/
static uint32_t get_binom_coeff(
	struct tm_subsets_cache *tm_subsets_cache,
	uint32_t n,
	uint32_t k)
{
	/* use direct formula for 0-3 as it's
	 * more efficient and doesn't use caching space
	 * for all others, use cache
	 */
	if (k > n)
		return 0;
	if (k == n || k == 0)
		return 1;
	if (k == 1)
		return n;
	if (k == 2)
		return n*(n-1)/2;
	if (k == 3)
		return n*(n-1)*(n-2)/6;

	/* should not happen*/
	if (tm_subsets_cache->binom_coeffs == NULL) {
		ASSERT_CRITICAL(0);
		return 0;
	}
	/* read from table*/
	return tm_subsets_cache->binom_coeffs[
		(n-4)*(tm_subsets_cache->max_num_cofunc_targets-3)+k-4];
}

/**
*
*	Computes binomial coefficients and stores
*	them in a table. We only cache (n choose k)
*	for k>3, as for k<=3 it's faster to compute directly.
*	The computation is done recursively
*	(look up Pascal Triangle in Google for details)
*
*	\return
*	void
*
*/
static void compute_binom_coeffs(
	struct tm_subsets_cache *tm_subsets_cache)
{
	uint32_t n = 0;
	uint32_t k = 0;
	struct dal_context *dal_context = tm_subsets_cache->dal_context;

	/* shouldn't happen*/
	if (tm_subsets_cache->binom_coeffs == 0) {
		TM_ERROR("%s: binomial_coeff is zero\n", __func__);
		return;
	}
	for (n = 4; n <=
		tm_subsets_cache->num_display_paths; ++n) {

		int offset = (n-4)*
			(tm_subsets_cache->max_num_cofunc_targets - 3);

		for (k = 4; k <=
			tm_subsets_cache->max_num_cofunc_targets; ++k) {
			if (n == k) {
				tm_subsets_cache->binom_coeffs[offset+k-4] = 1;
				break;
			}
			/* compute recursively, if cached, it would
			 * have been computed in the previous n-loop
			 */
			tm_subsets_cache->binom_coeffs[offset+k-4] =
				get_binom_coeff(tm_subsets_cache, n-1, k-1) +
				get_binom_coeff(tm_subsets_cache, n-1, k);
		}
	}
}

/**
*	Clears all DP to cache mapping information for both mappings
*/
static void reset_dp2_cache_mapping(
	struct tm_subsets_cache *tm_subset_cache)
{
	uint32_t i = 0;

	for (i = 0; i < tm_subset_cache->num_display_paths; ++i)
		tm_subset_cache->cache_2dp_mapping[i] = MAPPING_NOT_SET;

	for (i = 0; i < tm_subset_cache->num_display_paths; ++i)
		tm_subset_cache->dp2_cache_mapping[i] = MAPPING_NOT_SET;

}

struct tm_subsets_cache *dal_tm_subsets_cache_create(
	struct dal_context *dal_context,
	uint32_t num_of_display_paths,
	uint32_t max_num_of_cofunc_paths,
	uint32_t num_of_func_controllers)
{
	struct tm_subsets_cache *tm_subsets_cache = NULL;
	uint32_t cache_size_in_bytes = 0;

	tm_subsets_cache = dal_alloc(
		sizeof(struct tm_subsets_cache));

	if (tm_subsets_cache == NULL)
		return NULL;

	tm_subsets_cache->dal_context = dal_context;
	tm_subsets_cache->num_connected = 0;
	tm_subsets_cache->num_display_paths = 0;
	tm_subsets_cache->max_num_cofunc_targets = 0;
	tm_subsets_cache->num_cur_cached_paths = 0;
	tm_subsets_cache->binom_coeffs = NULL;
	tm_subsets_cache->all_connected_supported = CQR_UNKNOWN;
	tm_subsets_cache->cofunc_cache_single = 0;
	tm_subsets_cache->cofunc_cache_single_valid = 0;
	tm_subsets_cache->connected = 0;

	tm_subsets_cache->max_num_combinations = dal_get_num_of_combinations(
		tm_subsets_cache);


	/* need 2 bits per combination, also need to
	* align to the size of uint32_t
	* e.g. 53 combinations require 106 bits,
	* this is 53/4 = 106/8 = 13.25, rounded down
	* by default to 13 so we add 1 byte to get 14
	* but because we store in uint32_t*
	* we actually need 16 bytes assuming 32/64-bit int
	* Note that we reserve space for all sizes,
	* including size 1, although we don't use it as
	* we keep info about single displays
	*/
	/* separately for performance reasons. We would
	* save at most a couple bytes,
	* and it makes some math computations cleaner
	*/

	cache_size_in_bytes = sizeof(uint32_t) *
		(1 + tm_subsets_cache->max_num_combinations/
			(4 * sizeof(uint32_t)));

	/* AllocMemory also zeros the cache*/
	tm_subsets_cache->cofunc_cache = dal_alloc(cache_size_in_bytes);

	tm_subsets_cache->dp2_cache_mapping = dal_alloc(
		sizeof(uint32_t) * num_of_display_paths);

	tm_subsets_cache->cache_2dp_mapping = dal_alloc(
		sizeof(uint32_t) * num_of_display_paths);


	reset_dp2_cache_mapping(tm_subsets_cache);

	/* we cache binom coeffs (n choose k) only if k>3,
	* since for k<=3 it's faster to compute directly
	*/
	if (max_num_of_cofunc_paths > 3) {

		tm_subsets_cache->binom_coeffs = dal_alloc(
			sizeof(uint32_t) *
		((num_of_display_paths-3)*(max_num_of_cofunc_paths-3)));

		compute_binom_coeffs(tm_subsets_cache);
	}
	return tm_subsets_cache;

}

void dal_tm_subsets_cache_destroy(
	struct tm_subsets_cache **ptr)
{
	struct tm_subsets_cache *tm_subsets_cache;

	if (!ptr || !*ptr)
		return;

	tm_subsets_cache = *ptr;

	if (tm_subsets_cache->binom_coeffs != NULL) {
		dal_free(tm_subsets_cache->binom_coeffs);
		tm_subsets_cache->binom_coeffs = NULL;
	}

	if (tm_subsets_cache->cofunc_cache != NULL) {
		dal_free(tm_subsets_cache->cofunc_cache);
		tm_subsets_cache->cofunc_cache = NULL;
	}

	if (tm_subsets_cache->dp2_cache_mapping != NULL) {
		dal_free(tm_subsets_cache->dp2_cache_mapping);
		tm_subsets_cache->dp2_cache_mapping = NULL;
	}

	if (tm_subsets_cache->cache_2dp_mapping != NULL) {
		dal_free(tm_subsets_cache->cache_2dp_mapping);
		tm_subsets_cache->cache_2dp_mapping = NULL;
	}

	dal_free(tm_subsets_cache);
	*ptr = 0;
}

/*
*  Returns the number of combinations the
*  cache needs to store. It depends on the
*  number of display paths and the max
*  num of confunctional targets
*/
uint32_t dal_get_num_of_combinations(struct tm_subsets_cache *cache)
{
	uint32_t num_of_combinations = 0;
	uint32_t i = 0;
	/* number of subsets of size i*/
	uint32_t num_subsets_of_fixed_size = 1;

	for (i = 1; i <= cache->max_num_cofunc_targets; ++i) {
		if (i > cache->num_display_paths)
			return num_of_combinations;


		/* using the fact that:
		 *(N choose i) = (N choose i-1) * (N-(i-1)) / i
		 */
		num_subsets_of_fixed_size *= (cache->num_display_paths-i+1);
		/* it will always be divisible without remainder*/
		num_subsets_of_fixed_size /= i;
		num_of_combinations += num_subsets_of_fixed_size;
	}

	return num_of_combinations;

}

/**
 *	Clears all cached information about subsets
 *	supported. We keep information about
 *	supporting single displays separately,
 *	and there may be no need to wipe that cache too.
 *
 *	\param
 *	[in] singles_too : whether to invalidate
 *	the cache keeping info about individual displays
 */
void dal_invalidate_subsets_cache(
	struct tm_subsets_cache *tm_subset_cache,
	bool singles_too)
{
	uint32_t cache_size_in_bytes;

	if (tm_subset_cache->cofunc_cache == NULL) {
		ASSERT_CRITICAL(0);
		return;
	}
	/* need 2 bits per combination, also need
	* to align to the size of uint32_t
	* e.g. 53 combinations require 106 bits,
	* this is 53/4 = 106/8 = 13.25, rounded down
	* by default to 13 so we add 1 byte to get 14
	*/

	/* but because we store in uint32_t* we
	* actually need 16 bytes assuming 32/64-bit int
	*/
	cache_size_in_bytes = sizeof(uint32_t) *
		(1 + tm_subset_cache->max_num_combinations/
		(4 * sizeof(uint32_t)));

	dal_memset(tm_subset_cache->cofunc_cache, 0, cache_size_in_bytes);
	tm_subset_cache->all_connected_supported = CQR_UNKNOWN;

	if (singles_too) {
		tm_subset_cache->cofunc_cache_single = 0;
		tm_subset_cache->cofunc_cache_single_valid = 0;
	}
}


/**
*
* Since we keep only connected displays, if connectivity
* information changes, we need to update cache to DP mapping
* and possibly clear the cache
*
* \param [in] display_index: display index of the path whose
*	connectivity information (may have) changed
*
* \param [in] connected: is the display path with the
* given index connected or disconnected
*
*
*/
void dal_update_display_mapping(
	struct tm_subsets_cache *tm_subsets_cache,
	const uint32_t display_index,
	bool connected)
{
	struct dal_context *dal_context = tm_subsets_cache->dal_context;
	uint32_t cache_size_in_bytes;
	uint32_t i;

	if (tm_subsets_cache->cofunc_cache == NULL ||
		display_index >= tm_subsets_cache->num_display_paths) {
		TM_ERROR("%s: cofunctional cache is NULL", __func__);
		return;
	}


	if (connected != tm_utils_test_bit(
		&tm_subsets_cache->connected, display_index)) {

		if (connected) {
			tm_utils_set_bit(
				&tm_subsets_cache->connected,
				display_index);
			++tm_subsets_cache->num_connected;
		} else {
			tm_utils_clear_bit(
				&tm_subsets_cache->connected,
				display_index);
			--tm_subsets_cache->num_connected;
		}
	} else
		/* cache is already up-to-date*/
		return;


	/* Need to increase the cache, unfortunately there's
	 * no good way to keep previous cached lookups,
	 * have to wipe out everything
	 */
	/* so we will have cache misses requiring noncached
	 * lookups. For disconnect, we don't decrease cache size.
	 */
	if (tm_subsets_cache->num_connected >
		tm_subsets_cache->num_cur_cached_paths) {

		/* we keep it in sync*/
		if (tm_subsets_cache->num_connected !=
			tm_subsets_cache->num_cur_cached_paths + 1)
			TM_WARNING("%s: Subset cache not in sync\n", __func__);




		++tm_subsets_cache->num_cur_cached_paths;

		dal_free(tm_subsets_cache->cofunc_cache);

		tm_subsets_cache->cofunc_cache = NULL;

		tm_subsets_cache->max_num_combinations =
				dal_get_num_of_combinations(
					tm_subsets_cache);


		/* need 2 bits per combination, also need to
		 * align to the size of uint32_t
		 * e.g. 53 combinations require 106 bits,
		 * this is 53/4 = 106/8 = 13.25,
		 * rounded down by default to 13
		 * so we add 1 byte to get 14
		 */
		/* but because we store in uint32_t*
		 * we actually need 16 bytes assuming 32/64-bit int
		 */
		cache_size_in_bytes = sizeof(uint32_t) *
			(1 + tm_subsets_cache->max_num_combinations/
			(4 * sizeof(uint32_t)));
		/* AllocMemory also zeros the cache*/
		tm_subsets_cache->cofunc_cache = dal_alloc(cache_size_in_bytes);
	}

	/* now update DP mapping arrays*/
	if (connected) {
		if (tm_subsets_cache->
			dp2_cache_mapping[display_index] !=
				MAPPING_NOT_SET) {

			if (tm_subsets_cache->
				all_connected_supported ==
					CQR_SUPPORTED)
				tm_subsets_cache->
					all_connected_supported =
						CQR_UNKNOWN;

			/* this index already mapped into some cache
			 * index, we can skip invalidating cache too
			 */
			return;
		}
		for (i = 0; i < tm_subsets_cache->num_cur_cached_paths; ++i) {
			if (tm_subsets_cache->
					cache_2dp_mapping[i] ==
						MAPPING_NOT_SET) {
				tm_subsets_cache->
					cache_2dp_mapping[i] =
						display_index;
				tm_subsets_cache->
					dp2_cache_mapping[display_index] = i;
				break;
			}

			/* check if current index is set,
			 * but disconnected, we can reuse it
			 */
			if (!tm_utils_test_bit(
				&tm_subsets_cache->connected,
				tm_subsets_cache->
					cache_2dp_mapping[i])) {

				uint32_t previous_index =
					tm_subsets_cache->
					cache_2dp_mapping[i];
				tm_subsets_cache->
					cache_2dp_mapping[i] =
						display_index;
				tm_subsets_cache->
					dp2_cache_mapping[
					display_index] = i;
				tm_subsets_cache->
					dp2_cache_mapping[
					previous_index] =
					MAPPING_NOT_SET;
				break;
			}
		}
		/* whatever happened above, we need
		 * to reset the cache, no need to
		 * reset single index array
		 */
		dal_invalidate_subsets_cache(tm_subsets_cache, false);
	} else {
		if (tm_subsets_cache->
			all_connected_supported ==
				CQR_NOT_SUPPORTED)
			tm_subsets_cache->
				all_connected_supported =
					CQR_UNKNOWN;
	}
}

/**
*
*	Check whether the current DP mapping is
*	valid with respect to the display path
*	indices given as input true means that
*	we're currently caching information
*	about this subset.
*
*	\param [in] displays: array of display paths
*	 for which we will check that DP to cache mapping is valid
*	\param [in] array_size: size of the above array
*
*	\return
*	true - if given display path subset is already mapped
*	false - given display path subset is not mapped
*/
static bool is_dp_mapping_valid(
	struct tm_subsets_cache *tm_subset_cache,
	const uint32_t *displays,
	uint32_t array_size)
{
	bool ret = true;
	uint32_t i;

	for (i = 0; i < array_size; ++i) {
		if (tm_subset_cache->dp2_cache_mapping[displays[i]] ==
		MAPPING_NOT_SET) {
			ret = false;
			break;
		}
	}
	return ret;
}

/**
*
*	Whether all the displays paths are currently connected.
*	This is a very common case which can be further optimized
*
*	\param [in] displays:  array of display paths
*	\param [in] array_size:  size of the above array

*	\return
*	true if all display paths in displays are currently connected
*	false otherwise
*
*/
static bool all_connected(
	struct tm_subsets_cache *tm_subsets_cache,
	const uint32_t *displays,
	uint32_t array_size)
{
	uint32_t i;

	for (i = 0; i < array_size; ++i) {
		if (!tm_utils_test_bit(
				&tm_subsets_cache->connected,
				displays[i]))

			return false;
	}
	return true;
}

/**
*	Finds an index for the given subset of cache indices
*	corresponding to display paths (and not DPs themselves).
*	It uses combinatorial number system idea to lay out
*	all subsets of N elements with max size K.
*	If the subset indices are (c1, c2, ..., ck) then
*	the computed index is:
*	M + (ck choose k) + ... + (c2 choose 2) + (c1 choose 1)
*	where M = (N choose 1) + (N choose 2) + ... (N choose k-1)
*	There's a document giving more details.
*	The cache indices (c1, ..., ck) need to be sorted for the
*	formula to work. See the comment in code as a significant
*	part of this function's logic is sorting the indices
*	without allocating an additional array, or disturbing
*	the original array. Technically they are not sorted:
*	we get them in order and apply the formula.
*
*	\param [in] displays:  array of display paths
*	\param [in] array_size:  size of the above array

*	\return
*      index to the location in the cache where info
*      for subset displays is being stored
*/
static uint32_t find_index(
	struct tm_subsets_cache *tm_subsets_cache,
	const uint32_t *displays,
	uint32_t array_size)
{
	int index = 0;
	uint32_t i;
	uint32_t next_possible_min = 0;
	uint32_t cur_min = 0;

	/* first add all combinations with fewer display paths set*/
	for (i = 1; i < array_size; ++i) {
		index +=
		get_binom_coeff(tm_subsets_cache,
			tm_subsets_cache->num_cur_cached_paths, i);
	}

	/* find index among combinations of size m_subsetSize*/
	/* see
	 * http://en.wikipedia.org/wiki/Combinatorial_number_system
	 * for more details
	 */

	for (i = 0; i < array_size; ++i) {
		/* Need to sort mapped indices for the formula to work.
		 * Since KMD/Lnx may send unsorted arrays of display indices,
		 * even if we tried to keep mapping maintain sorting order,
		 * we'd still need to do this. It was requested that we
		 * avoid allocating a temporary array, so this n^2 type
		 * of algorithm will "spit" them out one by one in order.
		 * It finds the minimum in the first run, and because the
		 * array doesn't have duplicates, we know the next possible
		 * element must be at least bigger by 1, so find the
		 * smallest such element, and so on.
		 */
		uint32_t j = 0;

		while (j < array_size &&
			tm_subsets_cache->dp2_cache_mapping[displays[j]] <
			next_possible_min) {

			++j;
		}
		if (j == array_size)
			/* duplicates in the display array? cannot handle this,
			 * so return invalid value prompting non-cached lookup
			 */
			return tm_subsets_cache->max_num_combinations + 1;

		cur_min = tm_subsets_cache->dp2_cache_mapping[displays[j++]];
		for (; j < array_size; ++j) {

			uint32_t cur_dp_mapped_index =
				tm_subsets_cache->
					dp2_cache_mapping[displays[j]];
			if ((cur_dp_mapped_index < cur_min) &&
				(cur_dp_mapped_index >= next_possible_min))

				cur_min = cur_dp_mapped_index;

		}

		/* apply formula*/
		if (i < cur_min)
			index += get_binom_coeff(
				tm_subsets_cache,
				cur_min, i+1);

		next_possible_min = cur_min + 1;
	}

	return index;
}

/**
*	Check whether the given subset of display
*	paths is supported, i.e. if the display paths
*	can be enabled at the same time.
*
*	\param [in] displays: array of display paths for
*	which we will check whether they can be
*	enabled at the same time
*	\param [in] array_size: size of the above array
*
*	\return
*	CacheQueryResult enum:
*	Supported - the given subset is supported (cache hit)
*	NotSupported - the given subset is supported (cache hit)
*
*	Unknown - this display path subset is currently mapped
*	in the cache, but this is
*	the first query so it is not known whether
*	it's supported or not.
*	The caller must do a noncached lookup
*	and update the cache via
*	SetSubsetSupported() (cache miss)
*
*	DPMappingNotValid - this display path subset is currently
*	not being cached. The caller must
*	do a noncached lookup and not
*	attempt to update cache, since it will
*	fail (cache miss)
*
*/
enum cache_query_result dal_is_subset_supported(
	struct tm_subsets_cache *tm_subsets_cache,
	const uint32_t *displays,
	uint32_t array_size)
{
	uint32_t index;
	uint32_t word_num;
	uint32_t bit_mask;
	uint32_t ret;
	struct dal_context *dal_context = tm_subsets_cache->dal_context;

	ASSERT(displays != NULL);

	if (tm_subsets_cache->cofunc_cache == NULL ||
		displays != NULL) {
		ASSERT_CRITICAL(0);
		return CQR_DP_MAPPING_NOT_VALID;
	}

	if (array_size == 1) {

		ASSERT(displays[0] < tm_subsets_cache->num_display_paths);

		if (!tm_utils_test_bit(
			&tm_subsets_cache->cofunc_cache_single_valid,
			displays[0]))
			return CQR_UNKNOWN;
		if (tm_utils_test_bit(
			&tm_subsets_cache->cofunc_cache_single_valid,
			displays[0]))
			return CQR_SUPPORTED;
		/* mapping always valid for size == 1*/
		else
			return CQR_NOT_SUPPORTED;

	}

	/* check if this is a query for all connected
	 * (enabled) ones, which is the most common query observed
	 */
	if (array_size <= tm_subsets_cache->num_connected &&
		array_size <= tm_subsets_cache->max_num_cofunc_targets &&
		tm_subsets_cache->all_connected_supported != CQR_UNKNOWN) {

		if (all_connected(tm_subsets_cache, displays, array_size)) {
			if (tm_subsets_cache->all_connected_supported ==
				CQR_SUPPORTED)
				return CQR_SUPPORTED;
			/* if all connected are not supported, and the subset
			 * is smaller, it could be that it's supported,
			 * in that case we don't return here
			 */
			else if (array_size ==
				tm_subsets_cache->num_connected)
				return CQR_NOT_SUPPORTED;
		}
	}

	/* array_size > 1*/
	/* asking for a disconnected one with array_size > 1?*/
	/* the caller should do noncached lookup
	 * and return result, but not update the cache
	 */
	if (!is_dp_mapping_valid(tm_subsets_cache, displays, array_size))
		return CQR_DP_MAPPING_NOT_VALID;

	index = find_index(tm_subsets_cache, displays, array_size);
	if (index > tm_subsets_cache->max_num_combinations) {

		if (array_size > tm_subsets_cache->max_num_cofunc_targets)
			return CQR_NOT_SUPPORTED;

		/* this should not happen, fall back
		 * to noncached lookup without updating cache
		 */
		TM_ERROR("%s: Invalid index", __func__);
		return CQR_DP_MAPPING_NOT_VALID;
	}

	/* If we have index K, we want to read
	 * bits 2K and 2K+1 in the cache.
	 * Since cache is internally represented as an
	 * uint32_t array, we first convert this into bytes.
	 * 1 element has sizeof(int)*8 bits, so 2K'th bit is
	 * contained in the integer array element at location
	 * wordNum = (2K) / (sizeof(int)*8) = K / (sizeof(int)*4).
	 * bitMask is the offset within
	 * tm_subsets_cache->cofunc_cache[wordNum] - it's the
	 * remainder of the above division, multiplied by 2
	 * Note that 2 bits directly correspond to the
	 * enum 0x0 = Unknown, 0x1 = NotSupported, 0x2 = Supported...
	 * I.e. it's not that 1 bit is for valid or not, and the other
	 * for supported or not.
	 */

	/* *4 instead of *8 since every subset uses 2 bits*/
	word_num = index / (sizeof(uint32_t)*4);
	bit_mask = 0x3 << ((index % (sizeof(uint32_t)*4)) * 2);

	ret = (*(tm_subsets_cache->cofunc_cache +
		word_num) & bit_mask) >> ((index % (sizeof(uint32_t)*4)) * 2);
	return (enum cache_query_result)(ret);
}


/**
*
*	Set/Update cache information for the given display
*	path subset. This function will not do anything
*	if this subset is currently not mapped in the cache
*	(i.e. we're not caching this particular subset)
*
*	\param [in] displays:  array of display paths
*	\param [in] array_size:  size of the above array
*	\param [in] supported:  true if this display path
*	subset can be enabled at the same time, false otherwise
*
*/
void dal_set_subset_supported(
	struct tm_subsets_cache *tm_subsets_cache,
	const uint32_t *displays,
	uint32_t array_size,
	bool supported)
{
	uint32_t index;
	uint32_t word_num;
	uint32_t bit_mask;

	ASSERT(tm_subsets_cache->cofunc_cache != NULL);
	ASSERT(displays != NULL);
	if (tm_subsets_cache->cofunc_cache == NULL ||
		displays == NULL) {
		ASSERT_CRITICAL(0);
		return;
	}


	if (array_size == 1) {

		/* only one display path, so check only displays[0]*/
		if (displays[0] > tm_subsets_cache->num_display_paths)
			return;

		tm_utils_set_bit(
			&tm_subsets_cache->cofunc_cache_single_valid,
			displays[0]);

		if (supported)
			tm_utils_set_bit(
				&tm_subsets_cache->cofunc_cache_single,
				displays[0]);

		return;
	}

	if (all_connected(tm_subsets_cache, displays, array_size) &&
		array_size == tm_subsets_cache->num_connected)
		tm_subsets_cache->all_connected_supported =
			supported ? CQR_SUPPORTED : CQR_NOT_SUPPORTED;


	/* array_size > 1*/
	if (!is_dp_mapping_valid(tm_subsets_cache, displays, array_size))
		/* this case should not really happen as TM
		 * should not call SetSubsetSupported if mapping not valid
		 */
		return;


	index = find_index(tm_subsets_cache, displays, array_size);
	if (index > tm_subsets_cache->max_num_combinations) {
		/* this should not happen*/
		BREAK_TO_DEBUGGER();
		return;
	}

	/* If we have index K, we want to modify bits
	 * 2K and 2K+1 in the cache. Since cache is
	 * internally represented as an uint32_t array,
	 * we first convert this into bytes. 1 element
	 * has sizeof(int)*8 bits, so 2K'th bit is
	 * contained in the integer array element at
	 * location
	 * wordNum = (2K) / (sizeof(int)*8) = K / (sizeof(int)*4)
	 * bitMask is the offset within
	 * tm_subsets_cache->cofunc_cache[wordNum] - it's the
	 * remainder of the above division, multiplied by 2
	 * Note that 2 bits directly correspond to the
	 * enum 0x0 = Unknown, 0x1 = NotSupported, 0x2 = Supported...
	 * i.e. it's not that 1 bit is for valid or not,
	 * and the other for supported or not.
	 */
	/* *4 instead of *8 since every subset uses 2 bits*/
	word_num = index / (sizeof(uint32_t)*4);
	bit_mask = supported ? 0x2 : 0x1;
	/* now move it to the right location within those 32 bits*/
	bit_mask = bit_mask << ((index % (sizeof(uint32_t)*4)) * 2);

	*(tm_subsets_cache->cofunc_cache + word_num) |= bit_mask;
}




