/* $Id: setup.c,v 1.11 2001/03/02 15:52:03 bjornw Exp $
 *
 *  linux/arch/cris/kernel/setup.c
 *
 *  Copyright (C) 1995  Linus Torvalds
 *  Copyright (c) 2001  Axis Communications AB
 */

/*
 * This file handles the architecture-dependent parts of initialization
 */

#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/stddef.h>
#include <linux/unistd.h>
#include <linux/ptrace.h>
#include <linux/slab.h>
#include <linux/user.h>
#include <linux/a.out.h>
#include <linux/tty.h>
#include <linux/ioport.h>
#include <linux/delay.h>
#include <linux/config.h>
#include <linux/init.h>
#include <linux/bootmem.h>

#include <asm/segment.h>
#include <asm/system.h>
#include <asm/smp.h>
#include <asm/types.h>
#include <asm/svinto.h>

/*
 * Setup options
 */
struct drive_info_struct { char dummy[32]; } drive_info;
struct screen_info screen_info;

unsigned char aux_device_present;

extern int root_mountflags;
extern char _etext, _edata, _end;

#define COMMAND_LINE_SIZE 256

static char command_line[COMMAND_LINE_SIZE] = { 0, };
       char saved_command_line[COMMAND_LINE_SIZE];

extern const unsigned long text_start, edata; /* set by the linker script */

extern unsigned long romfs_start, romfs_length, romfs_in_flash; /* from head.S */

/* This mainly sets up the memory area, and can be really confusing.
 *
 * The physical DRAM is virtually mapped into dram_start to dram_end
 * (usually c0000000 to c0000000 + DRAM size). The physical address is
 * given by the macro __pa().
 *
 * In this DRAM, the kernel code and data is loaded, in the beginning.
 * It really starts at c00a0000 to make room for some special pages - 
 * the start address is text_start. The kernel data ends at _end. After
 * this the ROM filesystem is appended (if there is any).
 * 
 * Between this address and dram_end, we have RAM pages usable to the
 * boot code and the system.
 *
 */

void __init 
setup_arch(char **cmdline_p)
{
        unsigned long bootmap_size;
	unsigned long start_pfn, max_pfn;
	unsigned long memory_start;
	extern void console_print_etrax(const char *b);

#if (defined(CONFIG_CHR_DEV_FLASH) || defined(CONFIG_BLK_DEV_FLASH))
	/* TODO: move this into flash_init I think */
	flash_probe();
#endif

 	/* register an initial console printing routine for printk's */

	init_etrax_debug();

	/* we should really poll for DRAM size! */

	high_memory = &dram_end;

	if(romfs_in_flash || !romfs_length) {
		/* if we have the romfs in flash, or if there is no rom filesystem,
		 * our free area starts directly after the BSS 
		 */
		memory_start = (unsigned long) &_end;
	} else {
		/* otherwise the free area starts after the ROM filesystem */
		printk("ROM fs in RAM, size %d bytes\n", romfs_length);
		memory_start = romfs_start + romfs_length;
	}

	/* process 1's initial memory region is the kernel code/data */

	init_mm.start_code = (unsigned long) &text_start;
	init_mm.end_code =   (unsigned long) &_etext;
	init_mm.end_data =   (unsigned long) &_edata;
	init_mm.brk =        (unsigned long) &_end;

#define PFN_UP(x)       (((x) + PAGE_SIZE-1) >> PAGE_SHIFT)
#define PFN_DOWN(x)     ((x) >> PAGE_SHIFT)
#define PFN_PHYS(x)     ((x) << PAGE_SHIFT)

	/* min_low_pfn points to the start of DRAM, start_pfn points
	 * to the first DRAM pages after the kernel, and max_low_pfn
	 * to the end of DRAM.
	 */

        /*
         * partially used pages are not usable - thus
         * we are rounding upwards:
         */

        start_pfn = PFN_UP(memory_start);  /* usually c0000000 + kernel + romfs */
	max_pfn =   PFN_DOWN((unsigned long)high_memory); /* usually c0000000 + dram size */

        /*
         * Initialize the boot-time allocator (start, end)
	 *
	 * We give it access to all our DRAM, but we could as well just have
	 * given it a small slice. No point in doing that though, unless we
	 * have non-contiguous memory and want the boot-stuff to be in, say,
	 * the smallest area.
	 *
	 * It will put a bitmap of the allocated pages in the beginning
	 * of the range we give it, but it won't mark the bitmaps pages
	 * as reserved. We have to do that ourselves below.
	 *
	 * We need to use init_bootmem_node instead of init_bootmem
	 * because our map starts at a quite high address (min_low_pfn).
         */

	max_low_pfn = max_pfn;
	min_low_pfn = PAGE_OFFSET >> PAGE_SHIFT;

	bootmap_size = init_bootmem_node(NODE_DATA(0), start_pfn,
					 min_low_pfn, 
					 max_low_pfn);

	/* And free all memory not belonging to the kernel (addr, size) */

	free_bootmem(PFN_PHYS(start_pfn), PFN_PHYS(max_pfn - start_pfn));

        /*
         * Reserve the bootmem bitmap itself as well. We do this in two
         * steps (first step was init_bootmem()) because this catches
         * the (very unlikely) case of us accidentally initializing the
         * bootmem allocator with an invalid RAM area.
	 *
	 * Arguments are start, size
         */

        reserve_bootmem(PFN_PHYS(start_pfn), bootmap_size);

	/* paging_init() sets up the MMU and marks all pages as reserved */

	paging_init();

	/* we dont use a command line yet, so just let it be an empty string */

	*cmdline_p = command_line;
	strcpy(command_line, "root=/dev/rom"); /* use the appended romdisk as root */

	/* give credit for the CRIS port */

	printk("Linux/CRIS port on ETRAX 100LX (c) 2001 Axis Communications AB\n");

}

