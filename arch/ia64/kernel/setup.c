/*
 * Architecture-specific setup.
 *
 * Copyright (C) 1998-2001 Hewlett-Packard Co
 *	David Mosberger-Tang <davidm@hpl.hp.com>
 * Copyright (C) 1998, 1999, 2001 Stephane Eranian <eranian@hpl.hp.com>
 * Copyright (C) 2000, Rohit Seth <rohit.seth@intel.com>
 * Copyright (C) 1999 VA Linux Systems
 * Copyright (C) 1999 Walt Drummond <drummond@valinux.com>
 *
 * 11/12/01 D.Mosberger Convert get_cpuinfo() to seq_file based show_cpuinfo().
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
#include <linux/seq_file.h>
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

#if defined(CONFIG_SMP) && (IA64_CPU_SIZE > PAGE_SIZE)
# error "struct cpuinfo_ia64 too big!"
#endif

#define MIN(a,b)	((a) < (b) ? (a) : (b))
#define MAX(a,b)	((a) > (b) ? (a) : (b))

extern char _end;

#ifdef CONFIG_NUMA
 struct cpuinfo_ia64 *boot_cpu_data;
#else
 struct cpuinfo_ia64 _cpu_data[NR_CPUS] __attribute__ ((section ("__special_page_section")));
#endif

unsigned long ia64_cycles_per_usec;
struct ia64_boot_param *ia64_boot_param;
struct screen_info screen_info;

unsigned long ia64_iobase;	/* virtual address for I/O accesses */

#define COMMAND_LINE_SIZE	512

char saved_command_line[COMMAND_LINE_SIZE]; /* used in proc filesystem */

/*
 * Entries defined so far:
 * 	- boot param structure itself
 * 	- memory map
 * 	- initrd (optional)
 * 	- command line string
 * 	- kernel code & data
 *
 * More could be added if necessary
 */
#define IA64_MAX_RSVD_REGIONS 5

struct rsvd_region {
	unsigned long start;	/* virtual address of beginning of element */
	unsigned long end;	/* virtual address of end of element + 1 */
};

/*
 * We use a special marker for the end of memory and it uses the extra (+1) slot
 */
static struct rsvd_region rsvd_region[IA64_MAX_RSVD_REGIONS + 1];
static int num_rsvd_regions;

static unsigned long bootmap_start; /* physical address where the bootmem map is located */

static int
find_max_pfn (unsigned long start, unsigned long end, void *arg)
{
	unsigned long *max_pfn = arg, pfn;

	pfn = (PAGE_ALIGN(end - 1) - PAGE_OFFSET) >> PAGE_SHIFT;
	if (pfn > *max_pfn)
		*max_pfn = pfn;
	return 0;
}

#define IGNORE_PFN0	1	/* XXX fix me: ignore pfn 0 until TLB miss handler is updated... */

/*
 * Free available memory based on the primitive map created from
 * the boot parameters. This routine does not assume the incoming
 * segments are sorted.
 */
static int
free_available_memory (unsigned long start, unsigned long end, void *arg)
{
	unsigned long range_start, range_end, prev_start;
	int i;

#if IGNORE_PFN0
	if (start == PAGE_OFFSET) {
		printk("warning: skipping physical page 0\n");
		start += PAGE_SIZE;
		if (start >= end) return 0;
	}
#endif
	/*
	 * lowest possible address(walker uses virtual)
	 */
	prev_start = PAGE_OFFSET;

	for (i = 0; i < num_rsvd_regions; ++i) {
		range_start = MAX(start, prev_start);
		range_end   = MIN(end, rsvd_region[i].start);

		if (range_start < range_end)
			free_bootmem(__pa(range_start), range_end - range_start);

		/* nothing more available in this segment */
		if (range_end == end) return 0;

		prev_start = rsvd_region[i].end;
	}
	/* end of memory marker allows full processing inside loop body */
	return 0;
}


static int
find_bootmap_location (unsigned long start, unsigned long end, void *arg)
{
	unsigned long needed = *(unsigned long *)arg;
	unsigned long range_start, range_end, free_start;
	int i;

#if IGNORE_PFN0
	if (start == PAGE_OFFSET) {
		start += PAGE_SIZE;
		if (start >= end) return 0;
	}
#endif

	free_start = PAGE_OFFSET;

	for (i = 0; i < num_rsvd_regions; i++) {
		range_start = MAX(start, free_start);
		range_end   = MIN(end, rsvd_region[i].start);

		if (range_end <= range_start) continue;	/* skip over empty range */

	       	if (range_end - range_start >= needed) {
			bootmap_start = __pa(range_start);
			return 1;	/* done */
		}

		/* nothing more available in this segment */
		if (range_end == end) return 0;

		free_start = rsvd_region[i].end;
	}
	return 0;
}

