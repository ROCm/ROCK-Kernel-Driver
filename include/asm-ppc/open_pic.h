/*
 *  include/asm-ppc/open_pic.h -- OpenPIC Interrupt Handling
 *
 *  Copyright (C) 1997 Geert Uytterhoeven
 *
 *  This file is subject to the terms and conditions of the GNU General Public
 *  License.  See the file COPYING in the main directory of this archive
 *  for more details.
 *  
 */

#ifndef _PPC_KERNEL_OPEN_PIC_H
#define _PPC_KERNEL_OPEN_PIC_H

#include <linux/config.h>
#include <linux/irq.h>

#define OPENPIC_SIZE	0x40000

/*
 *  Non-offset'ed vector numbers
 */

#define OPENPIC_VEC_TIMER	64	/* and up */
#define OPENPIC_VEC_IPI		72	/* and up */
#define OPENPIC_VEC_SPURIOUS	127

/*
 * For the OpenPIC_InitSenses table, we include both the sense
 * and polarity in one number and mask out the value we want
 * later on. -- Tom
 */
#define IRQ_SENSE_MASK		0x1
#define IRQ_SENSE_LEVEL		0x1
#define IRQ_SENSE_EDGE		0x0

#define IRQ_POLARITY_MASK	0x2
#define IRQ_POLARITY_POSITIVE	0x2
#define IRQ_POLARITY_NEGATIVE	0x0

/* OpenPIC IRQ controller structure */
extern struct hw_interrupt_type open_pic;

/* OpenPIC IPI controller structure */
#ifdef CONFIG_SMP
extern struct hw_interrupt_type open_pic_ipi;
#endif /* CONFIG_SMP */

extern u_int OpenPIC_NumInitSenses;
extern u_char *OpenPIC_InitSenses;
extern void* OpenPIC_Addr;

/* Exported functions */
extern void openpic_set_sources(int first_irq, int num_irqs, void *isr);
extern void openpic_init(int linux_irq_offset);
extern void openpic_init_nmi_irq(u_int irq);
extern u_int openpic_irq(void);
extern void openpic_eoi(void);
extern void openpic_request_IPIs(void);
extern void do_openpic_setup_cpu(void);
extern int openpic_get_irq(struct pt_regs *regs);
extern void openpic_reset_processor_phys(u_int cpumask);
extern void openpic_setup_ISU(int isu_num, unsigned long addr);
extern void openpic_cause_IPI(u_int ipi, u_int cpumask);
extern void smp_openpic_message_pass(int target, int msg, unsigned long data,
				     int wait);

extern inline int openpic_to_irq(int irq)
{
	/* IRQ 0 usually means 'disabled'.. don't mess with it 
	 * exceptions to this (sandpoint maybe?) 
	 * shouldn't use openpic_to_irq 
	 */
	if (irq != 0){
		return irq += NUM_8259_INTERRUPTS;
	} else {
		return 0;
	}
}
/*extern int open_pic_irq_offset;*/
#endif /* _PPC_KERNEL_OPEN_PIC_H */
