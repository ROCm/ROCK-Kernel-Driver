/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 *
 * Copyright (c) 2009 Isaku Yamahata
 *                    VA Linux Systems Japan K.K.
 *
 */

#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/fs.h>
#include <linux/miscdevice.h>
#include <linux/pci.h>
#include <linux/sort.h>

#include <asm/setup.h>
#include <asm/uaccess.h>

#include "pci.h"
#include "iomulti.h"

#define PCI_NUM_BARS		6
#define PCI_BUS_MAX		255
#define PCI_DEV_MAX		31
#define PCI_FUNC_MAX		7
#define PCI_NUM_FUNC		8

/* see pci_resource_len */
static inline resource_size_t pci_iomul_len(const struct resource* r)
{
	if (r->start == 0 && r->start == r->end)
		return 0;
	return r->end - r->start + 1;
}

#define ROUND_UP(x, a)		(((x) + (a) - 1) & ~((a) - 1))
/* stolen from pbus_size_io() */
static unsigned long pdev_size_io(struct pci_dev *pdev)
{
	unsigned long size = 0, size1 = 0;
	int i;

	for (i = 0; i < PCI_NUM_RESOURCES; i++) {
		struct resource *r = &pdev->resource[i];
		unsigned long r_size;

		if (!(r->flags & IORESOURCE_IO))
			continue;

		r_size = r->end - r->start + 1;

		if (r_size < 0x400)
			/* Might be re-aligned for ISA */
			size += r_size;
		else
			size1 += r_size;
	}

/* To be fixed in 2.5: we should have sort of HAVE_ISA
   flag in the struct pci_bus. */
#if defined(CONFIG_ISA) || defined(CONFIG_EISA)
	size = (size & 0xff) + ((size & ~0xffUL) << 2);
#endif
	size = ROUND_UP(size + size1, 4096);
	return size;
}

/*
 * primary bus number of PCI-PCI bridge in switch on which
 * this slots sits.
 * i.e. the primary bus number of PCI-PCI bridge of downstream port
 *      or root port in switch.
 *      the secondary bus number of PCI-PCI bridge of upstream port
 *      in switch.
 */
static inline unsigned char pci_dev_switch_busnr(struct pci_dev *pdev)
{
	if (pci_find_capability(pdev, PCI_CAP_ID_EXP))
		return pdev->bus->primary;
	return pdev->bus->number;
}

struct pci_iomul_func {
	int		segment;
	uint8_t		bus;
	uint8_t		devfn;

	/* only start and end are used */
	unsigned long	io_size;
	uint8_t		io_bar;
	struct resource	resource[PCI_NUM_BARS];
	struct resource dummy_parent;
};

struct pci_iomul_switch {
	struct list_head	list;	/* bus_list_lock protects */

	/*
	 * This lock the following entry and following
	 * pci_iomul_slot/pci_iomul_func.
	 */
	struct mutex		lock;
	struct kref		kref;

	struct resource		io_resource;
	struct resource		*io_region;
	unsigned int		count;
	struct pci_dev		*current_pdev;

	int			segment;
	uint8_t			bus;

	uint32_t		io_base;
	uint32_t		io_limit;

	/* func which has the largeset io size*/
	struct pci_iomul_func	*func;

	struct list_head	slots;
};

struct pci_iomul_slot {
	struct list_head	sibling;
	struct kref		kref;
	/*
	 * busnr
	 * when pcie, the primary busnr of the PCI-PCI bridge on which
	 * this devices sits.
	 */
	uint8_t			switch_busnr;
	struct resource		dummy_parent[PCI_NUM_RESOURCES - PCI_BRIDGE_RESOURCES];

	/* device */
	int			segment;
	uint8_t			bus;
	uint8_t			dev;

	struct pci_iomul_func	*func[PCI_NUM_FUNC];
};

static LIST_HEAD(switch_list);
static DEFINE_MUTEX(switch_list_lock);

/*****************************************************************************/
static int inline pci_iomul_switch_io_allocated(
	const struct pci_iomul_switch *sw)
{
	return !(sw->io_base == 0 || sw->io_base > sw->io_limit);
}

static struct pci_iomul_switch *pci_iomul_find_switch_locked(int segment,
							     uint8_t bus)
{
	struct pci_iomul_switch *sw;

	BUG_ON(!mutex_is_locked(&switch_list_lock));
	list_for_each_entry(sw, &switch_list, list) {
		if (sw->segment == segment && sw->bus == bus)
			return sw;
	}
	return NULL;
}

static struct pci_iomul_slot *pci_iomul_find_slot_locked(
	struct pci_iomul_switch *sw, uint8_t busnr, uint8_t dev)
{
	struct pci_iomul_slot *slot;

	BUG_ON(!mutex_is_locked(&sw->lock));
	list_for_each_entry(slot, &sw->slots, sibling) {
		if (slot->bus == busnr && slot->dev == dev)
			return slot;
	}
	return NULL;
}

static void pci_iomul_switch_get(struct pci_iomul_switch *sw);
/* on successfull exit, sw->lock is locked for use slot and
 * refrence count of sw is incremented.
 */
static void pci_iomul_get_lock_switch(struct pci_dev *pdev,
				      struct pci_iomul_switch **swp,
				      struct pci_iomul_slot **slot)
{
	mutex_lock(&switch_list_lock);

	*swp = pci_iomul_find_switch_locked(pci_domain_nr(pdev->bus),
					    pci_dev_switch_busnr(pdev));
	if (*swp == NULL) {
		*slot = NULL;
		goto out;
	}

