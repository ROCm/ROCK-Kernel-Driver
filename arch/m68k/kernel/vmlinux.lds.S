#include <linux/config.h>

#ifdef CONFIG_SUN3
#include "vmlinux-sun3.lds"
#else
#include "vmlinux-std.lds"
#endif
