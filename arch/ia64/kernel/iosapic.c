/*
 * I/O SAPIC support.
 *
 * Copyright (C) 1999 Intel Corp.
 * Copyright (C) 1999 Asit Mallick <asit.k.mallick@intel.com>
 * Copyright (C) 2000-2002 J.I. Lee <jung-ik.lee@intel.com>
 * Copyright (C) 1999-2000, 2002 Hewlett-Packard Co.
 *	David Mosberger-Tang <davidm@hpl.hp.com>
 * Copyright (C) 1999 VA Linux Systems
 * Copyright (C) 1999,2000 Walt Drummond <drummond@valinux.com>
 *
 * 00/04/19	D. Mosberger	Rewritten to mirror more closely the x86 I/O APIC code.
 *				In particular, we now have separate handlers for edge
 *				and level triggered interrupts.
 * 00/10/27	Asit Mallick, Goutham Rao <goutham.rao@intel.com> IRQ vector allocation
 *				PCI to vector mapping, shared PCI interrupts.
 * 00/10/27	D. Mosberger	Document things a bit more to make them more understandable.
 *				Clean up much of the old IOSAPIC cruft.
 * 01/07/27	J.I. Lee	PCI irq routing, Platform/Legacy interrupts and fixes for
 *				ACPI S5(SoftOff) support.
 * 02/01/23	J.I. Lee	iosapic pgm fixes for PCI irq routing from _PRT
 * 02/01/07     E. Focht        <efocht@ess.nec.de> Redirectable interrupt vectors in
 *                              iosapic_set_affinity(), initializations for
 *                              /proc/irq/#/smp_affinity
 * 02/04/02	P. Diefenbaugh	Cleaned up ACPI PCI IRQ routing.
 * 02/04/18	J.I. Lee	bug fix in iosapic_init_pci_irq
 * 02/04/30	J.I. Lee	bug fix in find_iosapic to fix ACPI PCI IRQ to IOSAPIC mapping
 *				error
 */
/*
 * Here is what the interrupt logic between a PCI device and the CPU looks like:
 *
 * (1) A PCI device raises one of the four interrupt pins (INTA, INTB, INTC, INTD).  The
 *     device is uniquely identified by its bus--, and slot-number (the function
 *     number does not matter here because all functions share the same interrupt
 *     lines).
 *
 * (2) The motherboard routes the interrupt line to a pin on a IOSAPIC controller.
 *     Multiple interrupt lines may have to share the same IOSAPIC pin (if they're level
 *     triggered and use the same polarity).  Each interrupt line has a unique IOSAPIC
 *     irq number which can be calculated as the sum of the controller's base irq number
 *     and the IOSAPIC pin number to which the line connects.
 *
 * (3) The IOSAPIC uses an internal table to map the IOSAPIC pin into the IA-64 interrupt
 *     vector.  This interrupt vector is then sent to the CPU.
 *
 * In other words, there are two levels of indirections involved:
 *
 *	pci pin -> iosapic irq -> IA-64 vector
 *
 * Note: outside this module, IA-64 vectors are called "irqs".  This is because that's
 * the traditional name Linux uses for interrupt vectors.
 */
#include <linux/config.h>

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/pci.h>
#include <linux/smp.h>
#include <linux/smp_lock.h>
#include <linux/string.h>
#include <linux/irq.h>
#include <linux/acpi.h>

#include <asm/delay.h>
#include <asm/hw_irq.h>
#include <asm/io.h>
#include <asm/iosapic.h>
#include <asm/machvec.h>
#include <asm/processor.h>
#include <asm/ptrace.h>
#include <asm/system.h>


#undef DEBUG_IRQ_ROUTING
#undef OVERRIDE_DEBUG

static spinlock_t iosapic_lock = SPIN_LOCK_UNLOCKED;

/* PCI pin to IOSAPIC irq routing information.  This info typically comes from ACPI. */

static struct {
	int num_routes;
	struct pci_vector_struct *route;
} pci_irq;

/* This tables maps IA-64 vectors to the IOSAPIC pin that generates this vector. */

