/*
 * linux/arch/sh/kernel/irq_intc2.c
 *
 * Copyright (C) 2001 David J. Mckay (david.mckay@st.com)
 *
 * May be copied or modified under the terms of the GNU General Public
 * License.  See linux/COPYING for more information.                            
 *
 * Interrupt handling for INTC2-based IRQ.
 *
 * These are the "new Hitachi style" interrupts, as present on the 
 * Hitachi 7751 and the STM ST40 STB1.
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/irq.h>

#include <asm/system.h>
#include <asm/io.h>
#include <asm/machvec.h>


struct intc2_data {
	unsigned int addr;	/* Address of Interrupt Priority Register */
	int mask; /*Mask to apply */
};


static struct intc2_data intc2_data[NR_INTC2_IRQS];

static void enable_intc2_irq(unsigned int irq);
static void disable_intc2_irq(unsigned int irq);

/* shutdown is same as "disable" */
#define shutdown_intc2_irq disable_intc2_irq

static void mask_and_ack_intc2(unsigned int);
static void end_intc2_irq(unsigned int irq);

static unsigned int startup_intc2_irq(unsigned int irq)
{ 
	enable_intc2_irq(irq);
	return 0; /* never anything pending */
}

static struct hw_interrupt_type intc2_irq_type = {
	"INTC2-IRQ",
	startup_intc2_irq,
	shutdown_intc2_irq,
	enable_intc2_irq,
	disable_intc2_irq,
	mask_and_ack_intc2,
	end_intc2_irq
};

static void disable_intc2_irq(unsigned int irq)
{
	unsigned addr;
	int offset=irq-INTC2_FIRST_IRQ;
	unsigned val,flags;

	// Sanity check
	if(offset<0 || offset>=NR_INTC2_IRQS) return;

	addr=intc2_data[offset].addr+INTC2_INTMSK_OFFSET;

	local_irq_save(flags);
	val=ctrl_inl(addr);
	val|=intc2_data[offset].mask;
	ctrl_outl(val,addr);

	local_irq_restore(flags);
}

static void enable_intc2_irq(unsigned int irq)
{
	int offset=irq-INTC2_FIRST_IRQ;

	// Sanity check
	if(offset<0 || offset>=NR_INTC2_IRQS) return;

	ctrl_outl(intc2_data[offset].mask,
		  intc2_data[offset].addr+INTC2_INTMSKCLR_OFFSET);

}

static void mask_and_ack_intc2(unsigned int irq)
{
	disable_intc2_irq(irq);
}

static void end_intc2_irq(unsigned int irq)
{
	if (!(irq_desc[irq].status & (IRQ_DISABLED|IRQ_INPROGRESS)))
		enable_intc2_irq(irq);
}

void make_intc2_irq(unsigned int irq, unsigned int addr, 
		    unsigned int group,int pos, int priority)
{
	int offset=irq-INTC2_FIRST_IRQ;
	unsigned flags,val;

	if(offset<0 || offset>=NR_INTC2_IRQS) {
		return;
	}

	disable_irq_nosync(irq);
	/* Fill the data we need */
	intc2_data[offset].addr=addr;
	intc2_data[offset].mask=1<<pos;
		
	/* Set the priority level */
	local_irq_save(flags);
	val=ctrl_inl(addr+INTC2_INTPRI_OFFSET);
	val|=(priority)<< (group<<4);
	ctrl_outl(val,addr+INTC2_INTPRI_OFFSET);
	local_irq_restore(flags);

	irq_desc[irq].handler=&intc2_irq_type;

	disable_intc2_irq(irq);
}



