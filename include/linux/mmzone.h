#ifndef _LINUX_MMZONE_H
#define _LINUX_MMZONE_H

#ifdef __KERNEL__
#ifndef __ASSEMBLY__

#include <linux/config.h>
#include <linux/spinlock.h>
#include <linux/list.h>
#include <linux/wait.h>
#include <linux/cache.h>
#include <linux/threads.h>
#include <linux/numa.h>
#include <asm/atomic.h>

/* Free memory management - zoned buddy allocator.  */
#ifndef CONFIG_FORCE_MAX_ZONEORDER
#define MAX_ORDER 11
#else
#define MAX_ORDER CONFIG_FORCE_MAX_ZONEORDER
#endif

struct free_area {
	struct list_head	free_list;
	unsigned long		*map;
};

struct pglist_data;

/*
 * zone->lock and zone->lru_lock are two of the hottest locks in the kernel.
 * So add a wild amount of padding here to ensure that they fall into separate
 * cachelines.  There are very few zone structures in the machine, so space
 * consumption is not a concern here.
 */
#if defined(CONFIG_SMP)
struct zone_padding {
	int x;
} ____cacheline_maxaligned_in_smp;
#define ZONE_PADDING(name)	struct zone_padding name;
#else
#define ZONE_PADDING(name)
#endif

struct per_cpu_pages {
	int count;		/* number of pages in the list */
	int low;		/* low watermark, refill needed */
	int high;		/* high watermark, emptying needed */
	int batch;		/* chunk size for buddy add/remove */
	struct list_head list;	/* the list of pages */
};

struct per_cpu_pageset {
	struct per_cpu_pages pcp[2];	/* 0: hot.  1: cold */
} ____cacheline_aligned_in_smp;

/*
 * On machines where it is needed (eg PCs) we divide physical memory
 * into multiple physical zones. On a PC we have 3 zones:
 *
 * ZONE_DMA	  < 16 MB	ISA DMA capable memory
 * ZONE_NORMAL	16-896 MB	direct mapped by the kernel
 * ZONE_HIGHMEM	 > 896 MB	only page cache and user processes
 */

struct zone {
	/*
	 * Commonly accessed fields:
	 */
	spinlock_t		lock;
	unsigned long		free_pages;
	unsigned long		pages_min, pages_low, pages_high;

	ZONE_PADDING(_pad1_)

	spinlock_t		lru_lock;	
	struct list_head	active_list;
	struct list_head	inactive_list;
	atomic_t		refill_counter;
	unsigned long		nr_active;
	unsigned long		nr_inactive;
	int			all_unreclaimable; /* All pages pinned */
	unsigned long		pages_scanned;	   /* since last reclaim */

	ZONE_PADDING(_pad2_)

	/*
	 * prev_priority holds the scanning priority for this zone.  It is
	 * defined as the scanning priority at which we achieved our reclaim
	 * target at the previous try_to_free_pages() or balance_pgdat()
	 * invokation.
	 *
	 * We use prev_priority as a measure of how much stress page reclaim is
	 * under - it drives the swappiness decision: whether to unmap mapped
	 * pages.
	 *
	 * temp_priority is used to remember the scanning priority at which
	 * this zone was successfully refilled to free_pages == pages_high.
	 *
	 * Access to both these fields is quite racy even on uniprocessor.  But
	 * it is expected to average out OK.
	 */
	int temp_priority;
	int prev_priority;

	/*
	 * free areas of different sizes
	 */
	struct free_area	free_area[MAX_ORDER];

	/*
	 * wait_table		-- the array holding the hash table
	 * wait_table_size	-- the size of the hash table array
	 * wait_table_bits	-- wait_table_size == (1 << wait_table_bits)
	 *
	 * The purpose of all these is to keep track of the people
	 * waiting for a page to become available and make them
	 * runnable again when possible. The trouble is that this
	 * consumes a lot of space, especially when so few things
	 * wait on pages at a given time. So instead of using
	 * per-page waitqueues, we use a waitqueue hash table.
	 *
	 * The bucket discipline is to sleep on the same queue when
	 * colliding and wake all in that wait queue when removing.
	 * When something wakes, it must check to be sure its page is
	 * truly available, a la thundering herd. The cost of a
	 * collision is great, but given the expected load of the
	 * table, they should be so rare as to be outweighed by the
	 * benefits from the saved space.
	 *
	 * __wait_on_page_locked() and unlock_page() in mm/filemap.c, are the
	 * primary users of these fields, and in mm/page_alloc.c
	 * free_area_init_core() performs the initialization of them.
	 */
	wait_queue_head_t	* wait_table;
	unsigned long		wait_table_size;
	unsigned long		wait_table_bits;

