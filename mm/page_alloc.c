/*
 *  linux/mm/page_alloc.c
 *
 *  Copyright (C) 1991, 1992, 1993, 1994  Linus Torvalds
 *  Swap reorganised 29.12.95, Stephen Tweedie
 *  Support of BIGMEM added by Gerhard Wichert, Siemens AG, July 1999
 *  Reshaped it to be a zoned allocator, Ingo Molnar, Red Hat, 1999
 *  Discontiguous memory support, Kanoj Sarcar, SGI, Nov 1999
 *  Zone balancing, Kanoj Sarcar, SGI, Jan 2000
 */

#include <linux/config.h>
#include <linux/mm.h>
#include <linux/swap.h>
#include <linux/swapctl.h>
#include <linux/interrupt.h>
#include <linux/pagemap.h>
#include <linux/bootmem.h>
#include <linux/slab.h>
#include <linux/compiler.h>

int nr_swap_pages;
int nr_active_pages;
int nr_inactive_pages;
struct list_head inactive_list;
struct list_head active_list;
pg_data_t *pgdat_list;

static char *zone_names[MAX_NR_ZONES] = { "DMA", "Normal", "HighMem" };
static int zone_balance_ratio[MAX_NR_ZONES] __initdata = { 32, 128, 128, };
static int zone_balance_min[MAX_NR_ZONES] __initdata = { 20 , 20, 20, };
static int zone_balance_max[MAX_NR_ZONES] __initdata = { 255 , 255, 255, };

/*
 * Free_page() adds the page to the free lists. This is optimized for
 * fast normal cases (no error jumps taken normally).
 *
 * The way to optimize jumps for gcc-2.2.2 is to:
 *  - select the "normal" case and put it inside the if () { XXX }
 *  - no else-statements if you can avoid them
 *
 * With the above two rules, you get a straight-line execution path
 * for the normal case, giving better asm-code.
 */

#define memlist_init(x) INIT_LIST_HEAD(x)
#define memlist_add_head list_add
#define memlist_add_tail list_add_tail
#define memlist_del list_del
#define memlist_entry list_entry
#define memlist_next(x) ((x)->next)
#define memlist_prev(x) ((x)->prev)

/*
 * Temporary debugging check.
 */
#define BAD_RANGE(zone,x) (((zone) != (x)->zone) || (((x)-mem_map) < (zone)->zone_start_mapnr) || (((x)-mem_map) >= (zone)->zone_start_mapnr+(zone)->size))

/*
 * Buddy system. Hairy. You really aren't expected to understand this
 *
 * Hint: -mask = 1+~mask
 */

static void FASTCALL(__free_pages_ok (struct page *page, unsigned int order));
static void __free_pages_ok (struct page *page, unsigned int order)
{
	unsigned long index, page_idx, mask, flags;
	free_area_t *area;
	struct page *base;
	zone_t *zone;

	if (page->buffers)
		BUG();
	if (page->mapping)
		BUG();
	if (!VALID_PAGE(page))
		BUG();
	if (PageSwapCache(page))
		BUG();
	if (PageLocked(page))
		BUG();
	if (PageDecrAfter(page))
		BUG();
	if (PageActive(page))
		BUG();
	if (PageInactive(page))
		BUG();
	page->flags &= ~((1<<PG_referenced) | (1<<PG_dirty));

	if (current->flags & PF_FREE_PAGES)
		goto local_freelist;
 back_local_freelist:

	zone = page->zone;

	mask = (~0UL) << order;
	base = zone->zone_mem_map;
	page_idx = page - base;
	if (page_idx & ~mask)
		BUG();
	index = page_idx >> (1 + order);

	area = zone->free_area + order;

	spin_lock_irqsave(&zone->lock, flags);

	zone->free_pages -= mask;

	while (mask + (1 << (MAX_ORDER-1))) {
		struct page *buddy1, *buddy2;

		if (area >= zone->free_area + MAX_ORDER)
			BUG();
		if (!__test_and_change_bit(index, area->map))
			/*
			 * the buddy page is still allocated.
			 */
			break;
		/*
		 * Move the buddy up one level.
		 */
		buddy1 = base + (page_idx ^ -mask);
		buddy2 = base + page_idx;
		if (BAD_RANGE(zone,buddy1))
			BUG();
		if (BAD_RANGE(zone,buddy2))
			BUG();

		memlist_del(&buddy1->list);
		mask <<= 1;
		area++;
		index >>= 1;
		page_idx &= mask;
	}
	memlist_add_head(&(base + page_idx)->list, &area->free_list);

	spin_unlock_irqrestore(&zone->lock, flags);
	return;

 local_freelist:
	/*
	 * This is a little subtle: if the allocation order
	 * wanted is major than zero we'd better take all the pages
	 * local since we must deal with fragmentation too and we
	 * can't rely on the nr_local_pages information.
	 */
	if (current->nr_local_pages && !current->allocation_order)
		goto back_local_freelist;

	list_add(&page->list, &current->local_pages);
	page->index = order;
	current->nr_local_pages++;
}

