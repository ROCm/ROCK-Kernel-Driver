/*
 * linux/arch/h8300/platform/h8300h/ints.c
 *
 * Yoshinori Sato <ysato@users.sourceforge.jp>
 *
 * Based on linux/arch/$(ARCH)/platform/$(PLATFORM)/ints.c
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file COPYING in the main directory of this archive
 * for more details.
 *
 * Copyright 1996 Roman Zippel
 * Copyright 1999 D. Jeff Dionne <jeff@rt-control.com>
 */

#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/kernel_stat.h>
#include <linux/seq_file.h>

#include <asm/system.h>
#include <asm/irq.h>
#include <asm/traps.h>
#include <asm/io.h>
#include <asm/setup.h>
#include <asm/gpio.h>
#include <asm/hardirq.h>
#include <asm/regs306x.h>

#define INTERNAL_IRQS (64)

#define EXT_IRQ0 12
#define EXT_IRQ1 13
#define EXT_IRQ2 14
#define EXT_IRQ3 15
#define EXT_IRQ4 16
#define EXT_IRQ5 17
#define EXT_IRQ6 18
#define EXT_IRQ7 19

#define WDT_IRQ 20

/* table for system interrupt handlers */
static irq_handler_t irq_list[SYS_IRQS];

/* The number of spurious interrupts */
volatile unsigned int num_spurious;

/* assembler routines */
asmlinkage void system_call(void);
asmlinkage void bad_interrupt(void);

/* irq node variables for the 32 (potential) on chip sources */
/*static irq_node_t *int_irq_list[INTERNAL_IRQS];*/
static int int_irq_count[INTERNAL_IRQS];

#if 0
static void int_badint(int irq, void *dev_id, struct pt_regs *fp)
{
	num_spurious += 1;
}
#endif

void init_IRQ(void)
{
	int i;

	for (i = 0; i < SYS_IRQS; i++) {
		irq_list[i].handler = NULL;
		irq_list[i].flags   = 0;
		irq_list[i].devname = NULL;
		irq_list[i].dev_id  = NULL;
	}

}

int request_irq(unsigned int irq, void (*handler)(int, void *, struct pt_regs *),
                unsigned long flags, const char *devname, void *dev_id)
{
	if (irq >= EXT_IRQ0 && irq <= EXT_IRQ3) {
		if (H8300_GPIO_RESERVE(H8300_GPIO_P8, 1 << (irq - EXT_IRQ0)) == 0)
			return 1;
		H8300_GPIO_DDR(H8300_GPIO_P8, (irq - EXT_IRQ0), 0);
	}
	if (irq >= EXT_IRQ4 && irq <= EXT_IRQ5) {
		if (H8300_GPIO_RESERVE(H8300_GPIO_P9, 1 << (irq - EXT_IRQ0)) == 0)
			return 1;
		H8300_GPIO_DDR(H8300_GPIO_P9, (irq - EXT_IRQ0), 0);
	}
	irq_list[irq].handler = handler;
	irq_list[irq].flags   = flags;
	irq_list[irq].devname = devname;
	irq_list[irq].dev_id  = dev_id;
	if (irq >= EXT_IRQ0 && irq <= EXT_IRQ5)
		*(volatile unsigned char *)IER |= 1 << (irq - EXT_IRQ0);
	return 0;
}

void free_irq(unsigned int irq, void *dev_id)
{
	if (irq_list[irq].dev_id != dev_id)
		printk("%s: Removing probably wrong IRQ %d from %s\n",
		       __FUNCTION__, irq, irq_list[irq].devname);
	if (irq >= EXT_IRQ0 && irq <= EXT_IRQ5)
		*(volatile unsigned char *)IER &= ~(1 << (irq - EXT_IRQ0));
	irq_list[irq].handler = NULL;
	irq_list[irq].flags   = 0;
	irq_list[irq].dev_id  = NULL;
	irq_list[irq].devname = NULL;
}

/*
 * Do we need these probe functions on the m68k?
 */
unsigned long probe_irq_on (void)
{
	return 0;
}

int probe_irq_off (unsigned long irqs)
{
	return 0;
}

struct int_regs {
	unsigned long ier;
	unsigned long isr;
	unsigned char mask;
};

#define REGS_DEF(ier,isr,mask) {ier,isr,mask}

