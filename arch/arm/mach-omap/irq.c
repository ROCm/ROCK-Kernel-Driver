/*
 * linux/arch/arm/mach-omap/irq.c
 *
 * Interrupt handler for all OMAP boards
 *
 * Copyright (C) 2004 Nokia Corporation
 * Written by Tony Lindgren <tony@atomide.com>
 *
 * Completely re-written to support various OMAP chips with bank specific
 * interrupt handlers.
 *
 * Some snippets of the code taken from the older OMAP interrupt handler
 * Copyright (C) 2001 RidgeRun, Inc. Greg Lonnon <glonnon@ridgerun.com>
 *
 * GPIO interrupt handler moved to gpio.c by Juha Yrjola
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 *
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN
 * NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF
 * USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
 * ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * You should have received a copy of the  GNU General Public License along
 * with this program; if not, write  to the Free Software Foundation, Inc.,
 * 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <linux/config.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/sched.h>
#include <linux/interrupt.h>
#include <linux/ptrace.h>

#include <asm/hardware.h>
#include <asm/irq.h>
#include <asm/mach/irq.h>
#include <asm/arch/gpio.h>

#include <asm/io.h>

#include "irq.h"

static unsigned int banks = 0;
static struct omap_irq_bank irq_banks[MAX_NR_IRQ_BANKS];

static inline unsigned int irq_bank_readl(int bank, int offset)
{
	return omap_readl(irq_banks[bank].base_reg + offset);
}

static inline void irq_bank_writel(unsigned long value, int bank, int offset)
{
	omap_writel(value, irq_banks[bank].base_reg + offset);
}

/*
 * Ack routine for chips with register offsets of 0x100
 */
static void omap_offset_ack_irq(unsigned int irq)
{
	if (irq > 31)
		omap_writel(0x1, OMAP_IH2_BASE + IRQ_CONTROL_REG);

	omap_writel(0x1, OMAP_IH1_BASE + IRQ_CONTROL_REG);
}

/*
 * Mask routine for chips with register offsets of 0x100
 */
static void omap_offset_mask_irq(unsigned int irq)
{
	int bank = IRQ_TO_BANK(irq);

	if (bank) {
		omap_writel(
			omap_readl(OMAP_IH2_BASE + BANK_OFFSET(bank) + IRQ_MIR)
			| (1 << IRQ_BIT(irq)),
			OMAP_IH2_BASE + BANK_OFFSET(bank) + IRQ_MIR);
	} else {
		omap_writel(
			omap_readl(OMAP_IH1_BASE + IRQ_MIR)
			| (1 << IRQ_BIT(irq)),
			OMAP_IH1_BASE  + IRQ_MIR);
	}
}

/*
 * Unmask routine for chips with register offsets of 0x100
 */
static void omap_offset_unmask_irq(unsigned int irq)
{
	int bank = IRQ_TO_BANK(irq);

	if (bank) {
		omap_writel(
			omap_readl(OMAP_IH2_BASE + BANK_OFFSET(bank) + IRQ_MIR)
			& ~(1 << IRQ_BIT(irq)),
			OMAP_IH2_BASE + BANK_OFFSET(bank) + IRQ_MIR);
	} else {
		omap_writel(
			omap_readl(OMAP_IH1_BASE + IRQ_MIR)
			& ~(1 << IRQ_BIT(irq)),
			OMAP_IH1_BASE  + IRQ_MIR);
	}
}

static void omap_offset_mask_ack_irq(unsigned int irq)
{
	omap_offset_mask_irq(irq);
	omap_offset_ack_irq(irq);
}

/*
 * Given the irq number returns the bank number
 */
signed int irq_get_bank(unsigned int irq)
{
	int i;

	for (i = 0; i < banks; i++) {
		if (irq >= irq_banks[i].start_irq
		    && irq <= irq_banks[i].start_irq + BANK_NR_IRQS) {
			return i;
		}
	}

	printk(KERN_ERR "No irq handler found for irq %i\n", irq);

	return -ENODEV;
}

/*
 * Given the bank and irq number returns the irq bit at the bank register
 */
signed int irq_bank_get_bit(int bank, unsigned int irq)
{
	if (irq_banks[bank].start_irq > irq) {
		printk(KERN_ERR "Incorrect irq %i: bank %i offset %i\n",
		       irq, bank, irq_banks[bank].start_irq);
		return -ENODEV;
	}

	return irq - irq_banks[bank].start_irq;
}

