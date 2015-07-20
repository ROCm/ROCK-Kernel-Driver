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
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 * Authors: AMD
 *
 */

#include "dal_services.h"

#include "include/timing_service_types.h"
#include "include/flat_set.h"

#include "default_mode_list.h"

/* TODO might optimize by storing this table already sorted
 * and doing just memcpy at initialization*/
const struct mode_info default_mode_table[] = {
/* DMT Compliant Modes */
{640, 480, 60, TIMING_STANDARD_DMT, TIMING_SOURCE_BASICMODE, {0, 0, 0, 0} },
{640, 480, 75, TIMING_STANDARD_DMT, TIMING_SOURCE_UNDEFINED, {0, 0, 0, 0} },
{640, 480, 85, TIMING_STANDARD_DMT, TIMING_SOURCE_UNDEFINED, {0, 0, 0, 0} },
{800, 600, 60, TIMING_STANDARD_DMT, TIMING_SOURCE_BASICMODE, {0, 0, 0, 0} },
{800, 600, 75, TIMING_STANDARD_DMT, TIMING_SOURCE_UNDEFINED, {0, 0, 0, 0} },
{800, 600, 85, TIMING_STANDARD_DMT, TIMING_SOURCE_UNDEFINED, {0, 0, 0, 0} },
{1024, 768, 60, TIMING_STANDARD_DMT, TIMING_SOURCE_BASICMODE, {0, 0, 0, 0} },
{1024, 768, 75, TIMING_STANDARD_DMT, TIMING_SOURCE_UNDEFINED, {0, 0, 0, 0} },
{1024, 768, 85, TIMING_STANDARD_DMT, TIMING_SOURCE_UNDEFINED, {0, 0, 0, 0} },
{1152, 864, 75, TIMING_STANDARD_DMT, TIMING_SOURCE_UNDEFINED, {0, 0, 0, 0} },
{1280, 768, 60, TIMING_STANDARD_DMT, TIMING_SOURCE_UNDEFINED, {0, 0, 0, 0} },
{1280, 768, 75, TIMING_STANDARD_DMT, TIMING_SOURCE_UNDEFINED, {0, 0, 0, 0} },
{1280, 768, 85, TIMING_STANDARD_DMT, TIMING_SOURCE_UNDEFINED, {0, 0, 0, 0} },
{1280, 1024, 60, TIMING_STANDARD_DMT, TIMING_SOURCE_BASICMODE, {0, 0, 0, 0} },
{1280, 1024, 75, TIMING_STANDARD_DMT, TIMING_SOURCE_UNDEFINED, {0, 0, 0, 0} },
{1280, 1024, 85, TIMING_STANDARD_DMT, TIMING_SOURCE_UNDEFINED, {0, 0, 0, 0} },
{1360, 768, 60, TIMING_STANDARD_DMT, TIMING_SOURCE_UNDEFINED, {0, 0, 0, 0} },
{1366, 768, 60, TIMING_STANDARD_DMT, TIMING_SOURCE_UNDEFINED, {0, 0, 0, 0} },
{1440, 900, 60, TIMING_STANDARD_DMT, TIMING_SOURCE_UNDEFINED, {0, 0, 0, 0} },
{1440, 900, 75, TIMING_STANDARD_DMT, TIMING_SOURCE_UNDEFINED, {0, 0, 0, 0} },
{1440, 900, 85, TIMING_STANDARD_DMT, TIMING_SOURCE_UNDEFINED, {0, 0, 0, 0} },
{1600, 900, 60, TIMING_STANDARD_DMT, TIMING_SOURCE_UNDEFINED, {0, 0, 0, 0} },
{1600, 1200, 60, TIMING_STANDARD_DMT, TIMING_SOURCE_BASICMODE, {0, 0, 0, 0} },
{1600, 1200, 75, TIMING_STANDARD_DMT, TIMING_SOURCE_UNDEFINED, {0, 0, 0, 0} },
{1600, 1200, 85, TIMING_STANDARD_DMT, TIMING_SOURCE_UNDEFINED, {0, 0, 0, 0} },
{1680, 1050, 60, TIMING_STANDARD_DMT, TIMING_SOURCE_BASICMODE, {0, 0, 0, 0} },
{1680, 1050, 75, TIMING_STANDARD_DMT, TIMING_SOURCE_UNDEFINED, {0, 0, 0, 0} },
{1680, 1050, 85, TIMING_STANDARD_DMT, TIMING_SOURCE_UNDEFINED, {0, 0, 0, 0} },
{1920, 1200, 60, TIMING_STANDARD_DMT, TIMING_SOURCE_BASICMODE, {0, 0, 0, 0} },
{1920, 1200, 75, TIMING_STANDARD_DMT, TIMING_SOURCE_UNDEFINED, {0, 0, 0, 0} },
{1920, 1200, 85, TIMING_STANDARD_DMT, TIMING_SOURCE_UNDEFINED, {0, 0, 0, 0} },
{1920, 1440, 60, TIMING_STANDARD_DMT, TIMING_SOURCE_UNDEFINED, {0, 0, 0, 0} },
{1920, 1440, 75, TIMING_STANDARD_DMT, TIMING_SOURCE_UNDEFINED, {0, 0, 0, 0} },

/* GTF Compliant Modes */
{640, 480, 100, TIMING_STANDARD_GTF, TIMING_SOURCE_UNDEFINED, {0, 0, 0, 0} },
{640, 480, 120, TIMING_STANDARD_GTF, TIMING_SOURCE_UNDEFINED, {0, 0, 0, 0} },
{640, 480, 140, TIMING_STANDARD_GTF, TIMING_SOURCE_UNDEFINED, {0, 0, 0, 0} },
{640, 480, 160, TIMING_STANDARD_GTF, TIMING_SOURCE_UNDEFINED, {0, 0, 0, 0} },
{640, 480, 200, TIMING_STANDARD_GTF, TIMING_SOURCE_UNDEFINED, {0, 0, 0, 0} },
{800, 600, 100, TIMING_STANDARD_GTF, TIMING_SOURCE_UNDEFINED, {0, 0, 0, 0} },
{800, 600, 120, TIMING_STANDARD_GTF, TIMING_SOURCE_UNDEFINED, {0, 0, 0, 0} },
{800, 600, 140, TIMING_STANDARD_GTF, TIMING_SOURCE_UNDEFINED, {0, 0, 0, 0} },
{800, 600, 160, TIMING_STANDARD_GTF, TIMING_SOURCE_UNDEFINED, {0, 0, 0, 0} },
{800, 600, 200, TIMING_STANDARD_GTF, TIMING_SOURCE_UNDEFINED, {0, 0, 0, 0} },
{1024, 768, 100, TIMING_STANDARD_GTF, TIMING_SOURCE_UNDEFINED, {0, 0, 0, 0} },
{1024, 768, 120, TIMING_STANDARD_GTF, TIMING_SOURCE_UNDEFINED, {0, 0, 0, 0} },
{1024, 768, 140, TIMING_STANDARD_GTF, TIMING_SOURCE_UNDEFINED, {0, 0, 0, 0} },
{1024, 768, 160, TIMING_STANDARD_GTF, TIMING_SOURCE_UNDEFINED, {0, 0, 0, 0} },
{1024, 768, 200, TIMING_STANDARD_GTF, TIMING_SOURCE_UNDEFINED, {0, 0, 0, 0} },
{1152, 864, 60, TIMING_STANDARD_GTF, TIMING_SOURCE_UNDEFINED, {0, 0, 0, 0} },
{1152, 864, 85, TIMING_STANDARD_GTF, TIMING_SOURCE_UNDEFINED, {0, 0, 0, 0} },
{1152, 864, 100, TIMING_STANDARD_GTF, TIMING_SOURCE_UNDEFINED, {0, 0, 0, 0} },
{1152, 864, 120, TIMING_STANDARD_GTF, TIMING_SOURCE_UNDEFINED, {0, 0, 0, 0} },
{1152, 864, 140, TIMING_STANDARD_GTF, TIMING_SOURCE_UNDEFINED, {0, 0, 0, 0} },
{1152, 864, 160, TIMING_STANDARD_GTF, TIMING_SOURCE_UNDEFINED, {0, 0, 0, 0} },
{1152, 864, 200, TIMING_STANDARD_GTF, TIMING_SOURCE_UNDEFINED, {0, 0, 0, 0} },
{1280, 1024, 100, TIMING_STANDARD_GTF, TIMING_SOURCE_UNDEFINED, {0, 0, 0, 0} },
{1280, 1024, 120, TIMING_STANDARD_GTF, TIMING_SOURCE_UNDEFINED, {0, 0, 0, 0} },
{1280, 1024, 160, TIMING_STANDARD_GTF, TIMING_SOURCE_UNDEFINED, {0, 0, 0, 0} },
{1280, 1024, 180, TIMING_STANDARD_GTF, TIMING_SOURCE_UNDEFINED, {0, 0, 0, 0} },
{1600, 1200, 100, TIMING_STANDARD_GTF, TIMING_SOURCE_UNDEFINED, {0, 0, 0, 0} },
{1600, 1200, 120, TIMING_STANDARD_GTF, TIMING_SOURCE_UNDEFINED, {0, 0, 0, 0} },
{1800, 1440, 60, TIMING_STANDARD_GTF, TIMING_SOURCE_UNDEFINED, {0, 0, 0, 0} },
{1800, 1440, 75, TIMING_STANDARD_GTF, TIMING_SOURCE_UNDEFINED, {0, 0, 0, 0} },
{1800, 1440, 85, TIMING_STANDARD_GTF, TIMING_SOURCE_UNDEFINED, {0, 0, 0, 0} },
{1800, 1440, 100, TIMING_STANDARD_GTF, TIMING_SOURCE_UNDEFINED, {0, 0, 0, 0} },
{1800, 1440, 120, TIMING_STANDARD_GTF, TIMING_SOURCE_UNDEFINED, {0, 0, 0, 0} },
{2048, 1536, 60, TIMING_STANDARD_GTF, TIMING_SOURCE_BASICMODE, {0, 0, 0, 0} },
{2048, 1536, 75, TIMING_STANDARD_GTF, TIMING_SOURCE_UNDEFINED, {0, 0, 0, 0} },
{2048, 1536, 85, TIMING_STANDARD_GTF, TIMING_SOURCE_UNDEFINED, {0, 0, 0, 0} },

/* CVT Compliant Modes */
{1280, 800, 60, TIMING_STANDARD_CVT, TIMING_SOURCE_UNDEFINED, {0, 0, 0, 0} },

/* CE Compliant Modes */
{720, 480, 60, TIMING_STANDARD_CEA861, TIMING_SOURCE_UNDEFINED, {0, 0, 0, 0} },
{1280, 720, 60, TIMING_STANDARD_CEA861, TIMING_SOURCE_BASICMODE, {0, 0, 0, 0} },
{1920, 1080, 60, TIMING_STANDARD_CEA861, TIMING_SOURCE_BASICMODE, {0, 0, 0, 0} }
};