	mutex_lock(&(*swp)->lock);
	*slot = pci_iomul_find_slot_locked(*swp, pdev->bus->number,
					   PCI_SLOT(pdev->devfn));
	if (*slot == NULL) {
		mutex_unlock(&(*swp)->lock);
		*swp = NULL;
	} else {
		pci_iomul_switch_get(*swp);
	}
out:
	mutex_unlock(&switch_list_lock);
}

static struct pci_iomul_switch *pci_iomul_switch_alloc(int segment,
						       uint8_t bus)
{
	struct pci_iomul_switch *sw;

	BUG_ON(!mutex_is_locked(&switch_list_lock));

	sw = kmalloc(sizeof(*sw), GFP_KERNEL);

	mutex_init(&sw->lock);
	kref_init(&sw->kref);
	sw->io_region = NULL;
	sw->count = 0;
	sw->current_pdev = NULL;
	sw->segment = segment;
	sw->bus = bus;
	sw->io_base = 0;
	sw->io_limit = 0;
	sw->func = NULL;
	INIT_LIST_HEAD(&sw->slots);

	return sw;
}

static void pci_iomul_switch_add_locked(struct pci_iomul_switch *sw)
{
	BUG_ON(!mutex_is_locked(&switch_list_lock));
	list_add(&sw->list, &switch_list);
}

#ifdef CONFIG_HOTPLUG_PCI
static void pci_iomul_switch_del_locked(struct pci_iomul_switch *sw)
{
	BUG_ON(!mutex_is_locked(&switch_list_lock));
	list_del(&sw->list);
}
#endif

static void pci_iomul_switch_get(struct pci_iomul_switch *sw)
{
	kref_get(&sw->kref);
}

static void pci_iomul_switch_release(struct kref *kref)
{
	struct pci_iomul_switch *sw = container_of(kref,
						   struct pci_iomul_switch,
						   kref);
	kfree(sw);
}

static void pci_iomul_switch_put(struct pci_iomul_switch *sw)
{
	kref_put(&sw->kref, &pci_iomul_switch_release);
}

static int __devinit pci_iomul_slot_init(struct pci_dev *pdev,
					 struct pci_iomul_slot *slot)
{
	u16 rpcap;
	u16 cap;

	rpcap = pci_find_capability(pdev, PCI_CAP_ID_EXP);
	if (!rpcap) {
		/* pci device isn't supported */
		printk(KERN_INFO
		       "PCI: sharing io port of non PCIe device %s "
		       "isn't supported. ignoring.\n",
		       pci_name(pdev));
		return -ENOSYS;
	}

        pci_read_config_word(pdev, rpcap + PCI_CAP_FLAGS, &cap);
	switch ((cap & PCI_EXP_FLAGS_TYPE) >> 4) {
	case PCI_EXP_TYPE_RC_END:
		printk(KERN_INFO
		       "PCI: io port sharing of root complex integrated "
		       "endpoint %s isn't supported. ignoring.\n",
		       pci_name(pdev));
		return -ENOSYS;
	case PCI_EXP_TYPE_ENDPOINT:
	case PCI_EXP_TYPE_LEG_END:
		break;
	default:
		printk(KERN_INFO
		       "PCI: io port sharing of non endpoint %s "
		       "doesn't make sense. ignoring.\n",
		       pci_name(pdev));
		return -EINVAL;
	}

	kref_init(&slot->kref);
	slot->switch_busnr = pci_dev_switch_busnr(pdev);
	slot->segment = pci_domain_nr(pdev->bus);
	slot->bus = pdev->bus->number;
	slot->dev = PCI_SLOT(pdev->devfn);

	return 0;
}

static struct pci_iomul_slot *__devinit
pci_iomul_slot_alloc(struct pci_dev *pdev)
{
	struct pci_iomul_slot *slot;

	slot = kzalloc(sizeof(*slot), GFP_KERNEL);
	if (slot == NULL)
		return NULL;

	if (pci_iomul_slot_init(pdev, slot) != 0) {
		kfree(slot);
		return NULL;
	}
	return slot;
}

static void pci_iomul_slot_add_locked(struct pci_iomul_switch *sw,
				      struct pci_iomul_slot *slot)
{
	BUG_ON(!mutex_is_locked(&sw->lock));
	list_add(&slot->sibling, &sw->slots);
}

#ifdef CONFIG_HOTPLUG_PCI
static void pci_iomul_slot_del_locked(struct pci_iomul_switch *sw,
				       struct pci_iomul_slot *slot)
{
	BUG_ON(!mutex_is_locked(&sw->lock));
	list_del(&slot->sibling);
}
#endif

static void pci_iomul_slot_get(struct pci_iomul_slot *slot)
{
	kref_get(&slot->kref);
}

static void pci_iomul_slot_release(struct kref *kref)
{
	struct pci_iomul_slot *slot = container_of(kref, struct pci_iomul_slot,
						   kref);
	kfree(slot);
}

static void pci_iomul_slot_put(struct pci_iomul_slot *slot)
{
	kref_put(&slot->kref, &pci_iomul_slot_release);
}

