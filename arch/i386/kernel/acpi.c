/*
 *  acpi.c - Architecture-Specific Low-Level ACPI Support
 *
 *  Copyright (C) 2001 Paul Diefenbaugh <paul.s.diefenbaugh@intel.com>
 *  Copyright (C) 2001 Jun Nakajima <jun.nakajima@intel.com>
 *  Copyright (C) 2001 Patrick Mochel <mochel@osdl.org>
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
#include <linux/slab.h>
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
#include <asm/tlbflush.h>
#define ACPI_C
#include <asm/suspend.h>


#define PREFIX			"ACPI: "

extern struct acpi_boot_flags	acpi_boot;

int				acpi_mp_config = 0;


/* --------------------------------------------------------------------------
                              Boot-time Configuration
   -------------------------------------------------------------------------- */

#ifdef CONFIG_ACPI_BOOT

/*
 * Use reserved fixmap pages for physical-to-virtual mappings of ACPI tables.
 * Note that the same range is used for each table, so tables that need to
 * persist should be memcpy'd.
 */
char *
__acpi_map_table (
	unsigned long	phys_addr,
	unsigned long	size)
{
	unsigned long	base = 0;
	unsigned long	mapped_phys = phys_addr;
	unsigned long	offset = phys_addr & (PAGE_SIZE - 1);
	unsigned long	mapped_size = PAGE_SIZE - offset;
	unsigned long	avail_size = mapped_size + (PAGE_SIZE * FIX_ACPI_PAGES);
	int		idx = FIX_ACPI_BEGIN;

	if (!phys_addr || !size)
		return NULL;

	base = fix_to_virt(FIX_ACPI_BEGIN);

	set_fixmap(idx, mapped_phys);

	if (size > avail_size)
		return NULL;

	/* If the table doesn't map completely into the fist page... */
	if (size > mapped_size) {
		do {
			/* Make sure we don't go past our range */
			if (idx++ == FIX_ACPI_END)
				return NULL;
			mapped_phys = mapped_phys + PAGE_SIZE;
			set_fixmap(idx, mapped_phys);
			mapped_size = mapped_size + PAGE_SIZE;
		} while (mapped_size < size);
	}

	return ((unsigned char *) base + offset);
}


#ifdef CONFIG_X86_LOCAL_APIC

static int total_cpus __initdata = 0;

/* From mpparse.c */
extern void __init MP_processor_info(struct mpc_config_processor *);
extern void __init MP_ioapic_info (struct mpc_config_ioapic *);
extern void __init MP_lintsrc_info(struct mpc_config_lintsrc *);

int __init
acpi_parse_lapic (
	acpi_table_entry_header *header)
{
	struct acpi_table_lapic	*cpu = NULL;
	struct mpc_config_processor processor;

	cpu = (struct acpi_table_lapic*) header;
	if (!cpu)
		return -EINVAL;

	acpi_table_print_madt_entry(header);

	if (!cpu->flags.enabled) {
		printk(KERN_INFO "Processor #%d disabled\n", cpu->id);
		return 0;
	}

	if (cpu->id >= MAX_APICS) {
		printk(KERN_WARNING "Processor #%d invalid (max %d)\n",
			cpu->id, MAX_APICS);
		return -ENODEV;
	}

	/*
	 * Fill in the info we want to save.  Not concerned about
	 * the processor ID.  Processor features aren't present in
	 * the table.
	 */
	processor.mpc_type = MP_PROCESSOR;
	processor.mpc_apicid = cpu->id;
	processor.mpc_cpuflag = CPU_ENABLED;
	if (cpu->id == boot_cpu_physical_apicid) {
		/* TBD: Circular reference trying to establish BSP */
		processor.mpc_cpuflag |= CPU_BOOTPROCESSOR;
	}
	processor.mpc_cpufeature = (boot_cpu_data.x86 << 8)
		| (boot_cpu_data.x86_model << 4) | boot_cpu_data.x86_mask;
	processor.mpc_featureflag = boot_cpu_data.x86_capability[0];
	processor.mpc_reserved[0] = 0;
	processor.mpc_reserved[1] = 0;
	processor.mpc_apicver = 0x10;	/* Integrated APIC */

	MP_processor_info(&processor);

	total_cpus++;

	return 0;
}


