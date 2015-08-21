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
#include <linux/export.h>
#include <linux/ioport.h>
#include <linux/pci.h>
#include <linux/proc_fs.h>
#include <linux/msi.h>
#include <linux/smp.h>
#include <linux/errno.h>
#include <linux/io.h>
#include <linux/slab.h>

#include <xen/interface/physdev.h>
#include <xen/evtchn.h>
#include <xen/pcifront.h>

#include "pci.h"

static bool pci_msi_enable = true;
#if CONFIG_XEN_COMPAT < 0x040200
static bool pci_seg_supported = true;
#else
#define pci_seg_supported true
#endif
static bool msi_multi_vec_supported = true;

static LIST_HEAD(msi_dev_head);
DEFINE_SPINLOCK(msi_dev_lock);

struct msi_pirq_entry {
	struct list_head list;
	int pirq;
	int entry_nr;
	struct msi_dev_list *dev_entry;
};

struct msi_dev_list {
	struct pci_dev *dev;
	spinlock_t pirq_list_lock;
	/* Store default pre-assigned irq */
	unsigned int default_irq;
	domid_t owner;
	struct msi_pirq_entry e;
};

#define msix_table_size(flags)	((flags & PCI_MSIX_FLAGS_QSIZE) + 1)


/* Arch hooks */

static int (*get_owner)(struct pci_dev *dev);

static domid_t msi_get_dev_owner(struct pci_dev *dev)
{
	int owner;

	if (is_initial_xendomain()
	    && get_owner && (owner = get_owner(dev)) >= 0) {
		dev_info(&dev->dev, "get owner: %u\n", owner);
		return owner;
	}

	return DOMID_SELF;
}

static struct msi_dev_list *get_msi_dev_pirq_list(struct pci_dev *dev)
{
	struct msi_dev_list *msi_dev_list, *ret = NULL;
	unsigned long flags;

	spin_lock_irqsave(&msi_dev_lock, flags);

	list_for_each_entry(msi_dev_list, &msi_dev_head, e.list)
		if ( msi_dev_list->dev == dev )
			ret = msi_dev_list;

	if ( ret ) {
		spin_unlock_irqrestore(&msi_dev_lock, flags);
		if (ret->owner == DOMID_IO)
			ret->owner = msi_get_dev_owner(dev);
		return ret;
	}

	/* Has not allocate msi_dev until now. */
	ret = kzalloc(sizeof(struct msi_dev_list), GFP_ATOMIC);

	/* Failed to allocate msi_dev structure */
	if ( !ret ) {
		spin_unlock_irqrestore(&msi_dev_lock, flags);
		return NULL;
	}

	ret->dev = dev;
	spin_lock_init(&ret->pirq_list_lock);
	ret->owner = msi_get_dev_owner(dev);
	ret->e.entry_nr = -1;
	ret->e.dev_entry = ret;
	list_add_tail(&ret->e.list, &msi_dev_head);
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
	entry->dev_entry = msi_dev_entry;
	spin_lock_irqsave(&msi_dev_entry->pirq_list_lock, flags);
	list_add_tail(&entry->list, &msi_dev_entry->dev->msi_list);
	spin_unlock_irqrestore(&msi_dev_entry->pirq_list_lock, flags);
	return 0;
}

static void detach_pirq_entry(int entry_nr,
 							struct msi_dev_list *msi_dev_entry)
{
	unsigned long flags;
	struct msi_pirq_entry *pirq_entry;

	list_for_each_entry(pirq_entry, &msi_dev_entry->dev->msi_list, list) {
		if (pirq_entry->entry_nr == entry_nr) {
			spin_lock_irqsave(&msi_dev_entry->pirq_list_lock, flags);
			list_del(&pirq_entry->list);
			spin_unlock_irqrestore(&msi_dev_entry->pirq_list_lock, flags);
			kfree(pirq_entry);
			return;
		}
	}
}

#ifdef CONFIG_XEN_PRIVILEGED_GUEST
/*
 * pciback will provide device's owner
 */
