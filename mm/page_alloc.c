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

int nr_swap_pages;
int nr_active_pages;
int nr_inactive_dirty_pages;
pg_data_t *pgdat_list;

static char *zone_names[MAX_NR_ZONES] = { "DMA", "Normal", "HighMem" };
static int zone_balance_ratio[MAX_NR_ZONES] = { 32, 128, 128, };
static int zone_balance_min[MAX_NR_ZONES] = { 10 , 10, 10, };
static int zone_balance_max[MAX_NR_ZONES] = { 255 , 255, 255, };

struct list_head active_list;
struct list_head inactive_dirty_list;
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
#define BAD_RANGE(zone,x) (((zone) != (x)->zone) || (((x)-mem_map) < (zone)->offset) || (((x)-mem_map) >= (zone)->offset+(zone)->size))

/*
 * Buddy system. Hairy. You really aren't expected to understand this
 *
 * Hint: -mask = 1+~mask
 */

static void FASTCALL(__free_pages_ok (struct page *page, unsigned long order));
static void __free_pages_ok (struct page *page, unsigned long order)
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
	if (PageInactiveDirty(page))
		BUG();
	if (PageInactiveClean(page))
		BUG();

	page->flags &= ~((1<<PG_referenced) | (1<<PG_dirty));
	page->age = PAGE_AGE_START;
	
	zone = page->zone;

	mask = (~0UL) << order;
	base = mem_map + zone->offset;
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
		if (!test_and_change_bit(index, area->map))
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

	/*
	 * We don't want to protect this variable from race conditions
	 * since it's nothing important, but we do want to make sure
	 * it never gets negative.
	 */
	if (memory_pressure > NR_CPUS)
		memory_pressure--;
}

#define MARK_USED(index, order, area) \
	change_bit((index) >> (1+(order)), (area)->map)

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

static FASTCALL(struct page * rmqueue(zone_t *zone, unsigned long order));
static struct page * rmqueue(zone_t *zone, unsigned long order)
{
	free_area_t * area = zone->free_area + order;
	unsigned long curr_order = order;
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
			index = (page - mem_map) - zone->offset;
			MARK_USED(index, curr_order, area);
			zone->free_pages -= 1 << order;

			page = expand(zone, page, index, order, curr_order, area);
			spin_unlock_irqrestore(&zone->lock, flags);

			set_page_count(page, 1);
			if (BAD_RANGE(zone,page))
				BUG();
			DEBUG_ADD_PAGE
			return page;	
		}
		curr_order++;
		area++;
	} while (curr_order < MAX_ORDER);
	spin_unlock_irqrestore(&zone->lock, flags);

	return NULL;
}

#define PAGES_MIN	0
#define PAGES_LOW	1
#define PAGES_HIGH	2

/*
 * This function does the dirty work for __alloc_pages
 * and is separated out to keep the code size smaller.
 * (suggested by Davem at 1:30 AM, typed by Rik at 6 AM)
 */
static struct page * __alloc_pages_limit(zonelist_t *zonelist,
			unsigned long order, int limit, int direct_reclaim)
{
	zone_t **zone = zonelist->zones;

	for (;;) {
		zone_t *z = *(zone++);
		unsigned long water_mark;

		if (!z)
			break;
		if (!z->size)
			BUG();

		/*
		 * We allocate if the number of free + inactive_clean
		 * pages is above the watermark.
		 */
		switch (limit) {
			default:
			case PAGES_MIN:
				water_mark = z->pages_min;
				break;
			case PAGES_LOW:
				water_mark = z->pages_low;
				break;
			case PAGES_HIGH:
				water_mark = z->pages_high;
		}

		if (z->free_pages + z->inactive_clean_pages > water_mark) {
			struct page *page = NULL;
			/* If possible, reclaim a page directly. */
			if (direct_reclaim && z->free_pages < z->pages_min + 8)
				page = reclaim_page(z);
			/* If that fails, fall back to rmqueue. */
			if (!page)
				page = rmqueue(z, order);
			if (page)
				return page;
		}
	}

	/* Found nothing. */
	return NULL;
}


/*
 * This is the 'heart' of the zoned buddy allocator:
 */