#define MARK_USED(index, order, area) \
	__change_bit((index) >> (1+(order)), (area)->map)

static inline struct page * expand (zone_t *zone, struct page *page,
	 unsigned long index, int low, int high, free_area_t * area)
{
	unsigned long size = 1 << high;

	while (high > low) {
		if (BAD_RANGE(zone,page))
			BUG();
		area--;
		high--;
		size >>= 1;
		memlist_add_head(&(page)->list, &(area)->free_list);
		MARK_USED(index, high, area);
		index += size;
		page += size;
	}
	if (BAD_RANGE(zone,page))
		BUG();
	return page;
}

static FASTCALL(struct page * rmqueue(zone_t *zone, unsigned int order));
static struct page * rmqueue(zone_t *zone, unsigned int order)
{
	free_area_t * area = zone->free_area + order;
	unsigned int curr_order = order;
	struct list_head *head, *curr;
	unsigned long flags;
	struct page *page;

	spin_lock_irqsave(&zone->lock, flags);
	do {
		head = &area->free_list;
		curr = memlist_next(head);

		if (curr != head) {
			unsigned int index;

			page = memlist_entry(curr, struct page, list);
			if (BAD_RANGE(zone,page))
				BUG();
			memlist_del(curr);
			index = page - zone->zone_mem_map;
			if (curr_order != MAX_ORDER-1)
				MARK_USED(index, curr_order, area);
			zone->free_pages -= 1UL << order;

			page = expand(zone, page, index, order, curr_order, area);
			spin_unlock_irqrestore(&zone->lock, flags);

			set_page_count(page, 1);
			if (BAD_RANGE(zone,page))
				BUG();
			DEBUG_LRU_PAGE(page);
			return page;	
		}
		curr_order++;
		area++;
	} while (curr_order < MAX_ORDER);
	spin_unlock_irqrestore(&zone->lock, flags);

	return NULL;
}

#ifndef CONFIG_DISCONTIGMEM
struct page *_alloc_pages(unsigned int gfp_mask, unsigned int order)
{
	return __alloc_pages(gfp_mask, order,
		contig_page_data.node_zonelists+(gfp_mask & GFP_ZONEMASK));
}
#endif

static struct page * FASTCALL(balance_classzone(zone_t *, unsigned int, unsigned int, int *));
static struct page * balance_classzone(zone_t * classzone, unsigned int gfp_mask, unsigned int order, int * freed)
{
	struct page * page = NULL;
	int __freed = 0;

	if (!(gfp_mask & __GFP_WAIT))
		goto out;
	if (in_interrupt())
		BUG();

	current->allocation_order = order;
	current->flags |= PF_MEMALLOC | PF_FREE_PAGES;

	__freed = try_to_free_pages(classzone, gfp_mask, order);

	current->flags &= ~(PF_MEMALLOC | PF_FREE_PAGES);

