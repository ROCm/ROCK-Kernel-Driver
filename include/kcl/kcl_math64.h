#ifndef AMDKCL_MATH64_H
#define AMDKCL_MATH64_H

#include <linux/math64.h>

#ifndef DIV64_U64_ROUND_UP
#define DIV64_U64_ROUND_UP(ll, d)	\
	({ u64 _tmp = (d); div64_u64((ll) + _tmp - 1, _tmp); })
#endif

#endif