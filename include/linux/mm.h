#ifndef _LINUX_MM_H
#define _LINUX_MM_H

#include <linux/sched.h>
#include <linux/errno.h>

#ifdef __KERNEL__

#include <linux/config.h>
#include <linux/string.h>
#include <linux/list.h>
#include <linux/mmzone.h>

extern unsigned long max_mapnr;
extern unsigned long num_physpages;
extern void * high_memory;
extern int page_cluster;
/* The inactive_clean lists are per zone. */
extern struct list_head active_list;
extern struct list_head inactive_dirty_list;

#include <asm/page.h>
#include <asm/pgtable.h>
#include <asm/atomic.h>

/*
 * Linux kernel virtual memory manager primitives.
 * The idea being to have a "virtual" mm in the same way
 * we have a virtual fs - giving a cleaner interface to the
 * mm details, and allowing different kinds of memory mappings
 * (from shared memory to executable loading to arbitrary
 * mmap() functions).
 */

/*
 * This struct defines a memory VMM memory area. There is one of these
 * per VM-area/task.  A VM area is any part of the process virtual memory
 * space that has a special rule for the page-fault handlers (ie a shared
 * library, the executable area etc).
 */
struct vm_area_struct {
	struct mm_struct * vm_mm;	/* VM area parameters */
	unsigned long vm_start;
	unsigned long vm_end;

	/* linked list of VM areas per task, sorted by address */
	struct vm_area_struct *vm_next;

	pgprot_t vm_page_prot;
	unsigned long vm_flags;

	/* AVL tree of VM areas per task, sorted by address */
	short vm_avl_height;
	struct vm_area_struct * vm_avl_left;
	struct vm_area_struct * vm_avl_right;

	/* For areas with an address space and backing store,
	 * one of the address_space->i_mmap{,shared} lists,
	 * for shm areas, the list of attaches, otherwise unused.
	 */
	struct vm_area_struct *vm_next_share;
	struct vm_area_struct **vm_pprev_share;

	struct vm_operations_struct * vm_ops;
	unsigned long vm_pgoff;		/* offset in PAGE_SIZE units, *not* PAGE_CACHE_SIZE */
	struct file * vm_file;
	unsigned long vm_raend;
	void * vm_private_data;		/* was vm_pte (shared mem) */
};

/*
 * vm_flags..
 */
#define VM_READ		0x00000001	/* currently active flags */
#define VM_WRITE	0x00000002
#define VM_EXEC		0x00000004
#define VM_SHARED	0x00000008

#define VM_MAYREAD	0x00000010	/* limits for mprotect() etc */
#define VM_MAYWRITE	0x00000020
#define VM_MAYEXEC	0x00000040
#define VM_MAYSHARE	0x00000080

#define VM_GROWSDOWN	0x00000100	/* general info on the segment */
#define VM_GROWSUP	0x00000200
#define VM_SHM		0x00000400	/* shared memory area, don't swap out */
#define VM_DENYWRITE	0x00000800	/* ETXTBSY on write attempts.. */

#define VM_EXECUTABLE	0x00001000
#define VM_LOCKED	0x00002000
#define VM_IO           0x00004000	/* Memory mapped I/O or similar */

#define VM_SEQ_READ	0x00008000	/* App will access data sequentially */
#define VM_RAND_READ	0x00010000	/* App will not benefit from clustered reads */

#define VM_DONTCOPY	0x00020000      /* Do not copy this vma on fork */
#define VM_DONTEXPAND	0x00040000	/* Cannot expand with mremap() */
#define VM_RESERVED	0x00080000	/* Don't unmap it from swap_out */

#define VM_STACK_FLAGS	0x00000177

#define VM_READHINTMASK			(VM_SEQ_READ | VM_RAND_READ)
#define VM_ClearReadHint(v)		(v)->vm_flags &= ~VM_READHINTMASK
#define VM_NormalReadHint(v)		(!((v)->vm_flags & VM_READHINTMASK))
#define VM_SequentialReadHint(v)	((v)->vm_flags & VM_SEQ_READ)
#define VM_RandomReadHint(v)		((v)->vm_flags & VM_RAND_READ)

/*
 * mapping from the currently active vm_flags protection bits (the
 * low four bits) to a page protection mask..
 */
extern pgprot_t protection_map[16];


/*
 * These are the virtual MM functions - opening of an area, closing and
 * unmapping it (needed to keep files on disk up-to-date etc), pointer
 * to the functions called when a no-page or a wp-page exception occurs. 
 */
