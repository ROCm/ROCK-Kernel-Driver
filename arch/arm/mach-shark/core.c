/*
 *  linux/arch/arm/mach-shark/arch.c
 *
 *  Architecture specific stuff.
 */
#include <linux/kernel.h>
#include <linux/init.h>

#include <asm/setup.h>
#include <asm/mach-types.h>
#include <asm/io.h>

#include <asm/mach/map.h>
#include <asm/mach/arch.h>

extern void shark_init_irq(void);

static struct map_desc shark_io_desc[] __initdata = {
	{ IO_BASE	, IO_START	, IO_SIZE	, MT_DEVICE }
};

static void __init shark_map_io(void)
{
	iotable_init(shark_io_desc, ARRAY_SIZE(shark_io_desc));
}

MACHINE_START(SHARK, "Shark")
	MAINTAINER("Alexander Schulz")
	BOOT_MEM(0x08000000, 0x40000000, 0xe0000000)
	BOOT_PARAMS(0x08003000)
	MAPIO(shark_map_io)
	INITIRQ(shark_init_irq)
MACHINE_END
