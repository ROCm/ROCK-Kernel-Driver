#ifndef _ASM_IA64_ACPI_EXT_H
#define _ASM_IA64_ACPI_EXT_H

/*
 * Advanced Configuration and Power Infterface
 * Based on 'ACPI Specification 1.0b' Febryary 2, 1999
 * and 'IA-64 Extensions to the ACPI Specification' Rev 0.6
 * 
 * Copyright (C) 1999 VA Linux Systems
 * Copyright (C) 1999 Walt Drummond <drummond@valinux.com>
 * Copyright (C) 2000 Intel Corp.
 * Copyright (C) 2000 J.I. Lee <jung-ik.lee@intel.com>
 *	ACPI 2.0 specification 
 */

#include <linux/types.h>

#pragma	pack(1)
#define ACPI_RSDP_SIG "RSD PTR " /* Trailing space required */
#define ACPI_RSDP_SIG_LEN 8
typedef struct {
	char signature[8];
	u8 checksum;
	char oem_id[6];
	u8 revision;
	u32 rsdt;
	u32 lenth;
	struct acpi_xsdt *xsdt;
	u8 ext_checksum;
	u8 reserved[3];
} acpi20_rsdp_t;

typedef struct {
	char signature[4];
	u32 length;
	u8 revision;
	u8 checksum;
	char oem_id[6];
	char oem_table_id[8];
	u32 oem_revision;
	u32 creator_id;
	u32 creator_revision;
} acpi_desc_table_hdr_t;

#define ACPI_RSDT_SIG "RSDT"
#define ACPI_RSDT_SIG_LEN 4
typedef struct {
	acpi_desc_table_hdr_t header;
	u8 reserved[4];
	u32 entry_ptrs[1];	/* Not really . . . */
} acpi20_rsdt_t;

#define ACPI_XSDT_SIG "XSDT"
#define ACPI_XSDT_SIG_LEN 4
typedef struct acpi_xsdt {
	acpi_desc_table_hdr_t header;
	unsigned long entry_ptrs[1];	/* Not really . . . */
} acpi_xsdt_t;

/* Common structures for ACPI 2.0 and 0.71 */

typedef struct acpi_entry_iosapic {
	u8 type;
	u8 length;
	u8 id;
	u8 reserved;
	u32 irq_base;	/* start of IRQ's this IOSAPIC is responsible for. */
	unsigned long address;	/* Address of this IOSAPIC */
} acpi_entry_iosapic_t;

/* Local SAPIC flags */
#define LSAPIC_ENABLED                (1<<0)
#define LSAPIC_PERFORMANCE_RESTRICTED (1<<1)
#define LSAPIC_PRESENT                (1<<2)

/* Defines legacy IRQ->pin mapping */
typedef struct {
	u8 type;
	u8 length;
	u8 bus;		/* Constant 0 == ISA */
	u8 isa_irq;	/* ISA IRQ # */
	u32 pin;		/* called vector in spec; really IOSAPIC pin number */
	u16 flags;	/* Edge/Level trigger & High/Low active */
} acpi_entry_int_override_t;

#define INT_OVERRIDE_ACTIVE_LOW    0x03
#define INT_OVERRIDE_LEVEL_TRIGGER 0x0d

/* IA64 ext 0.71 */

typedef struct {
	char signature[8];
	u8 checksum;
	char oem_id[6];
	char reserved;		/* Must be 0 */
	struct acpi_rsdt *rsdt;
} acpi_rsdp_t;

typedef struct {
	acpi_desc_table_hdr_t header;
	u8 reserved[4];
	unsigned long entry_ptrs[1];	/* Not really . . . */
} acpi_rsdt_t;

#define ACPI_SAPIC_SIG "SPIC"
#define ACPI_SAPIC_SIG_LEN 4
typedef struct {
	acpi_desc_table_hdr_t header;
	u8 reserved[4];
	unsigned long interrupt_block;
} acpi_sapic_t;

/* SAPIC structure types */
#define ACPI_ENTRY_LOCAL_SAPIC         0
#define ACPI_ENTRY_IO_SAPIC            1
#define ACPI_ENTRY_INT_SRC_OVERRIDE    2
#define ACPI_ENTRY_PLATFORM_INT_SOURCE 3	/* Unimplemented */

typedef struct acpi_entry_lsapic {
	u8 type;
	u8 length;
	u16 acpi_processor_id;
	u16 flags;
	u8 id;
	u8 eid;
} acpi_entry_lsapic_t;

typedef struct {
	u8 type;
	u8 length;
	u16 flags;
	u8 int_type;
	u8 id;
	u8 eid;
	u8 iosapic_vector;
	u8 reserved[4];
	u32 global_vector;
} acpi_entry_platform_src_t;

/* ACPI 2.0 with 1.3 errata specific structures */

#define ACPI_MADT_SIG "APIC"
#define ACPI_MADT_SIG_LEN 4
typedef struct {
	acpi_desc_table_hdr_t header;
	u32 lapic_address;
	u32 flags;
} acpi_madt_t;

/* acpi 2.0 MADT structure types */
#define ACPI20_ENTRY_LOCAL_APIC                 0
#define ACPI20_ENTRY_IO_APIC                    1
#define ACPI20_ENTRY_INT_SRC_OVERRIDE           2
#define ACPI20_ENTRY_NMI_SOURCE                 3
#define ACPI20_ENTRY_LOCAL_APIC_NMI             4
#define ACPI20_ENTRY_LOCAL_APIC_ADDR_OVERRIDE   5
#define ACPI20_ENTRY_IO_SAPIC                   6
#define ACPI20_ENTRY_LOCAL_SAPIC                7
#define ACPI20_ENTRY_PLATFORM_INT_SOURCE        8

typedef struct acpi20_entry_lsapic {
	u8 type;
	u8 length;
	u8 acpi_processor_id;
	u8 id;
	u8 eid;
	u8 reserved[3];
	u32 flags;
} acpi20_entry_lsapic_t;

typedef struct acpi20_entry_lapic_addr_override {
	u8 type;
	u8 length;
	u8 reserved[2];
	unsigned long lapic_address;
} acpi20_entry_lapic_addr_override_t;

typedef struct {
	u8 type;
	u8 length;
	u16 flags;
	u8 int_type;
	u8 id;
	u8 eid;
	u8 iosapic_vector;
	u32 global_vector;
} acpi20_entry_platform_src_t;

extern int acpi20_parse(acpi20_rsdp_t *);
extern int acpi_parse(acpi_rsdp_t *);
extern const char *acpi_get_sysname (void);

extern void (*acpi_idle) (void);	/* power-management idle function, if any */
#pragma	pack()
#endif /* _ASM_IA64_ACPI_EXT_H */