/*****************************************************************************/
static int pci_get_sbd(const char *str,
		       int *segment__, uint8_t *bus__, uint8_t *dev__)
{
	int segment;
	int bus;
	int dev;

	if (sscanf(str, "%x:%x:%x", &segment, &bus, &dev) != 3) {
		if (sscanf(str, "%x:%x", &bus, &dev) == 2)
			segment = 0;
		else
			return -EINVAL;
	}

	if (segment < 0 || INT_MAX <= segment)
		return -EINVAL;
	if (bus < 0 || PCI_BUS_MAX < bus)
		return -EINVAL;
	if (dev < 0 || PCI_DEV_MAX < dev)
		return -EINVAL;

	*segment__ = segment;
	*bus__ = bus;
	*dev__ = dev;
	return 0;
}

static char iomul_param[COMMAND_LINE_SIZE];
#define TOKEN_MAX	10	/* SSSS:BB:DD length is 10 */
static int pci_is_iomul_dev_param(struct pci_dev *pdev)
{
        int len;
        char *p;
	char *next_str;

	for (p = &iomul_param[0]; *p != '\0'; p = next_str + 1) {
		next_str = strchr(p, ',');
		if (next_str != NULL)
			len = next_str - p;
		else
			len = strlen(p);

		if (len > 0 && len <= TOKEN_MAX) {
			char tmp[TOKEN_MAX+1];
			int seg;
			uint8_t bus;
			uint8_t dev;

			strlcpy(tmp, p, len);
			if (pci_get_sbd(tmp, &seg, &bus, &dev) == 0 &&
			    pci_domain_nr(pdev->bus) == seg &&
			    pdev->bus->number == bus &&
			    PCI_SLOT(pdev->devfn) == dev)
				return 1;
		}
		if (next_str == NULL)
			break;
	}

	/* check guestcev=<device>+iomul option */
	return pci_is_iomuldev(pdev);
}

/*
 * Format: [<segment>:]<bus>:<dev>[,[<segment>:]<bus>:<dev>[,...]
 */
static int __init pci_iomul_param_setup(char *str)
{
	if (strlen(str) >= COMMAND_LINE_SIZE)
		return 0;

	/* parse it after pci bus scanning */
	strlcpy(iomul_param, str, sizeof(iomul_param));
	return 1;
}
__setup("guestiomuldev=", pci_iomul_param_setup);

/*****************************************************************************/
static void __devinit pci_iomul_set_bridge_io_window(struct pci_dev *bridge,
						     uint32_t io_base,
						     uint32_t io_limit)
{
	uint16_t l;
	uint32_t upper16;

	io_base >>= 12;
	io_base <<= 4;
	io_limit >>= 12;
	io_limit <<= 4;
	l = (io_base & 0xff) | ((io_limit & 0xff) << 8);
	upper16 = ((io_base & 0xffff00) >> 8) |
		(((io_limit & 0xffff00) >> 8) << 16);

        /* Temporarily disable the I/O range before updating PCI_IO_BASE. */
        pci_write_config_dword(bridge, PCI_IO_BASE_UPPER16, 0x0000ffff);
        /* Update lower 16 bits of I/O base/limit. */
        pci_write_config_word(bridge, PCI_IO_BASE, l);
        /* Update upper 16 bits of I/O base/limit. */
        pci_write_config_dword(bridge, PCI_IO_BASE_UPPER16, upper16);
}

static void __devinit pci_disable_bridge_io_window(struct pci_dev *bridge)
{
	/* set base = 0xffffff limit = 0x0 */
	pci_iomul_set_bridge_io_window(bridge, 0xffffff, 0);
}

static int __devinit pci_iomul_func_scan(struct pci_dev *pdev,
					 struct pci_iomul_slot *slot,
					 uint8_t func)
{
	struct pci_iomul_func *f;
	unsigned int i;

	f = kzalloc(sizeof(*f), GFP_KERNEL);
	if (f == NULL)
		return -ENOMEM;

	f->segment = slot->segment;
	f->bus = slot->bus;
	f->devfn = PCI_DEVFN(slot->dev, func);
	f->io_size = pdev_size_io(pdev);

	for (i = 0; i < PCI_NUM_BARS; i++) {
		if (!(pci_resource_flags(pdev, i) & IORESOURCE_IO))
			continue;
		if (pci_resource_len(pdev, i) == 0)
			continue;

		f->io_bar |= 1 << i;
		f->resource[i] = pdev->resource[i];
	}

	if (f->io_bar)
		slot->func[func] = f;
	else
		kfree(f);
	return 0;
}

/*
 * This is tricky part.
 * fake PCI resource assignment routines by setting flags to 0.
 * PCI resource allocate routines think the resource should
 * be allocated by checking flags. 0 means this resource isn't used.
 * See pbus_size_io() and pdev_sort_resources().
 *
 * After allocated resources, flags (IORESOURCE_IO) is exported
 * to other part including user process.
 * So we have to set flags to IORESOURCE_IO, but at the same time
 * we must prevent those resources from reassigning when pci hot plug.
 * To achieve that, set r->parent to dummy resource.
 */
static void __devinit pci_iomul_disable_resource(struct resource *r)
{
	/* don't allocate this resource */
	r->flags = 0;
}

static void __devinit pci_iomul_reenable_resource(
	struct resource *dummy_parent, struct resource *r)
{
	int ret;

	dummy_parent->start = r->start;
	dummy_parent->end = r->end;
	dummy_parent->flags = r->flags;
	dummy_parent->name = "PCI IOMUL dummy resource";

	ret = request_resource(dummy_parent, r);
	BUG_ON(ret);
}

