/*
 * Copyright 2002 Momentum Computer
 * Author: Matthew Dharm <mdharm@momenco.com>
 *
 *  This program is free software; you can redistribute  it and/or modify it
 *  under  the terms of  the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the  License, or (at your
 *  option) any later version.
 *
 *  THIS  SOFTWARE  IS PROVIDED   ``AS  IS'' AND   ANY  EXPRESS OR IMPLIED
 *  WARRANTIES,   INCLUDING, BUT NOT  LIMITED  TO, THE IMPLIED WARRANTIES OF
 *  MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN
 *  NO  EVENT  SHALL   THE AUTHOR  BE    LIABLE FOR ANY   DIRECT, INDIRECT,
 *  INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 *  NOT LIMITED   TO, PROCUREMENT OF  SUBSTITUTE GOODS  OR SERVICES; LOSS OF
 *  USE, DATA,  OR PROFITS; OR  BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
 *  ANY THEORY OF LIABILITY, WHETHER IN  CONTRACT, STRICT LIABILITY, OR TORT
 *  (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 *  THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 *  You should have received a copy of the  GNU General Public License along
 *  with this program; if not, write  to the Free Software Foundation, Inc.,
 *  675 Mass Ave, Cambridge, MA 02139, USA.
 */
#include <linux/types.h>
#include <linux/pci.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/version.h>
#include <asm/pci.h>
#include <asm/io.h>
#include "gt64240.h"

#include <linux/init.h>

#define SELF 0
#define MASTER_ABORT_BIT 0x100

/*
 * These functions and structures provide the BIOS scan and mapping of the PCI
 * devices.
 */

#define MAX_PCI_DEVS 10

void gt64240_board_pcibios_fixup_bus(struct pci_bus *c);

/*  Functions to implement "pci ops"  */
static int galileo_pcibios_read_config_word(int bus, int devfn,
					    int offset, u16 * val);
static int galileo_pcibios_read_config_byte(int bus, int devfn,
					    int offset, u8 * val);
static int galileo_pcibios_read_config_dword(int bus, int devfn,
					     int offset, u32 * val);
static int galileo_pcibios_write_config_byte(int bus, int devfn,
					     int offset, u8 val);
static int galileo_pcibios_write_config_word(int bus, int devfn,
					     int offset, u16 val);
static int galileo_pcibios_write_config_dword(int bus, int devfn,
					      int offset, u32 val);
#if 0
static void galileo_pcibios_set_master(struct pci_dev *dev);
#endif

static int pci_read(struct pci_bus *bus, unsigned int devfs, int where,
		    int size, u32 * val);
static int pci_write(struct pci_bus *bus, unsigned int devfs, int where,
		     int size, u32 val);

/*
 *  General-purpose PCI functions.
 */


/*
 * pci_range_ck -
 *
 * Check if the pci device that are trying to access does really exists
 * on the evaluation board.
 *
 * Inputs :
 * bus - bus number (0 for PCI 0 ; 1 for PCI 1)
 * dev - number of device on the specific pci bus
 *
 * Outpus :
 * 0 - if OK , 1 - if failure
 */
static __inline__ int pci_range_ck(unsigned char bus, unsigned char dev)
{
	/* Accessing device 31 crashes the GT-64240. */
	if (dev < 5)
		return 0;
	return -1;
}

/*
 * galileo_pcibios_(read/write)_config_(dword/word/byte) -
 *
 * reads/write a dword/word/byte register from the configuration space
 * of a device.
 *
 * Note that bus 0 and bus 1 are local, and we assume all other busses are
 * bridged from bus 1.  This is a safe assumption, since any other
 * configuration will require major modifications to the CP7000G
 *
 * Inputs :
 * bus - bus number
 * dev - device number
 * offset - register offset in the configuration space
 * val - value to be written / read
 *
 * Outputs :
 * PCIBIOS_SUCCESSFUL when operation was succesfull
 * PCIBIOS_DEVICE_NOT_FOUND when the bus or dev is errorneous
 * PCIBIOS_BAD_REGISTER_NUMBER when accessing non aligned
 */

