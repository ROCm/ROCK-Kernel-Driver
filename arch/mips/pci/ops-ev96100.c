/*
 *
 * BRIEF MODULE DESCRIPTION
 *	Galileo EV96100 board specific pci support.
 *
 * Copyright 2000 MontaVista Software Inc.
 * Author: MontaVista Software, Inc.
 *         	ppopov@mvista.com or source@mvista.com
 *
 * This file was derived from Carsten Langgaard's
 * arch/mips/mips-boards/generic/pci.c
 *
 * Carsten Langgaard, carstenl@mips.com
 * Copyright (C) 1999,2000 MIPS Technologies, Inc.  All rights reserved.
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
#include <linux/init.h>

#include <asm/delay.h>
#include <asm			//gt64120.h>
#include <asm/galileo-boards/ev96100.h>
#include <asm/pci_channel.h>

#define PCI_ACCESS_READ  0
#define PCI_ACCESS_WRITE 1

#undef DEBUG

#ifdef DEBUG
#define DBG(x...) printk(x)
#else
#define DBG(x...)
#endif

#define GT_PCI_MEM_BASE    0x12000000
#define GT_PCI_MEM_SIZE    0x02000000
#define GT_PCI_IO_BASE     0x10000000
#define GT_PCI_IO_SIZE     0x02000000
static struct resource pci_io_resource = {
	"io pci IO space",
	0x10000000,
	0x10000000 + 0x02000000,
	IORESOURCE_IO
};

static struct resource pci_mem_resource = {
	"ext pci memory space",
	0x12000000,
	0x12000000 + 0x02000000,
	IORESOURCE_MEM
};

extern struct pci_ops gt96100_pci_ops;

struct pci_channel mips_pci_channels[] = {
	{&gt96100_pci_ops, &pci_io_resource, &pci_mem_resource, 1, 0xff},
	{NULL, NULL, NULL, NULL, NULL}
};

int
static gt96100_config_access(unsigned char access_type,
			     struct pci_dev *dev, unsigned char where,
			     u32 * data)
{
	unsigned char bus = dev->bus->number;
	unsigned char dev_fn = dev->devfn;
	u32 intr;


	if ((bus == 0) && (dev_fn >= PCI_DEVFN(31, 0))) {
		return -1;	/* Because of a bug in the galileo (for slot 31). */
	}

	/* Clear cause register bits */
	GT_WRITE(GT_INTRCAUSE_OFS, ~(GT_INTRCAUSE_MASABORT0_BIT |
				     GT_INTRCAUSE_TARABORT0_BIT));

	/* Setup address */
	GT_WRITE(GT_PCI0_CFGADDR_OFS,
		 (bus << GT_PCI0_CFGADDR_BUSNUM_SHF) |
		 (dev_fn << GT_PCI0_CFGADDR_FUNCTNUM_SHF) |
		 ((where / 4) << GT_PCI0_CFGADDR_REGNUM_SHF) |
		 GT_PCI0_CFGADDR_CONFIGEN_BIT);
	udelay(2);


	if (access_type == PCI_ACCESS_WRITE) {
		if (dev_fn != 0) {
			*data = le32_to_cpu(*data);
		}
		GT_WRITE(GT_PCI0_CFGDATA_OFS, *data);
	} else {
		GT_READ(GT_PCI0_CFGDATA_OFS, *data);
		if (dev_fn != 0) {
			*data = le32_to_cpu(*data);
		}
	}

	udelay(2);

	/* Check for master or target abort */
	GT_READ(GT_INTRCAUSE_OFS, intr);

	if (intr &
	    (GT_INTRCAUSE_MASABORT0_BIT | GT_INTRCAUSE_TARABORT0_BIT)) {
		//printk("config access error:  %x:%x\n", dev_fn,where);
		/* Error occured */

		/* Clear bits */
		GT_WRITE(GT_INTRCAUSE_OFS, ~(GT_INTRCAUSE_MASABORT0_BIT |
					     GT_INTRCAUSE_TARABORT0_BIT));

		if (access_type == PCI_ACCESS_READ) {
			*data = 0xffffffff;
		}
		return -1;
	}
	return 0;
}


