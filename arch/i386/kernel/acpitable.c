/*
 *  acpitable.c - IA32-specific ACPI boot-time initialization (Revision: 1)
 *
 *  Copyright (C) 1999 Andrew Henroid
 *  Copyright (C) 2001 Richard Schaal
 *  Copyright (C) 2001 Paul Diefenbaugh <paul.s.diefenbaugh@intel.com>
 *  Copyright (C) 2001 Jun Nakajima <jun.nakajima@intel.com>
 *  Copyright (C) 2001 Arjan van de Ven <arjanv@redhat.com>
 *
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 *
 */
#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/stddef.h>
#include <linux/slab.h>
#include <linux/pci.h>
#include <asm/mpspec.h>
#include <asm/io.h>
#include <asm/apic.h>
#include <asm/apicdef.h>
#include <asm/page.h>
#include <asm/pgtable.h>

#define SELF_CONTAINED_ACPI

#ifdef SELF_CONTAINED_ACPI
/*
 * The following codes are cut&pasted from drivers/acpi. Part of the code
 * there can be not updated or delivered yet.
 * To avoid conflicts when CONFIG_ACPI is defined, the following codes are
 * modified so that they are self-contained in this file.
 * -- jun
 */
#define dprintk printk
typedef unsigned int ACPI_TBLPTR;

#define AE_CODE_ENVIRONMENTAL           0x0000
#define AE_OK				(u32) 0x0000
#define AE_ERROR			(u32) (0x0001 | AE_CODE_ENVIRONMENTAL)
#define AE_NO_ACPI_TABLES		(u32) (0x0002 | AE_CODE_ENVIRONMENTAL)
#define AE_NOT_FOUND                    (u32) (0x0005 | AE_CODE_ENVIRONMENTAL)

typedef struct {		/* ACPI common table header */
	char signature[4];	/* identifies type of table */
	u32 length;		/* length of table,
				   in bytes, * including header */
	u8 revision;		/* specification minor version # */
	u8 checksum;		/* to make sum of entire table == 0 */
	char oem_id[6];		/* OEM identification */
	char oem_table_id[8];	/* OEM table identification */
	u32 oem_revision;	/* OEM revision number */
	char asl_compiler_id[4];	/* ASL compiler vendor ID */
	u32 asl_compiler_revision;	/* ASL compiler revision number */
} acpi_table_header __attribute__ ((packed));;

enum {
	ACPI_APIC = 0,
	ACPI_BOOT,
	ACPI_DBGP,
	ACPI_DSDT,
	ACPI_ECDT,
	ACPI_ETDT,
	ACPI_FACP,
	ACPI_FACS,
	ACPI_OEMX,
	ACPI_PSDT,
	ACPI_SBST,
	ACPI_SLIT,
	ACPI_SPCR,
	ACPI_SRAT,
	ACPI_SSDT,
	ACPI_SPMI,
	ACPI_XSDT,
	ACPI_TABLE_COUNT
};

static char *acpi_table_signatures[ACPI_TABLE_COUNT] = {
	"APIC",
	"BOOT",
	"DBGP",
	"DSDT",
	"ECDT",
	"ETDT",
	"FACP",
	"FACS",
	"OEM",
	"PSDT",
	"SBST",
	"SLIT",
	"SPCR",
	"SRAT",
	"SSDT",
	"SPMI",
	"XSDT"
};

struct acpi_table_madt {
	acpi_table_header header;
	u32 lapic_address;
	struct {
		u32 pcat_compat:1;
		u32 reserved:31;
	} flags __attribute__ ((packed));
} __attribute__ ((packed));;

enum {
	ACPI_MADT_LAPIC = 0,
	ACPI_MADT_IOAPIC,
	ACPI_MADT_INT_SRC_OVR,
	ACPI_MADT_NMI_SRC,
	ACPI_MADT_LAPIC_NMI,
	ACPI_MADT_LAPIC_ADDR_OVR,
	ACPI_MADT_IOSAPIC,
	ACPI_MADT_LSAPIC,
	ACPI_MADT_PLAT_INT_SRC,
	ACPI_MADT_ENTRY_COUNT
};

