/*
 * File:	msi.c
 * Purpose:	PCI Message Signaled Interrupt (MSI)
 *
 * Copyright (C) 2003-2004 Intel
 * Copyright (C) Tom Long Nguyen (tom.l.nguyen@intel.com)
 */

#include <linux/mm.h>
#include <linux/irq.h>
#include <linux/interrupt.h>
#include <linux/init.h>
#include <linux/config.h>
#include <linux/ioport.h>
#include <linux/smp_lock.h>
#include <linux/pci.h>
#include <linux/proc_fs.h>

#include <asm/errno.h>
#include <asm/io.h>
#include <asm/smp.h>

#include "msi.h"

static spinlock_t msi_lock = SPIN_LOCK_UNLOCKED;
static struct msi_desc* msi_desc[NR_IRQS] = { [0 ... NR_IRQS-1] = NULL };
static kmem_cache_t* msi_cachep;

static int pci_msi_enable = 1;
static int last_alloc_vector = 0;
static int nr_released_vectors = 0;
static int nr_reserved_vectors = NR_HP_RESERVED_VECTORS;
static int nr_msix_devices = 0;

#ifndef CONFIG_X86_IO_APIC
int vector_irq[NR_VECTORS] = { [0 ... NR_VECTORS - 1] = -1};
u8 irq_vector[NR_IRQ_VECTORS] = { FIRST_DEVICE_VECTOR , 0 };
#endif

static void msi_cache_ctor(void *p, kmem_cache_t *cache, unsigned long flags)
{
	memset(p, 0, NR_IRQS * sizeof(struct msi_desc));
}

static int msi_cache_init(void)
{
	msi_cachep = kmem_cache_create("msi_cache",
			NR_IRQS * sizeof(struct msi_desc),
		       	0, SLAB_HWCACHE_ALIGN, msi_cache_ctor, NULL);
	if (!msi_cachep)
		return -ENOMEM;

	return 0;
}

static void msi_set_mask_bit(unsigned int vector, int flag)
{
	struct msi_desc *entry;

	entry = (struct msi_desc *)msi_desc[vector];
	if (!entry || !entry->dev || !entry->mask_base)
		return;
	switch (entry->msi_attrib.type) {
	case PCI_CAP_ID_MSI:
	{
		int		pos;
		unsigned int	mask_bits;

		pos = entry->mask_base;
	        entry->dev->bus->ops->read(entry->dev->bus, entry->dev->devfn,
				pos, 4, &mask_bits);
		mask_bits &= ~(1);
		mask_bits |= flag;
	        entry->dev->bus->ops->write(entry->dev->bus, entry->dev->devfn,
				pos, 4, mask_bits);
		break;
	}
	case PCI_CAP_ID_MSIX:
	{
		int offset = entry->msi_attrib.entry_nr * PCI_MSIX_ENTRY_SIZE +
			PCI_MSIX_ENTRY_VECTOR_CTRL_OFFSET;
		writel(flag, entry->mask_base + offset);
		break;
	}
	default:
		break;
	}
}

#ifdef CONFIG_SMP
static void set_msi_affinity(unsigned int vector, cpumask_t cpu_mask)
{
	struct msi_desc *entry;
	struct msg_address address;

	entry = (struct msi_desc *)msi_desc[vector];
	if (!entry || !entry->dev)
		return;

	switch (entry->msi_attrib.type) {
	case PCI_CAP_ID_MSI:
	{
		int pos;

   		if (!(pos = pci_find_capability(entry->dev, PCI_CAP_ID_MSI)))
			return;

	        entry->dev->bus->ops->read(entry->dev->bus, entry->dev->devfn,
			msi_lower_address_reg(pos), 4,
			&address.lo_address.value);
		address.lo_address.value &= MSI_ADDRESS_DEST_ID_MASK;
		address.lo_address.value |= (cpu_mask_to_apicid(cpu_mask) <<
			MSI_TARGET_CPU_SHIFT);
		entry->msi_attrib.current_cpu = cpu_mask_to_apicid(cpu_mask);
		entry->dev->bus->ops->write(entry->dev->bus, entry->dev->devfn,
			msi_lower_address_reg(pos), 4,
			address.lo_address.value);
		break;
	}
	case PCI_CAP_ID_MSIX:
	{
		int offset = entry->msi_attrib.entry_nr * PCI_MSIX_ENTRY_SIZE +
			PCI_MSIX_ENTRY_LOWER_ADDR_OFFSET;

		address.lo_address.value = readl(entry->mask_base + offset);
		address.lo_address.value &= MSI_ADDRESS_DEST_ID_MASK;
		address.lo_address.value |= (cpu_mask_to_apicid(cpu_mask) <<
			MSI_TARGET_CPU_SHIFT);
		entry->msi_attrib.current_cpu = cpu_mask_to_apicid(cpu_mask);
		writel(address.lo_address.value, entry->mask_base + offset);
		break;
	}
	default:
		break;
	}
}

