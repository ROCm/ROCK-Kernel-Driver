/*
 *  linux/arch/x86-64/kernel/setup.c
 *
 *  Copyright (C) 1995  Linus Torvalds
 *
 *  Nov 2001 Dave Jones <davej@suse.de>
 *  Forked from i386 setup code.
 *
 *  $Id$
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
#include <linux/initrd.h>
#include <linux/highmem.h>
#include <linux/bootmem.h>
#include <linux/module.h>
#include <asm/processor.h>
#include <linux/console.h>
#include <linux/seq_file.h>
#include <linux/root_dev.h>
#include <linux/pci.h>
#include <linux/acpi.h>
#include <linux/kallsyms.h>
#include <asm/mtrr.h>
#include <asm/uaccess.h>
#include <asm/system.h>
#include <asm/io.h>
#include <asm/smp.h>
#include <asm/msr.h>
#include <asm/desc.h>
#include <video/edid.h>
#include <asm/e820.h>
#include <asm/dma.h>
#include <asm/mpspec.h>
#include <asm/mmu_context.h>
#include <asm/bootsetup.h>
#include <asm/smp.h>
#include <asm/proto.h>

/*
 * Machine setup..
 */

struct cpuinfo_x86 boot_cpu_data;

unsigned long mmu_cr4_features;
EXPORT_SYMBOL_GPL(mmu_cr4_features);

int acpi_disabled;
EXPORT_SYMBOL(acpi_disabled);
#ifdef	CONFIG_ACPI_BOOT
extern int __initdata acpi_ht;
extern acpi_interrupt_flags	acpi_sci_flags;
/* int __initdata acpi_force = 0; */
#endif

/* For PCI or other memory-mapped resources */
unsigned long pci_mem_start = 0x10000000;

unsigned long saved_video_mode;

int swiotlb;
EXPORT_SYMBOL(swiotlb);

/*
 * Setup options
 */
struct drive_info_struct { char dummy[32]; } drive_info;
struct screen_info screen_info;
struct sys_desc_table_struct {
	unsigned short length;
	unsigned char table[0];
};

struct edid_info edid_info;
struct e820map e820;

unsigned char aux_device_present;

extern int root_mountflags;
extern char _text, _etext, _edata, _end;

char command_line[COMMAND_LINE_SIZE];
char saved_command_line[COMMAND_LINE_SIZE];

struct resource standard_io_resources[] = {
	{ "dma1", 0x00, 0x1f, IORESOURCE_BUSY },
	{ "pic1", 0x20, 0x21, IORESOURCE_BUSY },
	{ "timer", 0x40, 0x5f, IORESOURCE_BUSY },
	{ "keyboard", 0x60, 0x6f, IORESOURCE_BUSY },
	{ "dma page reg", 0x80, 0x8f, IORESOURCE_BUSY },
	{ "pic2", 0xa0, 0xa1, IORESOURCE_BUSY },
	{ "dma2", 0xc0, 0xdf, IORESOURCE_BUSY },
	{ "fpu", 0xf0, 0xff, IORESOURCE_BUSY }
};

#define STANDARD_IO_RESOURCES (sizeof(standard_io_resources)/sizeof(struct resource))

struct resource code_resource = { "Kernel code", 0x100000, 0 };
struct resource data_resource = { "Kernel data", 0, 0 };
struct resource vram_resource = { "Video RAM area", 0xa0000, 0xbffff, IORESOURCE_BUSY };

/* System ROM resources */
#define MAXROMS 6
static struct resource rom_resources[MAXROMS] = {
	{ "System ROM", 0xF0000, 0xFFFFF, IORESOURCE_BUSY },
	{ "Video ROM", 0xc0000, 0xc7fff, IORESOURCE_BUSY }
};

#define romsignature(x) (*(unsigned short *)(x) == 0xaa55)

static void __init probe_roms(void)
{
	int roms = 1;
	unsigned long base;
	unsigned char *romstart;

	request_resource(&iomem_resource, rom_resources+0);

	/* Video ROM is standard at C000:0000 - C7FF:0000, check signature */
	for (base = 0xC0000; base < 0xE0000; base += 2048) {
		romstart = isa_bus_to_virt(base);
		if (!romsignature(romstart))
			continue;
		request_resource(&iomem_resource, rom_resources + roms);
		roms++;
		break;
	}

	/* Extension roms at C800:0000 - DFFF:0000 */
	for (base = 0xC8000; base < 0xE0000; base += 2048) {
		unsigned long length;

		romstart = isa_bus_to_virt(base);
		if (!romsignature(romstart))
			continue;
		length = romstart[2] * 512;
		if (length) {
			unsigned int i;
			unsigned char chksum;

			chksum = 0;
			for (i = 0; i < length; i++)
				chksum += romstart[i];

			/* Good checksum? */
			if (!chksum) {
				rom_resources[roms].start = base;
				rom_resources[roms].end = base + length - 1;
				rom_resources[roms].name = "Extension ROM";
				rom_resources[roms].flags = IORESOURCE_BUSY;

				request_resource(&iomem_resource, rom_resources + roms);
				roms++;
				if (roms >= MAXROMS)
					return;
			}
		}
	}

	/* Final check for motherboard extension rom at E000:0000 */
	base = 0xE0000;
	romstart = isa_bus_to_virt(base);

	if (romsignature(romstart)) {
		rom_resources[roms].start = base;
		rom_resources[roms].end = base + 65535;
		rom_resources[roms].name = "Extension ROM";
		rom_resources[roms].flags = IORESOURCE_BUSY;

		request_resource(&iomem_resource, rom_resources + roms);
	}
}

