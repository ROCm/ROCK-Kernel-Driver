#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/pci.h>

#include <asm/ddb5xxx/ddb5xxx.h>
#include <asm/ddb5xxx/debug.h>
#include <asm/ddb5xxx/pci.h>

static struct resource extpci_io_resource = {
	"ext pci IO space", 
	DDB_PCI0_IO_BASE - DDB_PCI_IO_BASE,
	DDB_PCI0_IO_BASE - DDB_PCI_IO_BASE + DDB_PCI0_IO_SIZE -1,
	IORESOURCE_IO};

static struct resource extpci_mem_resource = {
	"ext pci memory space", 
	DDB_PCI0_MEM_BASE,
	DDB_PCI0_MEM_BASE + DDB_PCI0_MEM_SIZE -1,
	IORESOURCE_MEM};

static struct resource iopci_io_resource = {
	"io pci IO space", 
	DDB_PCI1_IO_BASE - DDB_PCI_IO_BASE,
	DDB_PCI1_IO_BASE - DDB_PCI_IO_BASE + DDB_PCI1_IO_SIZE -1,
	IORESOURCE_IO};

static struct resource iopci_mem_resource = {
	"ext pci memory space", 
	DDB_PCI1_MEM_BASE,
	DDB_PCI1_MEM_BASE + DDB_PCI1_MEM_SIZE -1,
	IORESOURCE_MEM};

extern struct pci_ops ddb5477_ext_pci_ops;
extern struct pci_ops ddb5477_io_pci_ops;

struct pci_channel mips_pci_channels[] = {
	{ &ddb5477_ext_pci_ops, &extpci_io_resource, &extpci_mem_resource },
	{ &ddb5477_io_pci_ops, &iopci_io_resource, &iopci_mem_resource },
	{ NULL, NULL, NULL}
};


/*
 * we fix up irqs based on the slot number.
 * The first entry is at AD:11.
 * Fortunately this works because, although we have two pci buses,
 * they all have different slot numbers.
 * 
 * This does not work for devices on sub-buses.
 *
 * Note that the irq number in the array is relative number in vrc5477.
 * We need to translate it to global irq number.
 */

/*
 * irq mapping : PCI int # -> vrc5477 irq #
 * based on vrc5477 manual page 46
 */
#define		PCI_EXT_INTA		8
#define		PCI_EXT_INTB		9
#define		PCI_EXT_INTC		10
#define		PCI_EXT_INTD		11
#define		PCI_EXT_INTE		12

#define		PCI_IO_INTA		16
#define		PCI_IO_INTB		17
#define		PCI_IO_INTC		18
#define		PCI_IO_INTD		19

/* 
 * irq mapping : device -> pci int #, 
 * ddb5477 board manual page 4  and vrc5477 manual page 46
 */
#define		INT_ONBOARD_TULIP	PCI_EXT_INTA
#define		INT_SLOT1		PCI_EXT_INTB
#define		INT_SLOT2		PCI_EXT_INTC
#define		INT_SLOT3		PCI_EXT_INTD
#define		INT_SLOT4		PCI_EXT_INTE

#define		INT_USB_HOST		PCI_IO_INTA
#define		INT_USB_PERI		PCI_IO_INTB
#define		INT_AC97		PCI_IO_INTC

/*
 * based on ddb5477 manual page 11
 */
#define		MAX_SLOT_NUM		21
static unsigned char irq_map[MAX_SLOT_NUM] = {
	/* AD:11 */ 0xff, 0xff, 0xff, 0xff, 
	/* AD:15 */ INT_ONBOARD_TULIP, INT_SLOT1, INT_SLOT2, INT_SLOT3,
	/* AD:19 */ INT_SLOT4, 0xff, 0xff, 0xff,
	/* AD:23 */ 0xff, 0xff, 0xff, 0xff,
	/* AD:27 */ 0xff, 0xff, INT_AC97, INT_USB_PERI, 
	/* AD:31 */ INT_USB_HOST
};

extern int vrc5477_irq_to_irq(int irq);
void __init pcibios_fixup_irqs(void)
{
        struct pci_dev *dev = NULL;
        int slot_num;

	while ((dev = pci_find_device(PCI_ANY_ID, PCI_ANY_ID, dev)) != NULL) {
		slot_num = PCI_SLOT(dev->devfn);
		MIPS_ASSERT(slot_num < MAX_SLOT_NUM);
		MIPS_ASSERT(irq_map[slot_num] != 0xff);

		pci_write_config_byte(dev, 
				      PCI_INTERRUPT_LINE,
				      irq_map[slot_num]);
		dev->irq = vrc5477_irq_to_irq(irq_map[slot_num]);
	}
}

#if defined(CONFIG_LL_DEBUG)
extern void jsun_scan_pci_bus(void);
extern void jsun_assign_pci_resource(void);
#endif
void __init ddb_pci_reset_bus(void)
{	
	u32 temp;

	/*
	 * I am not sure about the "official" procedure, the following
	 * steps work as far as I know:
	 * We first set PCI cold reset bit (bit 31) in PCICTRL-H.
	 * Then we clear the PCI warm reset bit (bit 30) to 0 in PCICTRL-H.
	 * The same is true for both PCI channels.
	 */
	temp = ddb_in32(DDB_PCICTL0_H);
	temp |= 0x80000000;
	ddb_out32(DDB_PCICTL0_H, temp);
	temp &= ~0xc0000000;
	ddb_out32(DDB_PCICTL0_H, temp);

	temp = ddb_in32(DDB_PCICTL1_H);
	temp |= 0x80000000;
	ddb_out32(DDB_PCICTL1_H, temp);
	temp &= ~0xc0000000;
	ddb_out32(DDB_PCICTL1_H, temp);
}

unsigned __init int pcibios_assign_all_busses(void)
{
	return 1;
}