/*
 * Allows tuning the IRQ type and priority
 *
 * NOTE: There is currently no OMAP fiq handler for Linux. Read the
 *	 mailing list threads on FIQ handlers if you are planning to
 *	 add a FIQ handler for OMAP.
 */
void omap_irq_set_cfg(int irq, int fiq, int priority, int irq_level)
{
	signed int bank;
	unsigned int irq_bit;
	unsigned long val, offset;


	bank = irq_get_bank(irq);

	if (bank < 0)
		return;

	irq_bit = irq_bank_get_bit(bank, irq);

	if (irq_bit < 0)
		return;

	/* FIQ is only availabe on bank 0 interrupts */
	fiq = bank ? 0 : (fiq & 0x1);

	val = fiq | ((priority & 0x1f) << 2) | ((irq_level & 0x1) << 1);

	offset = IRQ_ILR0 + irq_bit * 0x4;

	irq_bank_writel(val, bank, offset);
}

static struct omap_irq_desc *irq_bank_desc[] __initdata = {
	&omap730_bank0_irqs,
	&omap730_bank1_irqs,
	&omap730_bank2_irqs,
	&omap1510_bank0_irqs,
	&omap1510_bank1_irqs,
	&omap1610_bank0_irqs,
	&omap1610_bank1_irqs,
	&omap1610_bank2_irqs,
	&omap1610_bank3_irqs,
	NULL,
};

void __init omap_init_irq(void)
{
	int i,j, board_irq_type = 0, interrupts = 0;
	struct omap_irq_desc *entry;

	if (cpu_is_omap730()) {
		board_irq_type = OMAP_IRQ_TYPE730;
	} else if (cpu_is_omap1510()) {
		board_irq_type = OMAP_IRQ_TYPE1510;
	} else if (cpu_is_omap1610() || cpu_is_omap5912()) {
		board_irq_type = OMAP_IRQ_TYPE1610;
	}

	if (board_irq_type == 0) {
		printk("Could not detect OMAP type\n");
		return;
	}

	/* Scan through the interrupt bank maps and copy the right data */
	for (i = 0; (entry = irq_bank_desc[i]) != NULL; i++) {
		if (entry->cpu_type == board_irq_type) {
			printk("Type %i IRQs from %3i to %3i base at 0x%lx\n",
			       board_irq_type, entry->start_irq,
			       entry->start_irq + BANK_NR_IRQS, entry->base_reg);

			irq_banks[banks].start_irq = entry->start_irq;
			irq_banks[banks].level_map = entry->level_map;
			irq_banks[banks].base_reg = entry->base_reg;
			irq_banks[banks].mask_reg = entry->mask_reg;
			irq_banks[banks].ack_reg = entry->ack_reg;
			irq_banks[banks].handler = entry->handler;

			interrupts += BANK_NR_IRQS;
			banks++;
		}
	}

	printk("Found total of %i interrupts in %i interrupt banks\n",
	       interrupts, banks);

	/* Mask and clear all interrupts */
	for (i = 0; i < banks; i++) {
		irq_bank_writel(~0x0, i, IRQ_MIR);
		irq_bank_writel(0x0, i, IRQ_ITR);
	}

	/*
	 * Clear any pending interrupts
	 */
	irq_bank_writel(3, 0, IRQ_CONTROL_REG);
	irq_bank_writel(3, 1, IRQ_CONTROL_REG);

	/* Install the interrupt handlers for each bank */
	for (i = 0; i < banks; i++) {
		for (j = irq_banks[i].start_irq;
		     j <= irq_banks[i].start_irq + BANK_NR_IRQS; j++) {
			int irq_level;
			set_irq_chip(j, irq_banks[i].handler);
			set_irq_handler(j, do_level_IRQ);
			set_irq_flags(j, IRQF_VALID);
			irq_level = irq_banks[i].level_map
				>> (j - irq_banks[i].start_irq) & 1;
			omap_irq_set_cfg(j, 0, 0, irq_level);
		}
	}

	/* Unmask level 2 handler */
	omap_writel(0, irq_banks[0].mask_reg);
}

EXPORT_SYMBOL(omap_irq_set_cfg);