struct page * __alloc_pages(zonelist_t *zonelist, unsigned long order)
{
	zone_t **zone;
	int direct_reclaim = 0;
	unsigned int gfp_mask = zonelist->gfp_mask;
	struct page * page;

	/*
	 * Allocations put pressure on the VM subsystem.
	 */
	memory_pressure++;

	/*
	 * (If anyone calls gfp from interrupts nonatomically then it
	 * will sooner or later tripped up by a schedule().)
	 *
	 * We are falling back to lower-level zones if allocation
	 * in a higher zone fails.
	 */

	/*
	 * Can we take pages directly from the inactive_clean
	 * list?
	 */
	if (order == 0 && (gfp_mask & __GFP_WAIT) &&
			!(current->flags & PF_MEMALLOC))
		direct_reclaim = 1;

	/*
	 * If we are about to get low on free pages and we also have
	 * an inactive page shortage, wake up kswapd.
	 */
	if (inactive_shortage() > inactive_target / 2 && free_shortage())
		wakeup_kswapd(0);
	/*
	 * If we are about to get low on free pages and cleaning
	 * the inactive_dirty pages would fix the situation,
	 * wake up bdflush.
	 */
	else if (free_shortage() && nr_inactive_dirty_pages > free_shortage()
			&& nr_inactive_dirty_pages >= freepages.high)
		wakeup_bdflush(0);

try_again:
	/*
	 * First, see if we have any zones with lots of free memory.
	 *
	 * We allocate free memory first because it doesn't contain
	 * any data ... DUH!
	 */
	zone = zonelist->zones;
	for (;;) {
		zone_t *z = *(zone++);
		if (!z)
			break;
		if (!z->size)
			BUG();

		if (z->free_pages >= z->pages_low) {
			page = rmqueue(z, order);
			if (page)
				return page;
		} else if (z->free_pages < z->pages_min &&
					waitqueue_active(&kreclaimd_wait)) {
				wake_up_interruptible(&kreclaimd_wait);
		}
	}

	/*
	 * Try to allocate a page from a zone with a HIGH
	 * amount of free + inactive_clean pages.
	 *
	 * If there is a lot of activity, inactive_target
	 * will be high and we'll have a good chance of
	 * finding a page using the HIGH limit.
	 */
	page = __alloc_pages_limit(zonelist, order, PAGES_HIGH, direct_reclaim);
	if (page)
		return page;

	/*
	 * Then try to allocate a page from a zone with more
	 * than zone->pages_low free + inactive_clean pages.
	 *
	 * When the working set is very large and VM activity
	 * is low, we're most likely to have our allocation
	 * succeed here.
	 */
	page = __alloc_pages_limit(zonelist, order, PAGES_LOW, direct_reclaim);
	if (page)
		return page;

	/*
	 * OK, none of the zones on our zonelist has lots
	 * of pages free.
	 *
	 * We wake up kswapd, in the hope that kswapd will
	 * resolve this situation before memory gets tight.
	 *
	 * We also yield the CPU, because that:
	 * - gives kswapd a chance to do something
	 * - slows down allocations, in particular the
	 *   allocations from the fast allocator that's
	 *   causing the problems ...
	 * - ... which minimises the impact the "bad guys"
	 *   have on the rest of the system
	 * - if we don't have __GFP_IO set, kswapd may be
	 *   able to free some memory we can't free ourselves
	 */
	wakeup_kswapd(0);
	if (gfp_mask & __GFP_WAIT) {
		__set_current_state(TASK_RUNNING);
		current->policy |= SCHED_YIELD;
		schedule();
	}

	/*
	 * After waking up kswapd, we try to allocate a page
	 * from any zone which isn't critical yet.
	 *
	 * Kswapd should, in most situations, bring the situation
	 * back to normal in no time.
	 */
	page = __alloc_pages_limit(zonelist, order, PAGES_MIN, direct_reclaim);
	if (page)
		return page;

