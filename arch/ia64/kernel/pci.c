/*
 * pci.c - Low-Level PCI Access in IA-64
 *
 * Derived from bios32.c of i386 tree.
 */
#include <linux/config.h>

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


#undef DEBUG
#define DEBUG

#ifdef DEBUG
#define DBG(x...) printk(x)
#else
#define DBG(x...)
#endif

#ifdef CONFIG_IA64_MCA
extern void ia64_mca_check_errors( void );
#endif

struct pci_fixup pcibios_fixups[1];

struct pci_ops *pci_root_ops;

int (*pci_config_read)(int seg, int bus, int dev, int fn, int reg, int len, u32 *value);
int (*pci_config_write)(int seg, int bus, int dev, int fn, int reg, int len, u32 value);


/*
 * Low-level SAL-based PCI configuration access functions. Note that SAL
 * calls are already serialized (via sal_lock), so we don't need another
 * synchronization mechanism here.  Not using segment number (yet).
 */

#define PCI_SAL_ADDRESS(bus, dev, fn, reg) \
	((u64)(bus << 16) | (u64)(dev << 11) | (u64)(fn << 8) | (u64)(reg))

static int
pci_sal_read (int seg, int bus, int dev, int fn, int reg, int len, u32 *value)
{
	int result = 0;
	u64 data = 0;

	if (!value || (bus > 255) || (dev > 31) || (fn > 7) || (reg > 255))
		return -EINVAL;

	result = ia64_sal_pci_config_read(PCI_SAL_ADDRESS(bus, dev, fn, reg), len, &data);

	*value = (u32) data;

	return result;
}

static int
pci_sal_write (int seg, int bus, int dev, int fn, int reg, int len, u32 value)
{
	if ((bus > 255) || (dev > 31) || (fn > 7) || (reg > 255))
		return -EINVAL;

	return ia64_sal_pci_config_write(PCI_SAL_ADDRESS(bus, dev, fn, reg), len, value);
}


static int
pci_sal_read_config_byte (struct pci_dev *dev, int where, u8 *value)
{
	int result = 0;
	u32 data = 0;

	if (!value)
		return -EINVAL;

	result = pci_sal_read(0, dev->bus->number, PCI_SLOT(dev->devfn),
			      PCI_FUNC(dev->devfn), where, 1, &data);

	*value = (u8) data;

	return result;
}

static int
pci_sal_read_config_word (struct pci_dev *dev, int where, u16 *value)
{
	int result = 0;
	u32 data = 0;

	if (!value)
		return -EINVAL;

	result = pci_sal_read(0, dev->bus->number, PCI_SLOT(dev->devfn),
			      PCI_FUNC(dev->devfn), where, 2, &data);

	*value = (u16) data;

	return result;
}

static int
pci_sal_read_config_dword (struct pci_dev *dev, int where, u32 *value)
{
	if (!value)
		return -EINVAL;

	return pci_sal_read(0, dev->bus->number, PCI_SLOT(dev->devfn),
			    PCI_FUNC(dev->devfn), where, 4, value);
}

static int
pci_sal_write_config_byte (struct pci_dev *dev, int where, u8 value)
{
	return pci_sal_write(0, dev->bus->number, PCI_SLOT(dev->devfn),
			     PCI_FUNC(dev->devfn), where, 1, value);
}

static int
pci_sal_write_config_word (struct pci_dev *dev, int where, u16 value)
{
	return pci_sal_write(0, dev->bus->number, PCI_SLOT(dev->devfn),
			     PCI_FUNC(dev->devfn), where, 2, value);
}

static int
pci_sal_write_config_dword (struct pci_dev *dev, int where, u32 value)
{
	return pci_sal_write(0, dev->bus->number, PCI_SLOT(dev->devfn),
			     PCI_FUNC(dev->devfn), where, 4, value);
}

struct pci_ops pci_sal_ops = {
	pci_sal_read_config_byte,
	pci_sal_read_config_word,
	pci_sal_read_config_dword,
	pci_sal_write_config_byte,
	pci_sal_write_config_word,
	pci_sal_write_config_dword
};


/*
 * Initialization. Uses the SAL interface
 */

struct pci_bus *
pcibios_scan_root(int seg, int bus)
{
	struct list_head *list = NULL;
	struct pci_bus *pci_bus = NULL;

	list_for_each(list, &pci_root_buses) {
		pci_bus = pci_bus_b(list);
		if (pci_bus->number == bus) {
			/* Already scanned */
			printk("PCI: Bus (%02x:%02x) already probed\n", seg, bus);
			return pci_bus;
		}
	}

	printk("PCI: Probing PCI hardware on bus (%02x:%02x)\n", seg, bus);

	return pci_scan_bus(bus, pci_root_ops, NULL);
}

void __init
pcibios_config_init (void)
{
	if (pci_root_ops)
		return;

	printk("PCI: Using SAL to access configuration space\n");

	pci_root_ops = &pci_sal_ops;
	pci_config_read = pci_sal_read;
	pci_config_write = pci_sal_write;

	return;
}

static int __init
pcibios_init (void)
{
#	define PCI_BUSES_TO_SCAN 255
	int i = 0;

#ifdef CONFIG_IA64_MCA
	ia64_mca_check_errors();    /* For post-failure MCA error logging */
#endif

	pcibios_config_init();

	platform_pci_fixup(0);	/* phase 0 fixups (before buses scanned) */

	printk("PCI: Probing PCI hardware\n");
	for (i = 0; i < PCI_BUSES_TO_SCAN; i++)
		pci_scan_bus(i, pci_root_ops, NULL);

	platform_pci_fixup(1);	/* phase 1 fixups (after buses scanned) */
	return 0;
}

subsys_initcall(pcibios_init);

/*
 *  Called after each bus is probed, but before its children
 *  are examined.
 */
void __init
pcibios_fixup_bus (struct pci_bus *b)
{
	return;
}

void __init
pcibios_update_resource (struct pci_dev *dev, struct resource *root,
			 struct resource *res, int resource)
{
	unsigned long where, size;
	u32 reg;

	where = PCI_BASE_ADDRESS_0 + (resource * 4);
	size = res->end - res->start;
	pci_read_config_dword(dev, where, &reg);
	reg = (reg & size) | (((u32)(res->start - root->start)) & ~size);
	pci_write_config_dword(dev, where, reg);

	/* ??? FIXME -- record old value for shutdown.  */
}

void __init
pcibios_update_irq (struct pci_dev *dev, int irq)
{
	pci_write_config_byte(dev, PCI_INTERRUPT_LINE, irq);

	/* ??? FIXME -- record old value for shutdown.  */
}

void __init
pcibios_fixup_pbus_ranges (struct pci_bus * bus, struct pbus_set_ranges_data * ranges)
{
}

int
pcibios_enable_device (struct pci_dev *dev)
{
	u16 cmd, old_cmd;
	int idx;
	struct resource *r;

	if (!dev)
		return -EINVAL;

	pci_read_config_word(dev, PCI_COMMAND, &cmd);
	old_cmd = cmd;
	for (idx=0; idx<6; idx++) {
		r = &dev->resource[idx];
		if (!r->start && r->end) {
			printk(KERN_ERR
			       "PCI: Device %s not available because of resource collisions\n",
			       dev->slot_name);
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
		printk("PCI: Enabling device %s (%04x -> %04x)\n", dev->slot_name, old_cmd, cmd);
		pci_write_config_word(dev, PCI_COMMAND, cmd);
	}

	printk(KERN_INFO "PCI: Found IRQ %d for device %s\n", dev->irq, dev->slot_name);

	return 0;
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
