/*
 *  Copyright (C) 1995  Linus Torvalds
 *
 *  Support of BIGMEM added by Gerhard Wichert, Siemens AG, July 1999
 *
 *  Memory region support
 *	David Parsons <orc@pell.chi.il.us>, July-August 1999
 *
 *  Added E820 sanitization routine (removes overlapping memory regions);
 *  Brian Moyle <bmoyle@mvista.com>, February 2001
 *
 * Moved CPU detection code to cpu/${cpu}.c
 *    Patrick Mochel <mochel@osdl.org>, March 2002
 *
 *  Provisions for empty E820 memory regions (reported by certain BIOSes).
 *  Alex Achenbach <xela@slit.de>, December 2002.
 *
 */

/*
 * This file handles the architecture-dependent parts of initialization
 */

#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/mmzone.h>
#include <linux/screen_info.h>
#include <linux/ioport.h>
#include <linux/acpi.h>
#include <linux/apm_bios.h>
#include <linux/initrd.h>
#include <linux/bootmem.h>
#include <linux/seq_file.h>
#include <linux/console.h>
#include <linux/mca.h>
#include <linux/root_dev.h>
#include <linux/highmem.h>
#include <linux/module.h>
#include <linux/efi.h>
#include <linux/init.h>
#include <linux/edd.h>
#include <linux/nodemask.h>
#include <linux/kernel.h>
#include <linux/percpu.h>
#include <linux/notifier.h>
#include <linux/kexec.h>
#include <linux/crash_dump.h>
#include <linux/dmi.h>
#include <linux/pfn.h>
#include <linux/init_ohci1394_dma.h>

#include <video/edid.h>

#include <asm/apic.h>
#include <asm/e820.h>
#include <asm/mpspec.h>
#include <asm/mmzone.h>
#include <asm/setup.h>
#include <asm/arch_hooks.h>
#include <asm/sections.h>
#include <asm/io_apic.h>
#include <asm/ist.h>
#include <asm/io.h>
#include <asm/hypervisor.h>
#include <xen/interface/physdev.h>
#include <xen/interface/memory.h>
#include <xen/features.h>
#include <xen/firmware.h>
#include <xen/xencons.h>
#include <setup_arch.h>
#include <bios_ebda.h>
#include <asm/cacheflush.h>

#ifdef CONFIG_XEN
#include <xen/interface/kexec.h>
#endif

static int xen_panic_event(struct notifier_block *, unsigned long, void *);
static struct notifier_block xen_panic_block = {
	xen_panic_event, NULL, 0 /* try to go last */
};

extern char hypercall_page[PAGE_SIZE];
EXPORT_SYMBOL(hypercall_page);

int disable_pse __cpuinitdata = 0;

/*
 * Machine setup..
 */
extern struct resource code_resource;
extern struct resource data_resource;
extern struct resource bss_resource;

/* cpu data as detected by the assembly code in head.S */
struct cpuinfo_x86 new_cpu_data __cpuinitdata = { 0, 0, 0, 0, -1, 1, 0, 0, -1 };
/* common cpu data for all cpus */
struct cpuinfo_x86 boot_cpu_data __read_mostly = { 0, 0, 0, 0, -1, 1, 0, 0, -1 };
EXPORT_SYMBOL(boot_cpu_data);

unsigned long mmu_cr4_features;

/* for MCA, but anyone else can use it if they want */
unsigned int machine_id;
unsigned int machine_submodel_id;
unsigned int BIOS_revision;
unsigned int mca_pentium_flag;

/* Boot loader ID as an integer, for the benefit of proc_dointvec */
int bootloader_type;

/* user-defined highmem size */
static unsigned int highmem_pages = -1;

/*
 * Setup options
 */
struct screen_info screen_info;
EXPORT_SYMBOL(screen_info);
struct apm_info apm_info;
EXPORT_SYMBOL(apm_info);
struct edid_info edid_info;
EXPORT_SYMBOL_GPL(edid_info);
#ifndef CONFIG_XEN
#define copy_edid() (edid_info = boot_params.edid_info)
#endif
struct ist_info ist_info;
#if defined(CONFIG_X86_SPEEDSTEP_SMI) || \
	defined(CONFIG_X86_SPEEDSTEP_SMI_MODULE)