int __init
acpi_parse_lapic_addr_ovr (
	acpi_table_entry_header *header)
{
	struct acpi_table_lapic_addr_ovr *lapic_addr_ovr = NULL;

	lapic_addr_ovr = (struct acpi_table_lapic_addr_ovr*) header;
	if (!lapic_addr_ovr)
		return -EINVAL;

	/* TBD: Support local APIC address override entries */

	return 0;
}


int __init
acpi_parse_lapic_nmi (
	acpi_table_entry_header *header)
{
	struct acpi_table_lapic_nmi *lacpi_nmi = NULL;

	lacpi_nmi = (struct acpi_table_lapic_nmi*) header;
	if (!lacpi_nmi)
		return -EINVAL;

	acpi_table_print_madt_entry(header);

	/* TBD: Support lapic_nmi entries */

	return 0;
}

#endif /*CONFIG_X86_LOCAL_APIC*/


#ifdef CONFIG_X86_IO_APIC

int __init
acpi_parse_ioapic (
	acpi_table_entry_header *header)
{
	struct acpi_table_ioapic *ioapic = NULL;
	/*
	struct mpc_config_ioapic mp_ioapic;
	struct IO_APIC_reg_01	reg_01;
	*/

	ioapic = (struct acpi_table_ioapic*) header;
	if (!ioapic)
		return -EINVAL;
 
	acpi_table_print_madt_entry(header);
 
	/*
	 * Cobble up an entry for the IOAPIC (just as we do for LAPIC entries).
	 * Note that we aren't doing anything with ioapic->vector, and 
	 * mpc_apicver gets read directly from ioapic.
	 */

	/*
	 * TBD: Complete I/O APIC support.
	 *
	mp_ioapic.mpc_type = MP_IOAPIC;
	mp_ioapic.mpc_apicid = ioapic->id;
	mp_ioapic.mpc_flags = MPC_APIC_USABLE;
	mp_ioapic.mpc_apicaddr = ioapic->address;
	set_fixmap_nocache(nr_ioapics + FIX_IO_APIC_BASE_0,
		mp_ioapic.mpc_apicaddr);

	printk("mapped IOAPIC to %08lx (%08lx)\n",
		__fix_to_virt(nr_ioapics), mp_ioapic.mpc_apicaddr);

	*(int *)&reg_01 = io_apic_read(nr_ioapics, 1);
	mp_ioapic.mpc_apicver = reg_01.version;
	MP_ioapic_info(&mp_ioapic);
	 */

	return 0;
}


int __init
acpi_parse_int_src_ovr (
	acpi_table_entry_header *header)
{
	struct acpi_table_int_src_ovr *int_src_ovr = NULL;
	/*
	struct mpc_config_intsrc my_intsrc;
	int			i = 0;
	*/

	int_src_ovr = (struct acpi_table_int_src_ovr*) header;
	if (!int_src_ovr)
		return -EINVAL;

	acpi_table_print_madt_entry(header);

	/*
	 * TBD: Complete I/O APIC support.
	 *
	my_intsrc.mpc_type = MP_INTSRC;
	my_intsrc.mpc_irqtype = mp_INT;
	my_intsrc.mpc_irqflag = *(unsigned short*)(&(int_src_ovr->flags));
	my_intsrc.mpc_srcbus = int_src_ovr->bus;
	my_intsrc.mpc_srcbusirq = int_src_ovr->bus_irq;
	my_intsrc.mpc_dstapic = 0;
	my_intsrc.mpc_dstirq = int_src_ovr->global_irq;

	for (i = 0; i < mp_irq_entries; i++) {
		if (mp_irqs[i].mpc_srcbusirq == my_intsrc.mpc_srcbusirq) {
			mp_irqs[i] = my_intsrc;
			break;
		}
	}
	 */

	return 0;
}


int __init
acpi_parse_nmi_src (
	acpi_table_entry_header *header)
{
	struct acpi_table_nmi_src *nmi_src = NULL;

	nmi_src = (struct acpi_table_nmi_src*) header;
	if (!nmi_src)
		return -EINVAL;

	acpi_table_print_madt_entry(header);

	/* TBD: Support nimsrc entries */

	return 0;
}


#endif /*CONFIG_X86_IO_APIC*/


int __init
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

#ifdef CONFIG_X86_LOCAL_APIC
	if (madt->lapic_address)
		mp_lapic_addr = madt->lapic_address;
	else
		mp_lapic_addr = APIC_DEFAULT_PHYS_BASE;
