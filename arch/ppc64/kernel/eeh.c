/*
 * eeh.c
 * Copyright (C) 2001 Dave Engebretsen & Todd Inglett IBM Corporation
 * 
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
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 */

#include <linux/init.h>
#include <linux/pci.h>
#include <linux/proc_fs.h>
#include <linux/bootmem.h>
#include <linux/mm.h>
#include <linux/rbtree.h>
#include <linux/spinlock.h>
#include <linux/seq_file.h>
#include <asm/paca.h>
#include <asm/processor.h>
#include <asm/naca.h>
#include <asm/io.h>
#include <asm/machdep.h>
#include <asm/pgtable.h>
#include "pci.h"

#undef DEBUG

#define BUID_HI(buid) ((buid) >> 32)
#define BUID_LO(buid) ((buid) & 0xffffffff)
#define CONFIG_ADDR(busno, devfn) \
		(((((busno) & 0xff) << 8) | ((devfn) & 0xf8)) << 8)

/* RTAS tokens */
static int ibm_set_eeh_option;
static int ibm_set_slot_reset;
static int ibm_read_slot_reset_state;

static int eeh_subsystem_enabled;
#define EEH_MAX_OPTS 4096
static char *eeh_opts;
static int eeh_opts_last;

/* System monitoring statistics */
static DEFINE_PER_CPU(unsigned long, total_mmio_ffs);
static DEFINE_PER_CPU(unsigned long, false_positives);
static DEFINE_PER_CPU(unsigned long, ignored_failures);

static int eeh_check_opts_config(struct device_node *dn, int class_code,
				 int vendor_id, int device_id,
				 int default_state);

/**
 * The pci address cache subsystem.  This subsystem places
 * PCI device address resources into a red-black tree, sorted
 * according to the address range, so that given only an i/o
 * address, the corresponding PCI device can be **quickly**
 * found.
 *
 * Currently, the only customer of this code is the EEH subsystem;
 * thus, this code has been somewhat tailored to suit EEH better.
 * In particular, the cache does *not* hold the addresses of devices
 * for which EEH is not enabled.
 *
 * (Implementation Note: The RB tree seems to be better/faster
 * than any hash algo I could think of for this problem, even
 * with the penalty of slow pointer chases for d-cache misses).
 */
struct pci_io_addr_range
{
	struct rb_node rb_node;
	unsigned long addr_lo;
	unsigned long addr_hi;
	struct pci_dev *pcidev;
	unsigned int flags;
};

static struct pci_io_addr_cache
{
	struct rb_root rb_root;
	spinlock_t piar_lock;
} pci_io_addr_cache_root;

static inline struct pci_dev *__pci_get_device_by_addr(unsigned long addr)
{
	struct rb_node *n = pci_io_addr_cache_root.rb_root.rb_node;

	while (n) {
		struct pci_io_addr_range *piar;
		piar = rb_entry(n, struct pci_io_addr_range, rb_node);

		if (addr < piar->addr_lo) {
			n = n->rb_left;
		} else {
			if (addr > piar->addr_hi) {
				n = n->rb_right;
			} else {
				pci_dev_get(piar->pcidev);
				return piar->pcidev;
			}
		}
	}

	return NULL;
}

/**
 * pci_get_device_by_addr - Get device, given only address
 * @addr: mmio (PIO) phys address or i/o port number
 *
 * Given an mmio phys address, or a port number, find a pci device
 * that implements this address.  Be sure to pci_dev_put the device
 * when finished.  I/O port numbers are assumed to be offset
 * from zero (that is, they do *not* have pci_io_addr added in).
 * It is safe to call this function within an interrupt.
 */
static struct pci_dev *pci_get_device_by_addr(unsigned long addr)
{
	struct pci_dev *dev;
	unsigned long flags;

	spin_lock_irqsave(&pci_io_addr_cache_root.piar_lock, flags);
	dev = __pci_get_device_by_addr(addr);
	spin_unlock_irqrestore(&pci_io_addr_cache_root.piar_lock, flags);
	return dev;
}

#ifdef DEBUG
/*
 * Handy-dandy debug print routine, does nothing more
 * than print out the contents of our addr cache.
 */
