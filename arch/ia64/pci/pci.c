/*
 * pci.c - Low-Level PCI Access in IA-64
 *
 * Derived from bios32.c of i386 tree.
 *
 * Copyright (C) 2002 Hewlett-Packard Co
 *	David Mosberger-Tang <davidm@hpl.hp.com>
 *	Bjorn Helgaas <bjorn_helgaas@hp.com>
 *
 * Note: Above list of copyright holders is incomplete...
 */
#include <linux/config.h>

#include <linux/acpi.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/init.h>
#include <linux/ioport.h>
#include <linux/slab.h>
#include <linux/smp_lock.h>
#include <linux/spinlock.h>

#include <asm/machvec.h>
#include <asm/page.h>
#include <asm/segment.h>
#include <asm/system.h>
#include <asm/io.h>

#include <asm/sal.h>


#ifdef CONFIG_SMP
# include <asm/smp.h>
#endif
#include <asm/irq.h>
#include <asm/hw_irq.h>


#undef DEBUG
#define DEBUG

#ifdef DEBUG
#define DBG(x...) printk(x)
#else
#define DBG(x...)
#endif

struct pci_fixup pcibios_fixups[1];

/*
 * Low-level SAL-based PCI configuration access functions. Note that SAL
 * calls are already serialized (via sal_lock), so we don't need another
 * synchronization mechanism here.
 */

#define PCI_SAL_ADDRESS(seg, bus, devfn, reg)	\
	((u64)(seg << 24) | (u64)(bus << 16) |	\
	 (u64)(devfn << 8) | (u64)(reg))

/* SAL 3.2 adds support for extended config space. */

#define PCI_SAL_EXT_ADDRESS(seg, bus, devfn, reg)	\
	((u64)(seg << 28) | (u64)(bus << 20) |		\
	 (u64)(devfn << 12) | (u64)(reg))

static int
pci_sal_read (int seg, int bus, int devfn, int reg, int len, u32 *value)
{
	u64 addr, mode, data = 0;
	int result = 0;

	if ((seg > 255) || (bus > 255) || (devfn > 255) || (reg > 4095))
		return -EINVAL;

	if ((seg | reg) <= 255) {
		addr = PCI_SAL_ADDRESS(seg, bus, devfn, reg);
		mode = 0;
	} else {
		addr = PCI_SAL_EXT_ADDRESS(seg, bus, devfn, reg);
		mode = 1;
	}
	result = ia64_sal_pci_config_read(addr, mode, len, &data);

	*value = (u32) data;

	return result;
}

static int
pci_sal_write (int seg, int bus, int devfn, int reg, int len, u32 value)
{
	u64 addr, mode;

	if ((seg > 65535) || (bus > 255) || (devfn > 255) || (reg > 4095))
		return -EINVAL;

	if ((seg | reg) <= 255) {
		addr = PCI_SAL_ADDRESS(seg, bus, devfn, reg);
		mode = 0;
	} else {
		addr = PCI_SAL_EXT_ADDRESS(seg, bus, devfn, reg);
		mode = 1;
	}
	return ia64_sal_pci_config_write(addr, mode, len, value);
}

static struct pci_raw_ops pci_sal_ops = {
	.read = 	pci_sal_read,
	.write =	pci_sal_write
};

struct pci_raw_ops *raw_pci_ops = &pci_sal_ops;

static int
pci_read (struct pci_bus *bus, unsigned int devfn, int where, int size, u32 *value)
{
	return raw_pci_ops->read(pci_domain_nr(bus), bus->number,
				 devfn, where, size, value);
}

static int
pci_write (struct pci_bus *bus, unsigned int devfn, int where, int size, u32 value)
{
	return raw_pci_ops->write(pci_domain_nr(bus), bus->number,
				  devfn, where, size, value);
}

static struct pci_ops pci_root_ops = {
	.read = pci_read,
	.write = pci_write,
};

static int __init
pci_acpi_init (void)
{
	struct pci_dev *dev = NULL;

	printk(KERN_INFO "PCI: Using ACPI for IRQ routing\n");

	/*
	 * PCI IRQ routing is set up by pci_enable_device(), but we
	 * also do it here in case there are still broken drivers that
	 * don't use pci_enable_device().
	 */
	while ((dev = pci_find_device(PCI_ANY_ID, PCI_ANY_ID, dev)) != NULL)
		acpi_pci_irq_enable(dev);

	return 0;
}

