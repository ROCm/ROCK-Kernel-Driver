/*
 *  prepare to run common code
 *
 *  Copyright (C) 2000 Andrea Arcangeli <andrea@suse.de> SuSE
 *
 *  Jun Nakajima <jun.nakajima@intel.com>
 *	Modified for Xen.
 */

#include <linux/init.h>
#include <linux/linkage.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/percpu.h>
#include <linux/start_kernel.h>
#include <linux/io.h>
#include <linux/memblock.h>

#include <asm/processor.h>
#include <asm/proto.h>
#include <asm/smp.h>
#include <asm/setup.h>
#include <asm/desc.h>
#include <asm/pgtable.h>
#include <asm/tlbflush.h>
#include <asm/sections.h>
#include <asm/kdebug.h>
#include <asm/e820.h>
#include <asm/bios_ebda.h>

#ifndef CONFIG_XEN
/*
 * Manage page tables very early on.
 */
extern pgd_t early_level4_pgt[PTRS_PER_PGD];
extern pmd_t early_dynamic_pgts[EARLY_DYNAMIC_PAGE_TABLES][PTRS_PER_PMD];
static unsigned int __initdata next_early_pgt = 2;
pmdval_t early_pmd_flags = __PAGE_KERNEL_LARGE & ~(_PAGE_GLOBAL | _PAGE_NX);

/* Wipe all early page tables except for the kernel symbol map */
static void __init reset_early_page_tables(void)
{
	unsigned long i;

	for (i = 0; i < PTRS_PER_PGD-1; i++)
		early_level4_pgt[i].pgd = 0;

	next_early_pgt = 0;

	write_cr3(__pa_nodebug(early_level4_pgt));
}

/* Create a new PMD entry */
int __init early_make_pgtable(unsigned long address)
{
	unsigned long physaddr = address - __PAGE_OFFSET;
	unsigned long i;
	pgdval_t pgd, *pgd_p;
	pudval_t pud, *pud_p;
	pmdval_t pmd, *pmd_p;

	/* Invalid address or early pgt is done ?  */
	if (physaddr >= MAXMEM || read_cr3() != __pa_nodebug(early_level4_pgt))
		return -1;

again:
	pgd_p = &early_level4_pgt[pgd_index(address)].pgd;
	pgd = *pgd_p;

	/*
	 * The use of __START_KERNEL_map rather than __PAGE_OFFSET here is
	 * critical -- __PAGE_OFFSET would point us back into the dynamic
	 * range and we might end up looping forever...
	 */
	if (pgd)
		pud_p = (pudval_t *)((pgd & PTE_PFN_MASK) + __START_KERNEL_map - phys_base);
	else {
		if (next_early_pgt >= EARLY_DYNAMIC_PAGE_TABLES) {
			reset_early_page_tables();
			goto again;
		}

		pud_p = (pudval_t *)early_dynamic_pgts[next_early_pgt++];
		for (i = 0; i < PTRS_PER_PUD; i++)
			pud_p[i] = 0;
		*pgd_p = (pgdval_t)pud_p - __START_KERNEL_map + phys_base + _KERNPG_TABLE;
	}
	pud_p += pud_index(address);
	pud = *pud_p;

	if (pud)
		pmd_p = (pmdval_t *)((pud & PTE_PFN_MASK) + __START_KERNEL_map - phys_base);
	else {
		if (next_early_pgt >= EARLY_DYNAMIC_PAGE_TABLES) {
			reset_early_page_tables();
			goto again;
		}

		pmd_p = (pmdval_t *)early_dynamic_pgts[next_early_pgt++];
		for (i = 0; i < PTRS_PER_PMD; i++)
			pmd_p[i] = 0;
		*pud_p = (pudval_t)pmd_p - __START_KERNEL_map + phys_base + _KERNPG_TABLE;
	}
	pmd = (physaddr & PMD_MASK) + early_pmd_flags;
	pmd_p[pmd_index(address)] = pmd;

	return 0;
}

/* Don't add a printk in there. printk relies on the PDA which is not initialized 
   yet. */
static void __init clear_bss(void)
{
	memset(__bss_start, 0,
	       (unsigned long) __bss_stop - (unsigned long) __bss_start);
}