int register_msi_get_owner(int (*func)(struct pci_dev *dev))
{
	if (get_owner) {
		pr_warning("register msi_get_owner again\n");
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
#endif

static void msi_unmap_pirq(struct pci_dev *dev, int pirq, unsigned int nr,
			   domid_t owner)
{
	struct physdev_unmap_pirq unmap;
	int rc;

	unmap.domid = owner;
	/* See comments in msi_map_vector, input parameter pirq means
	 * irq number only if the device belongs to dom0 itself.
	 */
	unmap.pirq = (unmap.domid != DOMID_SELF)
		? pirq : evtchn_get_xen_pirq(pirq);

	if ((rc = HYPERVISOR_physdev_op(PHYSDEVOP_unmap_pirq, &unmap)))
		dev_warn(&dev->dev, "unmap irq %d failed (%d)\n", pirq, rc);

	if (unmap.domid == DOMID_SELF)
		evtchn_map_pirq(pirq, 0, nr);
}

static u64 find_table_base(struct pci_dev *dev)
{
	u8 bar;
	u32 reg;
	unsigned long flags;

	pci_read_config_dword(dev, dev->msix_cap + PCI_MSIX_TABLE, &reg);
	bar = reg & PCI_MSIX_PBA_BIR;

	flags = pci_resource_flags(dev, bar);
	if (!flags ||
	     (flags & (IORESOURCE_DISABLED | IORESOURCE_UNSET |
		       IORESOURCE_BUSY)))
		return 0;

	return pci_resource_start(dev, bar);
}

/*
 * Protected by msi_lock
 */
static int msi_map_vector(struct pci_dev *dev, int entry_nr, u64 table_base,
			  domid_t domid)
{
	struct physdev_map_pirq map_irq;
	int rc = -EINVAL;

	map_irq.domid = domid;
	if (table_base || entry_nr <= 1)
		map_irq.type = MAP_PIRQ_TYPE_MSI_SEG;
	else
		map_irq.type = MAP_PIRQ_TYPE_MULTI_MSI;
	map_irq.index = -1;
	map_irq.pirq = -1;
	map_irq.bus = dev->bus->number | (pci_domain_nr(dev->bus) << 16);
	map_irq.devfn = dev->devfn;
	map_irq.entry_nr = entry_nr;
	map_irq.table_base = table_base;

	if (pci_seg_supported)
		rc = HYPERVISOR_physdev_op(PHYSDEVOP_map_pirq, &map_irq);
	if ((rc == -EINVAL || rc == -EOPNOTSUPP)
	    && map_irq.type == MAP_PIRQ_TYPE_MULTI_MSI
	    && map_irq.entry_nr == entry_nr) {
		msi_multi_vec_supported = false;
		return rc;
	}
#if CONFIG_XEN_COMPAT < 0x040200
	if (rc == -EINVAL && !pci_domain_nr(dev->bus)) {
		map_irq.type = MAP_PIRQ_TYPE_MSI;
		map_irq.index = -1;
		map_irq.pirq = -1;
		map_irq.bus = dev->bus->number;
		rc = HYPERVISOR_physdev_op(PHYSDEVOP_map_pirq, &map_irq);
		if (rc != -EINVAL)
			pci_seg_supported = false;
	}
#endif
	if (rc)
		dev_warn(&dev->dev, "map irq failed\n");

	if (rc < 0)
		return rc;
	/* This happens when MSI support is not enabled in older Xen. */
	if (rc == 0 && map_irq.pirq < 0)
		return -ENOSYS;

	BUG_ON(map_irq.pirq <= 0);

	if (table_base || entry_nr <= 0)
		entry_nr = 1;

	/* If mapping of this particular MSI is on behalf of another domain,
	 * we do not need to get an irq in dom0. This also implies:
	 * dev->irq in dom0 will be 'Xen pirq' if this device belongs to
	 * to another domain, and will be 'Linux irq' if it belongs to dom0.
	 */
	if (domid == DOMID_SELF) {
		rc = evtchn_map_pirq(-1, map_irq.pirq, entry_nr);
		if (rc < 0 || entry_nr == 1)
			dev_printk(KERN_DEBUG, &dev->dev,
				   "irq %d (%d) for MSI/MSI-X\n",
				   rc, map_irq.pirq);
		else
			dev_printk(KERN_DEBUG, &dev->dev,
				   "irq %d (%d) ... %d (%d) for MSI\n",
				   rc, map_irq.pirq, rc + entry_nr - 1,
				   map_irq.pirq + entry_nr - 1);
		return rc;
	}
	if (entry_nr == 1)
		dev_printk(KERN_DEBUG, &dev->dev,
			   "irq %d for dom%d MSI/MSI-X\n",
			   map_irq.pirq, domid);
	else
		dev_printk(KERN_DEBUG, &dev->dev,
			   "irq %d...%d for dom%d MSI\n",
			   map_irq.pirq, map_irq.pirq + entry_nr - 1, domid);
	return map_irq.pirq;
}

static void pci_intx_for_msi(struct pci_dev *dev, int enable)
{
	if (!(dev->dev_flags & PCI_DEV_FLAGS_MSI_INTX_DISABLE_BUG))
		pci_intx(dev, enable);
}

void pci_restore_msi_state(struct pci_dev *dev)
{
	int rc = -ENOSYS;

	if (!dev->msi_enabled && !dev->msix_enabled)
		return;

	pci_intx_for_msi(dev, 0);
	if (dev->msi_enabled)
		pci_msi_set_enable(dev, 0);
	if (dev->msix_enabled)
		pci_msix_clear_and_set_ctrl(dev, 0,
					    PCI_MSIX_FLAGS_ENABLE |
					    PCI_MSIX_FLAGS_MASKALL);

	if (pci_seg_supported) {
		struct physdev_pci_device restore = {
			.seg = pci_domain_nr(dev->bus),
			.bus = dev->bus->number,
			.devfn = dev->devfn
		};

		rc = HYPERVISOR_physdev_op(PHYSDEVOP_restore_msi_ext,
					   &restore);
	}
#if CONFIG_XEN_COMPAT < 0x040200
	if (rc == -ENOSYS && !pci_domain_nr(dev->bus)) {
		struct physdev_restore_msi restore = {
			.bus = dev->bus->number,
			.devfn = dev->devfn
		};

		pci_seg_supported = false;
		rc = HYPERVISOR_physdev_op(PHYSDEVOP_restore_msi, &restore);
	}
#endif
	WARN(rc && rc != -ENOSYS, "restore_msi -> %d\n", rc);

	if (dev->msix_enabled)
		pci_msix_clear_and_set_ctrl(dev, PCI_MSIX_FLAGS_MASKALL, 0);
}
EXPORT_SYMBOL_GPL(pci_restore_msi_state);

static ssize_t msi_mode_show(struct device *dev, struct device_attribute *attr,
			     char *buf)
{
	struct pci_dev *pdev = to_pci_dev(dev);

	return sprintf(buf, "msi%s\n", pdev->msix_enabled ? "x" : "");
}

static ssize_t msi_xen_irq_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct pci_dev *pdev = to_pci_dev(dev);
	struct msi_dev_list *dev_entry = get_msi_dev_pirq_list(pdev);
	struct msi_pirq_entry *entry;
	unsigned long pirq, flags;
	int retval;

	retval = kstrtoul(attr->attr.name, 10, &pirq);
	if (retval)
		return retval;

	if (pdev->msi_enabled) {
		if (pirq < pdev->irq ||
		    pirq >= pdev->irq - dev_entry->e.entry_nr)
			return -ENODEV;
		return sprintf(buf, "%ld\n",
			       dev_entry->owner == DOMID_SELF
			       ? evtchn_get_xen_pirq(pirq) : pirq);
	}

	spin_lock_irqsave(&dev_entry->pirq_list_lock, flags);
	list_for_each_entry(entry, &pdev->msi_list, list)
		if (entry->pirq == pirq) {
			spin_unlock_irqrestore(&dev_entry->pirq_list_lock, flags);
			return sprintf(buf, "%ld\n",
				       dev_entry->owner == DOMID_SELF
				       ? evtchn_get_xen_pirq(pirq) : pirq);
		}
	spin_unlock_irqrestore(&dev_entry->pirq_list_lock, flags);

	return -ENODEV;
}

static int populate_msi_sysfs(struct pci_dev *pdev)
{
	struct attribute **msi_attrs, **msi_xen_attrs;
	struct attribute *msi_attr;
	struct device_attribute *msi_dev_attr;
	struct attribute_group *msi_irq_group;
	const struct attribute_group **msi_irq_groups;
	const struct msi_dev_list *dev_entry = get_msi_dev_pirq_list(pdev);
	const struct msi_pirq_entry *entry;
	int ret = -ENOMEM;
	int num_msi = 0;
	int count = 0;

	/* Determine how many msi entries we have */
	if (pdev->msi_enabled)
		num_msi = -dev_entry->e.entry_nr;
	else
		list_for_each_entry(entry, &pdev->msi_list, list)
			++num_msi;
	if (!num_msi)
		return 0;

	/* Dynamically create the MSI attributes for the PCI device */
	msi_attrs = kzalloc(sizeof(void *) * (num_msi + 1) * 2, GFP_KERNEL);
	if (!msi_attrs)
		return -ENOMEM;
	msi_xen_attrs = msi_attrs + num_msi + 1;
	if (pdev->msi_enabled)
		while (count < num_msi) {
			msi_dev_attr = kzalloc(sizeof(*msi_dev_attr) * 2,
					       GFP_KERNEL);
			if (!msi_dev_attr)
				goto error_attrs;
			msi_attrs[count] = &msi_dev_attr[0].attr;
			sysfs_attr_init(&msi_dev_attr[0].attr);
			msi_xen_attrs[count] = &msi_dev_attr[1].attr;
			sysfs_attr_init(&msi_dev_attr[1].attr);
			msi_dev_attr[0].attr.name = kasprintf(GFP_KERNEL, "%d",
							      pdev->irq + count);
			if (!msi_dev_attr[0].attr.name)
				goto error_attrs;
			msi_dev_attr[1].attr.name = msi_dev_attr[0].attr.name;
			msi_dev_attr[0].attr.mode = S_IRUGO;
			msi_dev_attr[1].attr.mode = S_IRUGO;
			msi_dev_attr[0].show = msi_mode_show;
			msi_dev_attr[1].show = msi_xen_irq_show;
			++count;
		}
	else
		list_for_each_entry(entry, &pdev->msi_list, list) {
			msi_dev_attr = kzalloc(sizeof(*msi_dev_attr) * 2,
					       GFP_KERNEL);
			if (!msi_dev_attr)
				goto error_attrs;
			msi_attrs[count] = &msi_dev_attr[0].attr;
			sysfs_attr_init(&msi_dev_attr[0].attr);
			msi_xen_attrs[count] = &msi_dev_attr[1].attr;
			sysfs_attr_init(&msi_dev_attr[1].attr);
			msi_dev_attr[0].attr.name = kasprintf(GFP_KERNEL, "%d",
							      entry->pirq);
			if (!msi_dev_attr[0].attr.name)
				goto error_attrs;
			msi_dev_attr[1].attr.name = msi_dev_attr[0].attr.name;
			msi_dev_attr[0].attr.mode = S_IRUGO;
			msi_dev_attr[1].attr.mode = S_IRUGO;
			msi_dev_attr[0].show = msi_mode_show;
			msi_dev_attr[1].show = msi_xen_irq_show;
			++count;
		}

	msi_irq_group = kzalloc(sizeof(*msi_irq_group) * 2, GFP_KERNEL);
	if (!msi_irq_group)
		goto error_attrs;
	msi_irq_group[0].name = "msi_irqs";
	msi_irq_group[0].attrs = msi_attrs;
	msi_irq_group[1].name = "msi_pirqs";
	msi_irq_group[1].attrs = msi_xen_attrs;

	msi_irq_groups = kzalloc(sizeof(void *) * 3, GFP_KERNEL);
	if (!msi_irq_groups)
		goto error_irq_group;
	msi_irq_groups[0] = msi_irq_group;
	if (dev_entry->owner == DOMID_SELF)
		msi_irq_groups[1] = msi_irq_group + 1;

	ret = sysfs_create_groups(&pdev->dev.kobj, msi_irq_groups);
	if (ret)
		goto error_irq_groups;
	pdev->msi_irq_groups = msi_irq_groups;

	return 0;

error_irq_groups:
	kfree(msi_irq_groups);
error_irq_group:
	kfree(msi_irq_group);
error_attrs:
	count = 0;
	msi_attr = msi_attrs[count];
	while (msi_attr) {
		msi_dev_attr = container_of(msi_attr, struct device_attribute, attr);
		kfree(msi_attr->name);
		kfree(msi_dev_attr);
		++count;
		msi_attr = msi_attrs[count];
	}
	kfree(msi_attrs);
	return ret;
}

static void cleanup_msi_sysfs(struct pci_dev *pdev)
{
	struct attribute **msi_attrs, *msi_attr;
	unsigned int count = 0;

	if (!pdev->msi_irq_groups)
		return;

	sysfs_remove_groups(&pdev->dev.kobj, pdev->msi_irq_groups);
	msi_attrs = pdev->msi_irq_groups[0]->attrs;
	msi_attr = msi_attrs[count];
	while (msi_attr) {
		struct device_attribute *dev_attr;

		dev_attr = container_of(msi_attr, struct device_attribute,
					attr);
		kfree(dev_attr->attr.name);
		kfree(dev_attr);
		msi_attr = msi_attrs[++count];
	}
	kfree(msi_attrs);
	kfree(pdev->msi_irq_groups[0]);
	kfree(pdev->msi_irq_groups);
	pdev->msi_irq_groups = NULL;
}

/**
 * msi_capability_init - configure device's MSI capability structure
 * @dev: pointer to the pci_dev data structure of MSI device function
 * @nvec: number of interrupts to allocate
 *
 * Setup the MSI capability structure of the device with the requested
 * number of interrupts.  A return value of zero indicates the successful
 * setup of an entry with the new MSI irq.  A negative return value indicates
 * an error, and a positive return value indicates the number of interrupts
 * which could have been allocated.
 */
static int msi_capability_init(struct pci_dev *dev, int nvec)
{
	struct msi_dev_list *dev_entry = get_msi_dev_pirq_list(dev);
	int pirq;

	pci_msi_set_enable(dev, 0);	/* Disable MSI during set up */

	pirq = msi_map_vector(dev, nvec, 0, dev_entry->owner);
	if (pirq < 0)
		return pirq;
	dev_entry->e.entry_nr = -nvec;

	/* Set MSI enabled bits	 */
	pci_intx_for_msi(dev, 0);
	pci_msi_set_enable(dev, 1);
	dev->msi_enabled = 1;

	dev->irq = dev_entry->e.pirq = pirq;
	populate_msi_sysfs(dev);
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
	int pirq, i, j, mapped;
	struct msi_dev_list *msi_dev_entry = get_msi_dev_pirq_list(dev);
	struct msi_pirq_entry *pirq_entry;

	if (!msi_dev_entry)
		return -ENOMEM;

	/* Ensure MSI-X is disabled while it is set up */
	pci_msix_clear_and_set_ctrl(dev, PCI_MSIX_FLAGS_ENABLE, 0);

	table_base = find_table_base(dev);
	if (!table_base)
		return -ENODEV;

	/*
	 * Some devices require MSI-X to be enabled before we can touch the
	 * MSI-X registers.  We need to mask all the vectors to prevent
	 * interrupts coming in before they're fully set up.
	 */
	pci_msix_clear_and_set_ctrl(dev, 0,
				PCI_MSIX_FLAGS_MASKALL | PCI_MSIX_FLAGS_ENABLE);

	for (i = 0; i < nvec; i++) {
		mapped = 0;
		list_for_each_entry(pirq_entry, &dev->msi_list, list) {
			if (pirq_entry->entry_nr == entries[i].entry) {
				dev_warn(&dev->dev,
					 "msix entry %d was not freed\n",
					 entries[i].entry);
				(entries + i)->vector = pirq_entry->pirq;
				mapped = 1;
				break;
			}
		}
		if (mapped)
			continue;
		pirq = msi_map_vector(dev, entries[i].entry, table_base,
				      msi_dev_entry->owner);
		if (pirq < 0)
			break;
		attach_pirq_entry(pirq, entries[i].entry, msi_dev_entry);
		(entries + i)->vector = pirq;
	}

	if (i != nvec) {
		int avail = i - 1;
		for (j = --i; j >= 0; j--) {
			list_for_each_entry(pirq_entry, &dev->msi_list, list)
				if (pirq_entry->entry_nr == entries[i].entry)
					break;
			msi_unmap_pirq(dev, entries[j].vector, 1,
				       msi_dev_entry->owner);
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

	/* Set MSI-X enabled bits and unmask the function */
	pci_intx_for_msi(dev, 0);
	dev->msix_enabled = 1;
	populate_msi_sysfs(dev);

	pci_msix_clear_and_set_ctrl(dev, PCI_MSIX_FLAGS_MASKALL, 0);

	return 0;
}

/**
 * pci_msi_supported - check whether MSI may be enabled on a device
 * @dev: pointer to the pci_dev data structure of MSI device function
 * @nvec: how many MSIs have been requested ?
 *
 * Look at global flags, the device itself, and its parent buses
 * to determine if MSI/-X are supported for the device. If MSI/-X is
 * supported return 1, else return 0.
 **/
static int pci_msi_supported(struct pci_dev *dev, int nvec)
{
	struct pci_bus *bus;

	/* MSI must be globally enabled and supported by the device */
	if (!pci_msi_enable)
		return 0;

	if (!dev || dev->no_msi || dev->current_state != PCI_D0)
		return 0;

	/*
	 * You can't ask to have 0 or less MSIs configured.
	 *  a) it's stupid ..
	 *  b) the list manipulation code assumes nvec >= 1.
	 */
	if (nvec < 1)
		return 0;

	/*
	 * Any bridge which does NOT route MSI transactions from its
	 * secondary bus to its primary bus must set NO_MSI flag on
	 * the secondary pci_bus.
	 * We expect only arch-specific PCI host bus controller driver
	 * or quirks for specific PCI bridges to be setting NO_MSI.
	 */
	for (bus = dev->bus; bus; bus = bus->parent)
		if (bus->bus_flags & PCI_BUS_FLAGS_NO_MSI)
			return 0;

	return 1;
}

/**
 * pci_msi_vec_count - Return the number of MSI vectors a device can send
 * @dev: device to report about
 *
 * This function returns the number of MSI vectors a device requested via
 * Multiple Message Capable register. It returns a negative errno if the
 * device is not capable sending MSI interrupts. Otherwise, the call succeeds
 * and returns a power of two, up to a maximum of 2^5 (32), according to the
 * MSI specification.
 **/
int pci_msi_vec_count(struct pci_dev *dev)
{
	int ret;
	u16 msgctl;

	if (!dev->msi_cap)
		return -EINVAL;

	pci_read_config_word(dev, dev->msi_cap + PCI_MSI_FLAGS, &msgctl);
	ret = 1 << ((msgctl & PCI_MSI_FLAGS_QMASK) >> 1);

	return ret;
}
EXPORT_SYMBOL(pci_msi_vec_count);

void pci_msi_shutdown(struct pci_dev *dev)
{
	int pirq;
	struct msi_dev_list *msi_dev_entry = get_msi_dev_pirq_list(dev);

	if (!pci_msi_enable || !dev || !dev->msi_enabled)
		return;

	if (!is_initial_xendomain()) {
#ifdef CONFIG_XEN_PCIDEV_FRONTEND
		evtchn_map_pirq(dev->irq, 0, -msi_dev_entry->e.entry_nr);
		pci_frontend_disable_msi(dev);
		dev->irq = msi_dev_entry->default_irq;
		dev->msi_enabled = 0;
#endif
		return;
	}

	pirq = dev->irq;
	/* Restore dev->irq to its default pin-assertion vector */
	dev->irq = msi_dev_entry->default_irq;
	msi_unmap_pirq(dev, pirq, -msi_dev_entry->e.entry_nr,
		       msi_dev_entry->owner);
	msi_dev_entry->owner = DOMID_IO;

	/* Disable MSI mode */
	pci_msi_set_enable(dev, 0);
	pci_intx_for_msi(dev, 1);
	dev->msi_enabled = 0;
}

void pci_disable_msi(struct pci_dev *dev)
{
	pci_msi_shutdown(dev);
	cleanup_msi_sysfs(dev);
}
EXPORT_SYMBOL(pci_disable_msi);

/**
 * pci_msix_vec_count - return the number of device's MSI-X table entries
 * @dev: pointer to the pci_dev data structure of MSI-X device function
 * This function returns the number of device's MSI-X table entries and
 * therefore the number of MSI-X vectors device is capable of sending.
 * It returns a negative errno if the device is not capable of sending MSI-X
 * interrupts.
 **/
int pci_msix_vec_count(struct pci_dev *dev)
{
	u16 control;

	if (!dev->msix_cap)
		return -EINVAL;

	pci_read_config_word(dev, dev->msix_cap + PCI_MSIX_FLAGS, &control);
	return msix_table_size(control);
}
EXPORT_SYMBOL(pci_msix_vec_count);

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
 * of irqs or MSI-X vectors available. Driver should use the returned value to
 * re-send its request.
 **/
int pci_enable_msix(struct pci_dev *dev, struct msix_entry *entries, int nvec)
{
	int status, nr_entries;
	int i, j, temp;
	struct msi_dev_list *msi_dev_entry = get_msi_dev_pirq_list(dev);

	if (!pci_msi_supported(dev, nvec))
		return -EINVAL;

	if (!entries)
		return -EINVAL;

	if (!is_initial_xendomain()) {
#ifdef CONFIG_XEN_PCIDEV_FRONTEND
		struct msi_pirq_entry *pirq_entry;
		int ret, irq;

		temp = dev->irq;
		ret = pci_frontend_enable_msix(dev, entries, nvec);
		if (ret) {
			dev_warn(&dev->dev,
				 "got %x from frontend_enable_msix\n", ret);
			return ret;
		}
		dev->msix_enabled = 1;
		msi_dev_entry->default_irq = temp;

		for (i = 0; i < nvec; i++) {
			int mapped = 0;

			list_for_each_entry(pirq_entry, &dev->msi_list, list) {
				if (pirq_entry->entry_nr == entries[i].entry) {
					irq = pirq_entry->pirq;
					BUG_ON(entries[i].vector != evtchn_get_xen_pirq(irq));
					entries[i].vector = irq;
					mapped = 1;
					break;
				}
			}
			if (mapped)
				continue;
			irq = evtchn_map_pirq(-1, entries[i].vector, 1);
			attach_pirq_entry(irq, entries[i].entry, msi_dev_entry);
			entries[i].vector = irq;
		}
		populate_msi_sysfs(dev);
		return 0;
#else
		return -EOPNOTSUPP;
#endif
	}


	nr_entries = pci_msix_vec_count(dev);
	if (nr_entries < 0)
		return nr_entries;
	if (nvec > nr_entries)
		return nr_entries;

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
		dev_info(&dev->dev, "can't enable MSI-X (MSI IRQ already assigned)\n");
		return -EINVAL;
	}

	status = msix_capability_init(dev, entries, nvec);

	if ( !status )
		msi_dev_entry->default_irq = temp;

	return status;
}
EXPORT_SYMBOL(pci_enable_msix);

void pci_msix_shutdown(struct pci_dev *dev)
{
	struct msi_dev_list *msi_dev_entry;

	if (!pci_msi_enable || !dev || !dev->msix_enabled)
		return;

	if (!is_initial_xendomain())
#ifdef CONFIG_XEN_PCIDEV_FRONTEND
		pci_frontend_disable_msix(dev);
#else
		return;
#endif

	cleanup_msi_sysfs(dev);

	for (msi_dev_entry = get_msi_dev_pirq_list(dev); ; ) {
		struct msi_pirq_entry *pirq_entry;
		unsigned long flags;

		spin_lock_irqsave(&msi_dev_entry->pirq_list_lock, flags);
		pirq_entry = list_first_entry_or_null(&dev->msi_list,
						      struct msi_pirq_entry,
						      list);
		if (pirq_entry)
			list_del(&pirq_entry->list);
		spin_unlock_irqrestore(&msi_dev_entry->pirq_list_lock, flags);
		if (!pirq_entry)
			break;
		if (is_initial_xendomain())
			msi_unmap_pirq(dev, pirq_entry->pirq, 1,
				       msi_dev_entry->owner);
		else
			evtchn_map_pirq(pirq_entry->pirq, 0, 1);
		kfree(pirq_entry);
	}
	msi_dev_entry->owner = DOMID_IO;
	dev->irq = msi_dev_entry->default_irq;

	/* Disable MSI mode */
	if (is_initial_xendomain()) {
		pci_msix_clear_and_set_ctrl(dev, PCI_MSIX_FLAGS_ENABLE, 0);
		pci_intx_for_msi(dev, 1);
	}
	dev->msix_enabled = 0;
}

void pci_disable_msix(struct pci_dev *dev)
{
	pci_msix_shutdown(dev);
}
EXPORT_SYMBOL(pci_disable_msix);

void pci_no_msi(void)
{
	pci_msi_enable = false;
}

/**
 * pci_msi_enabled - is MSI enabled?
 *
 * Returns true if MSI has not been disabled by the command-line option
 * pci=nomsi.
 **/
int pci_msi_enabled(void)
{
	return pci_msi_enable;
}
EXPORT_SYMBOL(pci_msi_enabled);

void pci_msi_init_pci_dev(struct pci_dev *dev)
{
	INIT_LIST_HEAD(&dev->msi_list);
}

/**
 * pci_enable_msi_range - configure device's MSI capability structure
 * @dev: device to configure
 * @minvec: minimal number of interrupts to configure
 * @maxvec: maximum number of interrupts to configure
 *
 * This function tries to allocate a maximum possible number of interrupts in a
 * range between @minvec and @maxvec. It returns a negative errno if an error
 * occurs. If it succeeds, it returns the actual number of interrupts allocated
 * and updates the @dev's irq member to the lowest new interrupt number;
 * the other interrupt numbers allocated to this device are consecutive.
 **/
int pci_enable_msi_range(struct pci_dev *dev, int minvec, int maxvec)
{
	int nvec, temp;
	int rc;
	struct msi_dev_list *msi_dev_entry = get_msi_dev_pirq_list(dev);

	if (!pci_msi_supported(dev, minvec))
		return -EINVAL;

	WARN_ON(!!dev->msi_enabled);

	/* Check whether driver already requested MSI-X irqs */
	if (dev->msix_enabled) {
		dev_info(&dev->dev,
			 "can't enable MSI (MSI-X already enabled)\n");
		return -EINVAL;
	}

	if (minvec <= 0 || maxvec < minvec)
		return -ERANGE;

	if (msi_multi_vec_supported)
		nvec = pci_msi_vec_count(dev);
	else
		nvec = 1;
	if (nvec < 0)
		return nvec;
	else if (nvec < minvec)
		return -EINVAL;
	else if (nvec > maxvec)
		nvec = maxvec;

	temp = dev->irq;

	do {
		if (is_initial_xendomain())
			rc = msi_capability_init(dev, nvec);
		else
#ifdef CONFIG_XEN_PCIDEV_FRONTEND
			rc = pci_frontend_enable_msi(dev, nvec);
#else
			return -EOPNOTSUPP;
#endif
		if (rc < 0) {
			return rc;
		} else if (rc > 0) {
			if (rc < minvec)
				return -ENOSPC;
			nvec = rc;
		}
	} while (rc);

	msi_dev_entry->default_irq = temp;

#ifdef CONFIG_XEN_PCIDEV_FRONTEND
	if (!is_initial_xendomain()) {
		dev->irq = evtchn_map_pirq(-1, dev->irq, nvec);
		msi_dev_entry->e.entry_nr = -nvec;
		dev->msi_enabled = 1;
		populate_msi_sysfs(dev);
	}
#endif

	return nvec;
}
EXPORT_SYMBOL(pci_enable_msi_range);

/**
 * pci_enable_msix_range - configure device's MSI-X capability structure
 * @dev: pointer to the pci_dev data structure of MSI-X device function
 * @entries: pointer to an array of MSI-X entries
 * @minvec: minimum number of MSI-X irqs requested
 * @maxvec: maximum number of MSI-X irqs requested
 *
 * Setup the MSI-X capability structure of device function with a maximum
 * possible number of interrupts in the range between @minvec and @maxvec
 * upon its software driver call to request for MSI-X mode enabled on its
 * hardware device function. It returns a negative errno if an error occurs.
 * If it succeeds, it returns the actual number of interrupts allocated and
 * indicates the successful configuration of MSI-X capability structure
 * with new allocated MSI-X interrupts.
 **/
int pci_enable_msix_range(struct pci_dev *dev, struct msix_entry *entries,
			       int minvec, int maxvec)
{
	int nvec = maxvec;
	int rc;

	if (maxvec < minvec)
		return -ERANGE;

	do {
		rc = pci_enable_msix(dev, entries, nvec);
		if (rc < 0) {
			return rc;
		} else if (rc > 0) {
			if (rc < minvec)
				return -ENOSPC;
			nvec = rc;
		}
	} while (rc);

	return nvec;
}
EXPORT_SYMBOL(pci_enable_msix_range);
