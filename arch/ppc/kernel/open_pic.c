/*
 *  arch/ppc/kernel/openpic.c -- OpenPIC Interrupt Handling
 *
 *  Copyright (C) 1997 Geert Uytterhoeven
 *
 *  This file is subject to the terms and conditions of the GNU General Public
 *  License.  See the file COPYING in the main directory of this archive
 *  for more details.
 */

#include <linux/config.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/init.h>
#include <linux/openpic.h>
#include <asm/ptrace.h>
#include <asm/signal.h>
#include <asm/io.h>
#include <asm/irq.h>
#include <asm/prom.h>
#include "local_irq.h"

volatile struct OpenPIC *OpenPIC = NULL;
u_int OpenPIC_NumInitSenses __initdata = 0;
u_char *OpenPIC_InitSenses __initdata = NULL;
int open_pic_irq_offset;
extern int use_of_interrupt_tree;

void chrp_mask_irq(unsigned int);
void chrp_unmask_irq(unsigned int);
void find_ISUs(void);

static u_int NumProcessors;
static u_int NumSources;
OpenPIC_Source *ISU;
/*
 * We should use this if we have > 1 ISU.
 * We can just point each entry to the
 * appropriate source regs but it wastes a lot of space
 * so until we have >1 ISU I'll leave it unimplemented.
 * -- Cort
OpenPIC_Source ISU[128];
*/

struct hw_interrupt_type open_pic = {
	" OpenPIC  ",
	NULL,
	NULL,
	openpic_enable_irq,
	openpic_disable_irq,
	0,
	0
};

/*
 *  Accesses to the current processor's registers
 */
#ifndef __powerpc__
#define THIS_CPU		Private
#define CHECK_THIS_CPU		do {} while (0)
#else
#define THIS_CPU		Processor[cpu]
#define CHECK_THIS_CPU		check_arg_cpu(cpu)
#endif

#if 1
#define check_arg_ipi(ipi) \
    if (ipi < 0 || ipi >= OPENPIC_NUM_IPI) \
	printk("openpic.c:%d: illegal ipi %d\n", __LINE__, ipi);
#define check_arg_timer(timer) \
    if (timer < 0 || timer >= OPENPIC_NUM_TIMERS) \
	printk("openpic.c:%d: illegal timer %d\n", __LINE__, timer);
#define check_arg_vec(vec) \
    if (vec < 0 || vec >= OPENPIC_NUM_VECTORS) \
	printk("openpic.c:%d: illegal vector %d\n", __LINE__, vec);
#define check_arg_pri(pri) \
    if (pri < 0 || pri >= OPENPIC_NUM_PRI) \
	printk("openpic.c:%d: illegal priority %d\n", __LINE__, pri);
/*
 * I changed this to return to keep us from from trying to use irq #'s
 * that we're using for IPI's.
 *   -- Cort
 */ 
#define check_arg_irq(irq) \
    /*if (irq < 0 || irq >= (NumSources+open_pic_irq_offset)) \
      printk("openpic.c:%d: illegal irq %d\n", __LINE__, irq);*/
#define check_arg_cpu(cpu) \
    if (cpu < 0 || cpu >= NumProcessors) \
	printk("openpic.c:%d: illegal cpu %d\n", __LINE__, cpu);
#else
#define check_arg_ipi(ipi)	do {} while (0)
#define check_arg_timer(timer)	do {} while (0)
#define check_arg_vec(vec)	do {} while (0)
#define check_arg_pri(pri)	do {} while (0)
#define check_arg_irq(irq)	do {} while (0)
#define check_arg_cpu(cpu)	do {} while (0)
#endif

#ifdef CONFIG_SMP
void openpic_ipi_action(int cpl, void *dev_id, struct pt_regs *regs)
{
	smp_message_recv(cpl-OPENPIC_VEC_IPI, regs);
}
#endif /* CONFIG_SMP */

#ifdef __i386__
static inline u_int in_le32(volatile u_int *addr)
{
	return *addr;
}

static inline void out_le32(volatile u_int *addr, u_int val)
{
	*addr = val;
}
#endif

u_int openpic_read(volatile u_int *addr)
{
	u_int val;

	val = in_le32(addr);
	return val;
}

static inline void openpic_write(volatile u_int *addr, u_int val)
{
	out_le32(addr, val);
}

static inline u_int openpic_readfield(volatile u_int *addr, u_int mask)
{
	u_int val = openpic_read(addr);
	return val & mask;
}

inline void openpic_writefield(volatile u_int *addr, u_int mask,
			       u_int field)
{
	u_int val = openpic_read(addr);
	openpic_write(addr, (val & ~mask) | (field & mask));
}

static inline void openpic_clearfield(volatile u_int *addr, u_int mask)
{
	openpic_writefield(addr, mask, 0);
}