	ZONE_PADDING(_pad3_)

	struct per_cpu_pageset	pageset[NR_CPUS];

	/*
	 * Discontig memory support fields.
	 */
	struct pglist_data	*zone_pgdat;
	struct page		*zone_mem_map;
	/* zone_start_pfn == zone_start_paddr >> PAGE_SHIFT */
	unsigned long		zone_start_pfn;

	/*
	 * rarely used fields:
	 */
	char			*name;
	unsigned long		spanned_pages;	/* total size, including holes */
	unsigned long		present_pages;	/* amount of memory (excluding holes) */
} ____cacheline_maxaligned_in_smp;

#define ZONE_DMA		0
#define ZONE_NORMAL		1
#define ZONE_HIGHMEM		2
#define MAX_NR_ZONES		3
#define GFP_ZONEMASK	0x03

/*
 * One allocation request operates on a zonelist. A zonelist
 * is a list of zones, the first one is the 'goal' of the
 * allocation, the other zones are fallback zones, in decreasing
 * priority.
 *
 * Right now a zonelist takes up less than a cacheline. We never
 * modify it apart from boot-up, and only a few indices are used,
 * so despite the zonelist table being relatively big, the cache
 * footprint of this construct is very small.
 */
struct zonelist {
	struct zone *zones[MAX_NUMNODES * MAX_NR_ZONES + 1]; // NULL delimited
};


/*
 * The pg_data_t structure is used in machines with CONFIG_DISCONTIGMEM
 * (mostly NUMA machines?) to denote a higher-level memory zone than the
 * zone denotes.
 *
 * On NUMA machines, each NUMA node would have a pg_data_t to describe
 * it's memory layout.
 *
 * Memory statistics and page replacement data structures are maintained on a
 * per-zone basis.
 */
struct bootmem_data;
typedef struct pglist_data {
	struct zone node_zones[MAX_NR_ZONES];
	struct zonelist node_zonelists[MAX_NR_ZONES];
	int nr_zones;
	struct page *node_mem_map;
	unsigned long *valid_addr_bitmap;
	struct bootmem_data *bdata;
	unsigned long node_start_pfn;
	unsigned long node_present_pages; /* total number of physical pages */
	unsigned long node_spanned_pages; /* total size of physical page
					     range, including holes */
	int node_id;
	struct pglist_data *pgdat_next;
	wait_queue_head_t       kswapd_wait;
} pg_data_t;

#define node_present_pages(nid)	(NODE_DATA(nid)->node_present_pages)
#define node_spanned_pages(nid)	(NODE_DATA(nid)->node_spanned_pages)

extern int numnodes;
extern struct pglist_data *pgdat_list;

void get_zone_counts(unsigned long *active, unsigned long *inactive,
			unsigned long *free);
void build_all_zonelists(void);
void wakeup_kswapd(struct zone *zone);

/**
 * for_each_pgdat - helper macro to iterate over all nodes
 * @pgdat - pointer to a pg_data_t variable
 *
 * Meant to help with common loops of the form
 * pgdat = pgdat_list;
 * while(pgdat) {
 * 	...
 * 	pgdat = pgdat->pgdat_next;
 * }
 */
#define for_each_pgdat(pgdat) \
	for (pgdat = pgdat_list; pgdat; pgdat = pgdat->pgdat_next)

/*
 * next_zone - helper magic for for_each_zone()
 * Thanks to William Lee Irwin III for this piece of ingenuity.
 */
static inline struct zone *next_zone(struct zone *zone)
{
	pg_data_t *pgdat = zone->zone_pgdat;

	if (zone - pgdat->node_zones < MAX_NR_ZONES - 1)
		zone++;
	else if (pgdat->pgdat_next) {
		pgdat = pgdat->pgdat_next;
		zone = pgdat->node_zones;
	} else
		zone = NULL;

	return zone;
}

/**
 * for_each_zone - helper macro to iterate over all memory zones
 * @zone - pointer to struct zone variable
 *
 * The user only needs to declare the zone variable, for_each_zone
 * fills it in. This basically means for_each_zone() is an
 * easier to read version of this piece of code:
 *
 * for (pgdat = pgdat_list; pgdat; pgdat = pgdat->node_next)
 * 	for (i = 0; i < MAX_NR_ZONES; ++i) {
 * 		struct zone * z = pgdat->node_zones + i;
 * 		...
 * 	}
 * }
 */