const struct int_regs interrupt_registers[]= {
	REGS_DEF(IER,ISR,0x01),
	REGS_DEF(IER,ISR,0x02),
	REGS_DEF(IER,ISR,0x04),
	REGS_DEF(IER,ISR,0x08),
	REGS_DEF(IER,ISR,0x10),
	REGS_DEF(IER,ISR,0x20),
	REGS_DEF(IER,ISR,0x40),
	REGS_DEF(IER,ISR,0x80),
	REGS_DEF(TCSR,TCSR,0x20),
	REGS_DEF(RTMCSR,RTMCSR,0x40),
	REGS_DEF(0,0,0),
	REGS_DEF(ADCSR,ADCSR,0x40),
	REGS_DEF(TISRA,TISRA,0x10),
	REGS_DEF(TISRB,TISRB,0x10),
	REGS_DEF(TISRC,TISRC,0x10),
	REGS_DEF(0,0,0),
	REGS_DEF(TISRA,TISRA,0x20),
	REGS_DEF(TISRB,TISRB,0x20),
	REGS_DEF(TISRC,TISRC,0x20),
	REGS_DEF(0,0,0),
	REGS_DEF(TISRA,TISRA,0x40),
	REGS_DEF(TISRB,TISRB,0x40),
	REGS_DEF(TISRC,TISRC,0x40),
	REGS_DEF(0,0,0),
	REGS_DEF(_8TCR0,_8TCSR0,0x40),
	REGS_DEF(_8TCR0,_8TCSR0,0x80),
	REGS_DEF(_8TCR1,_8TCSR1,0xC0),
	REGS_DEF(_8TCR0,_8TCSR0,0x20),
	REGS_DEF(_8TCR2,_8TCSR2,0x40),
	REGS_DEF(_8TCR2,_8TCSR2,0x80),
	REGS_DEF(_8TCR3,_8TCSR3,0xC0),
	REGS_DEF(_8TCR2,_8TCSR2,0x20),
	REGS_DEF(DTCR0A,DTCR0A,0x0),
	REGS_DEF(DTCR0B,DTCR0B,0x0),
	REGS_DEF(DTCR1A,DTCR1A,0x0),
	REGS_DEF(DTCR1B,DTCR1B,0x0),
	REGS_DEF(0,0,0),
	REGS_DEF(0,0,0),
	REGS_DEF(0,0,0),
	REGS_DEF(0,0,0),
	REGS_DEF(SCR0,SSR0,0x40),
	REGS_DEF(SCR0,SSR0,0x40),
	REGS_DEF(SCR0,SSR0,0x80),
	REGS_DEF(SCR0,SSR0,0x04),
	REGS_DEF(SCR1,SSR1,0x40),
	REGS_DEF(SCR1,SSR1,0x40),
	REGS_DEF(SCR1,SSR1,0x80),
	REGS_DEF(SCR1,SSR1,0x04),
	REGS_DEF(SCR2,SSR2,0x40),
	REGS_DEF(SCR2,SSR2,0x40),
	REGS_DEF(SCR2,SSR2,0x80),
	REGS_DEF(SCR2,SSR2,0x04)
};

void enable_irq(unsigned int irq)
{
	unsigned char ier;
	const struct int_regs *regs=&interrupt_registers[irq - 12];
	if (irq == WDT_IRQ) {
		ier = ctrl_inb(TCSR);
		ier |= 0x20;
		ctrl_outb((0xa500 | ier),TCSR);
	} else {
		if ((irq > 12) && regs->ier) {
			ier = ctrl_inb(regs->ier);
			ier |= regs->mask;
			ctrl_outb(ier, regs->ier);
		} else
			panic("Unknown interrupt vector");
	}
}

void disable_irq(unsigned int irq)
{
	unsigned char ier;
	const struct int_regs *regs=&interrupt_registers[irq - 12];
	if (irq == WDT_IRQ) {
		ier = ctrl_inb(TCSR);
		ier &= ~0x20;
		ctrl_outb((0xa500 | ier),TCSR);
	} else {
		if ((irq > 12) && regs->ier) {
			ier = ctrl_inb(regs->ier);
			ier &= ~(regs->mask);
			ctrl_outb(ier, regs->ier);
		} else
			panic("Unknown interrupt vector");
	}
}

asmlinkage void process_int(unsigned long vec, struct pt_regs *fp)
{
	irq_enter();
	if (irq_list[vec].handler) {
		irq_list[vec].handler(vec, irq_list[vec].dev_id, fp);
		int_irq_count[vec]++;
	} else
		panic("No interrupt handler for %ld\n", vec);
	if (vec >= EXT_IRQ0 && vec <= EXT_IRQ5)
		*(volatile unsigned char *)ISR &= ~(1 << (vec - EXT_IRQ0));
	irq_exit();
}

int show_interrupts(struct seq_file *p, void *v)
{
	int i;

	for (i = 0; i < NR_IRQS; i++) {
		seq_printf(p, "%3d: %10u ",i,int_irq_count[i]);
		seq_printf(p, "%s\n", irq_list[i].devname);
	}

	return 0;
}

void init_irq_proc(void)
{
}
