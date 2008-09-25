/*
 * File:	msi.c
 * Purpose:	PCI Message Signaled Interrupt (MSI)
 *
 * Copyright (C) 2003-2004 Intel
 * Copyright (C) Tom Long Nguyen (tom.l.nguyen@intel.com)
 */

#include <linux/err.h>
#include <linux/mm.h>
#include <linux/irq.h>
#include <linux/interrupt.h>
#include <linux/init.h>
#include <linux/ioport.h>
#include <linux/pci.h>
#include <linux/proc_fs.h>
#include <linux/msi.h>
#include <linux/smp.h>

#include <xen/evtchn.h>

#include <asm/errno.h>
#include <asm/io.h>

#include "pci.h"
#include "msi.h"

static int pci_msi_enable = 1;

static LIST_HEAD(msi_dev_head);
DEFINE_SPINLOCK(msi_dev_lock);

struct msi_dev_list {
	struct pci_dev *dev;
	struct list_head list;
	spinlock_t pirq_list_lock;
	struct list_head pirq_list_head;
};

struct msi_pirq_entry {
	struct list_head list;
	int pirq;
	int entry_nr;
};

/* Arch hooks */

int __attribute__ ((weak))
arch_msi_check_device(struct pci_dev *dev, int nvec, int type)
{
	return 0;
}

#ifndef CONFIG_XEN
int __attribute__ ((weak))
arch_setup_msi_irq(struct pci_dev *dev, struct msi_desc *entry)
{
	return 0;
}

int __attribute__ ((weak))
arch_setup_msi_irqs(struct pci_dev *dev, int nvec, int type)
{
	struct msi_desc *entry;
	int ret;

	list_for_each_entry(entry, &dev->msi_list, list) {
		ret = arch_setup_msi_irq(dev, entry);
		if (ret)
			return ret;
	}

	return 0;
}

void __attribute__ ((weak)) arch_teardown_msi_irq(unsigned int irq)
{
	return;
}

void __attribute__ ((weak))
arch_teardown_msi_irqs(struct pci_dev *dev)
{
	struct msi_desc *entry;

	list_for_each_entry(entry, &dev->msi_list, list) {
		if (entry->irq != 0)
			arch_teardown_msi_irq(entry->irq);
	}
}
#endif

static void __msi_set_enable(struct pci_dev *dev, int pos, int enable)
{
	u16 control;

	if (pos) {
		pci_read_config_word(dev, pos + PCI_MSI_FLAGS, &control);
		control &= ~PCI_MSI_FLAGS_ENABLE;
		if (enable)
			control |= PCI_MSI_FLAGS_ENABLE;
		pci_write_config_word(dev, pos + PCI_MSI_FLAGS, control);
	}
}

static void msi_set_enable(struct pci_dev *dev, int enable)
{
	__msi_set_enable(dev, pci_find_capability(dev, PCI_CAP_ID_MSI), enable);
}

static void msix_set_enable(struct pci_dev *dev, int enable)
{
	int pos;
	u16 control;

	pos = pci_find_capability(dev, PCI_CAP_ID_MSIX);
	if (pos) {
		pci_read_config_word(dev, pos + PCI_MSIX_FLAGS, &control);
		control &= ~PCI_MSIX_FLAGS_ENABLE;
		if (enable)
			control |= PCI_MSIX_FLAGS_ENABLE;
		pci_write_config_word(dev, pos + PCI_MSIX_FLAGS, control);
	}
}

static struct msi_dev_list *get_msi_dev_pirq_list(struct pci_dev *dev)
{
	struct msi_dev_list *msi_dev_list, *ret = NULL;
	unsigned long flags;

	spin_lock_irqsave(&msi_dev_lock, flags);

	list_for_each_entry(msi_dev_list, &msi_dev_head, list)
		if ( msi_dev_list->dev == dev )
			ret = msi_dev_list;

	if ( ret ) {
		spin_unlock_irqrestore(&msi_dev_lock, flags);
		return ret;
	}

	/* Has not allocate msi_dev until now. */
	ret = kmalloc(sizeof(struct msi_dev_list), GFP_ATOMIC);

