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
#include <linux/apm_bios.h>
#ifdef CONFIG_BLK_DEV_RAM
#include <linux/blk.h>
#endif
#include <linux/highmem.h>
#include <linux/bootmem.h>
#include <asm/processor.h>
#include <linux/console.h>
#include <linux/seq_file.h>
#include <asm/mtrr.h>
#include <asm/uaccess.h>
#include <asm/system.h>
#include <asm/io.h>
#include <asm/smp.h>
#include <asm/msr.h>
#include <asm/desc.h>
#include <asm/e820.h>
#include <asm/dma.h>
#include <asm/mpspec.h>
#include <asm/mmu_context.h>
#include <asm/bootsetup.h>
#include <asm/smp.h>

/*
 * Machine setup..
 */

extern void mcheck_init(struct cpuinfo_x86 *c);
extern void init_memory_mapping(void);

struct cpuinfo_x86 boot_cpu_data = { 0, 0, 0, 0, -1, 1, 0, 0, -1 };

unsigned long mmu_cr4_features;

/* For PCI or other memory-mapped resources */
unsigned long pci_mem_start = 0x10000000;

/*
 * Setup options
 */
struct drive_info_struct { char dummy[32]; } drive_info;
struct screen_info screen_info;
struct sys_desc_table_struct {
	unsigned short length;
	unsigned char table[0];
};

struct e820map e820;

unsigned char aux_device_present;

extern int root_mountflags;
extern char _text, _etext, _edata, _end;

static int disable_x86_fxsr __initdata = 0;

static char command_line[COMMAND_LINE_SIZE];
       char saved_command_line[COMMAND_LINE_SIZE];

struct resource standard_io_resources[] = {
	{ "dma1", 0x00, 0x1f, IORESOURCE_BUSY },
	{ "pic1", 0x20, 0x3f, IORESOURCE_BUSY },
	{ "timer", 0x40, 0x5f, IORESOURCE_BUSY },
	{ "keyboard", 0x60, 0x6f, IORESOURCE_BUSY },
	{ "dma page reg", 0x80, 0x8f, IORESOURCE_BUSY },
	{ "pic2", 0xa0, 0xbf, IORESOURCE_BUSY },
	{ "dma2", 0xc0, 0xdf, IORESOURCE_BUSY },
	{ "fpu", 0xf0, 0xff, IORESOURCE_BUSY }
};

#define STANDARD_IO_RESOURCES (sizeof(standard_io_resources)/sizeof(struct resource))

static struct resource code_resource = { "Kernel code", 0x100000, 0 };
static struct resource data_resource = { "Kernel data", 0, 0 };
static struct resource vram_resource = { "Video RAM area", 0xa0000, 0xbffff, IORESOURCE_BUSY };

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

void __init add_memory_region(unsigned long long start,
                                  unsigned long long size, int type)
{
	int x = e820.nr_map;

	if (x == E820MAX) {
	    printk(KERN_ERR "Ooops! Too many entries in the memory map!\n");
	    return;
	}

	e820.map[x].addr = start;
	e820.map[x].size = size;
	e820.map[x].type = type;
	e820.nr_map++;
} /* add_memory_region */

#define E820_DEBUG	1

static void __init print_memory_map(char *who)
{
	int i;

	for (i = 0; i < e820.nr_map; i++) {
		printk(" %s: %016Lx - %016Lx ", who,
			(unsigned long long) e820.map[i].addr,
			(unsigned long long) (e820.map[i].addr + e820.map[i].size));
		switch (e820.map[i].type) {
		case E820_RAM:	printk("(usable)\n");
				break;
		case E820_RESERVED:
				printk("(reserved)\n");
				break;
		case E820_ACPI:
				printk("(ACPI data)\n");
				break;
		case E820_NVS:
				printk("(ACPI NVS)\n");
				break;
		default:	printk("type %u\n", e820.map[i].type);
				break;
		}
	}
}

/*
 * Sanitize the BIOS e820 map.
 *
 * Some e820 responses include overlapping entries.  The following 
 * replaces the original e820 map with a new one, removing overlaps.
 *
 */
