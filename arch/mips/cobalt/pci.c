/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Cobalt Qube specific PCI support.
 */
#include <linux/config.h>
#include <linux/types.h>
#include <linux/bios32.h>
#include <linux/pci.h>
#include <linux/kernel.h>
#include <asm/cobalt.h>
#include <asm/pci.h>
#include <asm/io.h>
 
#undef PCI_DEBUG 

#ifdef CONFIG_PCI

static void qube_expansion_slot_bist(void)
{
	unsigned char ctrl;
	int timeout = 100000;

	pcibios_read_config_byte(0, (0x0a<<3), PCI_BIST, &ctrl);
	if(!(ctrl & PCI_BIST_CAPABLE))
		return;

	pcibios_write_config_byte(0, (0x0a<<3), PCI_BIST, ctrl|PCI_BIST_START);
	do {
		pcibios_read_config_byte(0, (0x0a<<3), PCI_BIST, &ctrl);
		if(!(ctrl & PCI_BIST_START))
			break;
	} while(--timeout > 0);
	if((timeout <= 0) || (ctrl & PCI_BIST_CODE_MASK))
		printk("PCI: Expansion slot card failed BIST with code %x\n",
		       (ctrl & PCI_BIST_CODE_MASK));
}

static void qube_expansion_slot_fixup(void)
{
	unsigned short pci_cmd;
	unsigned long ioaddr_base = 0x10108000; /* It's magic, ask Doug. */
	unsigned long memaddr_base = 0x12000000;
	int i;

	/* Enable bits in COMMAND so driver can talk to it. */
	pcibios_read_config_word(0, (0x0a<<3), PCI_COMMAND, &pci_cmd);
	pci_cmd |= (PCI_COMMAND_IO | PCI_COMMAND_MEMORY | PCI_COMMAND_MASTER);
	pcibios_write_config_word(0, (0x0a<<3), PCI_COMMAND, pci_cmd);

	/* Give it a working IRQ. */
	pcibios_write_config_byte(0, (0x0a<<3), PCI_INTERRUPT_LINE, 9);

	/* Fixup base addresses, we only support I/O at the moment. */
	for(i = 0; i <= 5; i++) {
		unsigned int regaddr = (PCI_BASE_ADDRESS_0 + (i * 4));
		unsigned int rval, mask, size, alignme, aspace;
		unsigned long *basep = &ioaddr_base;

		/* Check type first, punt if non-IO. */
		pcibios_read_config_dword(0, (0x0a<<3), regaddr, &rval);
		aspace = (rval & PCI_BASE_ADDRESS_SPACE);
		if(aspace != PCI_BASE_ADDRESS_SPACE_IO)
			basep = &memaddr_base;

		/* Figure out how much it wants, if anything. */
		pcibios_write_config_dword(0, (0x0a<<3), regaddr, 0xffffffff);
		pcibios_read_config_dword(0, (0x0a<<3), regaddr, &rval);

		/* Unused? */
		if(rval == 0)
			continue;

		rval &= PCI_BASE_ADDRESS_IO_MASK;
		mask = (~rval << 1) | 0x1;
		size = (mask & rval) & 0xffffffff;
		alignme = size;
		if(alignme < 0x400)
			alignme = 0x400;
		rval = ((*basep + (alignme - 1)) & ~(alignme - 1));
		*basep = (rval + size);
		pcibios_write_config_dword(0, (0x0a<<3), regaddr, rval | aspace);
	}
	qube_expansion_slot_bist();
}

static void qube_raq_tulip_fixup(void)
{
	unsigned short pci_cmd;

	/* Enable the device. */
	pcibios_read_config_word(0, (0x0c<<3), PCI_COMMAND, &pci_cmd);
	pci_cmd |= (PCI_COMMAND_IO | PCI_COMMAND_MASTER);
	pcibios_write_config_word(0, (0x0c<<3), PCI_COMMAND, pci_cmd);

	/* Give it it's IRQ. */
	/* NOTE: RaQ board #1 has a bunch of green wires which swapped the
	 *       IRQ line values of Tulip 0 and Tulip 1.  All other
	 *       boards have eth0=4,eth1=13.  -DaveM
	 */
#ifndef RAQ_BOARD_1_WITH_HWHACKS
	pcibios_write_config_byte(0, (0x0c<<3), PCI_INTERRUPT_LINE, 13);
#else
	pcibios_write_config_byte(0, (0x0c<<3), PCI_INTERRUPT_LINE, 4);
#endif

	/* And finally, a usable I/O space allocation, right after what
	 * the first Tulip uses.
	 */
	pcibios_write_config_dword(0, (0x0c<<3), PCI_BASE_ADDRESS_0, 0x10101001);
}