	/* Failed to allocate msi_dev structure */
	if ( !ret ) {
		spin_unlock_irqrestore(&msi_dev_lock, flags);
		return NULL;
	}

	spin_lock_init(&ret->pirq_list_lock);
	INIT_LIST_HEAD(&ret->pirq_list_head);
	list_add_tail(&ret->list, &msi_dev_head);
	spin_unlock_irqrestore(&msi_dev_lock, flags);
	return ret;
}

static int attach_pirq_entry(int pirq, int entry_nr,
                             struct msi_dev_list *msi_dev_entry)
{
	struct msi_pirq_entry *entry = kmalloc(sizeof(*entry), GFP_ATOMIC);
	unsigned long flags;

	if (!entry)
		return -ENOMEM;
	entry->pirq = pirq;
	entry->entry_nr = entry_nr;
	spin_lock_irqsave(&msi_dev_entry->pirq_list_lock, flags);
	list_add_tail(&entry->list, &msi_dev_entry->pirq_list_head);
	spin_unlock_irqrestore(&msi_dev_entry->pirq_list_lock, flags);
	return 0;
}

static void detach_pirq_entry(int entry_nr,
 							struct msi_dev_list *msi_dev_entry)
{
	unsigned long flags;
	struct msi_pirq_entry *pirq_entry;

	list_for_each_entry(pirq_entry, &msi_dev_entry->pirq_list_head, list) {
		if (pirq_entry->entry_nr == entry_nr) {
			spin_lock_irqsave(&msi_dev_entry->pirq_list_lock, flags);
			list_del(&pirq_entry->list);
			spin_unlock_irqrestore(&msi_dev_entry->pirq_list_lock, flags);
			kfree(pirq_entry);
			return;
		}
	}
}

/*
 * pciback will provide device's owner
 */
static int (*get_owner)(struct pci_dev *dev);

int register_msi_get_owner(int (*func)(struct pci_dev *dev))
{
	if (get_owner) {
		printk(KERN_WARNING "register msi_get_owner again\n");
		return -EEXIST;
	}
	get_owner = func;
	return 0;
}
EXPORT_SYMBOL(register_msi_get_owner);

int unregister_msi_get_owner(int (*func)(struct pci_dev *dev))
{
	if (get_owner != func)
		return -EINVAL;
	get_owner = NULL;
	return 0;
}
EXPORT_SYMBOL(unregister_msi_get_owner);

static int msi_get_dev_owner(struct pci_dev *dev)
{
	int owner;

	BUG_ON(!is_initial_xendomain());
	if (get_owner && (owner = get_owner(dev)) >= 0) {
		printk(KERN_INFO "get owner for dev %x get %x \n",
		       dev->devfn, owner);
		return owner;
	}

	return DOMID_SELF;
}

static int msi_unmap_pirq(struct pci_dev *dev, int pirq)
{
	struct physdev_unmap_pirq unmap;
	int rc;

	unmap.domid = msi_get_dev_owner(dev);
	unmap.pirq = evtchn_get_xen_pirq(pirq);

	if ((rc = HYPERVISOR_physdev_op(PHYSDEVOP_unmap_pirq, &unmap)))
		printk(KERN_WARNING "unmap irq %x failed\n", pirq);

	if (rc < 0)
		return rc;

	evtchn_map_pirq(pirq, 0);
	return 0;
}

static u64 find_table_base(struct pci_dev *dev, int pos)
{
	u8 bar;
	u32 reg;
	unsigned long flags;

 	pci_read_config_dword(dev, msix_table_offset_reg(pos), &reg);
	bar = reg & PCI_MSIX_FLAGS_BIRMASK;

	flags = pci_resource_flags(dev, bar);
	if (flags & (IORESOURCE_DISABLED | IORESOURCE_UNSET | IORESOURCE_BUSY))
		return 0;

	return pci_resource_start(dev, bar);
}

/*
 * Protected by msi_lock
 */