	if (current->nr_local_pages) {
		struct list_head * entry, * local_pages;
		struct page * tmp;
		int nr_pages;

		local_pages = &current->local_pages;

		if (likely(__freed)) {
			/* pick from the last inserted so we're lifo */
			entry = local_pages->next;
			do {
				tmp = list_entry(entry, struct page, list);
				if (tmp->index == order && memclass(tmp->zone, classzone)) {
					list_del(entry);
					current->nr_local_pages--;
					set_page_count(tmp, 1);
					page = tmp;

					if (page->buffers)
						BUG();
					if (page->mapping)
						BUG();
					if (!VALID_PAGE(page))
						BUG();
					if (PageSwapCache(page))
						BUG();
					if (PageLocked(page))
						BUG();
					if (PageDecrAfter(page))
						BUG();
					if (PageActive(page))
						BUG();
					if (PageInactive(page))
						BUG();
					if (PageDirty(page))
						BUG();

					break;
				}
			} while ((entry = entry->next) != local_pages);
		}

		nr_pages = current->nr_local_pages;
		/* free in reverse order so that the global order will be lifo */
		while ((entry = local_pages->prev) != local_pages) {
			list_del(entry);
			tmp = list_entry(entry, struct page, list);
			__free_pages_ok(tmp, tmp->index);
			if (!nr_pages--)
				BUG();
		}
		current->nr_local_pages = 0;
	}
 out:
	*freed = __freed;
	return page;
}

static inline unsigned long zone_free_pages(zone_t * zone, unsigned int order)
{
	long free = zone->free_pages - (1UL << order);
	return free >= 0 ? free : 0;
}

/*
 * This is the 'heart' of the zoned buddy allocator:
 */
struct page * __alloc_pages(unsigned int gfp_mask, unsigned int order, zonelist_t *zonelist)
{
	zone_t **zone, * classzone;
	struct page * page;
	int freed;

	zone = zonelist->zones;
	classzone = *zone;
	for (;;) {
		zone_t *z = *(zone++);
		if (!z)
			break;

		if (zone_free_pages(z, order) > z->pages_low) {
			page = rmqueue(z, order);
			if (page)
				return page;
		}
	}

	classzone->need_balance = 1;
	mb();
	if (waitqueue_active(&kswapd_wait))
		wake_up_interruptible(&kswapd_wait);

	zone = zonelist->zones;
	for (;;) {
		unsigned long min;
		zone_t *z = *(zone++);
		if (!z)
			break;

		min = z->pages_min;
		if (!(gfp_mask & __GFP_WAIT))
			min >>= 2;
		if (zone_free_pages(z, order) > min) {
			page = rmqueue(z, order);
			if (page)
				return page;
		}
	}

	/* here we're in the low on memory slow path */

rebalance:
	if (current->flags & PF_MEMALLOC) {
		zone = zonelist->zones;
		for (;;) {
			zone_t *z = *(zone++);
			if (!z)
				break;

			page = rmqueue(z, order);
			if (page)
				return page;
		}
		return NULL;
	}

	/* Atomic allocations - we can't balance anything */
	if (!(gfp_mask & __GFP_WAIT))
		return NULL;

	page = balance_classzone(classzone, gfp_mask, order, &freed);
	if (page)
		return page;

	zone = zonelist->zones;
	for (;;) {
		zone_t *z = *(zone++);
		if (!z)
			break;

		if (zone_free_pages(z, order) > z->pages_min) {
			page = rmqueue(z, order);
			if (page)
				return page;
		}
	}

	/* Don't let big-order allocations loop */
	if (order)
		return NULL;

	/* Yield for kswapd, and try again */
	current->policy |= SCHED_YIELD;
	__set_current_state(TASK_RUNNING);
	schedule();
	goto rebalance;
}

/*
 * Common helper functions.
 */
unsigned long __get_free_pages(unsigned int gfp_mask, unsigned int order)
{
	struct page * page;

	page = alloc_pages(gfp_mask, order);
	if (!page)
		return 0;
	return (unsigned long) page_address(page);
}

unsigned long get_zeroed_page(unsigned int gfp_mask)
{
	struct page * page;

	page = alloc_pages(gfp_mask, 0);
	if (page) {
		void *address = page_address(page);
		clear_page(address);
		return (unsigned long) address;
	}
	return 0;
}