#endif /*CONFIG_X86_LOCAL_APIC*/

	printk(KERN_INFO PREFIX "Local APIC address 0x%08x\n",
		madt->lapic_address);

	return 0;
}


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
		if (0 != strncmp((char *) (start + offset), "RSD PTR ", sig_len))
			continue;
		return (start + offset);
	}

	return 0;
}


int __init
acpi_find_rsdp (
	unsigned long		*rsdp_phys)
{
	if (!rsdp_phys)
		return -EINVAL;

	/*
	 * Scan memory looking for the RSDP signature. First search EBDA (low
	 * memory) paragraphs and then search upper memory (E0000-FFFFF).
	 */
	(*rsdp_phys) = acpi_scan_rsdp (0, 0x400);
	if (!(*rsdp_phys))
		(*rsdp_phys) = acpi_scan_rsdp (0xE0000, 0xFFFFF);

	if (!(*rsdp_phys))
		return -ENODEV;

	return 0;
}


int __init
acpi_boot_init (
	char			*cmdline)
{
	int			result = 0;

	/* Initialize the ACPI boot-time table parser */
	result = acpi_table_init(cmdline);
	if (0 != result)
		return result;

#ifdef CONFIG_X86_LOCAL_APIC
#ifdef CONFIG_X86_IO_APIC

	/* 
	 * MADT
	 * ----
	 * Parse the Multiple APIC Description Table (MADT), if exists.
	 * Note that this table provides platform SMP configuration 
	 * information -- the successor to MPS tables.
	 */

	if (!acpi_boot.madt) {
		printk(KERN_INFO PREFIX "MADT parsing disabled via command-line\n");
		return 0;
	}

	result = acpi_table_parse(ACPI_APIC, acpi_parse_madt);
	if (0 == result) {
		printk(KERN_WARNING PREFIX "MADT not present\n");
		return 0;
	}
	else if (0 > result) {
		printk(KERN_ERR PREFIX "Error parsing MADT\n");
		return result;
	}
	else if (1 < result) 
		printk(KERN_WARNING PREFIX "Multiple MADT tables exist\n");

	/* Local APIC */

	result = acpi_table_parse_madt(ACPI_MADT_LAPIC_ADDR_OVR, acpi_parse_lapic_addr_ovr);
	if (0 > result) {
		printk(KERN_ERR PREFIX "Error parsing LAPIC address override entry\n");
		return result;
	}

	result = acpi_table_parse_madt(ACPI_MADT_LAPIC, acpi_parse_lapic);
	if (1 > result) {
		printk(KERN_ERR PREFIX "Error parsing MADT - no LAPIC entries!\n");
		return -ENODEV;
	}

	result = acpi_table_parse_madt(ACPI_MADT_LAPIC_NMI, acpi_parse_lapic_nmi);
	if (0 > result) {
		printk(KERN_ERR PREFIX "Error parsing LAPIC NMI entry\n");
		return result;
	}

	/* I/O APIC */

	result = acpi_table_parse_madt(ACPI_MADT_IOAPIC, acpi_parse_ioapic);
	if (1 > result) {
		printk(KERN_ERR PREFIX "Error parsing MADT - no IOAPIC entries!\n");
		return -ENODEV;
	}

	acpi_mp_config = 1;

	/*
	 * TBD: Complete I/O APIC support.
	 *
	construct_default_ACPI_table();
	 */

	result = acpi_table_parse_madt(ACPI_MADT_INT_SRC_OVR, acpi_parse_int_src_ovr);
	if (0 > result) {
		printk(KERN_ERR PREFIX "Error parsing interrupt source overrides entry\n");
		return result;
	}

	result = acpi_table_parse_madt(ACPI_MADT_NMI_SRC, acpi_parse_nmi_src);
	if (0 > result) {
		printk(KERN_ERR PREFIX "Error parsing NMI SRC entry\n");
		return result;
	}

	/* Make boot-up look pretty */
	printk("%d CPUs total\n", total_cpus);

#endif /*CONFIG_X86_IO_APIC*/
#endif /*CONFIG_X86_LOCAL_APIC*/

#ifdef CONFIG_SERIAL_ACPI
	/*
	 * TBD: Need phased approach to table parsing (only do those absolutely
	 *      required during boot-up).  Recommend expanding concept of fix-
	 *      feature devices (ACPI Bus driver) to include table-based devices 
	 *      such as serial ports, EC, SMBus, etc.
	 */
	/* acpi_table_parse(ACPI_SPCR, acpi_parse_spcr);*/
#endif /*CONFIG_SERIAL_ACPI*/

	return 0;
}