static inline void openpic_setfield(volatile u_int *addr, u_int mask)
{
	openpic_writefield(addr, mask, mask);
}

static void openpic_safe_writefield(volatile u_int *addr, u_int mask,
				    u_int field)
{
	openpic_setfield(addr, OPENPIC_MASK);
	/* wait until it's not in use */
	/* BenH: Is this code really enough ? I would rather check the result
	 *       and eventually retry ...
	 */
	while (openpic_read(addr) & OPENPIC_ACTIVITY);
	openpic_writefield(addr, mask | OPENPIC_MASK, field | OPENPIC_MASK);
}

void __init openpic_init(int main_pic)
{
	u_int t, i;
	u_int timerfreq;
	const char *version;

	if (!OpenPIC)
		panic("No OpenPIC found");

	if ( ppc_md.progress ) ppc_md.progress("openpic enter",0x122);

	t = openpic_read(&OpenPIC->Global.Feature_Reporting0);
	switch (t & OPENPIC_FEATURE_VERSION_MASK) {
	case 1:
		version = "1.0";
		break;
	case 2:
		version = "1.2";
		break;
	case 3:
		version = "1.3";
		break;
	default:
		version = "?";
		break;
	}
	NumProcessors = ((t & OPENPIC_FEATURE_LAST_PROCESSOR_MASK) >>
			 OPENPIC_FEATURE_LAST_PROCESSOR_SHIFT) + 1;
	NumSources = ((t & OPENPIC_FEATURE_LAST_SOURCE_MASK) >>
		      OPENPIC_FEATURE_LAST_SOURCE_SHIFT) + 1;
	if ( _machine != _MACH_Pmac )
	{
		printk("OpenPIC Version %s (%d CPUs and %d IRQ sources) at %p\n", version,
		       NumProcessors, NumSources, OpenPIC);
		timerfreq = openpic_read(&OpenPIC->Global.Timer_Frequency);
		printk("OpenPIC timer frequency is ");
		if (timerfreq)
			printk("%d MHz\n", timerfreq>>20);
		else
			printk("not set\n");
	}
	
	if ( main_pic )
	{
		/* Initialize timer interrupts */
		if ( ppc_md.progress ) ppc_md.progress("openpic timer",0x3ba);
		for (i = 0; i < OPENPIC_NUM_TIMERS; i++) {
			/* Disabled, Priority 0 */
			openpic_inittimer(i, 0, OPENPIC_VEC_TIMER+i);
			/* No processor */
			openpic_maptimer(i, 0);
		}
	    
		/* Initialize IPI interrupts */
		if ( ppc_md.progress ) ppc_md.progress("openpic ipi",0x3bb);
		for (i = 0; i < OPENPIC_NUM_IPI; i++) {
			/* Disabled, Priority 8 */
			openpic_initipi(i, 8, OPENPIC_VEC_IPI+i);
		}
		find_ISUs();
		if ( _machine != _MACH_Pmac )
		{
			/* Initialize external interrupts */
			if ( ppc_md.progress ) ppc_md.progress("openpic ext",0x3bc);
			/* SIOint (8259 cascade) is special */
			openpic_initirq(0, 8, open_pic_irq_offset, 1, 1);
			openpic_mapirq(0, 1<<0);
			for (i = 1; i < NumSources; i++) {
				/* Enabled, Priority 8 */
				openpic_initirq(i, 8, open_pic_irq_offset+i, 0,
						i < OpenPIC_NumInitSenses ? OpenPIC_InitSenses[i] : 1);
				/* Processor 0 */
				openpic_mapirq(i, 1<<0);
			}
		}
		else
		{
			/* Prevent any interrupt from occuring during initialisation.
			 * Hum... I believe this is not necessary, Apple does that in
			 * Darwin's PowerExpress code.
			 */
			openpic_set_priority(0, 0xf);
			
			/* First disable all interrupts and map them to CPU 0 */
			for (i = 0; i < NumSources; i++) {
				openpic_disable_irq(i);
				openpic_mapirq(i, 1<<0);
			}
			
			/* If we use the device tree, then lookup all interrupts and
			 * initialize them according to sense infos found in the tree
			 */
			if (use_of_interrupt_tree) {
				struct device_node* np = find_all_nodes();
			    	while(np) {
					int j, pri;
					pri = strcmp(np->name, "programmer-switch") ? 2 : 7;
					for (j=0;j<np->n_intrs;j++) {
						openpic_initirq(np->intrs[j].line,
								pri,
								np->intrs[j].line,
								0,
								np->intrs[j].sense);
						if (np->intrs[j].sense)
							irq_desc[np->intrs[j].line].status =  IRQ_LEVEL;
					}
					np = np->next;
				}
			}
		}
		
		/* Initialize the spurious interrupt */
		if ( ppc_md.progress ) ppc_md.progress("openpic spurious",0x3bd);
		openpic_set_spurious(OPENPIC_VEC_SPURIOUS);
		if ( !(_machine & (_MACH_gemini|_MACH_Pmac)) )
		{
			if (request_irq(IRQ_8259_CASCADE, no_action, SA_INTERRUPT,
					"82c59 cascade", NULL))
				printk("Unable to get OpenPIC IRQ 0 for cascade\n");
		}
		openpic_set_priority(0, 0);
		openpic_disable_8259_pass_through();
	}
	if ( ppc_md.progress ) ppc_md.progress("openpic exit",0x222);
}

