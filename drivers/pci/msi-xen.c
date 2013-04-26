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
#include "msi.h"

static int pci_msi_enable = 1;
#if CONFIG_XEN_COMPAT < 0x040200
static int pci_seg_supported = 1;
#else
#define pci_seg_supported 1
#endif

static LIST_HEAD(msi_dev_head);
DEFINE_SPINLOCK(msi_dev_lock);

struct msi_pirq_entry {
	struct list_head list;
	int pirq;
	int entry_nr;
	struct msi_dev_list *dev_entry;
	struct kobject kobj;
};

struct msi_dev_list {
	struct pci_dev *dev;
	spinlock_t pirq_list_lock;
	/* Store default pre-assigned irq */
	unsigned int default_irq;
	domid_t owner;
	struct msi_pirq_entry e;
};

/* Arch hooks */

#ifndef arch_msi_check_device
int arch_msi_check_device(struct pci_dev *dev, int nvec, int type)
{
	return 0;
}
#endif

static void msi_set_enable(struct pci_dev *dev, int pos, int enable)
{
	u16 control;

	BUG_ON(!pos);

	pci_read_config_word(dev, pos + PCI_MSI_FLAGS, &control);
	control &= ~PCI_MSI_FLAGS_ENABLE;
	if (enable)
		control |= PCI_MSI_FLAGS_ENABLE;
	pci_write_config_word(dev, pos + PCI_MSI_FLAGS, control);
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
	memset(&entry->kobj, 0, sizeof(entry->kobj));
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

static int msi_unmap_pirq(struct pci_dev *dev, int pirq, domid_t owner,
			  struct kobject *kobj)
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
		dev_warn(&dev->dev, "unmap irq %d failed\n", pirq);

	if (rc < 0)
		return rc;

	/*
	 * Its possible that we get into this path when populate_msi_sysfs()
	 * fails, which means the entries were not registered with sysfs.
	 * In that case don't unregister them.
	 */
	if (kobj->parent) {
		kobject_del(kobj);
		kobject_put(kobj);
	}

	if (unmap.domid == DOMID_SELF)
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
static int msi_map_vector(struct pci_dev *dev, int entry_nr, u64 table_base,
			  domid_t domid)
{
	struct physdev_map_pirq map_irq;
	int rc = -EINVAL;

	map_irq.domid = domid;
	map_irq.type = MAP_PIRQ_TYPE_MSI_SEG;
	map_irq.index = -1;
	map_irq.pirq = -1;
	map_irq.bus = dev->bus->number | (pci_domain_nr(dev->bus) << 16);
	map_irq.devfn = dev->devfn;
	map_irq.entry_nr = entry_nr;
	map_irq.table_base = table_base;

	if (pci_seg_supported)
		rc = HYPERVISOR_physdev_op(PHYSDEVOP_map_pirq, &map_irq);
#if CONFIG_XEN_COMPAT < 0x040200
	if (rc == -EINVAL && !pci_domain_nr(dev->bus)) {
		map_irq.type = MAP_PIRQ_TYPE_MSI;
		map_irq.index = -1;
		map_irq.pirq = -1;
		map_irq.bus = dev->bus->number;
		rc = HYPERVISOR_physdev_op(PHYSDEVOP_map_pirq, &map_irq);
		if (rc != -EINVAL)
			pci_seg_supported = 0;
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

	/* If mapping of this particular MSI is on behalf of another domain,
	 * we do not need to get an irq in dom0. This also implies:
	 * dev->irq in dom0 will be 'Xen pirq' if this device belongs to
	 * to another domain, and will be 'Linux irq' if it belongs to dom0.
	 */
	if (domid == DOMID_SELF) {
		rc = evtchn_map_pirq(-1, map_irq.pirq);
		dev_printk(KERN_DEBUG, &dev->dev,
			   "irq %d (%d) for MSI/MSI-X\n",
			   rc, map_irq.pirq);
		return rc;
	}
	dev_printk(KERN_DEBUG, &dev->dev, "irq %d for dom%d MSI/MSI-X\n",
		   map_irq.pirq, domid);
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
	if (dev->msi_enabled) {
		int pos = pci_find_capability(dev, PCI_CAP_ID_MSI);

		msi_set_enable(dev, pos, 0);
	}
	if (dev->msix_enabled)
		msix_set_enable(dev, 0);

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
}
EXPORT_SYMBOL_GPL(pci_restore_msi_state);


#define to_msi_attr(obj) container_of(obj, struct msi_attribute, attr)
#define to_pirq_entry(obj) container_of(obj, struct msi_pirq_entry, kobj)

struct msi_attribute {
	struct attribute        attr;
	ssize_t (*show)(struct msi_pirq_entry *, struct msi_attribute *,
			char *buf);
	ssize_t (*store)(struct msi_pirq_entry *, struct msi_attribute *,
			 const char *buf, size_t count);
};

static ssize_t show_msi_mode(struct msi_pirq_entry *entry,
			     struct msi_attribute *attr, char *buf)
{
	return sprintf(buf, "%s\n", entry->entry_nr >= 0 ? "msix" : "msi");
}

static ssize_t show_xen_irq(struct msi_pirq_entry *entry,
			    struct msi_attribute *attr, char *buf)
{
	return sprintf(buf, "%d\n", entry->dev_entry->owner == DOMID_SELF
				    ? evtchn_get_xen_pirq(entry->pirq)
				    : entry->pirq);
}

static ssize_t msi_irq_attr_show(struct kobject *kobj,
				 struct attribute *attr, char *buf)
{
	struct msi_attribute *attribute = to_msi_attr(attr);
	struct msi_pirq_entry *entry = to_pirq_entry(kobj);

	if (!attribute->show)
		return -EIO;

	return attribute->show(entry, attribute, buf);
}

static const struct sysfs_ops msi_irq_sysfs_ops = {
	.show = msi_irq_attr_show,
};

static struct msi_attribute mode_attribute =
	__ATTR(mode, S_IRUGO, show_msi_mode, NULL);

static struct msi_attribute xen_irq_attribute =
	__ATTR(xen_irq, S_IRUGO, show_xen_irq, NULL);

static struct attribute *msi_irq_default_attrs[] = {
	&mode_attribute.attr,
	&xen_irq_attribute.attr,
	NULL
};

static struct attribute *msi_pirq_default_attrs[] = {
	&mode_attribute.attr,
	NULL
};

static void msi_kobj_release(struct kobject *kobj)
{
	struct msi_dev_list *entry = to_pirq_entry(kobj)->dev_entry;

	pci_dev_put(entry->dev);
}

static struct kobj_type msi_irq_ktype = {
	.release = msi_kobj_release,
	.sysfs_ops = &msi_irq_sysfs_ops,
	.default_attrs = msi_irq_default_attrs,
};

static struct kobj_type msi_pirq_ktype = {
	.release = msi_kobj_release,
	.sysfs_ops = &msi_irq_sysfs_ops,
	.default_attrs = msi_pirq_default_attrs,
};

static int populate_msi_sysfs(struct pci_dev *pdev)
{
	struct msi_dev_list *dev_entry = get_msi_dev_pirq_list(pdev);
	domid_t owner = dev_entry->owner;
	struct msi_pirq_entry *pirq_entry;
	struct kobject *kobj;
	int ret;
	int count = 0;

	pdev->msi_kset = kset_create_and_add("msi_irqs", NULL, &pdev->dev.kobj);
	if (!pdev->msi_kset)
		return -ENOMEM;

	if (pdev->msi_enabled) {
		kobj = &dev_entry->e.kobj;
		kobj->kset = pdev->msi_kset;
		pci_dev_get(pdev);
		if (owner == DOMID_SELF)
			ret = kobject_init_and_add(kobj, &msi_irq_ktype, NULL,
						   "%u", pdev->irq);
		else
			ret = kobject_init_and_add(kobj, &msi_pirq_ktype, NULL,
						   "xen-%u", pdev->irq);
		if (ret)
			pci_dev_put(pdev);
		return ret;
	}

	list_for_each_entry(pirq_entry, &pdev->msi_list, list) {
		kobj = &pirq_entry->kobj;
		kobj->kset = pdev->msi_kset;
		pci_dev_get(pdev);
		if (owner == DOMID_SELF)
			ret = kobject_init_and_add(kobj, &msi_irq_ktype, NULL,
						   "%u", pirq_entry->pirq);
		else
			ret = kobject_init_and_add(kobj, &msi_pirq_ktype, NULL,
						   "xen-%u", pirq_entry->pirq);
		if (ret)
			goto out_unroll;

		count++;
	}

	return 0;

out_unroll:
	pci_dev_put(pdev);
	list_for_each_entry(pirq_entry, &pdev->msi_list, list) {
		if (!count)
			break;
		kobject_del(&pirq_entry->kobj);
		kobject_put(&pirq_entry->kobj);
		count--;
	}
	return ret;
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
	int pos, pirq;
	u16 control;

	pos = pci_find_capability(dev, PCI_CAP_ID_MSI);
	msi_set_enable(dev, pos, 0);	/* Disable MSI during set up */

	pci_read_config_word(dev, msi_control_reg(pos), &control);

	pirq = msi_map_vector(dev, 0, 0, dev_entry->owner);
	if (pirq < 0)
		return -EBUSY;

	/* Set MSI enabled bits	 */
	pci_intx_for_msi(dev, 0);
	msi_set_enable(dev, pos, 1);
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
	int pirq, i, j, mapped, pos;
	u16 control;
	struct msi_dev_list *msi_dev_entry = get_msi_dev_pirq_list(dev);
	struct msi_pirq_entry *pirq_entry;

	if (!msi_dev_entry)
		return -ENOMEM;

	msix_set_enable(dev, 0);/* Ensure msix is disabled as I set it up */

	pos = pci_find_capability(dev, PCI_CAP_ID_MSIX);
	pci_read_config_word(dev, pos + PCI_MSIX_FLAGS, &control);

	/* Ensure MSI-X is disabled while it is set up */
	control &= ~PCI_MSIX_FLAGS_ENABLE;
	pci_write_config_word(dev, pos + PCI_MSIX_FLAGS, control);

	table_base = find_table_base(dev, pos);
	if (!table_base)
		return -ENODEV;

	/*
	 * Some devices require MSI-X to be enabled before we can touch the
	 * MSI-X registers.  We need to mask all the vectors to prevent
	 * interrupts coming in before they're fully set up.
	 */
	control |= PCI_MSIX_FLAGS_MASKALL | PCI_MSIX_FLAGS_ENABLE;
	pci_write_config_word(dev, pos + PCI_MSIX_FLAGS, control);

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
			msi_unmap_pirq(dev, entries[j].vector,
				       msi_dev_entry->owner,
				       &pirq_entry->kobj);
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

	control &= ~PCI_MSIX_FLAGS_MASKALL;
	pci_write_config_word(dev, pos + PCI_MSIX_FLAGS, control);

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
static int pci_msi_check_device(struct pci_dev *dev, int nvec, int type)
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

	/*
	 * Any bridge which does NOT route MSI transactions from its
	 * secondary bus to its primary bus must set NO_MSI flag on
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
 * pci_enable_msi_block - configure device's MSI capability structure
 * @dev: device to configure
 * @nvec: number of interrupts to configure
 *
 * Allocate IRQs for a device with the MSI capability.
 * This function returns a negative errno if an error occurs.  If it
 * is unable to allocate the number of interrupts requested, it returns
 * the number of interrupts it might be able to allocate.  If it successfully
 * allocates at least the number of interrupts requested, it returns 0 and
 * updates the @dev's irq member to the lowest new interrupt number; the
 * other interrupt numbers allocated to this device are consecutive.
 */
int pci_enable_msi_block(struct pci_dev *dev, unsigned int nvec)
{
	int temp, status, pos, maxvec;
	u16 msgctl;
	struct msi_dev_list *msi_dev_entry = get_msi_dev_pirq_list(dev);

	pos = pci_find_capability(dev, PCI_CAP_ID_MSI);
	if (!pos)
		return -EINVAL;
	pci_read_config_word(dev, pos + PCI_MSI_FLAGS, &msgctl);
	maxvec = 1 /* XXX << ((msgctl & PCI_MSI_FLAGS_QMASK) >> 1) */;
	if (nvec > maxvec)
		return maxvec;

	status = pci_msi_check_device(dev, nvec, PCI_CAP_ID_MSI);
	if (status)
		return status;

	if (!is_initial_xendomain()) {
#ifdef CONFIG_XEN_PCIDEV_FRONTEND
		int ret;

		temp = dev->irq;
		ret = pci_frontend_enable_msi(dev);
		if (ret)
			return ret;

		dev->irq = evtchn_map_pirq(-1, dev->irq);
		dev->msi_enabled = 1;
		msi_dev_entry->default_irq = temp;
		populate_msi_sysfs(dev);
		return ret;
#else
		return -EOPNOTSUPP;
#endif
	}

	temp = dev->irq;

	/* Check whether driver already requested MSI-X irqs */
	if (dev->msix_enabled) {
		dev_info(&dev->dev, "can't enable MSI "
			 "(MSI-X already enabled)\n");
		return -EINVAL;
	}

	status = msi_capability_init(dev, nvec);
	if ( !status )
		msi_dev_entry->default_irq = temp;

	return status;
}
EXPORT_SYMBOL(pci_enable_msi_block);

int pci_enable_msi_block_auto(struct pci_dev *dev, unsigned int *maxvec)
{
	int ret, pos, nvec;
	u16 msgctl;

	pos = pci_find_capability(dev, PCI_CAP_ID_MSI);
	if (!pos)
		return -EINVAL;

	pci_read_config_word(dev, pos + PCI_MSI_FLAGS, &msgctl);
	ret = 1 << ((msgctl & PCI_MSI_FLAGS_QMASK) >> 1);

	if (maxvec)
		*maxvec = ret;

	do {
		nvec = ret;
		ret = pci_enable_msi_block(dev, nvec);
	} while (ret > 0);

	if (ret < 0)
		return ret;
	return nvec;
}
EXPORT_SYMBOL(pci_enable_msi_block_auto);

void pci_msi_shutdown(struct pci_dev *dev)
{
	int pirq, pos;
	struct msi_dev_list *msi_dev_entry = get_msi_dev_pirq_list(dev);

	if (!pci_msi_enable || !dev || !dev->msi_enabled)
		return;

	if (!is_initial_xendomain()) {
#ifdef CONFIG_XEN_PCIDEV_FRONTEND
		evtchn_map_pirq(dev->irq, 0);
		pci_frontend_disable_msi(dev);
		dev->irq = msi_dev_entry->default_irq;
		dev->msi_enabled = 0;
#endif
		return;
	}

	pirq = dev->irq;
	/* Restore dev->irq to its default pin-assertion vector */
	dev->irq = msi_dev_entry->default_irq;
	msi_unmap_pirq(dev, pirq, msi_dev_entry->owner,
		       &msi_dev_entry->e.kobj);
	msi_dev_entry->owner = DOMID_IO;
	memset(&msi_dev_entry->e.kobj, 0, sizeof(msi_dev_entry->e.kobj));

	/* Disable MSI mode */
	pos = pci_find_capability(dev, PCI_CAP_ID_MSI);
	msi_set_enable(dev, pos, 0);
	pci_intx_for_msi(dev, 1);
	dev->msi_enabled = 0;
}

void pci_disable_msi(struct pci_dev *dev)
{
	pci_msi_shutdown(dev);
	kset_unregister(dev->msi_kset);
	dev->msi_kset = NULL;
}
EXPORT_SYMBOL(pci_disable_msi);

/**
 * pci_msix_table_size - return the number of device's MSI-X table entries
 * @dev: pointer to the pci_dev data structure of MSI-X device function
 */
int pci_msix_table_size(struct pci_dev *dev)
{
	int pos;
	u16 control;

	pos = pci_find_capability(dev, PCI_CAP_ID_MSIX);
	if (!pos)
		return 0;

	pci_read_config_word(dev, msi_control_reg(pos), &control);
	return multi_msix_capable(control);
}

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
			irq = evtchn_map_pirq(-1, entries[i].vector);
			attach_pirq_entry(irq, entries[i].entry, msi_dev_entry);
			entries[i].vector = irq;
		}
		populate_msi_sysfs(dev);
		return 0;
#else
		return -EOPNOTSUPP;
#endif
	}

	status = pci_msi_check_device(dev, nvec, PCI_CAP_ID_MSIX);
	if (status)
		return status;

	nr_entries = pci_msix_table_size(dev);
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
		dev_info(&dev->dev, "can't enable MSI-X "
		       "(MSI IRQ already assigned)\n");
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
	if (!pci_msi_enable || !dev || !dev->msix_enabled)
		return;

	if (!is_initial_xendomain())
#ifdef CONFIG_XEN_PCIDEV_FRONTEND
		pci_frontend_disable_msix(dev);
#else
		return;
#endif

	msi_remove_pci_irq_vectors(dev);

	/* Disable MSI mode */
	if (is_initial_xendomain()) {
		msix_set_enable(dev, 0);
		pci_intx_for_msi(dev, 1);
	}
	dev->msix_enabled = 0;
}

