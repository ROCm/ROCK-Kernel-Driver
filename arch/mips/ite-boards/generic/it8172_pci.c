/*
 * BRIEF MODULE DESCRIPTION
 *	IT8172 system controller specific pci support.
 *
 * Copyright 2000 MontaVista Software Inc.
 * Author: MontaVista Software, Inc.
 *         	ppopov@mvista.com or source@mvista.com
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

#ifdef CONFIG_PCI

#include <linux/types.h>
#include <linux/pci.h>
#include <linux/kernel.h>
#include <linux/init.h>

#include <asm/it8172/it8172.h>
#include <asm/it8172/it8172_pci.h>

#define PCI_ACCESS_READ  0
#define PCI_ACCESS_WRITE 1

#undef DEBUG
#undef DEBUG_CONFIG_CYCLES

static int
it8172_pcibios_config_access(unsigned char access_type, struct pci_dev *dev,
                           unsigned char where, u32 *data)
{
	/* 
	 * config cycles are on 4 byte boundary only
	 */
	unsigned char bus = dev->bus->number;
	unsigned char dev_fn = dev->devfn;

#ifdef DEBUG_CONFIG_CYCLES
	printk("it config: type %d dev %x bus %d dev_fn %x data %x\n",
			access_type, dev, bus, dev_fn, *data);

#endif

	/* Setup address */
	IT_WRITE(IT_CONFADDR, (bus << IT_BUSNUM_SHF) | 
			(dev_fn << IT_FUNCNUM_SHF) | (where & ~0x3));


	if (access_type == PCI_ACCESS_WRITE) {
		IT_WRITE(IT_CONFDATA, *data);
	} 
	else {
		IT_READ(IT_CONFDATA, *data);
	}

	/*
	 * Revisit: check for master or target abort.
	 */
	return 0;


}


/*
 * We can't address 8 and 16 bit words directly.  Instead we have to
 * read/write a 32bit word and mask/modify the data we actually want.
 */
static int
it8172_pcibios_read_config_byte (struct pci_dev *dev, int where, u8 *val)
{
	u32 data = 0;

	if (it8172_pcibios_config_access(PCI_ACCESS_READ, dev, where, &data))
		return -1;

	*val = (data >> ((where & 3) << 3)) & 0xff;
#ifdef DEBUG
        printk("cfg read byte: bus %d dev_fn %x where %x: val %x\n", 
                dev->bus->number, dev->devfn, where, *val);
#endif

	return PCIBIOS_SUCCESSFUL;
}


static int
it8172_pcibios_read_config_word (struct pci_dev *dev, int where, u16 *val)
{
	u32 data = 0;

	if (where & 1)
		return PCIBIOS_BAD_REGISTER_NUMBER;

	if (it8172_pcibios_config_access(PCI_ACCESS_READ, dev, where, &data))
	       return -1;

	*val = (data >> ((where & 3) << 3)) & 0xffff;
#ifdef DEBUG
        printk("cfg read word: bus %d dev_fn %x where %x: val %x\n", 
                dev->bus->number, dev->devfn, where, *val);
#endif

	return PCIBIOS_SUCCESSFUL;
}

static int
it8172_pcibios_read_config_dword (struct pci_dev *dev, int where, u32 *val)
{
	u32 data = 0;

	if (where & 3)
		return PCIBIOS_BAD_REGISTER_NUMBER;
	
	if (it8172_pcibios_config_access(PCI_ACCESS_READ, dev, where, &data))
		return -1;

	*val = data;
#ifdef DEBUG
        printk("cfg read dword: bus %d dev_fn %x where %x: val %x\n", 
                dev->bus->number, dev->devfn, where, *val);
#endif

	return PCIBIOS_SUCCESSFUL;
}