void __free_pages(struct page *page, unsigned int order)
{
	if (!PageReserved(page) && put_page_testzero(page))
		__free_pages_ok(page, order);
}

void free_pages(unsigned long addr, unsigned int order)
{
	if (addr != 0)
		__free_pages(virt_to_page(addr), order);
}

/*
 * Total amount of free (allocatable) RAM:
 */
unsigned int nr_free_pages (void)
{
	unsigned int sum;
	zone_t *zone;
	pg_data_t *pgdat = pgdat_list;

	sum = 0;
	while (pgdat) {
		for (zone = pgdat->node_zones; zone < pgdat->node_zones + MAX_NR_ZONES; zone++)
			sum += zone->free_pages;
		pgdat = pgdat->node_next;
	}
	return sum;
}

/*
 * Amount of free RAM allocatable as buffer memory:
 */
unsigned int nr_free_buffer_pages (void)
{
	pg_data_t *pgdat = pgdat_list;
	unsigned int sum = 0;
	zonelist_t *zonelist;
	zone_t **zonep, *zone;

	do {
		zonelist = pgdat->node_zonelists + (GFP_USER & GFP_ZONEMASK);
		zonep = zonelist->zones;

		for (zone = *zonep++; zone; zone = *zonep++)
			sum += zone->free_pages;

		pgdat = pgdat->node_next;
	} while (pgdat);

	return sum + nr_active_pages + nr_inactive_pages;
}

#if CONFIG_HIGHMEM
unsigned int nr_free_highpages (void)
{
	pg_data_t *pgdat = pgdat_list;
	unsigned int pages = 0;

	while (pgdat) {
		pages += pgdat->node_zones[ZONE_HIGHMEM].free_pages;
		pgdat = pgdat->node_next;
	}
	return pages;
}
#endif

/*
 * Show free area list (used inside shift_scroll-lock stuff)
 * We also calculate the percentage fragmentation. We do this by counting the
 * memory on each free list with the exception of the first item on the list.
 */
void show_free_areas_core(pg_data_t *pgdat)
{
 	unsigned int order;
	unsigned type;
	pg_data_t *tmpdat = pgdat;

	printk("Free pages:      %6dkB (%6dkB HighMem)\n",
		nr_free_pages() << (PAGE_SHIFT-10),
		nr_free_highpages() << (PAGE_SHIFT-10));

	while (tmpdat) {
		zone_t *zone;
		for (zone = tmpdat->node_zones;
			       	zone < tmpdat->node_zones + MAX_NR_ZONES; zone++)
			printk("Zone:%s freepages:%6lukB min:%6luKB low:%6lukB " 
				       "high:%6lukB\n", 
					zone->name,
					(zone->free_pages)
					<< ((PAGE_SHIFT-10)),
					zone->pages_min
					<< ((PAGE_SHIFT-10)),
					zone->pages_low
					<< ((PAGE_SHIFT-10)),
					zone->pages_high
					<< ((PAGE_SHIFT-10)));
			
		tmpdat = tmpdat->node_next;
	}

	printk("Free pages:      %6dkB (%6dkB HighMem)\n",
		nr_free_pages() << (PAGE_SHIFT-10),
		nr_free_highpages() << (PAGE_SHIFT-10));

	printk("( Active: %d, inactive: %d, free: %d )\n",
	       nr_active_pages,
	       nr_inactive_pages,
	       nr_free_pages());

	for (type = 0; type < MAX_NR_ZONES; type++) {
		struct list_head *head, *curr;
		zone_t *zone = pgdat->node_zones + type;
 		unsigned long nr, total, flags;

		total = 0;
		if (zone->size) {
			spin_lock_irqsave(&zone->lock, flags);
		 	for (order = 0; order < MAX_ORDER; order++) {
				head = &(zone->free_area + order)->free_list;
				curr = head;
				nr = 0;
				for (;;) {
					curr = memlist_next(curr);
					if (curr == head)
						break;
					nr++;
				}
				total += nr * (1 << order);
				printk("%lu*%lukB ", nr,
						(PAGE_SIZE>>10) << order);
			}
			spin_unlock_irqrestore(&zone->lock, flags);
		}
		printk("= %lukB)\n", total * (PAGE_SIZE>>10));
	}

#ifdef SWAP_CACHE_INFO
	show_swap_cache_info();
#endif	
}

