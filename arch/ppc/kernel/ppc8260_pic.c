
#include <linux/stddef.h>
#include <linux/init.h>
#include <linux/sched.h>
#include <linux/signal.h>
#include <asm/irq.h>
#include <asm/immap_8260.h>
#include <asm/mpc8260.h>
#include "ppc8260_pic.h"

/* The 8260 internal interrupt controller.  It is usually
 * the only interrupt controller.
 * There are two 32-bit registers (high/low) for up to 64
 * possible interrupts.
 *
 * Now, the fun starts.....Interrupt Numbers DO NOT MAP
 * in a simple arithmetic fashion to mask or pending registers.
 * That is, interrupt 4 does not map to bit position 4.
 * We create two tables, indexed by vector number, to indicate
 * which register to use and which bit in the register to use.
 */
static	u_char	irq_to_siureg[] = {
	1, 1, 1, 1, 1, 1, 1, 1,
	1, 1, 1, 1, 1, 1, 1, 1,
	0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0,
	1, 1, 1, 1, 1, 1, 1, 1,
	1, 1, 1, 1, 1, 1, 1, 1,
	0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0
};

static	u_char	irq_to_siubit[] = {
	31, 16, 17, 18, 19, 20, 21, 22,
	23, 24, 25, 26, 27, 28, 29, 30,
	29, 30, 16, 17, 18, 19, 20, 21,
	22, 23, 24, 25, 26, 27, 28, 31,
	 0,  1,  2,  3,  4,  5,  6,  7,
	 8,  9, 10, 11, 12, 13, 14, 15,
	15, 14, 13, 12, 11, 10,  9,  8,
	 7,  6,  5,  4,  3,  2,  1,  0
};

static void m8260_mask_irq(unsigned int irq_nr)
{
	int	bit, word;
	volatile uint	*simr;

	bit = irq_to_siubit[irq_nr];
	word = irq_to_siureg[irq_nr];

	simr = &(immr->im_intctl.ic_simrh);
	ppc_cached_irq_mask[word] &= ~(1 << (31 - bit));
	simr[word] = ppc_cached_irq_mask[word];
}

static void m8260_unmask_irq(unsigned int irq_nr)
{
	int	bit, word;
	volatile uint	*simr;

	bit = irq_to_siubit[irq_nr];
	word = irq_to_siureg[irq_nr];

	simr = &(immr->im_intctl.ic_simrh);
	ppc_cached_irq_mask[word] |= (1 << (31 - bit));
	simr[word] = ppc_cached_irq_mask[word];
}

static void m8260_mask_and_ack(unsigned int irq_nr)
{
	int	bit, word;
	volatile uint	*simr, *sipnr;

	bit = irq_to_siubit[irq_nr];
	word = irq_to_siureg[irq_nr];

	simr = &(immr->im_intctl.ic_simrh);
	sipnr = &(immr->im_intctl.ic_sipnrh);
	ppc_cached_irq_mask[word] &= ~(1 << (31 - bit));
	simr[word] = ppc_cached_irq_mask[word];
	sipnr[word] = 1 << (31 - bit);
}

struct hw_interrupt_type ppc8260_pic = {
	" 8260 SIU  ",
	NULL,
	NULL,
	m8260_unmask_irq,
	m8260_mask_irq,
	m8260_mask_and_ack,
	0
};


int
m8260_get_irq(struct pt_regs *regs)
{
	int irq;
        unsigned long bits;

        /* For MPC8260, read the SIVEC register and shift the bits down
         * to get the irq number.         */
        bits = immr->im_intctl.ic_sivec;
        irq = bits >> 26;
#if 0
        irq += ppc8260_pic.irq_offset;
#endif
	return irq;
}