#ifdef CONFIG_PROC_FS
#define HAS_FPU		0x0001
#define HAS_MMU		0x0002
#define HAS_ETHERNET100	0x0004
#define HAS_TOKENRING	0x0008
#define HAS_SCSI	0x0010
#define HAS_ATA		0x0020
#define HAS_USB		0x0040
#define HAS_IRQ_BUG	0x0080
#define HAS_MMU_BUG     0x0100

static struct cpu_info {
	char *model;
	unsigned short cache;
	unsigned short flags;
} cpu_info[] = {
	{ "ETRAX 1",   0, 0 },
	{ "ETRAX 2",   0, 0 },	/* Don't say it HAS_TOKENRING - there are
				   lethal bugs in that chip that
				   prevents T-R from ever working.
				   Never go there, and never lead anyone
				   into believing it can work.  BTW:
				   Anyone working on a T-R network
				   driver? :-) :-) :-) :-/ */
	{ "ETRAX 3",   0, HAS_TOKENRING },
	{ "ETRAX 4",   0, HAS_TOKENRING | HAS_SCSI },
	{ "Unknown",   0, 0 },
	{ "Unknown",   0, 0 },
	{ "Unknown",   0, 0 },
	{ "Simulator",     8, HAS_ETHERNET100 | HAS_SCSI | HAS_ATA },
	{ "ETRAX 100",     8, HAS_ETHERNET100 | HAS_SCSI | HAS_ATA | HAS_IRQ_BUG },
	{ "ETRAX 100",     8, HAS_ETHERNET100 | HAS_SCSI | HAS_ATA },
	{ "ETRAX 100LX",  8, HAS_ETHERNET100 | HAS_SCSI | HAS_ATA | HAS_USB | HAS_MMU | HAS_MMU_BUG },
	{ "ETRAX 100LX v2",  8, HAS_ETHERNET100 | HAS_SCSI | HAS_ATA | HAS_USB | HAS_MMU },
	{ "Unknown",   0, 0 },
};

/*
 * BUFFER is PAGE_SIZE bytes long.
 */
int get_cpuinfo(char *buffer)
{
	int revision;
#ifndef CONFIG_SVINTO_SIM
	unsigned char tmp;

	__asm__ volatile ("move vr,%0" : "=rm" (tmp));
	revision = tmp;
#else
        /* Fake a revision for the simulator */
	revision = 7;
#endif

	return sprintf(buffer,
		       "cpu\t\t: CRIS\n"
		       "cpu revision\t: %d\n"
		       "cpu model\t: %s\n"
		       "cache size\t: %d kB\n"
		       "fpu\t\t: %s\n"
		       "mmu\t\t: %s\n"
		       "mmu DMA bug\t: %s\n"
		       "ethernet\t: %s Mbps\n"
		       "token ring\t: %s\n"
		       "scsi\t\t: %s\n"
		       "ata\t\t: %s\n"
		       "usb\t\t: %s\n"
		       "bogomips\t: %lu.%02lu\n",

		       revision,
		       cpu_info[revision].model,
		       cpu_info[revision].cache,
		       cpu_info[revision].flags & HAS_FPU ? "yes" : "no",
		       cpu_info[revision].flags & HAS_MMU ? "yes" : "no",
		       cpu_info[revision].flags & HAS_MMU_BUG ? "yes" : "no",
		       cpu_info[revision].flags & HAS_ETHERNET100 ? "10/100" : "10",
		       cpu_info[revision].flags & HAS_TOKENRING ? "4/16 Mbps" : "no",
		       cpu_info[revision].flags & HAS_SCSI ? "yes" : "no",
		       cpu_info[revision].flags & HAS_ATA ? "yes" : "no",
		       cpu_info[revision].flags & HAS_USB ? "yes" : "no",
		       (loops_per_jiffy * HZ + 500) / 100000,
		       ((loops_per_jiffy * HZ + 500) / 1000) % 100);
}
#endif /* CONFIG_PROC_FS */
