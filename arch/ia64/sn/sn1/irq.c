/*
 * Platform dependent support for SGI SN1
 *
 * Copyright (C) 2000   Silicon Graphics
 * Copyright (C) 2000   Jack Steiner (steiner@sgi.com)
 * Copyright (C) 2000   Alan Mayer (ajm@sgi.com)
 * Copyright (C) 2000   Kanoj Sarcar (kanoj@sgi.com)
 */

#include <linux/config.h>
#include <linux/init.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <asm/current.h>
#include <linux/irq.h>
#include <linux/interrupt.h>
#include <asm/sn/sgi.h>
#include <asm/sn/iograph.h>
#include <asm/sn/invent.h>
#include <linux/devfs_fs_kernel.h>
#include <asm/sn/hcl.h>
#include <asm/sn/types.h>
#include <asm/sn/pci/bridge.h>
#include <asm/sn/pci/pciio.h>
#include <asm/sn/pci/pciio_private.h>
#include <asm/sn/sn_cpuid.h>
#include <asm/sn/sn1/bedrock.h>
#include <asm/sn/intr.h>
#include <asm/sn/addrs.h>
#include <asm/sn/sn1/addrs.h>
#include <asm/sn/iobus.h>
#include <asm/sn/sn1/arch.h>
#include <asm/sn/synergy.h>

#define IRQ_BIT_OFFSET 64

int bit_pos_to_irq(int bit)
{
	if (bit > 118)
		bit = 118;
	return (bit + IRQ_BIT_OFFSET);
}

static inline int irq_to_bit_pos(int irq)
{
	int bit = irq - IRQ_BIT_OFFSET;

	if (bit > 63)
		bit -= 64;
	return bit;
}

static unsigned int
sn1_startup_irq(unsigned int irq)
{
        return(0);
}

static void
sn1_shutdown_irq(unsigned int irq)
{
}

static void
sn1_disable_irq(unsigned int irq)
{
}

static void
sn1_enable_irq(unsigned int irq)
{
}

static void
sn1_ack_irq(unsigned int irq)
{
}

static void
sn1_end_irq(unsigned int irq)
{
	int bit;

	bit = irq_to_bit_pos(irq);
	LOCAL_HUB_CLR_INTR(bit);
}

static void
sn1_set_affinity_irq(unsigned int irq, unsigned long mask)
{
}

struct hw_interrupt_type irq_type_sn1 = {
        "sn1_irq",
        sn1_startup_irq,
        sn1_shutdown_irq,
        sn1_enable_irq,
        sn1_disable_irq,
        sn1_ack_irq,
        sn1_end_irq,
        sn1_set_affinity_irq
};


void
sn1_irq_init (void)
{
	int i;

	for (i = 0; i <= NR_IRQS; ++i) {
		if (idesc_from_vector(i)->handler == &no_irq_type) {
			idesc_from_vector(i)->handler = &irq_type_sn1;
		}
	}
}



#if !defined(CONFIG_IA64_SGI_SN1)
void
sn1_pci_fixup(int arg)
{
}
#endif

#ifdef CONFIG_PERCPU_IRQ

extern irq_desc_t irq_descX[NR_IRQS];
irq_desc_t *irq_desc_ptr[NR_CPUS] = { irq_descX };

/*
 * Each slave AP allocates its own irq table.
 */
int __init cpu_irq_init(void)
{
	irq_desc_ptr[smp_processor_id()] = (irq_desc_t *)kmalloc(sizeof(irq_descX), GFP_KERNEL);
	if (irq_desc_ptr[smp_processor_id()] == 0)
		return(-1);
	memcpy(irq_desc_ptr[smp_processor_id()], irq_desc_ptr[0], 
							sizeof(irq_descX));
	return(0);
}

/*
 * This can also allocate the irq tables for the other cpus, specifically
 * on their nodes.
 */
int __init master_irq_init(void)
{
	return(0);
}

/*
 * The input is an ivt level.
 */
irq_desc_t *idesc_from_vector(unsigned int ivnum)
{
	return(irq_desc_ptr[smp_processor_id()] + ivnum);
}

/*
 * The input is a "soft" level, that we encoded in.
 */
irq_desc_t *idesc_from_irq(unsigned int irq)
{
	return(irq_desc_ptr[irq >> 8] + (irq & 0xff));
}

unsigned int ivector_from_irq(unsigned int irq)
{
	return(irq & 0xff);
}

/*
 * This should return the Linux irq # for the i/p vector on the
 * i/p cpu. We currently do not track this.
 */
unsigned int irq_from_cpuvector(int cpunum, unsigned int vector)
{
	return (vector);
}

#endif /* CONFIG_PERCPU_IRQ */