static void pci_addr_cache_print(struct pci_io_addr_cache *cache)
{
	struct rb_node *n;
	int cnt = 0;

	n = rb_first(&cache->rb_root);
	while (n) {
		struct pci_io_addr_range *piar;
		piar = rb_entry(n, struct pci_io_addr_range, rb_node);
		printk(KERN_DEBUG "PCI: %s addr range %d [%lx-%lx]: %s %s\n",
		       (piar->flags & IORESOURCE_IO) ? "i/o" : "mem", cnt,
		       piar->addr_lo, piar->addr_hi, pci_name(piar->pcidev),
		       pci_pretty_name(piar->pcidev));
		cnt++;
		n = rb_next(n);
	}
}
#endif

/* Insert address range into the rb tree. */
static struct pci_io_addr_range *
pci_addr_cache_insert(struct pci_dev *dev, unsigned long alo,
		      unsigned long ahi, unsigned int flags)
{
	struct rb_node **p = &pci_io_addr_cache_root.rb_root.rb_node;
	struct rb_node *parent = NULL;
	struct pci_io_addr_range *piar;

	/* Walk tree, find a place to insert into tree */
	while (*p) {
		parent = *p;
		piar = rb_entry(parent, struct pci_io_addr_range, rb_node);
		if (alo < piar->addr_lo) {
			p = &parent->rb_left;
		} else if (ahi > piar->addr_hi) {
			p = &parent->rb_right;
		} else {
			if (dev != piar->pcidev ||
			    alo != piar->addr_lo || ahi != piar->addr_hi) {
				printk(KERN_WARNING "PIAR: overlapping address range\n");
			}
			return piar;
		}
	}
	piar = (struct pci_io_addr_range *)kmalloc(sizeof(struct pci_io_addr_range), GFP_ATOMIC);
	if (!piar)
		return NULL;

	piar->addr_lo = alo;
	piar->addr_hi = ahi;
	piar->pcidev = dev;
	piar->flags = flags;

	rb_link_node(&piar->rb_node, parent, p);
	rb_insert_color(&piar->rb_node, &pci_io_addr_cache_root.rb_root);

	return piar;
}

static void __pci_addr_cache_insert_device(struct pci_dev *dev)
{
	struct device_node *dn;
	int i;

	dn = pci_device_to_OF_node(dev);
	if (!dn) {
		printk(KERN_WARNING "PCI: no pci dn found for dev=%s %s\n",
			pci_name(dev), pci_pretty_name(dev));
		pci_dev_put(dev);
		return;
	}

	/* Skip any devices for which EEH is not enabled. */
	if (!(dn->eeh_mode & EEH_MODE_SUPPORTED) ||
	    dn->eeh_mode & EEH_MODE_NOCHECK) {
#ifdef DEBUG
		printk(KERN_INFO "PCI: skip building address cache for=%s %s\n",
		       pci_name(dev), pci_pretty_name(dev));
#endif
		pci_dev_put(dev);
		return;
	}

	/* Walk resources on this device, poke them into the tree */
	for (i = 0; i < DEVICE_COUNT_RESOURCE; i++) {
		unsigned long start = pci_resource_start(dev,i);
		unsigned long end = pci_resource_end(dev,i);
		unsigned int flags = pci_resource_flags(dev,i);

		/* We are interested only bus addresses, not dma or other stuff */
		if (0 == (flags & (IORESOURCE_IO | IORESOURCE_MEM)))
			continue;
		if (start == 0 || ~start == 0 || end == 0 || ~end == 0)
			 continue;
		pci_addr_cache_insert(dev, start, end, flags);
	}
}

/**
 * pci_addr_cache_insert_device - Add a device to the address cache
 * @dev: PCI device whose I/O addresses we are interested in.
 *
 * In order to support the fast lookup of devices based on addresses,
 * we maintain a cache of devices that can be quickly searched.
 * This routine adds a device to that cache.
 */
void pci_addr_cache_insert_device(struct pci_dev *dev)
{
	unsigned long flags;

	spin_lock_irqsave(&pci_io_addr_cache_root.piar_lock, flags);
	__pci_addr_cache_insert_device(dev);
	spin_unlock_irqrestore(&pci_io_addr_cache_root.piar_lock, flags);
}