static void qube_raq_scsi_fixup(void)
{
	unsigned short pci_cmd;

	/* Enable the device. */
	pcibios_read_config_word(0, (0x08<<3), PCI_COMMAND, &pci_cmd);

	pci_cmd |= (PCI_COMMAND_IO | PCI_COMMAND_MASTER | PCI_COMMAND_MEMORY
		| PCI_COMMAND_INVALIDATE);
	pcibios_write_config_word(0, (0x08<<3), PCI_COMMAND, pci_cmd);

	/* Give it it's IRQ. */
	pcibios_write_config_byte(0, (0x08<<3), PCI_INTERRUPT_LINE, 4);

	/* And finally, a usable I/O space allocation, right after what
	 * the second Tulip uses.
	 */
	pcibios_write_config_dword(0, (0x08<<3), PCI_BASE_ADDRESS_0, 0x10102001);
	pcibios_write_config_dword(0, (0x08<<3), PCI_BASE_ADDRESS_1, 0x00002000);
	pcibios_write_config_dword(0, (0x08<<3), PCI_BASE_ADDRESS_2, 0x00100000);
}

static unsigned long
qube_pcibios_fixup(unsigned long mem_start, unsigned long mem_end)
{
	extern int cobalt_is_raq;
	int raq_p = cobalt_is_raq;
	unsigned int tmp;

	/* Fixup I/O and Memory space decoding on Galileo.  */
	isa_slot_offset = COBALT_LOCAL_IO_SPACE;

	/* Fix PCI latency-timer and cache-line-size values in Galileo
	 * host bridge.
	 */
	pcibios_write_config_byte(0, 0, PCI_LATENCY_TIMER, 64);
	pcibios_write_config_byte(0, 0, PCI_CACHE_LINE_SIZE, 7);

        /*
         * Now tell the SCSI device that we expect an interrupt at
         * IRQ 7 and not the default 0.
         */
        pcibios_write_config_byte(0, 0x08<<3, PCI_INTERRUPT_LINE,
                                  COBALT_SCSI_IRQ);

        /*
         * Now tell the Ethernet device that we expect an interrupt at
         * IRQ 13 and not the default 189.
	 *
	 * The IRQ of the first Tulip is different on Qube and RaQ
	 * hardware except for the weird first RaQ bringup board,
	 * see above for details.  -DaveM
         */
	if (! raq_p) {
		/* All Qube's route this the same way. */
		pcibios_write_config_byte(0, 0x07<<3, PCI_INTERRUPT_LINE,
					  COBALT_ETHERNET_IRQ);
	} else {
#ifndef RAQ_BOARD_1_WITH_HWHACKS
		pcibios_write_config_byte(0, (0x07<<3), PCI_INTERRUPT_LINE, 4);
#else
		pcibios_write_config_byte(0, (0x07<<3), PCI_INTERRUPT_LINE, 13);
#endif
	}

	if (! raq_p) {
		/* See if there is a device in the expansion slot, if so
		 * fixup IRQ, fix base addresses, and enable master +
		 * I/O + memory accesses in config space.
		 */
		pcibios_read_config_dword(0, 0x0a<<3, PCI_VENDOR_ID, &tmp);
		if(tmp != 0xffffffff && tmp != 0x00000000)
			qube_expansion_slot_fixup();
	} else {
		/* If this is a RAQ, we may need to setup the second Tulip
		 * and SCSI as well.  Due to the different configurations
		 * a RaQ can have, we need to explicitly check for the
		 * presence of each of these (optional) devices.  -DaveM
		 */
		pcibios_read_config_dword(0, 0x0c<<3, PCI_VENDOR_ID, &tmp);
		if(tmp != 0xffffffff && tmp != 0x00000000)
			qube_raq_tulip_fixup();

		pcibios_read_config_dword(0, 0x08<<3, PCI_VENDOR_ID, &tmp);
		if(tmp != 0xffffffff && tmp != 0x00000000)
			qube_raq_scsi_fixup();

		/* And if we are a 2800 we have to setup the expansion slot
		 * too.
		 */
		pcibios_read_config_dword(0, 0x0a<<3, PCI_VENDOR_ID, &tmp);
		if(tmp != 0xffffffff && tmp != 0x00000000)
			qube_expansion_slot_fixup();
	}

	return mem_start;
}

static __inline__ int pci_range_ck(unsigned char bus, unsigned char dev) 
{
	if ((bus == 0) && ( (dev==0) || ((dev>6) && (dev <= 12))) )
		return 0;  /* OK device number  */

	return -1;  /* NOT ok device number */
}

