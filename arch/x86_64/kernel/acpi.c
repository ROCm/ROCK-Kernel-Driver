/*
 *  acpi.c - Architecture-Specific Low-Level ACPI Support
 *
 *  Copyright (C) 2001, 2002 Paul Diefenbaugh <paul.s.diefenbaugh@intel.com>
 *  Copyright (C) 2001 Jun Nakajima <jun.nakajima@intel.com>
 *  Copyright (C) 2001 Patrick Mochel <mochel@osdl.org>
 *  Copyright (C) 2002 Andi Kleen, SuSE Labs (x86-64 port)
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
 */

#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/stddef.h>
#include <linux/slab.h>
#include <linux/pci.h>
#include <linux/bootmem.h>
#include <linux/irq.h>
#include <linux/acpi.h>
#include <asm/mpspec.h>
#include <asm/io.h>
#include <asm/apic.h>
#include <asm/apicdef.h>
#include <asm/page.h>
#include <asm/pgtable.h>
#include <asm/pgalloc.h>
#include <asm/io_apic.h>
#include <asm/proto.h>
#include <asm/desc.h>
#include <asm/system.h>
#include <asm/segment.h>

extern int acpi_disabled;

#define PREFIX			"ACPI: "


/* --------------------------------------------------------------------------
                              Boot-time Configuration
   -------------------------------------------------------------------------- */

#ifdef CONFIG_ACPI_BOOT

enum acpi_irq_model_id		acpi_irq_model;

/* rely on all ACPI tables being in the direct mapping */
char *
__acpi_map_table (
	unsigned long	phys_addr,
	unsigned long	size)
{
	if (!phys_addr || !size)
		return NULL;

	if (phys_addr < (end_pfn_map << PAGE_SHIFT)) 
		return __va(phys_addr); 

	return NULL; 
} 	      

#ifdef CONFIG_X86_LOCAL_APIC

int acpi_lapic;

static u64 acpi_lapic_addr __initdata = APIC_DEFAULT_PHYS_BASE;


static int __init
acpi_parse_madt (
	unsigned long		phys_addr,
	unsigned long		size)
{
	struct acpi_table_madt	*madt = NULL;

	if (!phys_addr || !size)
		return -EINVAL;

	madt = (struct acpi_table_madt *) __acpi_map_table(phys_addr, size);
	if (!madt) {
		printk(KERN_WARNING PREFIX "Unable to map MADT\n");
		return -ENODEV;
	}

	if (madt->lapic_address)
		acpi_lapic_addr = (u64) madt->lapic_address;

	printk(KERN_INFO PREFIX "Local APIC address 0x%08x\n",
		madt->lapic_address);

	return 0;
}


static int __init
acpi_parse_lapic (
	acpi_table_entry_header *header)
{
	struct acpi_table_lapic	*processor = NULL;

	processor = (struct acpi_table_lapic*) header;
	if (!processor)
		return -EINVAL;

	acpi_table_print_madt_entry(header);

	mp_register_lapic (
		processor->id,					   /* APIC ID */
		processor->flags.enabled);			  /* Enabled? */

	return 0;
}


static int __init
acpi_parse_lapic_addr_ovr (
	acpi_table_entry_header *header)
{
	struct acpi_table_lapic_addr_ovr *lapic_addr_ovr = NULL;

	lapic_addr_ovr = (struct acpi_table_lapic_addr_ovr*) header;
	if (!lapic_addr_ovr)
		return -EINVAL;

	acpi_lapic_addr = lapic_addr_ovr->address;

	return 0;
}


static int __init
acpi_parse_lapic_nmi (
	acpi_table_entry_header *header)
{
	struct acpi_table_lapic_nmi *lapic_nmi = NULL;

	lapic_nmi = (struct acpi_table_lapic_nmi*) header;
	if (!lapic_nmi)
		return -EINVAL;

	acpi_table_print_madt_entry(header);

	if (lapic_nmi->lint != 1)
		printk(KERN_WARNING PREFIX "NMI not connected to LINT 1!\n");

	return 0;
}

#endif /*CONFIG_X86_LOCAL_APIC*/

#ifdef CONFIG_X86_IO_APIC

int acpi_ioapic;

static int __init
acpi_parse_ioapic (
	acpi_table_entry_header *header)
{
	struct acpi_table_ioapic *ioapic = NULL;

	ioapic = (struct acpi_table_ioapic*) header;
	if (!ioapic)
		return -EINVAL;
 
	acpi_table_print_madt_entry(header);

	mp_register_ioapic (
		ioapic->id,
		ioapic->address,
		ioapic->global_irq_base);
 
	return 0;
}


