#ifndef _PARISC64_KERNEL_SYS32_H
#define _PARISC64_KERNEL_SYS32_H

#include <linux/compat.h>

/* Call a kernel syscall which will use kernel space instead of user
 * space for its copy_to/from_user.
 */
#define KERNEL_SYSCALL(ret, syscall, args...) \
{ \
    mm_segment_t old_fs = get_fs(); \
    set_fs(KERNEL_DS); \
    ret = syscall(args); \
    set_fs (old_fs); \
}

#ifdef CONFIG_COMPAT

typedef __u32 __sighandler_t32;

struct sigaction32 {
	__sighandler_t32 sa_handler;
	unsigned int sa_flags;
	compat_sigset_t sa_mask;		/* mask last for extensibility */
};

#endif

#endif
