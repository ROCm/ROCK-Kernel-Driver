#ifndef _LINUX_SWAP_H
#define _LINUX_SWAP_H

#include <linux/spinlock.h>
#include <asm/page.h>

#define SWAP_FLAG_PREFER	0x8000	/* set if swap priority specified */
#define SWAP_FLAG_PRIO_MASK	0x7fff
#define SWAP_FLAG_PRIO_SHIFT	0

#define MAX_SWAPFILES 8

union swap_header {
	struct 
	{
		char reserved[PAGE_SIZE - 10];
		char magic[10];
	} magic;
	struct 
	{
		char	     bootbits[1024];	/* Space for disklabel etc. */
		unsigned int version;
		unsigned int last_page;
		unsigned int nr_badpages;
		unsigned int padding[125];
		unsigned int badpages[1];
	} info;
};

#ifdef __KERNEL__

/*
 * Max bad pages in the new format..
 */
#define __swapoffset(x) ((unsigned long)&((union swap_header *)0)->x)
#define MAX_SWAP_BADPAGES \
	((__swapoffset(magic.magic) - __swapoffset(info.badpages)) / sizeof(int))

#include <asm/atomic.h>

#define SWP_USED	1
#define SWP_WRITEOK	3

#define SWAP_CLUSTER_MAX 32

#define SWAP_MAP_MAX	0x7fff
#define SWAP_MAP_BAD	0x8000

struct swap_info_struct {
	unsigned int flags;
	kdev_t swap_device;
	spinlock_t sdev_lock;
	struct dentry * swap_file;
	struct vfsmount *swap_vfsmnt;
	unsigned short * swap_map;
	unsigned int lowest_bit;
	unsigned int highest_bit;
	unsigned int cluster_next;
	unsigned int cluster_nr;
	int prio;			/* swap priority */
	int pages;
	unsigned long max;
	int next;			/* next entry on swap list */
};

extern int nr_swap_pages;
FASTCALL(unsigned int nr_free_pages(void));
FASTCALL(unsigned int nr_inactive_clean_pages(void));
FASTCALL(unsigned int nr_free_buffer_pages(void));
extern int nr_active_pages;
extern int nr_inactive_dirty_pages;
extern atomic_t nr_async_pages;
extern struct address_space swapper_space;
extern atomic_t page_cache_size;
extern atomic_t buffermem_pages;
extern spinlock_t pagecache_lock;
extern void __remove_inode_page(struct page *);

/* Incomplete types for prototype declarations: */
struct task_struct;
struct vm_area_struct;
struct sysinfo;

struct zone_t;

/* linux/mm/swap.c */
extern int memory_pressure;
extern void age_page_up(struct page *);
extern void age_page_up_nolock(struct page *);
extern void age_page_down(struct page *);
extern void age_page_down_nolock(struct page *);
extern void age_page_down_ageonly(struct page *);
extern void deactivate_page(struct page *);
extern void deactivate_page_nolock(struct page *);
extern void activate_page(struct page *);
extern void activate_page_nolock(struct page *);
extern void lru_cache_add(struct page *);
extern void __lru_cache_del(struct page *);
extern void lru_cache_del(struct page *);
extern void recalculate_vm_stats(void);
extern void swap_setup(void);

/* linux/mm/vmscan.c */
extern struct page * reclaim_page(zone_t *);
extern wait_queue_head_t kswapd_wait;
extern wait_queue_head_t kreclaimd_wait;
extern int page_launder(int, int);
extern int free_shortage(void);
extern int inactive_shortage(void);
extern void wakeup_kswapd(int);
extern int try_to_free_pages(unsigned int gfp_mask);

/* linux/mm/page_io.c */
extern void rw_swap_page(int, struct page *, int);
extern void rw_swap_page_nolock(int, swp_entry_t, char *, int);

/* linux/mm/page_alloc.c */

/* linux/mm/swap_state.c */
extern void show_swap_cache_info(void);
extern void add_to_swap_cache(struct page *, swp_entry_t);
extern int swap_check_entry(unsigned long);
extern struct page * lookup_swap_cache(swp_entry_t);
extern struct page * read_swap_cache_async(swp_entry_t, int);
#define read_swap_cache(entry) read_swap_cache_async(entry, 1);

/* linux/mm/oom_kill.c */
extern int out_of_memory(void);
extern void oom_kill(void);

/*
 * Make these inline later once they are working properly.
 */
extern void __delete_from_swap_cache(struct page *page);
extern void delete_from_swap_cache(struct page *page);
extern void delete_from_swap_cache_nolock(struct page *page);
extern void free_page_and_swap_cache(struct page *page);

/* linux/mm/swapfile.c */
extern unsigned int nr_swapfiles;
extern struct swap_info_struct swap_info[];
extern int is_swap_partition(kdev_t);
extern void si_swapinfo(struct sysinfo *);
extern swp_entry_t __get_swap_page(unsigned short);
extern void get_swaphandle_info(swp_entry_t, unsigned long *, kdev_t *, 
					struct inode **);