#define PCI_CFG_DATA	((volatile unsigned long *)0xb4000cfc)
#define PCI_CFG_CTRL	((volatile unsigned long *)0xb4000cf8)

#define PCI_CFG_SET(dev,fun,off) \
	((*PCI_CFG_CTRL) = (0x80000000 | ((dev)<<11) | \
			    ((fun)<<8) | (off)))

static int qube_pcibios_read_config_dword (unsigned char bus,
					   unsigned char dev,
					   unsigned char offset,
					   unsigned int *val)
{
	unsigned char fun = dev & 0x07;

	dev >>= 3;
	if (offset & 0x3)
		return PCIBIOS_BAD_REGISTER_NUMBER;
	if (pci_range_ck(bus, dev)) {
		*val = 0xFFFFFFFF;
		return PCIBIOS_DEVICE_NOT_FOUND;
	}
	PCI_CFG_SET(dev, fun, offset);
	*val = *PCI_CFG_DATA;
	return PCIBIOS_SUCCESSFUL;
}

static int qube_pcibios_read_config_word (unsigned char bus,
					  unsigned char dev,
					  unsigned char offset,
					  unsigned short *val)
{
	unsigned char fun = dev & 0x07;

	dev >>= 3;
	if (offset & 0x1)
		return PCIBIOS_BAD_REGISTER_NUMBER;
	if (pci_range_ck(bus, dev)) {
		*val = 0xffff;
		return PCIBIOS_DEVICE_NOT_FOUND;
	}
	PCI_CFG_SET(dev, fun, (offset & ~0x3));
	*val = *PCI_CFG_DATA >> ((offset & 3) * 8);
	return PCIBIOS_SUCCESSFUL;
}

static int qube_pcibios_read_config_byte (unsigned char bus,
					  unsigned char dev,
					  unsigned char offset,
					  unsigned char *val)
{
	unsigned char fun = dev & 0x07;

	dev >>= 3;
	if (pci_range_ck(bus, dev)) {
		*val = 0xff;
		return PCIBIOS_DEVICE_NOT_FOUND;
	}
	PCI_CFG_SET(dev, fun, (offset & ~0x3));
	*val = *PCI_CFG_DATA >> ((offset & 3) * 8);
	return PCIBIOS_SUCCESSFUL;
}

static int qube_pcibios_write_config_dword (unsigned char bus,
					    unsigned char dev,
					    unsigned char offset,
					    unsigned int val)
{
	unsigned char fun = dev & 0x07;

	dev >>= 3;
	if(offset & 0x3)
		return PCIBIOS_BAD_REGISTER_NUMBER;
	if (pci_range_ck(bus, dev))
		return PCIBIOS_DEVICE_NOT_FOUND;
	PCI_CFG_SET(dev, fun, offset);
	*PCI_CFG_DATA = val;
	return PCIBIOS_SUCCESSFUL;
}

static int
qube_pcibios_write_config_word (unsigned char bus, unsigned char dev,
                                unsigned char offset, unsigned short val)
{
	unsigned char fun = dev & 0x07;
	unsigned long tmp;

	dev >>= 3;
	if (offset & 0x1)
		return PCIBIOS_BAD_REGISTER_NUMBER;
	if (pci_range_ck(bus, dev))
		return PCIBIOS_DEVICE_NOT_FOUND;
	PCI_CFG_SET(dev, fun, (offset & ~0x3));
	tmp = *PCI_CFG_DATA;
	tmp &= ~(0xffff << ((offset & 0x3) * 8));
	tmp |=  (val << ((offset & 0x3) * 8));
	*PCI_CFG_DATA = tmp;
	return PCIBIOS_SUCCESSFUL;
}

static int
qube_pcibios_write_config_byte (unsigned char bus, unsigned char dev,
                                unsigned char offset, unsigned char val)
{
	unsigned char fun = dev & 0x07;
	unsigned long tmp;

	dev >>= 3;
	if (pci_range_ck(bus, dev))
		return PCIBIOS_DEVICE_NOT_FOUND;
	PCI_CFG_SET(dev, fun, (offset & ~0x3));
	tmp = *PCI_CFG_DATA;
	tmp &= ~(0xff << ((offset & 0x3) * 8));
	tmp |=  (val << ((offset & 0x3) * 8));
	*PCI_CFG_DATA = tmp;
	return PCIBIOS_SUCCESSFUL;
}

struct pci_ops qube_pci_ops = {
	qube_pcibios_fixup,
	qube_pcibios_read_config_byte,
	qube_pcibios_read_config_word,
	qube_pcibios_read_config_dword,
	qube_pcibios_write_config_byte,
	qube_pcibios_write_config_word,
	qube_pcibios_write_config_dword
};

#endif /* CONFIG_PCI */
