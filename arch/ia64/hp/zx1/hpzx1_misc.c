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
#include <linux/efi.h>

#include <asm/dma.h>
#include <asm/iosapic.h>

extern acpi_status acpi_evaluate_integer (acpi_handle, acpi_string, acpi_object_list *,
					  unsigned long *);

#define PFX "hpzx1: "

static int hpzx1_devices;

struct fake_pci_dev {
	unsigned long csr_base;
	unsigned long csr_size;
	unsigned long mapped_csrs;	// ioremapped
	int sizing;			// in middle of BAR sizing operation?
};

static struct pci_ops *orig_pci_ops;

#define HP_CFG_RD(sz, bits, name)						\
static int hp_cfg_read##sz (struct pci_dev *dev, int where, u##bits *value)	\
{										\
	struct fake_pci_dev *fake_dev;						\
	if (!(fake_dev = (struct fake_pci_dev *) dev->sysdata))			\
		return orig_pci_ops->name(dev, where, value);			\
										\
	if (where == PCI_BASE_ADDRESS_0) {					\
		if (fake_dev->sizing)						\
			*value = ~(fake_dev->csr_size - 1);			\
		else								\
			*value = (fake_dev->csr_base &				\
				    PCI_BASE_ADDRESS_MEM_MASK) |		\
				PCI_BASE_ADDRESS_SPACE_MEMORY;			\
		fake_dev->sizing = 0;						\
		return PCIBIOS_SUCCESSFUL;					\
	}									\
	*value = read##sz(fake_dev->mapped_csrs + where);			\
	if (where == PCI_COMMAND)						\
		*value |= PCI_COMMAND_MEMORY; /* SBA omits this */		\
	return PCIBIOS_SUCCESSFUL;						\
}

