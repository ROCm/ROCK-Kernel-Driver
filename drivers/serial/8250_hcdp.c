/*
 * linux/drivers/char/hcdp_serial.c
 *
 * Copyright (C) 2002 Hewlett-Packard Co.
 *	Khalid Aziz <khalid_aziz@hp.com>
 *
 * Parse the EFI HCDP table to locate serial console and debug ports and
 * initialize them.
 *
 * 2002/08/29 davidm	Adjust it to new 2.5 serial driver infrastructure.
 */

#include <linux/config.h>
#include <linux/console.h>
#include <linux/kernel.h>
#include <linux/efi.h>
#include <linux/init.h>
#include <linux/tty.h>
#include <linux/serial.h>
#include <linux/serial_core.h>
#include <linux/types.h>
#include <linux/acpi.h>

#include <asm/io.h>
#include <asm/serial.h>
#include <asm/acpi.h>

#include "8250_hcdp.h"

#undef SERIAL_DEBUG_HCDP

/*
 * Parse the HCDP table to find descriptions for headless console and debug
 * serial ports and add them to rs_table[]. A pointer to HCDP table is
 * passed as parameter. This function should be called before
 * serial_console_init() is called to make sure the HCDP serial console will
 * be available for use. IA-64 kernel calls this function from setup_arch()
 * after the EFI and ACPI tables have been parsed.
 */