static int __init sanitize_e820_map(struct e820entry * biosmap, char * pnr_map)
{
	struct change_member {
		struct e820entry *pbios; /* pointer to original bios entry */
		unsigned long long addr; /* address for this change point */
	};
	struct change_member change_point_list[2*E820MAX];
	struct change_member *change_point[2*E820MAX];
	struct e820entry *overlap_list[E820MAX];
	struct e820entry new_bios[E820MAX];
	struct change_member *change_tmp;
	unsigned long current_type, last_type;
	unsigned long long last_addr;
	int chgidx, still_changing;
	int overlap_entries;
	int new_bios_entry;
	int old_nr, new_nr;
	int i;

	/*
		Visually we're performing the following (1,2,3,4 = memory types)...

		Sample memory map (w/overlaps):
		   ____22__________________
		   ______________________4_
		   ____1111________________
		   _44_____________________
		   11111111________________
		   ____________________33__
		   ___________44___________
		   __________33333_________
		   ______________22________
		   ___________________2222_
		   _________111111111______
		   _____________________11_
		   _________________4______

		Sanitized equivalent (no overlap):
		   1_______________________
		   _44_____________________
		   ___1____________________
		   ____22__________________
		   ______11________________
		   _________1______________
		   __________3_____________
		   ___________44___________
		   _____________33_________
		   _______________2________
		   ________________1_______
		   _________________4______
		   ___________________2____
		   ____________________33__
		   ______________________4_
	*/

	/* if there's only one memory region, don't bother */
	if (*pnr_map < 2)
		return -1;

	old_nr = *pnr_map;

	/* bail out if we find any unreasonable addresses in bios map */
	for (i=0; i<old_nr; i++)
		if (biosmap[i].addr + biosmap[i].size < biosmap[i].addr)
			return -1;

	/* create pointers for initial change-point information (for sorting) */
	for (i=0; i < 2*old_nr; i++)
		change_point[i] = &change_point_list[i];

	/* record all known change-points (starting and ending addresses) */
	chgidx = 0;
	for (i=0; i < old_nr; i++)	{
		change_point[chgidx]->addr = biosmap[i].addr;
		change_point[chgidx++]->pbios = &biosmap[i];
		change_point[chgidx]->addr = biosmap[i].addr + biosmap[i].size;
		change_point[chgidx++]->pbios = &biosmap[i];
	}

	/* sort change-point list by memory addresses (low -> high) */
	still_changing = 1;
	while (still_changing)	{
		still_changing = 0;
		for (i=1; i < 2*old_nr; i++)  {
			/* if <current_addr> > <last_addr>, swap */
			/* or, if current=<start_addr> & last=<end_addr>, swap */
			if ((change_point[i]->addr < change_point[i-1]->addr) ||
				((change_point[i]->addr == change_point[i-1]->addr) &&
				 (change_point[i]->addr == change_point[i]->pbios->addr) &&
				 (change_point[i-1]->addr != change_point[i-1]->pbios->addr))
			   )
			{
				change_tmp = change_point[i];
				change_point[i] = change_point[i-1];
				change_point[i-1] = change_tmp;
				still_changing=1;
			}
		}
	}

	/* create a new bios memory map, removing overlaps */
	overlap_entries=0;	 /* number of entries in the overlap table */
	new_bios_entry=0;	 /* index for creating new bios map entries */
	last_type = 0;		 /* start with undefined memory type */
	last_addr = 0;		 /* start with 0 as last starting address */
	/* loop through change-points, determining affect on the new bios map */
	for (chgidx=0; chgidx < 2*old_nr; chgidx++)
	{
		/* keep track of all overlapping bios entries */
		if (change_point[chgidx]->addr == change_point[chgidx]->pbios->addr)
		{
			/* add map entry to overlap list (> 1 entry implies an overlap) */
			overlap_list[overlap_entries++]=change_point[chgidx]->pbios;
		}
		else
		{
			/* remove entry from list (order independent, so swap with last) */
			for (i=0; i<overlap_entries; i++)
			{
				if (overlap_list[i] == change_point[chgidx]->pbios)
					overlap_list[i] = overlap_list[overlap_entries-1];
			}
			overlap_entries--;
		}
		/* if there are overlapping entries, decide which "type" to use */
		/* (larger value takes precedence -- 1=usable, 2,3,4,4+=unusable) */
		current_type = 0;
		for (i=0; i<overlap_entries; i++)
			if (overlap_list[i]->type > current_type)
				current_type = overlap_list[i]->type;
		/* continue building up new bios map based on this information */
		if (current_type != last_type)	{
			if (last_type != 0)	 {
				new_bios[new_bios_entry].size =
					change_point[chgidx]->addr - last_addr;
				/* move forward only if the new size was non-zero */
				if (new_bios[new_bios_entry].size != 0)
					if (++new_bios_entry >= E820MAX)
						break; 	/* no more space left for new bios entries */
			}
			if (current_type != 0)	{
				new_bios[new_bios_entry].addr = change_point[chgidx]->addr;
				new_bios[new_bios_entry].type = current_type;
				last_addr=change_point[chgidx]->addr;
			}
			last_type = current_type;
		}
	}
	new_nr = new_bios_entry;   /* retain count for new bios entries */

	/* copy new bios mapping into original location */
	memcpy(biosmap, new_bios, new_nr*sizeof(struct e820entry));
	*pnr_map = new_nr;

	return 0;
}

