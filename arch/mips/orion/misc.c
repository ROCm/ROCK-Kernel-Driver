/*
 * Catch-all for Orion-specify code that doesn't fit easily elsewhere.
 *   -- Cort
 */

#include <linux/config.h>
#include <linux/errno.h>
#include <linux/hdreg.h>
#include <linux/init.h>
#include <linux/ioport.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/stddef.h>
#include <linux/string.h>
#include <linux/unistd.h>
#include <linux/ptrace.h>
#include <linux/malloc.h>
#include <linux/user.h>
#include <linux/utsname.h>
#include <linux/a.out.h>
#include <linux/tty.h>
#ifdef CONFIG_BLK_DEV_RAM
#include <linux/blk.h>
#endif
#include <linux/ide.h>
#ifdef CONFIG_RTC
#include <linux/timex.h>
#endif

#include <asm/asm.h>
#include <asm/bootinfo.h>
#include <asm/cachectl.h>
#include <asm/io.h>
#include <asm/stackframe.h>
#include <asm/system.h>
#include <asm/cpu.h>
#include <linux/bootmem.h>
#include <asm/addrspace.h>
#include <asm/mc146818rtc.h>

char arcs_cmdline[CL_SIZE] = {0, };
extern int _end;

static unsigned char orion_rtc_read_data(unsigned long addr)
{
	return 0;
}

static void orion_rtc_write_data(unsigned char data, unsigned long addr)
{
}

static int orion_rtc_bcd_mode(void)
{
	return 0;
}

struct rtc_ops orion_rtc_ops = {
	&orion_rtc_read_data,
	&orion_rtc_write_data,
	&orion_rtc_bcd_mode
};

extern void InitCIB(void);
extern void InitQpic(void);
extern void InitCupid(void);

void __init orion_setup(void)
{
	InitCIB();
	InitQpic();
	InitCupid();
}

#define PFN_UP(x)	(((x) + PAGE_SIZE-1) >> PAGE_SHIFT)
#define PFN_ALIGN(x)	(((unsigned long)(x) + (PAGE_SIZE - 1)) & PAGE_MASK)



int orion_sysinit(void)
{
	unsigned long mem_size, free_start, free_end, start_pfn, bootmap_size;
	
	mips_machgroup = MACH_GROUP_ORION;
	/* 64 MB non-upgradable */
	mem_size = 32 << 20;
	
	free_start = PHYSADDR(PFN_ALIGN(&_end));
	free_end = mem_size;
	start_pfn = PFN_UP((unsigned long)&_end);
	
	/* Register all the contiguous memory with the bootmem allocator
	   and free it.  Be careful about the bootmem freemap.  */
	bootmap_size = init_bootmem(start_pfn, mem_size >> PAGE_SHIFT);
	
	/* Free the entire available memory after the _end symbol.  */
	free_start += bootmap_size;
	free_bootmem(free_start, free_end-free_start);
}
