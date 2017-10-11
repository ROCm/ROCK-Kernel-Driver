#ifndef AMDKCL_BITOPS_H
#define AMDKCL_BITOPS_H
#include <linux/version.h>

#if LINUX_VERSION_CODE < KERNEL_VERSION(3, 12, 0)
#if BITS_PER_LONG == 32
# define _BITOPS_LONG_SHIFT 5
#elif BITS_PER_LONG == 64
# define _BITOPS_LONG_SHIFT 6
#else
# error "Unexpected BITS_PER_LONG"
#endif
#endif

#endif /* AMDKCL_BITOPS_H */