#define RSDP_SIG			"RSD PTR "
#define RSDT_SIG 			"RSDT"

#define ACPI_DEBUG_PRINT(pl)

#define ACPI_MEMORY_MODE                0x01
#define ACPI_LOGICAL_ADDRESSING         0x00
#define ACPI_PHYSICAL_ADDRESSING        0x01

#define LO_RSDP_WINDOW_BASE         	0	/* Physical Address */
#define HI_RSDP_WINDOW_BASE         	0xE0000	/* Physical Address */
#define LO_RSDP_WINDOW_SIZE         	0x400
#define HI_RSDP_WINDOW_SIZE         	0x20000
#define RSDP_SCAN_STEP			16
#define RSDP_CHECKSUM_LENGTH		20

typedef int (*acpi_table_handler) (acpi_table_header * header, unsigned long);

static acpi_table_handler acpi_boot_ops[ACPI_TABLE_COUNT];

struct acpi_table_rsdp {
	char signature[8];
	u8 checksum;
	char oem_id[6];
	u8 revision;
	u32 rsdt_address;
} __attribute__ ((packed));

struct acpi_table_rsdt {
	acpi_table_header header;
	u32 entry[ACPI_TABLE_COUNT];
} __attribute__ ((packed));

typedef struct {
	u8 type;
	u8 length;
} acpi_madt_entry_header __attribute__ ((packed));

typedef struct {
	u16 polarity:2;
	u16 trigger:2;
	u16 reserved:12;
} acpi_madt_int_flags __attribute__ ((packed));

struct acpi_table_lapic {
	acpi_madt_entry_header header;
	u8 acpi_id;
	u8 id;
	struct {
		u32 enabled:1;
		u32 reserved:31;
	} flags __attribute__ ((packed));
} __attribute__ ((packed));

struct acpi_table_ioapic {
	acpi_madt_entry_header header;
	u8 id;
	u8 reserved;
	u32 address;
	u32 global_irq_base;
} __attribute__ ((packed));

struct acpi_table_int_src_ovr {
	acpi_madt_entry_header header;
	u8 bus;
	u8 bus_irq;
	u32 global_irq;
	acpi_madt_int_flags flags;
} __attribute__ ((packed));

struct acpi_table_nmi_src {
	acpi_madt_entry_header header;
	acpi_madt_int_flags flags;
	u32 global_irq;
} __attribute__ ((packed));

struct acpi_table_lapic_nmi {
	acpi_madt_entry_header header;
	u8 acpi_id;
	acpi_madt_int_flags flags;
	u8 lint;
} __attribute__ ((packed));

struct acpi_table_lapic_addr_ovr {
	acpi_madt_entry_header header;
	u8 reserved[2];
	u64 address;
} __attribute__ ((packed));

struct acpi_table_iosapic {
	acpi_madt_entry_header header;
	u8 id;
	u8 reserved;
	u32 global_irq_base;
	u64 address;
} __attribute__ ((packed));

struct acpi_table_lsapic {
	acpi_madt_entry_header header;
	u8 acpi_id;
	u8 id;
	u8 eid;
	u8 reserved[3];
	struct {
		u32 enabled:1;
		u32 reserved:31;
	} flags;
} __attribute__ ((packed));

struct acpi_table_plat_int_src {
	acpi_madt_entry_header header;
	acpi_madt_int_flags flags;
	u8 type;
	u8 id;
	u8 eid;
	u8 iosapic_vector;
	u32 global_irq;
	u32 reserved;
} __attribute__ ((packed));

/*
 * ACPI Table Descriptor.  One per ACPI table
 */
