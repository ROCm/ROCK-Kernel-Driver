/*
 * BRIEF MODULE DESCRIPTION
 * Code to handle irqs on GT64120A boards
 *  Derived from mips/orion and Cort <cort@fsmlabs.com>
 *
 * Copyright (C) 2000 RidgeRun, Inc.
 * Author: RidgeRun, Inc.
 *   glonnon@ridgerun.com, skranz@ridgerun.com, stevej@ridgerun.com
 *
 *  This program is free software; you can redistribute  it and/or modify it
 *  under  the terms of  the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the  License, or (at your
 *  option) any later version.
 *
 *  THIS  SOFTWARE  IS PROVIDED   ``AS  IS'' AND   ANY  EXPRESS OR IMPLIED
 *  WARRANTIES,   INCLUDING, BUT NOT  LIMITED  TO, THE IMPLIED WARRANTIES OF
 *  MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN
 *  NO  EVENT  SHALL   THE AUTHOR  BE    LIABLE FOR ANY   DIRECT, INDIRECT,
 *  INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 *  NOT LIMITED   TO, PROCUREMENT OF  SUBSTITUTE GOODS  OR SERVICES; LOSS OF
 *  USE, DATA,  OR PROFITS; OR  BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
 *  ANY THEORY OF LIABILITY, WHETHER IN  CONTRACT, STRICT LIABILITY, OR TORT
 *  (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 *  THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 *  You should have received a copy of the  GNU General Public License along
 *  with this program; if not, write  to the Free Software Foundation, Inc.,
 *  675 Mass Ave, Cambridge, MA 02139, USA.
 *
 */
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/kernel_stat.h>
#include <linux/module.h>
#include <linux/signal.h>
#include <linux/sched.h>
#include <linux/types.h>
#include <linux/interrupt.h>
#include <linux/ioport.h>
#include <linux/timex.h>
#include <linux/slab.h>
#include <linux/random.h>
#include <asm/bitops.h>
#include <asm/bootinfo.h>
#include <asm/io.h>
#include <asm/irq.h>
#include <asm/mipsregs.h>
#include <asm/system.h>
#include <asm/galileo-boards/ev64120int.h>

#define MAX_AGENTS_PER_INT 21	/*  Random number  */
unsigned char pci_int_irq[MAX_AGENTS_PER_INT];
static int max_interrupts = 0;

asmlinkage void pci_intA(struct pt_regs *regs)
{
	unsigned int count = 0;

	/* This must be a joke - Ralf  */
	for (count = 0; count < max_interrupts; count++)
		do_IRQ(pci_int_irq[count], regs);
}

asmlinkage void pci_intD(struct pt_regs *regs)
{
	unsigned int count = 0;

	/* Encore une fois - This must be a joke - Ralf  */
	for (count = 0; count < max_interrupts; count++)
		do_IRQ(pci_int_irq[count], regs);
}

/*
 * Now this is scarry.  A disable_irq(2) or disable_irq(5) would just
 * accidently disable a pci irq.  It shouldn't happen but may just leaving
 * these always enabled or use some reference counting wouldn't be such a
 * bad thing.
 */
static void disable_ev64120_irq(unsigned int irq_nr)
{
	unsigned long flags;

	local_irq_save(flags);
	if (irq_nr >= 8) {
		/* All PCI interrupts are on line 5 or 2  */
		clear_c0_status(IE_IRQ0 | IE_IRQ3);
	} else {
		clear_c0_status(0x100 << irq_nr);
	}
	local_irq_restore(flags);
}

#define mask_and_ack_ev64120_irq disable_ev64120_irq

static inline void enable_ev64120_irq(unsigned int irq_nr)
{
	unsigned long flags;

	local_irq_save(flags);
	if (irq_nr >= 8) {
		/* All PCI interrupts are on line 5 or 2  */
		set_c0_status(IE_IRQ0 | IE_IRQ3);
	} else {
		set_c0_status(IE_SW0 << irq_nr);
	}
	local_irq_restore(flags);
}

static unsigned int startup_ev64120_irq(unsigned int irq)
{
	if (irq >= 8) {
		// NOTE:  Add error-handling if > max
		pci_int_irq[max_interrupts++] = irq;
	}
	enable_ev64120_irq(irq);

	return 0;
}

static void shutdown_ev64120_irq(unsigned int irq)
{
	int count, tmp;

	/*
	 * Remove PCI interrupts from the pci_int_irq list.  Make sure
	 * that some handler was removed before decrementing max_interrupts.
	 */
	if (irq >= 8) {
		for (count = 0; count < max_interrupts; count++) {
			if (pci_int_irq[count] == irq) {
				for (tmp = count; tmp < max_interrupts; tmp++) {
					pci_int_irq[tmp] =
					    pci_int_irq[tmp + 1];
				}
			}
		}
		max_interrupts--;
	}
}

static void end_ev64120_irq(unsigned int irq)
{
	if (!(irq_desc[irq].status & (IRQ_DISABLED|IRQ_INPROGRESS)))
		enable_ev64120_irq(irq);
}

static struct hw_interrupt_type ev64120_irq_type = {
	"EV64120",
	startup_ev64120_irq,
	shutdown_ev64120_irq,
	enable_ev64120_irq,
	disable_ev64120_irq,
	mask_and_ack_ev64120_irq,
	end_ev64120_irq
};

/*
 * galileo_irq_setup - Initializes CPU interrupts
 */
void __init init_IRQ(void)
{
	extern asmlinkage void galileo_handle_int(void);
	int i;

	init_generic_irq();

	/* Yes, how many interrupts does this beast actually have?  -- Ralf */
	for (i = 0; i < NR_IRQS; i++) {
		irq_desc[i].status      = IRQ_DISABLED;
		irq_desc[i].action      = 0;
		irq_desc[i].depth       = 1;
		irq_desc[i].handler     = &ev64120_irq_type;
	}

	/*
	 * Clear all of the interrupts while we change the able around a bit.
	 * Enable timer.  Other interrupts will be enabled as they are
	 * registered.
	 */
	change_c0_status(ST0_IM | IE_IRQ2, IE_IRQ2);

	/* Sets the exception_handler array. */
	set_except_vector(0, galileo_handle_int);
}
