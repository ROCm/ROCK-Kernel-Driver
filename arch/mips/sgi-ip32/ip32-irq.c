/*
 * Code to handle IP32 IRQs
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2000 Harald Koerfgen
 * Copyright (C) 2001 Keith M Wesolowski
 */
#include <linux/init.h>
#include <linux/kernel_stat.h>
#include <linux/types.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/bitops.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/mm.h>
#include <linux/random.h>
#include <linux/sched.h>

#include <asm/bitops.h>
#include <asm/mipsregs.h>
#include <asm/system.h>
#include <asm/ip32/ip32_ints.h>
#include <asm/ip32/crime.h>
#include <asm/ip32/mace.h>
#include <asm/signal.h>

/* issue a PIO read to make sure no PIO writes are pending */
#define flush_crime_bus() crime_read_64(CRIME_CONTROL);
#define flush_mace_bus() mace_read_64(MACEISA_FLASH_NIC_REG);

#undef DEBUG_IRQ
#ifdef DEBUG_IRQ
#define DBG(x...) printk(x)
#else
#define DBG(x...)
#endif

/* O2 irq map
 *
 * IP0 -> software (ignored)
 * IP1 -> software (ignored)
 * IP2 -> (irq0) C crime 1.1 all interrupts; crime 1.5 ???
 * IP3 -> (irq1) X unknown
 * IP4 -> (irq2) X unknown
 * IP5 -> (irq3) X unknown
 * IP6 -> (irq4) X unknown
 * IP7 -> (irq5) 0 CPU count/compare timer (system timer)
 *
 * crime: (C)
 *
 * CRIME_INT_STAT 31:0:
 *
 * 0 -> 1 Video in 1
 * 1 -> 2 Video in 2
 * 2 -> 3 Video out
 * 3 -> 4 Mace ethernet
 * 4 -> S  SuperIO sub-interrupt
 * 5 -> M  Miscellaneous sub-interrupt
 * 6 -> A  Audio sub-interrupt
 * 7 -> 8  PCI bridge errors
 * 8 -> 9  PCI SCSI aic7xxx 0
 * 9 -> 10  PCI SCSI aic7xxx 1
 * 10 -> 11 PCI slot 0
 * 11 -> 12 unused (PCI slot 1)
 * 12 -> 13 unused (PCI slot 2)
 * 13 -> 14 unused (PCI shared 0)
 * 14 -> 15 unused (PCI shared 1)
 * 15 -> 16 unused (PCI shared 2)
 * 16 -> 17 GBE0 (E)
 * 17 -> 18 GBE1 (E)
 * 18 -> 19 GBE2 (E)
 * 19 -> 20 GBE3 (E)
 * 20 -> 21 CPU errors
 * 21 -> 22 Memory errors
 * 22 -> 23 RE empty edge (E)
 * 23 -> 24 RE full edge (E)
 * 24 -> 25 RE idle edge (E)
 * 25 -> 26 RE empty level
 * 26 -> 27 RE full level
 * 27 -> 28 RE idle level
 * 28 -> 29  unused (software 0) (E)
 * 29 -> 30  unused (software 1) (E)
 * 30 -> 31  unused (software 2) - crime 1.5 CPU SysCorError (E)
 * 31 -> 32 VICE
 *
 * S, M, A: Use the MACE ISA interrupt register
 * MACE_ISA_INT_STAT 31:0
 *
 * 0-7 -> 33-40 Audio
 * 8 -> 41 RTC
 * 9 -> 42 Keyboard
 * 10 -> X Keyboard polled
 * 11 -> 44 Mouse
 * 12 -> X Mouse polled
 * 13-15 -> 46-48 Count/compare timers
 * 16-19 -> 49-52 Parallel (16 E)
 * 20-25 -> 53-58 Serial 1 (22 E)
 * 26-31 -> 59-64 Serial 2 (28 E)
 *
 * Note that this means IRQs 5-7, 43, and 45 do not exist.  This is a
 * different IRQ map than IRIX uses, but that's OK as Linux irq handling
 * is quite different anyway.
 */