/*
 * Copy the BIOS e820 map into a safe place.
 *
 * Sanity-check it while we're at it..
 *
 * If we're lucky and live on a modern system, the setup code
 * will have given us a memory map that we can use to properly
 * set up memory.  If we aren't, we'll fake a memory map.
 *
 * We check to see that the memory map contains at least 2 elements
 * before we'll use it, because the detection code in setup.S may
 * not be perfect and most every PC known to man has two memory
 * regions: one from 0 to 640k, and one from 1mb up.  (The IBM
 * thinkpad 560x, for example, does not cooperate with the memory
 * detection code.)
 */
static int __init copy_e820_map(struct e820entry * biosmap, int nr_map)
{
	/* Only one memory region (or negative)? Ignore it */
	if (nr_map < 2)
		return -1;

	do {
		unsigned long long start = biosmap->addr;
		unsigned long long size = biosmap->size;
		unsigned long long end = start + size;
		unsigned long type = biosmap->type;

		/* Overflow in 64 bits? Ignore the memory map. */
		if (start > end)
			return -1;

		/*
		 * Some BIOSes claim RAM in the 640k - 1M region.
		 * Not right. Fix it up.
		 */
		if (type == E820_RAM) {
			if (start < 0x100000ULL && end > 0xA0000ULL) {
				if (start < 0xA0000ULL)
					add_memory_region(start, 0xA0000ULL-start, type);
				if (end <= 0x100000ULL)
					continue;
				start = 0x100000ULL;
				size = end - start;
			}
		}
		add_memory_region(start, size, type);
	} while (biosmap++,--nr_map);
	return 0;
}

/*
 * Do NOT EVER look at the BIOS memory size location.
 * It does not work on many machines.
 */
#define LOWMEMSIZE()	(0x9f000)

void __init setup_memory_region(void)
{
	char *who = "BIOS-e820";

	/*
	 * Try to copy the BIOS-supplied E820-map.
	 *
	 * Otherwise fake a memory map; one section from 0k->640k,
	 * the next section from 1mb->appropriate_mem_k
	 */
	sanitize_e820_map(E820_MAP, &E820_MAP_NR);
	if (copy_e820_map(E820_MAP, E820_MAP_NR) < 0) {
		unsigned long mem_size;

		/* compare results from other methods and take the greater */
		if (ALT_MEM_K < EXT_MEM_K) {
			mem_size = EXT_MEM_K;
			who = "BIOS-88";
		} else {
			mem_size = ALT_MEM_K;
			who = "BIOS-e801";
		}

		e820.nr_map = 0;
		add_memory_region(0, LOWMEMSIZE(), E820_RAM);
		add_memory_region(HIGH_MEMORY, mem_size << 10, E820_RAM);
  	}
	printk(KERN_INFO "BIOS-provided physical RAM map:\n");
	print_memory_map(who);
} /* setup_memory_region */


