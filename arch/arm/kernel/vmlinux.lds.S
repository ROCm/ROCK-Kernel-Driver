#include <linux/config.h>

#ifdef CONFIG_CPU_26

#ifdef CONFIG_ROM_KERNEL

#include "vmlinux-armo-rom.lds.in"

#else

#include "vmlinux-armo.lds.in"

#endif

#endif

#ifdef CONFIG_CPU_32

#include "vmlinux-armv.lds.in"

#endif
