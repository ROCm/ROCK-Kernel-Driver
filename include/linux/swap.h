#ifndef _LINUX_SWAP_H
#define _LINUX_SWAP_H

#include <linux/spinlock.h>
#include <linux/kdev_t.h>
#include <linux/linkage.h>
#include <linux/mmzone.h>
#include <linux/list.h>
#include <asm/page.h>

#define SWAP_FLAG_PREFER	0x8000	/* set if swap priority specified */
#define SWAP_FLAG_PRIO_MASK	0x7fff
#define SWAP_FLAG_PRIO_SHIFT	0

/*
 * MAX_SWAPFILES defines the maximum number of swaptypes: things which can
 * be swapped to.  The swap type and the offset into that swap type are
 * encoded into pte's and into pgoff_t's in the swapcache.  Using five bits
 * for the type means that the maximum number of swapcache pages is 27 bits
 * on 32-bit-pgoff_t architectures.  And that assumes that the architecture packs
 * the type/offset into the pte as 5/27 as well.
 */
#define MAX_SWAPFILES_SHIFT	5
#define MAX_SWAPFILES		(1 << MAX_SWAPFILES_SHIFT)

/*
 * Magic header for a swap area. The first part of the union is
 * what the swap magic looks like for the old (limited to 128MB)
 * swap area format, the second part of the union adds - in the
 * old reserved area - some extra information. Note that the first
 * kilobyte is reserved for boot loader or disk label stuff...
 *
 * Having the magic at the end of the PAGE_SIZE makes detecting swap
 * areas somewhat tricky on machines that support multiple page sizes.
 * For 2.5 we'll probably want to move the magic to just beyond the
 * bootbits...
 */
union swap_header {
	struct 
	{
		char reserved[PAGE_SIZE - 10];
		char magic[10];			/* SWAP-SPACE or SWAPSPACE2 */
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

