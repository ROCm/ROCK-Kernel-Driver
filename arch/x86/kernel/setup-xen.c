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
#include <linux/sfi.h>
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
#include <linux/iscsi_ibft.h>
#include <linux/nodemask.h>
#include <linux/kexec.h>
#include <linux/dmi.h>
#include <linux/pfn.h>
#include <linux/pci.h>
#include <asm/pci-direct.h>
#include <linux/init_ohci1394_dma.h>
#include <linux/kvm_para.h>

#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/stddef.h>
#include <linux/unistd.h>
#include <linux/ptrace.h>
#include <linux/slab.h>
#include <linux/user.h>
#include <linux/delay.h>

#include <linux/kallsyms.h>
#include <linux/cpufreq.h>
#include <linux/dma-mapping.h>
#include <linux/ctype.h>
#include <linux/uaccess.h>

#include <linux/percpu.h>
#include <linux/crash_dump.h>
#include <linux/tboot.h>

#include <video/edid.h>

#include <asm/mtrr.h>
#include <asm/apic.h>
#include <asm/e820.h>
#include <asm/mpspec.h>
#include <asm/setup.h>
#include <asm/efi.h>
#include <asm/timer.h>
#include <asm/i8259.h>
#include <asm/sections.h>
#include <asm/dmi.h>
#include <asm/io_apic.h>
#include <asm/ist.h>
#include <asm/vmi.h>
#include <asm/setup_arch.h>
#include <asm/bios_ebda.h>
#include <asm/cacheflush.h>
#include <asm/processor.h>
#include <asm/bugs.h>

#include <asm/system.h>
#include <asm/vsyscall.h>
#include <asm/cpu.h>
#include <asm/desc.h>
#include <asm/dma.h>
#include <asm/iommu.h>
#include <asm/gart.h>
#include <asm/mmu_context.h>
#include <asm/proto.h>

#include <asm/paravirt.h>
#include <asm/hypervisor.h>

#include <asm/percpu.h>
#include <asm/topology.h>
#include <asm/apicdef.h>
#ifdef CONFIG_X86_64
#include <asm/numa_64.h>
#endif

#ifdef CONFIG_XEN
#include <asm/hypervisor.h>
#include <xen/interface/kexec.h>
#include <xen/interface/memory.h>
#include <xen/interface/nmi.h>
#include <xen/interface/physdev.h>
#include <xen/features.h>
#include <xen/firmware.h>
#include <xen/xencons.h>

static int xen_panic_event(struct notifier_block *, unsigned long, void *);
static struct notifier_block xen_panic_block = {
	xen_panic_event, NULL, 0 /* try to go last */
};

unsigned long *phys_to_machine_mapping;
EXPORT_SYMBOL(phys_to_machine_mapping);

unsigned long *pfn_to_mfn_frame_list_list, **pfn_to_mfn_frame_list;

/* Raw start-of-day parameters from the hypervisor. */
start_info_t *xen_start_info;
EXPORT_SYMBOL(xen_start_info);
#endif

/*
 * end_pfn only includes RAM, while max_pfn_mapped includes all e820 entries.
 * The direct mapping extends to max_pfn_mapped, so that we can directly access
 * apertures, ACPI and other tables without having to play with fixmaps.
 */
unsigned long max_low_pfn_mapped;
unsigned long max_pfn_mapped;

RESERVE_BRK(dmi_alloc, 65536);

unsigned int boot_cpu_id __read_mostly;

static __initdata unsigned long _brk_start = (unsigned long)__brk_base;
unsigned long _brk_end = (unsigned long)__brk_base;

#ifndef CONFIG_XEN
#ifdef CONFIG_X86_64
int default_cpu_present_to_apicid(int mps_cpu)
{
	return __default_cpu_present_to_apicid(mps_cpu);
}

int default_check_phys_apicid_present(int phys_apicid)
{
	return __default_check_phys_apicid_present(phys_apicid);
}
#endif

#ifndef CONFIG_DEBUG_BOOT_PARAMS
struct boot_params __initdata boot_params;
#else
struct boot_params boot_params;
#endif
#endif

/*
 * Machine setup..
 */
static struct resource data_resource = {
	.name	= "Kernel data",
	.start	= 0,
	.end	= 0,
	.flags	= IORESOURCE_BUSY | IORESOURCE_MEM
};

static struct resource code_resource = {
	.name	= "Kernel code",
	.start	= 0,
	.end	= 0,
	.flags	= IORESOURCE_BUSY | IORESOURCE_MEM
};

static struct resource bss_resource = {
	.name	= "Kernel bss",
	.start	= 0,
	.end	= 0,
	.flags	= IORESOURCE_BUSY | IORESOURCE_MEM
};


#ifdef CONFIG_X86_32
/* cpu data as detected by the assembly code in head.S */
struct cpuinfo_x86 new_cpu_data __cpuinitdata = {0, 0, 0, 0, -1, 1, 0, 0, -1};
/* common cpu data for all cpus */
struct cpuinfo_x86 boot_cpu_data __read_mostly = {0, 0, 0, 0, -1, 1, 0, 0, -1};
EXPORT_SYMBOL(boot_cpu_data);
#ifndef CONFIG_XEN
static void set_mca_bus(int x)
{
#ifdef CONFIG_MCA
	MCA_bus = x;
#endif
}