#ifdef CONFIG_IRQBALANCE
static inline void move_msi(int vector)
{
	if (!cpus_empty(pending_irq_balance_cpumask[vector])) {
		set_msi_affinity(vector, pending_irq_balance_cpumask[vector]);
		cpus_clear(pending_irq_balance_cpumask[vector]);
	}
}
#endif /* CONFIG_IRQBALANCE */
#endif /* CONFIG_SMP */

static void mask_MSI_irq(unsigned int vector)
{
	msi_set_mask_bit(vector, 1);
}

static void unmask_MSI_irq(unsigned int vector)
{
	msi_set_mask_bit(vector, 0);
}

static unsigned int startup_msi_irq_wo_maskbit(unsigned int vector)
{
	return 0;	/* never anything pending */
}

static void pci_disable_msi(unsigned int vector);
static void shutdown_msi_irq(unsigned int vector)
{
	pci_disable_msi(vector);
}

#define shutdown_msi_irq_wo_maskbit	shutdown_msi_irq
static void enable_msi_irq_wo_maskbit(unsigned int vector) {}
static void disable_msi_irq_wo_maskbit(unsigned int vector) {}
static void ack_msi_irq_wo_maskbit(unsigned int vector) {}
static void end_msi_irq_wo_maskbit(unsigned int vector)
{
	move_msi(vector);
	ack_APIC_irq();
}

static unsigned int startup_msi_irq_w_maskbit(unsigned int vector)
{
	unmask_MSI_irq(vector);
	return 0;	/* never anything pending */
}

#define shutdown_msi_irq_w_maskbit	shutdown_msi_irq
#define enable_msi_irq_w_maskbit	unmask_MSI_irq
#define disable_msi_irq_w_maskbit	mask_MSI_irq
#define ack_msi_irq_w_maskbit		mask_MSI_irq

static void end_msi_irq_w_maskbit(unsigned int vector)
{
	move_msi(vector);
	unmask_MSI_irq(vector);
	ack_APIC_irq();
}

/*
 * Interrupt Type for MSI-X PCI/PCI-X/PCI-Express Devices,
 * which implement the MSI-X Capability Structure.
 */
static struct hw_interrupt_type msix_irq_type = {
	.typename	= "PCI MSI-X",
	.startup	= startup_msi_irq_w_maskbit,
	.shutdown	= shutdown_msi_irq_w_maskbit,
	.enable		= enable_msi_irq_w_maskbit,
	.disable	= disable_msi_irq_w_maskbit,
	.ack		= ack_msi_irq_w_maskbit,
	.end		= end_msi_irq_w_maskbit,
	.set_affinity	= set_msi_irq_affinity
};

/*
 * Interrupt Type for MSI PCI/PCI-X/PCI-Express Devices,
 * which implement the MSI Capability Structure with
 * Mask-and-Pending Bits.
 */
static struct hw_interrupt_type msi_irq_w_maskbit_type = {
	.typename	= "PCI MSI",
	.startup	= startup_msi_irq_w_maskbit,
	.shutdown	= shutdown_msi_irq_w_maskbit,
	.enable		= enable_msi_irq_w_maskbit,
	.disable	= disable_msi_irq_w_maskbit,
	.ack		= ack_msi_irq_w_maskbit,
	.end		= end_msi_irq_w_maskbit,
	.set_affinity	= set_msi_irq_affinity
};

/*
 * Interrupt Type for MSI PCI/PCI-X/PCI-Express Devices,
 * which implement the MSI Capability Structure without
 * Mask-and-Pending Bits.
 */
static struct hw_interrupt_type msi_irq_wo_maskbit_type = {
	.typename	= "PCI MSI",
	.startup	= startup_msi_irq_wo_maskbit,
	.shutdown	= shutdown_msi_irq_wo_maskbit,
	.enable		= enable_msi_irq_wo_maskbit,
	.disable	= disable_msi_irq_wo_maskbit,
	.ack		= ack_msi_irq_wo_maskbit,
	.end		= end_msi_irq_wo_maskbit,
	.set_affinity	= set_msi_irq_affinity
};

static void msi_data_init(struct msg_data *msi_data,
			  unsigned int vector)
{
	memset(msi_data, 0, sizeof(struct msg_data));
	msi_data->vector = (u8)vector;
	msi_data->delivery_mode = MSI_DELIVERY_MODE;
	msi_data->level = MSI_LEVEL_MODE;
	msi_data->trigger = MSI_TRIGGER_MODE;
}

static void msi_address_init(struct msg_address *msi_address)
{
	unsigned int	dest_id;

	memset(msi_address, 0, sizeof(struct msg_address));
	msi_address->hi_address = (u32)0;
	dest_id = (MSI_ADDRESS_HEADER << MSI_ADDRESS_HEADER_SHIFT);
	msi_address->lo_address.u.dest_mode = MSI_DEST_MODE;
	msi_address->lo_address.u.redirection_hint = MSI_REDIRECTION_HINT_MODE;
	msi_address->lo_address.u.dest_id = dest_id;
	msi_address->lo_address.value |= (MSI_TARGET_CPU << MSI_TARGET_CPU_SHIFT);
}