/* Some initial interrupts to set up */
extern irqreturn_t crime_memerr_intr (int irq, void *dev_id,
			       struct pt_regs *regs);
extern irqreturn_t crime_cpuerr_intr (int irq, void *dev_id,
			       struct pt_regs *regs);

struct irqaction memerr_irq = { crime_memerr_intr, SA_INTERRUPT,
			       0, "CRIME memory error", NULL,
			       NULL };
struct irqaction cpuerr_irq = { crime_cpuerr_intr, SA_INTERRUPT,
			       0, "CRIME CPU error", NULL,
			       NULL };

extern void ip32_handle_int (void);

/*
 * For interrupts wired from a single device to the CPU.  Only the clock
 * uses this it seems, which is IRQ 0 and IP7.
 */

static void enable_cpu_irq(unsigned int irq)
{
	set_c0_status(STATUSF_IP7);
}

static unsigned int startup_cpu_irq(unsigned int irq)
{
	enable_cpu_irq(irq);
	return 0;
}

static void disable_cpu_irq(unsigned int irq)
{
	clear_c0_status(STATUSF_IP7);
}

static void end_cpu_irq(unsigned int irq)
{
	if (!(irq_desc[irq].status & (IRQ_DISABLED|IRQ_INPROGRESS)))
		enable_cpu_irq (irq);
}

#define shutdown_cpu_irq disable_cpu_irq
#define mask_and_ack_cpu_irq disable_cpu_irq

static struct hw_interrupt_type ip32_cpu_interrupt = {
	"IP32 CPU",
	startup_cpu_irq,
	shutdown_cpu_irq,
	enable_cpu_irq,
	disable_cpu_irq,
	mask_and_ack_cpu_irq,
	end_cpu_irq,
	NULL
};

/*
 * This is for pure CRIME interrupts - ie not MACE.  The advantage?
 * We get to split the register in half and do faster lookups.
 */

static u64 crime_mask=0;

static void enable_crime_irq(unsigned int irq)
{
	unsigned long flags;

	local_irq_save(flags);
	crime_mask |= 1 << (irq - 1);
	crime_write_64(CRIME_INT_MASK, crime_mask);
	local_irq_restore(flags);
}

static unsigned int startup_crime_irq(unsigned int irq)
{
        crime_mask = crime_read_64(CRIME_INT_MASK);
	enable_crime_irq(irq);
	return 0; /* This is probably not right; we could have pending irqs */
}

static void disable_crime_irq(unsigned int irq)
{
	unsigned long flags;

	local_irq_save(flags);
	crime_mask &= ~(1 << (irq - 1));
	crime_write_64(CRIME_INT_MASK, crime_mask);
	flush_crime_bus();
	local_irq_restore(flags);
}

static void mask_and_ack_crime_irq (unsigned int irq)
{
	unsigned long flags;

	/* Edge triggered interrupts must be cleared. */
	if ((irq >= CRIME_GBE0_IRQ && irq <= CRIME_GBE3_IRQ)
	    || (irq >= CRIME_RE_EMPTY_E_IRQ && irq <= CRIME_RE_IDLE_E_IRQ)
	    || (irq >= CRIME_SOFT0_IRQ && irq <= CRIME_SOFT2_IRQ)) {
	        u64 crime_int;
		local_irq_save(flags);
		crime_int = crime_read_64(CRIME_HARD_INT);
		crime_int &= ~(1 << (irq - 1));
		crime_write_64(CRIME_HARD_INT, crime_int);
		local_irq_restore(flags);
	}
	disable_crime_irq(irq);
}

static void end_crime_irq(unsigned int irq)
{
	if (!(irq_desc[irq].status & (IRQ_DISABLED|IRQ_INPROGRESS)))
		enable_crime_irq (irq);
}

#define shutdown_crime_irq disable_crime_irq

