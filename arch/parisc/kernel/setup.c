/*    $Id: setup.c,v 1.8 2000/02/02 04:42:38 prumpf Exp $
 *
 *    Initial setup-routines for HP 9000 based hardware.
 * 
 *    Copyright (C) 1991, 1992, 1995  Linus Torvalds
 *    Modifications for PA-RISC (C) 1999 Helge Deller <helge.deller@ruhr-uni-bochum.de>
 *    Modifications copyright 1999 SuSE GmbH (Philipp Rumpf)
 *    Modifications copyright 2000 Martin K. Petersen <mkp@mkp.net>
 *    Modifications copyright 2000 Philipp Rumpf <prumpf@tux.org>
 *
 *    Initial PA-RISC Version: 04-23-1999 by Helge Deller
 *
 *    This program is free software; you can redistribute it and/or modify
 *    it under the terms of the GNU General Public License as published by
 *    the Free Software Foundation; either version 2, or (at your option)
 *    any later version.
 *
 *    This program is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    GNU General Public License for more details.
 * 
 *    You should have received a copy of the GNU General Public License
 *    along with this program; if not, write to the Free Software
 *    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 */

#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/malloc.h>
#include <linux/mm.h>
#include <linux/ptrace.h>
#include <linux/sched.h>
#include <linux/stddef.h>
#include <linux/unistd.h>
#include <linux/user.h>
#include <linux/tty.h>
#include <linux/config.h>
#include <linux/fs.h>
#include <linux/kdev_t.h>
#include <linux/major.h>
#include <linux/string.h>
#include <linux/blk.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/console.h>
#include <linux/bootmem.h>
#include <linux/delay.h>
#include <linux/pci.h>
#include <linux/threads.h>

#include <asm/cache.h>
#include <asm/hardware.h>	/* for register_driver() stuff */
#include <asm/processor.h>
#include <asm/page.h>
#include <asm/pdc.h>
#include <asm/led.h>
#include <asm/real.h>
#include <asm/system.h>
#include <asm/machdep.h>	/* for pa7300lc_init() proto */

#include <asm/irq.h>		/* for struct irq_region */
#include <asm/pdcpat.h>		/* for PA_VIEW PDC_PAT_CPU_GET_NUMBER etc */

#include <linux/proc_fs.h>

#define COMMAND_LINE_SIZE 1024
char	saved_command_line[COMMAND_LINE_SIZE];

/*
** KLUGE ALERT!
**
** We *really* should be using a combination of request_resource()
** and request_region()! But request_region() requires kmalloc since
** returns a new struct resource. And kmalloc just isn't available
** until after mem_init() is called from start_kernel().
**
** FIXME: assume contiguous memory initially.
**     Additional chunks of memory might be added to sysram_resource.sibling.
*/
static struct resource sysrom_resource  = {
	name: "System ROM", start: 0x0f0000000UL, end: 0x0f00fffffUL,
	flags: IORESOURCE_BUSY | IORESOURCE_MEM,
	parent: &iomem_resource, sibling: NULL, child: NULL };

static struct resource pdcdata_resource;

static struct resource sysram_resource  = {
	name: "System RAM", start: 0UL, end: ~0UL /* bogus */,
	flags: IORESOURCE_MEM,
	parent: &iomem_resource, sibling: &sysrom_resource, child: &pdcdata_resource};

extern char _text;	/* start of kernel code, defined by linker */
extern int  data_start; 
extern char _edata;	/* end of data, begin BSS, defined by linker */
extern char _end;	/* end of BSS, defined by linker */

static struct resource data_resource    = {
	name: "kernel Data", start: virt_to_phys(&data_start), end: virt_to_phys(&_end)-1,
	flags: IORESOURCE_BUSY | IORESOURCE_MEM,
	parent: &sysram_resource, sibling: NULL, child: NULL};

static struct resource code_resource = {
	name: "Kernel Code", start: virt_to_phys(&_text), end: virt_to_phys(&data_start)-1,
	flags: IORESOURCE_BUSY | IORESOURCE_MEM,
	parent: &sysram_resource, sibling: &data_resource, child: NULL};

static struct resource pdcdata_resource = {
	name: "PDC data (Page Zero)", start: 0, end: 0x9ff,
	flags: IORESOURCE_BUSY | IORESOURCE_MEM,
	parent: &sysram_resource, sibling: &code_resource, child: NULL};




