/* SPDX-License-Identifier: GPL-2.0 */
#ifndef AMDKCL_KERNEL_H
#define AMDKCL_KERNEL_H

#include <linux/kernel.h>
#include <linux/gfp.h>

/* Copied from include/linux/kernel.h */
#ifndef u64_to_user_ptr
#define u64_to_user_ptr(x) (	\
{					\
	typecheck(u64, x);		\
	(void __user *)(uintptr_t)x;	\
}					\
)
#endif

#ifndef __GFP_RETRY_MAYFAIL
/* Copied from include/linux/gfp.h and modified for KCL */
#define __GFP_RETRY_MAYFAIL __GFP_NORETRY
#endif

#ifndef ALIGN_DOWN
#define ALIGN_DOWN(x, a)	__ALIGN_KERNEL((x) - ((a) - 1), (a))
#endif /* ALIGN_DOWN */

#ifndef ___GFP_KSWAPD_RECLAIM
#define ___GFP_KSWAPD_RECLAIM	0x00u
#define __GFP_KSWAPD_RECLAIM	((__force gfp_t)___GFP_KSWAPD_RECLAIM) /* kswapd can wake */
#endif /* ___GFP_KSWAPD_RECLAIM */

/* v5.13-335-gf39650de687e ("kernel.h: split out panic and oops helpers") */
#ifndef TAINT_CPU_OUT_OF_SPEC
#define TAINT_CPU_OUT_OF_SPEC TAINT_UNSAFE_SMP
#endif

#endif /* AMDKCL_KERNEL_H */
