/*
 *  irq.c,  Interrupt routines for the NEC Eagle/Hawk board.
 *
 *  Copyright (C) 2002  MontaVista Software, Inc.
 *    Author: Yoichi Yuasa <yyuasa@mvista.com, or source@mvista.com>
 *  Copyright (C) 2004  Yoichi Yuasa <yuasa@hh.iij4u.or.jp>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */
/*
 * Changes:
 *  MontaVista Software Inc. <yyuasa@mvista.com> or <source@mvista.com>
 *  - New creation, NEC Eagle is supported.
 *  - Added support for NEC Hawk.
 *
 *  Yoichi Yuasa <yuasa@hh.iij4u.or.jp>
 *  - Changed from board_irq_init to driver module.
 */
#include <linux/config.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/types.h>

#include <asm/io.h>
#include <asm/vr41xx/eagle.h>

MODULE_DESCRIPTION("IRQ module driver for NEC Eagle/Hawk");
MODULE_AUTHOR("Yoichi Yuasa <yyuasa@mvista.com>");
MODULE_LICENSE("GPL");

static void enable_pciint_irq(unsigned int irq)
{
	uint8_t val;

	val = readb(NEC_EAGLE_PCIINTMSKREG);
	val |= (uint8_t)1 << (irq - PCIINT_IRQ_BASE);
	writeb(val, NEC_EAGLE_PCIINTMSKREG);
}

static void disable_pciint_irq(unsigned int irq)
{
	uint8_t val;

	val = readb(NEC_EAGLE_PCIINTMSKREG);
	val &= ~((uint8_t)1 << (irq - PCIINT_IRQ_BASE));
	writeb(val, NEC_EAGLE_PCIINTMSKREG);
}

static unsigned int startup_pciint_irq(unsigned int irq)
{
	enable_pciint_irq(irq);
	return 0; /* never anything pending */
}

#define shutdown_pciint_irq	disable_pciint_irq
#define ack_pciint_irq		disable_pciint_irq

static void end_pciint_irq(unsigned int irq)
{
	if (!(irq_desc[irq].status & (IRQ_DISABLED | IRQ_INPROGRESS)))
		enable_pciint_irq(irq);
}

static struct hw_interrupt_type pciint_irq_type = {
	.typename	= "PCIINT",
	.startup	= startup_pciint_irq,
	.shutdown 	= shutdown_pciint_irq,
	.enable       	= enable_pciint_irq,
	.disable	= disable_pciint_irq,
	.ack		= ack_pciint_irq,
	.end		= end_pciint_irq,
};

static void enable_sdbint_irq(unsigned int irq)
{
	uint8_t val;

	val = readb(NEC_EAGLE_SDBINTMSK);
	val |= (uint8_t)1 << (irq - SDBINT_IRQ_BASE);
	writeb(val, NEC_EAGLE_SDBINTMSK);
}

static void disable_sdbint_irq(unsigned int irq)
{
	uint8_t val;

	val = readb(NEC_EAGLE_SDBINTMSK);
	val &= ~((uint8_t)1 << (irq - SDBINT_IRQ_BASE));
	writeb(val, NEC_EAGLE_SDBINTMSK);
}

static unsigned int startup_sdbint_irq(unsigned int irq)
{
	enable_sdbint_irq(irq);
	return 0; /* never anything pending */
}

#define shutdown_sdbint_irq	disable_sdbint_irq
#define ack_sdbint_irq		disable_sdbint_irq

static void end_sdbint_irq(unsigned int irq)
{
	if (!(irq_desc[irq].status & (IRQ_DISABLED | IRQ_INPROGRESS)))
		enable_sdbint_irq(irq);
}

static struct hw_interrupt_type sdbint_irq_type = {
	.typename	= "SDBINT",
	.startup	= startup_sdbint_irq,
	.shutdown	= shutdown_sdbint_irq,
	.enable		= enable_sdbint_irq,
	.disable	= disable_sdbint_irq,
	.ack		= ack_sdbint_irq,
	.end		= end_sdbint_irq,
};

static int eagle_get_irq_number(int irq)
{
	uint8_t sdbint, pciint;
	int i;

	sdbint = readb(NEC_EAGLE_SDBINT);
	sdbint &= (NEC_EAGLE_SDBINT_DEG | NEC_EAGLE_SDBINT_ENUM |
	           NEC_EAGLE_SDBINT_SIO1INT | NEC_EAGLE_SDBINT_SIO2INT |
	           NEC_EAGLE_SDBINT_PARINT);
	pciint = readb(NEC_EAGLE_PCIINTREG);
	pciint &= (NEC_EAGLE_PCIINT_CP_INTA | NEC_EAGLE_PCIINT_CP_INTB |
	           NEC_EAGLE_PCIINT_CP_INTC | NEC_EAGLE_PCIINT_CP_INTD |
	           NEC_EAGLE_PCIINT_LANINT);

	for (i = 1; i < 6; i++)
		if (sdbint & (0x01 << i))
			return SDBINT_IRQ_BASE + i;

	for (i = 0; i < 5; i++)
		if (pciint & (0x01 << i))
			return PCIINT_IRQ_BASE + i;

	return -EINVAL;
}

static int __devinit eagle_irq_init(void)
{
	int i, retval;

	writeb(0, NEC_EAGLE_SDBINTMSK);
	writeb(0, NEC_EAGLE_PCIINTMSKREG);

	vr41xx_set_irq_trigger(PCISLOT_PIN, TRIGGER_LEVEL, SIGNAL_THROUGH);
	vr41xx_set_irq_level(PCISLOT_PIN, LEVEL_HIGH);

	vr41xx_set_irq_trigger(FPGA_PIN, TRIGGER_LEVEL, SIGNAL_THROUGH);
	vr41xx_set_irq_level(FPGA_PIN, LEVEL_HIGH);

	vr41xx_set_irq_trigger(DCD_PIN, TRIGGER_EDGE, SIGNAL_HOLD);
	vr41xx_set_irq_level(DCD_PIN, LEVEL_LOW);

	for (i = SDBINT_IRQ_BASE; i <= SDBINT_IRQ_LAST; i++)
		irq_desc[i].handler = &sdbint_irq_type;

	for (i = PCIINT_IRQ_BASE; i <= PCIINT_IRQ_LAST; i++)
		irq_desc[i].handler = &pciint_irq_type;

	retval = vr41xx_cascade_irq(FPGA_CASCADE_IRQ, eagle_get_irq_number);
	if (retval != 0)
		printk(KERN_ERR "eagle: Cannot cascade IRQ %d\n", FPGA_CASCADE_IRQ);

	return retval;
}

static void __devexit eagle_irq_exit(void)
{
	free_irq(FPGA_CASCADE_IRQ, NULL);
}

module_init(eagle_irq_init);
module_exit(eagle_irq_exit);
