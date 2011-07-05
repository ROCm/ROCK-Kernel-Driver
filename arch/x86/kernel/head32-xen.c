/*
 *  linux/arch/i386/kernel/head32.c -- prepare to run common code
 *
 *  Copyright (C) 2000 Andrea Arcangeli <andrea@suse.de> SuSE
 *  Copyright (C) 2007 Eric Biederman <ebiederm@xmission.com>
 */

#include <linux/init.h>
#include <linux/start_kernel.h>
#include <linux/mm.h>
#include <linux/memblock.h>

#include <asm/setup.h>
#include <asm/sections.h>
#include <asm/e820.h>
#include <asm/trampoline.h>
#include <asm/apic.h>
#include <asm/io_apic.h>
#include <asm/tlbflush.h>

static void __init i386_default_early_setup(void)
{
	/* Initialize 32bit specific setup functions */
	x86_init.resources.reserve_resources = i386_reserve_resources;
#ifndef CONFIG_XEN
	x86_init.mpparse.setup_ioapic_ids = setup_ioapic_ids_from_mpc;

	reserve_ebda_region();
#endif
}

void __init i386_start_kernel(void)
{
#ifdef CONFIG_XEN
	struct xen_platform_parameters pp;

	WARN_ON(HYPERVISOR_vm_assist(VMASST_CMD_enable,
				     VMASST_TYPE_4gb_segments));

	init_mm.pgd = swapper_pg_dir = (pgd_t *)xen_start_info->pt_base;

	if (HYPERVISOR_xen_version(XENVER_platform_parameters, &pp) == 0) {
		hypervisor_virt_start = pp.virt_start;
		reserve_top_address(0UL - pp.virt_start);
	}

	BUG_ON(pte_index(hypervisor_virt_start));
#endif

	memblock_init();

	memblock_x86_reserve_range(__pa_symbol(&_text), __pa_symbol(&__bss_stop), "TEXT DATA BSS");

#ifndef CONFIG_XEN
#ifdef CONFIG_BLK_DEV_INITRD
	/* Reserve INITRD */
	if (boot_params.hdr.type_of_loader && boot_params.hdr.ramdisk_image) {
		/* Assume only end is not page aligned */
		u64 ramdisk_image = boot_params.hdr.ramdisk_image;
		u64 ramdisk_size  = boot_params.hdr.ramdisk_size;
		u64 ramdisk_end   = PAGE_ALIGN(ramdisk_image + ramdisk_size);
		memblock_x86_reserve_range(ramdisk_image, ramdisk_end, "RAMDISK");
	}
#endif

	/* Call the subarch specific early setup function */
	switch (boot_params.hdr.hardware_subarch) {
	case X86_SUBARCH_MRST:
		x86_mrst_early_setup();
		break;
	case X86_SUBARCH_CE4100:
		x86_ce4100_early_setup();
		break;
	default:
		i386_default_early_setup();
		break;
	}
#else
#ifdef CONFIG_BLK_DEV_INITRD
	BUG_ON(xen_start_info->flags & SIF_MOD_START_PFN);
	if (xen_start_info->mod_start)
		xen_initrd_start = __pa(xen_start_info->mod_start);
#endif
	{
		int max_cmdline;

		if ((max_cmdline = MAX_GUEST_CMDLINE) > COMMAND_LINE_SIZE)
			max_cmdline = COMMAND_LINE_SIZE;
		memcpy(boot_command_line, xen_start_info->cmd_line, max_cmdline);
		boot_command_line[max_cmdline-1] = '\0';
	}

	i386_default_early_setup();
	xen_start_kernel();
#endif

	/*
	 * At this point everything still needed from the boot loader
	 * or BIOS or kernel text should be early reserved or marked not
	 * RAM in e820. All other memory is free game.
	 */

	start_kernel();
}