static void __devinit pci_iomul_fixup_ioresource(struct pci_dev *pdev,
						 struct pci_iomul_func *func,
						 int reassign, int dealloc)
{
	uint8_t i;
	struct resource *r;

	printk(KERN_INFO "PCI: deallocating io resource[%s]. io size 0x%lx\n",
	       pci_name(pdev), func->io_size);
	for (i = 0; i < PCI_NUM_BARS; i++) {
		r = &pdev->resource[i];
		if (!(func->io_bar & (1 << i)))
			continue;

		if (reassign) {
			r->end -= r->start;
			r->start = 0;
			pci_update_resource(pdev, i);
			func->resource[i] = *r;
		}

		if (dealloc)
			/* don't allocate this resource */
			pci_iomul_disable_resource(r);
	}

	/* parent PCI-PCI bridge */
	if (!reassign)
		return;
	pdev = pdev->bus->self;
	if ((pdev->class >> 8) == PCI_CLASS_BRIDGE_HOST)
		return;
	pci_disable_bridge_io_window(pdev);
	for (i = 0; i < PCI_NUM_RESOURCES; i++) {
		r = &pdev->resource[i];
		if (!(r->flags & IORESOURCE_IO))
			continue;

		r->end -= r->start;
		r->start = 0;
		if (i < PCI_BRIDGE_RESOURCES)
			pci_update_resource(pdev, i);
	}
}

static void __devinit __quirk_iomul_dealloc_ioresource(
	struct pci_iomul_switch *sw,
	struct pci_dev *pdev, struct pci_iomul_slot *slot)
{
	struct pci_iomul_func *f;
	struct pci_iomul_func *__f;

	if (pci_iomul_func_scan(pdev, slot, PCI_FUNC(pdev->devfn)) != 0)
		return;

	f = slot->func[PCI_FUNC(pdev->devfn)];
	if (f == NULL)
		return;

	__f = sw->func;
	/* sw->io_base == 0 means that we are called at boot time.
	 * != 0 means that we are called by php after boot. */
	if (sw->io_base == 0 &&
	    (__f == NULL || __f->io_size < f->io_size)) {
		if (__f != NULL) {
			struct pci_bus *__pbus;
			struct pci_dev *__pdev;

			__pbus = pci_find_bus(__f->segment, __f->bus);
			BUG_ON(__pbus == NULL);
			__pdev = pci_get_slot(__pbus, __f->devfn);
			BUG_ON(__pdev == NULL);
			pci_iomul_fixup_ioresource(__pdev, __f, 0, 1);
			pci_dev_put(__pdev);
		}

		pci_iomul_fixup_ioresource(pdev, f, 1, 0);
		sw->func = f;
	} else {
		pci_iomul_fixup_ioresource(pdev, f, 1, 1);
	}
}

static void __devinit quirk_iomul_dealloc_ioresource(struct pci_dev *pdev)
{
	struct pci_iomul_switch *sw;
	struct pci_iomul_slot *slot;

	if (pdev->hdr_type != PCI_HEADER_TYPE_NORMAL)
		return;
	if ((pdev->class >> 8) == PCI_CLASS_BRIDGE_HOST)
		return;	/* PCI Host Bridge isn't a target device */
	if (!pci_is_iomul_dev_param(pdev))
		return;

	mutex_lock(&switch_list_lock);
	sw = pci_iomul_find_switch_locked(pci_domain_nr(pdev->bus),
					  pci_dev_switch_busnr(pdev));
	if (sw == NULL) {
		sw = pci_iomul_switch_alloc(pci_domain_nr(pdev->bus),
					    pci_dev_switch_busnr(pdev));
		if (sw == NULL) {
			mutex_unlock(&switch_list_lock);
			printk(KERN_WARNING
			       "PCI: can't allocate memory "
			       "for sw of IO mulplexing %s", pci_name(pdev));
			return;
		}
		pci_iomul_switch_add_locked(sw);
	}
	pci_iomul_switch_get(sw);
	mutex_unlock(&switch_list_lock);

	mutex_lock(&sw->lock);
	slot = pci_iomul_find_slot_locked(sw, pdev->bus->number,
					  PCI_SLOT(pdev->devfn));
	if (slot == NULL) {
		slot = pci_iomul_slot_alloc(pdev);
		if (slot == NULL) {
			mutex_unlock(&sw->lock);
			pci_iomul_switch_put(sw);
			printk(KERN_WARNING "PCI: can't allocate memory "
			       "for IO mulplexing %s", pci_name(pdev));
			return;
		}
		pci_iomul_slot_add_locked(sw, slot);
	}

	printk(KERN_INFO "PCI: disable device and release io resource[%s].\n",
	       pci_name(pdev));
	pci_disable_device(pdev);

	__quirk_iomul_dealloc_ioresource(sw, pdev, slot);

	mutex_unlock(&sw->lock);
	pci_iomul_switch_put(sw);
}
DECLARE_PCI_FIXUP_HEADER(PCI_ANY_ID, PCI_ANY_ID,
			 quirk_iomul_dealloc_ioresource);

