#include <linux/config.h>

#ifdef CONFIG_CPU_26

#ifdef CONFIG_ROM_KERNEL

#define DATAADDR 0x02080000
#define TEXTADDR 0x03800000
#include "vmlinux-armo-rom.lds.in"

#else

#define TEXTADDR 0x02080000
#include "vmlinux-armo.lds.in"

#endif

#endif

#ifdef CONFIG_CPU_32

#define TEXTADDR 0xC0008000
#include "vmlinux-armv.lds.in"

#endif
