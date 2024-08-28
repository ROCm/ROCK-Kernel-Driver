/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _KCL_TRACEPOINT_H_
#define _KCL_TRACEPOINT_H_

#include <linux/tracepoint.h>

#ifdef HAVE_ASSIGN_STR_ONE_ARGUMENT
#define __amdkcl_assign_str(dst, src) __assign_str(dst)
#else
#define __amdkcl_assign_str(dst, src) __assign_str(dst, src)
#endif

#endif
