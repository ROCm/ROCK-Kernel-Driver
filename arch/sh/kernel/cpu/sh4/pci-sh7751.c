/*
 *	Low-Level PCI Support for the SH7751
 *
 *  Dustin McIntire (dustin@sensoria.com)
 *	Derived from arch/i386/kernel/pci-*.c which bore the message:
 *	(c) 1999--2000 Martin Mares <mj@ucw.cz>
 *	
 *  May be copied or modified under the terms of the GNU General Public
 *  License.  See linux/COPYING for more information.
 *
*/

#include <linux/config.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/pci.h>
#include <linux/sched.h>
#include <linux/ioport.h>
#include <linux/errno.h>
#include <linux/irq.h>

#include <asm/machvec.h>
#include <asm/io.h>
#include <asm/pci-sh7751.h>

struct pci_ops *pci_check_direct(void);
void pcibios_resource_survey(void);
static u8 pcibios_swizzle(struct pci_dev *dev, u8 *pin);
static int pcibios_lookup_irq(struct pci_dev *dev, u8 slot, u8 pin);

unsigned int pci_probe = PCI_PROBE_BIOS | PCI_PROBE_CONF1;
int pcibios_last_bus = -1;
struct pci_bus *pci_root_bus;
struct pci_ops *pci_root_ops;

/*
 * Direct access to PCI hardware...
 */

#ifdef CONFIG_PCI_DIRECT


#define CONFIG_CMD(bus, devfn, where) (0x80000000 | (bus->number << 16) | (devfn << 8) | (where & ~3))

#define PCI_REG(reg) (SH7751_PCIREG_BASE+reg)

/*
 * Functions for accessing PCI configuration space with type 1 accesses
 */
static int pci_conf1_read(struct pci_bus *bus, unsigned int devfn, int where, int size, u32 *value)
{
	unsigned long flags;
	u32 word;

	/* 
	 * PCIPDR may only be accessed as 32 bit words, 
	 * so we must do byte alignment by hand 
	 */
	local_irq_save(flags);
	outl(CONFIG_CMD(bus,devfn,where), PCI_REG(SH7751_PCIPAR));
	word = inl(PCI_REG(SH7751_PCIPDR));
	local_irq_restore(flags);

	switch (size) {
	case 1:
		switch (where & 0x3) {
		    	case 3: *value = (u8)(word >> 24); break;
			case 2: *value = (u8)(word >> 16); break;
			case 1: *value = (u8)(word >> 8);  break;
		}
		break;
	case 2:
		switch (where & 0x2) {
			case 2:	*value = (u16)(word >> 16); break;
			case 1: *value = (u16)(word >> 8);  break;
		}
		break;
	case 4:
		*value = word;
		break;
	}

	PCIDBG(4,"pci_conf1_read@0x%08x=0x%x\n", CONFIG_CMD(bus,devfn,where),*value); 

	return PCIBIOS_SUCCESSFUL;
}

/* 
 * Since SH7751 only does 32bit access we'll have to do a read,mask,write operation.  
 * We'll allow an odd byte offset, though it should be illegal.
 */ 
static int pci_conf1_write(struct pci_bus *bus, unsigned int devfn, int where, int size, u32 value)
{
	u32 word,mask;
	unsigned long flags;
	u32 shift = (where & 3) * 8;

	if(size == 1) {
		mask = ((1 << 8) - 1) << shift;  // create the byte mask
	} else if(size == 2){
		if(shift == 24)
			return PCIBIOS_BAD_REGISTER_NUMBER;           
		mask = ((1 << 16) - 1) << shift;  // create the word mask
	}

	local_irq_save(flags);
	outl(CONFIG_CMD(bus,devfn,where), PCI_REG(SH7751_PCIPAR));

	if(size == 4){
		outl(value, PCI_REG(SH7751_PCIPDR));
		local_irq_restore(flags);

		PCIDBG(4,"pci_conf1_write@0x%08x=0x%x\n", CONFIG_CMD(bus,devfn,where),value);

		return PCIBIOS_SUCCESSFUL;
	}

	word = inl(PCI_REG(SH7751_PCIPDR)) ;
	word &= ~mask;
	word |= value << shift;
	outl(word, PCI_REG(SH7751_PCIPDR));
	local_irq_restore(flags);

	PCIDBG(4,"pci_conf1_write@0x%08x=0x%x\n", CONFIG_CMD(bus,devfn,where),word);

	return PCIBIOS_SUCCESSFUL;
}