static struct iosapic_irq {
	char *addr;			/* base address of IOSAPIC */
	unsigned int base_irq;		/* first irq assigned to this IOSAPIC */
	char pin;			/* IOSAPIC pin (-1 => not an IOSAPIC irq) */
	unsigned char dmode	: 3;	/* delivery mode (see iosapic.h) */
	unsigned char polarity	: 1;	/* interrupt polarity (see iosapic.h) */
	unsigned char trigger	: 1;	/* trigger mode (see iosapic.h) */
} iosapic_irq[IA64_NUM_VECTORS];

static struct iosapic {
	char *addr;			/* base address of IOSAPIC */
	unsigned int 	base_irq;	/* first irq assigned to this IOSAPIC */
	unsigned short 	max_pin;	/* max input pin supported in this IOSAPIC */
	unsigned char	pcat_compat;	/* 8259 compatibility flag */
} iosapic_lists[256] __initdata;

static int num_iosapic = 0;


/*
 * Find an IOSAPIC associated with an IRQ
 */
static inline int __init
find_iosapic (unsigned int irq)
{
	int i;

	for (i = 0; i < num_iosapic; i++) {
		if ((unsigned) (irq - iosapic_lists[i].base_irq) <= iosapic_lists[i].max_pin)
			return i;
	}

	return -1;
}

/*
 * Translate IOSAPIC irq number to the corresponding IA-64 interrupt vector.  If no
 * entry exists, return -1.
 */
static int
iosapic_irq_to_vector (int irq)
{
	int vector;

	for (vector = 0; vector < IA64_NUM_VECTORS; ++vector)
		if (iosapic_irq[vector].base_irq + iosapic_irq[vector].pin == irq)
			return vector;
	return -1;
}

/*
 * Map PCI pin to the corresponding IA-64 interrupt vector.  If no such mapping exists,
 * return -1.
 */
int
pci_pin_to_vector (int bus, int slot, int pci_pin)
{
	struct pci_vector_struct *r;

	for (r = pci_irq.route; r < pci_irq.route + pci_irq.num_routes; ++r)
		if (r->bus == bus && (r->pci_id >> 16) == slot && r->pin == pci_pin)
			return iosapic_irq_to_vector(r->irq);
	return -1;
}

static void
set_rte (unsigned int vector, unsigned long dest)
{
	unsigned long pol, trigger, dmode;
	u32 low32, high32;
	char *addr;
	int pin;
	char redir;

#ifdef DEBUG_IRQ_ROUTING
	printk(KERN_DEBUG "set_rte: routing vector 0x%02x to 0x%lx\n", vector, dest);
#endif

	pin = iosapic_irq[vector].pin;
	if (pin < 0)
		return;		/* not an IOSAPIC interrupt */

	addr    = iosapic_irq[vector].addr;
	pol     = iosapic_irq[vector].polarity;
	trigger = iosapic_irq[vector].trigger;
	dmode   = iosapic_irq[vector].dmode;

	redir = (dmode == IOSAPIC_LOWEST_PRIORITY) ? 1 : 0;
#ifdef CONFIG_SMP
	set_irq_affinity_info(vector, (int)(dest & 0xffff), redir);
#endif

	low32 = ((pol << IOSAPIC_POLARITY_SHIFT) |
		 (trigger << IOSAPIC_TRIGGER_SHIFT) |
		 (dmode << IOSAPIC_DELIVERY_SHIFT) |
		 vector);

	/* dest contains both id and eid */
	high32 = (dest << IOSAPIC_DEST_SHIFT);

	writel(IOSAPIC_RTE_HIGH(pin), addr + IOSAPIC_REG_SELECT);
	writel(high32, addr + IOSAPIC_WINDOW);
	writel(IOSAPIC_RTE_LOW(pin), addr + IOSAPIC_REG_SELECT);
	writel(low32, addr + IOSAPIC_WINDOW);
}

static void
nop (unsigned int vector)
{
	/* do nothing... */
}

static void
mask_irq (unsigned int irq)
{
	unsigned long flags;
	char *addr;
	u32 low32;
	int pin;
	ia64_vector vec = irq_to_vector(irq);

	addr = iosapic_irq[vec].addr;
	pin = iosapic_irq[vec].pin;

	if (pin < 0)
		return;			/* not an IOSAPIC interrupt! */

	spin_lock_irqsave(&iosapic_lock, flags);
	{
		writel(IOSAPIC_RTE_LOW(pin), addr + IOSAPIC_REG_SELECT);
		low32 = readl(addr + IOSAPIC_WINDOW);

		low32 |= (1 << IOSAPIC_MASK_SHIFT);    /* set only the mask bit */
		writel(low32, addr + IOSAPIC_WINDOW);
	}
	spin_unlock_irqrestore(&iosapic_lock, flags);
}