struct vm_operations_struct {
	void (*open)(struct vm_area_struct * area);
	void (*close)(struct vm_area_struct * area);
	struct page * (*nopage)(struct vm_area_struct * area, unsigned long address, int write_access);
};

/*
 * Try to keep the most commonly accessed fields in single cache lines
 * here (16 bytes or greater).  This ordering should be particularly
 * beneficial on 32-bit processors.
 *
 * The first line is data used in page cache lookup, the second line
 * is used for linear searches (eg. clock algorithm scans). 
 */
typedef struct page {
	struct list_head list;
	struct address_space *mapping;
	unsigned long index;
	struct page *next_hash;
	atomic_t count;
	unsigned long flags;	/* atomic flags, some possibly updated asynchronously */
	struct list_head lru;
	unsigned long age;
	wait_queue_head_t wait;
	struct page **pprev_hash;
	struct buffer_head * buffers;
	void *virtual; /* non-NULL if kmapped */
	struct zone_struct *zone;
} mem_map_t;

#define get_page(p)		atomic_inc(&(p)->count)
#define put_page(p)		__free_page(p)
#define put_page_testzero(p) 	atomic_dec_and_test(&(p)->count)
#define page_count(p)		atomic_read(&(p)->count)
#define set_page_count(p,v) 	atomic_set(&(p)->count, v)

/* Page flag bit values */
#define PG_locked		 0
#define PG_error		 1
#define PG_referenced		 2
#define PG_uptodate		 3
#define PG_dirty		 4
#define PG_decr_after		 5
#define PG_active		 6
#define PG_inactive_dirty	 7
#define PG_slab			 8
#define PG_swap_cache		 9
#define PG_skip			10
#define PG_inactive_clean	11
#define PG_highmem		12
				/* bits 21-29 unused */
#define PG_arch_1		30
#define PG_reserved		31

/* Make it prettier to test the above... */
#define Page_Uptodate(page)	test_bit(PG_uptodate, &(page)->flags)
#define SetPageUptodate(page)	set_bit(PG_uptodate, &(page)->flags)
#define ClearPageUptodate(page)	clear_bit(PG_uptodate, &(page)->flags)
#define PageDirty(page)		test_bit(PG_dirty, &(page)->flags)
#define SetPageDirty(page)	set_bit(PG_dirty, &(page)->flags)
#define ClearPageDirty(page)	clear_bit(PG_dirty, &(page)->flags)
#define PageLocked(page)	test_bit(PG_locked, &(page)->flags)
#define LockPage(page)		set_bit(PG_locked, &(page)->flags)
#define TryLockPage(page)	test_and_set_bit(PG_locked, &(page)->flags)

extern void __set_page_dirty(struct page *);

static inline void set_page_dirty(struct page * page)
{
	if (!test_and_set_bit(PG_dirty, &page->flags))
		__set_page_dirty(page);
}

/*
 * The first mb is necessary to safely close the critical section opened by the
 * TryLockPage(), the second mb is necessary to enforce ordering between
 * the clear_bit and the read of the waitqueue (to avoid SMP races with a
 * parallel wait_on_page).
 */
#define UnlockPage(page)	do { \
					smp_mb__before_clear_bit(); \
					if (!test_and_clear_bit(PG_locked, &(page)->flags)) BUG(); \
					smp_mb__after_clear_bit(); \
					if (waitqueue_active(&page->wait)) \
						wake_up(&page->wait); \
				} while (0)
#define PageError(page)		test_bit(PG_error, &(page)->flags)
#define SetPageError(page)	set_bit(PG_error, &(page)->flags)
#define ClearPageError(page)	clear_bit(PG_error, &(page)->flags)
#define PageReferenced(page)	test_bit(PG_referenced, &(page)->flags)
#define SetPageReferenced(page)	set_bit(PG_referenced, &(page)->flags)
#define ClearPageReferenced(page)	clear_bit(PG_referenced, &(page)->flags)
#define PageTestandClearReferenced(page)	test_and_clear_bit(PG_referenced, &(page)->flags)
#define PageDecrAfter(page)	test_bit(PG_decr_after, &(page)->flags)
#define SetPageDecrAfter(page)	set_bit(PG_decr_after, &(page)->flags)
#define PageTestandClearDecrAfter(page)	test_and_clear_bit(PG_decr_after, &(page)->flags)
#define PageSlab(page)		test_bit(PG_slab, &(page)->flags)
#define PageSwapCache(page)	test_bit(PG_swap_cache, &(page)->flags)
#define PageReserved(page)	test_bit(PG_reserved, &(page)->flags)

