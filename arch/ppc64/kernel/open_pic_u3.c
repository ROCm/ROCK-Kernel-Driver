/*
 *  arch/ppc/kernel/open_pic.c -- OpenPIC Interrupt Handling
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
#include <linux/init.h>
#include <linux/irq.h>
#include <linux/smp.h>
#include <linux/interrupt.h>
#include <asm/ptrace.h>
#include <asm/signal.h>
#include <asm/io.h>
#include <asm/pgtable.h>
#include <asm/irq.h>
#include <asm/prom.h>

#include <asm/machdep.h>

#include "open_pic.h"
#include "open_pic_defs.h"

void* OpenPIC2_Addr;
static volatile struct OpenPIC *OpenPIC2 = NULL;

extern u_int OpenPIC_NumInitSenses;
extern u_char *OpenPIC_InitSenses;

static u_int NumSources;
static int NumISUs;
static int open_pic2_irq_offset;

static OpenPIC_SourcePtr ISU2[OPENPIC_MAX_ISU];

unsigned int openpic2_vec_spurious;

/*
 *  Accesses to the current processor's openpic registers
 *  U3 secondary openpic has only one output
 */
#define THIS_CPU		Processor[0]
#define DECL_THIS_CPU
#define CHECK_THIS_CPU

#define GET_ISU(source)	ISU2[(source) >> 4][(source) & 0xf]

static inline u_int openpic2_read(volatile u_int *addr)
{
	u_int val;

	val = in_be32(addr);
	return val;
}

static inline void openpic2_write(volatile u_int *addr, u_int val)
{
	out_be32(addr, val);
}

static inline u_int openpic2_readfield(volatile u_int *addr, u_int mask)
{
	u_int val = openpic2_read(addr);
	return val & mask;
}

static inline void openpic2_writefield(volatile u_int *addr, u_int mask,
			       u_int field)
{
	u_int val = openpic2_read(addr);
	openpic2_write(addr, (val & ~mask) | (field & mask));
}

static inline void openpic2_clearfield(volatile u_int *addr, u_int mask)
{
	openpic2_writefield(addr, mask, 0);
}

static inline void openpic2_setfield(volatile u_int *addr, u_int mask)
{
	openpic2_writefield(addr, mask, mask);
}

static void openpic2_safe_writefield(volatile u_int *addr, u_int mask,
				    u_int field)
{
	unsigned int loops = 100000;

	openpic2_setfield(addr, OPENPIC_MASK);
	while (openpic2_read(addr) & OPENPIC_ACTIVITY) {
		if (!loops--) {
			printk(KERN_ERR "openpic2_safe_writefield timeout\n");
			break;
		}
	}
	openpic2_writefield(addr, mask | OPENPIC_MASK, field | OPENPIC_MASK);
}


static inline void openpic2_reset(void)
{
	openpic2_setfield(&OpenPIC2->Global.Global_Configuration0,
			 OPENPIC_CONFIG_RESET);
}

static void openpic2_disable_8259_pass_through(void)
{
	openpic2_setfield(&OpenPIC2->Global.Global_Configuration0,
			 OPENPIC_CONFIG_8259_PASSTHROUGH_DISABLE);
}

/*
 *  Find out the current interrupt
 */
static u_int openpic2_irq(void)
{
	u_int vec;
	DECL_THIS_CPU;
	CHECK_THIS_CPU;
	vec = openpic2_readfield(&OpenPIC2->THIS_CPU.Interrupt_Acknowledge,
				 OPENPIC_VECTOR_MASK);
	return vec;
}

static void openpic2_eoi(void)
{
	DECL_THIS_CPU;
	CHECK_THIS_CPU;
	openpic2_write(&OpenPIC2->THIS_CPU.EOI, 0);
	/* Handle PCI write posting */
	(void)openpic2_read(&OpenPIC2->THIS_CPU.EOI);
}


static inline u_int openpic2_get_priority(void)
{
	DECL_THIS_CPU;
	CHECK_THIS_CPU;
	return openpic2_readfield(&OpenPIC2->THIS_CPU.Current_Task_Priority,
				  OPENPIC_CURRENT_TASK_PRIORITY_MASK);
}

static void openpic2_set_priority(u_int pri)
{
	DECL_THIS_CPU;
	CHECK_THIS_CPU;
	openpic2_writefield(&OpenPIC2->THIS_CPU.Current_Task_Priority,
			    OPENPIC_CURRENT_TASK_PRIORITY_MASK, pri);
}

/*
 *  Get/set the spurious vector
 */
static inline u_int openpic2_get_spurious(void)
{
	return openpic2_readfield(&OpenPIC2->Global.Spurious_Vector,
				  OPENPIC_VECTOR_MASK);
}