	/*
	 * Damn, we didn't succeed.
	 *
	 * This can be due to 2 reasons:
	 * - we're doing a higher-order allocation
	 * 	--> move pages to the free list until we succeed
	 * - we're /really/ tight on memory
	 * 	--> wait on the kswapd waitqueue until memory is freed
	 */
	if (!(current->flags & PF_MEMALLOC)) {
		/*
		 * Are we dealing with a higher order allocation?
		 *
		 * Move pages from the inactive_clean to the free list
		 * in the hope of creating a large, physically contiguous
		 * piece of free memory.
		 */
		if (order > 0 && (gfp_mask & __GFP_WAIT)) {
			zone = zonelist->zones;
			/* First, clean some dirty pages. */
			current->flags |= PF_MEMALLOC;
			page_launder(gfp_mask, 1);
			current->flags &= ~PF_MEMALLOC;
			for (;;) {
				zone_t *z = *(zone++);
				if (!z)
					break;
				if (!z->size)
					continue;
				while (z->inactive_clean_pages) {
					struct page * page;
					/* Move one page to the free list. */
					page = reclaim_page(z);
					if (!page)
						break;
					__free_page(page);
					/* Try if the allocation succeeds. */
					page = rmqueue(z, order);
					if (page)
						return page;
				}
			}
		}
		/*
		 * When we arrive here, we are really tight on memory.
		 *
		 * We wake up kswapd and sleep until kswapd wakes us
		 * up again. After that we loop back to the start.
		 *
		 * We have to do this because something else might eat
		 * the memory kswapd frees for us and we need to be
		 * reliable. Note that we don't loop back for higher
		 * order allocations since it is possible that kswapd
		 * simply cannot free a large enough contiguous area
		 * of memory *ever*.
		 */
		if ((gfp_mask & (__GFP_WAIT|__GFP_IO)) == (__GFP_WAIT|__GFP_IO)) {
			wakeup_kswapd(1);
			memory_pressure++;
			if (!order)
				goto try_again;
		/*
		 * If __GFP_IO isn't set, we can't wait on kswapd because
		 * kswapd just might need some IO locks /we/ are holding ...
		 *
		 * SUBTLE: The scheduling point above makes sure that
		 * kswapd does get the chance to free memory we can't
		 * free ourselves...
		 */
		} else if (gfp_mask & __GFP_WAIT) {
			try_to_free_pages(gfp_mask);
			memory_pressure++;
			if (!order)
				goto try_again;
		}

	}

	/*
	 * Final phase: allocate anything we can!
	 *
	 * Higher order allocations, GFP_ATOMIC allocations and
	 * recursive allocations (PF_MEMALLOC) end up here.
	 *
	 * Only recursive allocations can use the very last pages
	 * in the system, otherwise it would be just too easy to
	 * deadlock the system...
	 */
	zone = zonelist->zones;
	for (;;) {
		zone_t *z = *(zone++);
		struct page * page = NULL;
		if (!z)
			break;
		if (!z->size)
			BUG();

		/*
		 * SUBTLE: direct_reclaim is only possible if the task
		 * becomes PF_MEMALLOC while looping above. This will
		 * happen when the OOM killer selects this task for
		 * instant execution...
		 */
		if (direct_reclaim) {
			page = reclaim_page(z);
			if (page)
				return page;
		}

		/* XXX: is pages_min/4 a good amount to reserve for this? */
		if (z->free_pages < z->pages_min / 4 &&
				!(current->flags & PF_MEMALLOC))
			continue;
		page = rmqueue(z, order);
		if (page)
			return page;
	}

	/* No luck.. */
	printk(KERN_ERR "__alloc_pages: %lu-order allocation failed.\n", order);
	return NULL;
}

/*
 * Common helper functions.
 */
unsigned long __get_free_pages(int gfp_mask, unsigned long order)
{
	struct page * page;

	page = alloc_pages(gfp_mask, order);
	if (!page)
		return 0;
	return (unsigned long) page_address(page);
}

unsigned long get_zeroed_page(int gfp_mask)
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

void __free_pages(struct page *page, unsigned long order)
{
	if (!PageReserved(page) && put_page_testzero(page))
		__free_pages_ok(page, order);
}

