/*
 * Platform dependent support for SGI SN1
 *
 * Copyright (C) 2000   Silicon Graphics
 * Copyright (C) 2000   Jack Steiner (steiner@sgi.com)
 * Copyright (C) 2000   Alan Mayer (ajm@sgi.com)
 */

#include <linux/config.h>
#include <linux/init.h>
#include <linux/sched.h>
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


int bit_pos_to_irq(int bit);
int irq_to_bit_pos(int irq);
void add_interrupt_randomness(int irq);
void * kmalloc(size_t size, int flags);
void kfree(const void *);
int sgi_pci_intr_support (unsigned int, device_desc_t *, devfs_handle_t *, pciio_intr_line_t *, devfs_handle_t *);
pciio_intr_t pciio_intr_alloc(devfs_handle_t, device_desc_t, pciio_intr_line_t, devfs_handle_t);
int request_irq(unsigned int, void (*)(int, void *, struct pt_regs *), unsigned long, const char *, void *);

/* This should be dynamically allocated, at least part of it. */
/* For the time being, though, we'll statically allocate it */
/* because kmalloc hasn't been initiallized at the time this */
/* array is initiallized.  One way to do it would be to statically */
/* allocate the data for node 0, then let other nodes, as they */
/* need it, dynamically allocate their own data space. */

struct sn1_cnode_action_list *sn1_node_actions[MAX_COMPACT_NODES];
struct sn1_cnode_action_list sn1_actions[MAX_COMPACT_NODES][256];


extern int numnodes;

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
}

static void
sn1_set_affinity_irq(unsigned int irq, unsigned long mask)
{
}


static void
sn1_handle_irq(int irq, void *dummy, struct pt_regs *regs)
{
	int bit, cnode;
	struct sn1_cnode_action_list *alp;
	struct sn1_intr_action *ap;
	void (*handler)(int, void *, struct pt_regs *);
	unsigned long flags = 0;
	int cpuid = smp_processor_id();


	bit = irq_to_bit_pos(irq);
	LOCAL_HUB_CLR_INTR(bit);
	cnode = cpuid_to_cnodeid(cpuid);
	alp = sn1_node_actions[cnode];
	ap = alp[irq].action_list;
	if (ap == NULL) {
		return;
	}
	while (ap) {
		flags |= ap->flags;
		handler = ap->handler;
		(*handler)(irq,ap->intr_arg,regs);
		ap = ap->next;
	}
	if ((flags & SA_SAMPLE_RANDOM) != 0)
                add_interrupt_randomness(irq);

        return;
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

struct irqaction sn1_irqaction = {
	sn1_handle_irq,
	0,
	0,
	NULL,
	NULL,
	NULL,
};

void
sn1_irq_init (void)
{
	int i,j;

	for (i = 0; i <= NR_IRQS; ++i) {
		if (irq_desc[i].handler == &no_irq_type) {
			irq_desc[i].handler = &irq_type_sn1;
			if (i >=71 && i <= 181) {
				irq_desc[i].action = &sn1_irqaction;
			}
		}
	}

	for (i = 0; i < numnodes; i++) {
		sn1_node_actions[i] = sn1_actions[i];
		memset(sn1_node_actions[i], 0,
			sizeof(struct sn1_cnode_action_list) *
			(IA64_MAX_VECTORED_IRQ + 1));
		for (j=0; j<IA64_MAX_VECTORED_IRQ+1; j++) {
			spin_lock_init(&sn1_node_actions[i][j].action_list_lock);
		}
	}
}


int          
sn1_request_irq (unsigned int requested_irq, void (*handler)(int, void *, struct pt_regs *),
             unsigned long irqflags, const char * devname, void *dev_id)
{ 
	devfs_handle_t curr_dev;
	devfs_handle_t dev;
	pciio_intr_t intr_handle;
	pciio_intr_line_t line;
	device_desc_t dev_desc;
        int cpuid, bit, cnode;
	struct sn1_intr_action *ap, *new_ap;
	struct sn1_cnode_action_list *alp;
	int irq;

	if ( (requested_irq & 0xff) == 0 ) {
		int ret;

		sgi_pci_intr_support(requested_irq,
			&dev_desc, &dev, &line, &curr_dev);
		intr_handle = pciio_intr_alloc(curr_dev, NULL, line, curr_dev);
		bit = intr_handle->pi_irq;
		cpuid = intr_handle->pi_cpu;
		irq = bit_pos_to_irq(bit);
		cnode = cpuid_to_cnodeid(cpuid);
		new_ap = (struct sn1_intr_action *)kmalloc(
			sizeof(struct sn1_intr_action), GFP_KERNEL);
		irq_desc[irq].status = 0;
		new_ap->handler = handler;
		new_ap->intr_arg = dev_id;
		new_ap->flags = irqflags;
		new_ap->next = NULL;
		alp = sn1_node_actions[cnode];

		spin_lock(&alp[irq].action_list_lock);
		ap = alp[irq].action_list;
		/* check action list for "share" consistency */
		while (ap){
			if (!(ap->flags & irqflags & SA_SHIRQ) ) {
				return(-EBUSY);
				spin_unlock(&alp[irq].action_list_lock);
			}
			ap = ap->next;
		}
		ap = alp[irq].action_list;
		if (ap) {
			while (ap->next) {
				ap = ap->next;
			}
			ap->next = new_ap;
		} else {
			alp[irq].action_list = new_ap;
		}
		ret = pciio_intr_connect(intr_handle, (intr_func_t)handler, dev_id, NULL);
		if (ret) { /* connect failed, undo what we did. */
			new_ap = alp[irq].action_list;
			if (new_ap == ap) {
				alp[irq].action_list = NULL;
				kfree(ap);
			} else {
				while (new_ap->next && new_ap->next != ap) {
					new_ap = new_ap->next;
				}
				if (new_ap->next == ap) {
					new_ap->next = ap->next;
					kfree(ap);
				}
			}
		}
			
		spin_unlock(&alp[irq].action_list_lock);
		return(ret);
	} else {
		return(request_irq(requested_irq, handler, irqflags, devname, dev_id));
	}
}

#if !defined(CONFIG_IA64_SGI_IO)
void
sn1_pci_fixup(int arg)
{
}
#endif

int
bit_pos_to_irq(int bit) {
#define BIT_TO_IRQ 64

        return bit + BIT_TO_IRQ;
}

int
irq_to_bit_pos(int irq) {
#define IRQ_TO_BIT 64

        return irq - IRQ_TO_BIT;
}