#define DEFAULT_MODE_TABLE_SIZE ARRAY_SIZE(default_mode_table)

struct default_mode_list {
	struct flat_set set;
};

bool default_mode_list_construct(
		struct default_mode_list *dml,
		const struct flat_set_init_data *fs_init_data)
{
	uint32_t i;

	if (!dal_flat_set_construct(&dml->set, fs_init_data))
		return false;

	for (i = 0; i < DEFAULT_MODE_TABLE_SIZE; ++i)
		if (!dal_flat_set_insert(
				&dml->set,
				&default_mode_table[i]))
			BREAK_TO_DEBUGGER();
	/* shouldn't happen, why have we already got this value?
	 * we also could fail to realloc*/

	return true;
}

struct default_mode_list *dal_default_mode_list_create(void)
{
	struct flat_set_init_data fs_init_data = {
			DEFAULT_MODE_TABLE_SIZE,
			sizeof(struct mode_info),
			{ dal_mode_info_less_than }
	};

	struct default_mode_list *dml = dal_alloc(
			sizeof(struct default_mode_list));
	if (dml == NULL)
		return NULL;

	if (!default_mode_list_construct(dml, &fs_init_data)) {
		dal_free(dml);
		return NULL;
	}

	return dml;
}

uint32_t dal_default_mode_list_get_count(
		const struct default_mode_list *dml)
{
	return dml->set.vector.count;
}

struct mode_info *dal_default_mode_list_get_mode_info_at_index(
	const struct default_mode_list *dml,
	uint32_t index)
{
	if (index >= dml->set.vector.count)
		return NULL;
	return (struct mode_info *)dal_vector_at_index(
			&dml->set.vector,
			index);
}

void dal_default_mode_list_destroy(struct default_mode_list **dml)
{
	if (dml == NULL || *dml == NULL)
		return;

	dal_flat_set_destruct(&(*dml)->set);
	dal_free(*dml);
	*dml = NULL;
}