static int assign_msi_vector(void)
{
	static int new_vector_avail = 1;
	int vector;
	unsigned long flags;

	/*
	 * msi_lock is provided to ensure that successful allocation of MSI
	 * vector is assigned unique among drivers.
	 */
	spin_lock_irqsave(&msi_lock, flags);

	if (!new_vector_avail) {
		/*
	 	 * vector_irq[] = -1 indicates that this specific vector is:
	 	 * - assigned for MSI (since MSI have no associated IRQ) or
	 	 * - assigned for legacy if less than 16, or
	 	 * - having no corresponding 1:1 vector-to-IOxAPIC IRQ mapping
	 	 * vector_irq[] = 0 indicates that this vector, previously
		 * assigned for MSI, is freed by hotplug removed operations.
		 * This vector will be reused for any subsequent hotplug added
		 * operations.
	 	 * vector_irq[] > 0 indicates that this vector is assigned for
		 * IOxAPIC IRQs. This vector and its value provides a 1-to-1
		 * vector-to-IOxAPIC IRQ mapping.
	 	 */
		for (vector = FIRST_DEVICE_VECTOR; vector < NR_IRQS; vector++) {
			if (vector_irq[vector] != 0)
				continue;
			vector_irq[vector] = -1;
			nr_released_vectors--;
			spin_unlock_irqrestore(&msi_lock, flags);
			return vector;
		}
		spin_unlock_irqrestore(&msi_lock, flags);
		return -EBUSY;
	}
	vector = assign_irq_vector(AUTO_ASSIGN);
	last_alloc_vector = vector;
	if (vector  == LAST_DEVICE_VECTOR)
		new_vector_avail = 0;

	spin_unlock_irqrestore(&msi_lock, flags);
	return vector;
}

static int get_new_vector(void)
{
	int vector;

	if ((vector = assign_msi_vector()) > 0)
		set_intr_gate(vector, interrupt[vector]);

	return vector;
}

static int msi_init(void)
{
	static int status = -ENOMEM;

	if (!status)
		return status;

	if ((status = msi_cache_init()) < 0) {
		pci_msi_enable = 0;
		printk(KERN_INFO "WARNING: MSI INIT FAILURE\n");
		return status;
	}
	printk(KERN_INFO "MSI INIT SUCCESS\n");

	return status;
}

static int get_msi_vector(struct pci_dev *dev)
{
	return get_new_vector();
}

static struct msi_desc* alloc_msi_entry(void)
{
	struct msi_desc *entry;

	entry = (struct msi_desc*) kmem_cache_alloc(msi_cachep, SLAB_KERNEL);
	if (!entry)
		return NULL;

	memset(entry, 0, sizeof(struct msi_desc));
	entry->link.tail = entry->link.head = 0;	/* single message */
	entry->dev = NULL;

	return entry;
}

static void attach_msi_entry(struct msi_desc *entry, int vector)
{
	unsigned long flags;

	spin_lock_irqsave(&msi_lock, flags);
	msi_desc[vector] = entry;
	spin_unlock_irqrestore(&msi_lock, flags);
}

static void irq_handler_init(int cap_id, int pos, int mask)
{
	spin_lock(&irq_desc[pos].lock);
	if (cap_id == PCI_CAP_ID_MSIX)
		irq_desc[pos].handler = &msix_irq_type;
	else {
		if (!mask)
			irq_desc[pos].handler = &msi_irq_wo_maskbit_type;
		else
			irq_desc[pos].handler = &msi_irq_w_maskbit_type;
	}
	spin_unlock(&irq_desc[pos].lock);
}

static void enable_msi_mode(struct pci_dev *dev, int pos, int type)
{
	u32 control;

	dev->bus->ops->read(dev->bus, dev->devfn,
		msi_control_reg(pos), 2, &control);
	if (type == PCI_CAP_ID_MSI) {
		/* Set enabled bits to single MSI & enable MSI_enable bit */
		msi_enable(control, 1);
	        dev->bus->ops->write(dev->bus, dev->devfn,
			msi_control_reg(pos), 2, control);
	} else {
		msix_enable(control);
	        dev->bus->ops->write(dev->bus, dev->devfn,
			msi_control_reg(pos), 2, control);
	}
    	if (pci_find_capability(dev, PCI_CAP_ID_EXP)) {
		/* PCI Express Endpoint device detected */
		u32 cmd;
	        dev->bus->ops->read(dev->bus, dev->devfn, PCI_COMMAND, 2, &cmd);
		cmd |= PCI_COMMAND_INTX_DISABLE;
	        dev->bus->ops->write(dev->bus, dev->devfn, PCI_COMMAND, 2, cmd);
	}
}

