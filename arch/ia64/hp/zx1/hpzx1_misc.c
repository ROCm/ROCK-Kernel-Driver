/*
 * Misc. support for HP zx1 chipset support
 *
 * Copyright (C) 2002 Hewlett-Packard Co
 * Copyright (C) 2002 Alex Williamson <alex_williamson@hp.com>
 * Copyright (C) 2002 Bjorn Helgaas <bjorn_helgaas@hp.com>
 */


#include <linux/config.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/acpi.h>
#include <asm/iosapic.h>
#include <asm/efi.h>

#include "../drivers/acpi/include/platform/acgcc.h"
#include "../drivers/acpi/include/actypes.h"
#include "../drivers/acpi/include/acexcep.h"
#include "../drivers/acpi/include/acpixf.h"
#include "../drivers/acpi/include/actbl.h"
#include "../drivers/acpi/include/acconfig.h"
#include "../drivers/acpi/include/acmacros.h"
#include "../drivers/acpi/include/aclocal.h"
#include "../drivers/acpi/include/acobject.h"
#include "../drivers/acpi/include/acstruct.h"
#include "../drivers/acpi/include/acnamesp.h"
#include "../drivers/acpi/include/acutils.h"
#include "../drivers/acpi/acpi_bus.h"

#define PFX "hpzx1: "

struct fake_pci_dev {
	struct fake_pci_dev *next;
	unsigned char bus;
	unsigned int devfn;
	int sizing;		// in middle of BAR sizing operation?
	unsigned long csr_base;
	unsigned int csr_size;
	unsigned long mapped_csrs;	// ioremapped
};

static struct fake_pci_dev *fake_pci_head, **fake_pci_tail = &fake_pci_head;

static struct pci_ops *orig_pci_ops;

static inline struct fake_pci_dev *
fake_pci_find_slot(unsigned char bus, unsigned int devfn)
{
	struct fake_pci_dev *dev;

	for (dev = fake_pci_head; dev; dev = dev->next)
		if (dev->bus == bus && dev->devfn == devfn)
			return dev;
	return NULL;
}

static struct fake_pci_dev *
alloc_fake_pci_dev(void)
{
        struct fake_pci_dev *dev;

        dev = kmalloc(sizeof(*dev), GFP_KERNEL);
	if (!dev)
		return NULL;

	memset(dev, 0, sizeof(*dev));

        *fake_pci_tail = dev;
        fake_pci_tail = &dev->next;

        return dev;
}

#define HP_CFG_RD(sz, bits, name) \
static int hp_cfg_read##sz (struct pci_dev *dev, int where, u##bits *value) \
{ \
	struct fake_pci_dev *fake_dev; \
	if (!(fake_dev = fake_pci_find_slot(dev->bus->number, dev->devfn))) \
		return orig_pci_ops->name(dev, where, value); \
	\
	switch (where) { \
	case PCI_COMMAND: \
		*value = read##sz(fake_dev->mapped_csrs + where); \
		*value |= PCI_COMMAND_MEMORY; /* SBA omits this */ \
		break; \
	case PCI_BASE_ADDRESS_0: \
		if (fake_dev->sizing) \
			*value = ~(fake_dev->csr_size - 1); \
		else \
			*value = (fake_dev->csr_base & \
				    PCI_BASE_ADDRESS_MEM_MASK) | \
				PCI_BASE_ADDRESS_SPACE_MEMORY; \
		fake_dev->sizing = 0; \
		break; \
	default: \
		*value = read##sz(fake_dev->mapped_csrs + where); \
		break; \
	} \
	return PCIBIOS_SUCCESSFUL; \
}

