/*
 *  linux/arch/arm/mach-rpc/riscpc.c
 *
 *  Copyright (C) 1998-2001 Russell King
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 *  Architecture specific fixups.
 */
#include <linux/tty.h>
#include <linux/delay.h>
#include <linux/pm.h>
#include <linux/init.h>

#include <asm/elf.h>
#include <asm/setup.h>
#include <asm/mach-types.h>
#include <asm/hardware.h>
#include <asm/page.h>
#include <asm/proc/domain.h>
#include <asm/setup.h>

#include <asm/mach/map.h>
#include <asm/mach/arch.h>

extern void rpc_init_irq(void);

extern unsigned int vram_size;

#if 0

unsigned int memc_ctrl_reg;
unsigned int number_mfm_drives;

static int __init parse_tag_acorn(const struct tag *tag)
{
	memc_ctrl_reg = tag->u.acorn.memc_control_reg;
	number_mfm_drives = tag->u.acorn.adfsdrives;

	switch (tag->u.acorn.vram_pages) {
	case 512:
		vram_size += PAGE_SIZE * 256;
	case 256:
		vram_size += PAGE_SIZE * 256;
	default:
		break;
	}
#if 0
	if (vram_size) {
		desc->video_start = 0x02000000;
		desc->video_end   = 0x02000000 + vram_size;
	}
#endif
	return 0;
}

__tagtable(ATAG_ACORN, parse_tag_acorn);

#endif

static struct map_desc rpc_io_desc[] __initdata = {
 { SCREEN_BASE,	SCREEN_START,	2*1048576, DOMAIN_IO, 0, 1, 0, 0 }, /* VRAM		*/
 { IO_BASE,	IO_START,	IO_SIZE	 , DOMAIN_IO, 0, 1, 0, 0 }, /* IO space		*/
 { EASI_BASE,	EASI_START,	EASI_SIZE, DOMAIN_IO, 0, 1, 0, 0 }, /* EASI space	*/
 LAST_DESC
};

void __init rpc_map_io(void)
{
	iotable_init(rpc_io_desc);

	/*
	 * RiscPC can't handle half-word loads and stores
	 */
	elf_hwcap &= ~HWCAP_HALF;
}

MACHINE_START(RISCPC, "Acorn-RiscPC")
	MAINTAINER("Russell King")
	BOOT_MEM(0x10000000, 0x03000000, 0xe0000000)
	BOOT_PARAMS(0x10000100)
	DISABLE_PARPORT(0)
	DISABLE_PARPORT(1)
	MAPIO(rpc_map_io)
	INITIRQ(rpc_init_irq)
MACHINE_END