static inline void parse_mem_cmdline (char ** cmdline_p)
{
	char c = ' ', *to = command_line, *from = COMMAND_LINE;
	int len = 0;
	int usermem = 0;

	/* Save unparsed command line copy for /proc/cmdline */
	memcpy(saved_command_line, COMMAND_LINE, COMMAND_LINE_SIZE);
	saved_command_line[COMMAND_LINE_SIZE-1] = '\0';

	for (;;) {
		/*
		 * "mem=nopentium" disables the 4MB page tables.
		 * "mem=XXX[kKmM]" defines a memory region from HIGH_MEM
		 * to <mem>, overriding the bios size.
		 * "mem=XXX[KkmM]@XXX[KkmM]" defines a memory region from
		 * <start> to <start>+<mem>, overriding the bios size.
		 */
		if (c == ' ' && !memcmp(from, "mem=", 4)) {
			if (to != command_line)
				to--;
			if (!memcmp(from+4, "nopentium", 9)) {
				from += 9+4;
				clear_bit(X86_FEATURE_PSE, &boot_cpu_data.x86_capability);
			} else if (!memcmp(from+4, "exactmap", 8)) {
				from += 8+4;
				e820.nr_map = 0;
				usermem = 1;
			} else {
				/* If the user specifies memory size, we
				 * blow away any automatically generated
				 * size
				 */
				unsigned long long start_at, mem_size;
 
				if (usermem == 0) {
					/* first time in: zap the whitelist
					 * and reinitialize it with the
					 * standard low-memory region.
					 */
					e820.nr_map = 0;
					usermem = 1;
					add_memory_region(0, LOWMEMSIZE(), E820_RAM);
				}
				mem_size = memparse(from+4, &from);
				if (*from == '@')
					start_at = memparse(from+1, &from);
				else {
					start_at = HIGH_MEMORY;
					mem_size -= HIGH_MEMORY;
					usermem=0;
				}
				add_memory_region(start_at, mem_size, E820_RAM);
			}
		}
		c = *(from++);
		if (!c)
			break;
		if (COMMAND_LINE_SIZE <= ++len)
			break;
		*(to++) = c;
	}
	*to = '\0';
	*cmdline_p = command_line;
	if (usermem) {
		printk(KERN_INFO "user-defined physical RAM map:\n");
		print_memory_map("user");
	}
}

unsigned long start_pfn, end_pfn; 