EXPORT_SYMBOL(ist_info);
#endif

extern void early_cpu_init(void);
extern int root_mountflags;

unsigned long saved_videomode;

#define RAMDISK_IMAGE_START_MASK  	0x07FF
#define RAMDISK_PROMPT_FLAG		0x8000
#define RAMDISK_LOAD_FLAG		0x4000	

static char __initdata command_line[COMMAND_LINE_SIZE];

struct boot_params __initdata boot_params;

/*
 * Point at the empty zero page to start with. We map the real shared_info
 * page as soon as fixmap is up and running.
 */
shared_info_t *HYPERVISOR_shared_info = (shared_info_t *)empty_zero_page;
EXPORT_SYMBOL(HYPERVISOR_shared_info);

unsigned long *phys_to_machine_mapping;
unsigned long *pfn_to_mfn_frame_list_list, *pfn_to_mfn_frame_list[16];
EXPORT_SYMBOL(phys_to_machine_mapping);

/* Raw start-of-day parameters from the hypervisor. */
start_info_t *xen_start_info;
EXPORT_SYMBOL(xen_start_info);

#if defined(CONFIG_EDD) || defined(CONFIG_EDD_MODULE)
struct edd edd;
#ifdef CONFIG_EDD_MODULE
EXPORT_SYMBOL(edd);
#endif
#ifndef CONFIG_XEN
/**
 * copy_edd() - Copy the BIOS EDD information
 *              from boot_params into a safe place.
 *
 */
static inline void copy_edd(void)
{
     memcpy(edd.mbr_signature, boot_params.edd_mbr_sig_buffer,
	    sizeof(edd.mbr_signature));
     memcpy(edd.edd_info, boot_params.eddbuf, sizeof(edd.edd_info));
     edd.mbr_signature_nr = boot_params.edd_mbr_sig_buf_entries;
     edd.edd_info_nr = boot_params.eddbuf_entries;
}
#endif
#else
static inline void copy_edd(void)
{
}
#endif

int __initdata user_defined_memmap = 0;

/*
 * "mem=nopentium" disables the 4MB page tables.
 * "mem=XXX[kKmM]" defines a memory region from HIGH_MEM
 * to <mem>, overriding the bios size.
 * "memmap=XXX[KkmM]@XXX[KkmM]" defines a memory region from
 * <start> to <start>+<mem>, overriding the bios size.
 *
 * HPA tells me bootloaders need to parse mem=, so no new
 * option should be mem=  [also see Documentation/i386/boot.txt]
 */
static int __init parse_mem(char *arg)
{
	if (!arg)
		return -EINVAL;

	if (strcmp(arg, "nopentium") == 0) {
		clear_bit(X86_FEATURE_PSE, boot_cpu_data.x86_capability);
		disable_pse = 1;
	} else {
		/* If the user specifies memory size, we
		 * limit the BIOS-provided memory map to
		 * that size. exactmap can be used to specify
		 * the exact map. mem=number can be used to
		 * trim the existing memory map.
		 */
		unsigned long long mem_size;
 
		mem_size = memparse(arg, &arg);
		limit_regions(mem_size);
		user_defined_memmap = 1;
	}
	return 0;
}
early_param("mem", parse_mem);

#ifdef CONFIG_PROC_VMCORE
/* elfcorehdr= specifies the location of elf core header
 * stored by the crashed kernel.
 */
static int __init parse_elfcorehdr(char *arg)
{
	if (!arg)
		return -EINVAL;

	elfcorehdr_addr = memparse(arg, &arg);
	return 0;
}
early_param("elfcorehdr", parse_elfcorehdr);
#endif /* CONFIG_PROC_VMCORE */

