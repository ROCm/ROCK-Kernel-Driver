/*
 * sleep.c - x86-specific ACPI sleep support.
 *
 *  Copyright (C) 2001-2003 Patrick Mochel
 */

#include <linux/acpi.h>
#include <linux/bootmem.h>
#include <asm/smp.h>


/* address in low memory of the wakeup routine. */
unsigned long acpi_wakeup_address = 0;
unsigned long acpi_video_flags;
extern char wakeup_start, wakeup_end;

extern unsigned long FASTCALL(acpi_copy_wakeup_routine(unsigned long));

static void init_low_mapping(pgd_t *pgd, int pgd_limit)
{
	int pgd_ofs = 0;

	while ((pgd_ofs < pgd_limit) && (pgd_ofs + USER_PTRS_PER_PGD < PTRS_PER_PGD)) {
		set_pgd(pgd, *(pgd+USER_PTRS_PER_PGD));
		pgd_ofs++, pgd++;
	}
}

/**
 * acpi_save_state_mem - save kernel state
 *
 * Create an identity mapped page table and copy the wakeup routine to
 * low memory.
 */
int acpi_save_state_mem (void)
{
#if CONFIG_X86_PAE
	panic("S3 and PAE do not like each other for now.");
	return 1;
#endif
	init_low_mapping(swapper_pg_dir, USER_PTRS_PER_PGD);
	memcpy((void *) acpi_wakeup_address, &wakeup_start, &wakeup_end - &wakeup_start);
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
	zap_low_mappings();
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
	if ((&wakeup_end - &wakeup_start) > PAGE_SIZE)
		printk(KERN_CRIT "ACPI: Wakeup code way too big, will crash on attempt to suspend\n");
	printk(KERN_DEBUG "ACPI: have wakeup address 0x%8.8lx\n", acpi_wakeup_address);
}

static int __init acpi_sleep_setup(char *str)
{
	while ((str != NULL) && (*str != '\0')) {
		if (strncmp(str, "s3_bios", 7) == 0)
			acpi_video_flags = 1;
		if (strncmp(str, "s3_mode", 7) == 0)
			acpi_video_flags |= 2;
		str = strchr(str, ',');
		if (str != NULL)
			str += strspn(str, ", \t");
	}
	return 1;
}


__setup("acpi_sleep=", acpi_sleep_setup);
