/*
 * Implements the dump driver interface for saving a dump in available
 * memory areas. The saved pages may be written out to persistent storage  
 * after a soft reboot.
 *
 * Started: Oct 2002 -  Suparna Bhattacharya <suparna@in.ibm.com>
 *
 * Copyright (C) 2002 International Business Machines Corp. 
 *
 * This code is released under version 2 of the GNU GPL.
 *
 * The approach of tracking pages containing saved dump using map pages 
 * allocated as needed has been derived from the Mission Critical Linux 
 * mcore dump implementation. 
 *
 * Credits and a big thanks for letting the lkcd project make use of 
 * the excellent piece of work and also helping with clarifications 
 * and tips along the way are due to:
 * 	Dave Winchell <winchell@mclx.com> (primary author of mcore)
 * 	Jeff Moyer <moyer@mclx.com>
 * 	Josh Huber <huber@mclx.com>
 *
 * For those familiar with the mcore code, the main differences worth
 * noting here (besides the dump device abstraction) result from enabling 
 * "high" memory pages (pages not permanently mapped in the kernel 
 * address space) to be used for saving dump data (because of which a 
 * simple virtual address based linked list cannot be used anymore for 
 * managing free pages), an added level of indirection for faster 
 * lookups during the post-boot stage, and the idea of pages being 
 * made available as they get freed up while dump to memory progresses 
 * rather than one time before starting the dump. The last point enables 
 * a full memory snapshot to be saved starting with an initial set of 
 * bootstrap pages given a good compression ratio. (See dump_overlay.c)
 *
 */

/*
 * -----------------MEMORY LAYOUT ------------------
 * The memory space consists of a set of discontiguous pages, and
 * discontiguous map pages as well, rooted in a chain of indirect
 * map pages (also discontiguous). Except for the indirect maps 
 * (which must be preallocated in advance), the rest of the pages 
 * could be in high memory.
 *
 * root
 *  |    ---------    --------        --------
 *  -->  | .  . +|--->|  .  +|------->| . .  |       indirect 
 *       --|--|---    ---|----        --|-|---	     maps
 *         |  |          |     	        | |	
 *    ------  ------   -------     ------ -------
 *    | .  |  | .  |   | .  . |    | .  | |  . . |   maps 
 *    --|---  --|---   --|--|--    --|--- ---|-|--
 *     page    page    page page   page   page page  data
 *                                                   pages
 *
 * Writes to the dump device happen sequentially in append mode.
 * The main reason for the existence of the indirect map is
 * to enable a quick way to lookup a specific logical offset in
 * the saved data post-soft-boot, e.g. to writeout pages
 * with more critical data first, even though such pages
 * would have been compressed and copied last, being the lowest
 * ranked candidates for reuse due to their criticality.
 * (See dump_overlay.c)
 */
#include <linux/mm.h>
#include <linux/highmem.h>
#include <linux/bootmem.h>
#include <linux/dump.h>
#include "dump_methods.h"

#define DUMP_MAP_SZ (PAGE_SIZE / sizeof(unsigned long)) /* direct map size */
#define DUMP_IND_MAP_SZ	DUMP_MAP_SZ - 1  /* indirect map size */
#define DUMP_NR_BOOTSTRAP	64  /* no of bootstrap pages */

extern int dump_low_page(struct page *);

/* check if the next entry crosses a page boundary */
static inline int is_last_map_entry(unsigned long *map)
{
	unsigned long addr = (unsigned long)(map + 1);

	return (!(addr & (PAGE_SIZE - 1)));
}

/* Todo: should have some validation checks */
/* The last entry in the indirect map points to the next indirect map */
/* Indirect maps are referred to directly by virtual address */
static inline unsigned long *next_indirect_map(unsigned long *map)
{
	return (unsigned long *)map[DUMP_IND_MAP_SZ];
}

