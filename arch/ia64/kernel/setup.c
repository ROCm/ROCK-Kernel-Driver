/*
 * Architecture-specific setup.
 *
 * Copyright (C) 1998-2000 Hewlett-Packard Co
 * Copyright (C) 1998-2000 David Mosberger-Tang <davidm@hpl.hp.com>
 * Copyright (C) 1998, 1999 Stephane Eranian <eranian@hpl.hp.com>
 * Copyright (C) 2000, Rohit Seth <rohit.seth@intel.com>
 * Copyright (C) 1999 VA Linux Systems
 * Copyright (C) 1999 Walt Drummond <drummond@valinux.com>
 *
 * 04/04/00 D.Mosberger renamed cpu_initialized to cpu_online_map
 * 03/31/00 R.Seth	cpu_initialized and current->processor fixes
 * 02/04/00 D.Mosberger	some more get_cpuinfo fixes...
 * 02/01/00 R.Seth	fixed get_cpuinfo for SMP
 * 01/07/99 S.Eranian	added the support for command line argument
 * 06/24/99 W.Drummond	added boot_cpu_data.
 */
#include <linux/config.h>
#include <linux/init.h>

#include <linux/bootmem.h>
#include <linux/delay.h>
#include <linux/kernel.h>
#include <linux/reboot.h>
#include <linux/sched.h>
#include <linux/string.h>
#include <linux/threads.h>
#include <linux/console.h>

#include <asm/acpi-ext.h>
#include <asm/ia32.h>
#include <asm/page.h>
#include <asm/machvec.h>
#include <asm/processor.h>
#include <asm/sal.h>
#include <asm/system.h>
#include <asm/efi.h>
#include <asm/mca.h>
#include <asm/smp.h>

#ifdef CONFIG_BLK_DEV_RAM
# include <linux/blk.h>
#endif

extern char _end;

/* cpu_data[0] is data for the bootstrap processor: */
struct cpuinfo_ia64 cpu_data[NR_CPUS];

unsigned long ia64_cycles_per_usec;
struct ia64_boot_param ia64_boot_param;
struct screen_info screen_info;
/* This tells _start which CPU is booting.  */
int cpu_now_booting = 0;

#ifdef CONFIG_SMP
volatile unsigned long cpu_online_map;
#endif

unsigned long ia64_iobase;	/* virtual address for I/O accesses */

#define COMMAND_LINE_SIZE	512

char saved_command_line[COMMAND_LINE_SIZE]; /* used in proc filesystem */

static int
find_max_pfn (unsigned long start, unsigned long end, void *arg)
{
	unsigned long *max_pfn = arg, pfn;

	pfn = (PAGE_ALIGN(end - 1) - PAGE_OFFSET) >> PAGE_SHIFT;
	if (pfn > *max_pfn)
		*max_pfn = pfn;
	return 0;
}

static int
free_available_memory (unsigned long start, unsigned long end, void *arg)
{
#	define KERNEL_END	((unsigned long) &_end)
#	define MIN(a,b)		((a) < (b) ? (a) : (b))
#	define MAX(a,b)		((a) > (b) ? (a) : (b))
	unsigned long range_start, range_end;

	range_start = MIN(start, KERNEL_START);
	range_end   = MIN(end, KERNEL_START);

	/*
	 * XXX This should not be necessary, but the bootmem allocator
	 * is broken and fails to work correctly when the starting
	 * address is not properly aligned.
	 */
	range_start = PAGE_ALIGN(range_start);

	if (range_start < range_end)
		free_bootmem(__pa(range_start), range_end - range_start);

	range_start = MAX(start, KERNEL_END);
	range_end   = MAX(end, KERNEL_END);

	/*
	 * XXX This should not be necessary, but the bootmem allocator
	 * is broken and fails to work correctly when the starting
	 * address is not properly aligned.
	 */
	range_start = PAGE_ALIGN(range_start);

	if (range_start < range_end)
		free_bootmem(__pa(range_start), range_end - range_start);

	return 0;
}