subsys_initcall(pci_acpi_init);

/* Called by ACPI when it finds a new root bus.  */

static struct pci_controller * __devinit
alloc_pci_controller (int seg)
{
	struct pci_controller *controller;

	controller = kmalloc(sizeof(*controller), GFP_KERNEL);
	if (!controller)
		return NULL;

	memset(controller, 0, sizeof(*controller));
	controller->segment = seg;
	return controller;
}

static int __devinit
alloc_resource (char *name, struct resource *root, unsigned long start, unsigned long end,
		unsigned long flags)
{
	struct resource *res;

	res = kmalloc(sizeof(*res), GFP_KERNEL);
	if (!res)
		return -ENOMEM;

	memset(res, 0, sizeof(*res));
	res->name = name;
	res->start = start;
	res->end = end;
	res->flags = flags;

	if (insert_resource(root, res))	{
		kfree(res);
		return -EBUSY;
	}

	return 0;
}

static u64 __devinit
add_io_space (struct acpi_resource_address64 *addr)
{
	u64 offset;
	int sparse = 0;
	int i;

	if (addr->address_translation_offset == 0)
		return IO_SPACE_BASE(0);	/* part of legacy IO space */

	if (addr->attribute.io.translation_attribute == ACPI_SPARSE_TRANSLATION)
		sparse = 1;

	offset = (u64) ioremap(addr->address_translation_offset, 0);
	for (i = 0; i < num_io_spaces; i++)
		if (io_space[i].mmio_base == offset &&
		    io_space[i].sparse == sparse)
			return IO_SPACE_BASE(i);

	if (num_io_spaces == MAX_IO_SPACES) {
		printk("Too many IO port spaces\n");
		return ~0;
	}

	i = num_io_spaces++;
	io_space[i].mmio_base = offset;
	io_space[i].sparse = sparse;

	return IO_SPACE_BASE(i);
}

static acpi_status __devinit
count_window (struct acpi_resource *resource, void *data)
{
	unsigned int *windows = (unsigned int *) data;
	struct acpi_resource_address64 addr;
	acpi_status status;

	status = acpi_resource_to_address64(resource, &addr);
	if (ACPI_SUCCESS(status))
		if (addr.resource_type == ACPI_MEMORY_RANGE ||
		    addr.resource_type == ACPI_IO_RANGE)
			(*windows)++;

	return AE_OK;
}

struct pci_root_info {
	struct pci_controller *controller;
	char *name;
};

static acpi_status __devinit
add_window (struct acpi_resource *res, void *data)
{
	struct pci_root_info *info = (struct pci_root_info *) data;
	struct pci_window *window;
	struct acpi_resource_address64 addr;
	acpi_status status;
	unsigned long flags, offset = 0;
	struct resource *root;

	status = acpi_resource_to_address64(res, &addr);
	if (ACPI_SUCCESS(status)) {
		if (!addr.address_length)
			return AE_OK;

		if (addr.resource_type == ACPI_MEMORY_RANGE) {
			flags = IORESOURCE_MEM;
			root = &iomem_resource;
			offset = addr.address_translation_offset;
		} else if (addr.resource_type == ACPI_IO_RANGE) {
			flags = IORESOURCE_IO;
			root = &ioport_resource;
			offset = add_io_space(&addr);
			if (offset == ~0)
				return AE_OK;
		} else
			return AE_OK;

		window = &info->controller->window[info->controller->windows++];
		window->resource.flags |= flags;
		window->resource.start  = addr.min_address_range;
		window->resource.end    = addr.max_address_range;
		window->offset		= offset;

		if (alloc_resource(info->name, root, addr.min_address_range + offset,
			addr.max_address_range + offset, flags))
			printk(KERN_ERR "alloc 0x%lx-0x%lx from %s for %s failed\n",
				addr.min_address_range + offset, addr.max_address_range + offset,
				root->name, info->name);
	}

	return AE_OK;
}