#define HP_CFG_WR(sz, bits, name)						\
static int hp_cfg_write##sz (struct pci_dev *dev, int where, u##bits value)	\
{										\
	struct fake_pci_dev *fake_dev;						\
										\
	if (!(fake_dev = (struct fake_pci_dev *) dev->sysdata))			\
		return orig_pci_ops->name(dev, where, value);			\
										\
	if (where == PCI_BASE_ADDRESS_0) {					\
		if (value == (u##bits) ~0)					\
			fake_dev->sizing = 1;					\
		return PCIBIOS_SUCCESSFUL;					\
	} else									\
		write##sz(value, fake_dev->mapped_csrs + where);		\
	return PCIBIOS_SUCCESSFUL;						\
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

static void
hpzx1_fake_pci_dev(char *name, unsigned int busnum, unsigned long addr, unsigned int size)
{
	struct fake_pci_dev *fake;
	int slot, ret;
	struct pci_dev *dev;
	struct pci_bus *b, *bus = NULL;
	u8 hdr;

        fake = kmalloc(sizeof(*fake), GFP_KERNEL);
	if (!fake) {
		printk(KERN_ERR PFX "No memory for %s (0x%p) sysdata\n", name, (void *) addr);
		return;
	}

	memset(fake, 0, sizeof(*fake));
	fake->csr_base = addr;
	fake->csr_size = size;
	fake->mapped_csrs = (unsigned long) ioremap(addr, size);
	fake->sizing = 0;

	pci_for_each_bus(b)
		if (busnum == b->number) {
			bus = b;
			break;
		}

	if (!bus) {
		printk(KERN_ERR PFX "No host bus 0x%02x for %s (0x%p)\n",
		       busnum, name, (void *) addr);
		kfree(fake);
		return;
	}

	for (slot = 0x1e; slot; slot--)
		if (!pci_find_slot(busnum, PCI_DEVFN(slot, 0)))
			break;

	if (slot < 0) {
		printk(KERN_ERR PFX "No space for %s (0x%p) on bus 0x%02x\n",
		       name, (void *) addr, busnum);
		kfree(fake);
		return;
	}

        dev = kmalloc(sizeof(*dev), GFP_KERNEL);
	if (!dev) {
		printk(KERN_ERR PFX "No memory for %s (0x%p)\n", name, (void *) addr);
		kfree(fake);
		return;
	}

	bus->ops = &hp_pci_conf;	// replace pci ops for this bus

	memset(dev, 0, sizeof(*dev));
	dev->bus = bus;
	dev->sysdata = fake;
	dev->dev.parent = bus->dev;
	dev->dev.bus = &pci_bus_type;
	dev->devfn = PCI_DEVFN(slot, 0);
	pci_read_config_word(dev, PCI_VENDOR_ID, &dev->vendor);
	pci_read_config_word(dev, PCI_DEVICE_ID, &dev->device);
	pci_read_config_byte(dev, PCI_HEADER_TYPE, &hdr);
	dev->hdr_type = hdr & 0x7f;

	pci_setup_device(dev);

	// pci_insert_device() without running /sbin/hotplug
	list_add_tail(&dev->bus_list, &bus->devices);
	list_add_tail(&dev->global_list, &pci_devices);

	strcpy(dev->dev.name, dev->name);
	strcpy(dev->dev.bus_id, dev->slot_name);
	ret = device_register(&dev->dev);
	if (ret < 0)
		printk(KERN_INFO PFX "fake device registration failed (%d)\n", ret);

	printk(KERN_INFO PFX "%s at 0x%lx; pci dev %s\n", name, addr, dev->slot_name);

	hpzx1_devices++;
}

typedef struct {
	u8	guid_id;
	u8	guid[16];
	u8	csr_base[8];
	u8	csr_length[8];
} acpi_hp_vendor_long;

#define HP_CCSR_LENGTH	0x21
#define HP_CCSR_TYPE	0x2
#define HP_CCSR_GUID	EFI_GUID(0x69e9adf9, 0x924f, 0xab5f,				\
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
	if (ACPI_FAILURE(status)) {
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
	acpi_status status;
	char *name = context;
	char fullname[16];

	status = hp_csr_space(obj, &csr_base, &csr_length);
	if (ACPI_FAILURE(status))
		return status;

	/*
	 * Only SBA shows up in ACPI namespace, so its CSR space
	 * includes both SBA and IOC.  Make SBA and IOC show up
	 * separately in PCI space.
	 */
	sprintf(fullname, "%s SBA", name);
	hpzx1_fake_pci_dev(fullname, 0, csr_base, 0x1000);
	sprintf(fullname, "%s IOC", name);
	hpzx1_fake_pci_dev(fullname, 0, csr_base + 0x1000, 0x1000);

	return AE_OK;
}

static acpi_status
hpzx1_lba_probe(acpi_handle obj, u32 depth, void *context, void **ret)
{
	u64 csr_base = 0, csr_length = 0;
	acpi_status status;
	NATIVE_UINT busnum;
	char *name = context;
	char fullname[32];

	status = hp_csr_space(obj, &csr_base, &csr_length);
	if (ACPI_FAILURE(status))
		return status;

	status = acpi_evaluate_integer(obj, METHOD_NAME__BBN, NULL, &busnum);
	if (ACPI_FAILURE(status)) {
		printk(KERN_WARNING PFX "evaluate _BBN fail=0x%x\n", status);
		busnum = 0;	// no _BBN; stick it on bus 0
	}

	sprintf(fullname, "%s _BBN 0x%02x", name, (unsigned int) busnum);
	hpzx1_fake_pci_dev(fullname, busnum, csr_base, csr_length);

	return AE_OK;
}

static void
hpzx1_acpi_dev_init(void)
{
	extern struct pci_ops *pci_root_ops;

	orig_pci_ops = pci_root_ops;

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
	if (hpzx1_devices) {
#endif
	acpi_get_devices("HWP0002", hpzx1_lba_probe, "HWP0002 PCI LBA", NULL);
	acpi_get_devices("HWP0003", hpzx1_lba_probe, "HWP0003 AGP LBA", NULL);

#ifdef CONFIG_IA64_HP_PROTO
	}

#define ZX1_FUNC_ID_VALUE    (PCI_DEVICE_ID_HP_ZX1_SBA << 16) | PCI_VENDOR_ID_HP
	/*
	 * Early protos don't have bridges in the ACPI namespace, so
	 * if we didn't find anything, add the things we know are
	 * there.
	 */
	if (hpzx1_devices == 0) {
		u64 hpa, csr_base;

		csr_base = 0xfed00000UL;
		hpa = (u64) ioremap(csr_base, 0x2000);
		if (__raw_readl(hpa) == ZX1_FUNC_ID_VALUE) {
			hpzx1_fake_pci_dev("HWP0001 SBA", 0, csr_base, 0x1000);
			hpzx1_fake_pci_dev("HWP0001 IOC", 0, csr_base + 0x1000,
					    0x1000);

			csr_base = 0xfed24000UL;
			iounmap(hpa);
			hpa = (u64) ioremap(csr_base, 0x1000);
			hpzx1_fake_pci_dev("HWP0003 AGP LBA", 0x40, csr_base,
					    0x1000);
		}
		iounmap(hpa);
	}
#endif
}

extern void sba_init(void);

void
hpzx1_pci_fixup (int phase)
{
	iosapic_pci_fixup(phase);
	switch (phase) {
	      case 0:
		/* zx1 has a hardware I/O TLB which lets us DMA from any device to any address */
		MAX_DMA_ADDRESS = ~0UL;
		break;

	      case 1:
		hpzx1_acpi_dev_init();
		sba_init();
		break;
	}
}