void __init
setup_arch (char **cmdline_p)
{
	extern unsigned long ia64_iobase;
	unsigned long max_pfn, bootmap_start, bootmap_size;

	unw_init();

	/*
	 * The secondary bootstrap loader passes us the boot
	 * parameters at the beginning of the ZERO_PAGE, so let's
	 * stash away those values before ZERO_PAGE gets cleared out.
	 */
	memcpy(&ia64_boot_param, (void *) ZERO_PAGE_ADDR, sizeof(ia64_boot_param));

	*cmdline_p = __va(ia64_boot_param.command_line);
	strncpy(saved_command_line, *cmdline_p, sizeof(saved_command_line));
	saved_command_line[COMMAND_LINE_SIZE-1] = '\0';		/* for safety */

	efi_init();

	max_pfn = 0;
	efi_memmap_walk(find_max_pfn, &max_pfn);

	/*
	 * This is wrong, wrong, wrong.  Darn it, you'd think if they
	 * change APIs, they'd do things for the better.  Grumble...
	 */
	bootmap_start = PAGE_ALIGN(__pa(&_end));
	if (ia64_boot_param.initrd_size)
		bootmap_start = PAGE_ALIGN(bootmap_start
					   + ia64_boot_param.initrd_size);
	bootmap_size = init_bootmem(bootmap_start >> PAGE_SHIFT, max_pfn);

	efi_memmap_walk(free_available_memory, 0);

	reserve_bootmem(bootmap_start, bootmap_size);

#ifdef CONFIG_BLK_DEV_INITRD
	initrd_start = ia64_boot_param.initrd_start;

	if (initrd_start) {
		u64 start, size;
#		define is_same_page(a,b) (((a)&PAGE_MASK) == ((b)&PAGE_MASK))

#if 1
		/* XXX for now some backwards compatibility... */
		if (initrd_start >= PAGE_OFFSET)
			printk("Warning: boot loader passed virtual address "
			       "for initrd, please upgrade the loader\n");
		else
#endif
			/* 
			 * The loader ONLY passes physical addresses
			 */
			initrd_start = (unsigned long)__va(initrd_start);
		initrd_end = initrd_start+ia64_boot_param.initrd_size;
		start      = initrd_start;
		size       = ia64_boot_param.initrd_size;

		printk("Initial ramdisk at: 0x%p (%lu bytes)\n",
		       (void *) initrd_start, ia64_boot_param.initrd_size);

		/*
		 * The kernel end and the beginning of initrd can be
		 * on the same page. This would cause the page to be
		 * reserved twice.  While not harmful, it does lead to
		 * a warning message which can cause confusion.  Thus,
		 * we make sure that in this case we only reserve new
		 * pages, i.e., initrd only pages. We need to:
		 *
		 *	- align up start
		 *	- adjust size of reserved section accordingly
		 *
		 * It should be noted that this operation is only
		 * valid for the reserve_bootmem() call and does not
		 * affect the integrety of the initrd itself.
		 *
		 * reserve_bootmem() considers partial pages as reserved.
		 */
		if (is_same_page(initrd_start, (unsigned long)&_end)) {
			start  = PAGE_ALIGN(start);
			size  -= start-initrd_start;

			printk("Initial ramdisk & kernel on the same page: "
			       "reserving start=%lx size=%ld bytes\n",
			       start, size);
		}
		reserve_bootmem(__pa(start), size);
	}
#endif
#if 0
	/* XXX fix me */
	init_mm.start_code = (unsigned long) &_stext;
	init_mm.end_code = (unsigned long) &_etext;
	init_mm.end_data = (unsigned long) &_edata;
	init_mm.brk = (unsigned long) &_end;

	code_resource.start = virt_to_bus(&_text);
	code_resource.end = virt_to_bus(&_etext) - 1;
	data_resource.start = virt_to_bus(&_etext);
	data_resource.end = virt_to_bus(&_edata) - 1;
#endif

	/* process SAL system table: */
	ia64_sal_init(efi.sal_systab);

#ifdef CONFIG_SMP
	current->processor = 0;
	cpu_physical_id(0) = hard_smp_processor_id();
#endif
	/*
	 *  Set `iobase' to the appropriate address in region 6
	 *    (uncached access range)
	 */
	__asm__ ("mov %0=ar.k0;;" : "=r"(ia64_iobase));
	ia64_iobase = __IA64_UNCACHED_OFFSET | (ia64_iobase & ~PAGE_OFFSET);

	cpu_init();	/* initialize the bootstrap CPU */

#ifdef CONFIG_IA64_GENERIC
	machvec_init(acpi_get_sysname());
#endif

#ifdef	CONFIG_ACPI20
	if (efi.acpi20) {
		/* Parse the ACPI 2.0 tables */
		acpi20_parse(efi.acpi20);
	} else 
#endif
	if (efi.acpi) {
		/* Parse the ACPI tables */
		acpi_parse(efi.acpi);
	}

#ifdef CONFIG_VT
# if defined(CONFIG_VGA_CONSOLE)
	conswitchp = &vga_con;
# elif defined(CONFIG_DUMMY_CONSOLE)
	conswitchp = &dummy_con;
# endif
#endif

#ifdef CONFIG_IA64_MCA
	/* enable IA-64 Machine Check Abort Handling */
	ia64_mca_init();
#endif

	paging_init();
	platform_setup(cmdline_p);
}