static __init void parse_cmdline_early (char ** cmdline_p)
{
	char c = ' ', *to = command_line, *from = COMMAND_LINE;
	int len = 0;

	/* Save unparsed command line copy for /proc/cmdline */
	memcpy(saved_command_line, COMMAND_LINE, COMMAND_LINE_SIZE);
	saved_command_line[COMMAND_LINE_SIZE-1] = '\0';

	for (;;) {
		if (c != ' ') 
			goto next_char; 

#ifdef  CONFIG_SMP
		/*
		 * If the BIOS enumerates physical processors before logical,
		 * maxcpus=N at enumeration-time can be used to disable HT.
		 */
		else if (!memcmp(from, "maxcpus=", 8)) {
			extern unsigned int maxcpus;

			maxcpus = simple_strtoul(from + 8, NULL, 0);
		}
#endif
#ifdef CONFIG_ACPI_BOOT
		/* "acpi=off" disables both ACPI table parsing and interpreter init */
		if (!memcmp(from, "acpi=off", 8))
			disable_acpi();

		if (!memcmp(from, "acpi=force", 10)) { 
			/* add later when we do DMI horrors: */
			/* acpi_force = 1; */	
			acpi_disabled = 0;
		}

		/* acpi=ht just means: do ACPI MADT parsing 
		   at bootup, but don't enable the full ACPI interpreter */
		if (!memcmp(from, "acpi=ht", 7)) { 
			acpi_ht = 1; 
		}
                else if (!memcmp(from, "pci=noacpi", 10)) 
			acpi_disable_pci();
		else if (!memcmp(from, "acpi=noirq", 10))
			acpi_noirq_set();

		else if (!memcmp(from, "acpi_sci=edge", 13))
			acpi_sci_flags.trigger =  1;
		else if (!memcmp(from, "acpi_sci=level", 14))
			acpi_sci_flags.trigger = 3;
		else if (!memcmp(from, "acpi_sci=high", 13))
			acpi_sci_flags.polarity = 1;
		else if (!memcmp(from, "acpi_sci=low", 12))
			acpi_sci_flags.polarity = 3;

		/* acpi=strict disables out-of-spec workarounds */
		else if (!memcmp(from, "acpi=strict", 11)) {
			acpi_strict = 1;
		}
#endif

		if (!memcmp(from, "nolapic", 7) ||
		    !memcmp(from, "disableapic", 11))
			disable_apic = 1;

		if (!memcmp(from, "noapic", 6)) 
			skip_ioapic_setup = 1;

		if (!memcmp(from, "apic", 4)) { 
			skip_ioapic_setup = 0;
			ioapic_force = 1;
		}
			
		if (!memcmp(from, "mem=", 4))
			parse_memopt(from+4, &from); 

#ifdef CONFIG_DISCONTIGMEM
		if (!memcmp(from, "numa=", 5))
			numa_setup(from+5); 
#endif

#ifdef CONFIG_GART_IOMMU 
		if (!memcmp(from,"iommu=",6)) { 
			iommu_setup(from+6); 
		}
#endif

		if (!memcmp(from,"oops=panic", 10))
			panic_on_oops = 1;

	next_char:
		c = *(from++);
		if (!c)
			break;
		if (COMMAND_LINE_SIZE <= ++len)
			break;
		*(to++) = c;
	}
	*to = '\0';
	*cmdline_p = command_line;
}

#ifndef CONFIG_DISCONTIGMEM
static void __init contig_initmem_init(void)
{
        unsigned long bootmap_size, bootmap; 
        bootmap_size = bootmem_bootmap_pages(end_pfn)<<PAGE_SHIFT;
        bootmap = find_e820_area(0, end_pfn<<PAGE_SHIFT, bootmap_size);
        if (bootmap == -1L) 
                panic("Cannot find bootmem map of size %ld\n",bootmap_size);
        bootmap_size = init_bootmem(bootmap >> PAGE_SHIFT, end_pfn);
        e820_bootmem_free(&contig_page_data, 0, end_pfn << PAGE_SHIFT); 
        reserve_bootmem(bootmap, bootmap_size);
} 
#endif