#ifdef CONFIG_CRASH_DUMP_SOFTBOOT
/* Called during early bootup - fixme: make this __init */
void dump_early_reserve_map(struct dump_memdev *dev)
{
	unsigned long *map1, *map2;
	loff_t off = 0, last = dev->last_used_offset >> PAGE_SHIFT;
	int i, j;
	
	printk("Reserve bootmap space holding previous dump of %lld pages\n",
			last);
	map1= (unsigned long *)dev->indirect_map_root;

	while (map1 && (off < last)) {
#ifdef CONFIG_X86_64
		reserve_bootmem_node(NODE_DATA(0), virt_to_phys((void *)map1),
				 PAGE_SIZE);
#else
		reserve_bootmem(virt_to_phys((void *)map1), PAGE_SIZE);
#endif
		for (i=0;  (i < DUMP_MAP_SZ - 1) && map1[i] && (off < last); 
			i++, off += DUMP_MAP_SZ) {
			pr_debug("indirect map[%d] = 0x%lx\n", i, map1[i]);
			if (map1[i] >= max_low_pfn)
				continue;
#ifdef CONFIG_X86_64
			reserve_bootmem_node(NODE_DATA(0), 
					map1[i] << PAGE_SHIFT, PAGE_SIZE);
#else
			reserve_bootmem(map1[i] << PAGE_SHIFT, PAGE_SIZE);
#endif
			map2 = pfn_to_kaddr(map1[i]);
			for (j = 0 ; (j < DUMP_MAP_SZ) && map2[j] && 
				(off + j < last); j++) {
				pr_debug("\t map[%d][%d] = 0x%lx\n", i, j, 
					map2[j]);
				if (map2[j] < max_low_pfn) {
#ifdef CONFIG_X86_64
					reserve_bootmem_node(NODE_DATA(0),
						map2[j] << PAGE_SHIFT,
						PAGE_SIZE);
#else
					reserve_bootmem(map2[j] << PAGE_SHIFT,
						PAGE_SIZE);
#endif
				}
			}
		}
		map1 = next_indirect_map(map1);
	}
	dev->nr_free = 0; /* these pages don't belong to this boot */
}
#endif

/* mark dump pages so that they aren't used by this kernel */
void dump_mark_map(struct dump_memdev *dev)
{
	unsigned long *map1, *map2;
	loff_t off = 0, last = dev->last_used_offset >> PAGE_SHIFT;
	struct page *page;
	int i, j;
	
	printk("Dump: marking pages in use by previous dump\n");
	map1= (unsigned long *)dev->indirect_map_root;

	while (map1 && (off < last)) {
		page = virt_to_page(map1);	
		set_page_count(page, 1);
		for (i=0;  (i < DUMP_MAP_SZ - 1) && map1[i] && (off < last); 
			i++, off += DUMP_MAP_SZ) {
			pr_debug("indirect map[%d] = 0x%lx\n", i, map1[i]);
			page = pfn_to_page(map1[i]);
			set_page_count(page, 1);
			map2 = kmap_atomic(page, KM_DUMP);
			for (j = 0 ; (j < DUMP_MAP_SZ) && map2[j] && 
				(off + j < last); j++) {
				pr_debug("\t map[%d][%d] = 0x%lx\n", i, j, 
					map2[j]);
				page = pfn_to_page(map2[j]);
				set_page_count(page, 1);
			}
		}
		map1 = next_indirect_map(map1);
	}
}
	

/* 
 * Given a logical offset into the mem device lookup the 
 * corresponding page 
 * 	loc is specified in units of pages 
 * Note: affects curr_map (even in the case where lookup fails)
 */
struct page *dump_mem_lookup(struct dump_memdev *dump_mdev, unsigned long loc)
{
	unsigned long *map;
	unsigned long i, index = loc / DUMP_MAP_SZ;
	struct page *page = NULL;
	unsigned long curr_pfn, curr_map, *curr_map_ptr = NULL;