/*
 * Display cpu info for all cpu's.
 */
int
get_cpuinfo (char *buffer)
{
#ifdef CONFIG_SMP
#	define lpj	c->loops_per_jiffy
#else
#	define lpj	loops_per_jiffy
#endif
	char family[32], model[32], features[128], *cp, *p = buffer;
	struct cpuinfo_ia64 *c;
	unsigned long mask;

	for (c = cpu_data; c < cpu_data + NR_CPUS; ++c) {
#ifdef CONFIG_SMP
		if (!(cpu_online_map & (1UL << (c - cpu_data))))
			continue;
#endif

		mask = c->features;

		if (c->family == 7)
			memcpy(family, "IA-64", 6);
		else
			sprintf(family, "%u", c->family);

		switch (c->model) {
		      case 0:	strcpy(model, "Itanium"); break;
		      default:	sprintf(model, "%u", c->model); break;
		}

		/* build the feature string: */
		memcpy(features, " standard", 10);
		cp = features;
		if (mask & 1) {
			strcpy(cp, " branchlong");
			cp = strchr(cp, '\0');
			mask &= ~1UL;
		}
		if (mask)
			sprintf(cp, " 0x%lx", mask);

		p += sprintf(p,
			     "processor  : %lu\n"
			     "vendor     : %s\n"
			     "family     : %s\n"
			     "model      : %s\n"
			     "revision   : %u\n"
			     "archrev    : %u\n"
			     "features   :%s\n"	/* don't change this---it _is_ right! */
			     "cpu number : %lu\n"
			     "cpu regs   : %u\n"
			     "cpu MHz    : %lu.%06lu\n"
			     "itc MHz    : %lu.%06lu\n"
			     "BogoMIPS   : %lu.%02lu\n\n",
			     c - cpu_data, c->vendor, family, model, c->revision, c->archrev,
			     features,
			     c->ppn, c->number, c->proc_freq / 1000000, c->proc_freq % 1000000,
			     c->itc_freq / 1000000, c->itc_freq % 1000000,
			     lpj*HZ/500000, (lpj*HZ/5000) % 100);
        }
	return p - buffer;
}