/* Use inline assembly to define this because the nops are defined 
   as inline assembly strings in the include files and we cannot 
   get them easily into strings. */
asm("\t.data\nk8nops: " 
    K8_NOP1 K8_NOP2 K8_NOP3 K8_NOP4 K8_NOP5 K8_NOP6
    K8_NOP7 K8_NOP8); 
    
extern unsigned char k8nops[];
static unsigned char *k8_nops[ASM_NOP_MAX+1] = { 
     NULL,
     k8nops,
     k8nops + 1,
     k8nops + 1 + 2,
     k8nops + 1 + 2 + 3,
     k8nops + 1 + 2 + 3 + 4,
     k8nops + 1 + 2 + 3 + 4 + 5,
     k8nops + 1 + 2 + 3 + 4 + 5 + 6,
     k8nops + 1 + 2 + 3 + 4 + 5 + 6 + 7,
}; 

/* Replace instructions with better alternatives for this CPU type.

   This runs before SMP is initialized to avoid SMP problems with
   self modifying code. This implies that assymetric systems where
   APs have less capabilities than the boot processor are not handled. 
   In this case boot with "noreplacement". */ 
void apply_alternatives(void *start, void *end) 
{ 
	struct alt_instr *a; 
	int diff, i, k;
	for (a = start; (void *)a < end; a++) { 
		if (!boot_cpu_has(a->cpuid))
			continue;

		BUG_ON(a->replacementlen > a->instrlen); 
		__inline_memcpy(a->instr, a->replacement, a->replacementlen); 
		diff = a->instrlen - a->replacementlen; 

		/* Pad the rest with nops */
		for (i = a->replacementlen; diff > 0; diff -= k, i += k) {
			k = diff;
			if (k > ASM_NOP_MAX)
				k = ASM_NOP_MAX;
			__inline_memcpy(a->instr + i, k8_nops[k], k); 
		} 
	}
} 

static int no_replacement __initdata = 0; 
 
void __init alternative_instructions(void)
{
	extern struct alt_instr __alt_instructions[], __alt_instructions_end[];
	if (no_replacement) 
		return;
	apply_alternatives(__alt_instructions, __alt_instructions_end);
}

static int __init noreplacement_setup(char *s)
{ 
     no_replacement = 1; 
     return 0; 
} 

__setup("noreplacement", noreplacement_setup); 