void __init
setup_serial_hcdp(void *tablep)
{
	hcdp_dev_t *hcdp_dev;
	struct uart_port port;
	unsigned long iobase;
	hcdp_t hcdp;
	int gsi, nr;
	static char options[16];
#if 0
	static int shift_once = 1;
#endif

#ifdef SERIAL_DEBUG_HCDP
	printk("Entering setup_serial_hcdp()\n");
#endif

	/* Verify we have a valid table pointer */
	if (!tablep)
		return;

	memset(&port, 0, sizeof(port));

	/*
	 * Don't trust firmware to give us a table starting at an aligned
	 * address. Make a local copy of the HCDP table with aligned
	 * structures.
	 */
	memcpy(&hcdp, tablep, sizeof(hcdp));

	/*
	 * Perform a sanity check on the table. Table should have a signature
	 * of "HCDP" and it should be atleast 82 bytes long to have any
	 * useful information.
	 */
	if ((strncmp(hcdp.signature, HCDP_SIGNATURE, HCDP_SIG_LEN) != 0))
		return;
	if (hcdp.len < 82)
		return;

#ifdef SERIAL_DEBUG_HCDP
	printk("setup_serial_hcdp(): table pointer = 0x%p, sig = '%.4s'\n",
	       tablep, hcdp.signature);
	printk(" length = %d, rev = %d, ", hcdp.len, hcdp.rev);
	printk("OEM ID = %.6s, # of entries = %d\n", hcdp.oemid,
			hcdp.num_entries);
#endif

	/*
	 * Parse each device entry
	 */
	for (nr = 0; nr < hcdp.num_entries; nr++) {
		hcdp_dev = hcdp.hcdp_dev + nr;
		/*
		 * We will parse only the primary console device which is
		 * the first entry for these devices. We will ignore rest
		 * of the entries for the same type device that has already
		 * been parsed and initialized
		 */
		if (hcdp_dev->type != HCDP_DEV_CONSOLE)
			continue;

		iobase = ((u64) hcdp_dev->base_addr.addrhi << 32) |
					hcdp_dev->base_addr.addrlo;
		gsi = hcdp_dev->global_int;

		/* See PCI spec v2.2, Appendix D (Class Codes): */
		switch (hcdp_dev->pci_prog_intfc) {
		case 0x00:
			port.type = PORT_8250;
			break;
		case 0x01:
			port.type = PORT_16450;
			break;
		case 0x02:
			port.type = PORT_16550;
			break;
		case 0x03:
			port.type = PORT_16650;
			break;
		case 0x04:
			port.type = PORT_16750;
			break;
		case 0x05:
			port.type = PORT_16850;
			break;
		case 0x06:
			port.type = PORT_16C950;
			break;
		default:
			printk(KERN_WARNING "warning: EFI HCDP table reports "
				"unknown serial programming interface 0x%02x; "
				"will autoprobe.\n", hcdp_dev->pci_prog_intfc);
			port.type = PORT_UNKNOWN;
			break;
		}

#ifdef SERIAL_DEBUG_HCDP
		printk("  type = %s, uart = %d\n",
			((hcdp_dev->type == HCDP_DEV_CONSOLE) ?
			"Headless Console" :
			((hcdp_dev->type == HCDP_DEV_DEBUG) ?
			"Debug port" : "Huh????")), port.type);
		printk("  base address space = %s, base address = 0x%lx\n",
		       ((hcdp_dev->base_addr.space_id == ACPI_MEM_SPACE) ?
		       "Memory Space" :
			((hcdp_dev->base_addr.space_id == ACPI_IO_SPACE) ?
			"I/O space" : "PCI space")),
		       iobase);
		printk("  gsi = %d, baud rate = %lu, bits = %d, clock = %d\n",
		       gsi, (unsigned long) hcdp_dev->baud, hcdp_dev->bits,
		       hcdp_dev->clock_rate);
		if (HCDP_PCI_UART(hcdp_dev))
			printk(" PCI id: %02x:%02x:%02x, vendor ID=0x%x, "
				"dev ID=0x%x\n", hcdp_dev->pci_seg,
				hcdp_dev->pci_bus, hcdp_dev->pci_dev,
				hcdp_dev->pci_vendor_id, hcdp_dev->pci_dev_id);
#endif
		/*
		 * Now fill in a port structure to update the 8250 port table..
		 */
		if (hcdp_dev->clock_rate)
			port.uartclk = hcdp_dev->clock_rate;
		else
			port.uartclk = BASE_BAUD * 16;

		/*
		 * Check if this is an I/O mapped address or a memory mapped
		 * address
		 */
		if (hcdp_dev->base_addr.space_id == ACPI_MEM_SPACE) {
			port.iobase = 0;
			port.mapbase = iobase;
			port.membase = ioremap(iobase, 64);
			port.iotype = SERIAL_IO_MEM;
		} else if (hcdp_dev->base_addr.space_id == ACPI_IO_SPACE) {
			port.iobase = iobase;
			port.mapbase = 0;
			port.membase = NULL;
			port.iotype = SERIAL_IO_PORT;
		} else if (hcdp_dev->base_addr.space_id == ACPI_PCICONF_SPACE) {
			printk(KERN_WARNING"warning: No support for PCI serial console\n");
			return;
		}

		if (HCDP_IRQ_SUPPORTED(hcdp_dev)) {
			if (HCDP_PCI_UART(hcdp_dev))
				port.irq = acpi_register_gsi(gsi,
					ACPI_LEVEL_SENSITIVE, ACPI_ACTIVE_LOW);
			else
				port.irq = acpi_register_gsi(gsi,
					ACPI_EDGE_SENSITIVE, ACPI_ACTIVE_HIGH);
			port.flags |= UPF_AUTO_IRQ;

			if (HCDP_PCI_UART(hcdp_dev))
				port.flags |= UPF_SHARE_IRQ;
		}

		port.flags |= UPF_SKIP_TEST | UPF_BOOT_AUTOCONF | UPF_RESOURCES;

		/*
		 * Note: the above memset() initializes port.line to 0,
		 * so we register this port as ttyS0.
		 */
		if (early_serial_setup(&port) < 0) {
			printk("setup_serial_hcdp(): early_serial_setup() "
				"for HCDP serial console port failed. "
				"Will try any additional consoles in HCDP.\n");
			memset(&port, 0, sizeof(port));
			continue;
		}

		if (efi_uart_console_only()) {
			snprintf(options, sizeof(options), "%lun%d",
				hcdp_dev->baud, hcdp_dev->bits);
			add_preferred_console("ttyS", port.line, options);
		}
		break;
	}

#ifdef SERIAL_DEBUG_HCDP
	printk("Leaving setup_serial_hcdp()\n");
#endif
}

#ifdef CONFIG_IA64_EARLY_PRINTK_UART
unsigned long
hcdp_early_uart (void)
{
	efi_system_table_t *systab;
	efi_config_table_t *config_tables;
	unsigned long addr = 0;
	hcdp_t *hcdp = 0;
	hcdp_dev_t *dev;
	int i;

	systab = (efi_system_table_t *) ia64_boot_param->efi_systab;
	if (!systab)
		return 0;
	systab = __va(systab);

	config_tables = (efi_config_table_t *) systab->tables;
	if (!config_tables)
		return 0;
	config_tables = __va(config_tables);

	for (i = 0; i < systab->nr_tables; i++) {
		if (efi_guidcmp(config_tables[i].guid, HCDP_TABLE_GUID) == 0) {
			hcdp = (hcdp_t *) config_tables[i].table;
			break;
		}
	}
	if (!hcdp)
		return 0;
	hcdp = __va(hcdp);

	for (i = 0, dev = hcdp->hcdp_dev; i < hcdp->num_entries; i++, dev++) {
		if (dev->type == HCDP_DEV_CONSOLE) {
			addr = (u64) dev->base_addr.addrhi << 32 | dev->base_addr.addrlo;
			break;
		}
	}
	return addr;
}
#endif /* CONFIG_IA64_EARLY_PRINTK_UART */
