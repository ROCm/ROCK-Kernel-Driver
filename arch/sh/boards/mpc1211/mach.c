/*
 * linux/arch/sh/kernel/mach_mpc1211.c
 *
 * Copyright (C) 2001 Saito.K & Jeanne
 *
 * Machine vector for the Interface MPC-1211
 */

#include <linux/config.h>
#include <linux/init.h>

#include <asm/machvec.h>
#include <asm/rtc.h>
#include <asm/machvec_init.h>

#include <asm/mpc1211/io.h>

void heartbeat_mpc1211(void);
void setup_mpc1211(void);
void init_mpc1211_IRQ(void);

/*
 * The Machine Vector
 */

struct sh_machine_vector mv_mpc1211 __initmv = {
	.mv_name		= "MPC-1211",

	.mv_nr_irqs		= 48,

	.mv_inb			= mpc1211_inb,
	.mv_inw			= mpc1211_inw,
	.mv_inl			= mpc1211_inl,
	.mv_outb		= mpc1211_outb,
	.mv_outw		= mpc1211_outw,
	.mv_outl		= mpc1211_outl,

	.mv_inb_p		= mpc1211_inb_p,
	.mv_inw_p		= mpc1211_inw,
	.mv_inl_p		= mpc1211_inl,
	.mv_outb_p		= mpc1211_outb_p,
	.mv_outw_p		= mpc1211_outw,
	.mv_outl_p		= mpc1211_outl,

	.mv_insb		= mpc1211_insb,
	.mv_insw		= mpc1211_insw,
	.mv_insl		= mpc1211_insl,
	.mv_outsb		= mpc1211_outsb,
	.mv_outsw		= mpc1211_outsw,
	.mv_outsl		= mpc1211_outsl,

	.mv_readb		= mpc1211_readb,
	.mv_readw		= mpc1211_readw,
	.mv_readl		= mpc1211_readl,
	.mv_writeb		= mpc1211_writeb,
	.mv_writew		= mpc1211_writew,
	.mv_writel		= mpc1211_writel,

	.mv_ioremap		= generic_ioremap,
	.mv_iounmap		= generic_iounmap,

	.mv_isa_port2addr	= mpc1211_isa_port2addr,

	.mv_irq_demux		= mpc1211_irq_demux,

	.mv_init_arch		= setup_mpc1211,
	.mv_init_irq		= init_mpc1211_IRQ,
	//	mv_init_pci            = mpc1211_pcibios_init,

#ifdef CONFIG_HEARTBEAT
	.mv_heartbeat		= heartbeat_mpc1211,
#endif

	.mv_rtc_gettimeofday	= mpc1211_rtc_gettimeofday,
	.mv_rtc_settimeofday	= mpc1211_rtc_settimeofday,
};
ALIAS_MV(mpc1211)