void __init setup_arch(char **cmdline_p)
{
	unsigned long low_mem_size;
	unsigned long kernel_end;

 	ROOT_DEV = old_decode_dev(ORIG_ROOT_DEV);
 	drive_info = DRIVE_INFO;
 	screen_info = SCREEN_INFO;
	edid_info = EDID_INFO;
	aux_device_present = AUX_DEVICE_INFO;
	saved_video_mode = SAVED_VIDEO_MODE;

#ifdef CONFIG_BLK_DEV_RAM
	rd_image_start = RAMDISK_FLAGS & RAMDISK_IMAGE_START_MASK;
	rd_prompt = ((RAMDISK_FLAGS & RAMDISK_PROMPT_FLAG) != 0);
	rd_doload = ((RAMDISK_FLAGS & RAMDISK_LOAD_FLAG) != 0);
#endif
	setup_memory_region();

	if (!MOUNT_ROOT_RDONLY)
		root_mountflags &= ~MS_RDONLY;
	init_mm.start_code = (unsigned long) &_text;
	init_mm.end_code = (unsigned long) &_etext;
	init_mm.end_data = (unsigned long) &_edata;
	init_mm.brk = (unsigned long) &_end;

	code_resource.start = virt_to_phys(&_text);
	code_resource.end = virt_to_phys(&_etext)-1;
	data_resource.start = virt_to_phys(&_etext);
	data_resource.end = virt_to_phys(&_edata)-1;

	parse_cmdline_early(cmdline_p);

	/*
	 * partially used pages are not usable - thus
	 * we are rounding upwards:
	 */
	end_pfn = e820_end_of_ram();

	check_efer();

	init_memory_mapping(); 

#ifdef CONFIG_DISCONTIGMEM
	numa_initmem_init(0, end_pfn); 
#else
	contig_initmem_init(); 
#endif

	/* Reserve direct mapping */
	reserve_bootmem_generic(table_start << PAGE_SHIFT, 
				(table_end - table_start) << PAGE_SHIFT);

	/* reserve kernel */
	kernel_end = round_up(__pa_symbol(&_end),PAGE_SIZE);
	reserve_bootmem_generic(HIGH_MEMORY, kernel_end - HIGH_MEMORY);

	/*
	 * reserve physical page 0 - it's a special BIOS page on many boxes,
	 * enabling clean reboots, SMP operation, laptop functions.
	 */
	reserve_bootmem_generic(0, PAGE_SIZE);

#ifdef CONFIG_SMP
	/*
	 * But first pinch a few for the stack/trampoline stuff
	 * FIXME: Don't need the extra page at 4K, but need to fix
	 * trampoline before removing it. (see the GDT stuff)
	 */
	reserve_bootmem_generic(PAGE_SIZE, PAGE_SIZE);

	/* Reserve SMP trampoline */
	reserve_bootmem_generic(SMP_TRAMPOLINE_BASE, PAGE_SIZE);
#endif

#ifdef CONFIG_ACPI_SLEEP
       /*
        * Reserve low memory region for sleep support.
        */
       acpi_reserve_bootmem();
#endif
#ifdef CONFIG_X86_LOCAL_APIC
	/*
	 * Find and reserve possible boot-time SMP configuration:
	 */
	find_smp_config();
#endif
#ifdef CONFIG_BLK_DEV_INITRD
	if (LOADER_TYPE && INITRD_START) {
		if (INITRD_START + INITRD_SIZE <= (end_pfn << PAGE_SHIFT)) {
			reserve_bootmem_generic(INITRD_START, INITRD_SIZE);
			initrd_start =
				INITRD_START ? INITRD_START + PAGE_OFFSET : 0;
			initrd_end = initrd_start+INITRD_SIZE;
		}
		else {
			printk(KERN_ERR "initrd extends beyond end of memory "
			    "(0x%08lx > 0x%08lx)\ndisabling initrd\n",
			    (unsigned long)(INITRD_START + INITRD_SIZE),
			    (unsigned long)(end_pfn << PAGE_SHIFT));
			initrd_start = 0;
		}
	}
#endif
	paging_init();

#ifndef CONFIG_SMP
	/* Temporary hack: disable the IO-APIC for UP Nvidia and VIA. */
		check_ioapic();
#endif
#ifdef CONFIG_ACPI_BOOT
       /*
        * Initialize the ACPI boot-time table parser (gets the RSDP and SDT).
        * Must do this after paging_init (due to reliance on fixmap, and thus
        * the bootmem allocator) but before get_smp_config (to allow parsing
        * of MADT).
        */
	if (!acpi_disabled)
		acpi_boot_init();
#endif
#ifdef CONFIG_X86_LOCAL_APIC
	/*
	 * get boot-time SMP configuration:
	 */
	if (smp_found_config)
		get_smp_config();
	init_apic_mappings();
#endif

	/*
	 * Request address space for all standard RAM and ROM resources
	 * and also for regions reported as reserved by the e820.
	 */
	probe_roms();
	e820_reserve_resources(); 

	request_resource(&iomem_resource, &vram_resource);

	{
	unsigned i;
	/* request I/O space for devices used on all i[345]86 PCs */
	for (i = 0; i < STANDARD_IO_RESOURCES; i++)
		request_resource(&ioport_resource, standard_io_resources+i);
	}

	/* Will likely break when you have unassigned resources with more
	   than 4GB memory and bridges that don't support more than 4GB. 
	   Doing it properly would require to use pci_alloc_consistent
	   in this case. */
	low_mem_size = ((end_pfn << PAGE_SHIFT) + 0xfffff) & ~0xfffff;
	if (low_mem_size > pci_mem_start)
		pci_mem_start = low_mem_size;

#ifdef CONFIG_GART_IOMMU
       iommu_hole_init();
#endif

#ifdef CONFIG_VT
#if defined(CONFIG_VGA_CONSOLE)
	conswitchp = &vga_con;
#elif defined(CONFIG_DUMMY_CONSOLE)
	conswitchp = &dummy_con;
#endif
#endif
}

static int __init get_model_name(struct cpuinfo_x86 *c)
{
	unsigned int *v;

	if (cpuid_eax(0x80000000) < 0x80000004)
		return 0;

	v = (unsigned int *) c->x86_model_id;
	cpuid(0x80000002, &v[0], &v[1], &v[2], &v[3]);
	cpuid(0x80000003, &v[4], &v[5], &v[6], &v[7]);
	cpuid(0x80000004, &v[8], &v[9], &v[10], &v[11]);
	c->x86_model_id[48] = 0;
	return 1;
}


