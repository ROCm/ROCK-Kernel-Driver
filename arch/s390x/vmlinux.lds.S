#include <linux/config.h>

#ifdef CONFIG_SHARED_KERNEL
#include "vmlinux-shared.lds"
#else
#include "vmlinux.lds"
#endif