typedef struct acpi_table_desc {
	struct acpi_table_desc *prev;
	struct acpi_table_desc *next;
	struct acpi_table_desc *installed_desc;
	acpi_table_header *pointer;
	void *base_pointer;
	u8 *aml_pointer;
	u64 physical_address;
	u32 aml_length;
	u32 length;
	u32 count;
	u16 table_id;
	u8 type;
	u8 allocation;
	u8 loaded_into_namespace;

} acpi_table_desc __attribute__ ((packed));;

static unsigned char __init
acpi_tb_checksum(void *buffer, int length)
{
	int i;
	unsigned char *bytebuffer;
	unsigned char sum = 0;

	if (!buffer || length <= 0)
		return 0;

	bytebuffer = (unsigned char *) buffer;

	for (i = 0; i < length; i++)
		sum += *(bytebuffer++);

	return sum;
}

static int __init
acpi_table_checksum(acpi_table_header * header)
{
	u8 *p = (u8 *) header;
	int length = 0;
	int sum = 0;

	if (!header)
		return -EINVAL;

	length = header->length;

	while (length--)
		sum += *p++;

	return sum & 0xFF;
}

static void __init
acpi_print_table_header(acpi_table_header * header)
{
	if (!header)
		return;

	printk(KERN_INFO "ACPI table found: %.4s v%d [%.6s %.8s %d.%d]\n",
	       header->signature, header->revision, header->oem_id,
	       header->oem_table_id, header->oem_revision >> 16,
	       header->oem_revision & 0xffff);

	return;
}

/*******************************************************************************
 *
 * FUNCTION:    Acpi_tb_scan_memory_for_rsdp
 *
 * PARAMETERS:  Start_address       - Starting pointer for search
 *              Length              - Maximum length to search
 *
 * RETURN:      Pointer to the RSDP if found, otherwise NULL.
 *
 * DESCRIPTION: Search a block of memory for the RSDP signature
 *
 ******************************************************************************/