static int
it8172_pcibios_write_config_byte (struct pci_dev *dev, int where, u8 val)
{
	u32 data = 0;
       
	if (it8172_pcibios_config_access(PCI_ACCESS_READ, dev, where, &data))
		return -1;

	data = (data & ~(0xff << ((where & 3) << 3))) |
	       (val << ((where & 3) << 3));

	if (it8172_pcibios_config_access(PCI_ACCESS_WRITE, dev, where, &data))
		return -1;

	return PCIBIOS_SUCCESSFUL;
}

static int
it8172_pcibios_write_config_word (struct pci_dev *dev, int where, u16 val)
{
        u32 data = 0;

	if (where & 1)
		return PCIBIOS_BAD_REGISTER_NUMBER;
       
        if (it8172_pcibios_config_access(PCI_ACCESS_READ, dev, where, &data))
	       return -1;

	data = (data & ~(0xffff << ((where & 3) << 3))) | 
	       (val << ((where & 3) << 3));

	if (it8172_pcibios_config_access(PCI_ACCESS_WRITE, dev, where, &data))
	       return -1;


	return PCIBIOS_SUCCESSFUL;
}

static int
it8172_pcibios_write_config_dword(struct pci_dev *dev, int where, u32 val)
{
	if (where & 3)
		return PCIBIOS_BAD_REGISTER_NUMBER;

	if (it8172_pcibios_config_access(PCI_ACCESS_WRITE, dev, where, &val))
	       return -1;

	return PCIBIOS_SUCCESSFUL;
}

struct pci_ops it8172_pci_ops = {
	it8172_pcibios_read_config_byte,
        it8172_pcibios_read_config_word,
	it8172_pcibios_read_config_dword,
	it8172_pcibios_write_config_byte,
	it8172_pcibios_write_config_word,
	it8172_pcibios_write_config_dword
};

void __init pcibios_init(void)
{

	printk("PCI: Probing PCI hardware on host bus 0.\n");
	pci_scan_bus(0, &it8172_pci_ops, NULL);
}

int __init
pcibios_enable_device(struct pci_dev *dev)
{
	u16 cmd, old_cmd;
	int idx;
	struct resource *r;

	pci_read_config_word(dev, PCI_COMMAND, &cmd);
	old_cmd = cmd;
	for(idx=0; idx<6; idx++) {
		r = &dev->resource[idx];
		if (!r->start && r->end) {
			printk(KERN_ERR "PCI: Device %s not available because of resource collisions\n", dev->slot_name);
			return -EINVAL;
		}
		if (r->flags & IORESOURCE_IO)
			cmd |= PCI_COMMAND_IO;
		if (r->flags & IORESOURCE_MEM)
			cmd |= PCI_COMMAND_MEMORY;
	}
	if (dev->resource[PCI_ROM_RESOURCE].start)
		cmd |= PCI_COMMAND_MEMORY;
	if (cmd != old_cmd) {
		printk("PCI: Enabling device %s (%04x -> %04x)\n", dev->slot_name, old_cmd, cmd);
		pci_write_config_word(dev, PCI_COMMAND, cmd);
	}
	return 0;
}


void __init
pcibios_align_resource(void *data, struct resource *res, unsigned long size)
{
    printk("pcibios_align_resource\n");
}

char * __init
pcibios_setup(char *str)
{
	/* Nothing to do for now.  */

	return str;
}

void __init
pcibios_update_resource(struct pci_dev *dev, struct resource *root,
                        struct resource *res, int resource)
{
	unsigned long where, size;
	u32 reg;

	where = PCI_BASE_ADDRESS_0 + (resource * 4);
	size = res->end - res->start;
	pci_read_config_dword(dev, where, &reg);
	reg = (reg & size) | (((u32)(res->start - root->start)) & ~size);
	pci_write_config_dword(dev, where, reg);
}

void __init pcibios_fixup_bus(struct pci_bus *b)
{
	//printk("pcibios_fixup_bus\n");
}

unsigned __init int pcibios_assign_all_busses(void)
{
	return 1;
}

#endif /* CONFIG_PCI */