unsigned int def_to_bigsmp;

/* for MCA, but anyone else can use it if they want */
unsigned int machine_id;
unsigned int machine_submodel_id;
unsigned int BIOS_revision;

struct apm_info apm_info;
EXPORT_SYMBOL(apm_info);
#endif

#if defined(CONFIG_X86_SPEEDSTEP_SMI_MODULE)
struct ist_info ist_info;
EXPORT_SYMBOL(ist_info);
#elif defined(CONFIG_X86_SPEEDSTEP_SMI)
struct ist_info ist_info;
#endif

#else
struct cpuinfo_x86 boot_cpu_data __read_mostly = {
	.x86_phys_bits = MAX_PHYSMEM_BITS,
};
EXPORT_SYMBOL(boot_cpu_data);
#endif


#if !defined(CONFIG_X86_PAE) || defined(CONFIG_X86_64)
unsigned long mmu_cr4_features;
#else
unsigned long mmu_cr4_features = X86_CR4_PAE;
#endif

/* Boot loader ID and version as integers, for the benefit of proc_dointvec */
int bootloader_type, bootloader_version;

/*
 * Setup options
 */
struct screen_info screen_info;
EXPORT_SYMBOL(screen_info);
struct edid_info edid_info;
EXPORT_SYMBOL_GPL(edid_info);

extern int root_mountflags;

unsigned long saved_video_mode;

#define RAMDISK_IMAGE_START_MASK	0x07FF
#define RAMDISK_PROMPT_FLAG		0x8000
#define RAMDISK_LOAD_FLAG		0x4000

static char __initdata command_line[COMMAND_LINE_SIZE];
#ifdef CONFIG_CMDLINE_BOOL
static char __initdata builtin_cmdline[COMMAND_LINE_SIZE] = CONFIG_CMDLINE;
#endif

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

void * __init extend_brk(size_t size, size_t align)
{
	size_t mask = align - 1;
	void *ret;

	BUG_ON(_brk_start == 0);
	BUG_ON(align & mask);

	_brk_end = (_brk_end + mask) & ~mask;
	BUG_ON((char *)(_brk_end + size) > __brk_limit);

	ret = (void *)_brk_end;
	_brk_end += size;

	memset(ret, 0, size);

	return ret;
}

#if defined(CONFIG_X86_64) && !defined(CONFIG_XEN)
static void __init init_gbpages(void)
{
	if (direct_gbpages && cpu_has_gbpages)
		printk(KERN_INFO "Using GB pages for direct mapping\n");
	else
		direct_gbpages = 0;
}
#else
static inline void init_gbpages(void)
{
}
#endif

static void __init reserve_brk(void)
{
	if (_brk_end > _brk_start)
		reserve_early(__pa(_brk_start), __pa(_brk_end), "BRK");

	/* Mark brk area as locked down and no longer taking any
	   new allocations */
	_brk_start = 0;
}

#ifdef CONFIG_BLK_DEV_INITRD

#define MAX_MAP_CHUNK	(NR_FIX_BTMAPS << PAGE_SHIFT)
static void __init relocate_initrd(void)
{
#ifndef CONFIG_XEN
	u64 ramdisk_image = boot_params.hdr.ramdisk_image;
	u64 ramdisk_size  = boot_params.hdr.ramdisk_size;
	u64 end_of_lowmem = max_low_pfn_mapped << PAGE_SHIFT;
	u64 ramdisk_here;
	unsigned long slop, clen, mapaddr;
	char *p, *q;

	/* We need to move the initrd down into lowmem */
	ramdisk_here = find_e820_area(0, end_of_lowmem, ramdisk_size,
					 PAGE_SIZE);

	if (ramdisk_here == -1ULL)
		panic("Cannot find place for new RAMDISK of size %lld\n",
			 ramdisk_size);

	/* Note: this includes all the lowmem currently occupied by
	   the initrd, we rely on that fact to keep the data intact. */
	reserve_early(ramdisk_here, ramdisk_here + ramdisk_size,
			 "NEW RAMDISK");
	initrd_start = ramdisk_here + PAGE_OFFSET;
	initrd_end   = initrd_start + ramdisk_size;
	printk(KERN_INFO "Allocated new RAMDISK: %08llx - %08llx\n",
			 ramdisk_here, ramdisk_here + ramdisk_size);

	q = (char *)initrd_start;

	/* Copy any lowmem portion of the initrd */
	if (ramdisk_image < end_of_lowmem) {
		clen = end_of_lowmem - ramdisk_image;
		p = (char *)__va(ramdisk_image);
		memcpy(q, p, clen);
		q += clen;
		ramdisk_image += clen;
		ramdisk_size  -= clen;
	}

	/* Copy the highmem portion of the initrd */
	while (ramdisk_size) {
		slop = ramdisk_image & ~PAGE_MASK;
		clen = ramdisk_size;
		if (clen > MAX_MAP_CHUNK-slop)
			clen = MAX_MAP_CHUNK-slop;
		mapaddr = ramdisk_image & PAGE_MASK;
		p = early_memremap(mapaddr, clen+slop);
		memcpy(q, p+slop, clen);
		early_iounmap(p, clen+slop);
		q += clen;
		ramdisk_image += clen;
		ramdisk_size  -= clen;
	}
	/* high pages is not converted by early_res_to_bootmem */
	ramdisk_image = boot_params.hdr.ramdisk_image;
	ramdisk_size  = boot_params.hdr.ramdisk_size;
	printk(KERN_INFO "Move RAMDISK from %016llx - %016llx to"
		" %08llx - %08llx\n",
		ramdisk_image, ramdisk_image + ramdisk_size - 1,
		ramdisk_here, ramdisk_here + ramdisk_size - 1);
#else
	printk(KERN_ERR "initrd extends beyond end of memory "
	       "(0x%08lx > 0x%08lx)\ndisabling initrd\n",
	       __pa(xen_start_info->mod_start) + xen_start_info->mod_len,
	       max_low_pfn_mapped << PAGE_SHIFT);
	initrd_start = 0;
#endif
}