#define HP_CFG_WR(sz, bits, name) \
static int hp_cfg_write##sz (struct pci_dev *dev, int where, u##bits value) \
{ \
	struct fake_pci_dev *fake_dev; \
	if (!(fake_dev = fake_pci_find_slot(dev->bus->number, dev->devfn))) \
		return orig_pci_ops->name(dev, where, value); \
	\
	switch (where) { \
	case PCI_BASE_ADDRESS_0: \
		if (value == (u##bits) ~0) \
			fake_dev->sizing = 1; \
		break; \
	default: \
		write##sz(value, fake_dev->mapped_csrs + where); \
		break; \
	} \
	return PCIBIOS_SUCCESSFUL; \
}

HP_CFG_RD(b,  8, read_byte)
HP_CFG_RD(w, 16, read_word)
HP_CFG_RD(l, 32, read_dword)
HP_CFG_WR(b,  8, write_byte)
HP_CFG_WR(w, 16, write_word)
HP_CFG_WR(l, 32, write_dword)

static struct pci_ops hp_pci_conf = {
	hp_cfg_readb,
	hp_cfg_readw,
	hp_cfg_readl,
	hp_cfg_writeb,
	hp_cfg_writew,
	hp_cfg_writel,
};

/*
 * Assume we'll never have a physical slot higher than 0x10, so we can
 * use slots above that for "fake" PCI devices to represent things
 * that only show up in the ACPI namespace.
 */
#define HP_MAX_SLOT	0x10

static struct fake_pci_dev *
hpzx1_fake_pci_dev(unsigned long addr, unsigned int bus, unsigned int size)
{
	struct fake_pci_dev *dev;
	int slot;

	// Note: lspci thinks 0x1f is invalid
	for (slot = 0x1e; slot > HP_MAX_SLOT; slot--) {
		if (!fake_pci_find_slot(bus, PCI_DEVFN(slot, 0)))
			break;
	}
	if (slot == HP_MAX_SLOT) {
		printk(KERN_ERR PFX
			"no slot space for device (0x%p) on bus 0x%02x\n",
			(void *) addr, bus);
		return NULL;
	}

	dev = alloc_fake_pci_dev();
	if (!dev) {
		printk(KERN_ERR PFX
			"no memory for device (0x%p) on bus 0x%02x\n",
			(void *) addr, bus);
		return NULL;
	}

	dev->bus = bus;
	dev->devfn = PCI_DEVFN(slot, 0);
	dev->csr_base = addr;
	dev->csr_size = size;

	/*
	 * Drivers should ioremap what they need, but we have to do
	 * it here, too, so PCI config accesses work.
	 */
	dev->mapped_csrs = (unsigned long) ioremap(dev->csr_base, dev->csr_size);

	return dev;
}

typedef struct {
	u8	guid_id;
	u8	guid[16];
	u8	csr_base[8];
	u8	csr_length[8];
} acpi_hp_vendor_long;

#define HP_CCSR_LENGTH 0x21
#define HP_CCSR_TYPE 0x2
#define HP_CCSR_GUID EFI_GUID(0x69e9adf9, 0x924f, 0xab5f,			\
			      0xf6, 0x4a, 0x24, 0xd2, 0x01, 0x37, 0x0e, 0xad)

extern acpi_status acpi_get_crs(acpi_handle, acpi_buffer *);
extern acpi_resource *acpi_get_crs_next(acpi_buffer *, int *);
extern acpi_resource_data *acpi_get_crs_type(acpi_buffer *, int *, int);
extern void acpi_dispose_crs(acpi_buffer *);

static acpi_status
hp_csr_space(acpi_handle obj, u64 *csr_base, u64 *csr_length)
{
	int i, offset = 0;
	acpi_status status;
	acpi_buffer buf;
	acpi_resource_vendor *res;
	acpi_hp_vendor_long *hp_res;
	efi_guid_t vendor_guid;

	*csr_base = 0;
	*csr_length = 0;

	status = acpi_get_crs(obj, &buf);
	if (status != AE_OK) {
		printk(KERN_ERR PFX "Unable to get _CRS data on object\n");
		return status;
	}

	res = (acpi_resource_vendor *)acpi_get_crs_type(&buf, &offset, ACPI_RSTYPE_VENDOR);
	if (!res) {
		printk(KERN_ERR PFX "Failed to find config space for device\n");
		acpi_dispose_crs(&buf);
		return AE_NOT_FOUND;
	}

	hp_res = (acpi_hp_vendor_long *)(res->reserved);

	if (res->length != HP_CCSR_LENGTH || hp_res->guid_id != HP_CCSR_TYPE) {
		printk(KERN_ERR PFX "Unknown Vendor data\n");
		acpi_dispose_crs(&buf);
		return AE_TYPE; /* Revisit error? */
	}

	memcpy(&vendor_guid, hp_res->guid, sizeof(efi_guid_t));
	if (efi_guidcmp(vendor_guid, HP_CCSR_GUID) != 0) {
		printk(KERN_ERR PFX "Vendor GUID does not match\n");
		acpi_dispose_crs(&buf);
		return AE_TYPE; /* Revisit error? */
	}

	for (i = 0 ; i < 8 ; i++) {
		*csr_base |= ((u64)(hp_res->csr_base[i]) << (i * 8));
		*csr_length |= ((u64)(hp_res->csr_length[i]) << (i * 8));
	}

	acpi_dispose_crs(&buf);

	return AE_OK;
}

static acpi_status
hpzx1_sba_probe(acpi_handle obj, u32 depth, void *context, void **ret)
{
	u64 csr_base = 0, csr_length = 0;
	char *name = context;
	struct fake_pci_dev *dev;
	acpi_status status;

	status = hp_csr_space(obj, &csr_base, &csr_length);

	if (status != AE_OK)
		return status;

	/*
	 * Only SBA shows up in ACPI namespace, so its CSR space
	 * includes both SBA and IOC.  Make SBA and IOC show up
	 * separately in PCI space.
	 */
	if ((dev = hpzx1_fake_pci_dev(csr_base, 0, 0x1000)))
		printk(KERN_INFO PFX "%s SBA at 0x%lx; pci dev %02x:%02x.%d\n",
			name, csr_base, dev->bus,
			PCI_SLOT(dev->devfn), PCI_FUNC(dev->devfn));
	if ((dev = hpzx1_fake_pci_dev(csr_base + 0x1000, 0, 0x1000)))
		printk(KERN_INFO PFX "%s IOC at 0x%lx; pci dev %02x:%02x.%d\n",
			name, csr_base + 0x1000, dev->bus,
			PCI_SLOT(dev->devfn), PCI_FUNC(dev->devfn));

	return AE_OK;
}

static acpi_status
hpzx1_lba_probe(acpi_handle obj, u32 depth, void *context, void **ret)
{
	acpi_status status;
	u64 csr_base = 0, csr_length = 0;
	char *name = context;
	NATIVE_UINT busnum = 0;
	struct fake_pci_dev *dev;

	status = hp_csr_space(obj, &csr_base, &csr_length);

	if (status != AE_OK)
		return status;

	status = acpi_evaluate_integer(obj, METHOD_NAME__BBN, NULL, &busnum);
	if (ACPI_FAILURE(status)) {
		printk(KERN_ERR PFX "evaluate _BBN fail=0x%x\n", status);
		busnum = 0;	// no _BBN; stick it on bus 0
	}

	if ((dev = hpzx1_fake_pci_dev(csr_base, busnum, csr_length)))
		printk(KERN_INFO PFX "%s LBA at 0x%lx, _BBN 0x%02x; "
			"pci dev %02x:%02x.%d\n",
			name, csr_base, (unsigned int) busnum, dev->bus,
			PCI_SLOT(dev->devfn), PCI_FUNC(dev->devfn));

	return AE_OK;
}

static void
hpzx1_acpi_dev_init(void)
{
	extern struct pci_ops *pci_root_ops;

	/*
	 * Make fake PCI devices for the following hardware in the
	 * ACPI namespace.  This makes it more convenient for drivers
	 * because they can claim these devices based on PCI
	 * information, rather than needing to know about ACPI.  The
	 * 64-bit "HPA" space for this hardware is available as BAR
	 * 0/1.
	 *
	 * HWP0001: Single IOC SBA w/o IOC in namespace
	 * HWP0002: LBA device
	 * HWP0003: AGP LBA device
	 */
	acpi_get_devices("HWP0001", hpzx1_sba_probe, "HWP0001", NULL);
#ifdef CONFIG_IA64_HP_PROTO
	if (fake_pci_tail != &fake_pci_head) {
#endif
	acpi_get_devices("HWP0002", hpzx1_lba_probe, "HWP0002", NULL);
	acpi_get_devices("HWP0003", hpzx1_lba_probe, "HWP0003", NULL);

#ifdef CONFIG_IA64_HP_PROTO
	}

#define ZX1_FUNC_ID_VALUE    (PCI_DEVICE_ID_HP_ZX1_SBA << 16) | PCI_VENDOR_ID_HP
	/*
	 * Early protos don't have bridges in the ACPI namespace, so
	 * if we didn't find anything, add the things we know are
	 * there.
	 */
	if (fake_pci_tail == &fake_pci_head) {
		u64 hpa, csr_base;
		struct fake_pci_dev *dev;

		csr_base = 0xfed00000UL;
		hpa = (u64) ioremap(csr_base, 0x1000);
		if (__raw_readl(hpa) == ZX1_FUNC_ID_VALUE) {
			if ((dev = hpzx1_fake_pci_dev(csr_base, 0, 0x1000)))
				printk(KERN_INFO PFX "HWP0001 SBA at 0x%lx; "
					"pci dev %02x:%02x.%d\n", csr_base,
					dev->bus, PCI_SLOT(dev->devfn),
					PCI_FUNC(dev->devfn));
			if ((dev = hpzx1_fake_pci_dev(csr_base + 0x1000, 0,
					0x1000)))
				printk(KERN_INFO PFX "HWP0001 IOC at 0x%lx; "
					"pci dev %02x:%02x.%d\n",
					csr_base + 0x1000,
					dev->bus, PCI_SLOT(dev->devfn),
					PCI_FUNC(dev->devfn));

			csr_base = 0xfed24000UL;
			iounmap(hpa);
			hpa = (u64) ioremap(csr_base, 0x1000);
			if ((dev = hpzx1_fake_pci_dev(csr_base, 0x40, 0x1000)))
				printk(KERN_INFO PFX "HWP0003 AGP LBA at "
					"0x%lx; pci dev %02x:%02x.%d\n",
					csr_base,
					dev->bus, PCI_SLOT(dev->devfn),
					PCI_FUNC(dev->devfn));
		}
		iounmap(hpa);
	}
#endif

	if (fake_pci_tail == &fake_pci_head)
		return;

	/*
	 * Replace PCI ops, but only if we made fake devices.
	 */
	orig_pci_ops = pci_root_ops;
	pci_root_ops = &hp_pci_conf;
}

extern void sba_init(void);

void
hpzx1_pci_fixup (int phase)
{
	if (phase == 0)
		hpzx1_acpi_dev_init();
	iosapic_pci_fixup(phase);
        if (phase == 1)
		sba_init();
}