static void __devinit pci_iomul_read_bridge_io(struct pci_iomul_switch *sw)
{
	struct pci_iomul_func *f = sw->func;

	struct pci_bus *pbus;
	struct pci_dev *pdev;
	struct pci_dev *bridge;

	uint16_t l;
	uint16_t base_upper16;
	uint16_t limit_upper16;
	uint32_t io_base;
	uint32_t io_limit;

	pbus = pci_find_bus(f->segment, f->bus);
	BUG_ON(pbus == NULL);

	pdev = pci_get_slot(pbus, f->devfn);
	BUG_ON(pdev == NULL);

	bridge = pdev->bus->self;
	pci_read_config_word(bridge, PCI_IO_BASE, &l);
	pci_read_config_word(bridge, PCI_IO_BASE_UPPER16, &base_upper16);
	pci_read_config_word(bridge, PCI_IO_LIMIT_UPPER16, &limit_upper16);

	io_base = (l & 0xf0) | ((uint32_t)base_upper16 << 8);
	io_base <<= 8;
	io_limit = (l >> 8) | ((uint32_t)limit_upper16 << 8);
	io_limit <<= 8;
	io_limit |= 0xfff;

	sw->io_base = io_base;
	sw->io_limit = io_limit;

	pci_dev_put(pdev);
	printk(KERN_INFO "PCI: bridge %s base 0x%x limit 0x%x\n",
	       pci_name(bridge), sw->io_base, sw->io_limit);
}

static void __devinit pci_iomul_setup_brige(struct pci_dev *bridge,
					    uint32_t io_base,
					    uint32_t io_limit)
{
	uint16_t cmd;

	if ((bridge->class >> 8) == PCI_CLASS_BRIDGE_HOST)
		return;

	pci_iomul_set_bridge_io_window(bridge, io_base, io_limit);

	/* and forcibly enables IO */
	pci_read_config_word(bridge, PCI_COMMAND, &cmd);
	if (!(cmd & PCI_COMMAND_IO)) {
		cmd |= PCI_COMMAND_IO;
                printk(KERN_INFO "PCI: Forcibly Enabling IO %s\n",
		       pci_name(bridge));
                pci_write_config_word(bridge, PCI_COMMAND, cmd);
	}
}

struct __bar {
	unsigned long size;
	uint8_t bar;
};

/* decending order */
static int __devinit pci_iomul_bar_cmp(const void *lhs__, const void *rhs__)
{
	const struct __bar *lhs = (struct __bar*)lhs__;
	const struct __bar *rhs = (struct __bar*)rhs__;
	return - (lhs->size - rhs->size);
}

static void __devinit pci_iomul_setup_dev(struct pci_dev *pdev,
					  struct pci_iomul_func *f,
					  uint32_t io_base)
{
	struct __bar bars[PCI_NUM_BARS];
	int i;
	uint8_t num_bars = 0;
	struct resource *r;

	printk(KERN_INFO "PCI: Forcibly assign IO %s from 0x%x\n",
	       pci_name(pdev), io_base);

	for (i = 0; i < PCI_NUM_BARS; i++) {
		if (!(f->io_bar & (1 << i)))
			continue;

		r = &f->resource[i];
		bars[num_bars].size = pci_iomul_len(r);
		bars[num_bars].bar = i;

		num_bars++;
	}

	sort(bars, num_bars, sizeof(bars[0]), &pci_iomul_bar_cmp, NULL);

	for (i = 0; i < num_bars; i++) {
		struct resource *fr = &f->resource[bars[i].bar];
		r = &pdev->resource[bars[i].bar];

		BUG_ON(r->start != 0);
		r->start += io_base;
		r->end += io_base;

		fr->start = r->start;
		fr->end = r->end;

		/* pci_update_resource() check flags. */
		r->flags = fr->flags;
		pci_update_resource(pdev, bars[i].bar);
		pci_iomul_reenable_resource(&f->dummy_parent, r);

		io_base += bars[i].size;
	}
}

static void __devinit pci_iomul_release_io_resource(
	struct pci_dev *pdev, struct pci_iomul_switch *sw,
	struct pci_iomul_slot *slot, struct pci_iomul_func *f)
{
	int i;
	struct resource *r;

	for (i = 0; i < PCI_NUM_BARS; i++) {
		if (pci_resource_flags(pdev, i) & IORESOURCE_IO &&
		    pdev->resource[i].parent != NULL) {
			r = &pdev->resource[i];
			f->resource[i] = *r;
			release_resource(r);
			pci_iomul_reenable_resource(&f->dummy_parent, r);
		}
	}

	/* parent PCI-PCI bridge */
	pdev = pdev->bus->self;
	if ((pdev->class >> 8) != PCI_CLASS_BRIDGE_HOST) {
		for (i = PCI_BRIDGE_RESOURCES; i < PCI_NUM_RESOURCES; i++) {
			struct resource *parent = pdev->resource[i].parent;

			if (pci_resource_flags(pdev, i) & IORESOURCE_IO &&
			    parent != NULL) {
				r = &pdev->resource[i];

				sw->io_resource.flags = r->flags;
				sw->io_resource.start = sw->io_base;
				sw->io_resource.end = sw->io_limit;
				sw->io_resource.name = "PCI IO Multiplexer";

				release_resource(r);
				pci_iomul_reenable_resource(
					&slot->dummy_parent[i - PCI_BRIDGE_RESOURCES], r);

				if (request_resource(parent,
						     &sw->io_resource))
					printk(KERN_ERR
					       "PCI IOMul: can't allocate "
					       "resource. [0x%x, 0x%x]",
					       sw->io_base, sw->io_limit);
			}
		}
	}
}