static int msi_map_pirq_to_vector(struct pci_dev *dev, int pirq,
				  int entry_nr, u64 table_base)
{
	struct physdev_map_pirq map_irq;
	int rc;
	domid_t domid = DOMID_SELF;

	domid = msi_get_dev_owner(dev);

	map_irq.domid = domid;
	map_irq.type = MAP_PIRQ_TYPE_MSI;
	map_irq.index = -1;
	map_irq.pirq = pirq < 0 ? -1 : evtchn_get_xen_pirq(pirq);
	map_irq.bus = dev->bus->number;
	map_irq.devfn = dev->devfn;
	map_irq.entry_nr = entry_nr;
	map_irq.table_base = table_base;

	if ((rc = HYPERVISOR_physdev_op(PHYSDEVOP_map_pirq, &map_irq)))
		printk(KERN_WARNING "map irq failed\n");

	if (rc < 0)
		return rc;
	/* This happens when MSI support is not enabled in Xen. */
	if (rc == 0 && map_irq.pirq < 0)
		return -ENOSYS;

	BUG_ON(map_irq.pirq <= 0);
	return evtchn_map_pirq(pirq, map_irq.pirq);
}

static int msi_map_vector(struct pci_dev *dev, int entry_nr, u64 table_base)
{
	return msi_map_pirq_to_vector(dev, -1, entry_nr, table_base);
}

static void pci_intx_for_msi(struct pci_dev *dev, int enable)
{
	if (!(dev->dev_flags & PCI_DEV_FLAGS_MSI_INTX_DISABLE_BUG))
		pci_intx(dev, enable);
}

static void __pci_restore_msi_state(struct pci_dev *dev)
{
	int pirq;

	if (!dev->msi_enabled)
		return;

	pirq = msi_map_pirq_to_vector(dev, dev->irq, 0, 0);
	if (pirq < 0)
		return;

	pci_intx_for_msi(dev, 0);
	msi_set_enable(dev, 0);
}

static void __pci_restore_msix_state(struct pci_dev *dev)
{
	int pos;
	unsigned long flags;
	u64 table_base;
	struct msi_dev_list *msi_dev_entry;
	struct msi_pirq_entry *pirq_entry, *tmp;

	pos = pci_find_capability(dev, PCI_CAP_ID_MSIX);
	if (pos <= 0)
		return;

	if (!dev->msix_enabled)
		return;

	msi_dev_entry = get_msi_dev_pirq_list(dev);
	table_base = find_table_base(dev, pos);
	if (!table_base)
		return;

	spin_lock_irqsave(&msi_dev_entry->pirq_list_lock, flags);
	list_for_each_entry_safe(pirq_entry, tmp,
				 &msi_dev_entry->pirq_list_head, list) {
		int rc = msi_map_pirq_to_vector(dev, pirq_entry->pirq,
						pirq_entry->entry_nr, table_base);
		if (rc < 0)
			printk(KERN_WARNING
			       "%s: re-mapping irq #%d (pirq%d) failed: %d\n",
			       pci_name(dev), pirq_entry->entry_nr,
			       pirq_entry->pirq, rc);
	}
	spin_unlock_irqrestore(&msi_dev_entry->pirq_list_lock, flags);

	pci_intx_for_msi(dev, 0);
	msix_set_enable(dev, 0);
}

void pci_restore_msi_state(struct pci_dev *dev)
{
	__pci_restore_msi_state(dev);
	__pci_restore_msix_state(dev);
}
EXPORT_SYMBOL_GPL(pci_restore_msi_state);

/**
 * msi_capability_init - configure device's MSI capability structure
 * @dev: pointer to the pci_dev data structure of MSI device function
 *
 * Setup the MSI capability structure of device function with a single
 * MSI irq, regardless of device function is capable of handling
 * multiple messages. A return of zero indicates the successful setup
 * of an entry zero with the new MSI irq or non-zero for otherwise.
 **/
static int msi_capability_init(struct pci_dev *dev)
{
	int pos, pirq;
	u16 control;

	msi_set_enable(dev, 0);	/* Ensure msi is disabled as I set it up */

	pos = pci_find_capability(dev, PCI_CAP_ID_MSI);
	pci_read_config_word(dev, msi_control_reg(pos), &control);

	pirq = msi_map_vector(dev, 0, 0);
	if (pirq < 0)
		return -EBUSY;

	/* Set MSI enabled bits	 */
	pci_intx_for_msi(dev, 0);
	msi_set_enable(dev, 1);
	dev->msi_enabled = 1;

	dev->irq = pirq;
	return 0;
}

