/*
 *  linux/drivers/char/acpi_serial.c
 *
 *  Copyright (C) 2000  Hewlett-Packard Co.
 *  Copyright (C) 2000  Khalid Aziz <khalid_aziz@hp.com>
 *
 *  Detect and initialize the headless console serial port defined in 
 *  SPCR table and debug serial port defined in DBGP table
 *
 */
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/pci.h>
#include <linux/pm.h>
#include <linux/acpi.h>
#include <linux/init.h>
#include <linux/serial.h>
#include <asm/serial.h>
#include <asm/io.h>
#include <linux/acpi_serial.h>
/*#include <asm/acpi-ext.h>*/

#undef SERIAL_DEBUG_ACPI

/*
 * Query ACPI tables for a debug and a headless console serial
 * port. If found, add them to rs_table[]. A pointer to either SPCR
 * or DBGP table is passed as parameter. This function should be called 
 * before serial_console_init() is called to make sure the SPCR serial 
 * console will be available for use. IA-64 kernel calls this function
 * from within acpi.c when it encounters SPCR or DBGP tables as it parses 
 * the ACPI 2.0 tables during bootup.
 *
 */
void __init setup_serial_acpi(void *tablep) 
{
	acpi_ser_t *acpi_ser_p;
	struct serial_struct serial_req;
	unsigned long iobase;
	int global_sys_irq;

#ifdef SERIAL_DEBUG_ACPI
	printk("Entering setup_serial_acpi()\n");
#endif

	/* Now get the table */
	if (tablep == NULL) {
		return;
	}

	acpi_ser_p = (acpi_ser_t *)tablep;

	/*
	 * Perform a sanity check on the table. Table should have a 
	 * signature of "SPCR" or "DBGP" and it should be atleast 52 bytes
	 * long.
	 */
	if ((strncmp(acpi_ser_p->signature, ACPI_SPCRT_SIGNATURE, 
					ACPI_SIG_LEN) != 0) && 
		(strncmp(acpi_ser_p->signature, ACPI_DBGPT_SIGNATURE, 
					ACPI_SIG_LEN) != 0)) {
		return;
	}
	if (acpi_ser_p->length < 52) {
		return;
	}

	iobase = (((u64) acpi_ser_p->base_addr.addrh) << 32) | acpi_ser_p->base_addr.addrl;
	global_sys_irq = (acpi_ser_p->global_int[3] << 24) | 
			(acpi_ser_p->global_int[2] << 16) |
			(acpi_ser_p->global_int[1] << 8) |
			acpi_ser_p->global_int[0];

#ifdef SERIAL_DEBUG_ACPI
	printk("setup_serial_acpi(): table pointer = 0x%p\n", acpi_ser_p);
	printk("                     sig = '%c%c%c%c'\n",
			acpi_ser_p->signature[0],
			acpi_ser_p->signature[1],
			acpi_ser_p->signature[2],
			acpi_ser_p->signature[3]);
	printk("                     length = %d\n", acpi_ser_p->length);
	printk("                     Rev = %d\n", acpi_ser_p->rev);
	printk("                     Interface type = %d\n", acpi_ser_p->intfc_type);
	printk("                     Base address = 0x%lX\n", iobase);
	printk("                     IRQ = %d\n", acpi_ser_p->irq);
	printk("                     Global System Int = %d\n", global_sys_irq);
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
	 * Now build a serial_req structure to update the entry in
	 * rs_table for the headless console port.
	 */
	switch (acpi_ser_p->intfc_type) {
 		case ACPI_SERIAL_INTFC_16550:
			serial_req.type = PORT_16550;
			serial_req.baud_base = BASE_BAUD;
			break;

 		case ACPI_SERIAL_INTFC_16450:
			serial_req.type = PORT_16450;
			serial_req.baud_base = BASE_BAUD;
			break;

		default:
			serial_req.type = PORT_UNKNOWN;
			break;
	}
	if (strncmp(acpi_ser_p->signature, ACPI_SPCRT_SIGNATURE,
					ACPI_SIG_LEN) == 0) {
		serial_req.line = ACPI_SERIAL_CONSOLE_PORT;
	}
	else if (strncmp(acpi_ser_p->signature, ACPI_DBGPT_SIGNATURE, 
					ACPI_SIG_LEN) == 0) {
		serial_req.line = ACPI_SERIAL_DEBUG_PORT;
	}
	/*
	 * Check if this is an I/O mapped address or a memory mapped address
	 */
	if (acpi_ser_p->base_addr.space_id == ACPI_SERIAL_MEM_SPACE) {
		serial_req.port = 0;
		serial_req.port_high = 0;
		serial_req.iomem_base = (void *)ioremap(iobase, 64);
		serial_req.io_type = SERIAL_IO_MEM;
	}
	else if (acpi_ser_p->base_addr.space_id == ACPI_SERIAL_IO_SPACE) {
		serial_req.port = (unsigned long) iobase & 0xffffffff;
		serial_req.port_high = (unsigned long)(((u64)iobase) >> 32);
		serial_req.iomem_base = NULL;
		serial_req.io_type = SERIAL_IO_PORT;
	}
	else if (acpi_ser_p->base_addr.space_id == ACPI_SERIAL_PCICONF_SPACE) {
		printk("WARNING: No support for PCI serial console\n");
		return;
	}

	/*
	 * If the table does not have IRQ information, use 0 for IRQ. 
	 * This will force rs_init() to probe for IRQ. 
	 */
	if (acpi_ser_p->length < 53) {
		serial_req.irq = 0;
	}
	else {
		serial_req.flags = ASYNC_SKIP_TEST | ASYNC_BOOT_AUTOCONF | 
					ASYNC_AUTO_IRQ;
		if (acpi_ser_p->int_type & 
			(ACPI_SERIAL_INT_APIC | ACPI_SERIAL_INT_SAPIC)) {
			serial_req.irq = global_sys_irq;
		}
		else if (acpi_ser_p->int_type & ACPI_SERIAL_INT_PCAT) {
			serial_req.irq = acpi_ser_p->irq;
		}
		else {
			/*
			 * IRQ type not being set would mean UART will
			 * run in polling mode. Do not probe for IRQ in
			 * that case.
			 */
			serial_req.flags = ASYNC_SKIP_TEST|ASYNC_BOOT_AUTOCONF;
		}
	}

	serial_req.xmit_fifo_size = serial_req.custom_divisor = 0;
	serial_req.close_delay = serial_req.hub6 = serial_req.closing_wait = 0;
	serial_req.iomem_reg_shift = 0;
	if (early_serial_setup(&serial_req) < 0) {
		printk("early_serial_setup() for ACPI serial console port failed\n");
		return;
	}

#ifdef SERIAL_DEBUG_ACPI
	printk("Leaving setup_serial_acpi()\n");
#endif
}