static void __devinit quirk_iomul_reassign_ioresource(struct pci_dev *pdev)
{
	struct pci_iomul_switch *sw;
	struct pci_iomul_slot *slot;
	struct pci_iomul_func *sf;
	struct pci_iomul_func *f;

	pci_iomul_get_lock_switch(pdev, &sw, &slot);
	if (sw == NULL || slot == NULL)
		return;

	if (sw->io_base == 0)
		pci_iomul_read_bridge_io(sw);
	if (!pci_iomul_switch_io_allocated(sw))
		goto out;

	sf = sw->func;
	f = slot->func[PCI_FUNC(pdev->devfn)];
	if (f == NULL)
		/* (sf == NULL || f == NULL) case
		 * can happen when all the specified devices
		 * don't have io space
		 */
		goto out;

	if (sf != NULL &&
	    (pci_domain_nr(pdev->bus) != sf->segment ||
	     pdev->bus->number != sf->bus ||
	     PCI_SLOT(pdev->devfn) != PCI_SLOT(sf->devfn)) &&
	    PCI_FUNC(pdev->devfn) == 0) {
		pci_iomul_setup_brige(pdev->bus->self,
				      sw->io_base, sw->io_limit);
	}

	BUG_ON(f->io_size > sw->io_limit - sw->io_base + 1);
	if (/* f == sf */
            sf != NULL &&
	    pci_domain_nr(pdev->bus) == sf->segment &&
	    pdev->bus->number == sf->bus &&
	    pdev->devfn == sf->devfn)
		pci_iomul_release_io_resource(pdev, sw, slot, f);
	else
		pci_iomul_setup_dev(pdev, f, sw->io_base);

out:
	mutex_unlock(&sw->lock);
	pci_iomul_switch_put(sw);
}

DECLARE_PCI_FIXUP_FINAL(PCI_ANY_ID, PCI_ANY_ID,
			quirk_iomul_reassign_ioresource);

/*****************************************************************************/
#ifdef CONFIG_HOTPLUG_PCI
static int __devinit __pci_iomul_notifier_del_device(struct pci_dev *pdev)
{
	struct pci_iomul_switch *sw;
	struct pci_iomul_slot *slot;
	int i;

	pci_iomul_get_lock_switch(pdev, &sw, &slot);
	if (sw == NULL || slot == NULL)
		return 0;

	if (sw->func == slot->func[PCI_FUNC(pdev->devfn)])
		sw->func = NULL;
	kfree(slot->func[PCI_FUNC(pdev->devfn)]);
	slot->func[PCI_FUNC(pdev->devfn)] = NULL;
	for (i = 0; i < PCI_NUM_FUNC; i++) {
		if (slot->func[i] != NULL)
			goto out;
	}

	pci_iomul_slot_del_locked(sw, slot);
	pci_iomul_slot_put(slot);

out:
	mutex_unlock(&sw->lock);
	pci_iomul_switch_put(sw);
	return 0;
}

static int __devinit __pci_iomul_notifier_del_switch(struct pci_dev *pdev)
{
	struct pci_iomul_switch *sw;

	mutex_lock(&switch_list_lock);
	sw = pci_iomul_find_switch_locked(pci_domain_nr(pdev->bus),
					  pdev->bus->number);
	if (sw == NULL)
		goto out;

	pci_iomul_switch_del_locked(sw);

	mutex_lock(&sw->lock);
	if (sw->io_resource.parent)
		release_resource(&sw->io_resource);
	sw->io_base = 0;	/* to tell this switch is removed */
	sw->io_limit = 0;
	BUG_ON(!list_empty(&sw->slots));
	mutex_unlock(&sw->lock);

out:
	mutex_unlock(&switch_list_lock);
	pci_iomul_switch_put(sw);
	return 0;
}

static int __devinit pci_iomul_notifier_del_device(struct pci_dev *pdev)
{
	int ret;
	switch (pdev->hdr_type) {
	case PCI_HEADER_TYPE_NORMAL:
		ret = __pci_iomul_notifier_del_device(pdev);
		break;
	case PCI_HEADER_TYPE_BRIDGE:
		ret = __pci_iomul_notifier_del_switch(pdev);
		break;
	default:
                printk(KERN_WARNING "PCI IOMUL: "
		       "device %s has unknown header type %02x, ignoring.\n",
		       pci_name(pdev), pdev->hdr_type);
		ret = -EIO;
		break;
	}
	return ret;
}

static int __devinit pci_iomul_notifier(struct notifier_block *nb,
					unsigned long action, void *data)
{
        struct device *dev = data;
        struct pci_dev *pdev = to_pci_dev(dev);

        switch (action) {
	case BUS_NOTIFY_ADD_DEVICE:
		quirk_iomul_reassign_ioresource(pdev);
		break;
	case BUS_NOTIFY_DEL_DEVICE:
		return pci_iomul_notifier_del_device(pdev);
	default:
		/* nothing */
		break;
	}

	return 0;
}

static struct notifier_block pci_iomul_nb = {
        .notifier_call = pci_iomul_notifier,
};

static int __init pci_iomul_hotplug_init(void)
{
        bus_register_notifier(&pci_bus_type, &pci_iomul_nb);
	return 0;
}

late_initcall(pci_iomul_hotplug_init);
#endif

/*****************************************************************************/
struct pci_iomul_data {
	struct mutex lock;

	struct pci_dev *pdev;
	struct pci_iomul_switch *sw;
	struct pci_iomul_slot *slot;	/* slot::kref */
	struct pci_iomul_func **func;	/* when dereferencing,
					   sw->lock is necessary */
};

static int pci_iomul_func_ioport(struct pci_iomul_func *func,
				 uint8_t bar, uint64_t offset, int *port)
{
	if (!(func->io_bar & (1 << bar)))
		return -EINVAL;

	*port = func->resource[bar].start + offset;
	if (*port < func->resource[bar].start ||
	    *port > func->resource[bar].end)
		return -EINVAL;

	return 0;
}