void __init setup_arch(char **cmdline_p)
{
	unsigned long bootmap_size, low_mem_size;
	int i;

 	ROOT_DEV = to_kdev_t(ORIG_ROOT_DEV);
 	drive_info = DRIVE_INFO;
 	screen_info = SCREEN_INFO;
	aux_device_present = AUX_DEVICE_INFO;

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

	parse_mem_cmdline(cmdline_p);

#define PFN_UP(x)	(((x) + PAGE_SIZE-1) >> PAGE_SHIFT)
#define PFN_DOWN(x)	((x) >> PAGE_SHIFT)
#define PFN_PHYS(x)	((x) << PAGE_SHIFT)

#define MAXMEM		(120UL * 1024 * 1024 * 1024 * 1024)  /* 120TB */ 
#define MAXMEM_PFN	PFN_DOWN(MAXMEM)
#define MAX_NONPAE_PFN	(1 << 20)

	/*
	 * partially used pages are not usable - thus
	 * we are rounding upwards:
	 */
	start_pfn = PFN_UP(__pa_symbol(&_end));

	/*
	 * Find the highest page frame number we have available
	 */
	end_pfn = 0;
	for (i = 0; i < e820.nr_map; i++) {
		unsigned long start, end;
		/* RAM? */
		if (e820.map[i].type != E820_RAM)
			continue;
		start = PFN_UP(e820.map[i].addr);
		end = PFN_DOWN(e820.map[i].addr + e820.map[i].size);
		if (start >= end)
			continue;
		if (end > end_pfn)
			end_pfn = end;
	}

	if (end_pfn > MAXMEM_PFN) {
		end_pfn = MAXMEM_PFN;
	}

	init_memory_mapping(); 

	/*
	 * Initialize the boot-time allocator (with low memory only):
	 */
	bootmap_size = init_bootmem(start_pfn, end_pfn);

	/*
	 * Register fully available low RAM pages with the bootmem allocator.
	 */
	for (i = 0; i < e820.nr_map; i++) {
		unsigned long curr_pfn, last_pfn, size;
 		/*
		 * Reserve usable low memory
		 */
		if (e820.map[i].type != E820_RAM)
			continue;
		/*
		 * We are rounding up the start address of usable memory:
		 */
		curr_pfn = PFN_UP(e820.map[i].addr);
		if (curr_pfn >= end_pfn)
			continue;
		/*
		 * ... and at the end of the usable range downwards:
		 */
		last_pfn = PFN_DOWN(e820.map[i].addr + e820.map[i].size);

		if (last_pfn > end_pfn)
			last_pfn = end_pfn;

		/*
		 * .. finally, did all the rounding and playing
		 * around just make the area go away?
		 */
		if (last_pfn <= curr_pfn)
			continue;

		size = last_pfn - curr_pfn;
		free_bootmem(PFN_PHYS(curr_pfn), PFN_PHYS(size));
	}
	/*
	 * Reserve the bootmem bitmap itself as well. We do this in two
	 * steps (first step was init_bootmem()) because this catches
	 * the (very unlikely) case of us accidentally initializing the
	 * bootmem allocator with an invalid RAM area.
	 */
	reserve_bootmem(HIGH_MEMORY, (PFN_PHYS(start_pfn) +
			 bootmap_size + PAGE_SIZE-1) - (HIGH_MEMORY));

	/*
	 * reserve physical page 0 - it's a special BIOS page on many boxes,
	 * enabling clean reboots, SMP operation, laptop functions.
	 */
	reserve_bootmem(0, PAGE_SIZE);

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
#ifdef CONFIG_X86_LOCAL_APIC
	/*
	 * Find and reserve possible boot-time SMP configuration:
	 */
	find_smp_config();
#endif
#ifdef CONFIG_BLK_DEV_INITRD
	if (LOADER_TYPE && INITRD_START) {
		if (INITRD_START + INITRD_SIZE <= (max_low_pfn << PAGE_SHIFT)) {
			reserve_bootmem(INITRD_START, INITRD_SIZE);
			initrd_start =
				INITRD_START ? INITRD_START + PAGE_OFFSET : 0;
			initrd_end = initrd_start+INITRD_SIZE;
		}
		else {
			printk(KERN_ERR "initrd extends beyond end of memory "
			    "(0x%08lx > 0x%08lx)\ndisabling initrd\n",
			    (unsigned long)(INITRD_START + INITRD_SIZE),
			    (unsigned long)(max_low_pfn << PAGE_SHIFT));
			initrd_start = 0;
		}
	}
#endif

	/*
	 * NOTE: before this point _nobody_ is allowed to allocate
	 * any memory using the bootmem allocator.
	 */

#ifdef CONFIG_SMP
	smp_alloc_memory(); /* AP processor realmode stacks in low memory*/
#endif
	paging_init();
#ifdef CONFIG_ACPI_BOOT
       /*
        * Initialize the ACPI boot-time table parser (gets the RSDP and SDT).
        * Must do this after paging_init (due to reliance on fixmap, and thus
        * the bootmem allocator) but before get_smp_config (to allow parsing
        * of MADT).
        */
       acpi_boot_init(*cmdline_p);
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
	for (i = 0; i < e820.nr_map; i++) {
		struct resource *res;
		if (e820.map[i].addr + e820.map[i].size > 0x100000000ULL)
			continue;
		res = alloc_bootmem_low(sizeof(struct resource));
		switch (e820.map[i].type) {
		case E820_RAM:	res->name = "System RAM"; break;
		case E820_ACPI:	res->name = "ACPI Tables"; break;
		case E820_NVS:	res->name = "ACPI Non-volatile Storage"; break;
		default:	res->name = "reserved";
		}
		res->start = e820.map[i].addr;
		res->end = res->start + e820.map[i].size - 1;
		res->flags = IORESOURCE_MEM | IORESOURCE_BUSY;
		request_resource(&iomem_resource, res);
		if (e820.map[i].type == E820_RAM) {
			/*
			 *  We dont't know which RAM region contains kernel data,
			 *  so we try it repeatedly and let the resource manager
			 *  test it.
			 */
			request_resource(res, &code_resource);
			request_resource(res, &data_resource);
		}
	}
	request_resource(&iomem_resource, &vram_resource);

	/* request I/O space for devices used on all i[345]86 PCs */
	for (i = 0; i < STANDARD_IO_RESOURCES; i++)
		request_resource(&ioport_resource, standard_io_resources+i);

	/* Tell the PCI layer not to allocate too close to the RAM area.. */
	/* ??? move this up on x86-64 */
	low_mem_size = ((max_low_pfn << PAGE_SHIFT) + 0xfffff) & ~0xfffff;
	if (low_mem_size > pci_mem_start)
		pci_mem_start = low_mem_size;

#ifdef CONFIG_VT
#if defined(CONFIG_VGA_CONSOLE)
	conswitchp = &vga_con;
#elif defined(CONFIG_DUMMY_CONSOLE)
	conswitchp = &dummy_con;
#endif
#endif
}