static unsigned char *__init
acpi_tb_scan_memory_for_rsdp(unsigned char *address, int length)
{
	u32 offset;

	if (length <= 0)
		return NULL;

	/* Search from given start addr for the requested length  */

	offset = 0;

	while (offset < length) {
		/* The signature must match and the checksum must be correct */
		if (strncmp(address, RSDP_SIG, sizeof(RSDP_SIG) - 1) == 0 &&
		    acpi_tb_checksum(address, RSDP_CHECKSUM_LENGTH) == 0) {
			/* If so, we have found the RSDP */
			printk(KERN_INFO
			       "ACPI: RSDP located at physical address %p\n",
			       address);
			return address;
		}
		offset += RSDP_SCAN_STEP;
		address += RSDP_SCAN_STEP;
	}

	/* Searched entire block, no RSDP was found */
	printk(KERN_INFO "ACPI: Searched entire block, no RSDP was found.\n");
	return NULL;
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_tb_find_rsdp
 *
 * PARAMETERS:  *Table_info             - Where the table info is returned
 *              Flags                   - Current memory mode (logical vs.
 *                                        physical addressing)
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Search lower 1_mbyte of memory for the root system descriptor
 *              pointer structure.  If it is found, set *RSDP to point to it.
 *
 *              NOTE: The RSDP must be either in the first 1_k of the Extended
 *              BIOS Data Area or between E0000 and FFFFF (ACPI 1.0 section
 *              5.2.2; assertion #421).
 *
 ******************************************************************************/

static int __init
acpi_tb_find_rsdp(acpi_table_desc * table_info, u32 flags)
{
	unsigned char *address;

	/*
	 * Physical address is given.
	 */
	/*
	 * Region 1) Search EBDA (low memory) paragraphs
	 */
	address =
	    acpi_tb_scan_memory_for_rsdp(__va(LO_RSDP_WINDOW_BASE),
					 LO_RSDP_WINDOW_SIZE);

	if (address) {
		/* Found it, return the physical address */
		table_info->physical_address = (ACPI_TBLPTR) __pa(address);
		return AE_OK;
	}

	/*
	 * Region 2) Search upper memory: 16-byte boundaries in E0000h-F0000h
	 */
	address = acpi_tb_scan_memory_for_rsdp(__va(HI_RSDP_WINDOW_BASE),
					       HI_RSDP_WINDOW_SIZE);
	if (address) {
		/* Found it, return the physical address */
		table_info->physical_address = (ACPI_TBLPTR) __pa(address);
		return AE_OK;
	}

	/* RSDP signature was not found */
	return AE_NOT_FOUND;
}

static unsigned long __init
acpi_find_root_pointer(u32 flags)
{
	acpi_table_desc table_info;
	int status;

	/* Get the RSDP */

	status = acpi_tb_find_rsdp(&table_info, flags);
	if (status)
		return 0;

	return table_info.physical_address;
}

static unsigned long __init
acpi_os_get_root_pointer(u32 flags)
{
	unsigned long address;

#ifndef CONFIG_ACPI_EFI

	address = acpi_find_root_pointer(flags);

#else
	if (efi.acpi20)
		address = (unsigned long) efi.acpi20;
	else if (efi.acpi)
		address = (unsigned long) efi.acpi;
	else
		address = 0;
#endif				/*CONFIG_ACPI_EFI */

	if (address == 0)
		printk(KERN_ERR "ACPI: System description tables not found\n");

	return address;
}

/*
 * Temporarily use the virtual area starting from FIX_IO_APIC_BASE_0,
 * to map the target physical address. The problem is that set_fixmap()
 * provides a single page, and it is possible that the page is not
 * sufficient.
 * By using this area, we can map up to MAX_IO_APICS pages temporarily,
 * i.e. until the next __va_range() call.
 */
static __inline__ char *
__va_range(unsigned long phys, unsigned long size)
{
	unsigned long base, offset, mapped_size, mapped_phys = phys;
	int idx = FIX_IO_APIC_BASE_0;

	offset = phys & (PAGE_SIZE - 1);
	mapped_size = PAGE_SIZE - offset;
	set_fixmap(idx, mapped_phys);
	base = fix_to_virt(FIX_IO_APIC_BASE_0);

	/*
	 * Most cases can be covered by the below.
	 */
	if (mapped_size >= size)
		return ((unsigned char *) base + offset);

	dprintk("__va_range: mapping more than a single page, size = 0x%lx\n",
		size);

	do {
		if (idx++ == FIX_IO_APIC_BASE_END)
			return 0;	/* cannot handle this */
		mapped_phys = mapped_phys + PAGE_SIZE;
		set_fixmap(idx, mapped_phys);
		mapped_size = mapped_size + PAGE_SIZE;
	} while (mapped_size < size);

	return ((unsigned char *) base + offset);
}

static int __init acpi_tables_init(void)
{
	int result = -ENODEV;
	int status = AE_OK;
	unsigned long rsdp_addr = 0;
	acpi_table_header *header = NULL;
	struct acpi_table_rsdp *rsdp = NULL;
#ifndef CONFIG_IA64
	struct acpi_table_rsdt *rsdt = NULL;
	struct acpi_table_rsdt saved_rsdt;
#else
	struct acpi071_table_rsdt *rsdt = NULL;
#endif
	int tables = 0;
	int type = 0;
	int i = 0;

	rsdp_addr = acpi_os_get_root_pointer(ACPI_PHYSICAL_ADDRESSING);

	if (!rsdp_addr)
		return -ENODEV;

	rsdp = (struct acpi_table_rsdp *) rsdp_addr;

	printk(KERN_INFO "%.8s v%d [%.6s]\n", rsdp->signature, rsdp->revision,
	       rsdp->oem_id);
	if (strncmp(rsdp->signature, RSDP_SIG,strlen(RSDP_SIG))) {
		printk(KERN_WARNING "RSDP table signature incorrect\n");
		return -EINVAL;
	}

	rsdt = (struct acpi_table_rsdt *)
	    __va_range(rsdp->rsdt_address, sizeof(struct acpi_table_rsdt));

	if (rsdt) {
		header = (acpi_table_header *) & rsdt->header;
		acpi_print_table_header(header);
		if (strncmp(header->signature, RSDT_SIG, strlen(RSDT_SIG))) {
			printk(KERN_WARNING "ACPI: RSDT signature incorrect\n");
			rsdt = NULL;
		} else {
			/* 
			 * The number of tables is computed by taking the 
			 * size of all entries (header size minus total 
			 * size of RSDT) divided by the size of each entry
			 * (4-byte table pointers).
			 */
			tables =
			    (header->length - sizeof(acpi_table_header)) / 4;
		}
	}

	if (!rsdt) {
		printk(KERN_WARNING
		       "ACPI: Invalid root system description tables (RSDT)\n");
		return -ENODEV;
	}

	memcpy(&saved_rsdt, rsdt, sizeof(saved_rsdt));

	if (saved_rsdt.header.length > sizeof(saved_rsdt)) {
		printk(KERN_WARNING "ACPI: Too big length in RSDT: %d\n",
		       saved_rsdt.header.length);
		return -ENODEV;
	}

	for (i = 0; i < tables; i++) {

		if (rsdt) {
			header = (acpi_table_header *)
			    __va_range(saved_rsdt.entry[i],
				       sizeof(acpi_table_header));
		}

		if (!header)
			break;

		acpi_print_table_header(header);

		for (type = 0; type < ACPI_TABLE_COUNT; type++)
			if (!strncmp
			    ((char *) &header->signature,
			     acpi_table_signatures[type],strlen(acpi_table_signatures[type])))
				break;

		if (type >= ACPI_TABLE_COUNT) {
			printk(KERN_WARNING "ACPI: Unsupported table %.4s\n",
			       header->signature);
			continue;
		}

		if (acpi_table_checksum(header)) {
			printk(KERN_WARNING "ACPI %s has invalid checksum\n",
			       acpi_table_signatures[i]);
			continue;
		}

		if (acpi_boot_ops && acpi_boot_ops[type])
			result =
			    acpi_boot_ops[type] (header,
						 (unsigned long) saved_rsdt.
						 entry[i]);
	}

	return result;
}
#endif				/* SELF_CONTAINED_ACPI */

static int total_cpus __initdata = 0;
int have_acpi_tables;

extern void __init MP_processor_info(struct mpc_config_processor *);

static void __init
acpi_parse_lapic(struct acpi_table_lapic *local_apic)
{
	struct mpc_config_processor proc_entry;
	int ix = 0;

	if (!local_apic)
		return;

	dprintk(KERN_INFO "LAPIC (acpi_id[0x%04x] id[0x%x] enabled[%d])\n",
		local_apic->acpi_id, local_apic->id, local_apic->flags.enabled);

	dprintk("CPU %d (0x%02x00)", total_cpus, local_apic->id);

	if (local_apic->flags.enabled) {
		printk(" enabled");
		ix = local_apic->id;
		if (ix >= MAX_APICS) {
			printk(KERN_WARNING
			       "Processor #%d INVALID - (Max ID: %d).\n", ix,
			       MAX_APICS);
			return;
		}
		/* 
		 * Fill in the info we want to save.  Not concerned about 
		 * the processor ID.  Processor features aren't present in 
		 * the table.
		 */
		proc_entry.mpc_type = MP_PROCESSOR;
		proc_entry.mpc_apicid = local_apic->id;
		proc_entry.mpc_cpuflag = CPU_ENABLED;
		if (proc_entry.mpc_apicid == boot_cpu_physical_apicid) {
			printk(" (BSP)");
			proc_entry.mpc_cpuflag |= CPU_BOOTPROCESSOR;
		}
		proc_entry.mpc_cpufeature =
		    (boot_cpu_data.x86 << 8) | (boot_cpu_data.
						x86_model << 4) | boot_cpu_data.
		    x86_mask;
		proc_entry.mpc_featureflag = boot_cpu_data.x86_capability[0];
		proc_entry.mpc_reserved[0] = 0;
		proc_entry.mpc_reserved[1] = 0;
		proc_entry.mpc_apicver = 0x10;	/* integrated APIC */
		MP_processor_info(&proc_entry);
	} else {
		printk(" disabled");
	}
	printk("\n");

	total_cpus++;
	return;
}

static void __init
acpi_parse_ioapic(struct acpi_table_ioapic *ioapic)
{

	if (!ioapic)
		return;

	printk(KERN_INFO
	       "IOAPIC (id[0x%x] address[0x%x] global_irq_base[0x%x])\n",
	       ioapic->id, ioapic->address, ioapic->global_irq_base);

	if (nr_ioapics >= MAX_IO_APICS) {
		printk(KERN_WARNING
		       "Max # of I/O APICs (%d) exceeded (found %d).\n",
		       MAX_IO_APICS, nr_ioapics);
		panic("Recompile kernel with bigger MAX_IO_APICS!\n");
	}
}

static void __init
acpi_parse_int_src_ovr(struct acpi_table_int_src_ovr *intsrc)
{
	/*
	   static int first_time_switch = 0;
	   struct mpc_config_intsrc my_intsrc;
	   int i;
	 */
	if (!intsrc)
		return;

	printk(KERN_INFO
	       "INT_SRC_OVR (bus[%d] irq[0x%x] global_irq[0x%x] polarity[0x%x] trigger[0x%x])\n",
	       intsrc->bus, intsrc->bus_irq, intsrc->global_irq,
	       intsrc->flags.polarity, intsrc->flags.trigger);
}

/*
 * At this point, we look at the interrupt assignment entries in the MPS
 * table.
 */ 
 
static void __init acpi_parse_nmi_src(struct acpi_table_nmi_src *nmisrc)
{
	if (!nmisrc)
		return;

	printk(KERN_INFO
	       "NMI_SRC (polarity[0x%x] trigger[0x%x] global_irq[0x%x])\n",
	       nmisrc->flags.polarity, nmisrc->flags.trigger,
	       nmisrc->global_irq);

}
static void __init
acpi_parse_lapic_nmi(struct acpi_table_lapic_nmi *localnmi)
{
	if (!localnmi)
		return;

	printk(KERN_INFO
	       "LAPIC_NMI (acpi_id[0x%04x] polarity[0x%x] trigger[0x%x] lint[0x%x])\n",
	       localnmi->acpi_id, localnmi->flags.polarity,
	       localnmi->flags.trigger, localnmi->lint);
}
static void __init
acpi_parse_lapic_addr_ovr(struct acpi_table_lapic_addr_ovr *lapic_addr_ovr)
{
	if (!lapic_addr_ovr)
		return;

	printk(KERN_INFO "LAPIC_ADDR_OVR (address[0x%lx])\n",
	       (unsigned long) lapic_addr_ovr->address);

}

#ifdef CONFIG_IA64
static void __init
acpi_parse_iosapic(struct acpi_table_iosapic *iosapic)
{
	if (!iosapic)
		return;

	printk(KERN_INFO "IOSAPIC (id[%x] global_irq_base[%x] address[%lx])\n",
	       iosapic->id, iosapic->global_irq_base,
	       (unsigned long) iosapic->address);

	return 0;
}
static void __init
acpi_parse_lsapic(struct acpi_table_lsapic *lsapic)
{
	if (!lsapic)
		return;

	printk(KERN_INFO
	       "LSAPIC (acpi_id[0x%04x] id[0x%x] eid[0x%x] enabled[%d])\n",
	       lsapic->acpi_id, lsapic->id, lsapic->eid, lsapic->flags.enabled);

	if (!lsapic->flags.enabled)
		return;
}
#endif
static void __init
acpi_parse_plat_int_src(struct acpi_table_plat_int_src *plintsrc)
{
	if (!plintsrc)
		return;

	printk(KERN_INFO
	       "PLAT_INT_SRC (polarity[0x%x] trigger[0x%x] type[0x%x] id[0x%04x] eid[0x%x] iosapic_vector[0x%x] global_irq[0x%x]\n",
	       plintsrc->flags.polarity, plintsrc->flags.trigger,
	       plintsrc->type, plintsrc->id, plintsrc->eid,
	       plintsrc->iosapic_vector, plintsrc->global_irq);
}
static int __init
acpi_parse_madt(acpi_table_header * header, unsigned long phys)
{

	struct acpi_table_madt *madt =
	    (struct acpi_table_madt *) __va_range(phys, header->length);
	acpi_madt_entry_header *entry_header = NULL;
	int table_size = 0;

	if (!madt)
		return -EINVAL;

	table_size = (int) (header->length - sizeof(*madt));
	entry_header =
	    (acpi_madt_entry_header *) ((void *) madt + sizeof(*madt));

	while (entry_header && (table_size > 0)) {
		switch (entry_header->type) {
		case ACPI_MADT_LAPIC:
			acpi_parse_lapic((struct acpi_table_lapic *)
					 entry_header);
			break;
		case ACPI_MADT_IOAPIC:
			acpi_parse_ioapic((struct acpi_table_ioapic *)
					  entry_header);
			break;
		case ACPI_MADT_INT_SRC_OVR:
			acpi_parse_int_src_ovr((struct acpi_table_int_src_ovr *)
					       entry_header);
			break;
		case ACPI_MADT_NMI_SRC:
			acpi_parse_nmi_src((struct acpi_table_nmi_src *)
					   entry_header);
			break;
		case ACPI_MADT_LAPIC_NMI:
			acpi_parse_lapic_nmi((struct acpi_table_lapic_nmi *)
					     entry_header);
			break;
		case ACPI_MADT_LAPIC_ADDR_OVR:
			acpi_parse_lapic_addr_ovr((struct
						   acpi_table_lapic_addr_ovr *)
						  entry_header);
			break;
#ifdef CONFIG_IA64
		case ACPI_MADT_IOSAPIC:
			acpi_parse_iosapic((struct acpi_table_iosapic *)
					   entry_header);
			break;
		case ACPI_MADT_LSAPIC:
			acpi_parse_lsapic((struct acpi_table_lsapic *)
					  entry_header);
			break;
#endif
		case ACPI_MADT_PLAT_INT_SRC:
			acpi_parse_plat_int_src((struct acpi_table_plat_int_src
						 *) entry_header);
			break;
		default:
			printk(KERN_WARNING
			       "Unsupported MADT entry type 0x%x\n",
			       entry_header->type);
			break;
		}
		table_size -= entry_header->length;
		entry_header =
		    (acpi_madt_entry_header *) ((void *) entry_header +
						entry_header->length);
	}

	if (!total_cpus) {
		printk("ACPI: No Processors found in the APCI table.\n");
		return -EINVAL;
	}

	printk(KERN_INFO "%d CPUs total\n", total_cpus);

	if (madt->lapic_address)
		mp_lapic_addr = madt->lapic_address;
	else
		mp_lapic_addr = APIC_DEFAULT_PHYS_BASE;

	printk(KERN_INFO "Local APIC address %x\n", madt->lapic_address);

	return 0;
}

extern int enable_acpi_smp_table;

/*
 * Configure the processor info using MADT in the ACPI tables. If we fail to
 * configure that, then we use the MPS tables.
 */
void __init
config_acpi_tables(void)
{
	int result = 0;

	/*
	 * Only do this when requested, either because of CPU/Bios type or from the command line
	 */
	if (!enable_acpi_smp_table) {
		return;
	}

	memset(&acpi_boot_ops, 0, sizeof(acpi_boot_ops));
	acpi_boot_ops[ACPI_APIC] = acpi_parse_madt;
	result = acpi_tables_init();

	if (!result) {
		have_acpi_tables = 1;
		printk("Enabling the CPU's according to the ACPI table\n");
	}
}