static inline int pci_iomul_valid(struct pci_iomul_data *iomul)
{
	BUG_ON(!mutex_is_locked(&iomul->lock));
	BUG_ON(!mutex_is_locked(&iomul->sw->lock));
	return pci_iomul_switch_io_allocated(iomul->sw) &&
		*iomul->func != NULL;
}

static void __pci_iomul_enable_io(struct pci_dev *pdev)
{
	uint16_t cmd;

	pci_dev_get(pdev);
	pci_read_config_word(pdev, PCI_COMMAND, &cmd);
	cmd |= PCI_COMMAND_IO;
	pci_write_config_word(pdev, PCI_COMMAND, cmd);
}

static void __pci_iomul_disable_io(struct pci_iomul_data *iomul,
				   struct pci_dev *pdev)
{
	uint16_t cmd;

	if (!pci_iomul_valid(iomul))
		return;

	pci_read_config_word(pdev, PCI_COMMAND, &cmd);
	cmd &= ~PCI_COMMAND_IO;
	pci_write_config_word(pdev, PCI_COMMAND, cmd);
	pci_dev_put(pdev);
}

static int pci_iomul_open(struct inode *inode, struct file *filp)
{
	struct pci_iomul_data *iomul;
	iomul = kmalloc(sizeof(*iomul), GFP_KERNEL);
	if (iomul == NULL)
		return -ENOMEM;

	mutex_init(&iomul->lock);
	iomul->pdev = NULL;
	iomul->sw = NULL;
	iomul->slot = NULL;
	iomul->func = NULL;
	filp->private_data = (void*)iomul;

	return 0;
}

static int pci_iomul_release(struct inode *inode, struct file *filp)
{
	struct pci_iomul_data *iomul =
		(struct pci_iomul_data*)filp->private_data;
	struct pci_iomul_switch *sw;
	struct pci_iomul_slot *slot = NULL;

	mutex_lock(&iomul->lock);
	sw = iomul->sw;
	slot = iomul->slot;
	if (iomul->pdev != NULL) {
		if (sw != NULL) {
			mutex_lock(&sw->lock);
			if (sw->current_pdev == iomul->pdev) {
				__pci_iomul_disable_io(iomul,
						       sw->current_pdev);
				sw->current_pdev = NULL;
			}
			sw->count--;
			if (sw->count == 0) {
				release_region(sw->io_region->start, sw->io_region->end - sw->io_region->start + 1);
				sw->io_region = NULL;
			}
			mutex_unlock(&sw->lock);
		}
		pci_dev_put(iomul->pdev);
	}
	mutex_unlock(&iomul->lock);

	if (slot != NULL)
		pci_iomul_slot_put(slot);
	if (sw != NULL)
		pci_iomul_switch_put(sw);
	kfree(iomul);
	return 0;
}

static long pci_iomul_setup(struct pci_iomul_data *iomul,
			    struct pci_iomul_setup __user *arg)
{
	long error = 0;
	struct pci_iomul_setup setup;
	struct pci_iomul_switch *sw = NULL;
	struct pci_iomul_slot *slot;
	struct pci_bus *pbus;
	struct pci_dev *pdev;

	if (copy_from_user(&setup, arg, sizeof(setup)))
		return -EFAULT;

	pbus = pci_find_bus(setup.segment, setup.bus);
	if (pbus == NULL)
		return -ENODEV;
	pdev = pci_get_slot(pbus, setup.dev);
	if (pdev == NULL)
		return -ENODEV;

	mutex_lock(&iomul->lock);
	if (iomul->sw != NULL) {
		error = -EBUSY;
		goto out0;
	}

	pci_iomul_get_lock_switch(pdev, &sw, &slot);
	if (sw == NULL || slot == NULL) {
		error = -ENODEV;
		goto out0;
	}
	if (!pci_iomul_switch_io_allocated(sw)) {
		error = -ENODEV;
		goto out;
	}

	if (slot->func[setup.func] == NULL) {
		error = -ENODEV;
		goto out;
	}

	if (sw->count == 0) {
		BUG_ON(sw->io_region != NULL);
		sw->io_region =
			request_region(sw->io_base,
				       sw->io_limit - sw->io_base + 1,
				       "PCI IO Multiplexer driver");
		if (sw->io_region == NULL) {
			mutex_unlock(&sw->lock);
			error = -EBUSY;
			goto out;
		}
	}
	sw->count++;
	pci_iomul_slot_get(slot);

	iomul->pdev = pdev;
	iomul->sw = sw;
	iomul->slot = slot;
	iomul->func = &slot->func[setup.func];

out:
	mutex_unlock(&sw->lock);
out0:
	mutex_unlock(&iomul->lock);
	if (error != 0) {
		if (sw != NULL)
			pci_iomul_switch_put(sw);
		pci_dev_put(pdev);
	}
	return error;
}

static int pci_iomul_lock(struct pci_iomul_data *iomul,
			  struct pci_iomul_switch **sw,
			  struct pci_iomul_func **func)
{
	mutex_lock(&iomul->lock);
	*sw = iomul->sw;
	if (*sw == NULL) {
		mutex_unlock(&iomul->lock);
		return -ENODEV;
	}
	mutex_lock(&(*sw)->lock);
	if (!pci_iomul_valid(iomul)) {
		mutex_unlock(&(*sw)->lock);
		mutex_unlock(&iomul->lock);
		return -ENODEV;
	}
	*func = *iomul->func;