static void __init reserve_initrd(void)
{
#ifndef CONFIG_XEN
	u64 ramdisk_image = boot_params.hdr.ramdisk_image;
	u64 ramdisk_size  = boot_params.hdr.ramdisk_size;
	u64 ramdisk_end   = ramdisk_image + ramdisk_size;
	u64 end_of_lowmem = max_low_pfn_mapped << PAGE_SHIFT;

	if (!boot_params.hdr.type_of_loader ||
	    !ramdisk_image || !ramdisk_size)
		return;		/* No initrd provided by bootloader */
#else
	unsigned long ramdisk_image = __pa(xen_start_info->mod_start);
	unsigned long ramdisk_size  = xen_start_info->mod_len;
	unsigned long ramdisk_end   = ramdisk_image + ramdisk_size;
	unsigned long end_of_lowmem = max_low_pfn_mapped << PAGE_SHIFT;

	if (!xen_start_info->mod_start || !ramdisk_size)
		return;		/* No initrd provided by bootloader */
#endif

	initrd_start = 0;

	if (ramdisk_size >= (end_of_lowmem>>1)) {
		free_early(ramdisk_image, ramdisk_end);
		printk(KERN_ERR "initrd too large to handle, "
		       "disabling initrd\n");
		return;
	}

	printk(KERN_INFO "RAMDISK: %08lx - %08lx\n", ramdisk_image,
			ramdisk_end);


	if (ramdisk_end <= end_of_lowmem) {
		/* All in lowmem, easy case */
		/*
		 * don't need to reserve again, already reserved early
		 * in i386_start_kernel
		 */
		initrd_start = ramdisk_image + PAGE_OFFSET;
		initrd_end = initrd_start + ramdisk_size;
#ifdef CONFIG_X86_64_XEN
		initrd_below_start_ok = 1;
#endif
		return;
	}

	relocate_initrd();

	free_early(ramdisk_image, ramdisk_end);
}
#else
static void __init reserve_initrd(void)
{
}
#endif /* CONFIG_BLK_DEV_INITRD */

static void __init parse_setup_data(void)
{
#ifndef CONFIG_XEN
	struct setup_data *data;
	u64 pa_data;

	if (boot_params.hdr.version < 0x0209)
		return;
	pa_data = boot_params.hdr.setup_data;
	while (pa_data) {
		data = early_memremap(pa_data, PAGE_SIZE);
		switch (data->type) {
		case SETUP_E820_EXT:
			parse_e820_ext(data, pa_data);
			break;
		default:
			break;
		}
		pa_data = data->next;
		early_iounmap(data, PAGE_SIZE);
	}
#endif
}

static void __init e820_reserve_setup_data(void)
{
#ifndef CONFIG_XEN
	struct setup_data *data;
	u64 pa_data;
	int found = 0;

	if (boot_params.hdr.version < 0x0209)
		return;
	pa_data = boot_params.hdr.setup_data;
	while (pa_data) {
		data = early_memremap(pa_data, sizeof(*data));
		e820_update_range(pa_data, sizeof(*data)+data->len,
			 E820_RAM, E820_RESERVED_KERN);
		found = 1;
		pa_data = data->next;
		early_iounmap(data, sizeof(*data));
	}
	if (!found)
		return;

	sanitize_e820_map(e820.map, ARRAY_SIZE(e820.map), &e820.nr_map);
	memcpy(&e820_saved, &e820, sizeof(struct e820map));
	printk(KERN_INFO "extended physical RAM map:\n");
	e820_print_map("reserve setup_data");
#endif
}

static void __init reserve_early_setup_data(void)
{
#ifndef CONFIG_XEN
	struct setup_data *data;
	u64 pa_data;
	char buf[32];

	if (boot_params.hdr.version < 0x0209)
		return;
	pa_data = boot_params.hdr.setup_data;
	while (pa_data) {
		data = early_memremap(pa_data, sizeof(*data));
		sprintf(buf, "setup data %x", data->type);
		reserve_early(pa_data, pa_data+sizeof(*data)+data->len, buf);
		pa_data = data->next;
		early_iounmap(data, sizeof(*data));
	}
#endif
}

/*
 * --------- Crashkernel reservation ------------------------------
 */

#ifdef CONFIG_KEXEC

#ifndef CONFIG_XEN
/**
 * Reserve @size bytes of crashkernel memory at any suitable offset.
 *
 * @size: Size of the crashkernel memory to reserve.
 * Returns the base address on success, and -1ULL on failure.
 */
