/*
 *  linux/arch/i386/kernel/head32.c -- prepare to run common code
 *
 *  Copyright (C) 2000 Andrea Arcangeli <andrea@suse.de> SuSE
 *  Copyright (C) 2007 Eric Biederman <ebiederm@xmission.com>
 */

#include <linux/init.h>
#include <linux/start_kernel.h>

#include <asm/setup.h>
#include <asm/setup_arch.h>
#include <asm/sections.h>
#include <asm/e820.h>
#include <asm/bios_ebda.h>
#include <asm/trampoline.h>

void __init i386_start_kernel(void)
{
	reserve_trampoline_memory();

	reserve_early(__pa_symbol(&_text), __pa_symbol(&__bss_stop), "TEXT DATA BSS");

#ifndef CONFIG_XEN
#ifdef CONFIG_BLK_DEV_INITRD
	/* Reserve INITRD */
	if (boot_params.hdr.type_of_loader && boot_params.hdr.ramdisk_image) {
		u64 ramdisk_image = boot_params.hdr.ramdisk_image;
		u64 ramdisk_size  = boot_params.hdr.ramdisk_size;
		u64 ramdisk_end   = ramdisk_image + ramdisk_size;
		reserve_early(ramdisk_image, ramdisk_end, "RAMDISK");
	}
#endif
	reserve_ebda_region();
#else
	{
		int max_cmdline;

		if ((max_cmdline = MAX_GUEST_CMDLINE) > COMMAND_LINE_SIZE)
			max_cmdline = COMMAND_LINE_SIZE;
		memcpy(boot_command_line, xen_start_info->cmd_line, max_cmdline);
		boot_command_line[max_cmdline-1] = '\0';
	}

	xen_start_kernel();
#endif

	/*
	 * At this point everything still needed from the boot loader
	 * or BIOS or kernel text should be early reserved or marked not
	 * RAM in e820. All other memory is free game.
	 */

	start_kernel();
}