void free_pages(unsigned long addr, unsigned long order)
{
	struct page *fpage;

#ifdef CONFIG_DISCONTIGMEM
	if (addr == 0) return;
#endif
	fpage = virt_to_page(addr);
	if (VALID_PAGE(fpage))
		__free_pages(fpage, order);
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
 * Total amount of inactive_clean (allocatable) RAM:
 */
unsigned int nr_inactive_clean_pages (void)
{
	unsigned int sum;
	zone_t *zone;
	pg_data_t *pgdat = pgdat_list;

	sum = 0;
	while (pgdat) {
		for (zone = pgdat->node_zones; zone < pgdat->node_zones + MAX_NR_ZONES; zone++)
			sum += zone->inactive_clean_pages;
		pgdat = pgdat->node_next;
	}
	return sum;
}

/*
 * Amount of free RAM allocatable as buffer memory:
 */
unsigned int nr_free_buffer_pages (void)
{
	unsigned int sum;

	sum = nr_free_pages();
	sum += nr_inactive_clean_pages();
	sum += nr_inactive_dirty_pages;

	/*
	 * Keep our write behind queue filled, even if
	 * kswapd lags a bit right now.
	 */
	if (sum < freepages.high + inactive_target)
		sum = freepages.high + inactive_target;
	/*
	 * We don't want dirty page writebehind to put too
	 * much pressure on the working set, but we want it
	 * to be possible to have some dirty pages in the
	 * working set without upsetting the writebehind logic.
	 */
	sum += nr_active_pages >> 4;

	return sum;
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
 	unsigned long order;
	unsigned type;

	printk("Free pages:      %6dkB (%6dkB HighMem)\n",
		nr_free_pages() << (PAGE_SHIFT-10),
		nr_free_highpages() << (PAGE_SHIFT-10));

	printk("( Active: %d, inactive_dirty: %d, inactive_clean: %d, free: %d (%d %d %d) )\n",
		nr_active_pages,
		nr_inactive_dirty_pages,
		nr_inactive_clean_pages(),
		nr_free_pages(),
		freepages.min,
		freepages.low,
		freepages.high);

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

	for (i = 0; i < NR_GFPINDEX; i++) {
		zonelist_t *zonelist;
		zone_t *zone;

		zonelist = pgdat->node_zonelists + i;
		memset(zonelist, 0, sizeof(*zonelist));

		zonelist->gfp_mask = i;
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
	unsigned int cumulative = 0;

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

	memlist_init(&active_list);
	memlist_init(&inactive_dirty_list);

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
		zone->inactive_clean_pages = 0;
		zone->inactive_dirty_pages = 0;
		memlist_init(&zone->inactive_clean_list);
		if (!size)
			continue;

		zone->offset = offset;
		cumulative += size;
		mask = (realsize / zone_balance_ratio[j]);
		if (mask < zone_balance_min[j])
			mask = zone_balance_min[j];
		else if (mask > zone_balance_max[j])
			mask = zone_balance_max[j];
		zone->pages_min = mask;
		zone->pages_low = mask*2;
		zone->pages_high = mask*3;
		/*
		 * Add these free targets to the global free target;
		 * we have to be SURE that freepages.high is higher
		 * than SUM [zone->pages_min] for all zones, otherwise
		 * we may have bad bad problems.
		 *
		 * This means we cannot make the freepages array writable
		 * in /proc, but have to add a separate extra_free_target
		 * for people who require it to catch load spikes in eg.
		 * gigabit ethernet routing...
		 */
		freepages.min += mask;
		freepages.low += mask*2;
		freepages.high += mask*3;
		zone->zone_mem_map = mem_map + offset;
		zone->zone_start_mapnr = offset;
		zone->zone_start_paddr = zone_start_paddr;

		for (i = 0; i < size; i++) {
			struct page *page = mem_map + offset + i;
			page->zone = zone;
			if (j != ZONE_HIGHMEM) {
				page->virtual = __va(zone_start_paddr);
				zone_start_paddr += PAGE_SIZE;
			}
		}

		offset += size;
		mask = -1;
		for (i = 0; i < MAX_ORDER; i++) {
			unsigned long bitmap_size;

			memlist_init(&zone->free_area[i].free_list);
			mask += mask;
			size = (size + ~mask) & mask;
			bitmap_size = size >> i;
			bitmap_size = (bitmap_size + 7) >> 3;
			bitmap_size = LONG_ALIGN(bitmap_size);
			zone->free_area[i].map = 
			  (unsigned int *) alloc_bootmem_node(pgdat, bitmap_size);
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
