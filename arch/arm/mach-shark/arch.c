/*
 *  linux/arch/arm/mach-shark/arch.c
 *
 *  Architecture specific fixups.  This is where any
 *  parameters in the params struct are fixed up, or
 *  any additional architecture specific information
 *  is pulled from the params struct.
 */
#include <linux/tty.h>
#include <linux/delay.h>
#include <linux/pm.h>
#include <linux/init.h>

#include <asm/hardware/dec21285.h>
#include <asm/elf.h>
#include <asm/setup.h>
#include <asm/mach-types.h>

#include <asm/mach/arch.h>

extern void setup_initrd(unsigned int start, unsigned int size);
extern void setup_ramdisk(int doload, int prompt, int start, unsigned int rd_sz);
extern void __init footbridge_map_io(void);
extern void __init shark_map_io(void);

MACHINE_START(SHARK, "Shark")
	MAINTAINER("Alexander Schulz")
	BOOT_MEM(0x08000000, 0x40000000, 0xe0000000)
	VIDEO(0x06000000, 0x061fffff)
	MAPIO(shark_map_io)
MACHINE_END
