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

#ifndef __DM_SERVICES_TYPES_H__
#define __DM_SERVICES_TYPES_H__

#define INVALID_DISPLAY_INDEX 0xffffffff

#if defined __KERNEL__

#include <asm/byteorder.h>
#include <linux/types.h>
#include <drm/drmP.h>

#include "cgs_linux.h"

#if defined(__BIG_ENDIAN) && !defined(BIGENDIAN_CPU)
#define BIGENDIAN_CPU
#elif defined(__LITTLE_ENDIAN) && !defined(LITTLEENDIAN_CPU)
#define LITTLEENDIAN_CPU
#endif

#undef READ
#undef WRITE
#undef FRAME_SIZE

#define dm_output_to_console(fmt, ...) DRM_INFO(fmt, ##__VA_ARGS__)

#define dm_error(fmt, ...) DRM_ERROR(fmt, ##__VA_ARGS__)

#define dm_debug(fmt, ...) DRM_DEBUG_KMS(fmt, ##__VA_ARGS__)

#define dm_vlog(fmt, args) vprintk(fmt, args)

#define dm_min(x, y) min(x, y)
#define dm_max(x, y) max(x, y)

#elif defined BUILD_DAL_TEST

#include <inttypes.h>
#include <stdlib.h>
#include <string.h>

#include <stdio.h>

#include <stdarg.h>

#include "cgs_linux.h"

#define LONG_MAX	((long)(~0UL>>1))
#define LONG_MIN	(-LONG_MAX - 1)
#define LLONG_MAX	((long long)(~0ULL>>1))
#define LLONG_MIN	(-LLONG_MAX - 1)
#define UINT_MAX	(~0U)

typedef _Bool bool;
enum { false, true };

#ifndef NULL
#define NULL ((void *)0)
#endif

#define LITTLEENDIAN_CPU 1

#include <test_context.h>

#define offsetof(TYPE, MEMBER) ((size_t) &((TYPE *)0)->MEMBER)

#define container_of(ptr, type, member) \
	((type *)((char *)(ptr) - offsetof(type, member)))

#define dal_test_not_implemented() \
	printf("[DAL_TEST_NOT_IMPL]:%s\n", __func__)

#define dm_output_to_console(fmt, ...) do { \
	printf("[DAL_LOG]" fmt, ##__VA_ARGS__); } \
	while (false)

#define dm_error(fmt, ...) printf("[DAL_ERROR]" fmt, ##__VA_ARGS__)

#define dm_output_to_console(fmt, ...) do { \
			printf("[DAL_LOG]" fmt, ##__VA_ARGS__); } \
				while (false)


#define dm_debug(fmt, ...) printf("[DAL_DBG]" fmt, ##__VA_ARGS__)

#define dm_vlog(fmt, args) vprintf(fmt, args)

#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]))

#define dm_min(x, y) ({\
	typeof(x) _min1 = (x);\
	typeof(y) _min2 = (y);\
	(void) (&_min1 == &_min2);\
	_min1 < _min2 ? _min1 : _min2; })

#define dm_max(x, y) ({\
	typeof(x) _max1 = (x);\
	typeof(y) _max2 = (y);\
	(void) (&_max1 == &_max2);\
	_max1 > _max2 ? _max1 : _max2; })

/* division functions */

static inline int64_t div64_s64(int64_t x, int64_t y)
{
	return x / y;
}

static inline uint64_t div64_u64(uint64_t x, uint64_t y)
{
	return x / y;
}

static inline uint64_t div_u64(uint64_t x, uint32_t y)
{
	return x / y;
}

static inline uint64_t div64_u64_rem(uint64_t x, uint64_t y, uint64_t *rem)
{
	if (rem)
		*rem = x % y;
	return x / y;
}

static inline uint64_t div_u64_rem(uint64_t x, uint32_t y, uint32_t *rem)
{
	if (rem)
		*rem = x % y;
	return x / y;
}

#define cpu_to_le16(do_nothing) do_nothing

#define le16_to_cpu(do_nothing) do_nothing

#define cpu_to_le32(do_nothing) do_nothing

#define le32_to_cpu(do_nothing) do_nothing

#endif

#endif