 /* A swap entry has to fit into a "unsigned long", as
  * the entry is hidden in the "index" field of the
  * swapper address space.
  */
typedef struct {
	unsigned long val;
} swp_entry_t;

#ifdef __KERNEL__

/*
 * A swap extent maps a range of a swapfile's PAGE_SIZE pages onto a range of
 * disk blocks.  A list of swap extents maps the entire swapfile.  (Where the
 * term `swapfile' refers to either a blockdevice or an IS_REG file.  Apart
 * from setup, they're handled identically.
 *
 * We always assume that blocks are of size PAGE_SIZE.
 */
struct swap_extent {
	struct list_head list;
	pgoff_t start_page;
	pgoff_t nr_pages;
	sector_t start_block;
};

/*
 * Max bad pages in the new format..
 */
#define __swapoffset(x) ((unsigned long)&((union swap_header *)0)->x)
#define MAX_SWAP_BADPAGES \
	((__swapoffset(magic.magic) - __swapoffset(info.badpages)) / sizeof(int))

#include <asm/atomic.h>

enum {
	SWP_USED	= (1 << 0),	/* is slot in swap_info[] used? */
	SWP_WRITEOK	= (1 << 1),	/* ok to write to this swap?	*/
	SWP_ACTIVE	= (SWP_USED | SWP_WRITEOK),
};

#define SWAP_CLUSTER_MAX 32

#define SWAP_MAP_MAX	0x7fff
#define SWAP_MAP_BAD	0x8000

/*
 * The in-memory structure used to track swap areas.
 * extent_list.prev points at the lowest-index extent.  That list is
 * sorted.
 */
struct swap_info_struct {
	unsigned int flags;
	spinlock_t sdev_lock;
	struct file *swap_file;
	struct block_device *bdev;
	struct list_head extent_list;
	int nr_extents;
	struct swap_extent *curr_swap_extent;
	unsigned old_block_size;
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

struct inode;
extern int nr_swap_pages;

/* Swap 50% full? Release swapcache more aggressively.. */
#define vm_swap_full() (nr_swap_pages*2 < total_swap_pages)

extern unsigned long totalram_pages;
extern unsigned long totalhigh_pages;
extern unsigned int nr_free_pages(void);
extern unsigned int nr_free_buffer_pages(void);
extern unsigned int nr_free_pagecache_pages(void);

/* Incomplete types for prototype declarations: */
struct task_struct;
struct vm_area_struct;
struct sysinfo;
struct address_space;
struct zone_t;

/* linux/mm/rmap.c */
extern int FASTCALL(page_referenced(struct page *));
extern void FASTCALL(page_add_rmap(struct page *, pte_t *));
extern void FASTCALL(page_remove_rmap(struct page *, pte_t *));
extern int FASTCALL(try_to_unmap(struct page *));
extern int FASTCALL(page_over_rsslimit(struct page *));

/* return values of try_to_unmap */
#define	SWAP_SUCCESS	0
#define	SWAP_AGAIN	1
#define	SWAP_FAIL	2
#define	SWAP_ERROR	3

/* linux/mm/swap.c */
extern void FASTCALL(lru_cache_add(struct page *));
extern void FASTCALL(__lru_cache_del(struct page *));
extern void FASTCALL(lru_cache_del(struct page *));

extern void FASTCALL(activate_page(struct page *));

extern void swap_setup(void);

/* linux/mm/vmscan.c */
extern wait_queue_head_t kswapd_wait;
extern int FASTCALL(try_to_free_pages(zone_t *, unsigned int, unsigned int));

/* linux/mm/page_io.c */
int swap_readpage(struct file *file, struct page *page);
int swap_writepage(struct page *page);
int rw_swap_page_sync(int rw, swp_entry_t entry, struct page *page);

/* linux/mm/page_alloc.c */

/* linux/mm/swap_state.c */
extern void show_swap_cache_info(void);
extern int add_to_swap_cache(struct page *, swp_entry_t);
extern int add_to_swap(struct page *);
extern void __delete_from_swap_cache(struct page *page);
extern void delete_from_swap_cache(struct page *page);
extern int move_to_swap_cache(struct page *page, swp_entry_t entry);
extern int move_from_swap_cache(struct page *page, unsigned long index,
		struct address_space *mapping);
extern void free_page_and_swap_cache(struct page *page);
extern struct page * lookup_swap_cache(swp_entry_t);
extern struct page * read_swap_cache_async(swp_entry_t);

/* linux/mm/oom_kill.c */
extern void out_of_memory(void);

/* linux/mm/swapfile.c */
extern int total_swap_pages;
extern unsigned int nr_swapfiles;
extern struct swap_info_struct swap_info[];
extern void si_swapinfo(struct sysinfo *);
extern swp_entry_t get_swap_page(void);
extern int swap_duplicate(swp_entry_t);
extern int valid_swaphandles(swp_entry_t, unsigned long *);
extern void swap_free(swp_entry_t);
extern void free_swap_and_cache(swp_entry_t);
sector_t map_swap_page(struct swap_info_struct *p, pgoff_t offset);
struct swap_info_struct *get_swap_info_struct(unsigned type);

struct swap_list_t {
	int head;	/* head of priority-ordered swapfile list */
	int next;	/* swapfile to be used next */
};
extern struct swap_list_t swap_list;
asmlinkage long sys_swapoff(const char *);
asmlinkage long sys_swapon(const char *, int);

extern spinlock_t _pagemap_lru_lock;

extern void FASTCALL(mark_page_accessed(struct page *));

/*
 * List add/del helper macros. These must be called
 * with the pagemap_lru_lock held!
 */
#define DEBUG_LRU_PAGE(page)			\
do {						\
	if (!PageLRU(page))			\
		BUG();				\
	if (PageActive(page))			\
		BUG();				\
} while (0)

#define __add_page_to_active_list(page)		\
do {						\
	list_add(&(page)->lru, &active_list);	\
	inc_page_state(nr_active);		\
} while (0)

#define add_page_to_active_list(page)		\
do {						\
	DEBUG_LRU_PAGE(page);			\
	SetPageActive(page);			\
	__add_page_to_active_list(page);	\
} while (0)

#define add_page_to_inactive_list(page)		\
do {						\
	DEBUG_LRU_PAGE(page);			\
	list_add(&(page)->lru, &inactive_list);	\
	inc_page_state(nr_inactive);		\
} while (0)

#define del_page_from_active_list(page)		\
do {						\
	list_del(&(page)->lru);			\
	ClearPageActive(page);			\
	dec_page_state(nr_active);		\
} while (0)

#define del_page_from_inactive_list(page)	\
do {						\
	list_del(&(page)->lru);			\
	dec_page_state(nr_inactive);		\
} while (0)

extern spinlock_t swaplock;

#define swap_list_lock()	spin_lock(&swaplock)
#define swap_list_unlock()	spin_unlock(&swaplock)
#define swap_device_lock(p)	spin_lock(&p->sdev_lock)
#define swap_device_unlock(p)	spin_unlock(&p->sdev_lock)

extern void shmem_unuse(swp_entry_t entry, struct page *page);

#endif /* __KERNEL__*/

#endif /* _LINUX_SWAP_H */