/*
 * highmem=size forces highmem to be exactly 'size' bytes.
 * This works even on boxes that have no highmem otherwise.
 * This also works to reduce highmem size on bigger boxes.
 */
static int __init parse_highmem(char *arg)
{
	if (!arg)
		return -EINVAL;

	highmem_pages = memparse(arg, &arg) >> PAGE_SHIFT;
	return 0;
}
early_param("highmem", parse_highmem);

/*
 * vmalloc=size forces the vmalloc area to be exactly 'size'
 * bytes. This can be used to increase (or decrease) the
 * vmalloc area - the default is 128m.
 */
static int __init parse_vmalloc(char *arg)
{
	if (!arg)
		return -EINVAL;

	__VMALLOC_RESERVE = memparse(arg, &arg);
	return 0;
}
early_param("vmalloc", parse_vmalloc);

#ifndef CONFIG_XEN
/*
 * reservetop=size reserves a hole at the top of the kernel address space which
 * a hypervisor can load into later.  Needed for dynamically loaded hypervisors,
 * so relocating the fixmap can be done before paging initialization.
 */
static int __init parse_reservetop(char *arg)
{
	unsigned long address;

	if (!arg)
		return -EINVAL;

	address = memparse(arg, &arg);
	reserve_top_address(address);
	return 0;
}
early_param("reservetop", parse_reservetop);
#endif

/*
 * Determine low and high memory ranges:
 */
unsigned long __init find_max_low_pfn(void)
{
	unsigned long max_low_pfn;

	max_low_pfn = max_pfn;
	if (max_low_pfn > MAXMEM_PFN) {
		if (highmem_pages == -1)
			highmem_pages = max_pfn - MAXMEM_PFN;
		if (highmem_pages + MAXMEM_PFN < max_pfn)
			max_pfn = MAXMEM_PFN + highmem_pages;
		if (highmem_pages + MAXMEM_PFN > max_pfn) {
			printk("only %luMB highmem pages available, ignoring highmem size of %uMB.\n", pages_to_mb(max_pfn - MAXMEM_PFN), pages_to_mb(highmem_pages));
			highmem_pages = 0;
		}
		max_low_pfn = MAXMEM_PFN;
#ifndef CONFIG_HIGHMEM
		/* Maximum memory usable is what is directly addressable */
		printk(KERN_WARNING "Warning only %ldMB will be used.\n",
					MAXMEM>>20);
		if (max_pfn > MAX_NONPAE_PFN)
			printk(KERN_WARNING "Use a HIGHMEM64G enabled kernel.\n");
		else
			printk(KERN_WARNING "Use a HIGHMEM enabled kernel.\n");
		max_pfn = MAXMEM_PFN;
#else /* !CONFIG_HIGHMEM */
#ifndef CONFIG_HIGHMEM64G
		if (max_pfn > MAX_NONPAE_PFN) {
			max_pfn = MAX_NONPAE_PFN;
			printk(KERN_WARNING "Warning only 4GB will be used.\n");
			printk(KERN_WARNING "Use a HIGHMEM64G enabled kernel.\n");
		}
#endif /* !CONFIG_HIGHMEM64G */
#endif /* !CONFIG_HIGHMEM */
	} else {
		if (highmem_pages == -1)
			highmem_pages = 0;
#ifdef CONFIG_HIGHMEM
		if (highmem_pages >= max_pfn) {
			printk(KERN_ERR "highmem size specified (%uMB) is bigger than pages available (%luMB)!.\n", pages_to_mb(highmem_pages), pages_to_mb(max_pfn));
			highmem_pages = 0;
		}
		if (highmem_pages) {
			if (max_low_pfn-highmem_pages < 64*1024*1024/PAGE_SIZE){
				printk(KERN_ERR "highmem size %uMB results in smaller than 64MB lowmem, ignoring it.\n", pages_to_mb(highmem_pages));
				highmem_pages = 0;
			}
			max_low_pfn -= highmem_pages;
		}
#else
		if (highmem_pages)
			printk(KERN_ERR "ignoring highmem size on non-highmem kernel!\n");
#endif
	}
	return max_low_pfn;
}