static void disable_msi_mode(struct pci_dev *dev, int pos, int type)
{
	u32 control;

	dev->bus->ops->read(dev->bus, dev->devfn,
		msi_control_reg(pos), 2, &control);
	if (type == PCI_CAP_ID_MSI) {
		/* Set enabled bits to single MSI & enable MSI_enable bit */
		msi_disable(control);
	        dev->bus->ops->write(dev->bus, dev->devfn,
			msi_control_reg(pos), 2, control);
	} else {
		msix_disable(control);
	        dev->bus->ops->write(dev->bus, dev->devfn,
			msi_control_reg(pos), 2, control);
	}
    	if (pci_find_capability(dev, PCI_CAP_ID_EXP)) {
		/* PCI Express Endpoint device detected */
		u32 cmd;
	        dev->bus->ops->read(dev->bus, dev->devfn, PCI_COMMAND, 2, &cmd);
		cmd &= ~PCI_COMMAND_INTX_DISABLE;
	        dev->bus->ops->write(dev->bus, dev->devfn, PCI_COMMAND, 2, cmd);
	}
}

static int msi_lookup_vector(struct pci_dev *dev)
{
	int vector;
	unsigned long flags;

	spin_lock_irqsave(&msi_lock, flags);
	for (vector = FIRST_DEVICE_VECTOR; vector < NR_IRQS; vector++) {
		if (!msi_desc[vector] || msi_desc[vector]->dev != dev ||
			msi_desc[vector]->msi_attrib.entry_nr ||
			msi_desc[vector]->msi_attrib.default_vector != dev->irq)
			continue;	/* not entry 0, skip */
		spin_unlock_irqrestore(&msi_lock, flags);
		/* This pre-assigned entry-0 MSI vector for this device
		   already exits. Override dev->irq with this vector */
		dev->irq = vector;
		return 0;
	}
	spin_unlock_irqrestore(&msi_lock, flags);

	return -EACCES;
}

void pci_scan_msi_device(struct pci_dev *dev)
{
	if (!dev)
		return;

   	if (pci_find_capability(dev, PCI_CAP_ID_MSIX) > 0) {
		nr_reserved_vectors++;
		nr_msix_devices++;
	} else if (pci_find_capability(dev, PCI_CAP_ID_MSI) > 0)
		nr_reserved_vectors++;
}

/**
 * msi_capability_init - configure device's MSI capability structure
 * @dev: pointer to the pci_dev data structure of MSI device function
 *
 * Setup the MSI capability structure of device funtion with a single
 * MSI vector, regardless of device function is capable of handling
 * multiple messages. A return of zero indicates the successful setup
 * of an entry zero with the new MSI vector or non-zero for otherwise.
 **/
static int msi_capability_init(struct pci_dev *dev)
{
	struct msi_desc *entry;
	struct msg_address address;
	struct msg_data data;
	int pos, vector;
	u32 control;

   	pos = pci_find_capability(dev, PCI_CAP_ID_MSI);
	if (!pos)
		return -EINVAL;

	dev->bus->ops->read(dev->bus, dev->devfn, msi_control_reg(pos),
		2, &control);
	if (control & PCI_MSI_FLAGS_ENABLE)
		return 0;

	if (!msi_lookup_vector(dev)) {
		/* Lookup Sucess */
		enable_msi_mode(dev, pos, PCI_CAP_ID_MSI);
		return 0;
	}
	/* MSI Entry Initialization */
	if (!(entry = alloc_msi_entry()))
		return -ENOMEM;

	if ((vector = get_msi_vector(dev)) < 0) {
		kmem_cache_free(msi_cachep, entry);
		return -EBUSY;
	}
	entry->msi_attrib.type = PCI_CAP_ID_MSI;
	entry->msi_attrib.entry_nr = 0;
	entry->msi_attrib.maskbit = is_mask_bit_support(control);
	entry->msi_attrib.default_vector = dev->irq;
	dev->irq = vector;	/* save default pre-assigned ioapic vector */
	entry->dev = dev;
	if (is_mask_bit_support(control)) {
		entry->mask_base = msi_mask_bits_reg(pos,
				is_64bit_address(control));
	}
	/* Replace with MSI handler */
	irq_handler_init(PCI_CAP_ID_MSI, vector, entry->msi_attrib.maskbit);
	/* Configure MSI capability structure */
	msi_address_init(&address);
	msi_data_init(&data, vector);
	entry->msi_attrib.current_cpu = ((address.lo_address.u.dest_id >>
				MSI_TARGET_CPU_SHIFT) & MSI_TARGET_CPU_MASK);
	dev->bus->ops->write(dev->bus, dev->devfn, msi_lower_address_reg(pos),
				4, address.lo_address.value);
	if (is_64bit_address(control)) {
		dev->bus->ops->write(dev->bus, dev->devfn,
			msi_upper_address_reg(pos), 4, address.hi_address);
		dev->bus->ops->write(dev->bus, dev->devfn,
			msi_data_reg(pos, 1), 2, *((u32*)&data));
	} else
		dev->bus->ops->write(dev->bus, dev->devfn,
			msi_data_reg(pos, 0), 2, *((u32*)&data));
	if (entry->msi_attrib.maskbit) {
		unsigned int maskbits, temp;
		/* All MSIs are unmasked by default, Mask them all */
	        dev->bus->ops->read(dev->bus, dev->devfn,
			msi_mask_bits_reg(pos, is_64bit_address(control)), 4,
			&maskbits);
		temp = (1 << multi_msi_capable(control));
		temp = ((temp - 1) & ~temp);
		maskbits |= temp;
		dev->bus->ops->write(dev->bus, dev->devfn,
			msi_mask_bits_reg(pos, is_64bit_address(control)), 4,
			maskbits);
	}
	attach_msi_entry(entry, vector);
	/* Set MSI enabled bits	 */
	enable_msi_mode(dev, pos, PCI_CAP_ID_MSI);

	return 0;
}