struct system_cpuinfo_parisc boot_cpu_data;
struct cpuinfo_parisc cpu_data[NR_CPUS];

extern void do_inventory(void);
extern void cache_init(void);
extern struct hp_device * register_module(void *hpa);

static int cpu_driver_callback(struct hp_device *, struct pa_iodc_driver *);

static struct pa_iodc_driver cpu_drivers_for[] = {
	{HPHW_NPROC, 0x0, 0x0, 0x0, 0, 0,
		DRIVER_CHECK_HWTYPE,
		"CPU", "PARISC", (void *) cpu_driver_callback},
	{0,0,0,0,0,0,
		0,
		(char *) NULL, (char *) NULL, (void *) NULL }
};

static long fallback_cpu_hpa[] = { 0xfffa0000L, 0xfffbe000L, 0x0 };


/*
**  	PARISC CPU driver - claim "device" and initialize CPU data structures.
**
** Consolidate per CPU initialization into (mostly) one module.
** Monarch CPU will initialize boot_cpu_data which shouldn't
** change once the system has booted.
**
** The callback *should* do per-instance initialization of
** everything including the monarch. Some of the code that's
** in setup.c:start_parisc() should migrate here and start_parisc()
** should "register_driver(cpu_driver_for)" before calling
** do_inventory().
**
** The goal of consolidating CPU initialization into one place is
** to make sure all CPU's get initialized the same way.
** It would be nice if the even the manarch through the exact same code path.
** (up to rendevous at least).
*/
#undef ASSERT
#define ASSERT(expr) \
	if(!(expr)) { \
		printk( "\n" __FILE__ ":%d: Assertion " #expr " failed!\n",__LINE__); \
		panic(#expr); \
	}

static int
cpu_driver_callback(struct hp_device *d, struct pa_iodc_driver *dri)
{
#ifdef __LP64__
	extern int pdc_pat;	/* arch/parisc/kernel/inventory.c */
	static unsigned long pdc_result[32] __attribute__ ((aligned (8))) = {0,0,0,0};
#endif
	struct cpuinfo_parisc *p;

#ifndef CONFIG_SMP
	if (boot_cpu_data.cpu_count > 0) {
		printk(KERN_INFO "CONFIG_SMP disabled - not claiming addional CPUs\n");
		return(1);
	}
#endif

	p = &cpu_data[boot_cpu_data.cpu_count];
	boot_cpu_data.cpu_count++;

/* TODO: Enable FP regs - done early in start_parisc() now */

	/* initialize counters */
	memset(p, 0, sizeof(struct cpuinfo_parisc));

	p->hpa = (unsigned long) d->hpa; /* save CPU hpa */

#ifdef __LP64__
	if (pdc_pat) {
		ulong status;
	        pdc_pat_cell_mod_maddr_block_t pa_pdc_cell;

		status = pdc_pat_cell_module(& pdc_result, d->pcell_loc,
			d->mod_index, PA_VIEW, & pa_pdc_cell);

		ASSERT(PDC_RET_OK == status);

		/* verify it's the same as what do_pat_inventory() found */
		ASSERT(d->mod_info == pa_pdc_cell.mod_info);
		ASSERT(d->pmod_loc == pa_pdc_cell.mod_location);
		ASSERT(d->mod_path == pa_pdc_cell.mod_path);

		p->txn_addr = pa_pdc_cell.mod[0];   /* id_eid for IO sapic */

		/* get the cpu number */
		status = mem_pdc_call( PDC_PAT_CPU, PDC_PAT_CPU_GET_NUMBER,
				__pa(& pdc_result), d->hpa);

		ASSERT(PDC_RET_OK == status);

		p->cpuid = pdc_result[0];

	} else
#endif
	{
		p->txn_addr = (unsigned long) d->hpa;	/* for normal parisc */

		/* logical CPU ID and update global counter */
		p->cpuid = boot_cpu_data.cpu_count - 1;
	}

	/*
	** itimer and ipi IRQ handlers are statically initialized in
	** arch/parisc/kernel/irq.c
	*/
	p->region = irq_region[IRQ_FROM_REGION(CPU_IRQ_REGION)];

	return(0);
}


void __xchg_called_with_bad_pointer(void)
{
    printk(KERN_EMERG "xchg() called with bad pointer !\n");
}


/* Some versions of IODC don't list the CPU, and since we don't walk
 * the bus yet, we have to probe for processors at well known hpa
 * addresses.  
 */

void __init register_fallback_cpu (void)
{
	struct hp_device *d = NULL;
	int i = 0;
	
#ifdef CONFIG_SMP
#error "Revisit CPU fallback addresses for SMP (Assuming bus walk hasn't been implemented)"
#endif
	printk ("No CPUs reported by firmware - probing...\n");
	
	while (fallback_cpu_hpa[i]) {
		
		d = register_module ((void *) fallback_cpu_hpa[i]);
		
		if (d > 0) {
			printk ("Found CPU at %lx\n", fallback_cpu_hpa[i]);
			cpu_driver_callback (d, 0);
			return;
		}
		
		i++;
	}
	
	panic ("No CPUs found.  System halted.\n");
	return;
}


/*
 * Get CPU information and store it in the boot_cpu_data structure.  */
void __init collect_boot_cpu_data(void)
{
	memset(&boot_cpu_data,0,sizeof(boot_cpu_data));

	boot_cpu_data.cpu_hz = 100 * PAGE0->mem_10msec; /* Hz of this PARISC */

	/* get CPU-Model Information... */
#define p ((unsigned long *)&boot_cpu_data.pdc.model)
	if(pdc_model_info(&boot_cpu_data.pdc.model)==0)
		printk("model	%08lx %08lx %08lx %08lx %08lx %08lx %08lx %08lx %08lx\n",
			p[0], p[1], p[2], p[3], p[4], p[5], p[6], p[7], p[8]);
#undef p

	if(pdc_model_versions(&boot_cpu_data.pdc.versions, 0)==0)
		printk("vers	%08lx\n", boot_cpu_data.pdc.versions.cpuid);

	if(pdc_model_cpuid(&boot_cpu_data.pdc.cpuid)==0)
		printk("cpuid	%08lx\n", boot_cpu_data.pdc.cpuid.cpuid);
	
	printk("CPUID	vers %ld rev %ld\n",
		(boot_cpu_data.pdc.cpuid.cpuid >> 5) & 127,
		boot_cpu_data.pdc.cpuid.cpuid & 31);

	if (pdc_model_sysmodel(boot_cpu_data.pdc.sys_model_name)==0)
		printk("model	%s\n",boot_cpu_data.pdc.sys_model_name);

	boot_cpu_data.model_name = parisc_getHWdescription(HPHW_NPROC,
		boot_cpu_data.pdc.model.hversion>>4,
		boot_cpu_data.pdc.model.sversion>>8);
	
	boot_cpu_data.hversion =  boot_cpu_data.pdc.model.hversion;
	boot_cpu_data.sversion =  boot_cpu_data.pdc.model.sversion;

	boot_cpu_data.cpu_type =
			parisc_get_cpu_type(boot_cpu_data.pdc.model.hversion);

	boot_cpu_data.cpu_name = cpu_name_version[boot_cpu_data.cpu_type][0];
	boot_cpu_data.family_name = cpu_name_version[boot_cpu_data.cpu_type][1];
}


#ifdef __LP64__
#define COMMAND_GLOBAL  0xfffffffffffe0030UL
#else
#define COMMAND_GLOBAL  0xfffe0030
#endif

#define CMD_RESET       5       /* reset any module */

/*
** The Wright Brothers and Gecko systems have a H/W problem
** (Lasi...'nuf said) may cause a broadcast reset to lockup
** the system. An HVERSION dependent PDC call was developed
** to perform a "safe", platform specific broadcast reset instead
** of kludging up all the code.
**
** Older machines which do not implement PDC_BROADCAST_RESET will
** return (with an error) and the regular broadcast reset can be
** issued. Obviously, if the PDC does implement PDC_BROADCAST_RESET
** the PDC call will not return (the system will be reset).
*/
static int
reset_parisc(struct notifier_block *self, unsigned long command, void *ptr)
{
	printk("%s: %s(cmd=%lu)\n", __FILE__, __FUNCTION__, command);

	switch(command) {
	case MACH_RESTART:
#ifdef FASTBOOT_SELFTEST_SUPPORT
		/*
		** If user has modified the Firmware Selftest Bitmap,
		** run the tests specified in the bitmap after the
		** system is rebooted w/PDC_DO_RESET.
		**
		** ftc_bitmap = 0x1AUL "Skip destructive memory tests"
		**
		** Using "directed resets" at each processor with the MEM_TOC
		** vector cleared will also avoid running destructive
		** memory self tests. (Not implemented yet)
		*/
		if (ftc_bitmap) {
			mem_pdc_call( PDC_BROADCAST_RESET,
				PDC_DO_FIRM_TEST_RESET, PDC_FIRM_TEST_MAGIC,
				ftc_bitmap);
		}
#endif

		/* "Normal" system reset */
		(void) mem_pdc_call(PDC_BROADCAST_RESET, PDC_DO_RESET,
			0L, 0L, 0L);

		/* Nope...box should reset with just CMD_RESET now */
		gsc_writel(CMD_RESET, COMMAND_GLOBAL);

		/* Wait for RESET to lay us to rest. */
		while (1) ;

		break;
	}
	return NOTIFY_DONE;
}

static struct notifier_block parisc_block = { reset_parisc, NULL, 0 };


/*  start_parisc() will be called from head.S to setup our new memory_start 
    and actually start our kernel !
    Memory-Layout is:
	- Kernel-Image (code+data+BSS)
	- Stack (stack-size see below!, stack-setup-code is in head.S)
	- memory_start at end of stack..
*/

unsigned long mem_start, mem_max;
unsigned long start_pfn, max_pfn;
extern asmlinkage void __init start_kernel(void);

#define PFN_UP(x)	(((x) + PAGE_SIZE-1) >> PAGE_SHIFT)
#define PFN_DOWN(x)	((x) >> PAGE_SHIFT)
#define PFN_PHYS(x)	((x) << PAGE_SHIFT)

void __init start_parisc(unsigned arg0, unsigned arg1,
			 unsigned arg2, unsigned arg3)
{
	register unsigned long ccr;
	unsigned long memory_start;

	/* Clear BSS */

	{
		char *p = &_edata, *q = &_end;

		while (p < q) {
			*p++ = 0;
		}
	}


	pdc_console_init();

#ifdef __LP64__
	printk("The 64-bit Kernel has started...\n");
#else
	printk("The 32-bit Kernel has started...\n");
#endif

	/*
	** Enable FP coprocessor
	**
	** REVISIT: ccr should be set by PDC_COPROC results to support PA1.0.
	** Hardcoding works for PA1.1 processors.
	**
	** REVISIT: this could be done in the "code 22" trap handler.
	** (frowands idea - that way we know which processes need FP
	** registers saved on the interrupt stack.)
	**
	** NEWS FLASH: wide kernels need FP coprocessor enabled to handle
	** formatted printing of %lx for example (double divides I think)
	*/
	ccr = 0xc0;
	mtctl(ccr, 10);
	printk("Enabled FP coprocessor\n");

#ifdef __LP64__
	printk( "If this is the LAST MESSAGE YOU SEE, you're probably using\n"
		"32-bit millicode by mistake.\n");
#endif

	memory_start = (unsigned long) &_end;
	memory_start = (memory_start + PAGE_SIZE) & PAGE_MASK;
	printk("Free memory starts at: 0x%lx\n", memory_start);

	/* Collect stuff passed in from the boot loader */
	printk(KERN_WARNING  "%s(0x%x,0x%x,0x%x,0x%x)\n", 
		    __FUNCTION__, arg0, arg1, arg2, arg3);

	/* arg0 is free-mem start, arg1 is ptr to command line */
	if (arg0 < 64) {
		/* called from hpux boot loader */
		saved_command_line[0] = '\0';
	} else {
		strcpy(saved_command_line, (char *)__va(arg1));
		printk("PALO command line: '%s'\nPALO initrd %x-%x\n",
		    saved_command_line, arg2, arg3);

#ifdef CONFIG_BLK_DEV_INITRD
		if (arg2 != 0) /* did palo pass us a ramdisk? */
		{
		    initrd_start = (unsigned long)__va(arg2);
		    initrd_end = (unsigned long)__va(arg3);
		}
#endif
	}

	mem_start = __pa(memory_start);
#define MAX_MEM (512*1024*1024)
	mem_max = (PAGE0->imm_max_mem > MAX_MEM ? MAX_MEM : PAGE0->imm_max_mem);

	collect_boot_cpu_data();

	/* initialize the LCD/LED after boot_cpu_data is available ! */
        led_init();				/* LCD/LED initialization */

	do_inventory();				/* probe for hardware */
        register_driver(cpu_drivers_for);	/* claim all the CPUs */

	if (boot_cpu_data.cpu_count == 0)
	    register_fallback_cpu();

	printk("CPU(s): %d x %s at %d.%06d MHz\n", 
			boot_cpu_data.cpu_count,
			boot_cpu_data.cpu_name,
			boot_cpu_data.cpu_hz / 1000000, 
			boot_cpu_data.cpu_hz % 1000000	);

	switch (boot_cpu_data.cpu_type) {
	case pcx:
	case pcxs:
	case pcxt:
		hppa_dma_ops = &pcx_dma_ops;
		break;
	case pcxl2:
		pa7300lc_init();
	case pcxl: /* falls through */
		hppa_dma_ops = &pcxl_dma_ops;
		break;
	default:
		break;
	}

#if 1
	/* KLUGE! this really belongs in kernel/resource.c! */
	iomem_resource.end = ~0UL;
#endif
	sysram_resource.end = mem_max - 1;
	notifier_chain_register(&mach_notifier, &parisc_block);
	start_kernel(); 	/* now back to arch-generic code... */
}

void __init setup_arch(char **cmdline_p)
{
	unsigned long bootmap_size;
	unsigned long start_pfn;
	unsigned long mem_free;

	*cmdline_p = saved_command_line;

	/* initialize bootmem */

	start_pfn = PFN_UP(mem_start);
	max_pfn = PFN_DOWN(mem_max);

	bootmap_size = init_bootmem(start_pfn, max_pfn);

	mem_start += bootmap_size;
	mem_free = mem_max - mem_start;

	/* free_bootmem handles rounding nicely */
	printk("free_bootmem(0x%lx, 0x%lx)\n", (unsigned long)mem_start,
			(unsigned long)mem_free);
	free_bootmem(mem_start, mem_free);

#ifdef CONFIG_BLK_DEV_INITRD
	printk("initrd: %08x-%08x\n", (int) initrd_start, (int) initrd_end);

	if (initrd_end != 0) {
		initrd_below_start_ok = 1;
		reserve_bootmem(__pa(initrd_start), initrd_end - initrd_start);
	}
#endif

	cache_init();

	paging_init();

	if((unsigned long)&init_task_union&(INIT_TASK_SIZE - 1)) {
		printk("init_task_union not aligned.  Please recompile the kernel after changing the first line in arch/parisc/kernel/init_task.c from \n\"#define PAD 0\" to\n\"#define PAD 1\" or vice versa\n");
		for(;;);
	}


#ifdef CONFIG_SERIAL_CONSOLE
	/* nothing */
#elif CONFIG_VT
#if   defined(CONFIG_STI_CONSOLE)
	conswitchp = &dummy_con;	/* we use take_over_console() later ! */
#elif defined(CONFIG_IODC_CONSOLE)
	conswitchp = &prom_con;		/* it's currently really "prom_con" */
#elif defined(CONFIG_DUMMY_CONSOLE)
	conswitchp = &dummy_con;
#endif
#endif

}

#ifdef CONFIG_PROC_FS
/*
 *	Get CPU information for use by procfs.
 */

int get_cpuinfo(char *buffer)
{
	char		  *p = buffer;
	int		  n;

	for(n=0; n<boot_cpu_data.cpu_count; n++) {
#ifdef CONFIG_SMP
		if (!(cpu_online_map & (1<<n)))
			continue;
#endif
		p += sprintf(p, "processor\t: %d\n"
				"cpu family\t: PA-RISC %s\n",
				n, boot_cpu_data.family_name);

		p += sprintf(p, "cpu\t\t: %s\n",  boot_cpu_data.cpu_name );
	
		/* cpu MHz */
		p += sprintf(p, "cpu MHz\t\t: %d.%06d\n",
				 boot_cpu_data.cpu_hz / 1000000, 
				 boot_cpu_data.cpu_hz % 1000000  );

		p += sprintf(p, "model\t\t: %s\n"
				"model name\t: %s\n",
				boot_cpu_data.pdc.sys_model_name,
				boot_cpu_data.model_name);

		p += sprintf(p, "hversion\t: 0x%08x\n"
			        "sversion\t: 0x%08x\n",
				boot_cpu_data.hversion,
				boot_cpu_data.sversion );

		p += get_cache_info(p);
		/* print cachesize info ? */
		p += sprintf(p, "bogomips\t: %lu.%02lu\n",
			     (loops_per_sec+2500)/500000,
			     ((loops_per_sec+2500)/5000) % 100);
	}
	return p - buffer;
}
#endif