#ifndef CONFIG_XEN
/*
 * workaround for Dell systems that neglect to reserve EBDA
 */
static void __init reserve_ebda_region(void)
{
	unsigned int addr;
	addr = get_bios_ebda();
	if (addr)
		reserve_bootmem(addr, PAGE_SIZE);	
}
#endif

#ifndef CONFIG_NEED_MULTIPLE_NODES
void __init setup_bootmem_allocator(void);
static unsigned long __init setup_memory(void)
{
	/*
	 * partially used pages are not usable - thus
	 * we are rounding upwards:
	 */
 	min_low_pfn = PFN_UP(__pa(xen_start_info->pt_base)) +
		xen_start_info->nr_pt_frames;

	find_max_pfn();

	max_low_pfn = find_max_low_pfn();

#ifdef CONFIG_HIGHMEM
	highstart_pfn = highend_pfn = max_pfn;
	if (max_pfn > max_low_pfn) {
		highstart_pfn = max_low_pfn;
	}
	printk(KERN_NOTICE "%ldMB HIGHMEM available.\n",
		pages_to_mb(highend_pfn - highstart_pfn));
	num_physpages = highend_pfn;
	high_memory = (void *) __va(highstart_pfn * PAGE_SIZE - 1) + 1;
#else
	num_physpages = max_low_pfn;
	high_memory = (void *) __va(max_low_pfn * PAGE_SIZE - 1) + 1;
#endif
#ifdef CONFIG_FLATMEM
	max_mapnr = num_physpages;
#endif
	printk(KERN_NOTICE "%ldMB LOWMEM available.\n",
			pages_to_mb(max_low_pfn));

	setup_bootmem_allocator();

	return max_low_pfn;
}

void __init zone_sizes_init(void)
{
	unsigned long max_zone_pfns[MAX_NR_ZONES];
	memset(max_zone_pfns, 0, sizeof(max_zone_pfns));
	max_zone_pfns[ZONE_DMA] =
		virt_to_phys((char *)MAX_DMA_ADDRESS) >> PAGE_SHIFT;
	max_zone_pfns[ZONE_NORMAL] = max_low_pfn;
#ifdef CONFIG_HIGHMEM
	max_zone_pfns[ZONE_HIGHMEM] = highend_pfn;
	add_active_range(0, 0, highend_pfn);
#else
	add_active_range(0, 0, max_low_pfn);
#endif

	free_area_init_nodes(max_zone_pfns);
}
#else
extern unsigned long __init setup_memory(void);
extern void zone_sizes_init(void);
#endif /* !CONFIG_NEED_MULTIPLE_NODES */

static inline unsigned long long get_total_mem(void)
{
	unsigned long long total;

	total = max_low_pfn - min_low_pfn;
#ifdef CONFIG_HIGHMEM
	total += highend_pfn - highstart_pfn;
#endif

	return total << PAGE_SHIFT;
}

#ifdef CONFIG_KEXEC
#ifndef CONFIG_XEN
static void __init reserve_crashkernel(void)
{
	unsigned long long total_mem;
	unsigned long long crash_size, crash_base;
	int ret;

	total_mem = get_total_mem();

	ret = parse_crashkernel(boot_command_line, total_mem,
			&crash_size, &crash_base);
	if (ret == 0 && crash_size > 0) {
		if (crash_base > 0) {
			printk(KERN_INFO "Reserving %ldMB of memory at %ldMB "
					"for crashkernel (System RAM: %ldMB)\n",
					(unsigned long)(crash_size >> 20),
					(unsigned long)(crash_base >> 20),
					(unsigned long)(total_mem >> 20));
			crashk_res.start = crash_base;
			crashk_res.end   = crash_base + crash_size - 1;
			reserve_bootmem(crash_base, crash_size);
		} else
			printk(KERN_INFO "crashkernel reservation failed - "
					"you have to specify a base address\n");
	}
}
#else
#define reserve_crashkernel xen_machine_kexec_setup_resources
#endif
#else
static inline void __init reserve_crashkernel(void)
{}
#endif