/**
 * msix_capability_init - configure device's MSI-X capability
 * @dev: pointer to the pci_dev data structure of MSI-X device function
 *
 * Setup the MSI-X capability structure of device funtion with a
 * single MSI-X vector. A return of zero indicates the successful setup
 * of an entry zero with the new MSI-X vector or non-zero for otherwise.
 * To request for additional MSI-X vectors, the device drivers are
 * required to utilize the following supported APIs:
 * 1) msi_alloc_vectors(...) for requesting one or more MSI-X vectors
 * 2) msi_free_vectors(...) for releasing one or more MSI-X vectors
 *    back to PCI subsystem before calling free_irq(...)
 **/
static int msix_capability_init(struct pci_dev	*dev)
{
	struct msi_desc *entry;
	struct msg_address address;
	struct msg_data data;
	int vector = 0, pos, dev_msi_cap;
	u32 phys_addr, table_offset;
	u32 control;
	u8 bir;
	void *base;

   	pos = pci_find_capability(dev, PCI_CAP_ID_MSIX);
	if (!pos)
		return -EINVAL;

	/* Request & Map MSI-X table region */
	dev->bus->ops->read(dev->bus, dev->devfn, msi_control_reg(pos), 2,
		&control);
	if (control & PCI_MSIX_FLAGS_ENABLE)
		return 0;

	if (!msi_lookup_vector(dev)) {
		/* Lookup Sucess */
		enable_msi_mode(dev, pos, PCI_CAP_ID_MSIX);
		return 0;
	}

	dev_msi_cap = multi_msix_capable(control);
	dev->bus->ops->read(dev->bus, dev->devfn,
		msix_table_offset_reg(pos), 4, &table_offset);
	bir = (u8)(table_offset & PCI_MSIX_FLAGS_BIRMASK);
	phys_addr = pci_resource_start (dev, bir);
	phys_addr += (u32)(table_offset & ~PCI_MSIX_FLAGS_BIRMASK);
	if (!request_mem_region(phys_addr,
		dev_msi_cap * PCI_MSIX_ENTRY_SIZE,
		"MSI-X iomap Failure"))
		return -ENOMEM;
	base = ioremap_nocache(phys_addr, dev_msi_cap * PCI_MSIX_ENTRY_SIZE);
	if (base == NULL)
		goto free_region;
	/* MSI Entry Initialization */
	entry = alloc_msi_entry();
	if (!entry)
		goto free_iomap;
	if ((vector = get_msi_vector(dev)) < 0)
		goto free_entry;

	entry->msi_attrib.type = PCI_CAP_ID_MSIX;
	entry->msi_attrib.entry_nr = 0;
	entry->msi_attrib.maskbit = 1;
	entry->msi_attrib.default_vector = dev->irq;
	dev->irq = vector;	/* save default pre-assigned ioapic vector */
	entry->dev = dev;
	entry->mask_base = (unsigned long)base;
	/* Replace with MSI handler */
	irq_handler_init(PCI_CAP_ID_MSIX, vector, 1);
	/* Configure MSI-X capability structure */
	msi_address_init(&address);
	msi_data_init(&data, vector);
	entry->msi_attrib.current_cpu = ((address.lo_address.u.dest_id >>
				MSI_TARGET_CPU_SHIFT) & MSI_TARGET_CPU_MASK);
	writel(address.lo_address.value, base + PCI_MSIX_ENTRY_LOWER_ADDR_OFFSET);
	writel(address.hi_address, base + PCI_MSIX_ENTRY_UPPER_ADDR_OFFSET);
	writel(*(u32*)&data, base + PCI_MSIX_ENTRY_DATA_OFFSET);
	/* Initialize all entries from 1 up to 0 */
	for (pos = 1; pos < dev_msi_cap; pos++) {
		writel(0, base + pos * PCI_MSIX_ENTRY_SIZE +
			PCI_MSIX_ENTRY_LOWER_ADDR_OFFSET);
		writel(0, base + pos * PCI_MSIX_ENTRY_SIZE +
			PCI_MSIX_ENTRY_UPPER_ADDR_OFFSET);
		writel(0, base + pos * PCI_MSIX_ENTRY_SIZE +
			PCI_MSIX_ENTRY_DATA_OFFSET);
	}
	attach_msi_entry(entry, vector);
	/* Set MSI enabled bits	 */
	enable_msi_mode(dev, pos, PCI_CAP_ID_MSIX);

	return 0;

free_entry:
	kmem_cache_free(msi_cachep, entry);
free_iomap:
	iounmap(base);
free_region:
	release_mem_region(phys_addr, dev_msi_cap * PCI_MSIX_ENTRY_SIZE);

	return ((vector < 0) ? -EBUSY : -ENOMEM);
}

