/*
 * Handle the memory map.
 * The functions here do the job until bootmem takes over.
 *
 *  Getting sanitize_e820_map() in sync with i386 version by applying change:
 *  -  Provisions for empty E820 memory regions (reported by certain BIOSes).
 *     Alex Achenbach <xela@slit.de>, December 2002.
 *  Venkatesh Pallipadi <venkatesh.pallipadi@intel.com>
 *
 */
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/init.h>
#include <linux/crash_dump.h>
#include <linux/export.h>
#include <linux/bootmem.h>
#include <linux/pfn.h>
#include <linux/suspend.h>
#include <linux/acpi.h>
#include <linux/firmware-map.h>
#include <linux/memblock.h>
#include <linux/sort.h>

#include <asm/e820.h>
#include <asm/proto.h>
#include <asm/setup.h>
#include <xen/interface/memory.h>

/*
 * The e820 map is the map that gets modified e.g. with command line parameters
 * and that is also registered with modifications in the kernel resource tree
 * with the iomem_resource as parent.
 *
 * The e820_saved is directly saved after the BIOS-provided memory map is
 * copied. It doesn't get modified afterwards. It's registered for the
 * /sys/firmware/memmap interface.
 *
 * That memory map is not modified and is used as base for kexec. The kexec'd
 * kernel should get the same memory map as the firmware provides. Then the
 * user can e.g. boot the original kernel with mem=1G while still booting the
 * next kernel with full memory.
 */
struct e820map e820;
#if !defined(CONFIG_XEN)
struct e820map e820_saved;
#elif defined(CONFIG_XEN_PRIVILEGED_GUEST)
struct e820map machine_e820;
# define e820_saved machine_e820
#else
# define machine_e820 e820
# define e820_saved e820
#endif

/* For PCI or other memory-mapped resources */
unsigned long pci_mem_start = 0xaeedbabe;
#ifdef CONFIG_PCI
EXPORT_SYMBOL(pci_mem_start);
#endif

/*
 * This function checks if any part of the range <start,end> is mapped
 * with type.
 */
