/*
 * linux/drivers/char/acpi_serial.c
 *
 * Copyright (C) 2000, 2002 Hewlett-Packard Co.
 *	Khalid Aziz <khalid_aziz@hp.com>
 *
 * Detect and initialize the headless console serial port defined in SPCR table and debug
 * serial port defined in DBGP table.
 *
 * 2002/08/29 davidm	Adjust it to new 2.5 serial driver infrastructure.
 */
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/acpi.h>
#include <linux/init.h>
#include <linux/tty.h>
#include <linux/serial.h>
#include <linux/serial_core.h>
#include <linux/acpi_serial.h>

#include <asm/io.h>
#include <asm/serial.h>

#undef SERIAL_DEBUG_ACPI

#define ACPI_SERIAL_CONSOLE_PORT	0
#define ACPI_SERIAL_DEBUG_PORT		5

/*
 * Query ACPI tables for a debug and a headless console serial port. If found, add them to
 * rs_table[]. A pointer to either SPCR or DBGP table is passed as parameter. This
 * function should be called before serial_console_init() is called to make sure the SPCR
 * serial console will be available for use. IA-64 kernel calls this function from within
 * acpi.c when it encounters SPCR or DBGP tables as it parses the ACPI 2.0 tables during
 * bootup.
 */
void __init
setup_serial_acpi (void *tablep)
{
	acpi_ser_t *acpi_ser_p;
	struct uart_port port;
	unsigned long iobase;
	int gsi;

#ifdef SERIAL_DEBUG_ACPI
	printk("Entering setup_serial_acpi()\n");
#endif

	/* Now get the table */
	if (!tablep)
		return;

	memset(&port, 0, sizeof(port));

	acpi_ser_p = (acpi_ser_t *) tablep;

	/*
	 * Perform a sanity check on the table. Table should have a signature of "SPCR" or
	 * "DBGP" and it should be atleast 52 bytes long.
	 */
	if (strncmp(acpi_ser_p->signature, ACPI_SPCRT_SIGNATURE, ACPI_SIG_LEN) != 0 &&
	    strncmp(acpi_ser_p->signature, ACPI_DBGPT_SIGNATURE, ACPI_SIG_LEN) != 0)
		return;
	if (acpi_ser_p->length < 52)
		return;

	iobase = (((u64) acpi_ser_p->base_addr.addrh) << 32) | acpi_ser_p->base_addr.addrl;
	gsi = (  (acpi_ser_p->global_int[3] << 24) | (acpi_ser_p->global_int[2] << 16)
	       | (acpi_ser_p->global_int[1] <<  8) | (acpi_ser_p->global_int[0] <<  0));

#ifdef SERIAL_DEBUG_ACPI
	printk("setup_serial_acpi(): table pointer = 0x%p\n", acpi_ser_p);
	printk("                     sig = '%c%c%c%c'\n", acpi_ser_p->signature[0],
	       acpi_ser_p->signature[1], acpi_ser_p->signature[2], acpi_ser_p->signature[3]);
	printk("                     length = %d\n", acpi_ser_p->length);
	printk("                     Rev = %d\n", acpi_ser_p->rev);
	printk("                     Interface type = %d\n", acpi_ser_p->intfc_type);
	printk("                     Base address = 0x%lX\n", iobase);
	printk("                     IRQ = %d\n", acpi_ser_p->irq);
	printk("                     Global System Int = %d\n", gsi);
	printk("                     Baud rate = ");
	switch (acpi_ser_p->baud) {
	      case ACPI_SERIAL_BAUD_9600:
		printk("9600\n");
		break;

	      case ACPI_SERIAL_BAUD_19200:
		printk("19200\n");
		break;

	      case ACPI_SERIAL_BAUD_57600:
		printk("57600\n");
		break;

	      case ACPI_SERIAL_BAUD_115200:
		printk("115200\n");
		break;

	      default:
		printk("Huh (%d)\n", acpi_ser_p->baud);
		break;
	}
	if (acpi_ser_p->base_addr.space_id == ACPI_SERIAL_PCICONF_SPACE) {
		printk("                     PCI serial port:\n");
		printk("                         Bus %d, Device %d, Vendor ID 0x%x, Dev ID 0x%x\n",
		       acpi_ser_p->pci_bus, acpi_ser_p->pci_dev,
		       acpi_ser_p->pci_vendor_id, acpi_ser_p->pci_dev_id);
	}
#endif
	/*
	 * Now build a serial_req structure to update the entry in rs_table for the
	 * headless console port.
	 */
	switch (acpi_ser_p->intfc_type) {
	      case ACPI_SERIAL_INTFC_16550:
		port.type = PORT_16550;
		port.uartclk = BASE_BAUD * 16;
		break;

	      case ACPI_SERIAL_INTFC_16450:
		port.type = PORT_16450;
		port.uartclk = BASE_BAUD * 16;
		break;

	      default:
		port.type = PORT_UNKNOWN;
		break;
	}
	if (strncmp(acpi_ser_p->signature, ACPI_SPCRT_SIGNATURE, ACPI_SIG_LEN) == 0)
		port.line = ACPI_SERIAL_CONSOLE_PORT;
	else if (strncmp(acpi_ser_p->signature, ACPI_DBGPT_SIGNATURE, ACPI_SIG_LEN) == 0)
		port.line = ACPI_SERIAL_DEBUG_PORT;
	/*
	 * Check if this is an I/O mapped address or a memory mapped address
	 */
	if (acpi_ser_p->base_addr.space_id == ACPI_SERIAL_MEM_SPACE) {
		port.iobase = 0;
		port.mapbase = iobase;
		port.membase = ioremap(iobase, 64);
		port.iotype = SERIAL_IO_MEM;
	} else if (acpi_ser_p->base_addr.space_id == ACPI_SERIAL_IO_SPACE) {
		port.iobase = iobase;
		port.mapbase = 0;
		port.membase = NULL;
		port.iotype = SERIAL_IO_PORT;
	} else if (acpi_ser_p->base_addr.space_id == ACPI_SERIAL_PCICONF_SPACE) {
		printk("WARNING: No support for PCI serial console\n");
		return;
	}

	/*
	 * If the table does not have IRQ information, use 0 for IRQ.  This will force
	 * rs_init() to probe for IRQ.
	 */
	if (acpi_ser_p->length < 53)
		port.irq = 0;
	else {
		port.flags = UPF_SKIP_TEST | UPF_BOOT_AUTOCONF | UPF_AUTO_IRQ;
		if (acpi_ser_p->int_type & (ACPI_SERIAL_INT_APIC | ACPI_SERIAL_INT_SAPIC))
			port.irq = gsi;
		else if (acpi_ser_p->int_type & ACPI_SERIAL_INT_PCAT)
			port.irq = acpi_ser_p->irq;
		else
			/*
			 * IRQ type not being set would mean UART will run in polling
			 * mode. Do not probe for IRQ in that case.
			 */
			port.flags &= UPF_AUTO_IRQ;
	}
	if (early_serial_setup(&port) < 0) {
		printk("early_serial_setup() for ACPI serial console port failed\n");
		return;
	}

#ifdef SERIAL_DEBUG_ACPI
	printk("Leaving setup_serial_acpi()\n");
#endif
}