/**
 * pci_enable_msi - configure device's MSI(X) capability structure
 * @dev: pointer to the pci_dev data structure of MSI(X) device function
 *
 * Setup the MSI/MSI-X capability structure of device function with
 * a single MSI(X) vector upon its software driver call to request for
 * MSI(X) mode enabled on its hardware device function. A return of zero
 * indicates the successful setup of an entry zero with the new MSI(X)
 * vector or non-zero for otherwise.
 **/
int pci_enable_msi(struct pci_dev* dev)
{
	int status = -EINVAL;

	if (!pci_msi_enable || !dev)
 		return status;

	if (msi_init() < 0)
		return -ENOMEM;

	if ((status = msix_capability_init(dev)) == -EINVAL)
		status = msi_capability_init(dev);
	if (!status)
		nr_reserved_vectors--;

	return status;
}

static int msi_free_vector(struct pci_dev* dev, int vector);
static void pci_disable_msi(unsigned int vector)
{
	int head, tail, type, default_vector;
	struct msi_desc *entry;
	struct pci_dev *dev;
	unsigned long flags;

	spin_lock_irqsave(&msi_lock, flags);
	entry = msi_desc[vector];
	if (!entry || !entry->dev) {
		spin_unlock_irqrestore(&msi_lock, flags);
		return;
	}
	dev = entry->dev;
	type = entry->msi_attrib.type;
	head = entry->link.head;
	tail = entry->link.tail;
	default_vector = entry->msi_attrib.default_vector;
	spin_unlock_irqrestore(&msi_lock, flags);

	disable_msi_mode(dev, pci_find_capability(dev, type), type);
	/* Restore dev->irq to its default pin-assertion vector */
	dev->irq = default_vector;
	if (type == PCI_CAP_ID_MSIX && head != tail) {
		/* Bad driver, which do not call msi_free_vectors before exit.
		   We must do a cleanup here */
		while (1) {
			spin_lock_irqsave(&msi_lock, flags);
			entry = msi_desc[vector];
			head = entry->link.head;
			tail = entry->link.tail;
			spin_unlock_irqrestore(&msi_lock, flags);
			if (tail == head)
				break;
			if (msi_free_vector(dev, entry->link.tail))
				break;
		}
	}
}

static int msi_alloc_vector(struct pci_dev* dev, int head)
{
	struct msi_desc *entry;
	struct msg_address address;
	struct msg_data data;
	int i, offset, pos, dev_msi_cap, vector;
	u32 low_address, control;
	unsigned long base = 0L;
	unsigned long flags;

	spin_lock_irqsave(&msi_lock, flags);
	entry = msi_desc[dev->irq];
	if (!entry) {
		spin_unlock_irqrestore(&msi_lock, flags);
		return -EINVAL;
	}
	base = entry->mask_base;
	spin_unlock_irqrestore(&msi_lock, flags);

   	pos = pci_find_capability(dev, PCI_CAP_ID_MSIX);
	dev->bus->ops->read(dev->bus, dev->devfn, msi_control_reg(pos),
		2, &control);
	dev_msi_cap = multi_msix_capable(control);
	for (i = 1; i < dev_msi_cap; i++) {
		if (!(low_address = readl(base + i * PCI_MSIX_ENTRY_SIZE)))
			 break;
	}
	if (i >= dev_msi_cap)
		return -EINVAL;

	/* MSI Entry Initialization */
	if (!(entry = alloc_msi_entry()))
		return -ENOMEM;

	if ((vector = get_new_vector()) < 0) {
		kmem_cache_free(msi_cachep, entry);
		return vector;
	}
	entry->msi_attrib.type = PCI_CAP_ID_MSIX;
	entry->msi_attrib.entry_nr = i;
	entry->msi_attrib.maskbit = 1;
	entry->dev = dev;
	entry->link.head = head;
	entry->mask_base = base;
	irq_handler_init(PCI_CAP_ID_MSIX, vector, 1);
	/* Configure MSI-X capability structure */
	msi_address_init(&address);
	msi_data_init(&data, vector);
	entry->msi_attrib.current_cpu = ((address.lo_address.u.dest_id >>
				MSI_TARGET_CPU_SHIFT) & MSI_TARGET_CPU_MASK);
	offset = entry->msi_attrib.entry_nr * PCI_MSIX_ENTRY_SIZE;
	writel(address.lo_address.value, base + offset +
		PCI_MSIX_ENTRY_LOWER_ADDR_OFFSET);
	writel(address.hi_address, base + offset +
		PCI_MSIX_ENTRY_UPPER_ADDR_OFFSET);
	writel(*(u32*)&data, base + offset + PCI_MSIX_ENTRY_DATA_OFFSET);
	writel(1, base + offset + PCI_MSIX_ENTRY_VECTOR_CTRL_OFFSET);
	attach_msi_entry(entry, vector);

	return vector;
}