	map = (unsigned long *)dump_mdev->indirect_map_root;
	if (!map)
		return NULL;

	if (loc > dump_mdev->last_offset >> PAGE_SHIFT)
		return NULL;

	/* 
	 * first locate the right indirect map 
	 * in the chain of indirect maps 
	 */
	for (i = 0; i + DUMP_IND_MAP_SZ < index ; i += DUMP_IND_MAP_SZ) {
		if (!(map = next_indirect_map(map)))
			return NULL;
	}
	/* then the right direct map */
	/* map entries are referred to by page index */
	if ((curr_map = map[index - i])) {
		page = pfn_to_page(curr_map);
		/* update the current traversal index */
		/* dump_mdev->curr_map = &map[index - i];*/
		curr_map_ptr = &map[index - i];
	}

	if (page)
		map = kmap_atomic(page, KM_DUMP);
	else 
		return NULL;

	/* and finally the right entry therein */
	/* data pages are referred to by page index */
	i = index * DUMP_MAP_SZ;
	if ((curr_pfn = map[loc - i])) {
		page = pfn_to_page(curr_pfn);
		dump_mdev->curr_map = curr_map_ptr;
		dump_mdev->curr_map_offset = loc - i;
		dump_mdev->ddev.curr_offset = loc << PAGE_SHIFT;
	} else {
		page = NULL;
	}
	kunmap_atomic(map, KM_DUMP);

	return page;
}
			
/* 
 * Retrieves a pointer to the next page in the dump device 
 * Used during the lookup pass post-soft-reboot 
 */
struct page *dump_mem_next_page(struct dump_memdev *dev)
{
	unsigned long i; 
	unsigned long *map;	
	struct page *page = NULL;

	if (dev->ddev.curr_offset + PAGE_SIZE >= dev->last_offset) {
		return NULL;
	}

	if ((i = (unsigned long)(++dev->curr_map_offset)) >= DUMP_MAP_SZ) {
		/* move to next map */	
		if (is_last_map_entry(++dev->curr_map)) {
			/* move to the next indirect map page */
			printk("dump_mem_next_page: go to next indirect map\n");
			dev->curr_map = (unsigned long *)*dev->curr_map;
			if (!dev->curr_map)
				return NULL;
		}
		i = dev->curr_map_offset = 0;
		pr_debug("dump_mem_next_page: next map 0x%lx, entry 0x%lx\n",
				dev->curr_map, *dev->curr_map);

	};
	
	if (*dev->curr_map) {
		map = kmap_atomic(pfn_to_page(*dev->curr_map), KM_DUMP);
		if (map[i])
			page = pfn_to_page(map[i]);
		kunmap_atomic(map, KM_DUMP);
		dev->ddev.curr_offset += PAGE_SIZE;
	};

	return page;
}

/* Copied from dump_filters.c */
static inline int kernel_page(struct page *p)
{
	/* FIXME: Need to exclude hugetlb pages. Clue: reserved but inuse */
	return (PageReserved(p) && !PageInuse(p)) || (!PageLRU(p) && PageInuse(p));
}

static inline int user_page(struct page *p)
{
	return PageInuse(p) && (!PageReserved(p) && PageLRU(p));
}

int dump_reused_by_boot(struct page *page)
{
	/* Todo
	 * Checks:
	 * if PageReserved 
	 * if < __end + bootmem_bootmap_pages for this boot + allowance 
	 * if overwritten by initrd (how to check ?)
	 * Also, add more checks in early boot code
	 * e.g. bootmem bootmap alloc verify not overwriting dump, and if
	 * so then realloc or move the dump pages out accordingly.
	 */

	/* Temporary proof of concept hack, avoid overwriting kern pages */

	return (kernel_page(page) || dump_low_page(page) || user_page(page));
}


