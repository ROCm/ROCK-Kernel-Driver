/*
 * Copyright 2002 Momentum Computer
 * Author: Matthew Dharm <mdharm@momenco.com>
 *
 * Copyright (C) 2003, 2004 Ralf Baechle (ralf@linux-mips.org)
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
#include <asm/mv64340.h>

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

static int mv64340_read_config(struct pci_bus *bus, unsigned int devfn, int reg,
	int size, u32 * val, u32 address_reg, u32 data_reg)
{
	u32 address;

	/* Accessing device 31 crashes the MV-64340. */
	if (PCI_SLOT(devfn) > 5)
		return PCIBIOS_DEVICE_NOT_FOUND;

	address = (bus->number << 16) | (devfn << 8) |
		  (reg & 0xfc) | 0x80000000;

	/* start the configuration cycle */
	MV_WRITE(address_reg, address);

	switch (size) {
	case 1:
		*val = MV_READ_8(data_reg + (reg & 0x3));
		break;

	case 2:
		*val = MV_READ_16(data_reg + (reg & 0x3));
		break;

	case 4:
		*val = MV_READ(data_reg);
		break;
	}

	return PCIBIOS_SUCCESSFUL;
}

static int mv64340_write_config(struct pci_bus *bus, unsigned int devfn,
	int reg, int size, u32 val, u32 address_reg, u32 data_reg)
{
	u32 address;

	/* Accessing device 31 crashes the MV-64340. */
	if (PCI_SLOT(devfn) > 5)
		return PCIBIOS_DEVICE_NOT_FOUND;

	address = (bus->number << 16) | (devfn << 8) |
		  (reg & 0xfc) | 0x80000000;

	/* start the configuration cycle */
	MV_WRITE(address_reg, address);

	switch (size) {
	case 1:
		/* write the data */
		MV_WRITE_8(data_reg + (reg & 0x3), val);
		break;

	case 2:
		/* write the data */
		MV_WRITE_16(data_reg + (reg & 0x3), val);
		break;

	case 4:
		/* write the data */
		MV_WRITE(data_reg, val);
		break;
	}

	return PCIBIOS_SUCCESSFUL;
}

#define BUILD_PCI_OPS(host)						\
									\
static int mv64340_bus ## host ## _read_config(struct pci_bus *bus,	\
	unsigned int devfn, int reg, int size, u32 * val)		\
{									\
	return mv64340_read_config(bus, devfn, reg, size, val,		\
	           MV64340_PCI_ ## host ## _CONFIG_ADDR,		\
	           MV64340_PCI_ ## host ## _CONFIG_DATA_VIRTUAL_REG);	\
}									\
									\
static int mv64340_bus ## host ## _write_config(struct pci_bus *bus,	\
	unsigned int devfn, int reg, int size, u32 val)			\
{									\
	return mv64340_write_config(bus, devfn, reg, size, val,		\
	           MV64340_PCI_ ## host ## _CONFIG_ADDR,		\
	           MV64340_PCI_ ## host ## _CONFIG_DATA_VIRTUAL_REG);	\
}									\
									\
struct pci_ops mv64340_bus ## host ## _pci_ops = {			\
	.read	= mv64340_bus ## host ## _read_config,			\
	.write	= mv64340_bus ## host ## _write_config			\
};

BUILD_PCI_OPS(0)
BUILD_PCI_OPS(1)