static struct hw_interrupt_type ip32_crime_interrupt = {
	"IP32 CRIME",
	startup_crime_irq,
	shutdown_crime_irq,
	enable_crime_irq,
	disable_crime_irq,
	mask_and_ack_crime_irq,
	end_crime_irq,
	NULL
};

/*
 * This is for MACE PCI interrupts.  We can decrease bus traffic by masking
 * as close to the source as possible.  This also means we can take the
 * next chunk of the CRIME register in one piece.
 */

static u32 macepci_mask;

static void enable_macepci_irq(unsigned int irq)
{
	unsigned long flags;

	local_irq_save(flags);
	macepci_mask |= MACEPCI_CONTROL_INT(irq - 9);
	mace_write_32(MACEPCI_CONTROL, macepci_mask);
        crime_mask |= 1 << (irq - 1);
        crime_write_64(CRIME_INT_MASK, crime_mask);
	local_irq_restore(flags);
}

static unsigned int startup_macepci_irq(unsigned int irq)
{
	crime_mask = crime_read_64 (CRIME_INT_MASK);
	macepci_mask = mace_read_32(MACEPCI_CONTROL);
  	enable_macepci_irq (irq);
	return 0;
}

static void disable_macepci_irq(unsigned int irq)
{
	unsigned long flags;

	local_irq_save(flags);
	crime_mask &= ~(1 << (irq - 1));
	crime_write_64(CRIME_INT_MASK, crime_mask);
	flush_crime_bus();
	macepci_mask &= ~MACEPCI_CONTROL_INT(irq - 9);
	mace_write_32(MACEPCI_CONTROL, macepci_mask);
	flush_mace_bus();
	local_irq_restore(flags);
}

static void end_macepci_irq(unsigned int irq)
{
	if (!(irq_desc[irq].status & (IRQ_DISABLED|IRQ_INPROGRESS)))
		enable_macepci_irq (irq);
}

#define shutdown_macepci_irq disable_macepci_irq
#define mask_and_ack_macepci_irq disable_macepci_irq

static struct hw_interrupt_type ip32_macepci_interrupt = {
	"IP32 MACE PCI",
	startup_macepci_irq,
	shutdown_macepci_irq,
	enable_macepci_irq,
	disable_macepci_irq,
	mask_and_ack_macepci_irq,
	end_macepci_irq,
	NULL
};

/* This is used for MACE ISA interrupts.  That means bits 4-6 in the
 * CRIME register.
 */

u32 maceisa_mask = 0;

static void enable_maceisa_irq (unsigned int irq)
{
	unsigned int crime_int = 0;
	unsigned long flags;

	DBG ("maceisa enable: %u\n", irq);

	switch (irq) {
	case MACEISA_AUDIO_SW_IRQ ... MACEISA_AUDIO3_MERR_IRQ:
		crime_int = MACE_AUDIO_INT;
		break;
	case MACEISA_RTC_IRQ ... MACEISA_TIMER2_IRQ:
		crime_int = MACE_MISC_INT;
		break;
	case MACEISA_PARALLEL_IRQ ... MACEISA_SERIAL2_RDMAOR_IRQ:
		crime_int = MACE_SUPERIO_INT;
		break;
	}
	DBG ("crime_int %016lx enabled\n", crime_int);
	local_irq_save(flags);
	crime_mask |= crime_int;
	crime_write_64(CRIME_INT_MASK, crime_mask);
	maceisa_mask |= 1 << (irq - 33);
	mace_write_32(MACEISA_INT_MASK, maceisa_mask);
	local_irq_restore(flags);
}

static unsigned int startup_maceisa_irq (unsigned int irq)
{
	crime_mask = crime_read_64 (CRIME_INT_MASK);
	maceisa_mask = mace_read_32(MACEISA_INT_MASK);
	enable_maceisa_irq(irq);
	return 0;
}