/**
 * msix_capability_init - configure device's MSI-X capability
 * @dev: pointer to the pci_dev data structure of MSI-X device function
 * @entries: pointer to an array of struct msix_entry entries
 * @nvec: number of @entries
 *
 * Setup the MSI-X capability structure of device function with a
 * single MSI-X irq. A return of zero indicates the successful setup of
 * requested MSI-X entries with allocated irqs or non-zero for otherwise.
 **/
static int msix_capability_init(struct pci_dev *dev,
				struct msix_entry *entries, int nvec)
{
	u64 table_base;
	int pirq, i, j, mapped, pos;
	struct msi_dev_list *msi_dev_entry = get_msi_dev_pirq_list(dev);
	struct msi_pirq_entry *pirq_entry;

	if (!msi_dev_entry)
		return -ENOMEM;

	msix_set_enable(dev, 0);/* Ensure msix is disabled as I set it up */

	pos = pci_find_capability(dev, PCI_CAP_ID_MSIX);
	table_base = find_table_base(dev, pos);
	if (!table_base)
		return -ENODEV;

	/* MSI-X Table Initialization */
	for (i = 0; i < nvec; i++) {
		mapped = 0;
		list_for_each_entry(pirq_entry, &msi_dev_entry->pirq_list_head, list) {
			if (pirq_entry->entry_nr == entries[i].entry) {
				printk(KERN_WARNING "msix entry %d for dev %02x:%02x:%01x are \
				       not freed before acquire again.\n", entries[i].entry,
					   dev->bus->number, PCI_SLOT(dev->devfn),
					   PCI_FUNC(dev->devfn));
				(entries + i)->vector = pirq_entry->pirq;
				mapped = 1;
				break;
			}
		}
		if (mapped)
			continue;
		pirq = msi_map_vector(dev, entries[i].entry, table_base);
		if (pirq < 0)
			break;
		attach_pirq_entry(pirq, entries[i].entry, msi_dev_entry);
		(entries + i)->vector = pirq;
	}

	if (i != nvec) {
		int avail = i - 1;
		for (j = --i; j >= 0; j--) {
			msi_unmap_pirq(dev, entries[j].vector);
			detach_pirq_entry(entries[j].entry, msi_dev_entry);
			entries[j].vector = 0;
		}
		/* If we had some success report the number of irqs
		 * we succeeded in setting up.
		 */
		if (avail <= 0)
			avail = -EBUSY;
		return avail;
	}

	pci_intx_for_msi(dev, 0);
	msix_set_enable(dev, 1);
	dev->msix_enabled = 1;

	return 0;
}

/**
 * pci_msi_check_device - check whether MSI may be enabled on a device
 * @dev: pointer to the pci_dev data structure of MSI device function
 * @nvec: how many MSIs have been requested ?
 * @type: are we checking for MSI or MSI-X ?
 *
 * Look at global flags, the device itself, and its parent busses
 * to determine if MSI/-X are supported for the device. If MSI/-X is
 * supported return 0, else return an error code.
 **/
static int pci_msi_check_device(struct pci_dev* dev, int nvec, int type)
{
	struct pci_bus *bus;
	int ret;

	/* MSI must be globally enabled and supported by the device */
	if (!pci_msi_enable || !dev || dev->no_msi)
		return -EINVAL;

	/*
	 * You can't ask to have 0 or less MSIs configured.
	 *  a) it's stupid ..
	 *  b) the list manipulation code assumes nvec >= 1.
	 */
	if (nvec < 1)
		return -ERANGE;

	/* Any bridge which does NOT route MSI transactions from it's
	 * secondary bus to it's primary bus must set NO_MSI flag on
	 * the secondary pci_bus.
	 * We expect only arch-specific PCI host bus controller driver
	 * or quirks for specific PCI bridges to be setting NO_MSI.
	 */
	for (bus = dev->bus; bus; bus = bus->parent)
		if (bus->bus_flags & PCI_BUS_FLAGS_NO_MSI)
			return -EINVAL;

	ret = arch_msi_check_device(dev, nvec, type);
	if (ret)
		return ret;

	if (!pci_find_capability(dev, type))
		return -EINVAL;

	return 0;
}