static int galileo_pcibios_read_config_dword(int bus, int devfn,
					     int offset, u32 * val)
{
	int dev, func;
	uint32_t address_reg, data_reg;
	uint32_t address;

	dev = PCI_SLOT(devfn);
	func = PCI_FUNC(devfn);

	/* verify the range */
	if (pci_range_ck(bus, dev))
		return PCIBIOS_DEVICE_NOT_FOUND;

	/* select the GT-64240 registers to communicate with the PCI bus */
	if (bus == 0) {
		address_reg = PCI_0CONFIGURATION_ADDRESS;
		data_reg = PCI_0CONFIGURATION_DATA_VIRTUAL_REGISTER;
		GT_WRITE(PCI_0ERROR_CAUSE, ~MASTER_ABORT_BIT);
	} else {
		address_reg = PCI_1CONFIGURATION_ADDRESS;
		data_reg = PCI_1CONFIGURATION_DATA_VIRTUAL_REGISTER;
		GT_WRITE(PCI_1ERROR_CAUSE, ~MASTER_ABORT_BIT);
		if (bus == 1)
			bus = 0;
	}

	address = (bus << 16) | (dev << 11) | (func << 8) |
	    (offset & 0xfc) | 0x80000000;

	/* start the configuration cycle */
	GT_WRITE(address_reg, address);

	/* read the data */
	GT_READ(data_reg, val);

	return PCIBIOS_SUCCESSFUL;
}


static int galileo_pcibios_read_config_word(int bus, int devfn,
					    int offset, u16 * val)
{
	int dev, func;
	uint32_t address_reg, data_reg;
	uint32_t address;

	dev = PCI_SLOT(devfn);
	func = PCI_FUNC(devfn);

	/* verify the range */
	if (pci_range_ck(bus, dev))
		return PCIBIOS_DEVICE_NOT_FOUND;

	/* select the GT-64240 registers to communicate with the PCI bus */
	if (bus == 0) {
		address_reg = PCI_0CONFIGURATION_ADDRESS;
		data_reg = PCI_0CONFIGURATION_DATA_VIRTUAL_REGISTER;
		GT_WRITE(PCI_0ERROR_CAUSE, ~MASTER_ABORT_BIT);
	} else {
		address_reg = PCI_1CONFIGURATION_ADDRESS;
		data_reg = PCI_1CONFIGURATION_DATA_VIRTUAL_REGISTER;
		GT_WRITE(PCI_1ERROR_CAUSE, ~MASTER_ABORT_BIT);
		if (bus == 1)
			bus = 0;
	}

	address = (bus << 16) | (dev << 11) | (func << 8) |
	    (offset & 0xfc) | 0x80000000;

	/* start the configuration cycle */
	GT_WRITE(address_reg, address);

	/* read the data */
	GT_READ_16(data_reg + (offset & 0x3), val);

	return PCIBIOS_SUCCESSFUL;
}

static int galileo_pcibios_read_config_byte(int bus, int devfn,
					    int offset, u8 * val)
{
	int dev, func;
	uint32_t address_reg, data_reg;
	uint32_t address;

	dev = PCI_SLOT(devfn);
	func = PCI_FUNC(devfn);

	/* verify the range */
	if (pci_range_ck(bus, dev))
		return PCIBIOS_DEVICE_NOT_FOUND;

	/* select the GT-64240 registers to communicate with the PCI bus */
	if (bus == 0) {
		address_reg = PCI_0CONFIGURATION_ADDRESS;
		data_reg = PCI_0CONFIGURATION_DATA_VIRTUAL_REGISTER;
	} else {
		address_reg = PCI_1CONFIGURATION_ADDRESS;
		data_reg = PCI_1CONFIGURATION_DATA_VIRTUAL_REGISTER;
		if (bus == 1)
			bus = 0;
	}

	address = (bus << 16) | (dev << 11) | (func << 8) |
	    (offset & 0xfc) | 0x80000000;

	/* start the configuration cycle */
	GT_WRITE(address_reg, address);

	/* write the data */
	GT_READ_8(data_reg + (offset & 0x3), val);

	return PCIBIOS_SUCCESSFUL;
}

