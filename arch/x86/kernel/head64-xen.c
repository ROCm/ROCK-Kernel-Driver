/*
 *  prepare to run common code
 *
 *  Copyright (C) 2000 Andrea Arcangeli <andrea@suse.de> SuSE
 *
 *  Jun Nakajima <jun.nakajima@intel.com>
 *	Modified for Xen.
 */

/* PDA is not ready to be used until the end of x86_64_start_kernel(). */
#define arch_use_lazy_mmu_mode() false

#include <linux/init.h>
#include <linux/linkage.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/percpu.h>
#include <linux/start_kernel.h>
#include <linux/io.h>
#include <linux/module.h>

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

/* boot cpu pda */
static struct x8664_pda _boot_cpu_pda __read_mostly;

#ifdef CONFIG_SMP
/*
 * We install an empty cpu_pda pointer table to indicate to early users
 * (numa_set_node) that the cpu_pda pointer table for cpus other than
 * the boot cpu is not yet setup.
 */
static struct x8664_pda *__cpu_pda[NR_CPUS] __initdata;
#else
static struct x8664_pda *__cpu_pda[NR_CPUS] __read_mostly;
#endif

void __init x86_64_init_pda(void)
{
	_cpu_pda = __cpu_pda;
	cpu_pda(0) = &_boot_cpu_pda;
	pda_init(0);
}

#ifndef CONFIG_XEN
static void __init zap_identity_mappings(void)
{
	pgd_t *pgd = pgd_offset_k(0UL);
	pgd_clear(pgd);
	__flush_tlb_all();
}

/* Don't add a printk in there. printk relies on the PDA which is not initialized 
   yet. */
static void __init clear_bss(void)
{
	memset(__bss_start, 0,
	       (unsigned long) __bss_stop - (unsigned long) __bss_start);
}
#endif

static void __init copy_bootdata(char *real_mode_data)
{
#ifndef CONFIG_XEN
	char * command_line;

	memcpy(&boot_params, real_mode_data, sizeof boot_params);
	if (boot_params.hdr.cmd_line_ptr) {
		command_line = __va(boot_params.hdr.cmd_line_ptr);
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
unsigned long *machine_to_phys_mapping;
EXPORT_SYMBOL(machine_to_phys_mapping);
unsigned int machine_to_phys_order;
EXPORT_SYMBOL(machine_to_phys_order);

void __init x86_64_start_kernel(char * real_mode_data)
{
	struct xen_machphys_mapping mapping;
	unsigned long machine_to_phys_nr_ents;

	/*
	 * Build-time sanity checks on the kernel image and module
	 * area mappings. (these are purely build-time and produce no code)
	 */
	BUILD_BUG_ON(MODULES_VADDR < KERNEL_IMAGE_START);
	BUILD_BUG_ON(MODULES_VADDR-KERNEL_IMAGE_START < KERNEL_IMAGE_SIZE);
	BUILD_BUG_ON(MODULES_LEN + KERNEL_IMAGE_SIZE > 2*PUD_SIZE);
	BUILD_BUG_ON((KERNEL_IMAGE_START & ~PMD_MASK) != 0);
	BUILD_BUG_ON((MODULES_VADDR & ~PMD_MASK) != 0);
	BUILD_BUG_ON(!(MODULES_VADDR > __START_KERNEL));
	BUILD_BUG_ON(!(((MODULES_END - 1) & PGDIR_MASK) ==
				(__START_KERNEL & PGDIR_MASK)));
	BUILD_BUG_ON(__fix_to_virt(__end_of_fixed_addresses) <= MODULES_END);

	xen_setup_features();

	xen_start_info = (struct start_info *)real_mode_data;
	if (!xen_feature(XENFEAT_auto_translated_physmap))
		phys_to_machine_mapping =
			(unsigned long *)xen_start_info->mfn_list;

	machine_to_phys_mapping = (unsigned long *)MACH2PHYS_VIRT_START;
	machine_to_phys_nr_ents = MACH2PHYS_NR_ENTRIES;
	if (HYPERVISOR_memory_op(XENMEM_machphys_mapping, &mapping) == 0) {
		machine_to_phys_mapping = (unsigned long *)mapping.v_start;
		machine_to_phys_nr_ents = mapping.max_mfn + 1;
	}
	while ((1UL << machine_to_phys_order) < machine_to_phys_nr_ents )
		machine_to_phys_order++;

#ifndef CONFIG_XEN
	/* clear bss before set_intr_gate with early_idt_handler */
	clear_bss();

	/* Make NULL pointers segfault */
	zap_identity_mappings();

	/* Cleanup the over mapped high alias */
	cleanup_highmap();

	for (i = 0; i < NUM_EXCEPTION_VECTORS; i++) {
#ifdef CONFIG_EARLY_PRINTK
		set_intr_gate(i, &early_idt_handlers[i]);
#else
		set_intr_gate(i, early_idt_handler);
#endif
	}
	load_idt((const struct desc_ptr *)&idt_descr);
#endif

	early_printk("Kernel alive\n");

	x86_64_init_pda();

	early_printk("Kernel really alive\n");

	x86_64_start_reservations(real_mode_data);
}

void __init x86_64_start_reservations(char *real_mode_data)
{
	copy_bootdata(__va(real_mode_data));

	reserve_early(__pa_symbol(&_text), __pa_symbol(&_end), "TEXT DATA BSS");

	reserve_early(round_up(__pa_symbol(&_end), PAGE_SIZE),
		      __pa(xen_start_info->pt_base)
		      + (xen_start_info->nr_pt_frames << PAGE_SHIFT),
		      "Xen provided");

	/*
	 * At this point everything still needed from the boot loader
	 * or BIOS or kernel text should be early reserved or marked not
	 * RAM in e820. All other memory is free game.
	 */

	start_kernel();
}
