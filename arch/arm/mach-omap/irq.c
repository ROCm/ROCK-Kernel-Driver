/*
 * linux/arch/arm/mach-omap/irq.c
 *
 * Interrupt handler for OMAP-1510 and 1610
 *
 * Copyright (C) 2001 RidgeRun, Inc.
 * Author: Greg Lonnon <glonnon@ridgerun.com>
 *
 * Modified for OMAP-1610 by Tony Lindgren <tony.lindgren@nokia.com>
 * GPIO interrupt handler moved to gpio.c for OMAP-1610 by Juha Yrjola
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
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
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

#define NUM_IRQS	IH_BOARD_BASE

static void mask_irq(unsigned int irq);
static void unmask_irq(unsigned int irq);
static void ack_irq(unsigned int irq);

static inline void
write_ih(int level, int reg, u32 value)
{
	if (cpu_is_omap1510()) {
		__raw_writel(value,
			(IO_ADDRESS((level ? OMAP_IH2_BASE : OMAP_IH1_BASE) +
				(reg))));
	} else {
		if (level) {
			__raw_writel(value,
				IO_ADDRESS(OMAP_IH2_BASE + ((level - 1) << 8) +
					reg));
		} else {
			__raw_writel(value, IO_ADDRESS(OMAP_IH1_BASE + reg));
		}
	}
}

static inline u32
read_ih(int level, int reg)
{
	if (cpu_is_omap1510()) {
		return __raw_readl((IO_ADDRESS((level ? OMAP_IH2_BASE : OMAP_IH1_BASE)
					 + (reg))));
	} else {
		if (level) {
			return __raw_readl(IO_ADDRESS(OMAP_IH2_BASE +
					((level - 1) << 8) + reg));
		} else {
			return __raw_readl(IO_ADDRESS(OMAP_IH1_BASE + reg));
		}
	}
}

static inline int
get_level(int irq)
{
	if (cpu_is_omap1510()) {
		return (((irq) < IH2_BASE) ? 0 : 1);
	} else {
		if (irq < IH2_BASE)
			return 0;
		else {
			return (irq >> 5);
		}
	}
}

static inline int
get_irq_num(int irq)
{
	if (cpu_is_omap1510()) {
		return (((irq) < IH2_BASE) ? irq : irq - IH2_BASE);
	} else {
		return irq & 0x1f;
	}
}

static void
mask_irq(unsigned int irq)
{
	int level = get_level(irq);
	int irq_num = get_irq_num(irq);
	u32 mask = read_ih(level, IRQ_MIR) | (1 << irq_num);
	write_ih(level, IRQ_MIR, mask);
}

static void
ack_irq(unsigned int irq)
{
	int level = get_level(irq);

	if (level > 1)
		level = 1;
	do {
		write_ih(level, IRQ_CONTROL_REG, 0x1);
		/*
		 * REVISIT: So says the TRM:
		 *	if (level) write_ih(0, ITR, 0);
		 */
	} while (level--);
}

void
unmask_irq(unsigned int irq)
{
	int level = get_level(irq);
	int irq_num = get_irq_num(irq);
	u32 mask = read_ih(level, IRQ_MIR) & ~(1 << irq_num);

	write_ih(level, IRQ_MIR, mask);
}

static void
mask_ack_irq(unsigned int irq)
{
	mask_irq(irq);
	ack_irq(irq);
}

static struct irqchip omap_normal_irq = {
	.ack		= mask_ack_irq,
	.mask		= mask_irq,
	.unmask		= unmask_irq,
};

static void
irq_priority(int irq, int fiq, int priority, int trigger)
{
	int level, irq_num;
	unsigned long reg_value, reg_addr;

	level = get_level(irq);
	irq_num = get_irq_num(irq);
	/* FIQ is only available on level 0 interrupts */
	fiq = level ? 0 : (fiq & 0x1);
	reg_value = (fiq) | ((priority & 0x1f) << 2) |
		((trigger & 0x1) << 1);
	reg_addr = (IRQ_ILR0 + irq_num * 0x4);
	write_ih(level, reg_addr, reg_value);
}

void __init
omap_init_irq(void)
{
	int i, irq_count, irq_bank_count = 0;
	uint *trigger;

	if (cpu_is_omap1510()) {
		static uint trigger_1510[2] = {
			0xb3febfff, 0xffbfffed
		};
		irq_bank_count = 2;
		irq_count = 64;
		trigger = trigger_1510;
	}
	if (cpu_is_omap1610()) {
		static uint trigger_1610[5] = {
			0xb3fefe8f, 0xfffff7ff, 0xffffffff
		};
		irq_bank_count = 5;
		irq_count = 160;
		trigger = trigger_1610;
	}
	if (cpu_is_omap730()) {
		static uint trigger_730[] = {
			0xb3f8e22f, 0xfdb9c1f2, 0x800040f3
		};
		irq_bank_count = 3;
		irq_count = 96;
		trigger = trigger_730;
	}

	for (i = 0; i < irq_bank_count; i++) {
		/* Mask and clear all interrupts */
		write_ih(i, IRQ_MIR, ~0x0);
		write_ih(i, IRQ_ITR, 0x0);
	}

	/* Clear any pending interrupts */
	write_ih(1, IRQ_CONTROL_REG, 3);
	write_ih(0, IRQ_CONTROL_REG, 3);

	for (i = 0; i < irq_count; i++) {
		set_irq_chip(i, &omap_normal_irq);
		set_irq_handler(i, do_level_IRQ);
		set_irq_flags(i, IRQF_VALID);

		irq_priority(i, 0, 0, trigger[get_level(i)] >> get_irq_num(i) & 1);
	}
	unmask_irq(INT_IH2_IRQ);
}
