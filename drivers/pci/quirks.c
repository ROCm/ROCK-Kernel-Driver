/*
 * $Id: quirks.c,v 1.5 1998/05/02 19:24:14 mj Exp $
 *
 *  This file contains work-arounds for many known PCI hardware
 *  bugs.  Devices present only on certain architectures (host
 *  bridges et cetera) should be handled in arch-specific code.
 *
 *  Copyright (c) 1999 Martin Mares <mj@suse.cz>
 *
 *  The bridge optimization stuff has been removed. If you really
 *  have a silly BIOS which is unable to set your host bridge right,
 *  use the PowerTweak utility (see http://powertweak.sourceforge.net).
 */

#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/init.h>

#undef DEBUG

/* Deal with broken BIOS'es that neglect to enable passive release,
   which can cause problems in combination with the 82441FX/PPro MTRRs */
static void __init quirk_passive_release(struct pci_dev *dev)
{
	struct pci_dev *d = NULL;
	unsigned char dlc;

	/* We have to make sure a particular bit is set in the PIIX3
	   ISA bridge, so we have to go out and find it. */
	while ((d = pci_find_device(PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_82371SB_0, d))) {
		pci_read_config_byte(d, 0x82, &dlc);
		if (!(dlc & 1<<1)) {
			printk("PCI: PIIX3: Enabling Passive Release on %s\n", d->slot_name);
			dlc |= 1<<1;
			pci_write_config_byte(d, 0x82, dlc);
		}
	}
}

/*  The VIA VP2/VP3/MVP3 seem to have some 'features'. There may be a workaround
    but VIA don't answer queries. If you happen to have good contacts at VIA
    ask them for me please -- Alan 
    
    This appears to be BIOS not version dependent. So presumably there is a 
    chipset level fix */
    

int isa_dma_bridge_buggy;		/* Exported */
    
static void __init quirk_isa_dma_hangs(struct pci_dev *dev)
{
	if (!isa_dma_bridge_buggy) {
		isa_dma_bridge_buggy=1;
		printk(KERN_INFO "Activating ISA DMA hang workarounds.\n");
	}
}

int pci_pci_problems;

/*
 *	Chipsets where PCI->PCI transfers vanish or hang
 */

static void __init quirk_nopcipci(struct pci_dev *dev)
{
	if((pci_pci_problems&PCIPCI_FAIL)==0)
	{
		printk(KERN_INFO "Disabling direct PCI/PCI transfers.\n");
		pci_pci_problems|=PCIPCI_FAIL;
	}
}

/*
 *	Triton requires workarounds to be used by the drivers
 */
 
static void __init quirk_triton(struct pci_dev *dev)
{
	if((pci_pci_problems&PCIPCI_TRITON)==0)
	{
		printk(KERN_INFO "Limiting direct PCI/PCI transfers.\n");
		pci_pci_problems|=PCIPCI_TRITON;
	}
}

/*
 *	VIA Apollo VP3 needs ETBF on BT848/878
 */
 
static void __init quirk_viaetbf(struct pci_dev *dev)
{
	if((pci_pci_problems&PCIPCI_VIAETBF)==0)
	{
		printk(KERN_INFO "Limiting direct PCI/PCI transfers.\n");
		pci_pci_problems|=PCIPCI_VIAETBF;
	}
}

/*
 *	Natoma has some interesting boundary conditions with Zoran stuff
 *	at least
 */
 
static void __init quirk_natoma(struct pci_dev *dev)
{
	if((pci_pci_problems&PCIPCI_NATOMA)==0)
	{
		printk(KERN_INFO "Limiting direct PCI/PCI transfers.\n");
		pci_pci_problems|=PCIPCI_NATOMA;
	}
}

/*
 *  S3 868 and 968 chips report region size equal to 32M, but they decode 64M.
 *  If it's needed, re-allocate the region.
 */

static void __init quirk_s3_64M(struct pci_dev *dev)
{
	struct resource *r = &dev->resource[0];

	if ((r->start & 0x3ffffff) || r->end != r->start + 0x3ffffff) {
		r->start = 0;
		r->end = 0x3ffffff;
	}
}

static void __init quirk_io_region(struct pci_dev *dev, unsigned region, unsigned size, int nr)
{
	region &= ~(size-1);
	if (region) {
		struct resource *res = dev->resource + nr;

		res->name = dev->name;
		res->start = region;
		res->end = region + size - 1;
		res->flags = IORESOURCE_IO;
		pci_claim_resource(dev, nr);
	}
}	

/*
 * Let's make the southbridge information explicit instead
 * of having to worry about people probing the ACPI areas,
 * for example.. (Yes, it happens, and if you read the wrong
 * ACPI register it will put the machine to sleep with no
 * way of waking it up again. Bummer).
 *
 * ALI M7101: Two IO regions pointed to by words at
 *	0xE0 (64 bytes of ACPI registers)
 *	0xE2 (32 bytes of SMB registers)
 */