/* Uses the free page passed in to expand available space */
int dump_mem_add_space(struct dump_memdev *dev, struct page *page)
{
	struct page *map_page;
	unsigned long *map;	
	unsigned long i; 

	if (!dev->curr_map)
		return -ENOMEM; /* must've exhausted indirect map */

	if (!*dev->curr_map || dev->curr_map_offset >= DUMP_MAP_SZ) {
		/* add map space */
		*dev->curr_map = page_to_pfn(page);
		dev->curr_map_offset = 0;
		return 0;
	}

	/* add data space */
	i = dev->curr_map_offset;
	map_page = pfn_to_page(*dev->curr_map);
	map = (unsigned long *)kmap_atomic(map_page, KM_DUMP);
	map[i] = page_to_pfn(page);
	kunmap_atomic(map, KM_DUMP);
	dev->curr_map_offset = ++i;
	dev->last_offset += PAGE_SIZE;
	if (i >= DUMP_MAP_SZ) {
		/* move to next map */
		if (is_last_map_entry(++dev->curr_map)) {
			/* move to the next indirect map page */
			pr_debug("dump_mem_add_space: using next"
			"indirect map\n");
			dev->curr_map = (unsigned long *)*dev->curr_map;
		}
	}		
	return 0;
}


/* Caution: making a dest page invalidates existing contents of the page */
int dump_check_and_free_page(struct dump_memdev *dev, struct page *page)
{
	int err = 0;

	/* 
	 * the page can be used as a destination only if we are sure
	 * it won't get overwritten by the soft-boot, and is not
	 * critical for us right now.
	 */
	if (dump_reused_by_boot(page))
		return 0;

	if ((err = dump_mem_add_space(dev, page))) {
		printk("Warning: Unable to extend memdev space. Err %d\n",
		err);
		return 0;
	}

	dev->nr_free++;
	return 1;
}


/* Set up the initial maps and bootstrap space  */
/* Must be called only after any previous dump is written out */
int dump_mem_open(struct dump_dev *dev, unsigned long devid)
{
	struct dump_memdev *dump_mdev = DUMP_MDEV(dev);
	unsigned long nr_maps, *map, *prev_map = &dump_mdev->indirect_map_root;
	void *addr;
	struct page *page;
	unsigned long i = 0;
	int err = 0;

	/* Todo: sanity check for unwritten previous dump */

	/* allocate pages for indirect map (non highmem area) */
	nr_maps = num_physpages / DUMP_MAP_SZ; /* maps to cover entire mem */
	for (i = 0; i < nr_maps; i += DUMP_IND_MAP_SZ) {
		if (!(map = (unsigned long *)dump_alloc_mem(PAGE_SIZE))) {
			printk("Unable to alloc indirect map %ld\n", 
				i / DUMP_IND_MAP_SZ);
			return -ENOMEM;
		}
		clear_page(map);
		*prev_map = (unsigned long)map;
		prev_map = &map[DUMP_IND_MAP_SZ];
	};
		
	dump_mdev->curr_map = (unsigned long *)dump_mdev->indirect_map_root;
	dump_mdev->curr_map_offset = 0;	

	/* 
	 * allocate a few bootstrap pages: at least 1 map and 1 data page
	 * plus enough to save the dump header
	 */
	i = 0;
	do {
		if (!(addr = dump_alloc_mem(PAGE_SIZE))) {
			printk("Unable to alloc bootstrap page %ld\n", i);
			return -ENOMEM;
		}

		page = virt_to_page(addr);
		if (dump_low_page(page)) {
			dump_free_mem(addr);
			continue;
		}

		if (dump_mem_add_space(dump_mdev, page)) {
			printk("Warning: Unable to extend memdev "
					"space. Err %d\n", err);
			dump_free_mem(addr);
			continue;
		}
		i++;
	} while (i < DUMP_NR_BOOTSTRAP);

	printk("dump memdev init: %ld maps, %ld bootstrap pgs, %ld free pgs\n",
		nr_maps, i, dump_mdev->last_offset >> PAGE_SHIFT);
	
	dump_mdev->last_bs_offset = dump_mdev->last_offset;

	return 0;
}