static void disable_maceisa_irq(unsigned int irq)
{
	unsigned int crime_int = 0;
	unsigned long flags;

	local_irq_save(flags);
	maceisa_mask &= ~(1 << (irq - 33));
        if(!(maceisa_mask & MACEISA_AUDIO_INT))
		crime_int |= MACE_AUDIO_INT;
        if(!(maceisa_mask & MACEISA_MISC_INT))
		crime_int |= MACE_MISC_INT;
        if(!(maceisa_mask & MACEISA_SUPERIO_INT))
		crime_int |= MACE_SUPERIO_INT;
	crime_mask &= ~crime_int;
	crime_write_64(CRIME_INT_MASK, crime_mask);
	flush_crime_bus();
	mace_write_32(MACEISA_INT_MASK, maceisa_mask);
	flush_mace_bus();
	local_irq_restore(flags);
}

static void mask_and_ack_maceisa_irq(unsigned int irq)
{
	u32 mace_int;
	unsigned long flags;

	switch (irq) {
	case MACEISA_PARALLEL_IRQ:
	case MACEISA_SERIAL1_TDMAPR_IRQ:
	case MACEISA_SERIAL2_TDMAPR_IRQ:
		/* edge triggered */
		local_irq_save(flags);
		mace_int = mace_read_32(MACEISA_INT_STAT);
		mace_int &= ~(1 << (irq - 33));
		mace_write_32(MACEISA_INT_STAT, mace_int);
		local_irq_restore(flags);
		break;
	}
	disable_maceisa_irq(irq);
}

static void end_maceisa_irq(unsigned irq)
{
	if (!(irq_desc[irq].status & (IRQ_DISABLED|IRQ_INPROGRESS)))
		enable_maceisa_irq (irq);
}

#define shutdown_maceisa_irq disable_maceisa_irq

static struct hw_interrupt_type ip32_maceisa_interrupt = {
	"IP32 MACE ISA",
	startup_maceisa_irq,
	shutdown_maceisa_irq,
	enable_maceisa_irq,
	disable_maceisa_irq,
	mask_and_ack_maceisa_irq,
	end_maceisa_irq,
	NULL
};

/* This is used for regular non-ISA, non-PCI MACE interrupts.  That means
 * bits 0-3 and 7 in the CRIME register.
 */

static void enable_mace_irq(unsigned int irq)
{
	unsigned long flags;

	local_irq_save(flags);
	crime_mask |= 1 << (irq - 1);
	crime_write_64 (CRIME_INT_MASK, crime_mask);
	local_irq_restore (flags);
}

static unsigned int startup_mace_irq(unsigned int irq)
{
        crime_mask = crime_read_64 (CRIME_INT_MASK);
	enable_mace_irq(irq);
	return 0;
}

static void disable_mace_irq(unsigned int irq)
{
	unsigned long flags;

	local_irq_save(flags);
	crime_mask &= ~(1 << (irq - 1));
	crime_write_64 (CRIME_INT_MASK, crime_mask);
	flush_crime_bus();
	local_irq_restore(flags);
}

static void end_mace_irq(unsigned int irq)
{
	if (!(irq_desc[irq].status & (IRQ_DISABLED|IRQ_INPROGRESS)))
		enable_mace_irq (irq);
}

#define shutdown_mace_irq disable_mace_irq
#define mask_and_ack_mace_irq disable_mace_irq

static struct hw_interrupt_type ip32_mace_interrupt = {
	"IP32 MACE",
	startup_mace_irq,
	shutdown_mace_irq,
	enable_mace_irq,
	disable_mace_irq,
	mask_and_ack_mace_irq,
	end_mace_irq,
	NULL
};