void show_free_areas(void)
{
	show_free_areas_core(pgdat_list);
}

/*
 * Builds allocation fallback zone lists.
 */
static inline void build_zonelists(pg_data_t *pgdat)
{
	int i, j, k;

	for (i = 0; i <= GFP_ZONEMASK; i++) {
		zonelist_t *zonelist;
		zone_t *zone;

		zonelist = pgdat->node_zonelists + i;
		memset(zonelist, 0, sizeof(*zonelist));

		j = 0;
		k = ZONE_NORMAL;
		if (i & __GFP_HIGHMEM)
			k = ZONE_HIGHMEM;
		if (i & __GFP_DMA)
			k = ZONE_DMA;

		switch (k) {
			default:
				BUG();
			/*
			 * fallthrough:
			 */
			case ZONE_HIGHMEM:
				zone = pgdat->node_zones + ZONE_HIGHMEM;
				if (zone->size) {
#ifndef CONFIG_HIGHMEM
					BUG();
#endif
					zonelist->zones[j++] = zone;
				}
			case ZONE_NORMAL:
				zone = pgdat->node_zones + ZONE_NORMAL;
				if (zone->size)
					zonelist->zones[j++] = zone;
			case ZONE_DMA:
				zone = pgdat->node_zones + ZONE_DMA;
				if (zone->size)
					zonelist->zones[j++] = zone;
		}
		zonelist->zones[j++] = NULL;
	} 
}

#define LONG_ALIGN(x) (((x)+(sizeof(long))-1)&~((sizeof(long))-1))

/*
 * Set up the zone data structures:
 *   - mark all pages reserved
 *   - mark all memory queues empty
 *   - clear the memory bitmaps
 */