/* Releases all pre-alloc'd pages */
int dump_mem_release(struct dump_dev *dev)
{
	struct dump_memdev *dump_mdev = DUMP_MDEV(dev);
	struct page *page, *map_page;
	unsigned long *map, *prev_map;
	void *addr;
	int i;

	if (!dump_mdev->nr_free)
		return 0;

	pr_debug("dump_mem_release\n");
	page = dump_mem_lookup(dump_mdev, 0);
	for (i = 0; page && (i < DUMP_NR_BOOTSTRAP - 1); i++) {
		if (PageHighMem(page))
			break;
		addr = page_address(page);
		if (!addr) {
			printk("page_address(%p) = NULL\n", page);
			break;
		}
		pr_debug("Freeing page at 0x%lx\n", addr); 
		dump_free_mem(addr);
		if (dump_mdev->curr_map_offset >= DUMP_MAP_SZ - 1) {
			map_page = pfn_to_page(*dump_mdev->curr_map);
			if (PageHighMem(map_page))
				break;
			page = dump_mem_next_page(dump_mdev);
			addr = page_address(map_page);
			if (!addr) {
				printk("page_address(%p) = NULL\n", 
					map_page);
				break;
			}
			pr_debug("Freeing map page at 0x%lx\n", addr);
			dump_free_mem(addr);
			i++;
		} else {
			page = dump_mem_next_page(dump_mdev);
		}
	}

	/* now for the last used bootstrap page used as a map page */
	if ((i < DUMP_NR_BOOTSTRAP) && (*dump_mdev->curr_map)) {
		map_page = pfn_to_page(*dump_mdev->curr_map);
		if ((map_page) && !PageHighMem(map_page)) {
			addr = page_address(map_page);
			if (!addr) {
				printk("page_address(%p) = NULL\n", map_page);
			} else {
				pr_debug("Freeing map page at 0x%lx\n", addr);
				dump_free_mem(addr);
				i++;
			}
		}
	}

	printk("Freed %d bootstrap pages\n", i);

	/* free the indirect maps */
	map = (unsigned long *)dump_mdev->indirect_map_root;

	i = 0;
	while (map) {
		prev_map = map;
		map = next_indirect_map(map);
		dump_free_mem(prev_map);
		i++;
	}

	printk("Freed %d indirect map(s)\n", i);

	/* Reset the indirect map */
	dump_mdev->indirect_map_root = 0;
	dump_mdev->curr_map = 0;

	/* Reset the free list */
	dump_mdev->nr_free = 0;

	dump_mdev->last_offset = dump_mdev->ddev.curr_offset = 0;
	dump_mdev->last_used_offset = 0;
	dump_mdev->curr_map = NULL;
	dump_mdev->curr_map_offset = 0;
	return 0;
}

/*
 * Long term:
 * It is critical for this to be very strict. Cannot afford
 * to have anything running and accessing memory while we overwrite 
 * memory (potential risk of data corruption).
 * If in doubt (e.g if a cpu is hung and not responding) just give
 * up and refuse to proceed with this scheme.
 *
 * Note: I/O will only happen after soft-boot/switchover, so we can 
 * safely disable interrupts and force stop other CPUs if this is
 * going to be a disruptive dump, no matter what they
 * are in the middle of.
 */
/* 
 * ATM Most of this is already taken care of in the nmi handler 
 * We may halt the cpus rightaway if we know this is going to be disruptive 
 * For now, since we've limited ourselves to overwriting free pages we
 * aren't doing much here. Eventually, we'd have to wait to make sure other
 * cpus aren't using memory we could be overwriting
 */
int dump_mem_silence(struct dump_dev *dev)
{
	struct dump_memdev *dump_mdev = DUMP_MDEV(dev);

	if (dump_mdev->last_offset > dump_mdev->last_bs_offset) {
		/* prefer to run lkcd config & start with a clean slate */
		return -EEXIST;
	}
	return 0;
}