/*
 * We can't address 8 and 16 bit words directly.  Instead we have to
 * read/write a 32bit word and mask/modify the data we actually want.
 */
static int read_config_byte(struct pci_dev *dev, int where, u8 * val)
{
	u32 data = 0;

	if (gt96100_config_access(PCI_ACCESS_READ, dev, where, &data)) {
		*val = 0xff;
		return -1;
	}

	*val = (data >> ((where & 3) << 3)) & 0xff;
	DBG("cfg read byte: bus %d dev_fn %x where %x: val %x\n",
	    dev->bus->number, dev->devfn, where, *val);

	return PCIBIOS_SUCCESSFUL;
}


static int read_config_word(struct pci_dev *dev, int where, u16 * val)
{
	u32 data = 0;

	if (where & 1)
		return PCIBIOS_BAD_REGISTER_NUMBER;

	if (gt96100_config_access(PCI_ACCESS_READ, dev, where, &data)) {
		*val = 0xffff;
		return -1;
	}

	*val = (data >> ((where & 3) << 3)) & 0xffff;
	DBG("cfg read word: bus %d dev_fn %x where %x: val %x\n",
	    dev->bus->number, dev->devfn, where, *val);

	return PCIBIOS_SUCCESSFUL;
}

static int read_config_dword(struct pci_dev *dev, int where, u32 * val)
{
	u32 data = 0;

	if (where & 3)
		return PCIBIOS_BAD_REGISTER_NUMBER;

	if (gt96100_config_access(PCI_ACCESS_READ, dev, where, &data)) {
		*val = 0xffffffff;
		return -1;
	}

	*val = data;
	DBG("cfg read dword: bus %d dev_fn %x where %x: val %x\n",
	    dev->bus->number, dev->devfn, where, *val);

	return PCIBIOS_SUCCESSFUL;
}


static int write_config_byte(struct pci_dev *dev, int where, u8 val)
{
	u32 data = 0;

	if (gt96100_config_access(PCI_ACCESS_READ, dev, where, &data))
		return -1;

	data = (data & ~(0xff << ((where & 3) << 3))) |
	    (val << ((where & 3) << 3));
	DBG("cfg write byte: bus %d dev_fn %x where %x: val %x\n",
	    dev->bus->number, dev->devfn, where, val);

	if (gt96100_config_access(PCI_ACCESS_WRITE, dev, where, &data))
		return -1;

	return PCIBIOS_SUCCESSFUL;
}

static int write_config_word(struct pci_dev *dev, int where, u16 val)
{
	u32 data = 0;

	if (where & 1)
		return PCIBIOS_BAD_REGISTER_NUMBER;

	if (gt96100_config_access(PCI_ACCESS_READ, dev, where, &data))
		return -1;

	data = (data & ~(0xffff << ((where & 3) << 3))) |
	    (val << ((where & 3) << 3));
	DBG("cfg write word: bus %d dev_fn %x where %x: val %x\n",
	    dev->bus->number, dev->devfn, where, val);

	if (gt96100_config_access(PCI_ACCESS_WRITE, dev, where, &data))
		return -1;


	return PCIBIOS_SUCCESSFUL;
}

static int write_config_dword(struct pci_dev *dev, int where, u32 val)
{
	if (where & 3)
		return PCIBIOS_BAD_REGISTER_NUMBER;

	if (gt96100_config_access(PCI_ACCESS_WRITE, dev, where, &val))
		return -1;
	DBG("cfg write dword: bus %d dev_fn %x where %x: val %x\n",
	    dev->bus->number, dev->devfn, where, val);

	return PCIBIOS_SUCCESSFUL;
}

struct pci_ops gt96100_pci_ops = {
	read_config_byte,
	read_config_word,
	read_config_dword,
	write_config_byte,
	write_config_word,
	write_config_dword
};
