/* SPDX-License-Identifier: GPL-2.0 */
#ifndef AMDKCL_KERNEL_H
#define AMDKCL_KERNEL_H

#include <linux/kernel.h>

/* Copied from include/linux/kernel.h */
#ifndef u64_to_user_ptr
#define u64_to_user_ptr(x) (	\
{					\
	typecheck(u64, x);		\
	(void __user *)(uintptr_t)x;	\
}					\
)
#endif

#endif /* AMDKCL_KERNEL_H */