struct pci_bus * __devinit
pci_acpi_scan_root (struct acpi_device *device, int domain, int bus)
{
	struct pci_root_info info;
	struct pci_controller *controller;
	unsigned int windows = 0;
	char *name;

	controller = alloc_pci_controller(domain);
	if (!controller)
		goto out1;

	controller->acpi_handle = device->handle;

	acpi_walk_resources(device->handle, METHOD_NAME__CRS, count_window, &windows);
	controller->window = kmalloc(sizeof(*controller->window) * windows, GFP_KERNEL);
	if (!controller->window)
		goto out2;

	name = kmalloc(16, GFP_KERNEL);
	if (!name)
		goto out3;

	sprintf(name, "PCI Bus %04x:%02x", domain, bus);
	info.controller = controller;
	info.name = name;
	acpi_walk_resources(device->handle, METHOD_NAME__CRS, add_window, &info);

	return pci_scan_bus(bus, &pci_root_ops, controller);

out3:
	kfree(controller->window);
out2:
	kfree(controller);
out1:
	return NULL;
}

void __init
pcibios_fixup_device_resources (struct pci_dev *dev, struct pci_bus *bus)
{
	struct pci_controller *controller = PCI_CONTROLLER(dev);
	struct pci_window *window;
	int i, j;

	for (i = 0; i < PCI_NUM_RESOURCES; i++) {
		if (!dev->resource[i].start)
			continue;

#define contains(win, res)	((res)->start >= (win)->start && \
				 (res)->end   <= (win)->end)

		for (j = 0; j < controller->windows; j++) {
			window = &controller->window[j];
			if (((dev->resource[i].flags & IORESOURCE_MEM &&
			      window->resource.flags & IORESOURCE_MEM) ||
			     (dev->resource[i].flags & IORESOURCE_IO &&
			      window->resource.flags & IORESOURCE_IO)) &&
			    contains(&window->resource, &dev->resource[i])) {
				dev->resource[i].start += window->offset;
				dev->resource[i].end   += window->offset;
			}
		}
		pci_claim_resource(dev, i);
	}
}

/*
 *  Called after each bus is probed, but before its children are examined.
 */
void __devinit
pcibios_fixup_bus (struct pci_bus *b)
{
	struct list_head *ln;

	for (ln = b->devices.next; ln != &b->devices; ln = ln->next)
		pcibios_fixup_device_resources(pci_dev_b(ln), b);

	return;
}

void __devinit
pcibios_update_irq (struct pci_dev *dev, int irq)
{
	pci_write_config_byte(dev, PCI_INTERRUPT_LINE, irq);

	/* ??? FIXME -- record old value for shutdown.  */
}

static inline int
pcibios_enable_resources (struct pci_dev *dev, int mask)
{
	u16 cmd, old_cmd;
	int idx;
	struct resource *r;

	if (!dev)
		return -EINVAL;

	pci_read_config_word(dev, PCI_COMMAND, &cmd);
	old_cmd = cmd;
	for (idx=0; idx<6; idx++) {
		/* Only set up the desired resources.  */
		if (!(mask & (1 << idx)))
			continue;

		r = &dev->resource[idx];
		if (!r->start && r->end) {
			printk(KERN_ERR
			       "PCI: Device %s not available because of resource collisions\n",
			       pci_name(dev));
			return -EINVAL;
		}
		if (r->flags & IORESOURCE_IO)
			cmd |= PCI_COMMAND_IO;
		if (r->flags & IORESOURCE_MEM)
			cmd |= PCI_COMMAND_MEMORY;
	}
	if (dev->resource[PCI_ROM_RESOURCE].start)
		cmd |= PCI_COMMAND_MEMORY;
	if (cmd != old_cmd) {
		printk("PCI: Enabling device %s (%04x -> %04x)\n", pci_name(dev), old_cmd, cmd);
		pci_write_config_word(dev, PCI_COMMAND, cmd);
	}
	return 0;
}

int
pcibios_enable_device (struct pci_dev *dev, int mask)
{
	int ret;

	ret = pcibios_enable_resources(dev, mask);
	if (ret < 0)
		return ret;

	return acpi_pci_irq_enable(dev);
}

void
pcibios_align_resource (void *data, struct resource *res,
		        unsigned long size, unsigned long align)
{
}