static void
sort_regions (struct rsvd_region *rsvd_region, int max)
{
	int j;

	/* simple bubble sorting */
	while (max--) {
		for (j = 0; j < max; ++j) {
			if (rsvd_region[j].start > rsvd_region[j+1].start) {
				struct rsvd_region tmp;
				tmp = rsvd_region[j];
				rsvd_region[j] = rsvd_region[j + 1];
				rsvd_region[j + 1] = tmp;
			}
		}
	}
}

static void
find_memory (void)
{
#	define KERNEL_END	((unsigned long) &_end)
	unsigned long bootmap_size;
	unsigned long max_pfn;
	int n = 0;

	/*
	 * none of the entries in this table overlap
	 */
	rsvd_region[n].start = (unsigned long) ia64_boot_param;
	rsvd_region[n].end   = rsvd_region[n].start + sizeof(*ia64_boot_param);
	n++;

	rsvd_region[n].start = (unsigned long) __va(ia64_boot_param->efi_memmap);
	rsvd_region[n].end   = rsvd_region[n].start + ia64_boot_param->efi_memmap_size;
	n++;

	rsvd_region[n].start = (unsigned long) __va(ia64_boot_param->command_line);
	rsvd_region[n].end   = (rsvd_region[n].start
				+ strlen(__va(ia64_boot_param->command_line)) + 1);
	n++;

	rsvd_region[n].start = KERNEL_START;
	rsvd_region[n].end   = KERNEL_END;
	n++;

#ifdef CONFIG_BLK_DEV_INITRD
	if (ia64_boot_param->initrd_start) {
		rsvd_region[n].start = (unsigned long)__va(ia64_boot_param->initrd_start);
		rsvd_region[n].end   = rsvd_region[n].start + ia64_boot_param->initrd_size;
		n++;
	}
#endif

	/* end of memory marker */
	rsvd_region[n].start = ~0UL;
	rsvd_region[n].end   = ~0UL;
	n++;

	num_rsvd_regions = n;

	sort_regions(rsvd_region, num_rsvd_regions);

	/* first find highest page frame number */
	max_pfn = 0;
	efi_memmap_walk(find_max_pfn, &max_pfn);

	/* how many bytes to cover all the pages */
	bootmap_size = bootmem_bootmap_pages(max_pfn) << PAGE_SHIFT;

	/* look for a location to hold the bootmap */
	bootmap_start = ~0UL;
	efi_memmap_walk(find_bootmap_location, &bootmap_size);
	if (bootmap_start == ~0UL)
		panic("Cannot find %ld bytes for bootmap\n", bootmap_size);

	bootmap_size = init_bootmem(bootmap_start >> PAGE_SHIFT, max_pfn);

	/* Free all available memory, then mark bootmem-map as being in use.  */
	efi_memmap_walk(free_available_memory, 0);
	reserve_bootmem(bootmap_start, bootmap_size);

#ifdef CONFIG_BLK_DEV_INITRD
	if (ia64_boot_param->initrd_start) {
		initrd_start = (unsigned long)__va(ia64_boot_param->initrd_start);
		initrd_end   = initrd_start+ia64_boot_param->initrd_size;

		printk("Initial ramdisk at: 0x%lx (%lu bytes)\n",
		       initrd_start, ia64_boot_param->initrd_size);
	}
#endif
}

