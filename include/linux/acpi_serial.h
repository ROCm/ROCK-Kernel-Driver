/*
 *  linux/include/linux/acpi_serial.h
 *
 *  Copyright (C) 2000  Hewlett-Packard Co.
 *  Copyright (C) 2000  Khalid Aziz <khalid_aziz@hp.com>
 *
 *  Definitions for ACPI defined serial ports (headless console and 
 *  debug ports)
 *
 */

extern void setup_serial_acpi(void *);

/* ACPI table signatures */
#define ACPI_SPCRT_SIGNATURE	"SPCR"
#define ACPI_DBGPT_SIGNATURE	"DBGP"

/* Interface type as defined in ACPI serial port tables */
#define ACPI_SERIAL_INTFC_16550	0
#define ACPI_SERIAL_INTFC_16450	1

/* Interrupt types for ACPI serial port tables */
#define ACPI_SERIAL_INT_PCAT	0x01
#define ACPI_SERIAL_INT_APIC	0x02
#define ACPI_SERIAL_INT_SAPIC	0x04

/* Baud rates as defined in ACPI serial port tables */
#define ACPI_SERIAL_BAUD_9600		3
#define ACPI_SERIAL_BAUD_19200		4
#define ACPI_SERIAL_BAUD_57600		6
#define ACPI_SERIAL_BAUD_115200		7

/* Parity as defined in ACPI serial port tables */
#define ACPI_SERIAL_PARITY_NONE		0

/* Flow control methods as defined in ACPI serial port tables */
#define ACPI_SERIAL_FLOW_DCD	0x01
#define ACPI_SERIAL_FLOW_RTS	0x02
#define ACPI_SERIAL_FLOW_XON	0x04

/* Terminal types as defined in ACPI serial port tables */
#define ACPI_SERIAL_TERM_VT100		0
#define ACPI_SERIAL_TERM_VT100X	1

/* PCI Flags as defined by SPCR table */
#define ACPI_SERIAL_PCIFLAG_PNP	0x00000001

/* Space ID as defined in base address structure in ACPI serial port tables */
#define ACPI_SERIAL_MEM_SPACE		0
#define ACPI_SERIAL_IO_SPACE		1
#define ACPI_SERIAL_PCICONF_SPACE	2

/* 
 * Generic Register Address Structure - as defined by Microsoft 
 * in http://www.microsoft.com/hwdev/onnow/download/LFreeACPI.doc
 *
*/
typedef struct {
	u8  space_id;
	u8  bit_width;
	u8  bit_offset;
	u8  resv;
	u32 addrl;
	u32 addrh;
} gen_regaddr;

/* Space ID for generic register address structure */
#define REGADDR_SPACE_SYSMEM	0
#define REGADDR_SPACE_SYSIO	1
#define REGADDR_SPACE_PCICONFIG	2

/* Serial Port Console Redirection and Debug Port Table formats */
typedef struct {
	u8 signature[4];
	u32 length;
	u8  rev;
	u8  chksum;
	u8  oemid[6];
	u8  oem_tabid[8];
	u32 oem_rev;
	u8  creator_id[4];
	u32 creator_rev;
	u8  intfc_type;
	u8  resv1[3];
	gen_regaddr base_addr;
	u8  int_type;
	u8  irq;
	u8  global_int[4];
	u8  baud;
	u8  parity;
	u8  stop_bits;
	u8  flow_ctrl;
	u8  termtype;
	u8  language;
	u16 pci_dev_id;
	u16 pci_vendor_id;
	u8  pci_bus;
	u8  pci_dev;
	u8  pci_func;
	u8  pci_flags[4];
	u8  pci_seg;
	u32 resv2;
} acpi_ser_t;
