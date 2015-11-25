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
 *
 * Authors: AMD
 *
 */
#include "dc_services.h"
#include "bw_fixed.h"


#define BITS_PER_FRACTIONAL_PART 24

#define MIN_I32 \
	(long long)(-(1LL << (63 - BITS_PER_FRACTIONAL_PART)))

#define MAX_I32 \
	(long long)((1ULL << (63 - BITS_PER_FRACTIONAL_PART)) - 1)

#define MIN_I64 \
	(long long)(-(1LL << 63))

#define MAX_I64 \
	(long long)((1ULL << 63) - 1)


#define FRACTIONAL_PART_MASK \
	((1ULL << BITS_PER_FRACTIONAL_PART) - 1)

#define GET_INTEGER_PART(x) \
	((x) >> BITS_PER_FRACTIONAL_PART)

#define GET_FRACTIONAL_PART(x) \
	(FRACTIONAL_PART_MASK & (x))

static unsigned long long abs_i64(long long arg)
{
	if (arg >= 0)
		return (unsigned long long)(arg);
	else
		return (unsigned long long)(-arg);
}

struct bw_fixed bw_min3(struct bw_fixed v1, struct bw_fixed v2, struct bw_fixed v3)
{
	return bw_min(bw_min(v1, v2), v3);
}

struct bw_fixed bw_max3(struct bw_fixed v1, struct bw_fixed v2, struct bw_fixed v3)
{
	return bw_max(bw_max(v1, v2), v3);
}

struct bw_fixed int_to_fixed(long long value)
{
	struct bw_fixed res;
	ASSERT(value < MAX_I32 && value > MIN_I32);
	res.value = value << BITS_PER_FRACTIONAL_PART;
	return res;
}

struct bw_fixed frc_to_fixed(long long numerator, long long denominator)
{
	struct bw_fixed res;
	bool arg1_negative = numerator < 0;
	bool arg2_negative = denominator < 0;
	unsigned long long arg1_value;
	unsigned long long arg2_value;
	unsigned long long remainder;

	/* determine integer part */
	unsigned long long res_value;

	ASSERT(denominator != 0);

	arg1_value = abs_i64(numerator);
	arg2_value = abs_i64(denominator);
	remainder = arg1_value % arg2_value;
	res_value = arg1_value / arg2_value;

	ASSERT(res_value <= MAX_I32);

	/* determine fractional part */
	{
		unsigned int i = BITS_PER_FRACTIONAL_PART;

		do
		{
			remainder <<= 1;

			res_value <<= 1;

			if (remainder >= arg2_value)
			{
				res_value |= 1;
				remainder -= arg2_value;
			}
		} while (--i != 0);
	}

	/* round up LSB */
	{
		unsigned long long summand = (remainder << 1) >= arg2_value;

		ASSERT(res_value <= MAX_I64 - summand);

		res_value += summand;
	}

	res.value = (signed long long)(res_value);

	if (arg1_negative ^ arg2_negative)
		res.value = -res.value;
	return res;
}

struct bw_fixed bw_min(const struct bw_fixed arg1, const struct bw_fixed arg2)
{
	return (arg1.value <= arg2.value) ? arg1 : arg2;
}

struct bw_fixed bw_max(const struct bw_fixed arg1, const struct bw_fixed arg2)
{
	return (arg2.value <= arg1.value) ? arg1 : arg2;
}

struct bw_fixed bw_floor(const struct bw_fixed arg, const struct bw_fixed significance)
{
	struct bw_fixed result;
	signed long long multiplicand = arg.value / abs_i64(significance.value);
	result.value = abs_i64(significance.value) * multiplicand;
	ASSERT(abs_i64(result.value) <= abs_i64(arg.value));
	return result;
}

struct bw_fixed bw_ceil(const struct bw_fixed arg, const struct bw_fixed significance)
{
	struct bw_fixed result;
	result.value = arg.value + arg.value % abs_i64(significance.value);
	if (result.value < significance.value)
		result.value = significance.value;
	return result;
}

struct bw_fixed add(const struct bw_fixed arg1, const struct bw_fixed arg2)
{
	struct bw_fixed res;

	res.value = arg1.value + arg2.value;

	return res;
}

struct bw_fixed sub(const struct bw_fixed arg1, const struct bw_fixed arg2)
{
	struct bw_fixed res;

	res.value = arg1.value - arg2.value;

	return res;
}

struct bw_fixed mul(const struct bw_fixed arg1, const struct bw_fixed arg2)
{
	struct bw_fixed res;

	bool arg1_negative = arg1.value < 0;
	bool arg2_negative = arg2.value < 0;

	unsigned long long arg1_value = abs_i64(arg1.value);
	unsigned long long arg2_value = abs_i64(arg2.value);

	unsigned long long arg1_int = GET_INTEGER_PART(arg1_value);
	unsigned long long arg2_int = GET_INTEGER_PART(arg2_value);

	unsigned long long arg1_fra = GET_FRACTIONAL_PART(arg1_value);
	unsigned long long arg2_fra = GET_FRACTIONAL_PART(arg2_value);

	unsigned long long tmp;

	res.value = arg1_int * arg2_int;

	ASSERT(res.value <= MAX_I32);

	res.value <<= BITS_PER_FRACTIONAL_PART;

	tmp = arg1_int * arg2_fra;

	ASSERT(tmp <= (unsigned long long)(MAX_I64 - res.value));

	res.value += tmp;

	tmp = arg2_int * arg1_fra;

	ASSERT(tmp <= (unsigned long long)(MAX_I64 - res.value));

	res.value += tmp;

	tmp = arg1_fra * arg2_fra;

	tmp = (tmp >> BITS_PER_FRACTIONAL_PART) +
		(tmp >= (unsigned long long)(frc_to_fixed(1, 2).value));

	ASSERT(tmp <= (unsigned long long)(MAX_I64 - res.value));

	res.value += tmp;

	if (arg1_negative ^ arg2_negative)
		res.value = -res.value;
	return res;
}

struct bw_fixed bw_div(const struct bw_fixed arg1, const struct bw_fixed arg2)
{
	struct bw_fixed res = frc_to_fixed(arg1.value, arg2.value);
	return res;
}

struct bw_fixed fixed31_32_to_bw_fixed(long long raw)
{
	struct bw_fixed result = { 0 };

	if (raw < 0) {
		raw = -raw;
		result.value = -(raw >> (32 - BITS_PER_FRACTIONAL_PART));
	} else {
		result.value = raw >> (32 - BITS_PER_FRACTIONAL_PART);
	}

	return result;
}

bool equ(const struct bw_fixed arg1, const struct bw_fixed arg2)
{
	return arg1.value == arg2.value;
}

bool neq(const struct bw_fixed arg1, const struct bw_fixed arg2)
{
	return arg1.value != arg2.value;
}

bool leq(const struct bw_fixed arg1, const struct bw_fixed arg2)
{
	return arg1.value <= arg2.value;
}

bool geq(const struct bw_fixed arg1, const struct bw_fixed arg2)
{
	return arg1.value >= arg2.value;
}

bool ltn(const struct bw_fixed arg1, const struct bw_fixed arg2)
{
	return arg1.value < arg2.value;
}

bool gtn(const struct bw_fixed arg1, const struct bw_fixed arg2)
{
	return arg1.value > arg2.value;
}