static void __init display_cacheinfo(struct cpuinfo_x86 *c)
{
	unsigned int n, dummy, eax, ebx, ecx, edx;

	n = cpuid_eax(0x80000000);

	if (n >= 0x80000005) {
		cpuid(0x80000005, &dummy, &ebx, &ecx, &edx);
		printk(KERN_INFO "CPU: L1 I Cache: %dK (%d bytes/line), D cache %dK (%d bytes/line)\n",
			edx>>24, edx&0xFF, ecx>>24, ecx&0xFF);
		c->x86_cache_size=(ecx>>24)+(edx>>24);	
		/* DTLB and ITLB together, but only 4K */
		c->x86_tlbsize = ((ebx>>16)&0xff) + (ebx&0xff);
	}

	if (n >= 0x80000006) {
		cpuid(0x80000006, &dummy, &ebx, &ecx, &edx);
	ecx = cpuid_ecx(0x80000006);
	c->x86_cache_size = ecx >> 16;
		c->x86_tlbsize += ((ebx >> 16) & 0xfff) + (ebx & 0xfff);

	printk(KERN_INFO "CPU: L2 Cache: %dK (%d bytes/line)\n",
		c->x86_cache_size, ecx & 0xFF);
	}

	if (n >= 0x80000007)
		cpuid(0x80000007, &dummy, &dummy, &dummy, &c->x86_power); 
	if (n >= 0x80000008) {
		cpuid(0x80000008, &eax, &dummy, &dummy, &dummy); 
		c->x86_virt_bits = (eax >> 8) & 0xff;
		c->x86_phys_bits = eax & 0xff;
	}
}


static int __init init_amd(struct cpuinfo_x86 *c)
{
	int r;
	int level;

	/* Bit 31 in normal CPUID used for nonstandard 3DNow ID;
	   3DNow is IDd by bit 31 in extended CPUID (1*32+31) anyway */
	clear_bit(0*32+31, &c->x86_capability);
	
	/* C-stepping K8? */
	level = cpuid_eax(1);
	if ((level >= 0x0f48 && level < 0x0f50) || level >= 0x0f58)
		set_bit(X86_FEATURE_K8_C, &c->x86_capability);

	r = get_model_name(c);
	if (!r) { 
		switch (c->x86) { 
		case 15:
			/* Should distinguish Models here, but this is only
			   a fallback anyways. */
			strcpy(c->x86_model_id, "Hammer");
			break; 
		} 
	} 
	display_cacheinfo(c);
	return r;
}

static void __init detect_ht(void)
{
#ifdef CONFIG_SMP
	u32 	eax, ebx, ecx, edx;
	int 	index_lsb, index_msb, tmp;
	int	initial_apic_id;
	int 	cpu = smp_processor_id();
	
	cpuid(1, &eax, &ebx, &ecx, &edx);
	smp_num_siblings = (ebx & 0xff0000) >> 16;
	
	if (smp_num_siblings == 1) {
		printk(KERN_INFO  "CPU: Hyper-Threading is disabled\n");
	} else if (smp_num_siblings > 1) {
		index_lsb = 0;
		index_msb = 31;
		/*
		 * At this point we only support two siblings per
		 * processor package.
		 */
		if (smp_num_siblings > NR_CPUS) {
			printk(KERN_WARNING "CPU: Unsupported number of the siblings %d", smp_num_siblings);
			smp_num_siblings = 1;
			return;
		}
		tmp = smp_num_siblings;
		while ((tmp & 1) == 0) {
			tmp >>=1 ;
			index_lsb++;
		}
		tmp = smp_num_siblings;
		while ((tmp & 0x80000000 ) == 0) {
			tmp <<=1 ;
			index_msb--;
		}
		if (index_lsb != index_msb )
			index_msb++;
		initial_apic_id = ebx >> 24 & 0xff;
		phys_proc_id[cpu] = initial_apic_id >> index_msb;
		
		printk(KERN_INFO  "CPU: Physical Processor ID: %d\n",
		       phys_proc_id[cpu]);
	}
#endif
}
	
#define LVL_1_INST	1
#define LVL_1_DATA	2
#define LVL_2		3
#define LVL_3		4
#define LVL_TRACE	5

struct _cache_table
{
	unsigned char descriptor;
	char cache_type;
	short size;
};

