/*
 *  linux/arch/x86_64/kernel/head64.c -- prepare to run common code
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
#include <linux/module.h>

#include <asm/processor.h>
#include <asm/proto.h>
#include <asm/smp.h>
#include <asm/bootsetup.h>
#include <asm/setup.h>
#include <asm/desc.h>
#include <asm/pgtable.h>
#include <asm/sections.h>

unsigned long start_pfn;

/* Don't add a printk in there. printk relies on the PDA which is not initialized 
   yet. */
#if 0
static void __init clear_bss(void)
{
	memset(__bss_start, 0,
	       (unsigned long) __bss_stop - (unsigned long) __bss_start);
}
#endif

#define NEW_CL_POINTER		0x228	/* Relative to real mode data */
#define OLD_CL_MAGIC_ADDR	0x90020
#define OLD_CL_MAGIC            0xA33F
#define OLD_CL_BASE_ADDR        0x90000
#define OLD_CL_OFFSET           0x90022


static void __init copy_bootdata(char *real_mode_data)
{
#ifndef CONFIG_XEN
	int new_data;
	char * command_line;

	memcpy(x86_boot_params, real_mode_data, BOOT_PARAM_SIZE);
	new_data = *(int *) (x86_boot_params + NEW_CL_POINTER);
	if (!new_data) {
		if (OLD_CL_MAGIC != * (u16 *) OLD_CL_MAGIC_ADDR) {
			return;
		}
		new_data = OLD_CL_BASE_ADDR + * (u16 *) OLD_CL_OFFSET;
	}
	command_line = (char *) ((u64)(new_data));
	memcpy(boot_command_line, command_line, COMMAND_LINE_SIZE);
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
	int i;

	xen_start_info = (struct start_info *)real_mode_data;
	if (!xen_feature(XENFEAT_auto_translated_physmap)) {
		phys_to_machine_mapping =
			(unsigned long *)xen_start_info->mfn_list;
		start_pfn = (__pa(xen_start_info->pt_base) >> PAGE_SHIFT) +
			xen_start_info->nr_pt_frames;
	}


	machine_to_phys_mapping = (unsigned long *)MACH2PHYS_VIRT_START;
	machine_to_phys_nr_ents = MACH2PHYS_NR_ENTRIES;
	if (HYPERVISOR_memory_op(XENMEM_machphys_mapping, &mapping) == 0) {
		machine_to_phys_mapping = (unsigned long *)mapping.v_start;
		machine_to_phys_nr_ents = mapping.max_mfn + 1;
	}
	while ((1UL << machine_to_phys_order) < machine_to_phys_nr_ents )
		machine_to_phys_order++;

#if 0
	/* clear bss before set_intr_gate with early_idt_handler */
	clear_bss();

	for (i = 0; i < IDT_ENTRIES; i++)
		set_intr_gate(i, early_idt_handler);
	asm volatile("lidt %0" :: "m" (idt_descr));
#endif

	early_printk("Kernel alive\n");

 	for (i = 0; i < NR_CPUS; i++)
 		cpu_pda(i) = &boot_cpu_pda[i];

	pda_init(0);
	copy_bootdata(real_mode_data);
#ifdef CONFIG_SMP
	cpu_set(0, cpu_online_map);
#endif
	start_kernel();
}
