/*
 *
 * Copyright 2001 MontaVista Software Inc.
 * Author: jsun@mvista.com or jsun@junsun.net
 *
 * arch/mips/gt64120/common/gt_irq.c
 *     Interrupt routines for gt64120.  Currently it only handles timer irq.
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */
#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <asm/ptrace.h>
#include <linux/config.h>
#include <linux/sched.h>
#include <linux/kernel_stat.h>
#include <asm/io.h>
#include <asm/gt64120/gt64120.h>

/*
 * These are interrupt handlers for the GT on-chip interrupts.  They
 * all come in to the MIPS on a single interrupt line, and have to
 * be handled and ack'ed differently than other MIPS interrupts.
 */

#if CURRENTLY_UNUSED

struct tq_struct irq_handlers[MAX_CAUSE_REGS][MAX_CAUSE_REG_WIDTH];
void hook_irq_handler(int int_cause, int bit_num, void *isr_ptr);

/*
 * Hooks IRQ handler to the system. When the system is interrupted
 * the interrupt service routine is called.
 *
 * Inputs :
 * int_cause - The interrupt cause number. In EVB64120 two parameters
 *             are declared, INT_CAUSE_MAIN and INT_CAUSE_HIGH.
 * bit_num   - Indicates which bit number in the cause register
 * isr_ptr   - Pointer to the interrupt service routine
 */
void hook_irq_handler(int int_cause, int bit_num, void *isr_ptr)
{
	irq_handlers[int_cause][bit_num].routine = isr_ptr;
}


/*
 * Enables the IRQ on Galileo Chip
 *
 * Inputs :
 * int_cause - The interrupt cause number. In EVB64120 two parameters
 *             are declared, INT_CAUSE_MAIN and INT_CAUSE_HIGH.
 * bit_num   - Indicates which bit number in the cause register
 *
 * Outputs :
 * 1 if succesful, 0 if failure
 */
int enable_galileo_irq(int int_cause, int bit_num)
{
	if (int_cause == INT_CAUSE_MAIN)
		SET_REG_BITS(CPU_INTERRUPT_MASK_REGISTER, (1 << bit_num));
	else if (int_cause == INT_CAUSE_HIGH)
		SET_REG_BITS(CPU_HIGH_INTERRUPT_MASK_REGISTER,
			     (1 << bit_num));
	else
		return 0;

	return 1;
}

/*
 * Disables the IRQ on Galileo Chip
 *
 * Inputs :
 * int_cause - The interrupt cause number. In EVB64120 two parameters
 *             are declared, INT_CAUSE_MAIN and INT_CAUSE_HIGH.
 * bit_num   - Indicates which bit number in the cause register
 *
 * Outputs :
 * 1 if succesful, 0 if failure
 */
int disable_galileo_irq(int int_cause, int bit_num)
{
	if (int_cause == INT_CAUSE_MAIN)
		RESET_REG_BITS(CPU_INTERRUPT_MASK_REGISTER,
			       (1 << bit_num));
	else if (int_cause == INT_CAUSE_HIGH)
		RESET_REG_BITS(CPU_HIGH_INTERRUPT_MASK_REGISTER,
			       (1 << bit_num));
	else
		return 0;
	return 1;
}
#endif				/*  UNUSED  */

/*
 * Interrupt handler for interrupts coming from the Galileo chip.
 * It could be timer interrupt, built in ethernet ports etc...
 */
