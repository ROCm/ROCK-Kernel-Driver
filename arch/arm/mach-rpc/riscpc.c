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
#include <linux/kernel.h>
#include <linux/tty.h>
#include <linux/delay.h>
#include <linux/pm.h>
#include <linux/init.h>
#include <linux/sched.h>
#include <linux/interrupt.h>

#include <asm/elf.h>
#include <asm/io.h>
#include <asm/mach-types.h>
#include <asm/hardware.h>
#include <asm/page.h>
#include <asm/domain.h>
#include <asm/setup.h>

#include <asm/mach/map.h>
#include <asm/mach/arch.h>
#include <asm/mach/time.h>

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
 { SCREEN_BASE,	SCREEN_START,	2*1048576, MT_DEVICE }, /* VRAM		*/
 { IO_BASE,	IO_START,	IO_SIZE	 , MT_DEVICE }, /* IO space		*/
 { EASI_BASE,	EASI_START,	EASI_SIZE, MT_DEVICE }  /* EASI space	*/
};

void __init rpc_map_io(void)
{
	iotable_init(rpc_io_desc, ARRAY_SIZE(rpc_io_desc));

	/*
	 * Turn off floppy.
	 */
	outb(0xc, 0x3f2);

	/*
	 * RiscPC can't handle half-word loads and stores
	 */
	elf_hwcap &= ~HWCAP_HALF;
}

static irqreturn_t
rpc_timer_interrupt(int irq, void *dev_id, struct pt_regs *regs)
{
	do_timer(regs);
	do_set_rtc();
	do_profile(regs);

	return IRQ_HANDLED;
}

static struct irqaction rpc_timer_irq = {
	.name		= "RiscPC Timer Tick",
	.flags		= SA_INTERRUPT,
	.handler	= rpc_timer_interrupt
};

/*
 * Set up timer interrupt.
 */
void __init rpc_init_time(void)
{
	extern void ioctime_init(void);
	ioctime_init();

	setup_irq(IRQ_TIMER, &rpc_timer_irq);
}

MACHINE_START(RISCPC, "Acorn-RiscPC")
	MAINTAINER("Russell King")
	BOOT_MEM(0x10000000, 0x03000000, 0xe0000000)
	BOOT_PARAMS(0x10000100)
	DISABLE_PARPORT(0)
	DISABLE_PARPORT(1)
	MAPIO(rpc_map_io)
	INITIRQ(rpc_init_irq)
	INITTIME(rpc_init_time)
MACHINE_END