static void __init quirk_ali7101_acpi(struct pci_dev *dev)
{
	u16 region;

	pci_read_config_word(dev, 0xE0, &region);
	quirk_io_region(dev, region, 64, PCI_BRIDGE_RESOURCES);
	pci_read_config_word(dev, 0xE2, &region);
	quirk_io_region(dev, region, 32, PCI_BRIDGE_RESOURCES+1);
}

/*
 * PIIX4 ACPI: Two IO regions pointed to by longwords at
 *	0x40 (64 bytes of ACPI registers)
 *	0x90 (32 bytes of SMB registers)
 */
static void __init quirk_piix4_acpi(struct pci_dev *dev)
{
	u32 region;

	pci_read_config_dword(dev, 0x40, &region);
	quirk_io_region(dev, region, 64, PCI_BRIDGE_RESOURCES);
	pci_read_config_dword(dev, 0x90, &region);
	quirk_io_region(dev, region, 32, PCI_BRIDGE_RESOURCES+1);
}

/*
 * VIA ACPI: One IO region pointed to by longword at
 *	0x48 or 0x20 (256 bytes of ACPI registers)
 */
static void __init quirk_vt82c586_acpi(struct pci_dev *dev)
{
	u8 rev;
	u32 region;

	pci_read_config_byte(dev, PCI_CLASS_REVISION, &rev);
	if (rev & 0x10) {
		pci_read_config_dword(dev, 0x48, &region);
		region &= PCI_BASE_ADDRESS_IO_MASK;
		quirk_io_region(dev, region, 256, PCI_BRIDGE_RESOURCES);
	}
}

/*
 * VIA VT82C686 ACPI: Three IO region pointed to by (long)words at
 *	0x48 (256 bytes of ACPI registers)
 *	0x70 (128 bytes of hardware monitoring register)
 *	0x90 (16 bytes of SMB registers)
 */
static void __init quirk_vt82c686_acpi(struct pci_dev *dev)
{
	u16 hm;
	u32 smb;

	quirk_vt82c586_acpi(dev);

	pci_read_config_word(dev, 0x70, &hm);
	hm &= PCI_BASE_ADDRESS_IO_MASK;
	quirk_io_region(dev, hm, 128, PCI_BRIDGE_RESOURCES + 1);

	pci_read_config_dword(dev, 0x90, &smb);
	smb &= PCI_BASE_ADDRESS_IO_MASK;
	quirk_io_region(dev, smb, 16, PCI_BRIDGE_RESOURCES + 2);
}

/*
 * PIIX3 USB: We have to disable USB interrupts that are
 * hardwired to PIRQD# and may be shared with an
 * external device.
 *
 * Legacy Support Register (LEGSUP):
 *     bit13:  USB PIRQ Enable (USBPIRQDEN),
 *     bit4:   Trap/SMI On IRQ Enable (USBSMIEN).
 *
 * We mask out all r/wc bits, too.
 */
static void __init quirk_piix3_usb(struct pci_dev *dev)
{
	u16 legsup;

	pci_read_config_word(dev, 0xc0, &legsup);
	legsup &= 0x50ef;
	pci_write_config_word(dev, 0xc0, legsup);
}

/*
 * VIA VT82C598 has its device ID settable and many BIOSes
 * set it to the ID of VT82C597 for backward compatibility.
 * We need to switch it off to be able to recognize the real
 * type of the chip.
 */
static void __init quirk_vt82c598_id(struct pci_dev *dev)
{
	pci_write_config_byte(dev, 0xfc, 0);
	pci_read_config_word(dev, PCI_DEVICE_ID, &dev->device);
}

/*
 *  The main table of quirks.
 */

