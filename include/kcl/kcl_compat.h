#ifndef AMDKCL_COMPATE_H
#define AMDKCL_COMPATE_H

#include <linux/compat.h>

#ifndef in_compat_syscall
static inline bool in_compat_syscall(void) { return is_compat_task(); }
#endif

#endif /* AMDKCL_COMPATE_H */