static inline void __pci_addr_cache_remove_device(struct pci_dev *dev)
{
	struct rb_node *n;

restart:
	n = rb_first(&pci_io_addr_cache_root.rb_root);
	while (n) {
		struct pci_io_addr_range *piar;
		piar = rb_entry(n, struct pci_io_addr_range, rb_node);

		if (piar->pcidev == dev) {
			rb_erase(n, &pci_io_addr_cache_root.rb_root);
			kfree(piar);
			goto restart;
		}
		n = rb_next(n);
	}
	pci_dev_put(dev);
}

/**
 * pci_addr_cache_remove_device - remove pci device from addr cache
 * @dev: device to remove
 *
 * Remove a device from the addr-cache tree.
 * This is potentially expensive, since it will walk
 * the tree multiple times (once per resource).
 * But so what; device removal doesn't need to be that fast.
 */
void pci_addr_cache_remove_device(struct pci_dev *dev)
{
	unsigned long flags;

	spin_lock_irqsave(&pci_io_addr_cache_root.piar_lock, flags);
	__pci_addr_cache_remove_device(dev);
	spin_unlock_irqrestore(&pci_io_addr_cache_root.piar_lock, flags);
}

/**
 * pci_addr_cache_build - Build a cache of I/O addresses
 *
 * Build a cache of pci i/o addresses.  This cache will be used to
 * find the pci device that corresponds to a given address.
 * This routine scans all pci busses to build the cache.
 * Must be run late in boot process, after the pci controllers
 * have been scaned for devices (after all device resources are known).
 */
void __init pci_addr_cache_build(void)
{
	struct pci_dev *dev = NULL;

	spin_lock_init(&pci_io_addr_cache_root.piar_lock);

	while ((dev = pci_get_device(PCI_ANY_ID, PCI_ANY_ID, dev)) != NULL) {
		/* Ignore PCI bridges ( XXX why ??) */
		if ((dev->class >> 16) == PCI_BASE_CLASS_BRIDGE) {
			pci_dev_put(dev);
			continue;
		}
		pci_addr_cache_insert_device(dev);
	}

#ifdef DEBUG
	/* Verify tree built up above, echo back the list of addrs. */
	pci_addr_cache_print(&pci_io_addr_cache_root);
#endif
}

/**
 * eeh_token_to_phys - convert EEH address token to phys address
 * @token i/o token, should be address in the form 0xA....
 *
 * Converts EEH address tokens into physical addresses.  Note that
 * ths routine does *not* convert I/O BAR addresses (which start
 * with 0xE...) to phys addresses!
 */
static unsigned long eeh_token_to_phys(unsigned long token)
{
	pte_t *ptep;
	unsigned long pa, vaddr;

	if (REGION_ID(token) == EEH_REGION_ID)
		vaddr = IO_TOKEN_TO_ADDR(token);
	else
		return token;

	ptep = find_linux_pte(ioremap_mm.pgd, vaddr);
	pa = pte_pfn(*ptep) << PAGE_SHIFT;

	return pa | (vaddr & (PAGE_SIZE-1));
}

/**
 * eeh_check_failure - check if all 1's data is due to EEH slot freeze
 * @token i/o token, should be address in the form 0xA....
 * @val value, should be all 1's (XXX why do we need this arg??)
 *
 * Check for an eeh failure at the given token address.
 * The given value has been read and it should be 1's (0xff, 0xffff or
 * 0xffffffff).
 *
 * Probe to determine if an error actually occurred.  If not return val.
 * Otherwise panic.
 *
 * Note this routine might be called in an interrupt context ...
 */