void __init setup_bootmem_allocator(void)
{
	unsigned long bootmap_size;
	/*
	 * Initialize the boot-time allocator (with low memory only):
	 */
	bootmap_size = init_bootmem(min_low_pfn, max_low_pfn);

	register_bootmem_low_pages(max_low_pfn);

	/*
	 * Reserve the bootmem bitmap itself as well. We do this in two
	 * steps (first step was init_bootmem()) because this catches
	 * the (very unlikely) case of us accidentally initializing the
	 * bootmem allocator with an invalid RAM area.
	 */
	reserve_bootmem(__pa_symbol(_text), (PFN_PHYS(min_low_pfn) +
			 bootmap_size + PAGE_SIZE-1) - __pa_symbol(_text));

#ifndef CONFIG_XEN
	/*
	 * reserve physical page 0 - it's a special BIOS page on many boxes,
	 * enabling clean reboots, SMP operation, laptop functions.
	 */
	reserve_bootmem(0, PAGE_SIZE);

	/* reserve EBDA region, it's a 4K region */
	reserve_ebda_region();

    /* could be an AMD 768MPX chipset. Reserve a page  before VGA to prevent
       PCI prefetch into it (errata #56). Usually the page is reserved anyways,
       unless you have no PS/2 mouse plugged in. */
	if (boot_cpu_data.x86_vendor == X86_VENDOR_AMD &&
	    boot_cpu_data.x86 == 6)
	     reserve_bootmem(0xa0000 - 4096, 4096);

#ifdef CONFIG_SMP
	/*
	 * But first pinch a few for the stack/trampoline stuff
	 * FIXME: Don't need the extra page at 4K, but need to fix
	 * trampoline before removing it. (see the GDT stuff)
	 */
	reserve_bootmem(PAGE_SIZE, PAGE_SIZE);
#endif
#ifdef CONFIG_ACPI_SLEEP
	/*
	 * Reserve low memory region for sleep support.
	 */
	acpi_reserve_bootmem();
#endif
	numa_kva_reserve();
#endif /* !CONFIG_XEN */

#ifdef CONFIG_BLK_DEV_INITRD
	if (xen_start_info->mod_start) {
		unsigned long ramdisk_image = __pa(xen_start_info->mod_start);
		unsigned long ramdisk_size  = xen_start_info->mod_len;
		unsigned long ramdisk_end   = ramdisk_image + ramdisk_size;
		unsigned long end_of_lowmem = max_low_pfn << PAGE_SHIFT;

		if (ramdisk_end <= end_of_lowmem) {
			/*reserve_bootmem(ramdisk_image, ramdisk_size);*/
			initrd_start = ramdisk_image + PAGE_OFFSET;
			initrd_end = initrd_start+ramdisk_size;
			initrd_below_start_ok = 1;
		} else {
			printk(KERN_ERR "initrd extends beyond end of memory "
			       "(0x%08lx > 0x%08lx)\ndisabling initrd\n",
			       ramdisk_end, end_of_lowmem);
			initrd_start = 0;
		}
	}
#endif
	reserve_crashkernel();
}

/*
 * The node 0 pgdat is initialized before all of these because
 * it's needed for bootmem.  node>0 pgdats have their virtual
 * space allocated before the pagetables are in place to access
 * them, so they can't be cleared then.
 *
 * This should all compile down to nothing when NUMA is off.
 */
static void __init remapped_pgdat_init(void)
{
	int nid;

	for_each_online_node(nid) {
		if (nid != 0)
			memset(NODE_DATA(nid), 0, sizeof(struct pglist_data));
	}
}

#ifdef CONFIG_MCA
static void set_mca_bus(int x)
{
	MCA_bus = x;
}
#else
static void set_mca_bus(int x) { }
#endif

/* Overridden in paravirt.c if CONFIG_PARAVIRT */
char * __init __attribute__((weak)) memory_setup(void)
{
	return machine_specific_memory_setup();
}