static void openpic2_set_spurious(u_int vec)
{
	openpic2_writefield(&OpenPIC2->Global.Spurious_Vector, OPENPIC_VECTOR_MASK,
			    vec);
}

/*
 *  Enable/disable an external interrupt source
 *
 *  Externally called, irq is an offseted system-wide interrupt number
 */
static void openpic2_enable_irq(u_int irq)
{
	unsigned int loops = 100000;

	openpic2_clearfield(&GET_ISU(irq - open_pic2_irq_offset).Vector_Priority, OPENPIC_MASK);
	/* make sure mask gets to controller before we return to user */
	do {
		if (!loops--) {
			printk(KERN_ERR "openpic_enable_irq timeout\n");
			break;
		}

		mb(); /* sync is probably useless here */
	} while(openpic2_readfield(&GET_ISU(irq - open_pic2_irq_offset).Vector_Priority,
			OPENPIC_MASK));
}

static void openpic2_disable_irq(u_int irq)
{
	u32 vp;
	unsigned int loops = 100000;
	
	openpic2_setfield(&GET_ISU(irq - open_pic2_irq_offset).Vector_Priority,
			  OPENPIC_MASK);
	/* make sure mask gets to controller before we return to user */
	do {
		if (!loops--) {
			printk(KERN_ERR "openpic_disable_irq timeout\n");
			break;
		}

		mb();  /* sync is probably useless here */
		vp = openpic2_readfield(&GET_ISU(irq - open_pic2_irq_offset).Vector_Priority,
    			OPENPIC_MASK | OPENPIC_ACTIVITY);
	} while((vp & OPENPIC_ACTIVITY) && !(vp & OPENPIC_MASK));
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
static void openpic2_initirq(u_int irq, u_int pri, u_int vec, int pol, int sense)
{
	openpic2_safe_writefield(&GET_ISU(irq).Vector_Priority,
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
static void openpic2_mapirq(u_int irq, u_int physmask)
{
	openpic2_write(&GET_ISU(irq).Destination, physmask);
}

/*
 *  Set the sense for an interrupt source (and disable it!)
 *
 *  sense: 1 for level, 0 for edge
 */
static inline void openpic2_set_sense(u_int irq, int sense)
{
	openpic2_safe_writefield(&GET_ISU(irq).Vector_Priority,
				 OPENPIC_SENSE_LEVEL,
				 (sense ? OPENPIC_SENSE_LEVEL : 0));
}

static void openpic2_end_irq(unsigned int irq_nr)
{
	openpic2_eoi();
}

int openpic2_get_irq(struct pt_regs *regs)
{
	int irq = openpic2_irq();

	if (irq == openpic2_vec_spurious)
		return -1;
	return irq + open_pic2_irq_offset;
}

struct hw_interrupt_type open_pic2 = {
	" OpenPIC2 ",
	NULL,
	NULL,
	openpic2_enable_irq,
	openpic2_disable_irq,
	NULL,
	openpic2_end_irq,
};

void __init openpic2_init(int offset)
{
	u_int t, i;
	const char *version;

	if (!OpenPIC2_Addr) {
		printk(KERN_INFO "No OpenPIC2 found !\n");
		return;
	}
	OpenPIC2 = (volatile struct OpenPIC *)OpenPIC2_Addr;

	ppc64_boot_msg(0x20, "OpenPic U3 Init");

	t = openpic2_read(&OpenPIC2->Global.Feature_Reporting0);
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
	printk(KERN_INFO "OpenPIC (U3) Version %s\n", version);

	open_pic2_irq_offset = offset;

	for (i=0; i<128; i+=0x10) {
		ISU2[i>>4] = &((struct OpenPIC *)OpenPIC2_Addr)->Source[i];
		NumISUs++;
	}
	NumSources = NumISUs * 0x10;
	openpic2_vec_spurious = NumSources;

	openpic2_set_priority(0xf);

	/* Init all external sources */
	for (i = 0; i < NumSources; i++) {
		int pri, sense;

		/* the bootloader may have left it enabled (bad !) */
		openpic2_disable_irq(i+offset);

		pri = 8;
		sense = (i < OpenPIC_NumInitSenses) ? OpenPIC_InitSenses[i]: 1;
		if (sense)
			irq_desc[i+offset].status = IRQ_LEVEL;

		/* Enabled, Priority 8 or 9 */
		openpic2_initirq(i, pri, i, !sense, sense);
		/* Processor 0 */
		openpic2_mapirq(i, 0x1);
	}

	/* Init descriptors */
	for (i = offset; i < NumSources + offset; i++)
		irq_desc[i].handler = &open_pic2;

	/* Initialize the spurious interrupt */
	openpic2_set_spurious(openpic2_vec_spurious);

	openpic2_set_priority(0);
	openpic2_disable_8259_pass_through();

	ppc64_boot_msg(0x25, "OpenPic2 Done");
}