unsigned long eeh_check_failure(void *token, unsigned long val)
{
	unsigned long addr;
	struct pci_dev *dev;
	struct device_node *dn;
	unsigned long ret, rets[2];
	static spinlock_t lock = SPIN_LOCK_UNLOCKED;
	/* dont want this on the stack */
	static unsigned char slot_err_buf[RTAS_ERROR_LOG_MAX];
	unsigned long flags;

	__get_cpu_var(total_mmio_ffs)++;

	if (!eeh_subsystem_enabled)
		return val;

	/* Finding the phys addr + pci device; this is pretty quick. */
	addr = eeh_token_to_phys((unsigned long)token);
	dev = pci_get_device_by_addr(addr);
	if (!dev)
		return val;

	dn = pci_device_to_OF_node(dev);
	if (!dn) {
		pci_dev_put(dev);
		return val;
	}

	/* Access to IO BARs might get this far and still not want checking. */
	if (!(dn->eeh_mode & EEH_MODE_SUPPORTED) ||
	    dn->eeh_mode & EEH_MODE_NOCHECK) {
		pci_dev_put(dev);
		return val;
	}

        /* Make sure we aren't ISA */
        if (!strcmp(dn->type, "isa")) {
                pci_dev_put(dev);
                return val;
        }

	if (!dn->eeh_config_addr) {
		pci_dev_put(dev);
		return val;
	}

	/*
	 * Now test for an EEH failure.  This is VERY expensive.
	 * Note that the eeh_config_addr may be a parent device
	 * in the case of a device behind a bridge, or it may be
	 * function zero of a multi-function device.
	 * In any case they must share a common PHB.
	 */
	ret = rtas_call(ibm_read_slot_reset_state, 3, 3, rets,
			dn->eeh_config_addr, BUID_HI(dn->phb->buid),
			BUID_LO(dn->phb->buid));

	if (ret == 0 && rets[1] == 1 && rets[0] >= 2) {
		unsigned long slot_err_ret;

		spin_lock_irqsave(&lock, flags);
		memset(slot_err_buf, 0, RTAS_ERROR_LOG_MAX);
		slot_err_ret = rtas_call(rtas_token("ibm,slot-error-detail"),
					 8, 1, NULL, dn->eeh_config_addr,
					 BUID_HI(dn->phb->buid),
					 BUID_LO(dn->phb->buid), NULL, 0,
					 __pa(slot_err_buf),
					 RTAS_ERROR_LOG_MAX,
					 2 /* Permanent Error */);

		if (slot_err_ret == 0)
			log_error(slot_err_buf, ERR_TYPE_RTAS_LOG,
				  1 /* Fatal */);

		spin_unlock_irqrestore(&lock, flags);

		/*
		 * XXX We should create a separate sysctl for this.
		 *
		 * Since the panic_on_oops sysctl is used to halt
		 * the system in light of potential corruption, we
		 * can use it here.
		 */
		if (panic_on_oops) {
			panic("EEH: MMIO failure (%ld) on device:%s %s\n",
			      rets[0], pci_name(dev), pci_pretty_name(dev));
		} else {
			__get_cpu_var(ignored_failures)++;
			printk(KERN_INFO "EEH: MMIO failure (%ld) on device:%s %s\n",
			       rets[0], pci_name(dev), pci_pretty_name(dev));
		}
	} else {
		__get_cpu_var(false_positives)++;
	}

	pci_dev_put(dev);
	return val;
}
EXPORT_SYMBOL(eeh_check_failure);

struct eeh_early_enable_info {
	unsigned int buid_hi;
	unsigned int buid_lo;
};

