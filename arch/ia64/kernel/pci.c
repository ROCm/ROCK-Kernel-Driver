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

/*
 * This interrupt-safe spinlock protects all accesses to PCI
 * configuration space.
 */
static spinlock_t pci_lock = SPIN_LOCK_UNLOCKED;

struct pci_fixup pcibios_fixups[] = {
	{ 0 }
};

/* Macro to build a PCI configuration address to be passed as a parameter to SAL. */

#define PCI_CONFIG_ADDRESS(dev, where) \
	(((u64) dev->bus->number << 16) | ((u64) (dev->devfn & 0xff) << 8) | (where & 0xff))

static int
pci_conf_read_config_byte(struct pci_dev *dev, int where, u8 *value)
{
	s64 status;
	u64 lval;

	status = ia64_sal_pci_config_read(PCI_CONFIG_ADDRESS(dev, where), 1, &lval);
	*value = lval;
	return status;
}

static int
pci_conf_read_config_word(struct pci_dev *dev, int where, u16 *value)
{
	s64 status;
	u64 lval;

	status = ia64_sal_pci_config_read(PCI_CONFIG_ADDRESS(dev, where), 2, &lval);
	*value = lval;
	return status;
}

static int
pci_conf_read_config_dword(struct pci_dev *dev, int where, u32 *value)
{
	s64 status;
	u64 lval;

	status = ia64_sal_pci_config_read(PCI_CONFIG_ADDRESS(dev, where), 4, &lval);
	*value = lval;
	return status;
}

static int
pci_conf_write_config_byte (struct pci_dev *dev, int where, u8 value)
{
	return ia64_sal_pci_config_write(PCI_CONFIG_ADDRESS(dev, where), 1, value);
}

static int
pci_conf_write_config_word (struct pci_dev *dev, int where, u16 value)
{
	return ia64_sal_pci_config_write(PCI_CONFIG_ADDRESS(dev, where), 2, value);
}

static int
pci_conf_write_config_dword (struct pci_dev *dev, int where, u32 value)
{
	return ia64_sal_pci_config_write(PCI_CONFIG_ADDRESS(dev, where), 4, value);
}

struct pci_ops pci_conf = {
      pci_conf_read_config_byte,
      pci_conf_read_config_word,
      pci_conf_read_config_dword,
      pci_conf_write_config_byte,
      pci_conf_write_config_word,
      pci_conf_write_config_dword
};

/*
 * Initialization. Uses the SAL interface
 */
void __init
pcibios_init (void)
{
#	define PCI_BUSES_TO_SCAN 255
	int i;

#ifdef CONFIG_IA64_MCA
	ia64_mca_check_errors();    /* For post-failure MCA error logging */
#endif

	platform_pci_fixup(0);	/* phase 0 initialization (before PCI bus has been scanned) */

	printk("PCI: Probing PCI hardware\n");
	for (i = 0; i < PCI_BUSES_TO_SCAN; i++)
		pci_scan_bus(i, &pci_conf, NULL);

	platform_pci_fixup(1);	/* phase 1 initialization (after PCI bus has been scanned) */
	return;
}

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
	ranges->io_start -= bus->resource[0]->start;
	ranges->io_end -= bus->resource[0]->start;
	ranges->mem_start -= bus->resource[1]->start;
	ranges->mem_end -= bus->resource[1]->start;
}

int
pcibios_enable_device (struct pci_dev *dev)
{
	/* Not needed, since we enable all devices at startup.  */
	return 0;
}

void
pcibios_align_resource (void *data, struct resource *res, unsigned long size)
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

	if (remap_page_range(vma->vm_start, vma->vm_pgoff << PAGE_SHIFT,
			     vma->vm_end - vma->vm_start,
			     vma->vm_page_prot))
		return -EAGAIN;

	return 0;
}