int
e820_any_mapped(u64 start, u64 end, unsigned type)
{
	int i;

#ifndef CONFIG_XEN
	for (i = 0; i < e820.nr_map; i++) {
		struct e820entry *ei = &e820.map[i];
#else
	if (!is_initial_xendomain())
		return 0;
	for (i = 0; i < machine_e820.nr_map; ++i) {
		const struct e820entry *ei = &machine_e820.map[i];
#endif

		if (type && ei->type != type)
			continue;
		if (ei->addr >= end || ei->addr + ei->size <= start)
			continue;
		return 1;
	}
	return 0;
}
EXPORT_SYMBOL_GPL(e820_any_mapped);

/*
 * This function checks if the entire range <start,end> is mapped with type.
 *
 * Note: this function only works correct if the e820 table is sorted and
 * not-overlapping, which is the case
 */
int __init e820_all_mapped(u64 start, u64 end, unsigned type)
{
	int i;

#ifndef CONFIG_XEN
	for (i = 0; i < e820.nr_map; i++) {
		struct e820entry *ei = &e820.map[i];
#else
	if (!is_initial_xendomain())
		return 0;
	for (i = 0; i < machine_e820.nr_map; ++i) {
		const struct e820entry *ei = &machine_e820.map[i];
#endif

		if (type && ei->type != type)
			continue;
		/* is the region (part) in overlap with the current region ?*/
		if (ei->addr >= end || ei->addr + ei->size <= start)
			continue;

		/* if the region is at the beginning of <start,end> we move
		 * start to the end of the region since it's ok until there
		 */
		if (ei->addr <= start)
			start = ei->addr + ei->size;
		/*
		 * if start is now at or beyond end, we're done, full
		 * coverage
		 */
		if (start >= end)
			return 1;
	}
	return 0;
}

/*
 * Add a memory region to the kernel e820 map.
 */
static void __init __e820_add_region(struct e820map *e820x, u64 start, u64 size,
					 int type)
{
	int x = e820x->nr_map;

	if (x >= ARRAY_SIZE(e820x->map)) {
		printk(KERN_ERR "e820: too many entries; ignoring [mem %#010llx-%#010llx]\n",
		       (unsigned long long) start,
		       (unsigned long long) (start + size - 1));
		return;
	}

	e820x->map[x].addr = start;
	e820x->map[x].size = size;
	e820x->map[x].type = type;
	e820x->nr_map++;
}

void __init e820_add_region(u64 start, u64 size, int type)
{
	__e820_add_region(&e820, start, size, type);
}

static void __init e820_print_type(u32 type)
{
	switch (type) {
	case E820_RAM:
	case E820_RESERVED_KERN:
		printk(KERN_CONT "usable");
		break;
	case E820_RESERVED:
		printk(KERN_CONT "reserved");
		break;
	case E820_ACPI:
		printk(KERN_CONT "ACPI data");
		break;
	case E820_NVS:
		printk(KERN_CONT "ACPI NVS");
		break;
	case E820_UNUSABLE:
		printk(KERN_CONT "unusable");
		break;
	default:
		printk(KERN_CONT "type %u", type);
		break;
	}
}

static void __init _e820_print_map(const struct e820map *e820, const char *who)
{
	int i;

	for (i = 0; i < e820->nr_map; i++) {
		printk(KERN_INFO "%s: [mem %#018Lx-%#018Lx] ", who,
		       (unsigned long long) e820->map[i].addr,
		       (unsigned long long)
		       (e820->map[i].addr + e820->map[i].size - 1));
		e820_print_type(e820->map[i].type);
		printk(KERN_CONT "\n");
	}
}

/*
 * Sanitize the BIOS e820 map.
 *
 * Some e820 responses include overlapping entries. The following
 * replaces the original e820 map with a new one, removing overlaps,
 * and resolving conflicting memory types in favor of highest
 * numbered type.
 *
 * The input parameter biosmap points to an array of 'struct
 * e820entry' which on entry has elements in the range [0, *pnr_map)
 * valid, and which has space for up to max_nr_map entries.
 * On return, the resulting sanitized e820 map entries will be in
 * overwritten in the same location, starting at biosmap.
 *
 * The integer pointed to by pnr_map must be valid on entry (the
 * current number of valid entries located at biosmap) and will
 * be updated on return, with the new number of valid entries
 * (something no more than max_nr_map.)
 *
 * The return value from sanitize_e820_map() is zero if it
 * successfully 'sanitized' the map entries passed in, and is -1
 * if it did nothing, which can happen if either of (1) it was
 * only passed one map entry, or (2) any of the input map entries
 * were invalid (start + size < start, meaning that the size was
 * so big the described memory range wrapped around through zero.)
 *
 *	Visually we're performing the following
 *	(1,2,3,4 = memory types)...
 *
 *	Sample memory map (w/overlaps):
 *	   ____22__________________
 *	   ______________________4_
 *	   ____1111________________
 *	   _44_____________________
 *	   11111111________________
 *	   ____________________33__
 *	   ___________44___________
 *	   __________33333_________
 *	   ______________22________
 *	   ___________________2222_
 *	   _________111111111______
 *	   _____________________11_
 *	   _________________4______
 *
 *	Sanitized equivalent (no overlap):
 *	   1_______________________
 *	   _44_____________________
 *	   ___1____________________
 *	   ____22__________________
 *	   ______11________________
 *	   _________1______________
 *	   __________3_____________
 *	   ___________44___________
 *	   _____________33_________
 *	   _______________2________
 *	   ________________1_______
 *	   _________________4______
 *	   ___________________2____
 *	   ____________________33__
 *	   ______________________4_
 */
struct change_member {
	struct e820entry *pbios; /* pointer to original bios entry */
	unsigned long long addr; /* address for this change point */
};

static int __init cpcompare(const void *a, const void *b)
{
	struct change_member * const *app = a, * const *bpp = b;
	const struct change_member *ap = *app, *bp = *bpp;

	/*
	 * Inputs are pointers to two elements of change_point[].  If their
	 * addresses are unequal, their difference dominates.  If the addresses
	 * are equal, then consider one that represents the end of its region
	 * to be greater than one that does not.
	 */
	if (ap->addr != bp->addr)
		return ap->addr > bp->addr ? 1 : -1;

	return (ap->addr != ap->pbios->addr) - (bp->addr != bp->pbios->addr);
}

int __init sanitize_e820_map(struct e820entry *biosmap, int max_nr_map,
			     u32 *pnr_map)
{
	static struct change_member change_point_list[2*E820_X_MAX] __initdata;
	static struct change_member *change_point[2*E820_X_MAX] __initdata;
	static struct e820entry *overlap_list[E820_X_MAX] __initdata;
	static struct e820entry new_bios[E820_X_MAX] __initdata;
	unsigned long current_type, last_type;
	unsigned long long last_addr;
	int chgidx;
	int overlap_entries;
	int new_bios_entry;
	int old_nr, new_nr, chg_nr;
	int i;

	/* if there's only one memory region, don't bother */
#ifdef CONFIG_XEN
	if (*pnr_map == 1)
		return 0;
#endif
	if (*pnr_map < 2)
		return -1;

	old_nr = *pnr_map;
	BUG_ON(old_nr > max_nr_map);

	/* bail out if we find any unreasonable addresses in bios map */
	for (i = 0; i < old_nr; i++)
		if (biosmap[i].addr + biosmap[i].size < biosmap[i].addr)
			return -1;

	/* create pointers for initial change-point information (for sorting) */
	for (i = 0; i < 2 * old_nr; i++)
		change_point[i] = &change_point_list[i];

	/* record all known change-points (starting and ending addresses),
	   omitting those that are for empty memory regions */
	chgidx = 0;
	for (i = 0; i < old_nr; i++)	{
		if (biosmap[i].size != 0) {
			change_point[chgidx]->addr = biosmap[i].addr;
			change_point[chgidx++]->pbios = &biosmap[i];
			change_point[chgidx]->addr = biosmap[i].addr +
				biosmap[i].size;
			change_point[chgidx++]->pbios = &biosmap[i];
		}
	}
	chg_nr = chgidx;

	/* sort change-point list by memory addresses (low -> high) */
	sort(change_point, chg_nr, sizeof *change_point, cpcompare, NULL);

	/* create a new bios memory map, removing overlaps */
	overlap_entries = 0;	 /* number of entries in the overlap table */
	new_bios_entry = 0;	 /* index for creating new bios map entries */
	last_type = 0;		 /* start with undefined memory type */
	last_addr = 0;		 /* start with 0 as last starting address */

	/* loop through change-points, determining affect on the new bios map */
	for (chgidx = 0; chgidx < chg_nr; chgidx++) {
		/* keep track of all overlapping bios entries */
		if (change_point[chgidx]->addr ==
		    change_point[chgidx]->pbios->addr) {
			/*
			 * add map entry to overlap list (> 1 entry
			 * implies an overlap)
			 */
			overlap_list[overlap_entries++] =
				change_point[chgidx]->pbios;
		} else {
			/*
			 * remove entry from list (order independent,
			 * so swap with last)
			 */
			for (i = 0; i < overlap_entries; i++) {
				if (overlap_list[i] ==
				    change_point[chgidx]->pbios)
					overlap_list[i] =
						overlap_list[overlap_entries-1];
			}
			overlap_entries--;
		}
		/*
		 * if there are overlapping entries, decide which
		 * "type" to use (larger value takes precedence --
		 * 1=usable, 2,3,4,4+=unusable)
		 */
		current_type = 0;
		for (i = 0; i < overlap_entries; i++)
			if (overlap_list[i]->type > current_type)
				current_type = overlap_list[i]->type;
		/*
		 * continue building up new bios map based on this
		 * information
		 */
		if (current_type != last_type)	{
			if (last_type != 0)	 {
				new_bios[new_bios_entry].size =
					change_point[chgidx]->addr - last_addr;
				/*
				 * move forward only if the new size
				 * was non-zero
				 */
				if (new_bios[new_bios_entry].size != 0)
					/*
					 * no more space left for new
					 * bios entries ?
					 */
					if (++new_bios_entry >= max_nr_map)
						break;
			}
			if (current_type != 0)	{
				new_bios[new_bios_entry].addr =
					change_point[chgidx]->addr;
				new_bios[new_bios_entry].type = current_type;
				last_addr = change_point[chgidx]->addr;
			}
			last_type = current_type;
		}
	}
	/* retain count for new bios entries */
	new_nr = new_bios_entry;

	/* copy new bios mapping into original location */
	memcpy(biosmap, new_bios, new_nr * sizeof(struct e820entry));
	*pnr_map = new_nr;

	return 0;
}

static int __init __append_e820_map(struct e820entry *biosmap, int nr_map)
{
	while (nr_map) {
		u64 start = biosmap->addr;
		u64 size = biosmap->size;
		u64 end = start + size;
		u32 type = biosmap->type;

		/* Overflow in 64 bits? Ignore the memory map. */
		if (start > end)
			return -1;

		e820_add_region(start, size, type);

		biosmap++;
		nr_map--;
	}
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
 */
static int __init append_e820_map(struct e820entry *biosmap, int nr_map)
{
#ifndef CONFIG_XEN
	/* Only one memory region (or negative)? Ignore it */
	if (nr_map < 2)
		return -1;
#else
	BUG_ON(nr_map < 1);
#endif

	return __append_e820_map(biosmap, nr_map);
}

static u64 __init __e820_update_range(struct e820map *e820x, u64 start,
					u64 size, unsigned old_type,
					unsigned new_type)
{
	u64 end;
	unsigned int i;
	u64 real_updated_size = 0;

	BUG_ON(old_type == new_type);

	if (size > (ULLONG_MAX - start))
		size = ULLONG_MAX - start;

	end = start + size;
	printk(KERN_DEBUG "e820: update [mem %#010Lx-%#010Lx] ",
	       (unsigned long long) start, (unsigned long long) (end - 1));
	e820_print_type(old_type);
	printk(KERN_CONT " ==> ");
	e820_print_type(new_type);
	printk(KERN_CONT "\n");

	for (i = 0; i < e820x->nr_map; i++) {
		struct e820entry *ei = &e820x->map[i];
		u64 final_start, final_end;
		u64 ei_end;

		if (ei->type != old_type)
			continue;

		ei_end = ei->addr + ei->size;
		/* totally covered by new range? */
		if (ei->addr >= start && ei_end <= end) {
			ei->type = new_type;
			real_updated_size += ei->size;
			continue;
		}

		/* new range is totally covered? */
		if (ei->addr < start && ei_end > end) {
			__e820_add_region(e820x, start, size, new_type);
			__e820_add_region(e820x, end, ei_end - end, ei->type);
			ei->size = start - ei->addr;
			real_updated_size += size;
			continue;
		}

		/* partially covered */
		final_start = max(start, ei->addr);
		final_end = min(end, ei_end);
		if (final_start >= final_end)
			continue;

		__e820_add_region(e820x, final_start, final_end - final_start,
				  new_type);

		real_updated_size += final_end - final_start;

		/*
		 * left range could be head or tail, so need to update
		 * size at first.
		 */
		ei->size -= final_end - final_start;
		if (ei->addr < final_start)
			continue;
		ei->addr = final_end;
	}
	return real_updated_size;
}

u64 __init e820_update_range(u64 start, u64 size, unsigned old_type,
			     unsigned new_type)
{
	return __e820_update_range(&e820, start, size, old_type, new_type);
}

#ifndef CONFIG_XEN_UNPRIVILEGED_GUEST
static u64 __init e820_update_range_saved(u64 start, u64 size,
					  unsigned old_type, unsigned new_type)
{
#ifdef CONFIG_XEN
	if (!is_initial_xendomain())
		return 0;
	return __e820_update_range(&machine_e820, phys_to_machine(start),
				   size, old_type, new_type);
#else
	return __e820_update_range(&e820_saved, start, size, old_type,
				     new_type);
#endif
}
#endif

/* make e820 not cover the range */
u64 __init e820_remove_range(u64 start, u64 size, unsigned old_type,
			     int checktype)
{
	int i;
	u64 end;
	u64 real_removed_size = 0;

	if (size > (ULLONG_MAX - start))
		size = ULLONG_MAX - start;

	end = start + size;
	printk(KERN_DEBUG "e820: remove [mem %#010Lx-%#010Lx] ",
	       (unsigned long long) start, (unsigned long long) (end - 1));
	if (checktype)
		e820_print_type(old_type);
	printk(KERN_CONT "\n");

	for (i = 0; i < e820.nr_map; i++) {
		struct e820entry *ei = &e820.map[i];
		u64 final_start, final_end;
		u64 ei_end;

		if (checktype && ei->type != old_type)
			continue;

		ei_end = ei->addr + ei->size;
		/* totally covered? */
		if (ei->addr >= start && ei_end <= end) {
			real_removed_size += ei->size;
			memset(ei, 0, sizeof(struct e820entry));
			continue;
		}

		/* new range is totally covered? */
		if (ei->addr < start && ei_end > end) {
			e820_add_region(end, ei_end - end, ei->type);
			ei->size = start - ei->addr;
			real_removed_size += size;
			continue;
		}

		/* partially covered */
		final_start = max(start, ei->addr);
		final_end = min(end, ei_end);
		if (final_start >= final_end)
			continue;
		real_removed_size += final_end - final_start;

		/*
		 * left range could be head or tail, so need to update
		 * size at first.
		 */
		ei->size -= final_end - final_start;
		if (ei->addr < final_start)
			continue;
		ei->addr = final_end;
	}
	return real_removed_size;
}

void __init update_e820(void)
{
	u32 nr_map;

	nr_map = e820.nr_map;
	if (sanitize_e820_map(e820.map, ARRAY_SIZE(e820.map), &nr_map))
		return;
	e820.nr_map = nr_map;
	printk(KERN_INFO "e820: modified physical RAM map:\n");
	_e820_print_map(&e820, "modified");
}
#ifndef CONFIG_XEN_UNPRIVILEGED_GUEST
static void __init update_e820_saved(void)
{
	u32 nr_map;

	nr_map = e820_saved.nr_map;
	if (sanitize_e820_map(e820_saved.map, ARRAY_SIZE(e820_saved.map), &nr_map))
		return;
	e820_saved.nr_map = nr_map;
}
#endif

#ifdef CONFIG_XEN
#define e820 machine_e820
#endif

#define MAX_GAP_END 0x100000000ull
/*
 * Search for a gap in the e820 memory space from start_addr to end_addr.
 */
__init int e820_search_gap(unsigned long *gapstart, unsigned long *gapsize,
		unsigned long start_addr, unsigned long long end_addr)
{
	unsigned long long last;
	int i = e820.nr_map;
	int found = 0;

	last = (end_addr && end_addr < MAX_GAP_END) ? end_addr : MAX_GAP_END;
#ifdef CONFIG_X86_64
	if (start_addr >= MAX_GAP_END)
		last = end_addr ?: (1UL << boot_cpu_data.x86_phys_bits);
#endif

	while (--i >= 0) {
		unsigned long long start = e820.map[i].addr;
		unsigned long long end = start + e820.map[i].size;

		if (end < start_addr)
			continue;

		/*
		 * Since "last" is at most 4GB, we know we'll
		 * fit in 32 bits if this condition is true
		 */
		if (last > end) {
			unsigned long gap = last - end;

			if (gap >= *gapsize) {
				*gapsize = gap;
				*gapstart = end;
				found = 1;
			}
		}
		if (start < last)
			last = start;
	}
	return found;
}

/*
 * Search for the biggest gap in the low 32 bits of the e820
 * memory space.  We pass this space to PCI to assign MMIO resources
 * for hotplug or unconfigured devices in.
 * Hopefully the BIOS let enough space left.
 */
__init void e820_setup_gap(void)
{
	unsigned long gapstart, gapsize;
	int found;

	gapstart = 0x10000000;
	gapsize = 0x400000;
	found  = e820_search_gap(&gapstart, &gapsize, 0, MAX_GAP_END);

#ifdef CONFIG_X86_64
	if (!found) {
		printk(KERN_ERR
	"e820: cannot find a gap in the 32bit address range\n"
	"e820: PCI devices with unassigned 32bit BARs may break!\n");
		found = e820_search_gap(&gapstart, &gapsize, MAX_GAP_END, 0);
		WARN_ON(!found);
	}
#endif

	/*
	 * e820_reserve_resources_late protect stolen RAM already
	 */
	pci_mem_start = gapstart;

	printk(KERN_INFO
	       "e820: [mem %#010lx-%#010lx] available for PCI devices\n",
	       gapstart, gapstart + gapsize - 1);
}

#undef e820

#ifndef CONFIG_XEN
/**
 * Because of the size limitation of struct boot_params, only first
 * 128 E820 memory entries are passed to kernel via
 * boot_params.e820_map, others are passed via SETUP_E820_EXT node of
 * linked list of struct setup_data, which is parsed here.
 */
void __init parse_e820_ext(u64 phys_addr, u32 data_len)
{
	int entries;
	struct e820entry *extmap;
	struct setup_data *sdata;

	sdata = early_memremap(phys_addr, data_len);
	entries = sdata->len / sizeof(struct e820entry);
	extmap = (struct e820entry *)(sdata->data);
	__append_e820_map(extmap, entries);
	sanitize_e820_map(e820.map, ARRAY_SIZE(e820.map), &e820.nr_map);
	early_iounmap(sdata, data_len);
	printk(KERN_INFO "e820: extended physical RAM map:\n");
	_e820_print_map(&e820, "extended");
}

#if defined(CONFIG_X86_64) || \
	(defined(CONFIG_X86_32) && defined(CONFIG_HIBERNATION))
/**
 * Find the ranges of physical addresses that do not correspond to
 * e820 RAM areas and mark the corresponding pages as nosave for
 * hibernation (32 bit) or software suspend and suspend to RAM (64 bit).
 *
 * This function requires the e820 map to be sorted and without any
 * overlapping entries and assumes the first e820 area to be RAM.
 */
void __init e820_mark_nosave_regions(unsigned long limit_pfn)
{
	int i;
	unsigned long pfn;

	pfn = PFN_DOWN(e820.map[0].addr + e820.map[0].size);
	for (i = 1; i < e820.nr_map; i++) {
		struct e820entry *ei = &e820.map[i];

		if (pfn < PFN_UP(ei->addr))
			register_nosave_region(pfn, PFN_UP(ei->addr));

		pfn = PFN_DOWN(ei->addr + ei->size);
		if (ei->type != E820_RAM && ei->type != E820_RESERVED_KERN)
			register_nosave_region(PFN_UP(ei->addr), pfn);

		if (pfn >= limit_pfn)
			break;
	}
}
#endif

#ifdef CONFIG_ACPI
/**
 * Mark ACPI NVS memory region, so that we can save/restore it during
 * hibernation and the subsequent resume.
 */
static int __init e820_mark_nvs_memory(void)
{
	int i;

	for (i = 0; i < e820.nr_map; i++) {
		struct e820entry *ei = &e820.map[i];

		if (ei->type == E820_NVS)
			acpi_nvs_register(ei->addr, ei->size);
	}

	return 0;
}
core_initcall(e820_mark_nvs_memory);
#endif
#endif

#ifndef CONFIG_XEN_UNPRIVILEGED_GUEST
/*
 * pre allocated 4k and reserved it in memblock and e820_saved
 */
u64 __init early_reserve_e820(u64 size, u64 align)
{
	u64 addr;
#ifdef CONFIG_XEN
	unsigned int order = get_order(size);
	int rc;
	unsigned long max_initmap_pfn;

	if (!is_initial_xendomain())
		return 0;
	size = PAGE_SIZE << order;
	if (align < PAGE_SIZE)
		align = PAGE_SIZE;
#endif
	addr = __memblock_alloc_base(size, align, MEMBLOCK_ALLOC_ACCESSIBLE);
	if (addr) {
		e820_update_range_saved(addr, size, E820_RAM, E820_RESERVED);
		printk(KERN_INFO "e820: update e820_saved for early_reserve_e820\n");
		update_e820_saved();
	}
#ifdef CONFIG_XEN
	else
		return 0;
	max_initmap_pfn = ALIGN(PFN_UP(__pa(xen_start_info->pt_base))
				       + xen_start_info->nr_pt_frames
				       + 1 + (1 << (19 - PAGE_SHIFT)),
				1UL << (22 - PAGE_SHIFT));
#ifdef CONFIG_X86_32
	if ((addr >> PAGE_SHIFT)
	    < max(max_initmap_pfn, max_pfn_mapped))
		rc = xen_create_contiguous_region((unsigned long)__va(addr),
						  order, 32);
#else
	if ((addr >> PAGE_SHIFT) < max_pfn_mapped)
		rc = xen_create_contiguous_region((unsigned long)__va(addr),
						  order, 32);
	else if ((addr >> PAGE_SHIFT) < max_initmap_pfn)
		rc = xen_create_contiguous_region(__START_KERNEL_map + addr,
						  order, 32);
#endif
	else
		rc = early_create_contiguous_region(addr >> PAGE_SHIFT,
						    order, 32);
	if (rc)
		return 0;
#endif

	return addr;
}
#endif

#ifdef CONFIG_X86_32
# ifdef CONFIG_X86_PAE
#  define MAX_ARCH_PFN		(1ULL<<(40-PAGE_SHIFT))
# else
#  define MAX_ARCH_PFN		(1ULL<<(32-PAGE_SHIFT))
# endif
#else /* CONFIG_X86_32 */
# define MAX_ARCH_PFN MAXMEM>>PAGE_SHIFT
#endif

/*
 * Find the highest page frame number we have available
 */
static unsigned long __init e820_end_pfn(unsigned long limit_pfn, unsigned type)
{
	int i;
	unsigned long last_pfn = 0;
	unsigned long max_arch_pfn = MAX_ARCH_PFN;

	for (i = 0; i < e820.nr_map; i++) {
		struct e820entry *ei = &e820.map[i];
		unsigned long start_pfn;
		unsigned long end_pfn;

		if (ei->type != type)
			continue;

		start_pfn = ei->addr >> PAGE_SHIFT;
		end_pfn = (ei->addr + ei->size) >> PAGE_SHIFT;

		if (start_pfn >= limit_pfn)
			continue;
		if (end_pfn > limit_pfn) {
			last_pfn = limit_pfn;
			break;
		}
		if (end_pfn > last_pfn)
			last_pfn = end_pfn;
	}

	if (last_pfn > max_arch_pfn)
		last_pfn = max_arch_pfn;

	printk(KERN_INFO "e820: last_pfn = %#lx max_arch_pfn = %#lx\n",
			 last_pfn, max_arch_pfn);
	return last_pfn;
}
unsigned long __init e820_end_of_ram_pfn(void)
{
	return e820_end_pfn(MAX_ARCH_PFN, E820_RAM);
}

unsigned long __init e820_end_of_low_ram_pfn(void)
{
	return e820_end_pfn(1UL<<(32 - PAGE_SHIFT), E820_RAM);
}

static void early_panic(char *msg)
{
	early_printk(msg);
	panic(msg);
}

static int userdef __initdata;

/* "mem=nopentium" disables the 4MB page tables. */
static int __init parse_memopt(char *p)
{
	u64 mem_size, current_end;
	unsigned int i;

	if (!p)
		return -EINVAL;

#ifndef CONFIG_XEN
	if (!strcmp(p, "nopentium")) {
#ifdef CONFIG_X86_32
		setup_clear_cpu_cap(X86_FEATURE_PSE);
		return 0;
#else
		printk(KERN_WARNING "mem=nopentium ignored! (only supported on x86_32)\n");
		return -EINVAL;
#endif
	}
#endif

	userdef = 1;
	mem_size = memparse(p, &p);
	/* don't remove all of memory when handling "mem={invalid}" param */
	if (mem_size == 0)
		return -EINVAL;
#ifdef CONFIG_XEN
	/*
	 * A little less than 2% of available memory are needed for page
	 * tables, p2m map, and mem_map. Hence the maximum amount of memory
	 * we can potentially balloon up to can in no case exceed about 50
	 * times of what we've been given initially. Since even with that we
	 * won't be able to boot (due to various calculations done based on
	 * the total number of pages) we further restrict this to factor 32.
	 */
	if ((mem_size >> (PAGE_SHIFT + 5)) > xen_start_info->nr_pages) {
		u64 size = (u64)xen_start_info->nr_pages << 5;

		pr_warn("mem=%Luk is invalid for an initial"
			" allocation of %luk, using %Luk\n",
			(unsigned long long)mem_size >> 10,
			xen_start_info->nr_pages << (PAGE_SHIFT - 10),
			(unsigned long long)size << (PAGE_SHIFT - 10));
		mem_size = size << PAGE_SHIFT;
	}
#endif
	e820_remove_range(mem_size, ULLONG_MAX - mem_size, E820_RAM, 1);

	i = e820.nr_map - 1;
	current_end = e820.map[i].addr + e820.map[i].size;
	if (current_end < mem_size) {
		/*
		 * The e820 map ends before our requested size so
		 * extend the final entry to the requested address.
		 */
		if (e820.map[i].type == E820_RAM)
			e820.map[i].size = mem_size - e820.map[i].addr;
		else
			e820_add_region(current_end, mem_size - current_end, E820_RAM);
	}

	return 0;
}
early_param("mem", parse_memopt);

#ifndef CONFIG_XEN
static int __init parse_memmap_one(char *p)
{
	char *oldp;
	u64 start_at, mem_size;

	if (!p)
		return -EINVAL;

	if (!strncmp(p, "exactmap", 8)) {
#ifdef CONFIG_CRASH_DUMP
		/*
		 * If we are doing a crash dump, we still need to know
		 * the real mem size before original memory map is
		 * reset.
		 */
		saved_max_pfn = e820_end_of_ram_pfn();
#endif
		e820.nr_map = 0;
		userdef = 1;
		return 0;
	}

	oldp = p;
	mem_size = memparse(p, &p);
	if (p == oldp)
		return -EINVAL;

	userdef = 1;
	if (*p == '@') {
		start_at = memparse(p+1, &p);
		e820_add_region(start_at, mem_size, E820_RAM);
	} else if (*p == '#') {
		start_at = memparse(p+1, &p);
		e820_add_region(start_at, mem_size, E820_ACPI);
	} else if (*p == '$') {
		start_at = memparse(p+1, &p);
		e820_add_region(start_at, mem_size, E820_RESERVED);
	} else
		e820_remove_range(mem_size, ULLONG_MAX - mem_size, E820_RAM, 1);

	return *p == '\0' ? 0 : -EINVAL;
}
static int __init parse_memmap_opt(char *str)
{
	while (str) {
		char *k = strchr(str, ',');

		if (k)
			*k++ = 0;

		parse_memmap_one(str);
		str = k;
	}

	return 0;
}
early_param("memmap", parse_memmap_opt);
#endif

void __init finish_e820_parsing(void)
{
	if (userdef) {
		u32 nr = e820.nr_map;

		if (sanitize_e820_map(e820.map, ARRAY_SIZE(e820.map), &nr) < 0)
			early_panic("Invalid user supplied memory map");
		e820.nr_map = nr;

		printk(KERN_INFO "e820: user-defined physical RAM map:\n");
		_e820_print_map(&e820, "user");
	}
}

static inline const char *e820_type_to_string(int e820_type)
{
	switch (e820_type) {
	case E820_RESERVED_KERN:
	case E820_RAM:	return "System RAM";
	case E820_ACPI:	return "ACPI Tables";
	case E820_NVS:	return "ACPI Non-volatile Storage";
	case E820_UNUSABLE:	return "Unusable memory";
	default:	return "reserved";
	}
}

#ifdef CONFIG_XEN
#define e820 machine_e820
#endif

/*
 * Mark e820 reserved areas as busy for the resource manager.
 */
static struct resource __initdata *e820_res;
void __init e820_reserve_resources(void)
{
	int i;
	struct resource *res;
	u64 end;

	res = alloc_bootmem(sizeof(struct resource) * e820.nr_map);
	e820_res = res;
	for (i = 0; i < e820.nr_map; i++) {
		end = e820.map[i].addr + e820.map[i].size - 1;
		if (end != (resource_size_t)end) {
			res++;
			continue;
		}
		res->name = e820_type_to_string(e820.map[i].type);
		res->start = e820.map[i].addr;
		res->end = end;

		res->flags = IORESOURCE_MEM;

		/*
		 * don't register the region that could be conflicted with
		 * pci device BAR resource and insert them later in
		 * pcibios_resource_survey()
		 */
		if (e820.map[i].type != E820_RESERVED || res->start < (1ULL<<20)) {
			res->flags |= IORESOURCE_BUSY;
			insert_resource(&iomem_resource, res);
		}
		res++;
	}

	for (i = 0; i < e820_saved.nr_map; i++) {
		struct e820entry *entry = &e820_saved.map[i];
		firmware_map_add_early(entry->addr,
			entry->addr + entry->size,
			e820_type_to_string(entry->type));
	}
}

/* How much should we pad RAM ending depending on where it is? */
static unsigned long ram_alignment(resource_size_t pos)
{
	unsigned long mb = pos >> 20;

	/* To 64kB in the first megabyte */
	if (!mb)
		return 64*1024;

	/* To 1MB in the first 16MB */
	if (mb < 16)
		return 1024*1024;

	/* To 64MB for anything above that */
	return 64*1024*1024;
}

#define MAX_RESOURCE_SIZE ((resource_size_t)-1)

void __init e820_reserve_resources_late(void)
{
	int i;
	struct resource *res;

	res = e820_res;
	for (i = 0; i < e820.nr_map; i++) {
		if (!res->parent && res->end)
			insert_resource_expand_to_fit(&iomem_resource, res);
		res++;
	}

	/*
	 * Try to bump up RAM regions to reasonable boundaries to
	 * avoid stolen RAM:
	 */
	for (i = 0; i < e820.nr_map; i++) {
		struct e820entry *entry = &e820.map[i];
		u64 start, end;

		if (entry->type != E820_RAM)
			continue;
		start = entry->addr + entry->size;
		end = round_up(start, ram_alignment(start)) - 1;
		if (end > MAX_RESOURCE_SIZE)
			end = MAX_RESOURCE_SIZE;
		if (start >= end)
			continue;
		printk(KERN_DEBUG
		       "e820: reserve RAM buffer [mem %#010llx-%#010llx]\n",
		       start, end);
		reserve_region_with_split(&iomem_resource, start, end,
					  "RAM buffer");
	}
}

#undef e820

char *__init default_machine_specific_memory_setup(void)
{
	int rc, nr_map;
	unsigned long maxmem;
	struct xen_memory_map memmap;
	static struct e820entry __initdata map[E820MAX];

	memmap.nr_entries = E820MAX;
	set_xen_guest_handle(memmap.buffer, map);

	rc = HYPERVISOR_memory_op(XENMEM_memory_map, &memmap);
	if (rc == -ENOSYS) {
		memmap.nr_entries = 1;
		map[0].addr = 0ULL;
		map[0].size = PFN_PHYS((unsigned long long)xen_start_info->nr_pages);
		/* 8MB slack (to balance backend allocations). */
		map[0].size += 8ULL << 20;
		map[0].type = E820_RAM;
		rc = 0;
	}
	BUG_ON(rc);

	nr_map = memmap.nr_entries;
	sanitize_e820_map(map, ARRAY_SIZE(map), &nr_map);

	if (append_e820_map(map, nr_map) < 0)
		BUG();

#ifdef CONFIG_XEN
	/* See the comment in parse_memopt(). */
	for (maxmem = rc = 0; rc < e820.nr_map; ++rc)
		if (e820.map[rc].type == E820_RAM)
			maxmem += e820.map[rc].size >> PAGE_SHIFT;
	if (is_initial_xendomain()) {
		domid_t domid = DOMID_SELF;

		rc = HYPERVISOR_memory_op(XENMEM_maximum_reservation, &domid);
		if (rc > 0 && maxmem > rc)
			maxmem = rc;
	}
	if ((maxmem >> 5) > xen_start_info->nr_pages) {
		unsigned long long size = (u64)xen_start_info->nr_pages << 5;

		pr_warn("maxmem of %luM is invalid for an initial"
			" allocation of %luM, using %LuM\n",
			maxmem >> (20 - PAGE_SHIFT),
			xen_start_info->nr_pages >> (20 - PAGE_SHIFT),
			size >> (20 - PAGE_SHIFT));
		size <<= PAGE_SHIFT;
		e820_remove_range(size, ULLONG_MAX - size, E820_RAM, 1);
	}

	if (is_initial_xendomain()) {
		memmap.nr_entries = E820MAX;
		set_xen_guest_handle(memmap.buffer, machine_e820.map);

		if (HYPERVISOR_memory_op(XENMEM_machine_memory_map, &memmap))
			BUG();
		machine_e820.nr_map = memmap.nr_entries;
	}
#endif

	return "Xen";
}

void __init setup_memory_map(void)
{
	char *who;

	who = x86_init.resources.memory_setup();
#ifndef CONFIG_XEN_UNPRIVILEGED_GUEST
#ifdef CONFIG_XEN
	if (is_initial_xendomain()) {
		printk(KERN_INFO "Xen-provided machine memory map:\n");
		_e820_print_map(&machine_e820, "BIOS");
	} else
#endif
		memcpy(&e820_saved, &e820, sizeof(struct e820map));
#endif
	printk(KERN_INFO "e820: Xen-provided physical RAM map:\n");
	_e820_print_map(&e820, who);
}

void __init memblock_x86_fill(void)
{
	int i;
	u64 end;

	/*
	 * EFI may have more than 128 entries
	 * We are safe to enable resizing, beause memblock_x86_fill()
	 * is rather later for x86
	 */
	memblock_allow_resize();

	for (i = 0; i < e820.nr_map; i++) {
		struct e820entry *ei = &e820.map[i];

		end = ei->addr + ei->size;
		if (end != (resource_size_t)end)
			continue;

		if (ei->type != E820_RAM && ei->type != E820_RESERVED_KERN)
			continue;

		memblock_add(ei->addr, ei->size);
	}

#ifdef CONFIG_XEN
	if (max_pfn > xen_start_info->nr_pages)
		memblock_reserve(PFN_PHYS(xen_start_info->nr_pages),
				 PFN_PHYS(max_pfn - xen_start_info->nr_pages));
#endif

	/* throw away partial pages */
	memblock_trim_memory(PAGE_SIZE);

	memblock_dump_all();
}

void __init memblock_find_dma_reserve(void)
{
#if defined(CONFIG_X86_64) && !defined(CONFIG_XEN)
	u64 nr_pages = 0, nr_free_pages = 0;
	unsigned long start_pfn, end_pfn;
	phys_addr_t start, end;
	int i;
	u64 u;

	/*
	 * need to find out used area below MAX_DMA_PFN
	 * need to use memblock to get free size in [0, MAX_DMA_PFN]
	 * at first, and assume boot_mem will not take below MAX_DMA_PFN
	 */
	for_each_mem_pfn_range(i, MAX_NUMNODES, &start_pfn, &end_pfn, NULL) {
		start_pfn = min_t(unsigned long, start_pfn, MAX_DMA_PFN);
		end_pfn = min_t(unsigned long, end_pfn, MAX_DMA_PFN);
		nr_pages += end_pfn - start_pfn;
	}

	for_each_free_mem_range(u, MAX_NUMNODES, &start, &end, NULL) {
		start_pfn = min_t(unsigned long, PFN_UP(start), MAX_DMA_PFN);
		end_pfn = min_t(unsigned long, PFN_DOWN(end), MAX_DMA_PFN);
		if (start_pfn < end_pfn)
			nr_free_pages += end_pfn - start_pfn;
	}

	set_dma_reserve(nr_pages - nr_free_pages);
#endif
}