/*
 * PCI BIOS setup, always defaults to SAL interface
 */
char * __init
pcibios_setup (char *str)
{
	return NULL;
}

int
pci_mmap_page_range (struct pci_dev *dev, struct vm_area_struct *vma,
		     enum pci_mmap_state mmap_state, int write_combine)
{
	/*
	 * I/O space cannot be accessed via normal processor loads and stores on this
	 * platform.
	 */
	if (mmap_state == pci_mmap_io)
		/*
		 * XXX we could relax this for I/O spaces for which ACPI indicates that
		 * the space is 1-to-1 mapped.  But at the moment, we don't support
		 * multiple PCI address spaces and the legacy I/O space is not 1-to-1
		 * mapped, so this is moot.
		 */
		return -EINVAL;

	/*
	 * Leave vm_pgoff as-is, the PCI space address is the physical address on this
	 * platform.
	 */
	vma->vm_flags |= (VM_SHM | VM_LOCKED | VM_IO);

	if (write_combine)
		vma->vm_page_prot = pgprot_writecombine(vma->vm_page_prot);
	else
		vma->vm_page_prot = pgprot_noncached(vma->vm_page_prot);

	if (remap_page_range(vma, vma->vm_start, vma->vm_pgoff << PAGE_SHIFT,
			     vma->vm_end - vma->vm_start, vma->vm_page_prot))
		return -EAGAIN;

	return 0;
}

/**
 * pci_cacheline_size - determine cacheline size for PCI devices
 * @dev: void
 *
 * We want to use the line-size of the outer-most cache.  We assume
 * that this line-size is the same for all CPUs.
 *
 * Code mostly taken from arch/ia64/kernel/palinfo.c:cache_info().
 *
 * RETURNS: An appropriate -ERRNO error value on eror, or zero for success.
 */
static unsigned long
pci_cacheline_size (void)
{
	u64 levels, unique_caches;
	s64 status;
	pal_cache_config_info_t cci;
	static u8 cacheline_size;

	if (cacheline_size)
		return cacheline_size;

	status = ia64_pal_cache_summary(&levels, &unique_caches);
	if (status != 0) {
		printk(KERN_ERR "%s: ia64_pal_cache_summary() failed (status=%ld)\n",
		       __FUNCTION__, status);
		return SMP_CACHE_BYTES;
	}

	status = ia64_pal_cache_config_info(levels - 1, /* cache_type (data_or_unified)= */ 2,
					    &cci);
	if (status != 0) {
		printk(KERN_ERR "%s: ia64_pal_cache_config_info() failed (status=%ld)\n",
		       __FUNCTION__, status);
		return SMP_CACHE_BYTES;
	}
	cacheline_size = 1 << cci.pcci_line_size;
	return cacheline_size;
}

/**
 * pcibios_prep_mwi - helper function for drivers/pci/pci.c:pci_set_mwi()
 * @dev: the PCI device for which MWI is enabled
 *
 * For ia64, we can get the cacheline sizes from PAL.
 *
 * RETURNS: An appropriate -ERRNO error value on eror, or zero for success.
 */
int
pcibios_prep_mwi (struct pci_dev *dev)
{
	unsigned long desired_linesize, current_linesize;
	int rc = 0;
	u8 pci_linesize;

	desired_linesize = pci_cacheline_size();

	pci_read_config_byte(dev, PCI_CACHE_LINE_SIZE, &pci_linesize);
	current_linesize = 4 * pci_linesize;
	if (desired_linesize != current_linesize) {
		printk(KERN_WARNING "PCI: slot %s has incorrect PCI cache line size of %lu bytes,",
		       pci_name(dev), current_linesize);
		if (current_linesize > desired_linesize) {
			printk(" expected %lu bytes instead\n", desired_linesize);
			rc = -EINVAL;
		} else {
			printk(" correcting to %lu\n", desired_linesize);
			pci_write_config_byte(dev, PCI_CACHE_LINE_SIZE, desired_linesize / 4);
		}
	}
	return rc;
}

int pci_vector_resources(int last, int nr_released)
{
	int count = nr_released;

 	count += (IA64_LAST_DEVICE_VECTOR - last);

	return count;
}