static
unsigned long long __init find_and_reserve_crashkernel(unsigned long long size)
{
	const unsigned long long alignment = 16<<20; 	/* 16M */
	unsigned long long start = 0LL;

	while (1) {
		int ret;

		start = find_e820_area(start, ULONG_MAX, size, alignment);
		if (start == -1ULL)
			return start;

		/* try to reserve it */
		ret = reserve_bootmem_generic(start, size, BOOTMEM_EXCLUSIVE);
		if (ret >= 0)
			return start;

		start += alignment;
	}
}

static inline unsigned long long get_total_mem(void)
{
	unsigned long long total;

	total = max_low_pfn - min_low_pfn;
#ifdef CONFIG_HIGHMEM
	total += highend_pfn - highstart_pfn;
#endif

	return total << PAGE_SHIFT;
}

static void __init reserve_crashkernel(void)
{
	unsigned long long total_mem;
	unsigned long long crash_size, crash_base;
	int ret;

	total_mem = get_total_mem();

	ret = parse_crashkernel(boot_command_line, total_mem,
			&crash_size, &crash_base);
	if (ret != 0 || crash_size <= 0)
		return;

	/* 0 means: find the address automatically */
	if (crash_base <= 0) {
		crash_base = find_and_reserve_crashkernel(crash_size);
		if (crash_base == -1ULL) {
			pr_info("crashkernel reservation failed. "
				"No suitable area found.\n");
			return;
		}
	} else {
		ret = reserve_bootmem_generic(crash_base, crash_size,
					BOOTMEM_EXCLUSIVE);
		if (ret < 0) {
			pr_info("crashkernel reservation failed - "
				"memory is in use\n");
			return;
		}
	}

	printk(KERN_INFO "Reserving %ldMB of memory at %ldMB "
			"for crashkernel (System RAM: %ldMB)\n",
			(unsigned long)(crash_size >> 20),
			(unsigned long)(crash_base >> 20),
			(unsigned long)(total_mem >> 20));

	crashk_res.start = crash_base;
	crashk_res.end   = crash_base + crash_size - 1;
	insert_resource(&iomem_resource, &crashk_res);
}
#else
#define reserve_crashkernel xen_machine_kexec_setup_resources
#endif
#else
static void __init reserve_crashkernel(void)
{
}
#endif

static struct resource standard_io_resources[] = {
	{ .name = "dma1", .start = 0x00, .end = 0x1f,
		.flags = IORESOURCE_BUSY | IORESOURCE_IO },
	{ .name = "pic1", .start = 0x20, .end = 0x21,
		.flags = IORESOURCE_BUSY | IORESOURCE_IO },
	{ .name = "timer0", .start = 0x40, .end = 0x43,
		.flags = IORESOURCE_BUSY | IORESOURCE_IO },
	{ .name = "timer1", .start = 0x50, .end = 0x53,
		.flags = IORESOURCE_BUSY | IORESOURCE_IO },
	{ .name = "keyboard", .start = 0x60, .end = 0x60,
		.flags = IORESOURCE_BUSY | IORESOURCE_IO },
	{ .name = "keyboard", .start = 0x64, .end = 0x64,
		.flags = IORESOURCE_BUSY | IORESOURCE_IO },
	{ .name = "dma page reg", .start = 0x80, .end = 0x8f,
		.flags = IORESOURCE_BUSY | IORESOURCE_IO },
	{ .name = "pic2", .start = 0xa0, .end = 0xa1,
		.flags = IORESOURCE_BUSY | IORESOURCE_IO },
	{ .name = "dma2", .start = 0xc0, .end = 0xdf,
		.flags = IORESOURCE_BUSY | IORESOURCE_IO },
	{ .name = "fpu", .start = 0xf0, .end = 0xff,
		.flags = IORESOURCE_BUSY | IORESOURCE_IO }
};

void __init reserve_standard_io_resources(void)
{
	int i;

	/* Nothing to do if not running in dom0. */
	if (!is_initial_xendomain())
		return;

	/* request I/O space for devices used on all i[345]86 PCs */
	for (i = 0; i < ARRAY_SIZE(standard_io_resources); i++)
		request_resource(&ioport_resource, &standard_io_resources[i]);

}

/*
 * Note: elfcorehdr_addr is not just limited to vmcore. It is also used by
 * is_kdump_kernel() to determine if we are booting after a panic. Hence
 * ifdef it under CONFIG_CRASH_DUMP and not CONFIG_PROC_VMCORE.
 */

#ifdef CONFIG_CRASH_DUMP
/* elfcorehdr= specifies the location of elf core header
 * stored by the crashed kernel. This option will be passed
 * by kexec loader to the capture kernel.
 */
static int __init setup_elfcorehdr(char *arg)
{
	char *end;
	if (!arg)
		return -EINVAL;
	elfcorehdr_addr = memparse(arg, &end);
	return end > arg ? 0 : -EINVAL;
}
early_param("elfcorehdr", setup_elfcorehdr);
#endif

#ifdef CONFIG_X86_RESERVE_LOW_64K
static int __init dmi_low_memory_corruption(const struct dmi_system_id *d)
{
	printk(KERN_NOTICE
		"%s detected: BIOS may corrupt low RAM, working around it.\n",
		d->ident);

	e820_update_range(0, 0x10000, E820_RAM, E820_RESERVED);
	sanitize_e820_map(e820.map, ARRAY_SIZE(e820.map), &e820.nr_map);

	return 0;
}
#endif