#undef CONFIG_CMD

static struct pci_ops pci_direct_conf1 = {
	.read =		pci_conf1_read,
	.write = 	pci_conf1_write,
};

struct pci_ops * __init pci_check_direct(void)
{
	unsigned int tmp, id;

	/* check for SH7751 hardware */
	id = (SH7751_DEVICE_ID << 16) | SH7751_VENDOR_ID;
	if(inl(SH7751_PCIREG_BASE+SH7751_PCICONF0) != id) {
		PCIDBG(2,"PCI: This is not an SH7751\n");
		return NULL;
	}
	/*
	 * Check if configuration works.
	 */
	if (pci_probe & PCI_PROBE_CONF1) {
		tmp = inl (PCI_REG(SH7751_PCIPAR));
		outl (0x80000000, PCI_REG(SH7751_PCIPAR));
		if (inl (PCI_REG(SH7751_PCIPAR)) == 0x80000000) {
			outl (tmp, PCI_REG(SH7751_PCIPAR));
			printk(KERN_INFO "PCI: Using configuration type 1\n");
			request_region(PCI_REG(SH7751_PCIPAR), 8, "PCI conf1");
			return &pci_direct_conf1;
		}
		outl (tmp, PCI_REG(SH7751_PCIPAR));
	}

	PCIDBG(2,"PCI: pci_check_direct failed\n");
	return NULL;
}

#endif

/*
 * BIOS32 and PCI BIOS handling.
 * 
 * The BIOS version of the pci functions is not yet implemented but it is left
 * in for completeness.  Currently an error will be generated at compile time. 
 */
 
#ifdef CONFIG_PCI_BIOS

#error PCI BIOS is not yet supported on SH7751

#endif /* CONFIG_PCI_BIOS */

/***************************************************************************************/

/*
 *  Handle bus scanning and fixups ....
 */


/*
 * Discover remaining PCI buses in case there are peer host bridges.
 * We use the number of last PCI bus provided by the PCI BIOS.
 */
static void __init pcibios_fixup_peer_bridges(void)
{
	int n;
	struct pci_bus bus;
	struct pci_dev dev;
	u16 l;

	if (pcibios_last_bus <= 0 || pcibios_last_bus >= 0xff)
		return;
	PCIDBG(2,"PCI: Peer bridge fixup\n");
	for (n=0; n <= pcibios_last_bus; n++) {
		if (pci_find_bus(0, n))
			continue;
		bus.number = n;
		bus.ops = pci_root_ops;
		dev.bus = &bus;
		for(dev.devfn=0; dev.devfn<256; dev.devfn += 8)
			if (!pci_read_config_word(&dev, PCI_VENDOR_ID, &l) &&
			    l != 0x0000 && l != 0xffff) {
				PCIDBG(3,"Found device at %02x:%02x [%04x]\n", n, dev.devfn, l);
				printk(KERN_INFO "PCI: Discovered peer bus %02x\n", n);
				pci_scan_bus(n, pci_root_ops, NULL);
				break;
			}
	}
}


static void __init pci_fixup_ide_bases(struct pci_dev *d)
{
	int i;

	/*
	 * PCI IDE controllers use non-standard I/O port decoding, respect it.
	 */
	if ((d->class >> 8) != PCI_CLASS_STORAGE_IDE)
		return;
	PCIDBG(3,"PCI: IDE base address fixup for %s\n", pci_name(d));
	for(i=0; i<4; i++) {
		struct resource *r = &d->resource[i];
		if ((r->start & ~0x80) == 0x374) {
			r->start |= 2;
			r->end = r->start;
		}
	}
}


/* Add future fixups here... */
struct pci_fixup pcibios_fixups[] = {
	{ PCI_FIXUP_HEADER,	PCI_ANY_ID,	PCI_ANY_ID,	pci_fixup_ide_bases },
	{ 0 }
};

/*
 *  Called after each bus is probed, but before its children
 *  are examined.
 */

void __init pcibios_fixup_bus(struct pci_bus *b)
{
	pci_read_bridge_bases(b);
}