#define PageSetSlab(page)	set_bit(PG_slab, &(page)->flags)
#define PageSetSwapCache(page)	set_bit(PG_swap_cache, &(page)->flags)

#define PageTestandSetSwapCache(page)	test_and_set_bit(PG_swap_cache, &(page)->flags)

#define PageClearSlab(page)		clear_bit(PG_slab, &(page)->flags)
#define PageClearSwapCache(page)	clear_bit(PG_swap_cache, &(page)->flags)

#define PageTestandClearSwapCache(page)	test_and_clear_bit(PG_swap_cache, &(page)->flags)

#define PageActive(page)	test_bit(PG_active, &(page)->flags)
#define SetPageActive(page)	set_bit(PG_active, &(page)->flags)
#define ClearPageActive(page)	clear_bit(PG_active, &(page)->flags)

#define PageInactiveDirty(page)	test_bit(PG_inactive_dirty, &(page)->flags)
#define SetPageInactiveDirty(page)	set_bit(PG_inactive_dirty, &(page)->flags)
#define ClearPageInactiveDirty(page)	clear_bit(PG_inactive_dirty, &(page)->flags)

#define PageInactiveClean(page)	test_bit(PG_inactive_clean, &(page)->flags)
#define SetPageInactiveClean(page)	set_bit(PG_inactive_clean, &(page)->flags)
#define ClearPageInactiveClean(page)	clear_bit(PG_inactive_clean, &(page)->flags)

#ifdef CONFIG_HIGHMEM
#define PageHighMem(page)		test_bit(PG_highmem, &(page)->flags)
#else
#define PageHighMem(page)		0 /* needed to optimize away at compile time */
#endif

#define SetPageReserved(page)		set_bit(PG_reserved, &(page)->flags)
#define ClearPageReserved(page)		clear_bit(PG_reserved, &(page)->flags)

/*
 * Error return values for the *_nopage functions
 */
#define NOPAGE_SIGBUS	(NULL)
#define NOPAGE_OOM	((struct page *) (-1))


/*
 * Various page->flags bits:
 *
 * PG_reserved is set for a page which must never be accessed (which
 * may not even be present).
 *
 * PG_DMA has been removed, page->zone now tells exactly wether the
 * page is suited to do DMAing into.
 *
 * Multiple processes may "see" the same page. E.g. for untouched
 * mappings of /dev/null, all processes see the same page full of
 * zeroes, and text pages of executables and shared libraries have
 * only one copy in memory, at most, normally.
 *
 * For the non-reserved pages, page->count denotes a reference count.
 *   page->count == 0 means the page is free.
 *   page->count == 1 means the page is used for exactly one purpose
 *   (e.g. a private data page of one process).
 *
 * A page may be used for kmalloc() or anyone else who does a
 * __get_free_page(). In this case the page->count is at least 1, and
 * all other fields are unused but should be 0 or NULL. The
 * management of this page is the responsibility of the one who uses
 * it.
 *
 * The other pages (we may call them "process pages") are completely
 * managed by the Linux memory manager: I/O, buffers, swapping etc.
 * The following discussion applies only to them.
 *
 * A page may belong to an inode's memory mapping. In this case,
 * page->inode is the pointer to the inode, and page->offset is the
 * file offset of the page (not necessarily a multiple of PAGE_SIZE).
 *
 * A page may have buffers allocated to it. In this case,
 * page->buffers is a circular list of these buffer heads. Else,
 * page->buffers == NULL.
 *
 * For pages belonging to inodes, the page->count is the number of
 * attaches, plus 1 if buffers are allocated to the page.
 *
 * All pages belonging to an inode make up a doubly linked list
 * inode->i_pages, using the fields page->next and page->prev. (These
 * fields are also used for freelist management when page->count==0.)
 * There is also a hash table mapping (inode,offset) to the page
 * in memory if present. The lists for this hash table use the fields
 * page->next_hash and page->pprev_hash.
 *
 * All process pages can do I/O:
 * - inode pages may need to be read from disk,
 * - inode pages which have been modified and are MAP_SHARED may need
 *   to be written to disk,
 * - private pages which have been modified may need to be swapped out
 *   to swap space and (later) to be read back into memory.
 * During disk I/O, PG_locked is used. This bit is set before I/O
 * and reset when I/O completes. page->wait is a wait queue of all
 * tasks waiting for the I/O on this page to complete.
 * PG_uptodate tells whether the page's contents is valid.
 * When a read completes, the page becomes uptodate, unless a disk I/O
 * error happened.
 *
 * For choosing which pages to swap out, inode pages carry a
 * PG_referenced bit, which is set any time the system accesses
 * that page through the (inode,offset) hash table.
 *
 * PG_skip is used on sparc/sparc64 architectures to "skip" certain
 * parts of the address space.
 *
 * PG_error is set to indicate that an I/O error occurred on this page.
 *
 * PG_arch_1 is an architecture specific page state bit.  The generic
 * code guarentees that this bit is cleared for a page when it first
 * is entered into the page cache.
 */