void __init free_area_init_core(int nid, pg_data_t *pgdat, struct page **gmap,
	unsigned long *zones_size, unsigned long zone_start_paddr, 
	unsigned long *zholes_size, struct page *lmem_map)
{
	struct page *p;
	unsigned long i, j;
	unsigned long map_size;
	unsigned long totalpages, offset, realtotalpages;
	const unsigned long zone_required_alignment = 1UL << (MAX_ORDER-1);

	if (zone_start_paddr & ~PAGE_MASK)
		BUG();

	totalpages = 0;
	for (i = 0; i < MAX_NR_ZONES; i++) {
		unsigned long size = zones_size[i];
		totalpages += size;
	}
	realtotalpages = totalpages;
	if (zholes_size)
		for (i = 0; i < MAX_NR_ZONES; i++)
			realtotalpages -= zholes_size[i];
			
	printk("On node %d totalpages: %lu\n", nid, realtotalpages);

	INIT_LIST_HEAD(&active_list);
	INIT_LIST_HEAD(&inactive_list);

	/*
	 * Some architectures (with lots of mem and discontinous memory
	 * maps) have to search for a good mem_map area:
	 * For discontigmem, the conceptual mem map array starts from 
	 * PAGE_OFFSET, we need to align the actual array onto a mem map 
	 * boundary, so that MAP_NR works.
	 */
	map_size = (totalpages + 1)*sizeof(struct page);
	if (lmem_map == (struct page *)0) {
		lmem_map = (struct page *) alloc_bootmem_node(pgdat, map_size);
		lmem_map = (struct page *)(PAGE_OFFSET + 
			MAP_ALIGN((unsigned long)lmem_map - PAGE_OFFSET));
	}
	*gmap = pgdat->node_mem_map = lmem_map;
	pgdat->node_size = totalpages;
	pgdat->node_start_paddr = zone_start_paddr;
	pgdat->node_start_mapnr = (lmem_map - mem_map);
	pgdat->nr_zones = 0;

	/*
	 * Initially all pages are reserved - free ones are freed
	 * up by free_all_bootmem() once the early boot process is
	 * done.
	 */
	for (p = lmem_map; p < lmem_map + totalpages; p++) {
		set_page_count(p, 0);
		SetPageReserved(p);
		init_waitqueue_head(&p->wait);
		memlist_init(&p->list);
	}

	offset = lmem_map - mem_map;	
	for (j = 0; j < MAX_NR_ZONES; j++) {
		zone_t *zone = pgdat->node_zones + j;
		unsigned long mask;
		unsigned long size, realsize;

		realsize = size = zones_size[j];
		if (zholes_size)
			realsize -= zholes_size[j];

		printk("zone(%lu): %lu pages.\n", j, size);
		zone->size = size;
		zone->name = zone_names[j];
		zone->lock = SPIN_LOCK_UNLOCKED;
		zone->zone_pgdat = pgdat;
		zone->free_pages = 0;
		zone->need_balance = 0;
		if (!size)
			continue;

		pgdat->nr_zones = j+1;

		mask = (realsize / zone_balance_ratio[j]);
		if (mask < zone_balance_min[j])
			mask = zone_balance_min[j];
		else if (mask > zone_balance_max[j])
			mask = zone_balance_max[j];
		zone->pages_min = mask;
		zone->pages_low = mask*2;
		zone->pages_high = mask*3;

		zone->zone_mem_map = mem_map + offset;
		zone->zone_start_mapnr = offset;
		zone->zone_start_paddr = zone_start_paddr;

		if ((zone_start_paddr >> PAGE_SHIFT) & (zone_required_alignment-1))
			printk("BUG: wrong zone alignment, it will crash\n");

		for (i = 0; i < size; i++) {
			struct page *page = mem_map + offset + i;
			page->zone = zone;
			if (j != ZONE_HIGHMEM)
				page->virtual = __va(zone_start_paddr);
			zone_start_paddr += PAGE_SIZE;
		}

		offset += size;
		for (i = 0; ; i++) {
			unsigned long bitmap_size;

			memlist_init(&zone->free_area[i].free_list);
			if (i == MAX_ORDER-1) {
				zone->free_area[i].map = NULL;
				break;
			}

			/*
			 * Page buddy system uses "index >> (i+1)",
			 * where "index" is at most "size-1".
			 *
			 * The extra "+3" is to round down to byte
			 * size (8 bits per byte assumption). Thus
			 * we get "(size-1) >> (i+4)" as the last byte
			 * we can access.
			 *
			 * The "+1" is because we want to round the
			 * byte allocation up rather than down. So
			 * we should have had a "+7" before we shifted
			 * down by three. Also, we have to add one as
			 * we actually _use_ the last bit (it's [0,n]
			 * inclusive, not [0,n[).
			 *
			 * So we actually had +7+1 before we shift
			 * down by 3. But (n+8) >> 3 == (n >> 3) + 1
			 * (modulo overflows, which we do not have).
			 *
			 * Finally, we LONG_ALIGN because all bitmap
			 * operations are on longs.
			 */
			bitmap_size = (size-1) >> (i+4);
			bitmap_size = LONG_ALIGN(bitmap_size+1);
			zone->free_area[i].map = 
			  (unsigned long *) alloc_bootmem_node(pgdat, bitmap_size);
		}
	}
	build_zonelists(pgdat);
}

void __init free_area_init(unsigned long *zones_size)
{
	free_area_init_core(0, &contig_page_data, &mem_map, zones_size, 0, 0, 0);
}

static int __init setup_mem_frac(char *str)
{
	int j = 0;

	while (get_option(&str, &zone_balance_ratio[j++]) == 2);
	printk("setup_mem_frac: ");
	for (j = 0; j < MAX_NR_ZONES; j++) printk("%d  ", zone_balance_ratio[j]);
	printk("\n");
	return 1;
}

__setup("memfrac=", setup_mem_frac);