/**
 * pci_enable_msi - configure device's MSI capability structure
 * @dev: pointer to the pci_dev data structure of MSI device function
 *
 * Setup the MSI capability structure of device function with
 * a single MSI irq upon its software driver call to request for
 * MSI mode enabled on its hardware device function. A return of zero
 * indicates the successful setup of an entry zero with the new MSI
 * vector or non-zero for otherwise.
 **/
extern int pci_frontend_enable_msi(struct pci_dev *dev);
int pci_enable_msi(struct pci_dev* dev)
{
	struct pci_bus *bus;
	int temp, status;

	for (bus = dev->bus; bus; bus = bus->parent)
		if (bus->bus_flags & PCI_BUS_FLAGS_NO_MSI)
			return -EINVAL;

	status = pci_msi_check_device(dev, 1, PCI_CAP_ID_MSI);
	if (status)
 		return status;

#ifdef CONFIG_XEN_PCIDEV_FRONTEND
	if (!is_initial_xendomain())
	{
		int ret;

		temp = dev->irq;
		ret = pci_frontend_enable_msi(dev);
		if (ret)
			return ret;

		dev->irq_old = temp;

		return ret;
	}
#endif

	temp = dev->irq;

	/* Check whether driver already requested for MSI-X irqs */
	if (dev->msix_enabled) {
		dev_info(&dev->dev, "can't enable MSI "
			 "(MSI-X already enabled)\n");
		return -EINVAL;
	}

	status = msi_capability_init(dev);
	if ( !status )
		dev->irq_old = temp;

	return status;
}
EXPORT_SYMBOL(pci_enable_msi);

extern void pci_frontend_disable_msi(struct pci_dev* dev);
void pci_msi_shutdown(struct pci_dev* dev)
{
	int pirq;

	if (!pci_msi_enable || !dev)
		return;

#ifdef CONFIG_XEN_PCIDEV_FRONTEND
	if (!is_initial_xendomain()) {
		pci_frontend_disable_msi(dev);
		dev->irq = dev->irq_old;
		return;
	}
#endif

	if (!dev->msi_enabled)
		return;

	pirq = dev->irq;
	/* Restore dev->irq to its default pin-assertion vector */
	dev->irq = dev->irq_old;
	msi_unmap_pirq(dev, pirq);

	/* Disable MSI mode */
	msi_set_enable(dev, 0);
	pci_intx_for_msi(dev, 1);
	dev->msi_enabled = 0;
}
void pci_disable_msi(struct pci_dev* dev)
{
	pci_msi_shutdown(dev);
}
EXPORT_SYMBOL(pci_disable_msi);

/**
 * pci_enable_msix - configure device's MSI-X capability structure
 * @dev: pointer to the pci_dev data structure of MSI-X device function
 * @entries: pointer to an array of MSI-X entries
 * @nvec: number of MSI-X irqs requested for allocation by device driver
 *
 * Setup the MSI-X capability structure of device function with the number
 * of requested irqs upon its software driver call to request for
 * MSI-X mode enabled on its hardware device function. A return of zero
 * indicates the successful configuration of MSI-X capability structure
 * with new allocated MSI-X irqs. A return of < 0 indicates a failure.
 * Or a return of > 0 indicates that driver request is exceeding the number
 * of irqs available. Driver should use the returned value to re-send
 * its request.
 **/
extern int pci_frontend_enable_msix(struct pci_dev *dev,
		struct msix_entry *entries, int nvec);