#endif /*CONFIG_ACPI_BOOT*/


int __init
acpi_get_interrupt_model (
	int		*type)
{
	if (!type)
		return -EINVAL;

#ifdef CONFIG_X86_IO_APIC
	*type = ACPI_INT_MODEL_IOAPIC;
#else
	*type = ACPI_INT_MODEL_PIC;
#endif

	return 0;
}


/* --------------------------------------------------------------------------
                              Low-Level Sleep Support
   -------------------------------------------------------------------------- */

#ifdef CONFIG_ACPI_SLEEP

#define DEBUG

#ifdef DEBUG
#include <linux/serial.h>
#endif

/* address in low memory of the wakeup routine. */
unsigned long acpi_wakeup_address = 0;

/* new page directory that we will be using */
static pmd_t *pmd;

/* saved page directory */
static pmd_t saved_pmd;

/* page which we'll use for the new page directory */
static pte_t *ptep;

extern unsigned long FASTCALL(acpi_copy_wakeup_routine(unsigned long));

/*
 * acpi_create_identity_pmd
 *
 * Create a new, identity mapped pmd.
 *
 * Do this by creating new page directory, and marking all the pages as R/W
 * Then set it as the new Page Middle Directory.
 * And, of course, flush the TLB so it takes effect.
 *
 * We save the address of the old one, for later restoration.
 */
static void acpi_create_identity_pmd (void)
{
	pgd_t *pgd;
	int i;

	ptep = (pte_t*)__get_free_page(GFP_KERNEL);

	/* fill page with low mapping */
	for (i = 0; i < PTRS_PER_PTE; i++)
		set_pte(ptep + i, pfn_pte(i, PAGE_SHARED));

	pgd = pgd_offset(current->active_mm, 0);
	pmd = pmd_alloc(current->mm,pgd, 0);

	/* save the old pmd */
	saved_pmd = *pmd;

	/* set the new one */
	set_pmd(pmd, __pmd(_PAGE_TABLE + __pa(ptep)));

	/* flush the TLB */
	local_flush_tlb();
}

/*
 * acpi_restore_pmd
 *
 * Restore the old pmd saved by acpi_create_identity_pmd and
 * free the page that said function alloc'd
 */
static void acpi_restore_pmd (void)
{
	set_pmd(pmd, saved_pmd);
	local_flush_tlb();
	free_page((unsigned long)ptep);
}

/**
 * acpi_save_state_mem - save kernel state
 *
 * Create an identity mapped page table and copy the wakeup routine to
 * low memory.
 */
int acpi_save_state_mem (void)
{
	acpi_create_identity_pmd();
	acpi_copy_wakeup_routine(acpi_wakeup_address);

	return 0;
}

/**
 * acpi_save_state_disk - save kernel state to disk
 *
 */
int acpi_save_state_disk (void)
{
	return 1;
}

/*
 * acpi_restore_state
 */
void acpi_restore_state_mem (void)
{
	acpi_restore_pmd();
}

/**
 * acpi_reserve_bootmem - do _very_ early ACPI initialisation
 *
 * We allocate a page in low memory for the wakeup
 * routine for when we come back from a sleep state. The
 * runtime allocator allows specification of <16M pages, but not
 * <1M pages.
 */
void __init acpi_reserve_bootmem(void)
{
	acpi_wakeup_address = (unsigned long)alloc_bootmem_low(PAGE_SIZE);
	printk(KERN_DEBUG "ACPI: have wakeup address 0x%8.8lx\n", acpi_wakeup_address);
}

/*
 * (KG): Since we affect stack here, we make this function as flat and easy
 * as possible in order to not provoke gcc to use local variables on the stack.
 * Note that on resume, all (expect nosave) variables will have the state from
 * the time of writing (suspend_save_image) and the registers (including the
 * stack pointer, but excluding the instruction pointer) will be loaded with 
 * the values saved at save_processor_context() time.
 */
void do_suspend_magic(int resume)
{
	/* DANGER WILL ROBINSON!
	 *
	 * If this function is too difficult for gcc to optimize, it will crash and burn!
	 * see above.
	 *
	 * DO NOT TOUCH.
	 */
	if (!resume) {
		save_processor_context();
		acpi_save_register_state((unsigned long)&&acpi_sleep_done);
		acpi_enter_sleep_state(3);
		return;
	}
acpi_sleep_done:
	restore_processor_context();
	printk("CPU context restored...\n");
}

#endif /*CONFIG_ACPI_SLEEP*/