extern mem_map_t * mem_map;

/*
 * There is only one page-allocator function, and two main namespaces to
 * it. The alloc_page*() variants return 'struct page *' and as such
 * can allocate highmem pages, the *get*page*() variants return
 * virtual kernel addresses to the allocated page(s).
 */
extern struct page * FASTCALL(__alloc_pages(zonelist_t *zonelist, unsigned long order));
extern struct page * alloc_pages_node(int nid, int gfp_mask, unsigned long order);

#ifndef CONFIG_DISCONTIGMEM
static inline struct page * alloc_pages(int gfp_mask, unsigned long order)
{
	/*
	 * Gets optimized away by the compiler.
	 */
	if (order >= MAX_ORDER)
		return NULL;
	return __alloc_pages(contig_page_data.node_zonelists+(gfp_mask), order);
}
#else /* !CONFIG_DISCONTIGMEM */
extern struct page * alloc_pages(int gfp_mask, unsigned long order);
#endif /* !CONFIG_DISCONTIGMEM */

#define alloc_page(gfp_mask) alloc_pages(gfp_mask, 0)

extern unsigned long FASTCALL(__get_free_pages(int gfp_mask, unsigned long order));
extern unsigned long FASTCALL(get_zeroed_page(int gfp_mask));

#define __get_free_page(gfp_mask) \
		__get_free_pages((gfp_mask),0)

#define __get_dma_pages(gfp_mask, order) \
		__get_free_pages((gfp_mask) | GFP_DMA,(order))

/*
 * The old interface name will be removed in 2.5:
 */
#define get_free_page get_zeroed_page

/*
 * There is only one 'core' page-freeing function.
 */
extern void FASTCALL(__free_pages(struct page *page, unsigned long order));
extern void FASTCALL(free_pages(unsigned long addr, unsigned long order));

#define __free_page(page) __free_pages((page), 0)
#define free_page(addr) free_pages((addr),0)

extern void show_free_areas(void);
extern void show_free_areas_node(pg_data_t *pgdat);

extern void clear_page_tables(struct mm_struct *, unsigned long, int);

struct page * shmem_nopage(struct vm_area_struct * vma, unsigned long address, int no_share);
struct file *shmem_file_setup(char * name, loff_t size);
extern int shmem_zero_setup(struct vm_area_struct *);

extern void zap_page_range(struct mm_struct *mm, unsigned long address, unsigned long size);
extern int copy_page_range(struct mm_struct *dst, struct mm_struct *src, struct vm_area_struct *vma);
extern int remap_page_range(unsigned long from, unsigned long to, unsigned long size, pgprot_t prot);
extern int zeromap_page_range(unsigned long from, unsigned long size, pgprot_t prot);

extern void vmtruncate(struct inode * inode, loff_t offset);
extern int handle_mm_fault(struct mm_struct *mm,struct vm_area_struct *vma, unsigned long address, int write_access);
extern int make_pages_present(unsigned long addr, unsigned long end);
extern int access_process_vm(struct task_struct *tsk, unsigned long addr, void *buf, int len, int write);
extern int ptrace_readdata(struct task_struct *tsk, unsigned long src, char *dst, int len);
extern int ptrace_writedata(struct task_struct *tsk, char * src, unsigned long dst, int len);

extern int pgt_cache_water[2];
extern int check_pgt_cache(void);

extern void free_area_init(unsigned long * zones_size);
extern void free_area_init_node(int nid, pg_data_t *pgdat, struct page *pmap,
	unsigned long * zones_size, unsigned long zone_start_paddr, 
	unsigned long *zholes_size);
extern void mem_init(void);
extern void show_mem(void);
extern void si_meminfo(struct sysinfo * val);
extern void swapin_readahead(swp_entry_t);

/* mmap.c */
extern void lock_vma_mappings(struct vm_area_struct *);
extern void unlock_vma_mappings(struct vm_area_struct *);
extern void insert_vm_struct(struct mm_struct *, struct vm_area_struct *);
extern void __insert_vm_struct(struct mm_struct *, struct vm_area_struct *);
extern void build_mmap_avl(struct mm_struct *);
extern void exit_mmap(struct mm_struct *);
extern unsigned long get_unmapped_area(unsigned long, unsigned long);