static int __init
acpi_parse_int_src_ovr (
	acpi_table_entry_header *header)
{
	struct acpi_table_int_src_ovr *intsrc = NULL;

	intsrc = (struct acpi_table_int_src_ovr*) header;
	if (!intsrc)
		return -EINVAL;

	acpi_table_print_madt_entry(header);

	mp_override_legacy_irq (
		intsrc->bus_irq,
		intsrc->flags.polarity,
		intsrc->flags.trigger,
		intsrc->global_irq);

	return 0;
}


static int __init
acpi_parse_nmi_src (
	acpi_table_entry_header *header)
{
	struct acpi_table_nmi_src *nmi_src = NULL;

	nmi_src = (struct acpi_table_nmi_src*) header;
	if (!nmi_src)
		return -EINVAL;

	acpi_table_print_madt_entry(header);

	/* TBD: Support nimsrc entries? */

	return 0;
}

#endif /*CONFIG_X86_IO_APIC*/

#ifdef CONFIG_HPET_TIMER
static int __init
acpi_parse_hpet (
	unsigned long		phys_addr,
	unsigned long		size)
{
	struct acpi_table_hpet *hpet_tbl;

	hpet_tbl = __va(phys_addr);

	if (hpet_tbl->addr.space_id != ACPI_SPACE_MEM) {
		printk(KERN_WARNING "acpi: HPET timers must be located in memory.\n");
		return -1;
	}

	hpet.address = hpet_tbl->addr.addrl | ((long) hpet_tbl->addr.addrh << 32);

	printk(KERN_INFO "acpi: HPET id: %#x base: %#lx\n", hpet_tbl->id, hpet.address);

	return 0;
} 
#endif

static unsigned long __init
acpi_scan_rsdp (
	unsigned long		start,
	unsigned long		length)
{
	unsigned long		offset = 0;
	unsigned long		sig_len = sizeof("RSD PTR ") - 1;

	/*
	 * Scan all 16-byte boundaries of the physical memory region for the
	 * RSDP signature.
	 */
	for (offset = 0; offset < length; offset += 16) {
		if (strncmp((char *) (start + offset), "RSD PTR ", sig_len))
			continue;
		return (start + offset);
	}

	return 0;
}


unsigned long __init
acpi_find_rsdp (void)
{
	unsigned long		rsdp_phys = 0;

	/*
	 * Scan memory looking for the RSDP signature. First search EBDA (low
	 * memory) paragraphs and then search upper memory (E0000-FFFFF).
	 */
	rsdp_phys = acpi_scan_rsdp (0, 0x400);
	if (!rsdp_phys)
		rsdp_phys = acpi_scan_rsdp (0xE0000, 0xFFFFF);

	return rsdp_phys;
}