static void
unmask_irq (unsigned int irq)
{
	unsigned long flags;
	char *addr;
	u32 low32;
	int pin;
	ia64_vector vec = irq_to_vector(irq);

	addr = iosapic_irq[vec].addr;
	pin = iosapic_irq[vec].pin;
	if (pin < 0)
		return;			/* not an IOSAPIC interrupt! */

	spin_lock_irqsave(&iosapic_lock, flags);
	{
		writel(IOSAPIC_RTE_LOW(pin), addr + IOSAPIC_REG_SELECT);
		low32 = readl(addr + IOSAPIC_WINDOW);

		low32 &= ~(1 << IOSAPIC_MASK_SHIFT);    /* clear only the mask bit */
		writel(low32, addr + IOSAPIC_WINDOW);
	}
	spin_unlock_irqrestore(&iosapic_lock, flags);
}


static void
iosapic_set_affinity (unsigned int irq, unsigned long mask)
{
#ifdef CONFIG_SMP
	unsigned long flags;
	u32 high32, low32;
	int dest, pin;
	char *addr;
	int redir = (irq & (1<<31)) ? 1 : 0;

	mask &= cpu_online_map;

	if (!mask || irq >= IA64_NUM_VECTORS)
		return;

	dest = cpu_physical_id(ffz(~mask));

	pin = iosapic_irq[irq].pin;
	addr = iosapic_irq[irq].addr;

	if (pin < 0)
		return;			/* not an IOSAPIC interrupt */

	set_irq_affinity_info(irq,dest,redir);

	/* dest contains both id and eid */
	high32 = dest << IOSAPIC_DEST_SHIFT;

	spin_lock_irqsave(&iosapic_lock, flags);
	{
		/* get current delivery mode by reading the low32 */
		writel(IOSAPIC_RTE_LOW(pin), addr + IOSAPIC_REG_SELECT);
		low32 = readl(addr + IOSAPIC_WINDOW);

		low32 &= ~(7 << IOSAPIC_DELIVERY_SHIFT);
		if (redir)
		        /* change delivery mode to lowest priority */
			low32 |= (IOSAPIC_LOWEST_PRIORITY << IOSAPIC_DELIVERY_SHIFT);
		else
		        /* change delivery mode to fixed */
			low32 |= (IOSAPIC_FIXED << IOSAPIC_DELIVERY_SHIFT);

		writel(IOSAPIC_RTE_HIGH(pin), addr + IOSAPIC_REG_SELECT);
		writel(high32, addr + IOSAPIC_WINDOW);
		writel(IOSAPIC_RTE_LOW(pin), addr + IOSAPIC_REG_SELECT);
		writel(low32, addr + IOSAPIC_WINDOW);
	}
	spin_unlock_irqrestore(&iosapic_lock, flags);
#endif
}

/*
 * Handlers for level-triggered interrupts.
 */

static unsigned int
iosapic_startup_level_irq (unsigned int irq)
{
	unmask_irq(irq);
	return 0;
}

static void
iosapic_end_level_irq (unsigned int irq)
{
	ia64_vector vec = irq_to_vector(irq);

	writel(vec, iosapic_irq[vec].addr + IOSAPIC_EOI);
}

#define iosapic_shutdown_level_irq	mask_irq
#define iosapic_enable_level_irq	unmask_irq
#define iosapic_disable_level_irq	mask_irq
#define iosapic_ack_level_irq		nop

struct hw_interrupt_type irq_type_iosapic_level = {
	.typename =	"IO-SAPIC-level",
	.startup =	iosapic_startup_level_irq,
	.shutdown =	iosapic_shutdown_level_irq,
	.enable =	iosapic_enable_level_irq,
	.disable =	iosapic_disable_level_irq,
	.ack =		iosapic_ack_level_irq,
	.end =		iosapic_end_level_irq,
	.set_affinity =	iosapic_set_affinity
};