extern unsigned long do_mmap_pgoff(struct file *file, unsigned long addr,
	unsigned long len, unsigned long prot,
	unsigned long flag, unsigned long pgoff);

static inline unsigned long do_mmap(struct file *file, unsigned long addr,
	unsigned long len, unsigned long prot,
	unsigned long flag, unsigned long offset)
{
	unsigned long ret = -EINVAL;
	if ((offset + PAGE_ALIGN(len)) < offset)
		goto out;
	if (!(offset & ~PAGE_MASK))
		ret = do_mmap_pgoff(file, addr, len, prot, flag, offset >> PAGE_SHIFT);
out:
	return ret;
}

extern int do_munmap(struct mm_struct *, unsigned long, size_t);

extern unsigned long do_brk(unsigned long, unsigned long);

struct zone_t;
/* filemap.c */
extern void remove_inode_page(struct page *);
extern unsigned long page_unuse(struct page *);
extern void truncate_inode_pages(struct address_space *, loff_t);

/* generic vm_area_ops exported for stackable file systems */
extern int filemap_sync(struct vm_area_struct *, unsigned long,	size_t, unsigned int);
extern struct page *filemap_nopage(struct vm_area_struct *, unsigned long, int);

/*
 * GFP bitmasks..
 */
#define __GFP_WAIT	0x01
#define __GFP_HIGH	0x02
#define __GFP_IO	0x04
#define __GFP_DMA	0x08
#ifdef CONFIG_HIGHMEM
#define __GFP_HIGHMEM	0x10
#else
#define __GFP_HIGHMEM	0x0 /* noop */
#endif


#define GFP_BUFFER	(__GFP_HIGH | __GFP_WAIT)
#define GFP_ATOMIC	(__GFP_HIGH)
#define GFP_USER	(             __GFP_WAIT | __GFP_IO)
#define GFP_HIGHUSER	(             __GFP_WAIT | __GFP_IO | __GFP_HIGHMEM)
#define GFP_KERNEL	(__GFP_HIGH | __GFP_WAIT | __GFP_IO)
#define GFP_NFS		(__GFP_HIGH | __GFP_WAIT | __GFP_IO)
#define GFP_KSWAPD	(                          __GFP_IO)

/* Flag - indicates that the buffer will be suitable for DMA.  Ignored on some
   platforms, used as appropriate on others */

#define GFP_DMA		__GFP_DMA

/* Flag - indicates that the buffer can be taken from high memory which is not
   permanently mapped by the kernel */

#define GFP_HIGHMEM	__GFP_HIGHMEM

/* vma is the first one with  address < vma->vm_end,
 * and even  address < vma->vm_start. Have to extend vma. */
static inline int expand_stack(struct vm_area_struct * vma, unsigned long address)
{
	unsigned long grow;

	address &= PAGE_MASK;
	grow = (vma->vm_start - address) >> PAGE_SHIFT;
	if (vma->vm_end - address > current->rlim[RLIMIT_STACK].rlim_cur ||
	    ((vma->vm_mm->total_vm + grow) << PAGE_SHIFT) > current->rlim[RLIMIT_AS].rlim_cur)
		return -ENOMEM;
	vma->vm_start = address;
	vma->vm_pgoff -= grow;
	vma->vm_mm->total_vm += grow;
	if (vma->vm_flags & VM_LOCKED)
		vma->vm_mm->locked_vm += grow;
	return 0;
}

/* Look up the first VMA which satisfies  addr < vm_end,  NULL if none. */
extern struct vm_area_struct * find_vma(struct mm_struct * mm, unsigned long addr);
extern struct vm_area_struct * find_vma_prev(struct mm_struct * mm, unsigned long addr,
					     struct vm_area_struct **pprev);

/* Look up the first VMA which intersects the interval start_addr..end_addr-1,
   NULL if none.  Assume start_addr < end_addr. */
static inline struct vm_area_struct * find_vma_intersection(struct mm_struct * mm, unsigned long start_addr, unsigned long end_addr)
{
	struct vm_area_struct * vma = find_vma(mm,start_addr);

	if (vma && end_addr <= vma->vm_start)
		vma = NULL;
	return vma;
}

extern struct vm_area_struct *find_extend_vma(struct mm_struct *mm, unsigned long addr);

#define buffer_under_min()	(atomic_read(&buffermem_pages) * 100 < \
				buffer_mem.min_percent * num_physpages)
#define pgcache_under_min()	(atomic_read(&page_cache_size) * 100 < \
				page_cache.min_percent * num_physpages)

#endif /* __KERNEL__ */

#endif