/* all the cache descriptor types we care about (no TLB or trace cache entries) */
static struct _cache_table cache_table[] __initdata =
{
	{ 0x06, LVL_1_INST, 8 },
	{ 0x08, LVL_1_INST, 16 },
	{ 0x0a, LVL_1_DATA, 8 },
	{ 0x0c, LVL_1_DATA, 16 },
	{ 0x22, LVL_3,      512 },
	{ 0x23, LVL_3,      1024 },
	{ 0x25, LVL_3,      2048 },
	{ 0x29, LVL_3,      4096 },
	{ 0x2c, LVL_1_DATA, 32 },
	{ 0x30, LVL_1_INST, 32 },
	{ 0x39, LVL_2,      128 },
	{ 0x3b, LVL_2,      128 },
	{ 0x3c, LVL_2,      256 },
	{ 0x41, LVL_2,      128 },
	{ 0x42, LVL_2,      256 },
	{ 0x43, LVL_2,      512 },
	{ 0x44, LVL_2,      1024 },
	{ 0x45, LVL_2,      2048 },
	{ 0x66, LVL_1_DATA, 8 },
	{ 0x67, LVL_1_DATA, 16 },
	{ 0x68, LVL_1_DATA, 32 },
	{ 0x70, LVL_TRACE,  12 },
	{ 0x71, LVL_TRACE,  16 },
	{ 0x72, LVL_TRACE,  32 },
	{ 0x79, LVL_2,      128 },
	{ 0x7a, LVL_2,      256 },
	{ 0x7b, LVL_2,      512 },
	{ 0x7c, LVL_2,      1024 },
	{ 0x82, LVL_2,      256 },
	{ 0x83, LVL_2,      512 },
	{ 0x84, LVL_2,      1024 },
	{ 0x85, LVL_2,      2048 },
	{ 0x86, LVL_2,      512 },
	{ 0x87, LVL_2,      1024 },
	{ 0x00, 0, 0}
};

static void __init init_intel(struct cpuinfo_x86 *c)
{
	/* Cache sizes */
	unsigned int trace = 0, l1i = 0, l1d = 0, l2 = 0, l3 = 0; 
	unsigned n;

	select_idle_routine(c);
	if (c->cpuid_level > 1) {
		/* supports eax=2  call */
		int i, j, n;
		int regs[4];
		unsigned char *dp = (unsigned char *)regs;

		/* Number of times to iterate */
		n = cpuid_eax(2) & 0xFF;

		for ( i = 0 ; i < n ; i++ ) {
			cpuid(2, &regs[0], &regs[1], &regs[2], &regs[3]);
			
			/* If bit 31 is set, this is an unknown format */
			for ( j = 0 ; j < 3 ; j++ ) {
				if ( regs[j] < 0 ) regs[j] = 0;
			}

			/* Byte 0 is level count, not a descriptor */
			for ( j = 1 ; j < 16 ; j++ ) {
				unsigned char des = dp[j];
				unsigned char k = 0;

				/* look up this descriptor in the table */
				while (cache_table[k].descriptor != 0)
				{
					if (cache_table[k].descriptor == des) {
						switch (cache_table[k].cache_type) {
						case LVL_1_INST:
							l1i += cache_table[k].size;
							break;
						case LVL_1_DATA:
							l1d += cache_table[k].size;
							break;
						case LVL_2:
							l2 += cache_table[k].size;
							break;
						case LVL_3:
							l3 += cache_table[k].size;
							break;
						case LVL_TRACE:
							trace += cache_table[k].size;
							break;
						}

						break;
					}

					k++;
				}
			}
		}

		if (trace)
			printk (KERN_INFO "CPU: Trace cache: %dK uops", trace);
		else if (l1i)
			printk (KERN_INFO "CPU: L1 I cache: %dK", l1i);
		if (l1d)
			printk(", L1 D cache: %dK\n", l1d);
		else
			printk("\n"); 
		if (l2)
			printk(KERN_INFO "CPU: L2 cache: %dK\n", l2);
		if (l3)
			printk(KERN_INFO "CPU: L3 cache: %dK\n", l3);

		c->x86_cache_size = l2 ? l2 : (l1i+l1d);
	}

	if (cpu_has(c, X86_FEATURE_HT))
		detect_ht(); 

	n = cpuid_eax(0x80000000);
	if (n >= 0x80000008) {
		unsigned eax = cpuid_eax(0x80000008);
		c->x86_virt_bits = (eax >> 8) & 0xff;
		c->x86_phys_bits = eax & 0xff;
	}
}

void __init get_cpu_vendor(struct cpuinfo_x86 *c)
{
	char *v = c->x86_vendor_id;

	if (!strcmp(v, "AuthenticAMD"))
		c->x86_vendor = X86_VENDOR_AMD;
	else if (!strcmp(v, "GenuineIntel"))
		c->x86_vendor = X86_VENDOR_INTEL;
	else
		c->x86_vendor = X86_VENDOR_UNKNOWN;
}

struct cpu_model_info {
	int vendor;
	int family;
	char *model_names[16];
};

/* Do some early cpuid on the boot CPU to get some parameter that are
   needed before check_bugs. Everything advanced is in identify_cpu
   below. */