int __init
acpi_boot_init (void)
{
	int			result = 0;

	/*
	 * The default interrupt routing model is PIC (8259).  This gets
	 * overridden if IOAPICs are enumerated (below).
	 */
	acpi_irq_model = ACPI_IRQ_MODEL_PIC;

	/* 
	 * Initialize the ACPI boot-time table parser.
	 */
	result = acpi_table_init();
	if (result)
		return result;

	result = acpi_blacklisted();
	if (result) {
		acpi_disabled = 1;
		return result;
	} else
               printk(KERN_NOTICE PREFIX "BIOS passes blacklist\n");

	extern int disable_apic;
	if (disable_apic)
		return 0;

#ifdef CONFIG_X86_LOCAL_APIC

	/* 
	 * MADT
	 * ----
	 * Parse the Multiple APIC Description Table (MADT), if exists.
	 * Note that this table provides platform SMP configuration 
	 * information -- the successor to MPS tables.
	 */

	result = acpi_table_parse(ACPI_APIC, acpi_parse_madt);
	if (!result) {
		printk(KERN_WARNING PREFIX "MADT not present\n");
		return 0;
	}
	else if (result < 0) {
		printk(KERN_ERR PREFIX "Error parsing MADT\n");
		return result;
	}
	else if (result > 1) 
		printk(KERN_WARNING PREFIX "Multiple MADT tables exist\n");

	/* 
	 * Local APIC
	 * ----------
	 * Note that the LAPIC address is obtained from the MADT (32-bit value)
	 * and (optionally) overridden by a LAPIC_ADDR_OVR entry (64-bit value).
	 */

	result = acpi_table_parse_madt(ACPI_MADT_LAPIC_ADDR_OVR, acpi_parse_lapic_addr_ovr);
	if (result < 0) {
		printk(KERN_ERR PREFIX "Error parsing LAPIC address override entry\n");
		return result;
	}

	mp_register_lapic_address(acpi_lapic_addr);

	result = acpi_table_parse_madt(ACPI_MADT_LAPIC, acpi_parse_lapic);
	if (!result) { 
		printk(KERN_ERR PREFIX "No LAPIC entries present\n");
		/* TBD: Cleanup to allow fallback to MPS */
		return -ENODEV;
	}
	else if (result < 0) {
		printk(KERN_ERR PREFIX "Error parsing LAPIC entry\n");
		/* TBD: Cleanup to allow fallback to MPS */
		return result;
	}

	result = acpi_table_parse_madt(ACPI_MADT_LAPIC_NMI, acpi_parse_lapic_nmi);
	if (result < 0) {
		printk(KERN_ERR PREFIX "Error parsing LAPIC NMI entry\n");
		/* TBD: Cleanup to allow fallback to MPS */
		return result;
	}

	acpi_lapic = 1;

#endif /*CONFIG_X86_LOCAL_APIC*/

#ifdef CONFIG_X86_IO_APIC

	/* 
	 * I/O APIC 
	 * --------
	 */

	result = acpi_table_parse_madt(ACPI_MADT_IOAPIC, acpi_parse_ioapic);
	if (!result) { 
		printk(KERN_ERR PREFIX "No IOAPIC entries present\n");
		return -ENODEV;
	}
	else if (result < 0) {
		printk(KERN_ERR PREFIX "Error parsing IOAPIC entry\n");
		return result;
	}

	/* Build a default routing table for legacy (ISA) interrupts. */
	mp_config_acpi_legacy_irqs();

	result = acpi_table_parse_madt(ACPI_MADT_INT_SRC_OVR, acpi_parse_int_src_ovr);
	if (result < 0) {
		printk(KERN_ERR PREFIX "Error parsing interrupt source overrides entry\n");
		/* TBD: Cleanup to allow fallback to MPS */
		return result;
	}

	result = acpi_table_parse_madt(ACPI_MADT_NMI_SRC, acpi_parse_nmi_src);
	if (result < 0) {
		printk(KERN_ERR PREFIX "Error parsing NMI SRC entry\n");
		/* TBD: Cleanup to allow fallback to MPS */
		return result;
	}

	acpi_irq_model = ACPI_IRQ_MODEL_IOAPIC;

	acpi_ioapic = 1;

#endif /*CONFIG_X86_IO_APIC*/

#ifdef CONFIG_X86_LOCAL_APIC
	if (acpi_lapic && acpi_ioapic)
		smp_found_config = 1;
#endif

#ifdef CONFIG_HPET_TIMER
	result = acpi_table_parse(ACPI_HPET, acpi_parse_hpet);
	if (result < 0) 
		printk("ACPI: no HPET table found (%d).\n", result); 
#endif

	return 0;
}

#endif /*CONFIG_ACPI_BOOT*/


/* --------------------------------------------------------------------------
                              Low-Level Sleep Support
   -------------------------------------------------------------------------- */

#ifdef CONFIG_ACPI_SLEEP

extern void acpi_prepare_wakeup(void);
extern unsigned char acpi_wakeup[], acpi_wakeup_end[], s3_prot16[];

/* address in low memory of the wakeup routine. */
unsigned long acpi_wakeup_address;

/**
 * acpi_save_state_mem - save kernel state
 */
int acpi_save_state_mem (void)
{
	if (!acpi_wakeup_address)
		return -1;

	memcpy((void*)acpi_wakeup_address, acpi_wakeup, acpi_wakeup_end - acpi_wakeup); 
	return 0;
}

/**
 * acpi_save_state_disk - save kernel state to disk
 *
 * Assume preemption/interrupts are already turned off and that we're running
 * on the BP (note this doesn't imply SMP is handled correctly)
 */
int acpi_save_state_disk (void)
{
	unsigned long pbase = read_cr3() & PAGE_MASK; 
	if (pbase >= 0xffffffffUL) {
		printk(KERN_ERR "ACPI: High page table. Suspend disabled.\n");
	return 1;
	} 
	set_seg_base(smp_processor_id(), GDT_ENTRY_KERNELCS16, s3_prot16); 
	swap_low_mappings();
	acpi_prepare_wakeup();
	return 0;
}

/*
 * acpi_restore_state
 */
void acpi_restore_state_mem (void)
{
	swap_low_mappings();
}

/**
 * acpi_reserve_bootmem - do _very_ early ACPI initialisation
 *
 * We allocate a page in 1MB low memory for the real-mode wakeup
 * routine for when we come back from a sleep state. The
 * runtime allocator allows specification of <16M pages, but not
 * <1M pages.
 */
void __init acpi_reserve_bootmem(void)
{
	acpi_wakeup_address = (unsigned long)alloc_bootmem_low(PAGE_SIZE);
	if (!acpi_wakeup_address) { 
		printk(KERN_ERR "ACPI: Cannot allocate lowmem. S3 disabled.\n");
		return;
	} 
}

#endif /*CONFIG_ACPI_SLEEP*/

void acpi_pci_link_exit(void) {}