void pci_disable_msix(struct pci_dev *dev)
{
	pci_msix_shutdown(dev);
	kset_unregister(dev->msi_kset);
	dev->msi_kset = NULL;
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
void msi_remove_pci_irq_vectors(struct pci_dev *dev)
{
	unsigned long flags;
	struct msi_dev_list *msi_dev_entry;
	struct msi_pirq_entry *pirq_entry, *tmp;

	if (!pci_msi_enable || !dev)
		return;

	msi_dev_entry = get_msi_dev_pirq_list(dev);

	spin_lock_irqsave(&msi_dev_entry->pirq_list_lock, flags);
	list_for_each_entry_safe(pirq_entry, tmp, &dev->msi_list, list) {
		if (is_initial_xendomain())
			msi_unmap_pirq(dev, pirq_entry->pirq,
				       msi_dev_entry->owner,
				       &pirq_entry->kobj);
		else
			evtchn_map_pirq(pirq_entry->pirq, 0);
		list_del(&pirq_entry->list);
		kfree(pirq_entry);
	}
	spin_unlock_irqrestore(&msi_dev_entry->pirq_list_lock, flags);
	msi_dev_entry->owner = DOMID_IO;
	dev->irq = msi_dev_entry->default_irq;
}

void pci_no_msi(void)
{
	pci_msi_enable = 0;
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
	int pos;
	INIT_LIST_HEAD(&dev->msi_list);

	/* Disable the msi hardware to avoid screaming interrupts
	 * during boot.  This is the power on reset default so
	 * usually this should be a noop.
	 * But on a Xen host don't do this for IOMMUs which the hypervisor
	 * is in control of (and hence has already enabled on purpose).
	 */
	if (is_initial_xendomain()
	    && (dev->class >> 8) == PCI_CLASS_SYSTEM_IOMMU
	    && dev->vendor == PCI_VENDOR_ID_AMD)
		return;
	pos = pci_find_capability(dev, PCI_CAP_ID_MSI);
	if (pos)
		msi_set_enable(dev, pos, 0);
	msix_set_enable(dev, 0);
}