static void gt64120_irq(int irq, void *dev_id, struct pt_regs *regs)
{
	unsigned int irq_src, int_high_src, irq_src_mask,
	    int_high_src_mask;
	int handled;

	GT_READ(GT_INTRCAUSE_OFS, &irq_src);
	GT_READ(GT_INTRMASK_OFS, &irq_src_mask);
	GT_READ(GT_HINTRCAUSE_OFS, &int_high_src);
	GT_READ(GT_HINTRMASK_OFS, &int_high_src_mask);
	irq_src = irq_src & irq_src_mask;
	int_high_src = int_high_src & int_high_src_mask;

	handled = 0;

	/* Execute all interrupt handlers */
	/* Check for timer interrupt */
	if (irq_src & 0x00000800) {
		handled = 1;
		irq_src &= ~0x00000800;
		//    RESET_REG_BITS (INTERRUPT_CAUSE_REGISTER,BIT8);
		do_timer(regs);
	}

	if (irq_src) {
		printk(KERN_INFO
		       "Other Galileo interrupt received irq_src %x\n",
		       irq_src);
#if CURRENTLY_UNUSED
		for (count = 0; count < MAX_CAUSE_REG_WIDTH; count++) {
			if (irq_src & (1 << count)) {
				if (irq_handlers[INT_CAUSE_MAIN][count].
				    routine) {
					queue_task(&irq_handlers
						   [INT_CAUSE_MAIN][count],
						   &tq_immediate);
					mark_bh(IMMEDIATE_BH);
					handled = 1;
				}
			}
		}
#endif				/*  UNUSED  */
	}
	GT_WRITE(GT_INTRCAUSE_OFS, 0);
	GT_WRITE(GT_HINTRCAUSE_OFS, 0);

#undef GALILEO_I2O
#ifdef GALILEO_I2O
	/*
	 * Future I2O support.  We currently attach I2O interrupt handlers to
	 * the Galileo interrupt (int 4) and handle them in do_IRQ.
	 */
	if (isInBoundDoorBellInterruptSet()) {
		printk(KERN_INFO "I2O doorbell interrupt received.\n");
		handled = 1;
	}

	if (isInBoundPostQueueInterruptSet()) {
		printk(KERN_INFO "I2O Queue interrupt received.\n");
		handled = 1;
	}

	/*
	 * This normally would be outside of the ifdef, but since we're
	 * handling I2O outside of this handler, this printk shows up every
	 * time we get a valid I2O interrupt.  So turn this off for now.
	 */
	if (handled == 0) {
		if (counter < 50) {
			printk("Spurious Galileo interrupt...\n");
			counter++;
		}
	}
#endif
}

/*
 * Initializes timer using galileo's built in timer.
 */
#ifdef CONFIG_SYSCLK_100
#define Sys_clock (100 * 1000000)	// 100 MHz
#endif
#ifdef CONFIG_SYSCLK_83
#define Sys_clock (83.333 * 1000000)	// 83.333 MHz
#endif
#ifdef CONFIG_SYSCLK_75
#define Sys_clock (75 * 1000000)	// 75 MHz
#endif

/*
 * This will ignore the standard MIPS timer interrupt handler
 * that is passed in as *irq (=irq0 in ../kernel/time.c).
 * We will do our own timer interrupt handling.
 */
void gt64120_time_init(void)
{
	extern irq_desc_t irq_desc[NR_IRQS];
	static struct irqaction timer;

	/* Disable timer first */
	GT_WRITE(GT_TC_CONTROL_OFS, 0);
	/* Load timer value for 100 Hz */
	GT_WRITE(GT_TC3_OFS, Sys_clock / 100);

	/*
	 * Create the IRQ structure entry for the timer.  Since we're too early
	 * in the boot process to use the "request_irq()" call, we'll hard-code
	 * the values to the correct interrupt line.
	 */
	timer.handler = &gt64120_irq;
	timer.flags = SA_SHIRQ | SA_INTERRUPT;
	timer.name = "timer";
	timer.dev_id = NULL;
	timer.next = NULL;
	timer.mask = 0;
	irq_desc[TIMER].action = &timer;

	enable_irq(TIMER);

	/* Enable timer ints */
	GT_WRITE(GT_TC_CONTROL_OFS, 0xc0);
	/* clear Cause register first */
	GT_WRITE(GT_INTRCAUSE_OFS, 0x0);
	/* Unmask timer int */
	GT_WRITE(GT_INTRMASK_OFS, 0x800);
	/* Clear High int register */
	GT_WRITE(GT_HINTRCAUSE_OFS, 0x0);
	/* Mask All interrupts at High cause interrupt */
	GT_WRITE(GT_HINTRMASK_OFS, 0x0);
}

void gt64120_irq_init(void)
{
#if CURRENTLY_UNUSED
	int i, j;

	/* Reset irq handlers pointers to NULL */
	for (i = 0; i < MAX_CAUSE_REGS; i++) {
		for (j = 0; j < MAX_CAUSE_REG_WIDTH; j++) {
			irq_handlers[i][j].next = NULL;
			irq_handlers[i][j].sync = 0;
			irq_handlers[i][j].routine = NULL;
			irq_handlers[i][j].data = NULL;
		}
	}
#endif
}