static int msi_free_vector(struct pci_dev* dev, int vector)
{
	struct msi_desc *entry;
	int entry_nr, type;
	unsigned long base = 0L;
	unsigned long flags;

	spin_lock_irqsave(&msi_lock, flags);
	entry = msi_desc[vector];
	if (!entry || entry->dev != dev) {
		spin_unlock_irqrestore(&msi_lock, flags);
		return -EINVAL;
	}
	type = entry->msi_attrib.type;
	entry_nr = entry->msi_attrib.entry_nr;
	base = entry->mask_base;
	if (entry->link.tail != entry->link.head) {
		msi_desc[entry->link.head]->link.tail = entry->link.tail;
		if (entry->link.tail)
			msi_desc[entry->link.tail]->link.head = entry->link.head;
	}
	entry->dev = NULL;
	vector_irq[vector] = 0;
	nr_released_vectors++;
	msi_desc[vector] = NULL;
	spin_unlock_irqrestore(&msi_lock, flags);

	kmem_cache_free(msi_cachep, entry);
	if (type == PCI_CAP_ID_MSIX) {
		int offset;

		offset = entry_nr * PCI_MSIX_ENTRY_SIZE;
		writel(1, base + offset + PCI_MSIX_ENTRY_VECTOR_CTRL_OFFSET);
		writel(0, base + offset + PCI_MSIX_ENTRY_LOWER_ADDR_OFFSET);
	}

	return 0;
}

/**
 * msi_alloc_vectors - allocate additional MSI-X vectors
 * @dev: pointer to the pci_dev data structure of MSI-X device function
 * @vector: pointer to an array of new allocated MSI-X vectors
 * @nvec: number of MSI-X vectors requested for allocation by device driver
 *
 * Allocate additional MSI-X vectors requested by device driver. A
 * return of zero indicates the successful setup of MSI-X capability
 * structure with new allocated MSI-X vectors or non-zero for otherwise.
 **/
int msi_alloc_vectors(struct pci_dev* dev, int *vector, int nvec)
{
	struct msi_desc *entry;
	int i, head, pos, vec, free_vectors, alloc_vectors;
	int *vectors = (int *)vector;
	u32 control;
	unsigned long flags;

	if (!pci_msi_enable || !dev)
 		return -EINVAL;

   	if (!(pos = pci_find_capability(dev, PCI_CAP_ID_MSIX)))
 		return -EINVAL;

	dev->bus->ops->read(dev->bus, dev->devfn, msi_control_reg(pos), 			2, &control);
	if (nvec > multi_msix_capable(control))
		return -EINVAL;

	spin_lock_irqsave(&msi_lock, flags);
	entry = msi_desc[dev->irq];
	if (!entry || entry->dev != dev ||		/* legal call */
	   entry->msi_attrib.type != PCI_CAP_ID_MSIX || /* must be MSI-X */
	   entry->link.head != entry->link.tail) {	/* already multi */
		spin_unlock_irqrestore(&msi_lock, flags);
		return -EINVAL;
	}
	/*
	 * msi_lock is provided to ensure that enough vectors resources are
	 * available before granting.
	 */
	free_vectors = pci_vector_resources(last_alloc_vector,
				nr_released_vectors);
	/* Ensure that each MSI/MSI-X device has one vector reserved by
	   default to avoid any MSI-X driver to take all available
 	   resources */
	free_vectors -= nr_reserved_vectors;
	/* Find the average of free vectors among MSI-X devices */
	if (nr_msix_devices > 0)
		free_vectors /= nr_msix_devices;
	spin_unlock_irqrestore(&msi_lock, flags);

	if (nvec > free_vectors)
		return -EBUSY;

	alloc_vectors = 0;
	head = dev->irq;
	for (i = 0; i < nvec; i++) {
		if ((vec = msi_alloc_vector(dev, head)) < 0)
			break;
		*(vectors + i) = vec;
		head = vec;
		alloc_vectors++;
	}
	if (alloc_vectors != nvec) {
		for (i = 0; i < alloc_vectors; i++) {
			vec = *(vectors + i);
			msi_free_vector(dev, vec);
		}
		spin_lock_irqsave(&msi_lock, flags);
		msi_desc[dev->irq]->link.tail = msi_desc[dev->irq]->link.head;
		spin_unlock_irqrestore(&msi_lock, flags);
		return -EBUSY;
	}
	if (nr_msix_devices > 0)
		nr_msix_devices--;

	return 0;
}