void
identify_cpu (struct cpuinfo_ia64 *c)
{
	union {
		unsigned long bits[5];
		struct {
			/* id 0 & 1: */
			char vendor[16];

			/* id 2 */
			u64 ppn;		/* processor serial number */

			/* id 3: */
			unsigned number		:  8;
			unsigned revision	:  8;
			unsigned model		:  8;
			unsigned family		:  8;
			unsigned archrev	:  8;
			unsigned reserved	: 24;

			/* id 4: */
			u64 features;
		} field;
	} cpuid;
	pal_vm_info_1_u_t vm1;
	pal_vm_info_2_u_t vm2;
	pal_status_t status;
	unsigned long impl_va_msb = 50, phys_addr_size = 44;	/* Itanium defaults */
	int i;

	for (i = 0; i < 5; ++i)
		cpuid.bits[i] = ia64_get_cpuid(i);

	memset(c, 0, sizeof(struct cpuinfo_ia64));

	memcpy(c->vendor, cpuid.field.vendor, 16);
	c->ppn = cpuid.field.ppn;
	c->number = cpuid.field.number;
	c->revision = cpuid.field.revision;
	c->model = cpuid.field.model;
	c->family = cpuid.field.family;
	c->archrev = cpuid.field.archrev;
	c->features = cpuid.field.features;

	status = ia64_pal_vm_summary(&vm1, &vm2);
	if (status == PAL_STATUS_SUCCESS) {
		impl_va_msb = vm2.pal_vm_info_2_s.impl_va_msb;
		phys_addr_size = vm1.pal_vm_info_1_s.phys_add_size;
	}
	printk("CPU %d: %lu virtual and %lu physical address bits\n",
	       smp_processor_id(), impl_va_msb + 1, phys_addr_size);
	c->unimpl_va_mask = ~((7L<<61) | ((1L << (impl_va_msb + 1)) - 1));
	c->unimpl_pa_mask = ~((1L<<63) | ((1L << phys_addr_size) - 1));

#ifdef CONFIG_IA64_SOFTSDV_HACKS
	/* BUG: SoftSDV doesn't support the cpuid registers. */
	if (c->vendor[0] == '\0') 
		memcpy(c->vendor, "Intel", 6);
#endif
}

/*
 * cpu_init() initializes state that is per-CPU.  This function acts
 * as a 'CPU state barrier', nothing should get across.
 */
void
cpu_init (void)
{
	extern void __init ia64_rid_init (void);
	extern void __init ia64_tlb_init (void);
	pal_vm_info_2_u_t vmi;
	unsigned int max_ctx;

	identify_cpu(&my_cpu_data);

	/* Clear the stack memory reserved for pt_regs: */
	memset(ia64_task_regs(current), 0, sizeof(struct pt_regs));

	/*
	 * Initialize default control register to defer all speculative faults.  The
	 * kernel MUST NOT depend on a particular setting of these bits (in other words,
	 * the kernel must have recovery code for all speculative accesses).
	 */
	ia64_set_dcr(  IA64_DCR_DM | IA64_DCR_DP | IA64_DCR_DK | IA64_DCR_DX | IA64_DCR_DR
		     | IA64_DCR_DA | IA64_DCR_DD);
#ifndef CONFIG_SMP
	ia64_set_fpu_owner(0);		/* initialize ar.k5 */
#endif

	atomic_inc(&init_mm.mm_count);
	current->active_mm = &init_mm;

	ia64_rid_init();
	ia64_tlb_init();

#ifdef	CONFIG_IA32_SUPPORT
	/* initialize global ia32 state - CR0 and CR4 */
	__asm__("mov ar.cflg = %0"
		: /* no outputs */
		: "r" (((ulong) IA32_CR4 << 32) | IA32_CR0));
#endif

#ifdef CONFIG_SMP
	normal_xtp();
#endif

	/* set ia64_ctx.max_rid to the maximum RID that is supported by all CPUs: */
	if (ia64_pal_vm_summary(NULL, &vmi) == 0)
		max_ctx = (1U << (vmi.pal_vm_info_2_s.rid_size - 3)) - 1;
	else {
		printk("ia64_rid_init: PAL VM summary failed, assuming 18 RID bits\n");
		max_ctx = (1U << 15) - 1;	/* use architected minimum */
	}
	while (max_ctx < ia64_ctx.max_ctx) {
		unsigned int old = ia64_ctx.max_ctx;
		if (cmpxchg(&ia64_ctx.max_ctx, old, max_ctx) == old)
			break;
	}
}
