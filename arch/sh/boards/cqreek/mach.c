/* $Id: mach.c,v 1.1.2.4.2.1 2003/01/10 17:26:32 lethal Exp $
 *
 * arch/sh/kernel/setup_cqreek.c
 *
 * Copyright (C) 2000  Niibe Yutaka
 *
 * CqREEK IDE/ISA Bridge Support.
 *
 */

#include <asm/rtc.h>
#include <asm/io.h>
#include <asm/io_generic.h>
#include <asm/machvec.h>
#include <asm/machvec_init.h>
#include <asm/cqreek/cqreek.h>

/*
 * The Machine Vector
 */

struct sh_machine_vector mv_cqreek __initmv = {
#if defined(CONFIG_CPU_SH4)
	.mv_nr_irqs		= 48,
#elif defined(CONFIG_CPU_SUBTYPE_SH7708)
	.mv_nr_irqs		= 32,
#elif defined(CONFIG_CPU_SUBTYPE_SH7709)
	.mv_nr_irqs		= 61,
#endif

	.mv_inb			= generic_inb,
	.mv_inw			= generic_inw,
	.mv_inl			= generic_inl,
	.mv_outb		= generic_outb,
	.mv_outw		= generic_outw,
	.mv_outl		= generic_outl,

	.mv_inb_p		= generic_inb_p,
	.mv_inw_p		= generic_inw_p,
	.mv_inl_p		= generic_inl_p,
	.mv_outb_p		= generic_outb_p,
	.mv_outw_p		= generic_outw_p,
	.mv_outl_p		= generic_outl_p,

	.mv_insb		= generic_insb,
	.mv_insw		= generic_insw,
	.mv_insl		= generic_insl,
	.mv_outsb		= generic_outsb,
	.mv_outsw		= generic_outsw,
	.mv_outsl		= generic_outsl,

	.mv_readb		= generic_readb,
	.mv_readw		= generic_readw,
	.mv_readl		= generic_readl,
	.mv_writeb		= generic_writeb,
	.mv_writew		= generic_writew,
	.mv_writel		= generic_writel,

	.mv_init_irq		= init_cqreek_IRQ,

	.mv_isa_port2addr	= cqreek_port2addr,

	.mv_ioremap		= generic_ioremap,
	.mv_iounmap		= generic_iounmap,
};
ALIAS_MV(cqreek)
