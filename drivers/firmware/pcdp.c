/*
 * Parse the EFI PCDP table to locate the console device.
 *
 * (c) Copyright 2002, 2003, 2004 Hewlett-Packard Development Company, L.P.
 *	Khalid Aziz <khalid.aziz@hp.com>
 *	Alex Williamson <alex.williamson@hp.com>
 *	Bjorn Helgaas <bjorn.helgaas@hp.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/acpi.h>
#include <linux/console.h>
#include <linux/efi.h>
#include <linux/tty.h>
#include <linux/serial.h>
#include <linux/serial_core.h>
#include <asm/io.h>
#include <asm/serial.h>
#include "pcdp.h"

static inline int
uart_irq_supported(int rev, struct pcdp_uart *uart)
{
	if (rev < 3)
		return uart->pci_func & PCDP_UART_IRQ;
	return uart->flags & PCDP_UART_IRQ;
}

static inline int
uart_pci(int rev, struct pcdp_uart *uart)
{
	if (rev < 3)
		return uart->pci_func & PCDP_UART_PCI;
	return uart->flags & PCDP_UART_PCI;
}

static inline int
uart_active_high_low(int rev, struct pcdp_uart *uart)
{
	if (uart_pci(rev, uart) || uart->flags & PCDP_UART_ACTIVE_LOW)
		return ACPI_ACTIVE_LOW;
	return ACPI_ACTIVE_HIGH;
}

static inline int
uart_edge_level(int rev, struct pcdp_uart *uart)
{
	if (uart_pci(rev, uart))
		return ACPI_LEVEL_SENSITIVE;
	if (rev < 3 || uart->flags & PCDP_UART_EDGE_SENSITIVE)
		return ACPI_EDGE_SENSITIVE;
	return ACPI_LEVEL_SENSITIVE;
}

static void __init
setup_serial_console(int rev, struct pcdp_uart *uart)
{
#ifdef CONFIG_SERIAL_8250_CONSOLE
	struct uart_port port;
	static char options[16];
	int mapsize = 64;

	memset(&port, 0, sizeof(port));
	port.uartclk = uart->clock_rate;
	if (!port.uartclk)	/* some FW doesn't supply this */
		port.uartclk = BASE_BAUD * 16;

	if (uart->addr.address_space_id == ACPI_ADR_SPACE_SYSTEM_MEMORY) {
		port.mapbase = uart->addr.address;
		port.membase = ioremap(port.mapbase, mapsize);
		if (!port.membase) {
			printk(KERN_ERR "%s: couldn't ioremap 0x%lx-0x%lx\n",
				__FUNCTION__, port.mapbase, port.mapbase + mapsize);
			return;
		}
		port.iotype = UPIO_MEM;
	} else if (uart->addr.address_space_id == ACPI_ADR_SPACE_SYSTEM_IO) {
		port.iobase = uart->addr.address;
		port.iotype = UPIO_PORT;
	} else
		return;

	switch (uart->pci_prog_intfc) {
		case 0x0: port.type = PORT_8250;    break;
		case 0x1: port.type = PORT_16450;   break;
		case 0x2: port.type = PORT_16550;   break;
		case 0x3: port.type = PORT_16650;   break;
		case 0x4: port.type = PORT_16750;   break;
		case 0x5: port.type = PORT_16850;   break;
		case 0x6: port.type = PORT_16C950;  break;
		default:  port.type = PORT_UNKNOWN; break;
	}

	port.flags = UPF_SKIP_TEST | UPF_BOOT_AUTOCONF;

	if (uart_irq_supported(rev, uart)) {
		port.irq = acpi_register_gsi(uart->gsi,
			uart_active_high_low(rev, uart),
			uart_edge_level(rev, uart));
		port.flags |= UPF_AUTO_IRQ;  /* some FW reported wrong GSI */
		if (uart_pci(rev, uart))
			port.flags |= UPF_SHARE_IRQ;
	}

	if (early_serial_setup(&port) < 0)
		return;

	snprintf(options, sizeof(options), "%lun%d", uart->baud,
		uart->bits ? uart->bits : 8);
	add_preferred_console("ttyS", port.line, options);

	printk(KERN_INFO "PCDP: serial console at %s 0x%lx (ttyS%d, options %s)\n",
		port.iotype == UPIO_MEM ? "MMIO" : "I/O",
		uart->addr.address, port.line, options);
#endif
}

static void __init
setup_vga_console(struct pcdp_vga *vga)
{
#ifdef CONFIG_VT
#ifdef CONFIG_VGA_CONSOLE
	if (efi_mem_type(0xA0000) == EFI_CONVENTIONAL_MEMORY) {
		printk(KERN_ERR "PCDP: VGA selected, but frame buffer is not MMIO!\n");
		return;
	}

	conswitchp = &vga_con;
	printk(KERN_INFO "PCDP: VGA console\n");
#endif
#endif
}

void __init
efi_setup_pcdp_console(char *cmdline)
{
	struct pcdp *pcdp;
	struct pcdp_uart *uart;
	struct pcdp_device *dev, *end;
	int i, serial = 0;

	pcdp = efi.hcdp;
	if (!pcdp)
		return;

	printk(KERN_INFO "PCDP: v%d at 0x%p\n", pcdp->rev, pcdp);

	if (pcdp->rev < 3) {
		if (strstr(cmdline, "console=ttyS0") || efi_uart_console_only())
			serial = 1;
	}

	for (i = 0, uart = pcdp->uart; i < pcdp->num_uarts; i++, uart++) {
		if (uart->flags & PCDP_UART_PRIMARY_CONSOLE || serial) {
			if (uart->type == PCDP_CONSOLE_UART) {
				setup_serial_console(pcdp->rev, uart);
				return;
			}
		}
	}

	end = (struct pcdp_device *) ((u8 *) pcdp + pcdp->length);
	for (dev = (struct pcdp_device *) (pcdp->uart + pcdp->num_uarts);
	     dev < end;
	     dev = (struct pcdp_device *) ((u8 *) dev + dev->length)) {
		if (dev->flags & PCDP_PRIMARY_CONSOLE) {
			if (dev->type == PCDP_CONSOLE_VGA) {
				setup_vga_console((struct pcdp_vga *) dev);
				return;
			}
		}
	}
}

#ifdef CONFIG_IA64_EARLY_PRINTK_UART
unsigned long
hcdp_early_uart (void)
{
	efi_system_table_t *systab;
	efi_config_table_t *config_tables;
	unsigned long addr = 0;
	struct pcdp *pcdp = 0;
	struct pcdp_uart *uart;
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
			pcdp = (struct pcdp *) config_tables[i].table;
			break;
		}
	}
	if (!pcdp)
		return 0;
	pcdp = __va(pcdp);

	for (i = 0, uart = pcdp->uart; i < pcdp->num_uarts; i++, uart++) {
		if (uart->type == PCDP_CONSOLE_UART) {
			addr = uart->addr.address;
			break;
		}
	}
	return addr;
}
#endif /* CONFIG_IA64_EARLY_PRINTK_UART */