static unsigned long get_cmd_line_ptr(void)
{
	unsigned long cmd_line_ptr = boot_params.hdr.cmd_line_ptr;

	cmd_line_ptr |= (u64)boot_params.ext_cmd_line_ptr << 32;

	return cmd_line_ptr;
}
#else
const unsigned long phys_base = 0;
#endif

static void __init copy_bootdata(char *real_mode_data)
{
#ifndef CONFIG_XEN
	char * command_line;
	unsigned long cmd_line_ptr;

	memcpy(&boot_params, real_mode_data, sizeof boot_params);
	sanitize_boot_params(&boot_params);
	cmd_line_ptr = get_cmd_line_ptr();
	if (cmd_line_ptr) {
		command_line = __va(cmd_line_ptr);
		memcpy(boot_command_line, command_line, COMMAND_LINE_SIZE);
	}
#else
	int max_cmdline;
	
	if ((max_cmdline = MAX_GUEST_CMDLINE) > COMMAND_LINE_SIZE)
		max_cmdline = COMMAND_LINE_SIZE;
	memcpy(boot_command_line, xen_start_info->cmd_line, max_cmdline);
	boot_command_line[max_cmdline-1] = '\0';
#endif
}

#include <xen/interface/memory.h>

asmlinkage __visible void __init x86_64_start_kernel(char * real_mode_data)
{
	int rc;

	/*
	 * Build-time sanity checks on the kernel image and module
	 * area mappings. (these are purely build-time and produce no code)
	 */
	BUILD_BUG_ON(MODULES_VADDR < __START_KERNEL_map);
	BUILD_BUG_ON(MODULES_VADDR - __START_KERNEL_map < KERNEL_IMAGE_SIZE);
	BUILD_BUG_ON(MODULES_LEN + KERNEL_IMAGE_SIZE > 2*PUD_SIZE);
	BUILD_BUG_ON((__START_KERNEL_map & ~PMD_MASK) != 0);
	BUILD_BUG_ON((MODULES_VADDR & ~PMD_MASK) != 0);
	BUILD_BUG_ON(!(MODULES_VADDR > __START_KERNEL));
	BUILD_BUG_ON(!(((MODULES_END - 1) & PGDIR_MASK) ==
				(__START_KERNEL & PGDIR_MASK)));
	BUILD_BUG_ON(__fix_to_virt(__end_of_fixed_addresses) <= MODULES_END);

	cr4_init_shadow();

	xen_start_info = (struct start_info *)real_mode_data;

	rc = HYPERVISOR_vm_assist(VMASST_CMD_enable, VMASST_TYPE_m2p_strict);

	xen_start_kernel();

#ifndef CONFIG_XEN
	/* Kill off the identity-map trampoline */
	reset_early_page_tables();

	clear_bss();

	clear_page(init_level4_pgt);

	kasan_early_init();

	for (i = 0; i < NUM_EXCEPTION_VECTORS; i++)
		set_intr_gate(i, early_idt_handler_array[i]);
	load_idt((const struct desc_ptr *)&idt_descr);

	copy_bootdata(__va(real_mode_data));

	/*
	 * Load microcode early on BSP.
	 */
	load_ucode_bsp();
#endif

#ifndef CONFIG_XEN
	/* set init_level4_pgt kernel high mapping*/
	init_level4_pgt[511] = early_level4_pgt[511];

#else
	if (rc)
		printk(KERN_WARNING "M2P strict mode unavailable (%d)\n", rc);

	xen_switch_pt();
#endif
	x86_64_start_reservations(real_mode_data);
}

void __init x86_64_start_reservations(char *real_mode_data)
{
	copy_bootdata(__va(real_mode_data));

#ifdef CONFIG_BLK_DEV_INITRD
	/* Reserve INITRD if needed. */
	if (xen_start_info->flags & SIF_MOD_START_PFN)
		xen_initrd_start = PFN_PHYS(xen_start_info->mod_start);
	else if (xen_start_info->mod_start)
		xen_initrd_start = __pa(xen_start_info->mod_start);
#endif

	if (xen_feature(XENFEAT_auto_translated_physmap))
		xen_start_info->mfn_list = ~0UL;
	else if (xen_start_info->mfn_list < __START_KERNEL_map)
		memblock_reserve(PFN_PHYS(xen_start_info->first_p2m_pfn),
				 PFN_PHYS(xen_start_info->nr_p2m_frames));

	start_kernel();
}