/*
 * Determine if we were loaded by an EFI loader.  If so, then we have also been
 * passed the efi memmap, systab, etc., so we should use these data structures
 * for initialization.  Note, the efi init code path is determined by the
 * global efi_enabled. This allows the same kernel image to be used on existing
 * systems (with a traditional BIOS) as well as on EFI systems.
 */
void __init setup_arch(char **cmdline_p)
{
	int i, j, k, fpp;
	struct physdev_set_iopl set_iopl;
	unsigned long max_low_pfn;
	unsigned long p2m_pages;

	/* Force a quick death if the kernel panics (not domain 0). */
	extern int panic_timeout;
	if (!is_initial_xendomain()) {
		if (!panic_timeout)
			panic_timeout = 1;

		/* Register a call for panic conditions. */
		atomic_notifier_chain_register(&panic_notifier_list, &xen_panic_block);
	}

	WARN_ON(HYPERVISOR_vm_assist(VMASST_CMD_enable,
				     VMASST_TYPE_4gb_segments));
	WARN_ON(HYPERVISOR_vm_assist(VMASST_CMD_enable,
				     VMASST_TYPE_writable_pagetables));

	memcpy(&boot_cpu_data, &new_cpu_data, sizeof(new_cpu_data));
	early_cpu_init();
#ifdef CONFIG_SMP
	prefill_possible_map();
#endif

	/*
	 * FIXME: This isn't an official loader_type right
	 * now but does currently work with elilo.
	 * If we were configured as an EFI kernel, check to make
	 * sure that we were loaded correctly from elilo and that
	 * the system table is valid.  If not, then initialize normally.
	 */
#ifdef CONFIG_EFI
	if ((boot_params.hdr.type_of_loader == 0x50) &&
	    boot_params.efi_info.efi_systab)
		efi_enabled = 1;
#endif

	/* This must be initialized to UNNAMED_MAJOR for ipconfig to work
	   properly.  Setting ROOT_DEV to default to /dev/ram0 breaks initrd.
	*/
	ROOT_DEV = MKDEV(UNNAMED_MAJOR,0);
	screen_info = boot_params.screen_info;
	copy_edid();
	apm_info.bios = boot_params.apm_bios_info;
	ist_info = boot_params.ist_info;
	saved_videomode = boot_params.hdr.vid_mode;
	if( boot_params.sys_desc_table.length != 0 ) {
		set_mca_bus(boot_params.sys_desc_table.table[3] & 0x2);
		machine_id = boot_params.sys_desc_table.table[0];
		machine_submodel_id = boot_params.sys_desc_table.table[1];
		BIOS_revision = boot_params.sys_desc_table.table[2];
	}
	bootloader_type = boot_params.hdr.type_of_loader;

	if (is_initial_xendomain()) {
		const struct dom0_vga_console_info *info =
			(void *)((char *)xen_start_info +
			         xen_start_info->console.dom0.info_off);

		dom0_init_screen_info(info,
		                      xen_start_info->console.dom0.info_size);
		xen_start_info->console.domU.mfn = 0;
		xen_start_info->console.domU.evtchn = 0;
	} else
		screen_info.orig_video_isVGA = 0;

#ifdef CONFIG_BLK_DEV_RAM
	rd_image_start = boot_params.hdr.ram_size & RAMDISK_IMAGE_START_MASK;
	rd_prompt = ((boot_params.hdr.ram_size & RAMDISK_PROMPT_FLAG) != 0);
	rd_doload = ((boot_params.hdr.ram_size & RAMDISK_LOAD_FLAG) != 0);
#endif

	xen_setup_features();

	ARCH_SETUP
	if (efi_enabled)
		efi_init();
	else {
		printk(KERN_INFO "BIOS-provided physical RAM map:\n");
		print_memory_map(memory_setup());
	}

	copy_edd();

	if (!boot_params.hdr.root_flags)
		root_mountflags &= ~MS_RDONLY;
	init_mm.start_code = (unsigned long) _text;
	init_mm.end_code = (unsigned long) _etext;
	init_mm.end_data = (unsigned long) _edata;
	init_mm.brk = (PFN_UP(__pa(xen_start_info->pt_base)) +
		       xen_start_info->nr_pt_frames) << PAGE_SHIFT;

	code_resource.start = virt_to_phys(_text);
	code_resource.end = virt_to_phys(_etext)-1;
	data_resource.start = virt_to_phys(_etext);
	data_resource.end = virt_to_phys(_edata)-1;
	bss_resource.start = virt_to_phys(&__bss_start);
	bss_resource.end = virt_to_phys(&__bss_stop)-1;

	if ((i = MAX_GUEST_CMDLINE) > COMMAND_LINE_SIZE)
		i = COMMAND_LINE_SIZE;
	memcpy(boot_command_line, xen_start_info->cmd_line, i);
	boot_command_line[i - 1] = '\0';
	parse_early_param();

	if (user_defined_memmap) {
		printk(KERN_INFO "user-defined physical RAM map:\n");
		print_memory_map("user");
	}

	strlcpy(command_line, boot_command_line, COMMAND_LINE_SIZE);
	*cmdline_p = command_line;

	max_low_pfn = setup_memory();

#ifdef CONFIG_VMI
	/*
	 * Must be after max_low_pfn is determined, and before kernel
	 * pagetables are setup.
	 */
	vmi_init();
#endif

	/*
	 * NOTE: before this point _nobody_ is allowed to allocate
	 * any memory using the bootmem allocator.  Although the
	 * allocator is now initialised only the first 8Mb of the kernel
	 * virtual address space has been mapped.  All allocations before
	 * paging_init() has completed must use the alloc_bootmem_low_pages()
	 * variant (which allocates DMA'able memory) and care must be taken
	 * not to exceed the 8Mb limit.
	 */

#ifdef CONFIG_SMP
	smp_alloc_memory(); /* AP processor realmode stacks in low memory*/
#endif
	paging_init();

	/*
	 * NOTE: On x86-32, only from this point on, fixmaps are ready for use.
	 */

#ifdef CONFIG_PROVIDE_OHCI1394_DMA_INIT
	if (init_ohci1394_dma_early)
		init_ohci1394_dma_on_all_controllers();
#endif

	remapped_pgdat_init();
	sparse_init();
	zone_sizes_init();

#ifdef CONFIG_X86_FIND_SMP_CONFIG
	/*
	 * Find and reserve possible boot-time SMP configuration:
	 */
	find_smp_config();
#endif

	p2m_pages = max_pfn;
	if (xen_start_info->nr_pages > max_pfn) {
		/*
		 * the max_pfn was shrunk (probably by mem= or highmem=
		 * kernel parameter); shrink reservation with the HV
		 */
		struct xen_memory_reservation reservation = {
			.address_bits = 0,
			.extent_order = 0,
			.domid = DOMID_SELF
		};
		unsigned int difference;
		int ret;

		difference = xen_start_info->nr_pages - max_pfn;

		set_xen_guest_handle(reservation.extent_start,
				     ((unsigned long *)xen_start_info->mfn_list) + max_pfn);
		reservation.nr_extents = difference;
		ret = HYPERVISOR_memory_op(XENMEM_decrease_reservation,
					   &reservation);
		BUG_ON (ret != difference);
	}
	else if (max_pfn > xen_start_info->nr_pages)
		p2m_pages = xen_start_info->nr_pages;

	/* Make sure we have a correctly sized P->M table. */
	if (!xen_feature(XENFEAT_auto_translated_physmap)) {
		phys_to_machine_mapping = alloc_bootmem_low_pages(
		     max_pfn * sizeof(unsigned long));
		memset(phys_to_machine_mapping, ~0,
		       max_pfn * sizeof(unsigned long));
		memcpy(phys_to_machine_mapping,
		       (unsigned long *)xen_start_info->mfn_list,
		       p2m_pages * sizeof(unsigned long));
		free_bootmem(
		     __pa(xen_start_info->mfn_list),
		     PFN_PHYS(PFN_UP(xen_start_info->nr_pages *
				     sizeof(unsigned long))));

		/*
		 * Initialise the list of the frames that specify the list of
		 * frames that make up the p2m table. Used by save/restore
		 */
		pfn_to_mfn_frame_list_list = alloc_bootmem_low_pages(PAGE_SIZE);

		fpp = PAGE_SIZE/sizeof(unsigned long);
		for (i=0, j=0, k=-1; i< max_pfn; i+=fpp, j++) {
			if ((j % fpp) == 0) {
				k++;
				BUG_ON(k>=16);
				pfn_to_mfn_frame_list[k] =
					alloc_bootmem_low_pages(PAGE_SIZE);
				pfn_to_mfn_frame_list_list[k] =
					virt_to_mfn(pfn_to_mfn_frame_list[k]);
				j=0;
			}
			pfn_to_mfn_frame_list[k][j] =
				virt_to_mfn(&phys_to_machine_mapping[i]);
		}
		HYPERVISOR_shared_info->arch.max_pfn = max_pfn;
		HYPERVISOR_shared_info->arch.pfn_to_mfn_frame_list_list =
		     virt_to_mfn(pfn_to_mfn_frame_list_list);
	}

	/* Mark all ISA DMA channels in-use - using them wouldn't work. */
	for (i = 0; i < MAX_DMA_CHANNELS; ++i)
		if (i != 4 && request_dma(i, "xen") != 0)
			BUG();

	/*
	 * NOTE: at this point the bootmem allocator is fully available.
	 */

	paravirt_post_allocator_init();

	if (is_initial_xendomain())
		dmi_scan_machine();

#ifdef CONFIG_X86_GENERICARCH
	generic_apic_probe();
#endif	
	if (efi_enabled)
		efi_map_memmap();

	set_iopl.iopl = 1;
	WARN_ON(HYPERVISOR_physdev_op(PHYSDEVOP_set_iopl, &set_iopl));

#ifdef CONFIG_ACPI
	if (!is_initial_xendomain()) {
		printk(KERN_INFO "ACPI in unprivileged domain disabled\n");
		acpi_disabled = 1;
		acpi_ht = 0;
	}

	/*
	 * Parse the ACPI tables for possible boot-time SMP configuration.
	 */
	acpi_boot_table_init();
#endif

#if defined(CONFIG_PCI) && !defined(CONFIG_XEN)
	early_quirks();
#endif

#ifdef CONFIG_ACPI
	acpi_boot_init();

#if defined(CONFIG_SMP) && defined(CONFIG_X86_PC)
	if (def_to_bigsmp)
		printk(KERN_WARNING "More than 8 CPUs detected and "
			"CONFIG_X86_PC cannot handle it.\nUse "
			"CONFIG_X86_GENERICARCH or CONFIG_X86_BIGSMP.\n");
#endif
#endif
#ifdef CONFIG_X86_LOCAL_APIC
	if (smp_found_config)
		get_smp_config();
#endif

	e820_register_memory();
	e820_mark_nosave_regions();

	if (is_initial_xendomain()) {
#ifdef CONFIG_VT
#if defined(CONFIG_VGA_CONSOLE)
		if (!efi_enabled ||
		    (efi_mem_type(0xa0000) != EFI_CONVENTIONAL_MEMORY))
			conswitchp = &vga_con;
#elif defined(CONFIG_DUMMY_CONSOLE)
		conswitchp = &dummy_con;
#endif
#endif
	} else {
#if defined(CONFIG_VT) && defined(CONFIG_DUMMY_CONSOLE)
		conswitchp = &dummy_con;
#endif
	}
}

static int
xen_panic_event(struct notifier_block *this, unsigned long event, void *ptr)
{
	HYPERVISOR_shutdown(SHUTDOWN_crash);
	/* we're never actually going to get here... */
	return NOTIFY_DONE;
}