void __init early_identify_cpu(struct cpuinfo_x86 *c)
{
	u32 tfms;

	c->loops_per_jiffy = loops_per_jiffy;
	c->x86_cache_size = -1;
	c->x86_vendor = X86_VENDOR_UNKNOWN;
	c->x86_model = c->x86_mask = 0;	/* So far unknown... */
	c->x86_vendor_id[0] = '\0'; /* Unset */
	c->x86_model_id[0] = '\0';  /* Unset */
	c->x86_clflush_size = 64;
	memset(&c->x86_capability, 0, sizeof c->x86_capability);

	/* Get vendor name */
	cpuid(0x00000000, &c->cpuid_level,
	      (int *)&c->x86_vendor_id[0],
	      (int *)&c->x86_vendor_id[8],
	      (int *)&c->x86_vendor_id[4]);
		
	get_cpu_vendor(c);

	/* Initialize the standard set of capabilities */
	/* Note that the vendor-specific code below might override */

	/* Intel-defined flags: level 0x00000001 */
	if (c->cpuid_level >= 0x00000001) {
		__u32 misc;
		cpuid(0x00000001, &tfms, &misc, &c->x86_capability[4],
		      &c->x86_capability[0]);
		c->x86 = (tfms >> 8) & 0xf;
		c->x86_model = (tfms >> 4) & 0xf;
		c->x86_mask = tfms & 0xf;
		if (c->x86 == 0xf) {
			c->x86 += (tfms >> 20) & 0xff;
			c->x86_model += ((tfms >> 16) & 0xF) << 4;
		} 
		if (c->x86_capability[0] & (1<<19)) 
       		c->x86_clflush_size = ((misc >> 8) & 0xff) * 8;
	} else {
		/* Have CPUID level 0 only - unheard of */
		c->x86 = 4;
	}
}

/*
 * This does the hard work of actually picking apart the CPU stuff...
 */
void __init identify_cpu(struct cpuinfo_x86 *c)
{
	int i;
	u32 xlvl;

	early_identify_cpu(c);

	/* AMD-defined flags: level 0x80000001 */
	xlvl = cpuid_eax(0x80000000);
	if ( (xlvl & 0xffff0000) == 0x80000000 ) {
		if ( xlvl >= 0x80000001 )
			c->x86_capability[1] = cpuid_edx(0x80000001);
		if ( xlvl >= 0x80000004 )
			get_model_name(c); /* Default name */
	}

	/* Transmeta-defined flags: level 0x80860001 */
	xlvl = cpuid_eax(0x80860000);
	if ( (xlvl & 0xffff0000) == 0x80860000 ) {
		if (  xlvl >= 0x80860001 )
			c->x86_capability[2] = cpuid_edx(0x80860001);
	}

	/*
	 * Vendor-specific initialization.  In this section we
	 * canonicalize the feature flags, meaning if there are
	 * features a certain CPU supports which CPUID doesn't
	 * tell us, CPUID claiming incorrect flags, or other bugs,
	 * we handle them here.
	 *
	 * At the end of this section, c->x86_capability better
	 * indicate the features this CPU genuinely supports!
	 */
	switch ( c->x86_vendor ) {

		case X86_VENDOR_AMD:
			init_amd(c);
			break;

		case X86_VENDOR_INTEL:
			init_intel(c); 
			break; 

		case X86_VENDOR_UNKNOWN:
		default:
			display_cacheinfo(c);
			break;
	}
	
	/*
	 * On SMP, boot_cpu_data holds the common feature set between
	 * all CPUs; so make sure that we indicate which features are
	 * common between the CPUs.  The first time this routine gets
	 * executed, c == &boot_cpu_data.
	 */
	if ( c != &boot_cpu_data ) {
		/* AND the already accumulated flags with these */
		for ( i = 0 ; i < NCAPINTS ; i++ )
			boot_cpu_data.x86_capability[i] &= c->x86_capability[i];
	}

	mcheck_init(c);
}
 

void __init print_cpu_info(struct cpuinfo_x86 *c)
{
	if (c->x86_model_id[0])
		printk("%s", c->x86_model_id);

	if (c->x86_mask || c->cpuid_level >= 0) 
		printk(" stepping %02x\n", c->x86_mask);
	else
		printk("\n");
}

/*
 *	Get CPU information for use by the procfs.
 */

