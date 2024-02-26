/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _KCL_MMZONE_H
#define _KCL_MMZONE_H

#include <linux/mmzone.h>

#ifndef __ASSEMBLY__
#ifndef __GENERATING_BOUNDS_H

#ifndef MAX_PAGE_ORDER
#define MAX_PAGE_ORDER MAX_ORDER
#endif

#ifndef NR_PAGE_ORDERS
#define NR_PAGE_ORDERS (MAX_PAGE_ORDER + 1)
#endif

#endif
#endif

#endif