/*
 * Initialization. Try all known PCI access methods. Note that we support
 * using both PCI BIOS and direct access: in such cases, we use I/O ports
 * to access config space.
 * 
 * Note that the platform specific initialization (BSC registers, and memory
 * space mapping) will be called via the machine vectors (sh_mv.mv_pci_init()) if it
 * exitst and via the platform defined function pcibios_init_platform().  
 * See pci_bigsur.c for implementation;
 * 
 * The BIOS version of the pci functions is not yet implemented but it is left
 * in for completeness.  Currently an error will be genereated at compile time. 
 */

void __init pcibios_init(void)
{
	struct pci_ops *bios = NULL;
	struct pci_ops *dir = NULL;

	PCIDBG(1,"PCI: Starting intialization.\n");
#ifdef CONFIG_PCI_BIOS
	if ((pci_probe & PCI_PROBE_BIOS) && ((bios = pci_find_bios()))) {
		pci_probe |= PCI_BIOS_SORT;
		pci_bios_present = 1;
	}
#endif
#ifdef CONFIG_PCI_DIRECT
	if (pci_probe & PCI_PROBE_CONF1 )
		dir = pci_check_direct();
#endif
	if (dir) {
		pci_root_ops = dir;
	    if(!pcibios_init_platform())
			PCIDBG(1,"PCI: Initialization failed\n");
	    if (sh_mv.mv_init_pci != NULL)
            sh_mv.mv_init_pci();
	}
	else if (bios)
		pci_root_ops = bios;
	else {
		PCIDBG(1,"PCI: No PCI bus detected\n");
		return;
	}

	PCIDBG(1,"PCI: Probing PCI hardware\n");
	pci_root_bus = pci_scan_bus(0, pci_root_ops, NULL);
	//pci_assign_unassigned_resources();
	pci_fixup_irqs(pcibios_swizzle, pcibios_lookup_irq);
	pcibios_fixup_peer_bridges();
	pcibios_resource_survey();

#ifdef CONFIG_PCI_BIOS
	if ((pci_probe & PCI_BIOS_SORT) && !(pci_probe & PCI_NO_SORT))
		pcibios_sort();
#endif
}

char * __init pcibios_setup(char *str)
{
	if (!strcmp(str, "off")) {
		pci_probe = 0;
		return NULL;
	}
#ifdef CONFIG_PCI_BIOS
	else if (!strcmp(str, "bios")) {
		pci_probe = PCI_PROBE_BIOS;
		return NULL;
	} else if (!strcmp(str, "nobios")) {
		pci_probe &= ~PCI_PROBE_BIOS;
		return NULL;
	} else if (!strcmp(str, "nosort")) {
		pci_probe |= PCI_NO_SORT;
		return NULL;
	} else if (!strcmp(str, "biosirq")) {
		pci_probe |= PCI_BIOS_IRQ_SCAN;
		return NULL;
	}
#endif
#ifdef CONFIG_PCI_DIRECT
	else if (!strcmp(str, "conf1")) {
		pci_probe = PCI_PROBE_CONF1 | PCI_NO_CHECKS;
		return NULL;
	}
#endif
	else if (!strcmp(str, "rom")) {
		pci_probe |= PCI_ASSIGN_ROMS;
		return NULL;
	} else if (!strncmp(str, "lastbus=", 8)) {
		pcibios_last_bus = simple_strtol(str+8, NULL, 0);
		return NULL;
	}
	return str;
}

/*
 *    Allocate the bridge and device resources
 */

static void __init pcibios_allocate_bus_resources(struct list_head *bus_list)
{
	struct list_head *ln;
	struct pci_bus *bus;
	struct pci_dev *dev;
	int idx;
	struct resource *r, *pr;
	
	PCIDBG(2,"PCI: pcibios_allocate_bus_reasources called\n" );
	/* Depth-First Search on bus tree */
	for (ln=bus_list->next; ln != bus_list; ln=ln->next) {
		bus = pci_bus_b(ln);
		if ((dev = bus->self)) {
			for (idx = PCI_BRIDGE_RESOURCES; idx < PCI_NUM_RESOURCES; idx++) {
				r = &dev->resource[idx];
				if (!r->start)
					continue;
				pr = pci_find_parent_resource(dev, r);
				if (!pr || request_resource(pr, r) < 0)
					printk(KERN_ERR "PCI: Cannot allocate resource region %d of bridge %s\n", idx, pci_name(dev));
			}
		}
		pcibios_allocate_bus_resources(&bus->children);
	}
}

