#include <linux/config.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/ptrace.h>
#include <linux/interrupt.h>
#include <linux/mm.h>
#include <linux/init.h>
#include <linux/ioport.h>

#include <asm/io.h>
#include <asm/system.h>

#include <asm/mach/pci.h>

#define MAX_SLOTS		7

#define CONFIG_CMD(dev, where)   (0x80000000 | (dev->bus->number << 16) | (dev->devfn << 8) | (where & ~3))

static int
via82c505_read_config_byte(struct pci_dev *dev, int where, u8 *value)
{
	outl(CONFIG_CMD(dev,where),0xCF8);
	*value=inb(0xCFC + (where&3));
	return PCIBIOS_SUCCESSFUL;
}

static int
via82c505_read_config_word(struct pci_dev *dev, int where, u16 *value)
{
	outl(CONFIG_CMD(dev,where),0xCF8);
	*value=inw(0xCFC + (where&2));
	return PCIBIOS_SUCCESSFUL;
}

static int
via82c505_read_config_dword(struct pci_dev *dev, int where, u32 *value)
{
	outl(CONFIG_CMD(dev,where),0xCF8);
	*value=inl(0xCFC);
	return PCIBIOS_SUCCESSFUL;
}

static int
via82c505_write_config_byte(struct pci_dev *dev, int where, u8 value)
{
	outl(CONFIG_CMD(dev,where),0xCF8);
	outb(value, 0xCFC + (where&3));
	return PCIBIOS_SUCCESSFUL;
}

static int
via82c505_write_config_word(struct pci_dev *dev, int where, u16 value)
{
	outl(CONFIG_CMD(dev,where),0xCF8);
	outw(value, 0xCFC + (where&2));
	return PCIBIOS_SUCCESSFUL;
}

static int
via82c505_write_config_dword(struct pci_dev *dev, int where, u32 value)
{
	outl(CONFIG_CMD(dev,where),0xCF8);
	outl(value, 0xCFC);
	return PCIBIOS_SUCCESSFUL;
}

static struct pci_ops via82c505_ops = {
	via82c505_read_config_byte,
	via82c505_read_config_word,
	via82c505_read_config_dword,
	via82c505_write_config_byte,
	via82c505_write_config_word,
	via82c505_write_config_dword,
};

void __init via82c505_preinit(void *sysdata)
{
	struct pci_bus *bus;

	printk(KERN_DEBUG "PCI: VIA 82c505\n");
	request_region(0xA8,2,"via config");
	request_region(0xCF8,8,"pci config");

	/* Enable compatible Mode */
	outb(0x96,0xA8);
	outb(0x18,0xA9);
	outb(0x93,0xA8);
	outb(0xd0,0xA9);

}

int __init via82c505_setup(int nr, struct pci_sys_data *sys)
{
	return (nr == 0);
}

struct pci_bus * __init via82c505_scan_bus(int nr, struct pci_sys_data *sysdata)
{
	if (nr == 0)
		return pci_scan_bus(0, &via82c505_ops, sysdata);

	return NULL;
}