static int galileo_pcibios_write_config_dword(int bus, int devfn,
					      int offset, u32 val)
{
	int dev, func;
	uint32_t address_reg, data_reg;
	uint32_t address;

	dev = PCI_SLOT(devfn);
	func = PCI_FUNC(devfn);

	/* verify the range */
	if (pci_range_ck(bus, dev))
		return PCIBIOS_DEVICE_NOT_FOUND;

	/* select the GT-64240 registers to communicate with the PCI bus */
	if (bus == 0) {
		address_reg = PCI_0CONFIGURATION_ADDRESS;
		data_reg = PCI_0CONFIGURATION_DATA_VIRTUAL_REGISTER;
	} else {
		address_reg = PCI_1CONFIGURATION_ADDRESS;
		data_reg = PCI_1CONFIGURATION_DATA_VIRTUAL_REGISTER;
		if (bus == 1)
			bus = 0;
	}

	address = (bus << 16) | (dev << 11) | (func << 8) |
	    (offset & 0xfc) | 0x80000000;

	/* start the configuration cycle */
	GT_WRITE(address_reg, address);

	/* write the data */
	GT_WRITE(data_reg, val);

	return PCIBIOS_SUCCESSFUL;
}


static int galileo_pcibios_write_config_word(int bus, int devfn,
					     int offset, u16 val)
{
	int dev, func;
	uint32_t address_reg, data_reg;
	uint32_t address;

	dev = PCI_SLOT(devfn);
	func = PCI_FUNC(devfn);

	/* verify the range */
	if (pci_range_ck(bus, dev))
		return PCIBIOS_DEVICE_NOT_FOUND;

	/* select the GT-64240 registers to communicate with the PCI bus */
	if (bus == 0) {
		address_reg = PCI_0CONFIGURATION_ADDRESS;
		data_reg = PCI_0CONFIGURATION_DATA_VIRTUAL_REGISTER;
	} else {
		address_reg = PCI_1CONFIGURATION_ADDRESS;
		data_reg = PCI_1CONFIGURATION_DATA_VIRTUAL_REGISTER;
		if (bus == 1)
			bus = 0;
	}

	address = (bus << 16) | (dev << 11) | (func << 8) |
	    (offset & 0xfc) | 0x80000000;

	/* start the configuration cycle */
	GT_WRITE(address_reg, address);

	/* write the data */
	GT_WRITE_16(data_reg + (offset & 0x3), val);

	return PCIBIOS_SUCCESSFUL;
}

static int galileo_pcibios_write_config_byte(int bus, int devfn,
					     int offset, u8 val)
{
	int dev, func;
	uint32_t address_reg, data_reg;
	uint32_t address;

	dev = PCI_SLOT(devfn);
	func = PCI_FUNC(devfn);

	/* verify the range */
	if (pci_range_ck(bus, dev))
		return PCIBIOS_DEVICE_NOT_FOUND;

	/* select the GT-64240 registers to communicate with the PCI bus */
	if (bus == 0) {
		address_reg = PCI_0CONFIGURATION_ADDRESS;
		data_reg = PCI_0CONFIGURATION_DATA_VIRTUAL_REGISTER;
	} else {
		address_reg = PCI_1CONFIGURATION_ADDRESS;
		data_reg = PCI_1CONFIGURATION_DATA_VIRTUAL_REGISTER;
		if (bus == 1)
			bus = 0;
	}

	address = (bus << 16) | (dev << 11) | (func << 8) |
	    (offset & 0xfc) | 0x80000000;

	/* start the configuration cycle */
	GT_WRITE(address_reg, address);

	/* write the data */
	GT_WRITE_8(data_reg + (offset & 0x3), val);

	return PCIBIOS_SUCCESSFUL;
}