/*
 * Handlers for edge-triggered interrupts.
 */

static unsigned int
iosapic_startup_edge_irq (unsigned int irq)
{
	unmask_irq(irq);
	/*
	 * IOSAPIC simply drops interrupts pended while the
	 * corresponding pin was masked, so we can't know if an
	 * interrupt is pending already.  Let's hope not...
	 */
	return 0;
}

static void
iosapic_ack_edge_irq (unsigned int irq)
{
	irq_desc_t *idesc = irq_desc(irq);
	/*
	 * Once we have recorded IRQ_PENDING already, we can mask the
	 * interrupt for real. This prevents IRQ storms from unhandled
	 * devices.
	 */
	if ((idesc->status & (IRQ_PENDING|IRQ_DISABLED)) == (IRQ_PENDING|IRQ_DISABLED))
		mask_irq(irq);
}

#define iosapic_enable_edge_irq		unmask_irq
#define iosapic_disable_edge_irq	nop
#define iosapic_end_edge_irq		nop

struct hw_interrupt_type irq_type_iosapic_edge = {
	.typename =	"IO-SAPIC-edge",
	.startup =	iosapic_startup_edge_irq,
	.shutdown =	iosapic_disable_edge_irq,
	.enable =	iosapic_enable_edge_irq,
	.disable =	iosapic_disable_edge_irq,
	.ack =		iosapic_ack_edge_irq,
	.end =		iosapic_end_edge_irq,
	.set_affinity =	iosapic_set_affinity
};

unsigned int
iosapic_version (char *addr)
{
	/*
	 * IOSAPIC Version Register return 32 bit structure like:
	 * {
	 *	unsigned int version   : 8;
	 *	unsigned int reserved1 : 8;
	 *	unsigned int pins      : 8;
	 *	unsigned int reserved2 : 8;
	 * }
	 */
	writel(IOSAPIC_VERSION, addr + IOSAPIC_REG_SELECT);
	return readl(IOSAPIC_WINDOW + addr);
}

/*
 * if the given vector is already owned by other,
 *  assign a new vector for the other and make the vector available
 */
static void
iosapic_reassign_vector (int vector)
{
	int new_vector;

	if (iosapic_irq[vector].pin >= 0 || iosapic_irq[vector].addr
	    || iosapic_irq[vector].base_irq || iosapic_irq[vector].dmode
	    || iosapic_irq[vector].polarity || iosapic_irq[vector].trigger)
	{
		new_vector = ia64_alloc_irq();
		printk("Reassigning vector 0x%x to 0x%x\n", vector, new_vector);
		memcpy (&iosapic_irq[new_vector], &iosapic_irq[vector],
			sizeof(struct iosapic_irq));
		memset (&iosapic_irq[vector], 0, sizeof(struct iosapic_irq));
		iosapic_irq[vector].pin = -1;
	}
}

static void
register_irq (u32 global_vector, int vector, int pin, unsigned char delivery,
	      unsigned long polarity, unsigned long edge_triggered,
	      u32 base_irq, char *iosapic_address)
{
	irq_desc_t *idesc;
	struct hw_interrupt_type *irq_type;

	gsi_to_vector(global_vector) = vector;
	iosapic_irq[vector].pin	= pin;
	iosapic_irq[vector].polarity = polarity ? IOSAPIC_POL_HIGH : IOSAPIC_POL_LOW;
	iosapic_irq[vector].dmode    = delivery;

	/*
	 * In override, it does not provide addr/base_irq.  global_vector is enough to
	 * locate iosapic addr, base_irq and pin by examining base_irq and max_pin of
	 * registered iosapics (tbd)
	 */
#ifndef	OVERRIDE_DEBUG
	if (iosapic_address) {
		iosapic_irq[vector].addr = iosapic_address;
		iosapic_irq[vector].base_irq = base_irq;
	}
#else
	if (iosapic_address) {
		if (iosapic_irq[vector].addr && (iosapic_irq[vector].addr != iosapic_address))
			printk("WARN: register_irq: diff IOSAPIC ADDRESS for gv %x, v %x\n",
			       global_vector, vector);
		iosapic_irq[vector].addr = iosapic_address;
		if (iosapic_irq[vector].base_irq && (iosapic_irq[vector].base_irq != base_irq)) {
			printk("WARN: register_irq: diff BASE IRQ %x for gv %x, v %x\n",
			       base_irq, global_vector, vector);
		}
		iosapic_irq[vector].base_irq = base_irq;
	} else if (!iosapic_irq[vector].addr)
		printk("WARN: register_irq: invalid override for gv %x, v %x\n",
		       global_vector, vector);
#endif
	if (edge_triggered) {
		iosapic_irq[vector].trigger = IOSAPIC_EDGE;
		irq_type = &irq_type_iosapic_edge;
	} else {
		iosapic_irq[vector].trigger = IOSAPIC_LEVEL;
		irq_type = &irq_type_iosapic_level;
	}

	idesc = irq_desc(vector);
	if (idesc->handler != irq_type) {
		if (idesc->handler != &no_irq_type)
			printk("register_irq(): changing vector 0x%02x from "
			       "%s to %s\n", vector, idesc->handler->typename, irq_type->typename);
		idesc->handler = irq_type;
	}
}