#define for_each_zone(zone) \
	for (zone = pgdat_list->node_zones; zone; zone = next_zone(zone))

/**
 * is_highmem - helper function to quickly check if a struct zone is a 
 *              highmem zone or not.  This is an attempt to keep references
 *              to ZONE_{DMA/NORMAL/HIGHMEM/etc} in general code to a minimum.
 * @zone - pointer to struct zone variable
 */
static inline int is_highmem(struct zone *zone)
{
	return (zone - zone->zone_pgdat->node_zones == ZONE_HIGHMEM);
}

/* These two functions are used to setup the per zone pages min values */
struct ctl_table;
struct file;
int min_free_kbytes_sysctl_handler(struct ctl_table *, int, struct file *, 
					  void *, size_t *);
extern void setup_per_zone_pages_min(void);


#ifdef CONFIG_NUMA
#define MAX_NR_MEMBLKS	BITS_PER_LONG /* Max number of Memory Blocks */
#else /* !CONFIG_NUMA */
#define MAX_NR_MEMBLKS	1
#endif /* CONFIG_NUMA */

#include <linux/topology.h>
/* Returns the number of the current Node. */
#define numa_node_id()		(cpu_to_node(smp_processor_id()))

#ifndef CONFIG_DISCONTIGMEM

extern struct pglist_data contig_page_data;
#define NODE_DATA(nid)		(&contig_page_data)
#define NODE_MEM_MAP(nid)	mem_map
#define MAX_NODES_SHIFT		0

#else /* CONFIG_DISCONTIGMEM */

#include <asm/mmzone.h>

#if BITS_PER_LONG == 32
/*
 * with 32 bit flags field, page->zone is currently 8 bits.
 * there are 3 zones (2 bits) and this leaves 8-2=6 bits for nodes.
 */
#define MAX_NODES_SHIFT		6
#elif BITS_PER_LONG == 64
/*
 * with 64 bit flags field, there's plenty of room.
 */
#define MAX_NODES_SHIFT		10
#endif

#endif /* !CONFIG_DISCONTIGMEM */

#if NODES_SHIFT > MAX_NODES_SHIFT
#error NODES_SHIFT > MAX_NODES_SHIFT
#endif

extern DECLARE_BITMAP(node_online_map, MAX_NUMNODES);
extern DECLARE_BITMAP(memblk_online_map, MAX_NR_MEMBLKS);

#if defined(CONFIG_DISCONTIGMEM) || defined(CONFIG_NUMA)

#define node_online(node)	test_bit(node, node_online_map)
#define node_set_online(node)	set_bit(node, node_online_map)
#define node_set_offline(node)	clear_bit(node, node_online_map)
static inline unsigned int num_online_nodes(void)
{
	int i, num = 0;

	for(i = 0; i < MAX_NUMNODES; i++){
		if (node_online(i))
			num++;
	}
	return num;
}

#define memblk_online(memblk)		test_bit(memblk, memblk_online_map)
#define memblk_set_online(memblk)	set_bit(memblk, memblk_online_map)
#define memblk_set_offline(memblk)	clear_bit(memblk, memblk_online_map)
static inline unsigned int num_online_memblks(void)
{
	int i, num = 0;

	for(i = 0; i < MAX_NR_MEMBLKS; i++){
		if (memblk_online(i))
			num++;
	}
	return num;
}

#else /* !CONFIG_DISCONTIGMEM && !CONFIG_NUMA */

#define node_online(node) \
	({ BUG_ON((node) != 0); test_bit(node, node_online_map); })
#define node_set_online(node) \
	({ BUG_ON((node) != 0); set_bit(node, node_online_map); })
#define node_set_offline(node) \
	({ BUG_ON((node) != 0); clear_bit(node, node_online_map); })
#define num_online_nodes()	1

#define memblk_online(memblk) \
	({ BUG_ON((memblk) != 0); test_bit(memblk, memblk_online_map); })
#define memblk_set_online(memblk) \
	({ BUG_ON((memblk) != 0); set_bit(memblk, memblk_online_map); })
#define memblk_set_offline(memblk) \
	({ BUG_ON((memblk) != 0); clear_bit(memblk, memblk_online_map); })
#define num_online_memblks()		1

#endif /* CONFIG_DISCONTIGMEM || CONFIG_NUMA */
#endif /* !__ASSEMBLY__ */
#endif /* __KERNEL__ */
#endif /* _LINUX_MMZONE_H */