int pci_enable_msix(struct pci_dev* dev, struct msix_entry *entries, int nvec)
{
	int status, pos, nr_entries;
	int i, j, temp;
	u16 control;

	if (!entries)
 		return -EINVAL;

#ifdef CONFIG_XEN_PCIDEV_FRONTEND
	if (!is_initial_xendomain()) {
		int ret;

		ret = pci_frontend_enable_msix(dev, entries, nvec);
		if (ret) {
			printk("get %x from pci_frontend_enable_msix\n", ret);
			return ret;
		}

        return 0;
	}
#endif

	status = pci_msi_check_device(dev, nvec, PCI_CAP_ID_MSIX);
	if (status)
		return status;

	pos = pci_find_capability(dev, PCI_CAP_ID_MSIX);
	pci_read_config_word(dev, msi_control_reg(pos), &control);
	nr_entries = multi_msix_capable(control);
	if (nvec > nr_entries)
		return -EINVAL;

	/* Check for any invalid entries */
	for (i = 0; i < nvec; i++) {
		if (entries[i].entry >= nr_entries)
			return -EINVAL;		/* invalid entry */
		for (j = i + 1; j < nvec; j++) {
			if (entries[i].entry == entries[j].entry)
				return -EINVAL;	/* duplicate entry */
		}
	}

	temp = dev->irq;
	/* Check whether driver already requested for MSI vector */
	if (dev->msi_enabled) {
		dev_info(&dev->dev, "can't enable MSI-X "
		       "(MSI IRQ already assigned)\n");
		return -EINVAL;
	}

	status = msix_capability_init(dev, entries, nvec);

	if ( !status )
		dev->irq_old = temp;

	return status;
}
EXPORT_SYMBOL(pci_enable_msix);

extern void pci_frontend_disable_msix(struct pci_dev* dev);
void pci_msix_shutdown(struct pci_dev* dev)
{
	if (!pci_msi_enable)
		return;
	if (!dev)
		return;

#ifdef CONFIG_XEN_PCIDEV_FRONTEND
	if (!is_initial_xendomain()) {
		pci_frontend_disable_msix(dev);
		dev->irq = dev->irq_old;
		return;
	}
#endif

	if (!dev->msix_enabled)
		return;

	msi_remove_pci_irq_vectors(dev);

	/* Disable MSI mode */
	msix_set_enable(dev, 0);
	pci_intx_for_msi(dev, 1);
	dev->msix_enabled = 0;
}
void pci_disable_msix(struct pci_dev* dev)
{
	pci_msix_shutdown(dev);
}
EXPORT_SYMBOL(pci_disable_msix);

/**
 * msi_remove_pci_irq_vectors - reclaim MSI(X) irqs to unused state
 * @dev: pointer to the pci_dev data structure of MSI(X) device function
 *
 * Being called during hotplug remove, from which the device function
 * is hot-removed. All previous assigned MSI/MSI-X irqs, if
 * allocated for this device function, are reclaimed to unused state,
 * which may be used later on.
 **/
void msi_remove_pci_irq_vectors(struct pci_dev* dev)
{
	unsigned long flags;
	struct msi_dev_list *msi_dev_entry;
	struct msi_pirq_entry *pirq_entry, *tmp;

	if (!pci_msi_enable || !dev)
 		return;

	msi_dev_entry = get_msi_dev_pirq_list(dev);

	spin_lock_irqsave(&msi_dev_entry->pirq_list_lock, flags);
	if (!list_empty(&msi_dev_entry->pirq_list_head))
	{
		printk(KERN_WARNING "msix pirqs for dev %02x:%02x:%01x are not freed \
		       before acquire again.\n", dev->bus->number, PCI_SLOT(dev->devfn),
			   PCI_FUNC(dev->devfn));
		list_for_each_entry_safe(pirq_entry, tmp,
		                         &msi_dev_entry->pirq_list_head, list) {
			msi_unmap_pirq(dev, pirq_entry->pirq);
			list_del(&pirq_entry->list);
			kfree(pirq_entry);
		}
	}
	spin_unlock_irqrestore(&msi_dev_entry->pirq_list_lock, flags);
	dev->irq = dev->irq_old;
}

void pci_no_msi(void)
{
	pci_msi_enable = 0;
}

void pci_msi_init_pci_dev(struct pci_dev *dev)
{
#ifndef CONFIG_XEN
	INIT_LIST_HEAD(&dev->msi_list);
#endif
}