/*
 * ACPI can describe IOSAPIC interrupts via static tables and namespace
 * methods.  This provides an interface to register those interrupts and
 * program the IOSAPIC RTE.
 */
int
iosapic_register_irq (u32 global_vector, unsigned long polarity, unsigned long
                      edge_triggered, u32 base_irq, char *iosapic_address)
{
	int vector;

	vector = iosapic_irq_to_vector(global_vector);
	if (vector < 0)
		vector = ia64_alloc_irq();

	register_irq (global_vector, vector, global_vector - base_irq,
			IOSAPIC_LOWEST_PRIORITY, polarity, edge_triggered,
			base_irq, iosapic_address);

	printk("IOSAPIC 0x%x(%s,%s) -> Vector 0x%x\n", global_vector,
	       (polarity ? "high" : "low"), (edge_triggered ? "edge" : "level"), vector);

	/* program the IOSAPIC routing table */
	set_rte(vector, (ia64_get_lid() >> 16) & 0xffff);
	return vector;
}

/*
 * ACPI calls this when it finds an entry for a platform interrupt.
 * Note that the irq_base and IOSAPIC address must be set in iosapic_init().
 */
int
iosapic_register_platform_irq (u32 int_type, u32 global_vector,
			       u32 iosapic_vector, u16 eid, u16 id, unsigned long polarity,
			       unsigned long edge_triggered, u32 base_irq, char *iosapic_address)
{
	unsigned char delivery;
	int vector;

	switch (int_type) {
	      case ACPI_INTERRUPT_PMI:
		vector = iosapic_vector;
		/*
		 * since PMI vector is alloc'd by FW(ACPI) not by kernel,
		 * we need to make sure the vector is available
		 */
		iosapic_reassign_vector(vector);
		delivery = IOSAPIC_PMI;
		break;
	      case ACPI_INTERRUPT_INIT:
		vector = ia64_alloc_irq();
		delivery = IOSAPIC_INIT;
		break;
	      case ACPI_INTERRUPT_CPEI:
		vector = IA64_PCE_VECTOR;
		delivery = IOSAPIC_LOWEST_PRIORITY;
		break;
	      default:
		printk("iosapic_register_platform_irq(): invalid int type\n");
		return -1;
	}

	register_irq(global_vector, vector, global_vector - base_irq, delivery, polarity,
		     edge_triggered, base_irq, iosapic_address);

	printk("PLATFORM int 0x%x: IOSAPIC 0x%x(%s,%s) -> Vector 0x%x CPU %.02u:%.02u\n",
	       int_type, global_vector, (polarity ? "high" : "low"),
	       (edge_triggered ? "edge" : "level"), vector, eid, id);

	/* program the IOSAPIC routing table */
	set_rte(vector, ((id << 8) | eid) & 0xffff);
	return vector;
}


/*
 * ACPI calls this when it finds an entry for a legacy ISA interrupt.
 * Note that the irq_base and IOSAPIC address must be set in iosapic_init().
 */
