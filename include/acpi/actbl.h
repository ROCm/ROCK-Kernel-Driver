/******************************************************************************
 *
 * Name: actbl.h - Table data structures defined in ACPI specification
 *
 *****************************************************************************/

/*
 * Copyright (C) 2000 - 2003, R. Byron Moore
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions, and the following disclaimer,
 *    without modification.
 * 2. Redistributions in binary form must reproduce at minimum a disclaimer
 *    substantially similar to the "NO WARRANTY" disclaimer below
 *    ("Disclaimer") and any redistribution must be conditioned upon
 *    including a substantially similar Disclaimer requirement for further
 *    binary redistribution.
 * 3. Neither the names of the above-listed copyright holders nor the names
 *    of any contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * Alternatively, this software may be distributed under the terms of the
 * GNU General Public License ("GPL") version 2 as published by the Free
 * Software Foundation.
 *
 * NO WARRANTY
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTIBILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * HOLDERS OR CONTRIBUTORS BE LIABLE FOR SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
 * IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGES.
 */

#ifndef __ACTBL_H__
#define __ACTBL_H__


/*
 *  Values for description table header signatures
 */
#define RSDP_NAME               "RSDP"
#define RSDP_SIG                "RSD PTR "  /* RSDT Pointer signature */
#define APIC_SIG                "APIC"      /* Multiple APIC Description Table */
#define DSDT_SIG                "DSDT"      /* Differentiated System Description Table */
#define FADT_SIG                "FACP"      /* Fixed ACPI Description Table */
#define FACS_SIG                "FACS"      /* Firmware ACPI Control Structure */
#define PSDT_SIG                "PSDT"      /* Persistent System Description Table */
#define RSDT_SIG                "RSDT"      /* Root System Description Table */
#define XSDT_SIG                "XSDT"      /* Extended  System Description Table */
#define SSDT_SIG                "SSDT"      /* Secondary System Description Table */
#define SBST_SIG                "SBST"      /* Smart Battery Specification Table */
#define SPIC_SIG                "SPIC"      /* IOSAPIC table */
#define BOOT_SIG                "BOOT"      /* Boot table */


#define GL_OWNED                0x02        /* Ownership of global lock is bit 1 */

/* values of Mapic.Model */

#define DUAL_PIC                0
#define MULTIPLE_APIC           1

/* values of Type in struct apic_header */

#define APIC_PROC               0
#define APIC_IO                 1


/*
 * Common table types.  The base code can remain
 * constant if the underlying tables are changed
 */
#define RSDT_DESCRIPTOR         struct rsdt_descriptor_rev2
#define XSDT_DESCRIPTOR         struct xsdt_descriptor_rev2
#define FACS_DESCRIPTOR         struct facs_descriptor_rev2
#define FADT_DESCRIPTOR         struct fadt_descriptor_rev2


#pragma pack(1)

/*
 * Architecture-independent tables
 * The architecture dependent tables are in separate files
 */
struct rsdp_descriptor         /* Root System Descriptor Pointer */
{
	char                            signature [8];          /* ACPI signature, contains "RSD PTR " */
	u8                              checksum;               /* To make sum of struct == 0 */
	char                            oem_id [6];             /* OEM identification */
	u8                              revision;               /* Must be 0 for 1.0, 2 for 2.0 */
	u32                             rsdt_physical_address;  /* 32-bit physical address of RSDT */
	u32                             length;                 /* XSDT Length in bytes including hdr */
	u64                             xsdt_physical_address;  /* 64-bit physical address of XSDT */
	u8                              extended_checksum;      /* Checksum of entire table */
	char                            reserved [3];           /* Reserved field must be 0 */
};


struct acpi_table_header         /* ACPI common table header */
{
	char                            signature [4];          /* ACPI signature (4 ASCII characters) */
	u32                             length;                 /* Length of table, in bytes, including header */
	u8                              revision;               /* ACPI Specification minor version # */
	u8                              checksum;               /* To make sum of entire table == 0 */
	char                            oem_id [6];             /* OEM identification */
	char                            oem_table_id [8];       /* OEM table identification */
	u32                             oem_revision;           /* OEM revision number */
	char                            asl_compiler_id [4];    /* ASL compiler vendor ID */
	u32                             asl_compiler_revision;  /* ASL compiler revision number */
};


struct acpi_common_facs          /* Common FACS for internal use */
{
	u32                             *global_lock;
	u64                             *firmware_waking_vector;
	u8                              vector_width;
};


struct apic_table
{
	struct acpi_table_header        header;                 /* ACPI table header */
	u32                             local_apic_address;     /* Physical address for accessing local APICs */
	u32                             PCATcompat      : 1;    /* a one indicates system also has dual 8259s */
	u32                             reserved1       : 31;
};


struct apic_header
{
	u8                              type;                   /* APIC type.  Either APIC_PROC or APIC_IO */
	u8                              length;                 /* Length of APIC structure */
};


struct processor_apic
{
	struct apic_header              header;
	u8                              processor_apic_id;      /* ACPI processor id */
	u8                              local_apic_id;          /* Processor's local APIC id */
	u32                             processor_enabled: 1;   /* Processor is usable if set */
	u32                             reserved1       : 31;
};


struct io_apic
{
	struct apic_header              header;
	u8                              io_apic_id;             /* I/O APIC ID */
	u8                              reserved;               /* Reserved - must be zero */
	u32                             io_apic_address;        /* APIC's physical address */
	u32                             vector;                 /* Interrupt vector index where INTI
			  * lines start */
};


/*
 *  IA64 TBD:  Add SAPIC Tables
 */

/*
 *  IA64 TBD:   Modify Smart Battery Description to comply with ACPI IA64
 *              extensions.
 */
struct smart_battery_description_table
{
	struct acpi_table_header        header;
	u32                             warning_level;
	u32                             low_level;
	u32                             critical_level;
};

struct hpet_description_table
{
	struct acpi_table_header        header;
	u32                             hardware_id;
	u32                             base_address[3];
	u8                              hpet_number;
	u16                             clock_tick;
	u8                              attributes;
};
#pragma pack()


/*
 * ACPI Table information.  We save the table address, length,
 * and type of memory allocation (mapped or allocated) for each
 * table for 1) when we exit, and 2) if a new table is installed
 */
#define ACPI_MEM_NOT_ALLOCATED  0
#define ACPI_MEM_ALLOCATED      1
#define ACPI_MEM_MAPPED         2

/* Definitions for the Flags bitfield member of struct acpi_table_support */

#define ACPI_TABLE_SINGLE       0x00
#define ACPI_TABLE_MULTIPLE     0x01
#define ACPI_TABLE_EXECUTABLE   0x02

#define ACPI_TABLE_ROOT         0x00
#define ACPI_TABLE_PRIMARY      0x10
#define ACPI_TABLE_SECONDARY    0x20
#define ACPI_TABLE_ALL          0x30
#define ACPI_TABLE_TYPE_MASK    0x30

/* Data about each known table type */

struct acpi_table_support
{
	char                            *name;
	char                            *signature;
	void                            **global_ptr;
	u8                              sig_length;
	u8                              flags;
};


/*
 * Get the architecture-specific tables
 */
#include "actbl1.h"   /* Acpi 1.0 table definitions */
#include "actbl2.h"   /* Acpi 2.0 table definitions */

#endif /* __ACTBL_H__ */
