/*
 * BRIEF MODULE DESCRIPTION
 *	Alchemy/AMD Au1x00 pci support.
 *
 * Copyright 2001,2002,2003 MontaVista Software Inc.
 * Author: MontaVista Software, Inc.
 *         	ppopov@mvista.com or source@mvista.com
 *
 *  Support for all devices (greater than 16) added by David Gathright.
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
#include <linux/config.h>
#include <linux/types.h>
#include <linux/pci.h>
#include <linux/kernel.h>
#include <linux/init.h>

#include <asm/mach-au1x00/au1000.h>

#define PCI_ACCESS_READ  0
#define PCI_ACCESS_WRITE 1


static int config_access(unsigned char access_type, struct pci_bus *bus,
			 unsigned int devfn, unsigned char where,
			 u32 * data)
{
	unsigned int device = PCI_SLOT(devfn);
	unsigned int function = PCI_FUNC(devfn);
	unsigned long config, status;
	unsigned long cfg_addr;

	if (device > 19) {
		*data = 0xffffffff;
		return -1;
	}

	au_writel(((0x2000 << 16) |
		   (au_readl(Au1500_PCI_STATCMD) & 0xffff)),
		  Au1500_PCI_STATCMD);
	//au_writel(au_readl(Au1500_PCI_CFG) & ~PCI_ERROR, Au1500_PCI_CFG);
	au_sync_udelay(1);

	/* setup the config window */
	if (bus->number == 0) {
		cfg_addr = (unsigned long) ioremap(Au1500_EXT_CFG |
						   ((1 << device) << 11),
						   0x00100000);
	} else {
		cfg_addr = (unsigned long) ioremap(Au1500_EXT_CFG_TYPE1 |
						   (bus->
						    number << 16) | (device
								     <<
								     11),
						   0x00100000);
	}

	if (!cfg_addr)
		panic(KERN_ERR "PCI unable to ioremap cfg space\n");

	/* setup the lower bits of the 36 bit address */
	config = cfg_addr | (function << 8) | (where & ~0x3);

#if 1
	if (access_type == PCI_ACCESS_WRITE) {
		printk("cfg write:  ");
	} else {
		printk("cfg read:  ");
	}
	printk("devfn %x, device %x func %x  \n", devfn, device, function);
	if (access_type == PCI_ACCESS_WRITE) {
		printk("data %x\n", *data);
	}
#endif

	if (access_type == PCI_ACCESS_WRITE) {
		au_writel(*data, config);
	} else {
		*data = au_readl(config);
	}
	au_sync_udelay(2);

	/* unmap io space */
	iounmap((void *) cfg_addr);

	/* check master abort */
	status = au_readl(Au1500_PCI_STATCMD);
	if (status & (1 << 29)) {
		printk("master abort\n");
		*data = 0xffffffff;
		return -1;
	} else if ((status >> 28) & 0xf) {
		printk("PCI ERR detected: status %x\n", status);
		*data = 0xffffffff;
		return -1;
	} else {
		printk("bios_successful: %x\n", *data);
		return PCIBIOS_SUCCESSFUL;
	}
}

static int read_config_byte(struct pci_bus *bus, unsigned int devfn,
			    int where, u8 * val)
{
	u32 data;
	int ret;

	ret = config_access(PCI_ACCESS_READ, bus, devfn, where, &data);
	if (where & 1)
		data >>= 8;
	if (where & 2)
		data >>= 16;
	*val = data & 0xff;
	return ret;
}


static int read_config_word(struct pci_bus *bus, unsigned int devfn,
			    int where, u16 * val)
{
	u32 data;
	int ret;

	ret = config_access(PCI_ACCESS_READ, bus, devfn, where, &data);
	if (where & 2)
		data >>= 16;
	*val = data & 0xffff;
	return ret;
}

static int read_config_dword(struct pci_bus *bus, unsigned int devfn,
			     int where, u32 * val)
{
	int ret;

	ret = config_access(PCI_ACCESS_READ, bus, devfn, where, val);
	return ret;
}

static int
write_config_byte(struct pci_bus *bus, unsigned int devfn, int where,
		  u8 val)
{
	u32 data = 0;

	if (config_access(PCI_ACCESS_READ, bus, devfn, where, &data))
		return -1;

	data = (data & ~(0xff << ((where & 3) << 3))) |
	    (val << ((where & 3) << 3));

	if (config_access(PCI_ACCESS_WRITE, bus, devfn, where, &data))
		return -1;

	return PCIBIOS_SUCCESSFUL;
}

static int
write_config_word(struct pci_bus *bus, unsigned int devfn, int where,
		  u16 val)
{
	u32 data = 0;

	if (config_access(PCI_ACCESS_READ, bus, devfn, where, &data))
		return -1;

	data = (data & ~(0xffff << ((where & 3) << 3))) |
	    (val << ((where & 3) << 3));

	if (config_access(PCI_ACCESS_WRITE, bus, devfn, where, &data))
		return -1;


	return PCIBIOS_SUCCESSFUL;
}

static int
write_config_dword(struct pci_bus *bus, unsigned int devfn, int where,
		   u32 val)
{
	if (config_access(PCI_ACCESS_WRITE, bus, devfn, where, &val))
		return -1;

	return PCIBIOS_SUCCESSFUL;
}

static int config_read(struct pci_bus *bus, unsigned int devfn,
		       int where, int size, u32 * val)
{
	switch (size) {
	case 1:
		return read_config_byte(bus, devfn, where, (u8 *) val);
	case 2:
		return read_config_word(bus, devfn, where, (u16 *) val);
	default:
		return read_config_dword(bus, devfn, where, val);
	}
}

static int config_write(struct pci_bus *bus, unsigned int devfn,
			int where, int size, u32 val)
{
	switch (size) {
	case 1:
		return write_config_byte(bus, devfn, where, (u8) val);
	case 2:
		return write_config_word(bus, devfn, where, (u16) val);
	default:
		return write_config_dword(bus, devfn, where, val);
	}
}


struct pci_ops au1x_pci_ops = {
	config_read,
	config_write
};