void
iosapic_register_legacy_irq (unsigned long irq,
			     unsigned long pin, unsigned long polarity,
			     unsigned long edge_triggered)
{
	int vector = isa_irq_to_vector(irq);

	register_irq(irq, vector, (int)pin, IOSAPIC_LOWEST_PRIORITY, polarity, edge_triggered,
		     0, NULL);		/* ignored for override */

#ifdef DEBUG_IRQ_ROUTING
	printk("ISA: IRQ %u -> IOSAPIC irq 0x%02x (%s, %s) -> vector %02x\n",
	       (unsigned) irq, (unsigned) pin,
	       polarity ? "high" : "low", edge_triggered ? "edge" : "level",
	       vector);
#endif

	/* program the IOSAPIC routing table */
	set_rte(vector, (ia64_get_lid() >> 16) & 0xffff);
}

void __init
iosapic_init (unsigned long phys_addr, unsigned int base_irq, int pcat_compat)
{
	int irq, max_pin, vector, pin;
	unsigned int ver;
	char *addr;
	static int first_time = 1;

	if (first_time) {
		first_time = 0;
		for (vector = 0; vector < IA64_NUM_VECTORS; ++vector)
			iosapic_irq[vector].pin = -1;	/* mark as unused */
	}

	if (pcat_compat) {
		/*
		 * Disable the compatibility mode interrupts (8259 style), needs IN/OUT support
		 * enabled.
		 */
		printk("%s: Disabling PC-AT compatible 8259 interrupts\n", __FUNCTION__);
		outb(0xff, 0xA1);
		outb(0xff, 0x21);
	}

	addr = ioremap(phys_addr, 0);
	ver = iosapic_version(addr);
	max_pin = (ver >> 16) & 0xff;

	iosapic_lists[num_iosapic].addr = addr;
	iosapic_lists[num_iosapic].pcat_compat = pcat_compat;
	iosapic_lists[num_iosapic].base_irq = base_irq;
	iosapic_lists[num_iosapic].max_pin = max_pin;
	num_iosapic++;

	printk("IOSAPIC: version %x.%x, address 0x%lx, IRQs 0x%02x-0x%02x\n",
	       (ver & 0xf0) >> 4, (ver & 0x0f), phys_addr, base_irq, base_irq + max_pin);

	if ((base_irq == 0) && pcat_compat) {
		/*
		 * Map the legacy ISA devices into the IOSAPIC data.  Some of these may
		 * get reprogrammed later on with data from the ACPI Interrupt Source
		 * Override table.
		 */
		for (irq = 0; irq < 16; ++irq) {
			vector = isa_irq_to_vector(irq);
			if ((pin = iosapic_irq[vector].pin) == -1)
				pin = irq;

			register_irq(irq, vector, pin,
				     /* IOSAPIC_POL_HIGH, IOSAPIC_EDGE */
				     IOSAPIC_LOWEST_PRIORITY, 1, 1, base_irq, addr);

#ifdef DEBUG_IRQ_ROUTING
			printk("ISA: IRQ %u -> IOSAPIC irq 0x%02x (high, edge) -> vector 0x%02x\n",
			       irq, iosapic_irq[vector].base_irq + iosapic_irq[vector].pin,
			       vector);
#endif

			/* program the IOSAPIC routing table: */
			set_rte(vector, (ia64_get_lid() >> 16) & 0xffff);
		}
	}
}

static void __init
iosapic_init_pci_irq (void)
{
	int i, index, vector, pin;
	int base_irq, max_pin, pcat_compat;
	unsigned int irq;
	char *addr;

	if (acpi_get_prt(&pci_irq.route, &pci_irq.num_routes))
		return;

	for (i = 0; i < pci_irq.num_routes; i++) {

		irq = pci_irq.route[i].irq;

		index = find_iosapic(irq);
		if (index < 0) {
			printk("PCI: IRQ %u has no IOSAPIC mapping\n", irq);
			continue;
		}

		addr = iosapic_lists[index].addr;
		base_irq = iosapic_lists[index].base_irq;
		max_pin = iosapic_lists[index].max_pin;
		pcat_compat = iosapic_lists[index].pcat_compat;
		pin = irq - base_irq;

		if ((unsigned) pin > max_pin)
			/* the interrupt route is for another controller... */
			continue;

		if (pcat_compat && (irq < 16))
			vector = isa_irq_to_vector(irq);
		else {
			vector = iosapic_irq_to_vector(irq);
			if (vector < 0)
				/* new iosapic irq: allocate a vector for it */
				vector = ia64_alloc_irq();
		}

		register_irq(irq, vector, pin, IOSAPIC_LOWEST_PRIORITY, 0, 0, base_irq, addr);

#ifdef DEBUG_IRQ_ROUTING
		printk("PCI: (B%d,I%d,P%d) -> IOSAPIC irq 0x%02x -> vector 0x%02x\n",
		       pci_irq.route[i].bus, pci_irq.route[i].pci_id>>16, pci_irq.route[i].pin,
		       iosapic_irq[vector].base_irq + iosapic_irq[vector].pin, vector);
#endif
		/*
		 * NOTE: The IOSAPIC RTE will be programmed in iosapic_pci_fixup().  It
		 * needs to be done there to ensure PCI hotplug works right.
		 */
	}
}