	return 0;
}

static long pci_iomul_disable_io(struct pci_iomul_data *iomul)
{
	long error = 0;
	struct pci_iomul_switch *sw;
	struct pci_iomul_func *dummy_func;
	struct pci_dev *pdev;

	if (pci_iomul_lock(iomul, &sw, &dummy_func) < 0)
		return -ENODEV;

	pdev = iomul->pdev;
	if (pdev == NULL)
		error = -ENODEV;

	if (pdev != NULL && sw->current_pdev == pdev) {
		__pci_iomul_disable_io(iomul, pdev);
		sw->current_pdev = NULL;
	}

	mutex_unlock(&sw->lock);
	mutex_unlock(&iomul->lock);
	return error;
}

static void pci_iomul_switch_to(
	struct pci_iomul_data *iomul, struct pci_iomul_switch *sw,
	struct pci_dev *next_pdev)
{
	if (sw->current_pdev == next_pdev)
		/* nothing to do */
		return;

	if (sw->current_pdev != NULL)
		__pci_iomul_disable_io(iomul, sw->current_pdev);

	__pci_iomul_enable_io(next_pdev);
	sw->current_pdev = next_pdev;
}

static long pci_iomul_in(struct pci_iomul_data *iomul,
			 struct pci_iomul_in __user *arg)
{
	struct pci_iomul_in in;
	struct pci_iomul_switch *sw;
	struct pci_iomul_func *func;

	long error = 0;
	int port;
	uint32_t value = 0;

	if (copy_from_user(&in, arg, sizeof(in)))
		return -EFAULT;

	if (pci_iomul_lock(iomul, &sw, &func) < 0)
		return -ENODEV;

	error = pci_iomul_func_ioport(func, in.bar, in.offset, &port);
	if (error)
		goto out;

	pci_iomul_switch_to(iomul, sw, iomul->pdev);
	switch (in.size) {
	case 4:
		value = inl(port);
		break;
	case 2:
		value = inw(port);
		break;
	case 1:
		value = inb(port);
		break;
	default:
		error = -EINVAL;
		break;
	}

out:
	mutex_unlock(&sw->lock);
	mutex_unlock(&iomul->lock);

	if (error == 0 && put_user(value, &arg->value))
		return -EFAULT;
	return error;
}

static long pci_iomul_out(struct pci_iomul_data *iomul,
			  struct pci_iomul_out __user *arg)
{
	struct pci_iomul_in out;
	struct pci_iomul_switch *sw;
	struct pci_iomul_func *func;

	long error = 0;
	int port;

	if (copy_from_user(&out, arg, sizeof(out)))
		return -EFAULT;

	if (pci_iomul_lock(iomul, &sw, &func) < 0)
		return -ENODEV;

	error = pci_iomul_func_ioport(func, out.bar, out.offset, &port);
	if (error)
		goto out;

	pci_iomul_switch_to(iomul, sw, iomul->pdev);
	switch (out.size) {
	case 4:
		outl(out.value, port);
		break;
	case 2:
		outw(out.value, port);
		break;
	case 1:
		outb(out.value, port);
		break;
	default:
		error = -EINVAL;
		break;
	}

out:
	mutex_unlock(&sw->lock);
	mutex_unlock(&iomul->lock);
	return error;
}

static long pci_iomul_ioctl(struct file *filp,
			    unsigned int cmd, unsigned long arg)
{
	long error;
	struct pci_iomul_data *iomul =
		(struct pci_iomul_data*)filp->private_data;

	if (!capable(CAP_SYS_ADMIN) || !capable(CAP_SYS_RAWIO))
		return -EPERM;

	switch (cmd) {
	case PCI_IOMUL_SETUP:
		error = pci_iomul_setup(iomul,
					(struct pci_iomul_setup __user *)arg);
		break;
	case PCI_IOMUL_DISABLE_IO:
		error = pci_iomul_disable_io(iomul);
		break;
	case PCI_IOMUL_IN:
		error = pci_iomul_in(iomul, (struct pci_iomul_in __user *)arg);
		break;
	case PCI_IOMUL_OUT:
		error = pci_iomul_out(iomul,
				      (struct pci_iomul_out __user *)arg);
		break;
	default:
		error = -ENOSYS;
		break;
	}

	return error;
}

static const struct file_operations pci_iomul_fops = {
	.owner = THIS_MODULE,

	.open = pci_iomul_open, /* nonseekable_open */
	.release = pci_iomul_release,

	.unlocked_ioctl = pci_iomul_ioctl,
};

static struct miscdevice pci_iomul_miscdev = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = "pci_iomul",
	.fops = &pci_iomul_fops,
};

static int pci_iomul_init(void)
{
	int error;
	error = misc_register(&pci_iomul_miscdev);
	if (error != 0) {
		printk(KERN_ALERT "Couldn't register /dev/misc/pci_iomul");
		return error;
	}
	printk("PCI IO multiplexer device installed.\n");
	return 0;
}

#if 0
static void pci_iomul_cleanup(void)
{
	misc_deregister(&pci_iomul_miscdev);
}
#endif

/*
 * This must be called after pci fixup final which is called by
 * device_initcall(pci_init).
 */
late_initcall(pci_iomul_init);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Isaku Yamahata <yamahata@valinux.co.jp>");
MODULE_DESCRIPTION("PCI IO space multiplexing driver");
