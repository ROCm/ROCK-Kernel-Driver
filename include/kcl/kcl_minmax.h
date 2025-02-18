/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _KCL_MINMAX_H
#define _KCL_MINMAX_H

#include <linux/minmax.h>

#ifndef umin
#define umin(x, y)	\
	min((x) + 0u + 0ul + 0ull, (y) + 0u + 0ul + 0ull)
#endif

#endif /* _KCL_MINMAX_H */