void find_ISUs(void)
{
#ifdef CONFIG_PPC64BRIDGE
	/* hardcode this for now since the IBM 260 is the only thing with
	 * a distributed openpic right now.  -- Cort
	 */
	ISU = (OpenPIC_Source *)0xfeff7c00;
	NumSources = 0x10;
#else
	/* for non-distributed OpenPIC implementations it's in the IDU -- Cort */
	ISU = (OpenPIC_Source *)OpenPIC->Source;
#endif
}

void openpic_reset(void)
{
	openpic_setfield(&OpenPIC->Global.Global_Configuration0,
			 OPENPIC_CONFIG_RESET);
}

void openpic_enable_8259_pass_through(void)
{
	openpic_clearfield(&OpenPIC->Global.Global_Configuration0,
			   OPENPIC_CONFIG_8259_PASSTHROUGH_DISABLE);
}

void openpic_disable_8259_pass_through(void)
{
	openpic_setfield(&OpenPIC->Global.Global_Configuration0,
			 OPENPIC_CONFIG_8259_PASSTHROUGH_DISABLE);
}

#ifndef __i386__
/*
 *  Find out the current interrupt
 */
u_int openpic_irq(u_int cpu)
{
	u_int vec;

	check_arg_cpu(cpu);
	vec = openpic_readfield(&OpenPIC->THIS_CPU.Interrupt_Acknowledge,
				OPENPIC_VECTOR_MASK);
	return vec;
}
#endif

#ifndef __powerpc__
void openpic_eoi(void)
#else
void openpic_eoi(u_int cpu)
#endif
{
	check_arg_cpu(cpu);
	openpic_write(&OpenPIC->THIS_CPU.EOI, 0);
	/* Handle PCI write posting */
	(void)openpic_read(&OpenPIC->THIS_CPU.EOI);
}


#ifndef __powerpc__
u_int openpic_get_priority(void)
#else
u_int openpic_get_priority(u_int cpu)
#endif
{
	CHECK_THIS_CPU;
	return openpic_readfield(&OpenPIC->THIS_CPU.Current_Task_Priority,
				 OPENPIC_CURRENT_TASK_PRIORITY_MASK);
}

#ifndef __powerpc__
void openpic_set_priority(u_int pri)
#else
void openpic_set_priority(u_int cpu, u_int pri)
#endif
{
	CHECK_THIS_CPU;
	check_arg_pri(pri);
	openpic_writefield(&OpenPIC->THIS_CPU.Current_Task_Priority,
			   OPENPIC_CURRENT_TASK_PRIORITY_MASK, pri);
}

/*
 *  Get/set the spurious vector
 */
u_int openpic_get_spurious(void)
{
	return openpic_readfield(&OpenPIC->Global.Spurious_Vector,
				 OPENPIC_VECTOR_MASK);
}

void openpic_set_spurious(u_int vec)
{
	check_arg_vec(vec);
	openpic_writefield(&OpenPIC->Global.Spurious_Vector, OPENPIC_VECTOR_MASK,
			   vec);
}

void openpic_init_processor(u_int cpumask)
{
	openpic_write(&OpenPIC->Global.Processor_Initialization, cpumask);
}

/*
 *  Initialize an interprocessor interrupt (and disable it)
 *
 *  ipi: OpenPIC interprocessor interrupt number
 *  pri: interrupt source priority
 *  vec: the vector it will produce
 */
void openpic_initipi(u_int ipi, u_int pri, u_int vec)
{
	check_arg_timer(ipi);
	check_arg_pri(pri);
	check_arg_vec(vec);
	openpic_safe_writefield(&OpenPIC->Global.IPI_Vector_Priority(ipi),
				OPENPIC_PRIORITY_MASK | OPENPIC_VECTOR_MASK,
				(pri << OPENPIC_PRIORITY_SHIFT) | vec);
}

/*
 *  Send an IPI to one or more CPUs
 */