static int show_cpuinfo(struct seq_file *m, void *v)
{
	struct cpuinfo_x86 *c = v;

	/* 
	 * These flag bits must match the definitions in <asm/cpufeature.h>.
	 * NULL means this bit is undefined or reserved; either way it doesn't
	 * have meaning as far as Linux is concerned.  Note that it's important
	 * to realize there is a difference between this table and CPUID -- if
	 * applications want to get the raw CPUID data, they should access
	 * /dev/cpu/<cpu_nr>/cpuid instead.
	 */
	static char *x86_cap_flags[] = {
		/* Intel-defined */
	        "fpu", "vme", "de", "pse", "tsc", "msr", "pae", "mce",
	        "cx8", "apic", NULL, "sep", "mtrr", "pge", "mca", "cmov",
	        "pat", "pse36", "pn", "clflush", NULL, "dts", "acpi", "mmx",
	        "fxsr", "sse", "sse2", "ss", "ht", "tm", "ia64", NULL,

		/* AMD-defined */
		NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
		NULL, NULL, NULL, "syscall", NULL, NULL, NULL, NULL,
		NULL, NULL, NULL, NULL, "nx", NULL, "mmxext", NULL,
		NULL, NULL, NULL, NULL, NULL, "lm", "3dnowext", "3dnow",

		/* Transmeta-defined */
		"recovery", "longrun", NULL, "lrti", NULL, NULL, NULL, NULL,
		NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
		NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
		NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,

		/* Other (Linux-defined) */
		"cxmmx", "k6_mtrr", "cyrix_arr", "centaur_mcr", NULL, NULL, NULL, NULL,
		NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
		NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
		NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,

		/* Intel-defined (#2) */
		"pni", NULL, NULL, "monitor", "ds_cpl", NULL, NULL, "tm2",
		"est", NULL, "cid", NULL, NULL, "cmpxchg16b", NULL, NULL,
		NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
		NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
	};
	static char *x86_power_flags[] = { 
		"ts",	/* temperature sensor */
		"fid",  /* frequency id control */
		"vid",  /* voltage id control */
		"ttp",  /* thermal trip */
	};


#ifdef CONFIG_SMP
	if (!cpu_online(c-cpu_data))
		return 0;
#endif

	seq_printf(m,"processor\t: %u\n"
		     "vendor_id\t: %s\n"
		     "cpu family\t: %d\n"
		     "model\t\t: %d\n"
		     "model name\t: %s\n",
		     (unsigned)(c-cpu_data),
		     c->x86_vendor_id[0] ? c->x86_vendor_id : "unknown",
		     c->x86,
		     (int)c->x86_model,
		     c->x86_model_id[0] ? c->x86_model_id : "unknown");
	
	if (c->x86_mask || c->cpuid_level >= 0)
		seq_printf(m, "stepping\t: %d\n", c->x86_mask);
	else
		seq_printf(m, "stepping\t: unknown\n");
	
	if (cpu_has(c,X86_FEATURE_TSC)) {
		seq_printf(m, "cpu MHz\t\t: %u.%03u\n",
			     cpu_khz / 1000, (cpu_khz % 1000));
	}

	/* Cache size */
	if (c->x86_cache_size >= 0) 
		seq_printf(m, "cache size\t: %d KB\n", c->x86_cache_size);
	
#ifdef CONFIG_X86_HT
	if (cpu_has_ht) {
		seq_printf(m, "physical id\t: %d\n", phys_proc_id[c - cpu_data]);
		seq_printf(m, "siblings\t: %d\n", smp_num_siblings);
	}
#endif	

	seq_printf(m,
	        "fpu\t\t: yes\n"
	        "fpu_exception\t: yes\n"
	        "cpuid level\t: %d\n"
	        "wp\t\t: yes\n"
	        "flags\t\t:",
		   c->cpuid_level);

	{ 
		int i; 
		for ( i = 0 ; i < 32*NCAPINTS ; i++ )
			if ( test_bit(i, &c->x86_capability) &&
			     x86_cap_flags[i] != NULL )
				seq_printf(m, " %s", x86_cap_flags[i]);
	}
		
	seq_printf(m, "\nbogomips\t: %lu.%02lu\n",
		   c->loops_per_jiffy/(500000/HZ),
		   (c->loops_per_jiffy/(5000/HZ)) % 100);

	if (c->x86_tlbsize > 0) 
		seq_printf(m, "TLB size\t: %d 4K pages\n", c->x86_tlbsize);
	seq_printf(m, "clflush size\t: %d\n", c->x86_clflush_size);

	seq_printf(m, "address sizes\t: %u bits physical, %u bits virtual\n", 
		   c->x86_phys_bits, c->x86_virt_bits);

	seq_printf(m, "power management:");
	{
		unsigned i;
		for (i = 0; i < 32; i++) 
			if (c->x86_power & (1 << i)) {
				if (i < ARRAY_SIZE(x86_power_flags))
					seq_printf(m, " %s", x86_power_flags[i]);
				else
					seq_printf(m, " [%d]", i);
			}
	}

	seq_printf(m, "\n\n"); 

	return 0;
}

static void *c_start(struct seq_file *m, loff_t *pos)
{
	return *pos < NR_CPUS ? cpu_data + *pos : NULL;
}

static void *c_next(struct seq_file *m, void *v, loff_t *pos)
{
	++*pos;
	return c_start(m, pos);
}

static void c_stop(struct seq_file *m, void *v)
{
}

struct seq_operations cpuinfo_op = {
	.start =c_start,
	.next =	c_next,
	.stop =	c_stop,
	.show =	show_cpuinfo,
};