static struct pci_fixup pci_fixups[] __initdata = {
	{ PCI_FIXUP_FINAL,	PCI_VENDOR_ID_INTEL,	PCI_DEVICE_ID_INTEL_82441,	quirk_passive_release },
	{ PCI_FIXUP_FINAL,	PCI_VENDOR_ID_INTEL,	PCI_DEVICE_ID_INTEL_82441,	quirk_passive_release },
	/*
	 * Its not totally clear which chipsets are the problematic ones
	 * We know 82C586 and 82C596 variants are affected.
	 */
	{ PCI_FIXUP_FINAL,	PCI_VENDOR_ID_VIA,	PCI_DEVICE_ID_VIA_82C586_0,	quirk_isa_dma_hangs },
	{ PCI_FIXUP_FINAL,	PCI_VENDOR_ID_VIA,	PCI_DEVICE_ID_VIA_82C596,	quirk_isa_dma_hangs },
	{ PCI_FIXUP_FINAL,      PCI_VENDOR_ID_INTEL,    PCI_DEVICE_ID_INTEL_82371SB_0,  quirk_isa_dma_hangs },
	{ PCI_FIXUP_HEADER,	PCI_VENDOR_ID_S3,	PCI_DEVICE_ID_S3_868,		quirk_s3_64M },
	{ PCI_FIXUP_HEADER,	PCI_VENDOR_ID_S3,	PCI_DEVICE_ID_S3_968,		quirk_s3_64M },
	{ PCI_FIXUP_FINAL,	PCI_VENDOR_ID_INTEL, 	PCI_DEVICE_ID_INTEL_82437, 	quirk_triton }, 
	{ PCI_FIXUP_FINAL,	PCI_VENDOR_ID_INTEL, 	PCI_DEVICE_ID_INTEL_82437VX, 	quirk_triton }, 
	{ PCI_FIXUP_FINAL,	PCI_VENDOR_ID_INTEL, 	PCI_DEVICE_ID_INTEL_82439, 	quirk_triton }, 
	{ PCI_FIXUP_FINAL,	PCI_VENDOR_ID_INTEL, 	PCI_DEVICE_ID_INTEL_82439TX, 	quirk_triton }, 
	{ PCI_FIXUP_FINAL,	PCI_VENDOR_ID_INTEL, 	PCI_DEVICE_ID_INTEL_82441, 	quirk_natoma }, 
	{ PCI_FIXUP_FINAL,	PCI_VENDOR_ID_INTEL, 	PCI_DEVICE_ID_INTEL_82443LX_0, 	quirk_natoma }, 
	{ PCI_FIXUP_FINAL,	PCI_VENDOR_ID_INTEL, 	PCI_DEVICE_ID_INTEL_82443LX_1, 	quirk_natoma }, 
	{ PCI_FIXUP_FINAL,	PCI_VENDOR_ID_INTEL, 	PCI_DEVICE_ID_INTEL_82443BX_0, 	quirk_natoma }, 
	{ PCI_FIXUP_FINAL,	PCI_VENDOR_ID_INTEL, 	PCI_DEVICE_ID_INTEL_82443BX_1, 	quirk_natoma }, 
	{ PCI_FIXUP_FINAL,	PCI_VENDOR_ID_INTEL, 	PCI_DEVICE_ID_INTEL_82443BX_2, 	quirk_natoma },
	{ PCI_FIXUP_FINAL,	PCI_VENDOR_ID_SI,	PCI_DEVICE_ID_SI_5597,		quirk_nopcipci },
	{ PCI_FIXUP_FINAL,	PCI_VENDOR_ID_SI,	PCI_DEVICE_ID_SI_496,		quirk_nopcipci },
	{ PCI_FIXUP_FINAL,	PCI_VENDOR_ID_VIA,	PCI_DEVICE_ID_VIA_82C597_0,	quirk_viaetbf },
	{ PCI_FIXUP_HEADER,	PCI_VENDOR_ID_VIA,	PCI_DEVICE_ID_VIA_82C597_0,	quirk_vt82c598_id },
	{ PCI_FIXUP_HEADER,	PCI_VENDOR_ID_VIA,	PCI_DEVICE_ID_VIA_82C586_3,	quirk_vt82c586_acpi },
	{ PCI_FIXUP_HEADER,	PCI_VENDOR_ID_VIA,	PCI_DEVICE_ID_VIA_82C686_4,	quirk_vt82c686_acpi },
	{ PCI_FIXUP_HEADER,	PCI_VENDOR_ID_INTEL,	PCI_DEVICE_ID_INTEL_82371AB_3,	quirk_piix4_acpi },
	{ PCI_FIXUP_HEADER,	PCI_VENDOR_ID_AL,	PCI_DEVICE_ID_AL_M7101,		quirk_ali7101_acpi },
 	{ PCI_FIXUP_HEADER,	PCI_VENDOR_ID_INTEL,	PCI_DEVICE_ID_INTEL_82371SB_2,	quirk_piix3_usb },
	{ PCI_FIXUP_HEADER,	PCI_VENDOR_ID_INTEL,	PCI_DEVICE_ID_INTEL_82371AB_2,	quirk_piix3_usb },
	{ 0 }
};


static void pci_do_fixups(struct pci_dev *dev, int pass, struct pci_fixup *f)
{
	while (f->pass) {
		if (f->pass == pass &&
 		    (f->vendor == dev->vendor || f->vendor == (u16) PCI_ANY_ID) &&
 		    (f->device == dev->device || f->device == (u16) PCI_ANY_ID)) {
#ifdef DEBUG
			printk("PCI: Calling quirk %p for %s\n", f->hook, dev->slot_name);
#endif
			f->hook(dev);
		}
		f++;
	}
}

void pci_fixup_device(int pass, struct pci_dev *dev)
{
	pci_do_fixups(dev, pass, pcibios_fixups);
	pci_do_fixups(dev, pass, pci_fixups);
}