void
iosapic_pci_fixup (int phase)
{
	struct	pci_dev	*dev;
	unsigned char pin;
	int vector;
	struct hw_interrupt_type *irq_type;
	irq_desc_t *idesc;

	if (phase == 0) {
		iosapic_init_pci_irq();
		return;
	}

	if (phase != 1)
		return;

	pci_for_each_dev(dev) {
		pci_read_config_byte(dev, PCI_INTERRUPT_PIN, &pin);
		if (pin) {
			pin--;          /* interrupt pins are numbered starting from 1 */
			vector = pci_pin_to_vector(dev->bus->number, PCI_SLOT(dev->devfn), pin);
			if (vector < 0 && dev->bus->parent) {
				/* go back to the bridge */
				struct pci_dev *bridge = dev->bus->self;

				if (bridge) {
					/* allow for multiple bridges on an adapter */
					do {
						/* do the bridge swizzle... */
						pin = (pin + PCI_SLOT(dev->devfn)) % 4;
						vector = pci_pin_to_vector(bridge->bus->number,
									   PCI_SLOT(bridge->devfn),
									   pin);
					} while (vector < 0 && (bridge = bridge->bus->self));
				}
				if (vector >= 0)
					printk(KERN_WARNING
					       "PCI: using PPB(B%d,I%d,P%d) to get vector %02x\n",
					       dev->bus->number, PCI_SLOT(dev->devfn),
					       pin, vector);
				else
					printk(KERN_WARNING
					       "PCI: Couldn't map irq for (B%d,I%d,P%d)\n",
					       dev->bus->number, PCI_SLOT(dev->devfn), pin);
			}
			if (vector >= 0) {
				printk("PCI->APIC IRQ transform: (B%d,I%d,P%d) -> 0x%02x\n",
				       dev->bus->number, PCI_SLOT(dev->devfn), pin, vector);
				dev->irq = vector;

				irq_type = &irq_type_iosapic_level;
				idesc = irq_desc(vector);
				if (idesc->handler != irq_type) {
					if (idesc->handler != &no_irq_type)
						printk("iosapic_pci_fixup: changing vector 0x%02x "
						       "from %s to %s\n", vector,
						       idesc->handler->typename,
						       irq_type->typename);
					idesc->handler = irq_type;
				}
#ifdef CONFIG_SMP
				/*
				 * For platforms that do not support interrupt redirect
				 * via the XTP interface, we can round-robin the PCI
				 * device interrupts to the processors
				 */
				if (!(smp_int_redirect & SMP_IRQ_REDIRECTION)) {
					static int cpu_index = 0;

					while (!cpu_online(cpu_index))
						if (++cpu_index >= NR_CPUS)
							cpu_index = 0;

					set_rte(vector, cpu_physical_id(cpu_index) & 0xffff);
				} else {
					/*
					 * Direct the interrupt vector to the current cpu,
					 * platform redirection will distribute them.
					 */
					set_rte(vector, (ia64_get_lid() >> 16) & 0xffff);
				}
#else
				/* direct the interrupt vector to the running cpu id */
				set_rte(vector, (ia64_get_lid() >> 16) & 0xffff);
#endif
			}
		}
		/*
		 * Nothing to fixup
		 * Fix out-of-range IRQ numbers
		 */
		if (dev->irq >= IA64_NUM_VECTORS)
			dev->irq = 15;	/* Spurious interrupts */
	}
}