#ifndef CONFIG_X86_TSC
static int tsc_disable __initdata = 0;

static int __init tsc_setup(char *str)
{
	tsc_disable = 1;
	return 1;
}

__setup("notsc", tsc_setup);
#endif

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
	unsigned int n, dummy, ecx, edx;

	n = cpuid_eax(0x80000000);

	if (n >= 0x80000005) {
		cpuid(0x80000005, &dummy, &dummy, &ecx, &edx);
		printk(KERN_INFO "CPU: L1 I Cache: %dK (%d bytes/line), D cache %dK (%d bytes/line)\n",
			edx>>24, edx&0xFF, ecx>>24, ecx&0xFF);
		c->x86_cache_size=(ecx>>24)+(edx>>24);	
	}

	if (n < 0x80000006)
		return;

	ecx = cpuid_ecx(0x80000006);
	c->x86_cache_size = ecx >> 16;

	printk(KERN_INFO "CPU: L2 Cache: %dK (%d bytes/line)\n",
		c->x86_cache_size, ecx & 0xFF);
}


static int __init init_amd(struct cpuinfo_x86 *c)
{
	int r;

	/* Bit 31 in normal CPUID used for nonstandard 3DNow ID;
	   3DNow is IDd by bit 31 in extended CPUID (1*32+31) anyway */
	clear_bit(0*32+31, &c->x86_capability);
	
	r = get_model_name(c);
	if (!r) { 
		switch (c->x86) { 
		case 15:
			/* Should distingush Models here, but this is only
			   a fallback anyways. */
			strcpy(c->x86_model_id, "Hammer");
			break; 
		} 
	} 
	display_cacheinfo(c);
	return r;
}


void __init get_cpu_vendor(struct cpuinfo_x86 *c)
{
	char *v = c->x86_vendor_id;

	if (!strcmp(v, "AuthenticAMD"))
		c->x86_vendor = X86_VENDOR_AMD;
	else
		c->x86_vendor = X86_VENDOR_UNKNOWN;
}

struct cpu_model_info {
	int vendor;
	int family;
	char *model_names[16];
};