static void ip32_unknown_interrupt (struct pt_regs *regs)
{
	u64 crime;
	u32 mace;

	printk ("Unknown interrupt occurred!\n");
	printk ("cp0_status: %08x\n",
		read_c0_status ());
	printk ("cp0_cause: %08x\n",
		read_c0_cause ());
	crime = crime_read_64 (CRIME_INT_MASK);
	printk ("CRIME interrupt mask: %016lx\n", crime);
	crime = crime_read_64 (CRIME_INT_STAT);
	printk ("CRIME interrupt status: %016lx\n", crime);
	crime = crime_read_64 (CRIME_HARD_INT);
	printk ("CRIME hardware interrupt register: %016lx\n", crime);
	mace = mace_read_32 (MACEISA_INT_MASK);
	printk ("MACE ISA interrupt mask: %08x\n", mace);
	mace = mace_read_32 (MACEISA_INT_STAT);
	printk ("MACE ISA interrupt status: %08x\n", mace);
	mace = mace_read_32 (MACEPCI_CONTROL);
	printk ("MACE PCI control register: %08x\n", mace);

	printk("Register dump:\n");
	show_regs(regs);

	printk("Please mail this report to linux-mips@linux-mips.org\n");
	printk("Spinning...");
	while(1) ;
}

/* CRIME 1.1 appears to deliver all interrupts to this one pin. */
void ip32_irq0(struct pt_regs *regs)
{
	u64 crime_int;
	int irq = 0;

	crime_int = crime_read_64(CRIME_INT_STAT) & crime_mask;
        irq = ffs(crime_int);
        crime_int = 1ULL << (irq - 1);

	if (crime_int & CRIME_MACEISA_INT_MASK) {
		u32 mace_int = mace_read_32 (MACEISA_INT_STAT) & maceisa_mask;
                irq = ffs (mace_int) + 32;
	}
	DBG("*irq %u*\n", irq);
	do_IRQ(irq, regs);
}

void ip32_irq1(struct pt_regs *regs)
{
	ip32_unknown_interrupt (regs);
}

void ip32_irq2(struct pt_regs *regs)
{
	ip32_unknown_interrupt (regs);
}

void ip32_irq3(struct pt_regs *regs)
{
	ip32_unknown_interrupt (regs);
}

void ip32_irq4(struct pt_regs *regs)
{
	ip32_unknown_interrupt (regs);
}

void ip32_irq5(struct pt_regs *regs)
{
	do_IRQ (CLOCK_IRQ, regs);
}

void __init init_IRQ(void)
{
	unsigned int irq;
	int i;

	/* Install our interrupt handler, then clear and disable all
	 * CRIME and MACE interrupts.
	 */
	crime_write_64(CRIME_INT_MASK, 0);
	crime_write_64(CRIME_HARD_INT, 0);
	crime_write_64(CRIME_SOFT_INT, 0);
	mace_write_32(MACEISA_INT_STAT, 0);
	mace_write_32(MACEISA_INT_MASK, 0);
	set_except_vector(0, ip32_handle_int);

	for (i = 0; i < NR_IRQS; i++) {
		irq_desc[i].status  = IRQ_DISABLED;
		irq_desc[i].action  = NULL;
		irq_desc[i].depth   = 1;
		irq_desc[i].handler = &no_irq_type;
	}

	for (irq = 0; irq <= IP32_IRQ_MAX; irq++) {
		hw_irq_controller *controller;

		if (irq == CLOCK_IRQ)
			controller = &ip32_cpu_interrupt;
		else if (irq <= MACE_PCI_BRIDGE_IRQ && irq >= MACE_VID_IN1_IRQ)
			controller = &ip32_mace_interrupt;
		else if (irq <= MACEPCI_SHARED2_IRQ && irq >= MACEPCI_SCSI0_IRQ)
			controller = &ip32_macepci_interrupt;
		else if (irq <= CRIME_VICE_IRQ && irq >= CRIME_GBE0_IRQ)
			controller = &ip32_crime_interrupt;
		else
			controller = &ip32_maceisa_interrupt;

		irq_desc[irq].status = IRQ_DISABLED;
		irq_desc[irq].action = 0;
		irq_desc[irq].depth = 0;
		irq_desc[irq].handler = controller;
	}
	setup_irq(CRIME_MEMERR_IRQ, &memerr_irq);
	setup_irq(CRIME_CPUERR_IRQ, &cpuerr_irq);
}
