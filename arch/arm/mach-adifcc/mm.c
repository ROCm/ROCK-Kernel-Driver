/*
 *  linux/arch/arm/mach-xscale/mm.c
 */
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/init.h>

#include <asm/io.h>
#include <asm/pgtable.h>
#include <asm/page.h>

#include <asm/mach/map.h>


static struct map_desc adifcc_io_desc[] __initdata = {
 /* on-board devices */
 { 0xff400000,   0x00400000,   0x00300000,   MT_DEVICE }
};

void __init adifcc_map_io(void)
{
	iotable_init(adifcc_io_desc, ARRAY_SIZE(adifcc_io_desc));
}