/* List of systems that have known low memory corruption BIOS problems */
static struct dmi_system_id __initdata bad_bios_dmi_table[] = {
#ifdef CONFIG_X86_RESERVE_LOW_64K
	{
		.callback = dmi_low_memory_corruption,
		.ident = "AMI BIOS",
		.matches = {
			DMI_MATCH(DMI_BIOS_VENDOR, "American Megatrends Inc."),
		},
	},
	{
		.callback = dmi_low_memory_corruption,
		.ident = "Phoenix BIOS",
		.matches = {
			DMI_MATCH(DMI_BIOS_VENDOR, "Phoenix Technologies"),
		},
	},
	{
	/*
	 * AMI BIOS with low memory corruption was found on Intel DG45ID board.
	 * It hase different DMI_BIOS_VENDOR = "Intel Corp.", for now we will
	 * match only DMI_BOARD_NAME and see if there is more bad products
	 * with this vendor.
	 */
		.callback = dmi_low_memory_corruption,
		.ident = "AMI BIOS",
		.matches = {
			DMI_MATCH(DMI_BOARD_NAME, "DG45ID"),
		},
	},
#endif
	{}
};

/*
 * Determine if we were loaded by an EFI loader.  If so, then we have also been
 * passed the efi memmap, systab, etc., so we should use these data structures
 * for initialization.  Note, the efi init code path is determined by the
 * global efi_enabled. This allows the same kernel image to be used on existing
 * systems (with a traditional BIOS) as well as on EFI systems.
 */
/*
 * setup_arch - architecture-specific boot-time initializations
 *
 * Note: On x86_64, fixmaps are ready for use even before this is called.
 */