extern int swap_duplicate(swp_entry_t);
extern int swap_count(struct page *);
extern int valid_swaphandles(swp_entry_t, unsigned long *);
#define get_swap_page() __get_swap_page(1)
extern void __swap_free(swp_entry_t, unsigned short);
#define swap_free(entry) __swap_free((entry), 1)
struct swap_list_t {
	int head;	/* head of priority-ordered swapfile list */
	int next;	/* swapfile to be used next */
};
extern struct swap_list_t swap_list;
asmlinkage long sys_swapoff(const char *);
asmlinkage long sys_swapon(const char *, int);

#define SWAP_CACHE_INFO

#ifdef SWAP_CACHE_INFO
extern unsigned long swap_cache_add_total;
extern unsigned long swap_cache_del_total;
extern unsigned long swap_cache_find_total;
extern unsigned long swap_cache_find_success;
#endif

/*
 * Work out if there are any other processes sharing this page, ignoring
 * any page reference coming from the swap cache, or from outstanding
 * swap IO on this page.  (The page cache _does_ count as another valid
 * reference to the page, however.)
 */
static inline int is_page_shared(struct page *page)
{
	unsigned int count;
	if (PageReserved(page))
		return 1;
	count = page_count(page);
	if (PageSwapCache(page))
		count += swap_count(page) - 2 - !!page->buffers;
	return  count > 1;
}

extern spinlock_t pagemap_lru_lock;

/*
 * Page aging defines.
 * Since we do exponential decay of the page age, we
 * can chose a fairly large maximum.
 */
#define PAGE_AGE_START 2
#define PAGE_AGE_ADV 3
#define PAGE_AGE_MAX 64

/*
 * List add/del helper macros. These must be called
 * with the pagemap_lru_lock held!
 */
#define DEBUG_ADD_PAGE \
	if (PageActive(page) || PageInactiveDirty(page) || \
					PageInactiveClean(page)) BUG();

#define ZERO_PAGE_BUG \
	if (page_count(page) == 0) BUG();

#define add_page_to_active_list(page) { \
	DEBUG_ADD_PAGE \
	ZERO_PAGE_BUG \
	SetPageActive(page); \
	list_add(&(page)->lru, &active_list); \
	nr_active_pages++; \
}

#define add_page_to_inactive_dirty_list(page) { \
	DEBUG_ADD_PAGE \
	ZERO_PAGE_BUG \
	SetPageInactiveDirty(page); \
	list_add(&(page)->lru, &inactive_dirty_list); \
	nr_inactive_dirty_pages++; \
	page->zone->inactive_dirty_pages++; \
}

#define add_page_to_inactive_clean_list(page) { \
	DEBUG_ADD_PAGE \
	ZERO_PAGE_BUG \
	SetPageInactiveClean(page); \
	list_add(&(page)->lru, &page->zone->inactive_clean_list); \
	page->zone->inactive_clean_pages++; \
}

#define del_page_from_active_list(page) { \
	list_del(&(page)->lru); \
	ClearPageActive(page); \
	nr_active_pages--; \
	DEBUG_ADD_PAGE \
	ZERO_PAGE_BUG \
}

#define del_page_from_inactive_dirty_list(page) { \
	list_del(&(page)->lru); \
	ClearPageInactiveDirty(page); \
	nr_inactive_dirty_pages--; \
	page->zone->inactive_dirty_pages--; \
	DEBUG_ADD_PAGE \
	ZERO_PAGE_BUG \
}

#define del_page_from_inactive_clean_list(page) { \
	list_del(&(page)->lru); \
	ClearPageInactiveClean(page); \
	page->zone->inactive_clean_pages--; \
	DEBUG_ADD_PAGE \
	ZERO_PAGE_BUG \
}

/*
 * In mm/swap.c::recalculate_vm_stats(), we substract
 * inactive_target from memory_pressure every second.
 * This means that memory_pressure is smoothed over
 * 64 (1 << INACTIVE_SHIFT) seconds.
 */
#define INACTIVE_SHIFT 6
#define inactive_min(a,b) ((a) < (b) ? (a) : (b))
#define inactive_target inactive_min((memory_pressure >> INACTIVE_SHIFT), \
		(num_physpages / 4))

/*
 * Ugly ugly ugly HACK to make sure the inactive lists
 * don't fill up with unfreeable ramdisk pages. We really
 * want to fix the ramdisk driver to mark its pages as
 * unfreeable instead of using dirty buffer magic, but the
 * next code-change time is when 2.5 is forked...
 */
#ifndef _LINUX_KDEV_T_H
#include <linux/kdev_t.h>
#endif
#ifndef _LINUX_MAJOR_H
#include <linux/major.h>
#endif

#define page_ramdisk(page) \
	(page->buffers && (MAJOR(page->buffers->b_dev) == RAMDISK_MAJOR))

extern spinlock_t swaplock;

#define swap_list_lock()	spin_lock(&swaplock)
#define swap_list_unlock()	spin_unlock(&swaplock)
#define swap_device_lock(p)	spin_lock(&p->sdev_lock)
#define swap_device_unlock(p)	spin_unlock(&p->sdev_lock)

extern void shmem_unuse(swp_entry_t entry, struct page *page);

#endif /* __KERNEL__*/

#endif /* _LINUX_SWAP_H */