int __init x86_fxsr_setup(char * s)
{
	disable_x86_fxsr = 1;
	return 1;
}
__setup("nofxsr", x86_fxsr_setup);



/*
 * This does the hard work of actually picking apart the CPU stuff...
 */
void __init identify_cpu(struct cpuinfo_x86 *c)
{
	int junk, i;
	u32 xlvl, tfms;

	c->loops_per_jiffy = loops_per_jiffy;
	c->x86_cache_size = -1;
	c->x86_vendor = X86_VENDOR_UNKNOWN;
	c->x86_model = c->x86_mask = 0;	/* So far unknown... */
	c->x86_vendor_id[0] = '\0'; /* Unset */
	c->x86_model_id[0] = '\0';  /* Unset */
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
	if ( c->cpuid_level >= 0x00000001 ) {
		cpuid(0x00000001, &tfms, &junk, &junk,
		      &c->x86_capability[0]);
		c->x86 = (tfms >> 8) & 15;
		c->x86_model = (tfms >> 4) & 15;
		c->x86_mask = tfms & 15;
	} else {
		/* Have CPUID level 0 only - unheard of */
		c->x86 = 4;
	}

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


	printk(KERN_DEBUG "CPU: Before vendor init, caps: %08x %08x %08x, vendor = %d\n",
	       c->x86_capability[0],
	       c->x86_capability[1],
	       c->x86_capability[2],
	       c->x86_vendor);

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

		case X86_VENDOR_UNKNOWN:
		default:
			/* Not much we can do here... */
			break;
	}
	
	printk(KERN_DEBUG "CPU: After vendor init, caps: %08x %08x %08x %08x\n",
	       c->x86_capability[0],
	       c->x86_capability[1],
	       c->x86_capability[2],
	       c->x86_capability[3]);

	/*
	 * The vendor-specific functions might have changed features.  Now
	 * we do "generic changes."
	 */

	/* TSC disabled? */
#ifndef CONFIG_X86_TSC
	if ( tsc_disable )
		clear_bit(X86_FEATURE_TSC, &c->x86_capability);
#endif

	/* FXSR disabled? */
	if (disable_x86_fxsr) {
		clear_bit(X86_FEATURE_FXSR, &c->x86_capability);
		clear_bit(X86_FEATURE_XMM, &c->x86_capability);
	}

	/* Now the feature flags better reflect actual CPU features! */

	printk(KERN_DEBUG "CPU:     After generic, caps: %08x %08x %08x %08x\n",
	       c->x86_capability[0],
	       c->x86_capability[1],
	       c->x86_capability[2],
	       c->x86_capability[3]);

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

	printk(KERN_DEBUG "CPU:             Common caps: %08x %08x %08x %08x\n",
	       boot_cpu_data.x86_capability[0],
	       boot_cpu_data.x86_capability[1],
	       boot_cpu_data.x86_capability[2],
	       boot_cpu_data.x86_capability[3]);
}
 

void __init print_cpu_info(struct cpuinfo_x86 *c)
{
	if (c->x86_model_id[0])
		printk("AMD %s", c->x86_model_id);

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
	        "fxsr", "sse", "sse2", "ss", NULL, "tm", "ia64", NULL,

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
	};

#ifdef CONFIG_SMP
	if (!(cpu_online_map & (1<<(c-cpu_data))))
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
	
	if ( test_bit(X86_FEATURE_TSC, &c->x86_capability) ) {
		seq_printf(m, "cpu MHz\t\t: %u.%03u\n",
			     cpu_khz / 1000, (cpu_khz % 1000));
	}

	/* Cache size */
	if (c->x86_cache_size >= 0) 
		seq_printf(m, "cache size\t: %d KB\n", c->x86_cache_size);
	
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
		
	seq_printf(m, "\nbogomips\t: %lu.%02lu\n\n",
		   c->loops_per_jiffy/(500000/HZ),
		   (c->loops_per_jiffy/(5000/HZ)) % 100);

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
	start:	c_start,
	next:	c_next,
	stop:	c_stop,
	show:	show_cpuinfo,
};