void __init setup_arch(char **cmdline_p)
{
#ifdef CONFIG_XEN
	unsigned int i;
	unsigned long p2m_pages;
	struct physdev_set_iopl set_iopl;

	if (!is_initial_xendomain()) {
#ifdef CONFIG_X86_32
		/* Force a quick death if the kernel panics (not domain 0). */
		extern int panic_timeout;
		if (!panic_timeout)
			panic_timeout = 1;
#endif

		/* Register a call for panic conditions. */
		atomic_notifier_chain_register(&panic_notifier_list, &xen_panic_block);
	}
#endif /* CONFIG_XEN */

#ifdef CONFIG_X86_32
	memcpy(&boot_cpu_data, &new_cpu_data, sizeof(new_cpu_data));
	visws_early_detect();
#else
	printk(KERN_INFO "Command line: %s\n", boot_command_line);
#endif

	/* VMI may relocate the fixmap; do this before touching ioremap area */
	vmi_init();

	early_cpu_init();
	early_ioremap_init();

#ifndef CONFIG_XEN
	ROOT_DEV = old_decode_dev(boot_params.hdr.root_dev);
	screen_info = boot_params.screen_info;
	edid_info = boot_params.edid_info;
#ifdef CONFIG_X86_32
	apm_info.bios = boot_params.apm_bios_info;
	ist_info = boot_params.ist_info;
	if (boot_params.sys_desc_table.length != 0) {
		set_mca_bus(boot_params.sys_desc_table.table[3] & 0x2);
		machine_id = boot_params.sys_desc_table.table[0];
		machine_submodel_id = boot_params.sys_desc_table.table[1];
		BIOS_revision = boot_params.sys_desc_table.table[2];
	}
#endif
	saved_video_mode = boot_params.hdr.vid_mode;
	bootloader_type = boot_params.hdr.type_of_loader;
	if ((bootloader_type >> 4) == 0xe) {
		bootloader_type &= 0xf;
		bootloader_type |= (boot_params.hdr.ext_loader_type+0x10) << 4;
	}
	bootloader_version  = bootloader_type & 0xf;
	bootloader_version |= boot_params.hdr.ext_loader_ver << 4;

#ifdef CONFIG_BLK_DEV_RAM
	rd_image_start = boot_params.hdr.ram_size & RAMDISK_IMAGE_START_MASK;
	rd_prompt = ((boot_params.hdr.ram_size & RAMDISK_PROMPT_FLAG) != 0);
	rd_doload = ((boot_params.hdr.ram_size & RAMDISK_LOAD_FLAG) != 0);
#endif
#ifdef CONFIG_EFI
	if (!strncmp((char *)&boot_params.efi_info.efi_loader_signature,
#ifdef CONFIG_X86_32
		     "EL32",
#else
		     "EL64",
#endif
	 4)) {
		efi_enabled = 1;
		efi_reserve_early();
	}
#endif
#else /* CONFIG_XEN */
#ifdef CONFIG_X86_32
	/* This must be initialized to UNNAMED_MAJOR for ipconfig to work
	   properly.  Setting ROOT_DEV to default to /dev/ram0 breaks initrd.
	*/
	ROOT_DEV = MKDEV(UNNAMED_MAJOR,0);
#else
 	ROOT_DEV = MKDEV(RAMDISK_MAJOR,0);
#endif
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
	copy_edid();
#endif /* CONFIG_XEN */

	x86_init.oem.arch_setup();

	setup_memory_map();
	parse_setup_data();
	/* update the e820_saved too */
	e820_reserve_setup_data();

	copy_edd();

#ifndef CONFIG_XEN
	if (!boot_params.hdr.root_flags)
		root_mountflags &= ~MS_RDONLY;
#endif
	init_mm.start_code = (unsigned long) _text;
	init_mm.end_code = (unsigned long) _etext;
	init_mm.end_data = (unsigned long) _edata;
	init_mm.brk = _brk_end;

	code_resource.start = virt_to_phys(_text);
	code_resource.end = virt_to_phys(_etext)-1;
	data_resource.start = virt_to_phys(_etext);
	data_resource.end = virt_to_phys(_edata)-1;
	bss_resource.start = virt_to_phys(&__bss_start);
	bss_resource.end = virt_to_phys(&__bss_stop)-1;

#ifdef CONFIG_CMDLINE_BOOL
#ifdef CONFIG_CMDLINE_OVERRIDE
	strlcpy(boot_command_line, builtin_cmdline, COMMAND_LINE_SIZE);
#else
	if (builtin_cmdline[0]) {
		/* append boot loader cmdline to builtin */
		strlcat(builtin_cmdline, " ", COMMAND_LINE_SIZE);
		strlcat(builtin_cmdline, boot_command_line, COMMAND_LINE_SIZE);
		strlcpy(boot_command_line, builtin_cmdline, COMMAND_LINE_SIZE);
	}
#endif
#endif

	strlcpy(command_line, boot_command_line, COMMAND_LINE_SIZE);
	*cmdline_p = command_line;

#ifdef CONFIG_X86_64
	/*
	 * Must call this twice: Once just to detect whether hardware doesn't
	 * support NX (so that the early EHCI debug console setup can safely
	 * call set_fixmap(), and then again after parsing early parameters to
	 * honor the respective command line option.
	 */
	check_efer();
#endif

	parse_early_param();

#ifdef CONFIG_X86_64
	check_efer();
#endif

	/* Must be before kernel pagetables are setup */
	vmi_activate();

	/* after early param, so could get panic from serial */
	reserve_early_setup_data();

	if (acpi_mps_check()) {
#if defined(CONFIG_X86_LOCAL_APIC) && !defined(CONFIG_XEN)
		disable_apic = 1;
#endif
		setup_clear_cpu_cap(X86_FEATURE_APIC);
	}

#ifdef CONFIG_PCI
	if (pci_early_dump_regs)
		early_dump_pci_devices();
#endif

	finish_e820_parsing();

	if (efi_enabled)
		efi_init();

	if (is_initial_xendomain()) {
		dmi_scan_machine();

		dmi_check_system(bad_bios_dmi_table);
	}

	/*
	 * VMware detection requires dmi to be available, so this
	 * needs to be done after dmi_scan_machine, for the BP.
	 */
	init_hypervisor_platform();

	x86_init.resources.probe_roms();

#ifndef CONFIG_XEN
	/* after parse_early_param, so could debug it */
	insert_resource(&iomem_resource, &code_resource);
	insert_resource(&iomem_resource, &data_resource);
	insert_resource(&iomem_resource, &bss_resource);


#ifdef CONFIG_X86_32
	if (ppro_with_ram_bug()) {
		e820_update_range(0x70000000ULL, 0x40000ULL, E820_RAM,
				  E820_RESERVED);
		sanitize_e820_map(e820.map, ARRAY_SIZE(e820.map), &e820.nr_map);
		printk(KERN_INFO "fixed physical RAM map:\n");
		e820_print_map("bad_ppro");
	}
#else
	early_gart_iommu_check();
#endif
#endif /* CONFIG_XEN */

	/*
	 * partially used pages are not usable - thus
	 * we are rounding upwards:
	 */
	max_pfn = e820_end_of_ram_pfn();

	/* preallocate 4k for mptable mpc */
	early_reserve_e820_mpc_new();
	/* update e820 for memory not covered by WB MTRRs */
	mtrr_bp_init();
#ifndef CONFIG_XEN
	if (mtrr_trim_uncached_memory(max_pfn))
		max_pfn = e820_end_of_ram_pfn();
#endif

#ifdef CONFIG_X86_32
	/* max_low_pfn get updated here */
	find_low_pfn_range();
#else
	num_physpages = max_pfn;
	max_mapnr = max_pfn;

#ifdef CONFIG_X86_LOCAL_APIC
	check_x2apic();
#endif

	/* How many end-of-memory variables you have, grandma! */
	/* need this before calling reserve_initrd */
	if (max_pfn > (1UL<<(32 - PAGE_SHIFT)))
		max_low_pfn = e820_end_of_low_ram_pfn();
	else
		max_low_pfn = max_pfn;

	high_memory = (void *)__va(max_pfn * PAGE_SIZE - 1) + 1;
#ifndef CONFIG_XEN
	max_pfn_mapped = KERNEL_IMAGE_SIZE >> PAGE_SHIFT;
#endif
#endif

#ifdef CONFIG_X86_CHECK_BIOS_CORRUPTION
	setup_bios_corruption_check();
#endif

	printk(KERN_DEBUG "initial memory mapped : 0 - %08lx\n",
			max_pfn_mapped<<PAGE_SHIFT);

	reserve_brk();

	init_gbpages();

	/* max_pfn_mapped is updated here */
	max_low_pfn_mapped = init_memory_mapping(0, max_low_pfn<<PAGE_SHIFT);
	max_pfn_mapped = max_low_pfn_mapped;

#ifdef CONFIG_X86_64
	if (max_pfn > max_low_pfn) {
		max_pfn_mapped = init_memory_mapping(1UL<<32,
						     max_pfn<<PAGE_SHIFT);
		/* can we preseve max_low_pfn ?*/
		max_low_pfn = max_pfn;
	}
#endif

	/*
	 * NOTE: On x86-32, only from this point on, fixmaps are ready for use.
	 */

#ifdef CONFIG_PROVIDE_OHCI1394_DMA_INIT
	if (init_ohci1394_dma_early)
		init_ohci1394_dma_on_all_controllers();
#endif

	reserve_initrd();

#ifndef CONFIG_XEN
	vsmp_init();
#endif

	io_delay_init();

#ifdef CONFIG_ACPI
	if (!is_initial_xendomain()) {
		printk(KERN_INFO "ACPI in unprivileged domain disabled\n");
		disable_acpi();
	}
#endif

	/*
	 * Parse the ACPI tables for possible boot-time SMP configuration.
	 */
	acpi_boot_table_init();

	early_acpi_boot_init();

#ifdef CONFIG_ACPI_NUMA
	/*
	 * Parse SRAT to discover nodes.
	 */
	acpi_numa_init();
#endif

	initmem_init(0, max_pfn);

#ifdef CONFIG_ACPI_SLEEP
	/*
	 * Reserve low memory region for sleep support.
	 */
	acpi_reserve_bootmem();
#endif
	/*
	 * Find and reserve possible boot-time SMP configuration:
	 */
	find_smp_config();

	reserve_crashkernel();

#if defined(CONFIG_X86_64) && !defined(CONFIG_XEN)
	/*
	 * dma32_reserve_bootmem() allocates bootmem which may conflict
	 * with the crashkernel command line, so do that after
	 * reserve_crashkernel()
	 */
	dma32_reserve_bootmem();
#endif

	reserve_ibft_region();

#ifdef CONFIG_KVM_CLOCK
	kvmclock_init();
#endif

	x86_init.paging.pagetable_setup_start(swapper_pg_dir);
	paging_init();
	x86_init.paging.pagetable_setup_done(swapper_pg_dir);

	tboot_probe();

#ifdef CONFIG_X86_64
	map_vsyscall();
#endif

#ifdef CONFIG_XEN
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
				     phys_to_machine_mapping + max_pfn);
		reservation.nr_extents = difference;
		ret = HYPERVISOR_memory_op(XENMEM_decrease_reservation,
					   &reservation);
		BUG_ON(ret != difference);
	}
	else if (max_pfn > xen_start_info->nr_pages)
		p2m_pages = xen_start_info->nr_pages;

	if (!xen_feature(XENFEAT_auto_translated_physmap)) {
		unsigned long i, j, size;
		unsigned int k, fpp;

		/* Make sure we have a large enough P->M table. */
		phys_to_machine_mapping = alloc_bootmem_pages(
			max_pfn * sizeof(unsigned long));
		memcpy(phys_to_machine_mapping,
		       __va(__pa(xen_start_info->mfn_list)),
		       p2m_pages * sizeof(unsigned long));
		memset(phys_to_machine_mapping + p2m_pages, ~0,
		       (max_pfn - p2m_pages) * sizeof(unsigned long));

#ifdef CONFIG_X86_64
		if (xen_start_info->mfn_list == VMEMMAP_START) {
			/*
			 * Since it is well isolated we can (and since it is
			 * perhaps large we should) also free the page tables
			 * mapping the initial P->M table.
			 */
			unsigned long va = VMEMMAP_START, pa;
			pgd_t *pgd = pgd_offset_k(va);
			pud_t *pud_page = pud_offset(pgd, 0);

			BUILD_BUG_ON(VMEMMAP_START & ~PGDIR_MASK);
			xen_l4_entry_update(pgd, __pgd(0));
			for(;;) {
				pud_t *pud = pud_page + pud_index(va);

				if (pud_none(*pud))
					va += PUD_SIZE;
				else if (pud_large(*pud)) {
					pa = pud_val(*pud) & PHYSICAL_PAGE_MASK;
					make_pages_writable(__va(pa),
						PUD_SIZE >> PAGE_SHIFT,
						XENFEAT_writable_page_tables);
					free_bootmem(pa, PUD_SIZE);
					va += PUD_SIZE;
				} else {
					pmd_t *pmd = pmd_offset(pud, va);

					if (pmd_large(*pmd)) {
						pa = pmd_val(*pmd) & PHYSICAL_PAGE_MASK;
						make_pages_writable(__va(pa),
							PMD_SIZE >> PAGE_SHIFT,
							XENFEAT_writable_page_tables);
						free_bootmem(pa, PMD_SIZE);
					} else if (!pmd_none(*pmd)) {
						pte_t *pte = pte_offset_kernel(pmd, va);

						for (i = 0; i < PTRS_PER_PTE; ++i) {
							if (pte_none(pte[i]))
								break;
							pa = pte_pfn(pte[i]) << PAGE_SHIFT;
							make_page_writable(__va(pa),
								XENFEAT_writable_page_tables);
							free_bootmem(pa, PAGE_SIZE);
						}
						ClearPagePinned(virt_to_page(pte));
						make_page_writable(pte,
							XENFEAT_writable_page_tables);
						free_bootmem(__pa(pte), PAGE_SIZE);
					}
					va += PMD_SIZE;
					if (pmd_index(va))
						continue;
					ClearPagePinned(virt_to_page(pmd));
					make_page_writable(pmd,
						XENFEAT_writable_page_tables);
					free_bootmem(__pa((unsigned long)pmd
							  & PAGE_MASK),
						PAGE_SIZE);
				}
				if (!pud_index(va))
					break;
			}
			ClearPagePinned(virt_to_page(pud_page));
			make_page_writable(pud_page,
				XENFEAT_writable_page_tables);
			free_bootmem(__pa((unsigned long)pud_page & PAGE_MASK),
				PAGE_SIZE);
		} else if (!WARN_ON(xen_start_info->mfn_list
				    < __START_KERNEL_map))
#endif
			free_bootmem(__pa(xen_start_info->mfn_list),
				PFN_PHYS(PFN_UP(xen_start_info->nr_pages *
						sizeof(unsigned long))));


		/*
		 * Initialise the list of the frames that specify the list of
		 * frames that make up the p2m table. Used by save/restore.
		 */
		fpp = PAGE_SIZE/sizeof(unsigned long);
		size = (max_pfn + fpp - 1) / fpp;
		size = (size + fpp - 1) / fpp;
		++size; /* include a zero terminator for crash tools */
		size *= sizeof(unsigned long);
		pfn_to_mfn_frame_list_list = alloc_bootmem_pages(size);
		if (size > PAGE_SIZE
		    && xen_create_contiguous_region((unsigned long)
						    pfn_to_mfn_frame_list_list,
						    get_order(size), 0))
			BUG();
		size -= sizeof(unsigned long);
		pfn_to_mfn_frame_list = alloc_bootmem(size);

		for (i = j = 0, k = -1; i < max_pfn; i += fpp, j++) {
			if (j == fpp)
				j = 0;
			if (j == 0) {
				k++;
				BUG_ON(k * sizeof(unsigned long) >= size);
				pfn_to_mfn_frame_list[k] =
					alloc_bootmem_pages(PAGE_SIZE);
				pfn_to_mfn_frame_list_list[k] =
					virt_to_mfn(pfn_to_mfn_frame_list[k]);
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
#else /* CONFIG_XEN */
	generic_apic_probe();

	early_quirks();
#endif

	/*
	 * Read APIC and some other early information from ACPI tables.
	 */
	acpi_boot_init();

	sfi_init();

	/*
	 * get boot-time SMP configuration:
	 */
	if (smp_found_config)
		get_smp_config();

	prefill_possible_map();

#ifdef CONFIG_X86_64
	init_cpu_to_node();
#endif

#ifndef CONFIG_XEN
	init_apic_mappings();
	ioapic_init_mappings();

	/* need to wait for io_apic is mapped */
	probe_nr_irqs_gsi();

	kvm_guest_init();

	e820_reserve_resources();
	e820_mark_nosave_regions(max_low_pfn);
#else
	if (is_initial_xendomain())
		e820_reserve_resources();
#endif

	x86_init.resources.reserve_resources();

#ifndef CONFIG_XEN
	e820_setup_gap();

#ifdef CONFIG_VT
#if defined(CONFIG_VGA_CONSOLE)
	if (!efi_enabled || (efi_mem_type(0xa0000) != EFI_CONVENTIONAL_MEMORY))
		conswitchp = &vga_con;
#elif defined(CONFIG_DUMMY_CONSOLE)
	conswitchp = &dummy_con;
#endif
#endif
#else /* CONFIG_XEN */
	if (is_initial_xendomain())
		e820_setup_gap();

	set_iopl.iopl = 1;
	WARN_ON(HYPERVISOR_physdev_op(PHYSDEVOP_set_iopl, &set_iopl));

#ifdef CONFIG_VT
#ifdef CONFIG_DUMMY_CONSOLE
	conswitchp = &dummy_con;
#endif
#ifdef CONFIG_VGA_CONSOLE
	if (is_initial_xendomain())
		conswitchp = &vga_con;
#endif
#endif
#endif /* CONFIG_XEN */
	x86_init.oem.banner();
}

#ifdef CONFIG_X86_32

static struct resource video_ram_resource = {
	.name	= "Video RAM area",
	.start	= 0xa0000,
	.end	= 0xbffff,
	.flags	= IORESOURCE_BUSY | IORESOURCE_MEM
};

void __init i386_reserve_resources(void)
{
	if (is_initial_xendomain())
		request_resource(&iomem_resource, &video_ram_resource);
	reserve_standard_io_resources();
}

#endif /* CONFIG_X86_32 */

#ifdef CONFIG_XEN
static int
xen_panic_event(struct notifier_block *this, unsigned long event, void *ptr)
{
	HYPERVISOR_shutdown(SHUTDOWN_crash);
	/* we're never actually going to get here... */
	return NOTIFY_DONE;
}
#endif /* !CONFIG_XEN */