#if 0
static void galileo_pcibios_set_master(struct pci_dev *dev)
{
	u16 cmd;

	galileo_pcibios_read_config_word(dev, PCI_COMMAND, &cmd);
	cmd |= PCI_COMMAND_MASTER;
	galileo_pcibios_write_config_word(dev, PCI_COMMAND, cmd);
}
#endif

/*  Externally-expected functions.  Do not change function names  */

int pcibios_enable_resources(struct pci_dev *dev)
{
	u16 cmd, old_cmd;
	u8 tmp1;
	int idx;
	struct resource *r;

	pci_read(dev->bus, dev->devfn, PCI_COMMAND, 2, (u32 *) & cmd);
	old_cmd = cmd;
	for (idx = 0; idx < 6; idx++) {
		r = &dev->resource[idx];
		if (!r->start && r->end) {
			printk(KERN_ERR
			       "PCI: Device %s not available because of "
			       "resource collisions\n", pci_name(dev));
			return -EINVAL;
		}
		if (r->flags & IORESOURCE_IO)
			cmd |= PCI_COMMAND_IO;
		if (r->flags & IORESOURCE_MEM)
			cmd |= PCI_COMMAND_MEMORY;
	}
	if (cmd != old_cmd) {
		pci_write(dev->bus, dev->devfn, PCI_COMMAND, 2, cmd);
	}

	/*
	 * Let's fix up the latency timer and cache line size here.  Cache
	 * line size = 32 bytes / sizeof dword (4) = 8.
	 * Latency timer must be > 8.  32 is random but appears to work.
	 */
	pci_read(dev->bus, dev->devfn, PCI_CACHE_LINE_SIZE, 1,
		 (u32 *) & tmp1);
	if (tmp1 != 8) {
		printk(KERN_WARNING
		       "PCI setting cache line size to 8 from " "%d\n",
		       tmp1);
		pci_write(dev->bus, dev->devfn, PCI_CACHE_LINE_SIZE, 1, 8);
	}
	pci_read(dev->bus, dev->devfn, PCI_LATENCY_TIMER, 1,
		 (u32 *) & tmp1);
	if (tmp1 < 32) {
		printk(KERN_WARNING
		       "PCI setting latency timer to 32 from %d\n", tmp1);
		pci_write(dev->bus, dev->devfn, PCI_LATENCY_TIMER, 1, 32);
	}

	return 0;
}

int pcibios_enable_device(struct pci_dev *dev, int mask)
{
	return pcibios_enable_resources(dev);
}

void pcibios_align_resource(void *data, struct resource *res,
			    unsigned long size, unsigned long align)
{
	struct pci_dev *dev = data;

	if (res->flags & IORESOURCE_IO) {
		unsigned long start = res->start;

		/* We need to avoid collisions with `mirrored' VGA ports
		   and other strange ISA hardware, so we always want the
		   addresses kilobyte aligned.  */
		if (size > 0x100) {
			printk(KERN_ERR "PCI: I/O Region %s/%d too large"
			       " (%ld bytes)\n", pci_name(dev),
			       dev->resource - res, size);
		}

		start = (start + 1024 - 1) & ~(1024 - 1);
		res->start = start;
	}
}

struct pci_ops galileo_pci_ops = {
	.read = pci_read,
	.write = pci_write
};

static int pci_read(struct pci_bus *bus, unsigned int devfn, int where,
		    int size, u32 * val)
{
	switch (size) {
	case 1:
		return galileo_pcibios_read_config_byte(bus->number,
							devfn, where,
							(u8 *) val);
	case 2:
		return galileo_pcibios_read_config_word(bus->number,
							devfn, where,
							(u16 *) val);
	case 4:
		return galileo_pcibios_read_config_dword(bus->number,
							 devfn, where,
							 (u32 *) val);
	}
	return PCIBIOS_FUNC_NOT_SUPPORTED;
}