/* Enable eeh for the given device node. */
static void *early_enable_eeh(struct device_node *dn, void *data)
{
	struct eeh_early_enable_info *info = data;
	long ret;
	char *status = get_property(dn, "status", 0);
	u32 *class_code = (u32 *)get_property(dn, "class-code", 0);
	u32 *vendor_id = (u32 *)get_property(dn, "vendor-id", 0);
	u32 *device_id = (u32 *)get_property(dn, "device-id", 0);
	u32 *regs;
	int enable;

	if (status && strcmp(status, "ok") != 0)
		return NULL;	/* ignore devices with bad status */

	/* Weed out PHBs or other bad nodes. */
	if (!class_code || !vendor_id || !device_id)
		return NULL;

	/* Ignore known PHBs and EADs bridges */
	if (*vendor_id == PCI_VENDOR_ID_IBM &&
	    (*device_id == 0x0102 || *device_id == 0x008b ||
	     *device_id == 0x0188 || *device_id == 0x0302))
		return NULL;

	/*
	 * Now decide if we are going to "Disable" EEH checking
	 * for this device.  We still run with the EEH hardware active,
	 * but we won't be checking for ff's.  This means a driver
	 * could return bad data (very bad!), an interrupt handler could
	 * hang waiting on status bits that won't change, etc.
	 * But there are a few cases like display devices that make sense.
	 */
	enable = 1;	/* i.e. we will do checking */
	if ((*class_code >> 16) == PCI_BASE_CLASS_DISPLAY)
		enable = 0;

	if (!eeh_check_opts_config(dn, *class_code, *vendor_id, *device_id,
				   enable)) {
		if (enable) {
			printk(KERN_WARNING "EEH: %s user requested to run "
			       "without EEH.\n", dn->full_name);
			enable = 0;
		}
	}

	if (!enable) {
		dn->eeh_mode = EEH_MODE_NOCHECK;
		return NULL;
	}

	/* This device may already have an EEH parent. */
	if (dn->parent && (dn->parent->eeh_mode & EEH_MODE_SUPPORTED)) {
		/* Parent supports EEH. */
		dn->eeh_mode |= EEH_MODE_SUPPORTED;
		dn->eeh_config_addr = dn->parent->eeh_config_addr;
		return NULL;
	}

	/* Ok... see if this device supports EEH. */
	regs = (u32 *)get_property(dn, "reg", 0);
	if (regs) {
		/* First register entry is addr (00BBSS00)  */
		/* Try to enable eeh */
		ret = rtas_call(ibm_set_eeh_option, 4, 1, NULL,
				regs[0], info->buid_hi, info->buid_lo,
				EEH_ENABLE);
		if (ret == 0) {
			eeh_subsystem_enabled = 1;
			dn->eeh_mode |= EEH_MODE_SUPPORTED;
			dn->eeh_config_addr = regs[0];
#ifdef DEBUG
			printk(KERN_DEBUG "EEH: %s: eeh enabled\n",
			       dn->full_name);
#endif
		} else {
			printk(KERN_WARNING "EEH: %s: rtas_call failed.\n",
			       dn->full_name);
		}
	} else {
		printk(KERN_WARNING "EEH: %s: unable to get reg property.\n",
		       dn->full_name);
	}

	return NULL; 
}

/*
 * Initialize EEH by trying to enable it for all of the adapters in the system.
 * As a side effect we can determine here if eeh is supported at all.
 * Note that we leave EEH on so failed config cycles won't cause a machine
 * check.  If a user turns off EEH for a particular adapter they are really
 * telling Linux to ignore errors.
 *
 * We should probably distinguish between "ignore errors" and "turn EEH off"
 * but for now disabling EEH for adapters is mostly to work around drivers that
 * directly access mmio space (without using the macros).
 *
 * The eeh-force-off option does literally what it says, so if Linux must
 * avoid enabling EEH this must be done.
 */
void __init eeh_init(void)
{
	struct device_node *phb;
	struct eeh_early_enable_info info;
	char *eeh_force_off = strstr(saved_command_line, "eeh-force-off");

	ibm_set_eeh_option = rtas_token("ibm,set-eeh-option");
	ibm_set_slot_reset = rtas_token("ibm,set-slot-reset");
	ibm_read_slot_reset_state = rtas_token("ibm,read-slot-reset-state");

	if (ibm_set_eeh_option == RTAS_UNKNOWN_SERVICE)
		return;

	if (eeh_force_off) {
		printk(KERN_WARNING "EEH: WARNING: PCI Enhanced I/O Error "
		       "Handling is user disabled\n");
		return;
	}

	/* Enable EEH for all adapters.  Note that eeh requires buid's */
	for (phb = of_find_node_by_name(NULL, "pci"); phb;
	     phb = of_find_node_by_name(phb, "pci")) {
		int len;
		int *buid_vals;

		buid_vals = (int *)get_property(phb, "ibm,fw-phb-id", &len);
		if (!buid_vals)
			continue;
		if (len == sizeof(int)) {
			info.buid_lo = buid_vals[0];
			info.buid_hi = 0;
		} else if (len == sizeof(int)*2) {
			info.buid_hi = buid_vals[0];
			info.buid_lo = buid_vals[1];
		} else {
			printk(KERN_INFO "EEH: odd ibm,fw-phb-id len returned: %d\n", len);
			continue;
		}
		traverse_pci_devices(phb, early_enable_eeh, NULL, &info);
	}

	if (eeh_subsystem_enabled)
		printk(KERN_INFO "EEH: PCI Enhanced I/O Error Handling Enabled\n");
}