static void __init pcibios_allocate_resources(int pass)
{
	struct pci_dev *dev;
	int idx, disabled;
	u16 command;
	struct resource *r, *pr;

	PCIDBG(2,"PCI: pcibios_allocate_resources pass %d called\n", pass);
	pci_for_each_dev(dev) {
		pci_read_config_word(dev, PCI_COMMAND, &command);
		for(idx = 0; idx < 6; idx++) {
			r = &dev->resource[idx];
			if (r->parent)		/* Already allocated */
				continue;
			if (!r->start)		/* Address not assigned at all */
				continue;
			if (r->flags & IORESOURCE_IO)
				disabled = !(command & PCI_COMMAND_IO);
			else
				disabled = !(command & PCI_COMMAND_MEMORY);
			if (pass == disabled) {
				PCIDBG(3,"PCI: Resource %08lx-%08lx (f=%lx, d=%d, p=%d)\n",
				    r->start, r->end, r->flags, disabled, pass);
				pr = pci_find_parent_resource(dev, r);
				if (!pr || request_resource(pr, r) < 0) {
					printk(KERN_ERR "PCI: Cannot allocate resource region %d of device %s\n", idx, pci_name(dev));
					/* We'll assign a new address later */
					r->end -= r->start;
					r->start = 0;
				}
			}
		}
		if (!pass) {
			r = &dev->resource[PCI_ROM_RESOURCE];
			if (r->flags & PCI_ROM_ADDRESS_ENABLE) {
				/* Turn the ROM off, leave the resource region, but keep it unregistered. */
				u32 reg;
				PCIDBG(3,"PCI: Switching off ROM of %s\n", pci_name(dev));
				r->flags &= ~PCI_ROM_ADDRESS_ENABLE;
				pci_read_config_dword(dev, dev->rom_base_reg, &reg);
				pci_write_config_dword(dev, dev->rom_base_reg, reg & ~PCI_ROM_ADDRESS_ENABLE);
			}
		}
	}
}

static void __init pcibios_assign_resources(void)
{
	struct pci_dev *dev;
	int idx;
	struct resource *r;

	PCIDBG(2,"PCI: pcibios_assign_resources called\n");
	pci_for_each_dev(dev) {
		int class = dev->class >> 8;

		/* Don't touch classless devices and host bridges */
		if (!class || class == PCI_CLASS_BRIDGE_HOST)
			continue;

		for(idx=0; idx<6; idx++) {
			r = &dev->resource[idx];

			/*
			 *  Don't touch IDE controllers and I/O ports of video cards!
			 */
			if ((class == PCI_CLASS_STORAGE_IDE && idx < 4) ||
			    (class == PCI_CLASS_DISPLAY_VGA && (r->flags & IORESOURCE_IO)))
				continue;

			/*
			 *  We shall assign a new address to this resource, either because
			 *  the BIOS forgot to do so or because we have decided the old
			 *  address was unusable for some reason.
			 */
			if (!r->start && r->end)
				pci_assign_resource(dev, idx);
		}

		if (pci_probe & PCI_ASSIGN_ROMS) {
			r = &dev->resource[PCI_ROM_RESOURCE];
			r->end -= r->start;
			r->start = 0;
			if (r->end)
				pci_assign_resource(dev, PCI_ROM_RESOURCE);
		}
	}
}

void __init pcibios_resource_survey(void)
{
	PCIDBG(1,"PCI: Allocating resources\n");
	pcibios_allocate_bus_resources(&pci_root_buses);
	pcibios_allocate_resources(0);
	pcibios_allocate_resources(1);
	pcibios_assign_resources();
}


/***************************************************************************************/
/* 
 * 	IRQ functions 
 */
static u8 __init pcibios_swizzle(struct pci_dev *dev, u8 *pin)
{
	/* no swizzling */
	return PCI_SLOT(dev->devfn);
}

static int pcibios_lookup_irq(struct pci_dev *dev, u8 slot, u8 pin)
{
	int irq = -1;

	/* now lookup the actual IRQ on a platform specific basis (pci-'platform'.c) */
	irq = pcibios_map_platform_irq(slot,pin);
	if( irq < 0 ) {
	    PCIDBG(3,"PCI: Error mapping IRQ on device %s\n", dev->name);
		return irq;
	}
	
	PCIDBG(2,"Setting IRQ for slot %s to %d\n", pci_name(dev), irq);

	return irq;
}