static int pci_write(struct pci_bus *bus, unsigned int devfn, int where,
		     int size, u32 val)
{
	switch (size) {
	case 1:
		return galileo_pcibios_write_config_byte(bus->number,
							 devfn, where,
							 val);
	case 2:
		return galileo_pcibios_write_config_word(bus->number,
							 devfn, where,
							 val);
	case 4:
		return galileo_pcibios_write_config_dword(bus->number,
							  devfn, where,
							  val);
	}
	return PCIBIOS_FUNC_NOT_SUPPORTED;
}

struct pci_fixup pcibios_fixups[] = {
	{0}
};

void __devinit pcibios_fixup_bus(struct pci_bus *c)
{
	gt64240_board_pcibios_fixup_bus(c);
}


/********************************************************************
* pci0P2PConfig - This function set the PCI_0 P2P configurate.
*                 For more information on the P2P read PCI spec.
*
* Inputs:  unsigned int SecondBusLow - Secondery PCI interface Bus Range Lower
*                                      Boundry.
*          unsigned int SecondBusHigh - Secondry PCI interface Bus Range upper
*                                      Boundry.
*          unsigned int busNum - The CPI bus number to which the PCI interface
*                                      is connected.
*          unsigned int devNum - The PCI interface's device number.
*
* Returns:  true.
*/
void pci0P2PConfig(unsigned int SecondBusLow, unsigned int SecondBusHigh,
		   unsigned int busNum, unsigned int devNum)
{
	uint32_t regData;

	regData = (SecondBusLow & 0xff) | ((SecondBusHigh & 0xff) << 8) |
	    ((busNum & 0xff) << 16) | ((devNum & 0x1f) << 24);
	GT_WRITE(PCI_0P2P_CONFIGURATION, regData);
}

/********************************************************************
* pci1P2PConfig - This function set the PCI_1 P2P configurate.
*                 For more information on the P2P read PCI spec.
*
* Inputs:  unsigned int SecondBusLow - Secondery PCI interface Bus Range Lower
*               Boundry.
*          unsigned int SecondBusHigh - Secondry PCI interface Bus Range upper
*               Boundry.
*          unsigned int busNum - The CPI bus number to which the PCI interface
*               is connected.
*          unsigned int devNum - The PCI interface's device number.
*
* Returns:  true.
*/
void pci1P2PConfig(unsigned int SecondBusLow, unsigned int SecondBusHigh,
		   unsigned int busNum, unsigned int devNum)
{
	uint32_t regData;

	regData = (SecondBusLow & 0xff) | ((SecondBusHigh & 0xff) << 8) |
	    ((busNum & 0xff) << 16) | ((devNum & 0x1f) << 24);
	GT_WRITE(PCI_1P2P_CONFIGURATION, regData);
}

#define PCI0_STATUS_COMMAND_REG                 0x4
#define PCI1_STATUS_COMMAND_REG                 0x84

static int __init pcibios_init(void)
{
	/* Reset PCI I/O and PCI MEM values */
	ioport_resource.start = 0xe0000000;
	ioport_resource.end = 0xe0000000 + 0x20000000 - 1;
	iomem_resource.start = 0xc0000000;
	iomem_resource.end = 0xc0000000 + 0x20000000 - 1;

	pci_scan_bus(0, &galileo_pci_ops, NULL);
	pci_scan_bus(1, &galileo_pci_ops, NULL);

	return 0;
}

subsys_initcall(pcibios_init);

/*
 * for parsing "pci=" kernel boot arguments.
 */
char *pcibios_setup(char *str)
{
	printk(KERN_INFO "rr: pcibios_setup\n");
	/* Nothing to do for now.  */

	return str;
}

unsigned __init int pcibios_assign_all_busses(void)
{
	return 1;
}