/**
 * eeh_add_device - perform EEH initialization for the indicated pci device
 * @dev: pci device for which to set up EEH
 *
 * This routine can be used to perform EEH initialization for PCI
 * devices that were added after system boot (e.g. hotplug, dlpar).
 * Whether this actually enables EEH or not for this device depends
 * on the type of the device, on earlier boot command-line
 * arguments & etc.
 */
void eeh_add_device(struct pci_dev *dev)
{
	struct device_node *dn;
	struct pci_controller *phb;
	struct eeh_early_enable_info info;

	if (!dev || !eeh_subsystem_enabled)
		return;

#ifdef DEBUG
	printk(KERN_DEBUG "EEH: adding device %s %s\n", pci_name(dev),
	       pci_pretty_name(dev));
#endif
	dn = pci_device_to_OF_node(dev);
	if (NULL == dn)
		return;

	phb = PCI_GET_PHB_PTR(dev);
	if (NULL == phb || 0 == phb->buid) {
		printk(KERN_WARNING "EEH: Expected buid but found none\n");
		return;
	}

	info.buid_hi = BUID_HI(phb->buid);
	info.buid_lo = BUID_LO(phb->buid);

	early_enable_eeh(dn, &info);
	pci_addr_cache_insert_device (dev);
}
EXPORT_SYMBOL(eeh_add_device);

/**
 * eeh_remove_device - undo EEH setup for the indicated pci device
 * @dev: pci device to be removed
 *
 * This routine should be when a device is removed from a running
 * system (e.g. by hotplug or dlpar).
 */
void eeh_remove_device(struct pci_dev *dev)
{
	if (!dev || !eeh_subsystem_enabled)
		return;

	/* Unregister the device with the EEH/PCI address search system */
#ifdef DEBUG
	printk(KERN_DEBUG "EEH: remove device %s %s\n", pci_name(dev),
	       pci_pretty_name(dev));
#endif
	pci_addr_cache_remove_device(dev);
}
EXPORT_SYMBOL(eeh_remove_device);

/*
 * If EEH is implemented, find the PCI device using given phys addr
 * and check to see if eeh failure checking is disabled.
 * Remap the addr (trivially) to the EEH region if EEH checking enabled.
 * For addresses not known to PCI the vaddr is simply returned unchanged.
 */
void *eeh_ioremap(unsigned long addr, void *vaddr)
{
	struct pci_dev *dev;
	struct device_node *dn;

	if (!eeh_subsystem_enabled)
		return vaddr;

	dev = pci_get_device_by_addr(addr);
	if (!dev)
		return vaddr;

	dn = pci_device_to_OF_node(dev);
	if (!dn) {
		pci_dev_put(dev);
		return vaddr;
	}

	if (dn->eeh_mode & EEH_MODE_NOCHECK) {
		pci_dev_put(dev);
		return vaddr;
	}

	pci_dev_put(dev);
	return (void *)IO_ADDR_TO_TOKEN(vaddr);
}

static int proc_eeh_show(struct seq_file *m, void *v)
{
	unsigned int cpu;
	unsigned long ffs = 0, positives = 0, failures = 0;

	for_each_cpu(cpu) {
		ffs += per_cpu(total_mmio_ffs, cpu);
		positives += per_cpu(false_positives, cpu);
		failures += per_cpu(ignored_failures, cpu);
	}

	if (0 == eeh_subsystem_enabled) {
		seq_printf(m, "EEH Subsystem is globally disabled\n");
		seq_printf(m, "eeh_total_mmio_ffs=%ld\n", ffs);
	} else {
		seq_printf(m, "EEH Subsystem is enabled\n");
		seq_printf(m, "eeh_total_mmio_ffs=%ld\n"
			   "eeh_false_positives=%ld\n"
			   "eeh_ignored_failures=%ld\n",
			   ffs, positives, failures);
	}

	return 0;
}

static int proc_eeh_open(struct inode *inode, struct file *file)
{
	return single_open(file, proc_eeh_show, NULL);
}