/**
 * msi_free_vectors - reclaim MSI-X vectors to unused state
 * @dev: pointer to the pci_dev data structure of MSI-X device function
 * @vector: pointer to an array of released MSI-X vectors
 * @nvec: number of MSI-X vectors requested for release by device driver
 *
 * Reclaim MSI-X vectors released by device driver to unused state,
 * which may be used later on. A return of zero indicates the
 * success or non-zero for otherwise. Device driver should call this
 * before calling function free_irq.
 **/
int msi_free_vectors(struct pci_dev* dev, int *vector, int nvec)
{
	struct msi_desc *entry;
	int i;
	unsigned long flags;

	if (!pci_msi_enable)
 		return -EINVAL;

	spin_lock_irqsave(&msi_lock, flags);
	entry = msi_desc[dev->irq];
	if (!entry || entry->dev != dev ||
	   	entry->msi_attrib.type != PCI_CAP_ID_MSIX ||
		entry->link.head == entry->link.tail) {	/* Nothing to free */
		spin_unlock_irqrestore(&msi_lock, flags);
		return -EINVAL;
	}
	spin_unlock_irqrestore(&msi_lock, flags);

	for (i = 0; i < nvec; i++) {
		if (*(vector + i) == dev->irq)
			continue;/* Don't free entry 0 if mistaken by driver */
		msi_free_vector(dev, *(vector + i));
	}

	return 0;
}

/**
 * msi_remove_pci_irq_vectors - reclaim MSI(X) vectors to unused state
 * @dev: pointer to the pci_dev data structure of MSI(X) device function
 *
 * Being called during hotplug remove, from which the device funciton
 * is hot-removed. All previous assigned MSI/MSI-X vectors, if
 * allocated for this device function, are reclaimed to unused state,
 * which may be used later on.
 **/
void msi_remove_pci_irq_vectors(struct pci_dev* dev)
{
	struct msi_desc *entry;
	int type, temp;
	unsigned long flags;

	if (!pci_msi_enable || !dev)
 		return;

   	if (!pci_find_capability(dev, PCI_CAP_ID_MSI)) {
   		if (!pci_find_capability(dev, PCI_CAP_ID_MSIX))
			return;
	}
	temp = dev->irq;
	if (msi_lookup_vector(dev))
		return;

	spin_lock_irqsave(&msi_lock, flags);
	entry = msi_desc[dev->irq];
	if (!entry || entry->dev != dev) {
		spin_unlock_irqrestore(&msi_lock, flags);
		return;
	}
	type = entry->msi_attrib.type;
	spin_unlock_irqrestore(&msi_lock, flags);

	msi_free_vector(dev, dev->irq);
	if (type == PCI_CAP_ID_MSIX) {
		int i, pos, dev_msi_cap;
		u32 phys_addr, table_offset;
		u32 control;
		u8 bir;

   		pos = pci_find_capability(dev, PCI_CAP_ID_MSIX);
		dev->bus->ops->read(dev->bus, dev->devfn, msi_control_reg(pos), 			2, &control);
		dev_msi_cap = multi_msix_capable(control);
		dev->bus->ops->read(dev->bus, dev->devfn,
			msix_table_offset_reg(pos), 4, &table_offset);
		bir = (u8)(table_offset & PCI_MSIX_FLAGS_BIRMASK);
		phys_addr = pci_resource_start (dev, bir);
		phys_addr += (u32)(table_offset & ~PCI_MSIX_FLAGS_BIRMASK);
		for (i = FIRST_DEVICE_VECTOR; i < NR_IRQS; i++) {
			spin_lock_irqsave(&msi_lock, flags);
			if (!msi_desc[i] || msi_desc[i]->dev != dev) {
				spin_unlock_irqrestore(&msi_lock, flags);
				continue;
			}
			spin_unlock_irqrestore(&msi_lock, flags);
			msi_free_vector(dev, i);
		}
		writel(1, entry->mask_base + PCI_MSIX_ENTRY_VECTOR_CTRL_OFFSET);
		iounmap((void*)entry->mask_base);
		release_mem_region(phys_addr, dev_msi_cap * PCI_MSIX_ENTRY_SIZE);
	}
	dev->irq = temp;
	nr_reserved_vectors++;
}

EXPORT_SYMBOL(pci_enable_msi);
EXPORT_SYMBOL(msi_alloc_vectors);
EXPORT_SYMBOL(msi_free_vectors);