#ifndef __powerpc__
void openpic_cause_IPI(u_int ipi, u_int cpumask)
#else
void openpic_cause_IPI(u_int cpu, u_int ipi, u_int cpumask)
#endif
{
	CHECK_THIS_CPU;
	check_arg_ipi(ipi);
	openpic_write(&OpenPIC->THIS_CPU.IPI_Dispatch(ipi), cpumask);
}

void openpic_enable_IPI(u_int ipi)
{
	check_arg_ipi(ipi);
	openpic_clearfield(&OpenPIC->Global.IPI_Vector_Priority(ipi),
			   OPENPIC_MASK);
}

/*
 * Do per-cpu setup for SMP systems.
 *
 * Get IPI's working and start taking interrupts.
 *   -- Cort
  */
void do_openpic_setup_cpu(void)
{
 	int i;
 	
 	for ( i = 0; i < OPENPIC_NUM_IPI ; i++ )
		openpic_enable_IPI(i);
#if 0	
 	/* let the openpic know we want intrs */
 	for ( i = 0; i < NumSources ; i++ )
 		openpic_mapirq(i, openpic_read(ISU[i].Destination)
 			       | (1<<smp_processor_id()) );
#endif	
 	openpic_set_priority(smp_processor_id(), 0);
}

/*
 *  Initialize a timer interrupt (and disable it)
 *
 *  timer: OpenPIC timer number
 *  pri: interrupt source priority
 *  vec: the vector it will produce
 */
void openpic_inittimer(u_int timer, u_int pri, u_int vec)
{
	check_arg_timer(timer);
	check_arg_pri(pri);
	check_arg_vec(vec);
	openpic_safe_writefield(&OpenPIC->Global.Timer[timer].Vector_Priority,
				OPENPIC_PRIORITY_MASK | OPENPIC_VECTOR_MASK,
				(pri << OPENPIC_PRIORITY_SHIFT) | vec);
}

/*
 *  Map a timer interrupt to one or more CPUs
 */
void openpic_maptimer(u_int timer, u_int cpumask)
{
	check_arg_timer(timer);
	openpic_write(&OpenPIC->Global.Timer[timer].Destination, cpumask);
}

/*
 *  Enable/disable an interrupt source
 */
void openpic_enable_irq(u_int irq)
{
	check_arg_irq(irq);
	openpic_clearfield(&ISU[irq - open_pic_irq_offset].Vector_Priority, OPENPIC_MASK);
	/* make sure mask gets to controller before we return to user */
	do {
		mb(); /* sync is probably useless here */
	} while(openpic_readfield(&ISU[irq - open_pic_irq_offset].Vector_Priority,
			OPENPIC_MASK));
}

void openpic_disable_irq(u_int irq)
{
	check_arg_irq(irq);
	openpic_setfield(&ISU[irq - open_pic_irq_offset].Vector_Priority, OPENPIC_MASK);
	/* make sure mask gets to controller before we return to user */
	do {
		mb();  /* sync is probably useless here */
	} while(!openpic_readfield(&ISU[irq - open_pic_irq_offset].Vector_Priority,
    			OPENPIC_MASK));
}

/*
 *  Initialize an interrupt source (and disable it!)
 *
 *  irq: OpenPIC interrupt number
 *  pri: interrupt source priority
 *  vec: the vector it will produce
 *  pol: polarity (1 for positive, 0 for negative)
 *  sense: 1 for level, 0 for edge
 */
void openpic_initirq(u_int irq, u_int pri, u_int vec, int pol, int sense)
{
	check_arg_irq(irq);
	check_arg_pri(pri);
	check_arg_vec(vec);
	openpic_safe_writefield(&ISU[irq].Vector_Priority,
				OPENPIC_PRIORITY_MASK | OPENPIC_VECTOR_MASK |
				OPENPIC_SENSE_MASK | OPENPIC_POLARITY_MASK,
				(pri << OPENPIC_PRIORITY_SHIFT) | vec |
				(pol ? OPENPIC_POLARITY_POSITIVE :
			    		OPENPIC_POLARITY_NEGATIVE) |
				(sense ? OPENPIC_SENSE_LEVEL : OPENPIC_SENSE_EDGE));
}

/*
 *  Map an interrupt source to one or more CPUs
 */
void openpic_mapirq(u_int irq, u_int cpumask)
{
	check_arg_irq(irq);
	openpic_write(&ISU[irq].Destination, cpumask);
}

/*
 *  Set the sense for an interrupt source (and disable it!)
 *
 *  sense: 1 for level, 0 for edge
 */
void openpic_set_sense(u_int irq, int sense)
{
	check_arg_irq(irq);
	openpic_safe_writefield(&ISU[irq].Vector_Priority,
				OPENPIC_SENSE_LEVEL,
				(sense ? OPENPIC_SENSE_LEVEL : 0));
}