static struct file_operations proc_eeh_operations = {
	.open		= proc_eeh_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

static int __init eeh_init_proc(void)
{
	struct proc_dir_entry *e;

	if (systemcfg->platform & PLATFORM_PSERIES) {
		e = create_proc_entry("ppc64/eeh", 0, NULL);
		if (e)
			e->proc_fops = &proc_eeh_operations;
	}

        return 0;
}
__initcall(eeh_init_proc);

/*
 * Test if "dev" should be configured on or off.
 * This processes the options literally from left to right.
 * This lets the user specify stupid combinations of options,
 * but at least the result should be very predictable.
 */
static int eeh_check_opts_config(struct device_node *dn,
				 int class_code, int vendor_id, int device_id,
				 int default_state)
{
	char devname[32], classname[32];
	char *strs[8], *s;
	int nstrs, i;
	int ret = default_state;

	/* Build list of strings to match */
	nstrs = 0;
	s = (char *)get_property(dn, "ibm,loc-code", 0);
	if (s)
		strs[nstrs++] = s;
	sprintf(devname, "dev%04x:%04x", vendor_id, device_id);
	strs[nstrs++] = devname;
	sprintf(classname, "class%04x", class_code);
	strs[nstrs++] = classname;
	strs[nstrs++] = "";	/* yes, this matches the empty string */

	/*
	 * Now see if any string matches the eeh_opts list.
	 * The eeh_opts list entries start with + or -.
	 */
	for (s = eeh_opts; s && (s < (eeh_opts + eeh_opts_last));
	     s += strlen(s)+1) {
		for (i = 0; i < nstrs; i++) {
			if (strcasecmp(strs[i], s+1) == 0) {
				ret = (strs[i][0] == '+') ? 1 : 0;
			}
		}
	}
	return ret;
}

/*
 * Handle kernel eeh-on & eeh-off cmd line options for eeh.
 *
 * We support:
 *	eeh-off=loc1,loc2,loc3...
 *
 * and this option can be repeated so
 *      eeh-off=loc1,loc2 eeh-off=loc3
 * is the same as eeh-off=loc1,loc2,loc3
 *
 * loc is an IBM location code that can be found in a manual or
 * via openfirmware (or the Hardware Management Console).
 *
 * We also support these additional "loc" values:
 *
 *	dev#:#    vendor:device id in hex (e.g. dev1022:2000)
 *	class#    class id in hex (e.g. class0200)
 *
 * If no location code is specified all devices are assumed
 * so eeh-off means eeh by default is off.
 */

/*
 * This is implemented as a null separated list of strings.
 * Each string looks like this:  "+X" or "-X"
 * where X is a loc code, vendor:device, class (as shown above)
 * or empty which is used to indicate all.
 *
 * We interpret this option string list so that it will literally
 * behave left-to-right even if some combinations don't make sense.
 */
static int __init eeh_parm(char *str, int state)
{
	char *s, *cur, *curend;

	if (!eeh_opts) {
		eeh_opts = alloc_bootmem(EEH_MAX_OPTS);
		eeh_opts[eeh_opts_last++] = '+'; /* default */
		eeh_opts[eeh_opts_last++] = '\0';
	}
	if (*str == '\0') {
		eeh_opts[eeh_opts_last++] = state ? '+' : '-';
		eeh_opts[eeh_opts_last++] = '\0';
		return 1;
	}
	if (*str == '=')
		str++;
	for (s = str; s && *s != '\0'; s = curend) {
		cur = s;
		/* ignore empties.  Don't treat as "all-on" or "all-off" */
		while (*cur == ',')
			cur++;
		curend = strchr(cur, ',');
		if (!curend)
			curend = cur + strlen(cur);
		if (*cur) {
			int curlen = curend-cur;
			if (eeh_opts_last + curlen > EEH_MAX_OPTS-2) {
				printk(KERN_WARNING "EEH: sorry...too many "
				       "eeh cmd line options\n");
				return 1;
			}
			eeh_opts[eeh_opts_last++] = state ? '+' : '-';
			strncpy(eeh_opts+eeh_opts_last, cur, curlen);
			eeh_opts_last += curlen;
			eeh_opts[eeh_opts_last++] = '\0';
		}
	}

	return 1;
}

static int __init eehoff_parm(char *str)
{
	return eeh_parm(str, 0);
}

static int __init eehon_parm(char *str)
{
	return eeh_parm(str, 1);
}

__setup("eeh-off", eehoff_parm);
__setup("eeh-on", eehon_parm);
