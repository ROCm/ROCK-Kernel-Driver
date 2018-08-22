#ifndef AMDKCL_COMPATE_H
#define AMDKCL_COMPATE_H

#include <linux/version.h>
#include <linux/compat.h>

#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 6, 0)
#define in_compat_syscall is_compat_task
#endif

#endif /* AMDKCL_COMPATE_H */