void __init
setup_arch (char **cmdline_p)
{
	extern unsigned long ia64_iobase;

	unw_init();

	*cmdline_p = __va(ia64_boot_param->command_line);
	strncpy(saved_command_line, *cmdline_p, sizeof(saved_command_line));
	saved_command_line[COMMAND_LINE_SIZE-1] = '\0';		/* for safety */

	efi_init();

	find_memory();

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

	/*
	 *  Set `iobase' to the appropriate address in region 6
	 *    (uncached access range)
	 *
	 *  The EFI memory map is the "prefered" location to get the I/O port
	 *  space base, rather the relying on AR.KR0. This should become more
	 *  clear in future SAL specs. We'll fall back to getting it out of
	 *  AR.KR0 if no appropriate entry is found in the memory map.
	 */
	ia64_iobase = efi_get_iobase();
	if (ia64_iobase)
		/* set AR.KR0 since this is all we use it for anyway */
		ia64_set_kr(IA64_KR_IO_BASE, ia64_iobase);
	else {
		ia64_iobase = ia64_get_kr(IA64_KR_IO_BASE);
		printk("No I/O port range found in EFI memory map, falling back to AR.KR0\n");
		printk("I/O port base = 0x%lx\n", ia64_iobase);
	}
	ia64_iobase = __IA64_UNCACHED_OFFSET | (ia64_iobase & ~PAGE_OFFSET);

#ifdef CONFIG_SMP
	cpu_physical_id(0) = hard_smp_processor_id();
#endif

	cpu_init();	/* initialize the bootstrap CPU */

#ifdef CONFIG_IA64_GENERIC
	machvec_init(acpi_get_sysname());
#endif

	if (efi.acpi20) {
		/* Parse the ACPI 2.0 tables */
		acpi20_parse(efi.acpi20);
	} else if (efi.acpi) {
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

	platform_setup(cmdline_p);
	paging_init();

	unw_create_gate_table();
}

/*
 * Display cpu info for all cpu's.
 */
static int
show_cpuinfo (struct seq_file *m, void *v)
{
#ifdef CONFIG_SMP
#	define lpj	c->loops_per_jiffy
#else
#	define lpj	loops_per_jiffy
#endif
	char family[32], features[128], *cp;
	struct cpuinfo_ia64 *c = v;
	unsigned long mask, cpu = c - cpu_data(0);

#ifdef CONFIG_SMP
	if (!(cpu_online_map & (1 << cpu)))
		return 0;
#endif

	mask = c->features;

	switch (c->family) {
	      case 0x07:	memcpy(family, "Itanium", 8); break;
	      case 0x1f:	memcpy(family, "McKinley", 9); break;
	      default:		sprintf(family, "%u", c->family); break;
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

	seq_printf(m,
		   "processor  : %lu\n"
		   "vendor     : %s\n"
		   "arch       : IA-64\n"
		   "family     : %s\n"
		   "model      : %u\n"
		   "revision   : %u\n"
		   "archrev    : %u\n"
		   "features   :%s\n"	/* don't change this---it _is_ right! */
		   "cpu number : %lu\n"
		   "cpu regs   : %u\n"
		   "cpu MHz    : %lu.%06lu\n"
		   "itc MHz    : %lu.%06lu\n"
		   "BogoMIPS   : %lu.%02lu\n\n",
		   cpu, c->vendor, family, c->model, c->revision, c->archrev,
		   features, c->ppn, c->number,
		   c->proc_freq / 1000000, c->proc_freq % 1000000,
		   c->itc_freq / 1000000, c->itc_freq % 1000000,
		   lpj*HZ/500000, (lpj*HZ/5000) % 100);
	return 0;
}

static void *
c_start (struct seq_file *m, loff_t *pos)
{
	return *pos < NR_CPUS ? cpu_data(*pos) : NULL;
}

static void *
c_next (struct seq_file *m, void *v, loff_t *pos)
{
	++*pos;
	return c_start(m, pos);
}

static void
c_stop (struct seq_file *m, void *v)
{
}

struct seq_operations cpuinfo_op = {
	start:	c_start,
	next:	c_next,
	stop:	c_stop,
	show:	show_cpuinfo
};

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
}

/*
 * cpu_init() initializes state that is per-CPU.  This function acts
 * as a 'CPU state barrier', nothing should get across.
 */
void
cpu_init (void)
{
	extern void __init ia64_mmu_init (void *);
	unsigned long num_phys_stacked;
	pal_vm_info_2_u_t vmi;
	unsigned int max_ctx;
	struct cpuinfo_ia64 *my_cpu_data;
#ifdef CONFIG_NUMA
	int cpu, order;

	/*
	 * If NUMA is configured, the cpu_data array is not preallocated. The boot cpu
	 * allocates entries for every possible cpu. As the remaining cpus come online,
	 * they reallocate a new cpu_data structure on their local node. This extra work
	 * is required because some boot code references all cpu_data structures
	 * before the cpus are actually started.
	 */
	if (!boot_cpu_data) {
		my_cpu_data = alloc_bootmem_pages_node(NODE_DATA(numa_node_id()),
						       sizeof(struct cpuinfo_ia64));
		boot_cpu_data = my_cpu_data;
		my_cpu_data->cpu_data[0] = my_cpu_data;
		for (cpu = 1; cpu < NR_CPUS; ++cpu)
			my_cpu_data->cpu_data[cpu]
				= alloc_bootmem_pages_node(NODE_DATA(numa_node_id()),
							   sizeof(struct cpuinfo_ia64));
		for (cpu = 1; cpu < NR_CPUS; ++cpu)
			memcpy(my_cpu_data->cpu_data[cpu]->cpu_data_ptrs,
			       my_cpu_data->cpu_data, sizeof(my_cpu_data->cpu_data));
	} else {
		order = get_order(sizeof(struct cpuinfo_ia64));
		my_cpu_data = page_address(alloc_pages_node(numa_node_id(), GFP_KERNEL, order));
		memcpy(my_cpu_data, boot_cpu_data->cpu_data[smp_processor_id()],
		       sizeof(struct cpuinfo_ia64));
		__free_pages(virt_to_page(boot_cpu_data->cpu_data[smp_processor_id()]),
			     order);
		for (cpu = 0; cpu < NR_CPUS; ++cpu)
			boot_cpu_data->cpu_data[cpu]->cpu_data[smp_processor_id()] = my_cpu_data;
	}
#else
	my_cpu_data = cpu_data(smp_processor_id());
#endif

	/*
	 * We can't pass "local_cpu_data" to identify_cpu() because we haven't called
	 * ia64_mmu_init() yet.  And we can't call ia64_mmu_init() first because it
	 * depends on the data returned by identify_cpu().  We break the dependency by
	 * accessing cpu_data() the old way, through identity mapped space.
	 */
	identify_cpu(my_cpu_data);

	/* Clear the stack memory reserved for pt_regs: */
	memset(ia64_task_regs(current), 0, sizeof(struct pt_regs));

	/*
	 * Initialize default control register to defer all speculative faults.  The
	 * kernel MUST NOT depend on a particular setting of these bits (in other words,
	 * the kernel must have recovery code for all speculative accesses).  Turn on
	 * dcr.lc as per recommendation by the architecture team.  Most IA-32 apps
	 * shouldn't be affected by this (moral: keep your ia32 locks aligned and you'll
	 * be fine).
	 */
	ia64_set_dcr(  IA64_DCR_DM | IA64_DCR_DP | IA64_DCR_DK | IA64_DCR_DX | IA64_DCR_DR
		     | IA64_DCR_DA | IA64_DCR_DD | IA64_DCR_LC);
#ifndef CONFIG_SMP
	ia64_set_fpu_owner(0);
#endif

	atomic_inc(&init_mm.mm_count);
	current->active_mm = &init_mm;

	ia64_mmu_init(my_cpu_data);

#ifdef CONFIG_IA32_SUPPORT
	/* initialize global ia32 state - CR0 and CR4 */
	asm volatile ("mov ar.cflg = %0" :: "r" (((ulong) IA32_CR4 << 32) | IA32_CR0));
#endif

	/* disable all local interrupt sources: */
	ia64_set_itv(1 << 16);
	ia64_set_lrr0(1 << 16);
	ia64_set_lrr1(1 << 16);
	ia64_set_pmv(1 << 16);
	ia64_set_cmcv(1 << 16);

	/* clear TPR & XTP to enable all interrupt classes: */
	ia64_set_tpr(0);
#ifdef CONFIG_SMP
	normal_xtp();
#endif

	/* set ia64_ctx.max_rid to the maximum RID that is supported by all CPUs: */
	if (ia64_pal_vm_summary(NULL, &vmi) == 0)
		max_ctx = (1U << (vmi.pal_vm_info_2_s.rid_size - 3)) - 1;
	else {
		printk("cpu_init: PAL VM summary failed, assuming 18 RID bits\n");
		max_ctx = (1U << 15) - 1;	/* use architected minimum */
	}
	while (max_ctx < ia64_ctx.max_ctx) {
		unsigned int old = ia64_ctx.max_ctx;
		if (cmpxchg(&ia64_ctx.max_ctx, old, max_ctx) == old)
			break;
	}

	if (ia64_pal_rse_info(&num_phys_stacked, 0) != 0) {
		printk ("cpu_init: PAL RSE info failed, assuming 96 physical stacked regs\n");
		num_phys_stacked = 96;
	}
	local_cpu_data->phys_stacked_size_p8 = num_phys_stacked*8 + 8;
}
