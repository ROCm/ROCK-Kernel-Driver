/*
 *	$Id: mach.c,v 1.4 2003/05/20 03:04:36 lethal Exp $
 *	SEGA Dreamcast machine vector
 */

#include <linux/config.h>
#include <linux/init.h>
#include <linux/time.h>

#include <asm/machvec.h>
#include <asm/machvec_init.h>

#include <asm/io_generic.h>
#include <asm/dreamcast/io.h>
#include <asm/irq.h>

void __init dreamcast_pcibios_init(void);

/*
 * The Machine Vector
 */

struct sh_machine_vector mv_dreamcast __initmv = {
	.mv_nr_irqs		= NR_IRQS,

	.mv_inb			= generic_inb,
	.mv_inw			= generic_inw,
	.mv_inl			= generic_inl,
	.mv_outb		= generic_outb,
	.mv_outw		= generic_outw,
	.mv_outl		= generic_outl,

	.mv_inb_p		= generic_inb_p,
	.mv_inw_p		= generic_inw,
	.mv_inl_p		= generic_inl,
	.mv_outb_p		= generic_outb_p,
	.mv_outw_p		= generic_outw,
	.mv_outl_p		= generic_outl,

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

	.mv_ioremap		= generic_ioremap,
	.mv_iounmap		= generic_iounmap,

	.mv_isa_port2addr	= dreamcast_isa_port2addr,
	.mv_irq_demux		= systemasic_irq_demux,
};
ALIAS_MV(dreamcast)