extern int dump_overlay_resume(void);

/* Trigger the next stage of dumping */
int dump_mem_resume(struct dump_dev *dev)
{
	dump_overlay_resume(); 
	return 0;
}

/* 
 * Allocate mem dev pages as required and copy buffer contents into it.
 * Fails if the no free pages are available
 * Keeping it simple and limited for starters (can modify this over time)
 *  Does not handle holes or a sparse layout
 *  Data must be in multiples of PAGE_SIZE
 */
int dump_mem_write(struct dump_dev *dev, void *buf, unsigned long len)
{
	struct dump_memdev *dump_mdev = DUMP_MDEV(dev);
	struct page *page;
	unsigned long n = 0;
	void *addr;
	unsigned long *saved_curr_map, saved_map_offset;
	int ret = 0;

	pr_debug("dump_mem_write: offset 0x%llx, size %ld\n", 
		dev->curr_offset, len);

	if (dev->curr_offset + len > dump_mdev->last_offset)  {
		printk("Out of space to write\n");
		return -ENOSPC;
	}
	
	if ((len & (PAGE_SIZE - 1)) || (dev->curr_offset & (PAGE_SIZE - 1)))
		return -EINVAL; /* not aligned in units of page size */

	saved_curr_map = dump_mdev->curr_map;
	saved_map_offset = dump_mdev->curr_map_offset;
	page = dump_mem_lookup(dump_mdev, dev->curr_offset >> PAGE_SHIFT);

	for (n = len; (n > 0) && page; n -= PAGE_SIZE, buf += PAGE_SIZE ) {
		addr = kmap_atomic(page, KM_DUMP);
		/* memset(addr, 'x', PAGE_SIZE); */
		memcpy(addr, buf, PAGE_SIZE);
		kunmap_atomic(addr, KM_DUMP);
		/* dev->curr_offset += PAGE_SIZE; */
		page = dump_mem_next_page(dump_mdev);
	}

	dump_mdev->curr_map = saved_curr_map;
	dump_mdev->curr_map_offset = saved_map_offset;

	if (dump_mdev->last_used_offset < dev->curr_offset)
		dump_mdev->last_used_offset = dev->curr_offset;

	return (len - n) ? (len - n) : ret ;
}

/* dummy - always ready */
int dump_mem_ready(struct dump_dev *dev, void *buf)
{
	return 0;
}

/* 
 * Should check for availability of space to write upto the offset 
 * affects only the curr_offset; last_offset untouched 
 * Keep it simple: Only allow multiples of PAGE_SIZE for now 
 */
int dump_mem_seek(struct dump_dev *dev, loff_t offset)
{
	struct dump_memdev *dump_mdev = DUMP_MDEV(dev);

	if (offset & (PAGE_SIZE - 1))
		return -EINVAL; /* allow page size units only for now */
	
	/* Are we exceeding available space ? */
	if (offset > dump_mdev->last_offset) {
		printk("dump_mem_seek failed for offset 0x%llx\n",
			offset);
		return -ENOSPC;	
	}

	dump_mdev->ddev.curr_offset = offset;
	return 0;
}

struct dump_dev_ops dump_memdev_ops = {
	.open 		= dump_mem_open,
	.release	= dump_mem_release,
	.silence	= dump_mem_silence,
	.resume 	= dump_mem_resume,
	.seek		= dump_mem_seek,
	.write		= dump_mem_write,
	.read		= NULL, /* not implemented at the moment */
	.ready		= dump_mem_ready
};

static struct dump_memdev default_dump_memdev = {
	.ddev = {.type_name = "memdev", .ops = &dump_memdev_ops,
        	 .device_id = 0x14}
	/* assume the rest of the fields are zeroed by default */
};	
	
/* may be overwritten if a previous dump exists */
struct dump_memdev *dump_memdev = &default_dump_memdev;

