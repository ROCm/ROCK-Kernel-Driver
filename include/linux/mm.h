#ifndef _LINUX_MM_H
#define _LINUX_MM_H

#include <linux/sched.h>
#include <linux/errno.h>

#ifdef __KERNEL__

#include <linux/config.h>
#include <linux/gfp.h>
#include <linux/string.h>
#include <linux/list.h>
#include <linux/mmzone.h>
#include <linux/swap.h>
#include <linux/rbtree.h>
#include <linux/fs.h>

extern unsigned long max_mapnr;
extern unsigned long num_physpages;
extern void * high_memory;
extern int page_cluster;
/* The inactive_clean lists are per zone. */
extern struct list_head active_list;
extern struct list_head inactive_list;

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
	struct mm_struct * vm_mm;	/* The address space we belong to. */
	unsigned long vm_start;		/* Our start address within vm_mm. */
	unsigned long vm_end;		/* The first byte after our end address
					   within vm_mm. */

	/* linked list of VM areas per task, sorted by address */
	struct vm_area_struct *vm_next;

	pgprot_t vm_page_prot;		/* Access permissions of this VMA. */
	unsigned long vm_flags;		/* Flags, listed below. */

	rb_node_t vm_rb;

	/*
	 * For areas with an address space and backing store,
	 * one of the address_space->i_mmap{,shared} lists,
	 * for shm areas, the list of attaches, otherwise unused.
	 */
	list_t shared;

	/* Function pointers to deal with this struct. */
	struct vm_operations_struct * vm_ops;

	/* Information about our backing store: */
	unsigned long vm_pgoff;		/* Offset (within vm_file) in PAGE_SIZE
					   units, *not* PAGE_CACHE_SIZE */
	struct file * vm_file;		/* File we map to (can be NULL). */
	unsigned long vm_raend;		/* XXX: put full readahead info here. */
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

					/* Used by sys_madvise() */
#define VM_SEQ_READ	0x00008000	/* App will access data sequentially */
#define VM_RAND_READ	0x00010000	/* App will not benefit from clustered reads */

#define VM_DONTCOPY	0x00020000      /* Do not copy this vma on fork */
#define VM_DONTEXPAND	0x00040000	/* Cannot expand with mremap() */
#define VM_RESERVED	0x00080000	/* Don't unmap it from swap_out */
#define VM_ACCOUNT	0x00100000	/* Is a VM accounted object */

#define VM_STACK_FLAGS	(0x00000100 | VM_DATA_DEFAULT_FLAGS | VM_ACCOUNT)

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
	struct page * (*nopage)(struct vm_area_struct * area, unsigned long address, int unused);
};

/* forward declaration; pte_chain is meant to be internal to rmap.c */
struct pte_chain;

/*
 * Each physical page in the system has a struct page associated with
 * it to keep track of whatever it is we are using the page for at the
 * moment. Note that we have no way to track which tasks are using
 * a page.
 *
 * Try to keep the most commonly accessed fields in single cache lines
 * here (16 bytes or greater).  This ordering should be particularly
 * beneficial on 32-bit processors.
 *
 * The first line is data used in page cache lookup, the second line
 * is used for linear searches (eg. clock algorithm scans). 
 *
 * TODO: make this structure smaller, it could be as small as 32 bytes.
 */
struct page {
	unsigned long flags;		/* atomic flags, some possibly
					   updated asynchronously */
	atomic_t count;			/* Usage count, see below. */
	struct list_head list;		/* ->mapping has some page lists. */
	struct address_space *mapping;	/* The inode (or ...) we belong to. */
	unsigned long index;		/* Our offset within mapping. */
	struct list_head lru;		/* Pageout list, eg. active_list;
					   protected by zone->lru_lock !! */
	union {
		struct pte_chain * chain;	/* Reverse pte mapping pointer.
					 * protected by PG_chainlock */
		pte_t		 * direct;
	} pte;
	unsigned long private;		/* mapping-private opaque data */

	/*
	 * On machines where all RAM is mapped into kernel address space,
	 * we can simply calculate the virtual address. On machines with
	 * highmem some memory is mapped into kernel virtual memory
	 * dynamically, so we need a place to store that address.
	 * Note that this field could be 16 bits on x86 ... ;)
	 *
	 * Architectures with slow multiplication can define
	 * WANT_PAGE_VIRTUAL in asm/page.h
	 */
#if defined(CONFIG_HIGHMEM) || defined(WANT_PAGE_VIRTUAL)
	void *virtual;			/* Kernel virtual address (NULL if
					   not kmapped, ie. highmem) */
#endif /* CONFIG_HIGMEM || WANT_PAGE_VIRTUAL */
};

/*
 * FIXME: take this include out, include page-flags.h in
 * files which need it (119 of them)
 */
#include <linux/page-flags.h>

/*
 * Methods to modify the page usage count.
 *
 * What counts for a page usage:
 * - cache mapping   (page->mapping)
 * - private data    (page->private)
 * - page mapped in a task's page tables, each mapping
 *   is counted separately
 *
 * Also, many kernel routines increase the page count before a critical
 * routine so they can be sure the page doesn't go away from under them.
 */
#define get_page(p)		atomic_inc(&(p)->count)
#define __put_page(p)		atomic_dec(&(p)->count)
#define put_page_testzero(p)				\
	({						\
		BUG_ON(page_count(page) == 0);		\
		atomic_dec_and_test(&(p)->count);	\
	})
#define page_count(p)		atomic_read(&(p)->count)
#define set_page_count(p,v) 	atomic_set(&(p)->count, v)

extern void FASTCALL(__page_cache_release(struct page *));
void FASTCALL(__free_pages_ok(struct page *page, unsigned int order));

static inline void put_page(struct page *page)
{
	if (!PageReserved(page) && put_page_testzero(page))
		__page_cache_release(page);
}

/*
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
 * page->mapping is the pointer to the inode, and page->index is the
 * file offset of the page, in units of PAGE_CACHE_SIZE.
 *
 * A page contains an opaque `private' member, which belongs to the
 * page's address_space.  Usually, this is the address of a circular
 * list of the page's disk buffers.
 *
 * For pages belonging to inodes, the page->count is the number of
 * attaches, plus 1 if `private' contains something, plus one for
 * the page cache itself.
 *
 * All pages belonging to an inode are in these doubly linked lists:
 * mapping->clean_pages, mapping->dirty_pages and mapping->locked_pages;
 * using the page->list list_head. These fields are also used for
 * freelist managemet (when page->count==0).
 *
 * There is also a per-mapping radix tree mapping index to the page
 * in memory if present. The tree is rooted at mapping->root.  
 *
 * All process pages can do I/O:
 * - inode pages may need to be read from disk,
 * - inode pages which have been modified and are MAP_SHARED may need
 *   to be written to disk,
 * - private pages which have been modified may need to be swapped out
 *   to swap space and (later) to be read back into memory.
 */

/*
 * The zone field is never updated after free_area_init_core()
 * sets it, so none of the operations on it need to be atomic.
 */
#define NODE_SHIFT 4
#define ZONE_SHIFT (BITS_PER_LONG - 8)

struct zone;
extern struct zone *zone_table[];

static inline struct zone *page_zone(struct page *page)
{
	return zone_table[page->flags >> ZONE_SHIFT];
}

static inline void set_page_zone(struct page *page, unsigned long zone_num)
{
	page->flags &= ~(~0UL << ZONE_SHIFT);
	page->flags |= zone_num << ZONE_SHIFT;
}

/*
 * In order to avoid #ifdefs within C code itself, we define
 * set_page_address to a noop for non-highmem machines, where
 * the field isn't useful.
 * The same is true for page_address() in arch-dependent code.
 */
#if defined(CONFIG_HIGHMEM) || defined(WANT_PAGE_VIRTUAL)

#define set_page_address(page, address)			\
	do {						\
		(page)->virtual = (address);		\
	} while(0)

#else /* CONFIG_HIGHMEM || WANT_PAGE_VIRTUAL */
#define set_page_address(page, address)  do { } while(0)
#endif /* CONFIG_HIGHMEM || WANT_PAGE_VIRTUAL */

/*
 * Permanent address of a page. Obviously must never be
 * called on a highmem page.
 */
#if defined(CONFIG_HIGHMEM) || defined(WANT_PAGE_VIRTUAL)

#define page_address(page) ((page)->virtual)

#else /* CONFIG_HIGHMEM || WANT_PAGE_VIRTUAL */

#define page_address(page)						\
	__va( ( ((page) - page_zone(page)->zone_mem_map)		\
			+ page_zone(page)->zone_start_pfn) << PAGE_SHIFT)

#endif /* CONFIG_HIGHMEM || WANT_PAGE_VIRTUAL */

/*
 * Error return values for the *_nopage functions
 */
#define NOPAGE_SIGBUS	(NULL)
#define NOPAGE_OOM	((struct page *) (-1))

/*
 * Different kinds of faults, as returned by handle_mm_fault().
 * Used to decide whether a process gets delivered SIGBUS or
 * just gets major/minor fault counters bumped up.
 */
#define VM_FAULT_OOM	(-1)
#define VM_FAULT_SIGBUS	0
#define VM_FAULT_MINOR	1
#define VM_FAULT_MAJOR	2

/* The array of struct pages */
extern struct page *mem_map;

extern void show_free_areas(void);

extern int fail_writepage(struct page *);
struct page * shmem_nopage(struct vm_area_struct * vma, unsigned long address, int unused);
struct file *shmem_file_setup(char * name, loff_t size, unsigned long flags);
extern void shmem_lock(struct file * file, int lock);
extern int shmem_zero_setup(struct vm_area_struct *);

extern void zap_page_range(struct vm_area_struct *vma, unsigned long address, unsigned long size);
extern int copy_page_range(struct mm_struct *dst, struct mm_struct *src, struct vm_area_struct *vma);
extern int remap_page_range(struct vm_area_struct *vma, unsigned long from, unsigned long to, unsigned long size, pgprot_t prot);
extern int zeromap_page_range(struct vm_area_struct *vma, unsigned long from, unsigned long size, pgprot_t prot);

extern int vmtruncate(struct inode * inode, loff_t offset);
extern pmd_t *FASTCALL(__pmd_alloc(struct mm_struct *mm, pgd_t *pgd, unsigned long address));
extern pte_t *FASTCALL(pte_alloc_kernel(struct mm_struct *mm, pmd_t *pmd, unsigned long address));
extern pte_t *FASTCALL(pte_alloc_map(struct mm_struct *mm, pmd_t *pmd, unsigned long address));
extern int handle_mm_fault(struct mm_struct *mm,struct vm_area_struct *vma, unsigned long address, int write_access);
extern int make_pages_present(unsigned long addr, unsigned long end);
extern int access_process_vm(struct task_struct *tsk, unsigned long addr, void *buf, int len, int write);

int get_user_pages(struct task_struct *tsk, struct mm_struct *mm, unsigned long start,
		int len, int write, int force, struct page **pages, struct vm_area_struct **vmas);

int __set_page_dirty_buffers(struct page *page);
int __set_page_dirty_nobuffers(struct page *page);

/*
 * If the mapping doesn't provide a set_page_dirty a_op, then
 * just fall through and assume that it wants buffer_heads.
 * FIXME: make the method unconditional.
 */
static inline int set_page_dirty(struct page *page)
{
	if (page->mapping) {
		int (*spd)(struct page *);

		spd = page->mapping->a_ops->set_page_dirty;
		if (spd)
			return (*spd)(page);
	}
	return __set_page_dirty_buffers(page);
}

/*
 * On a two-level page table, this ends up being trivial. Thus the
 * inlining and the symmetry break with pte_alloc_map() that does all
 * of this out-of-line.
 */
static inline pmd_t *pmd_alloc(struct mm_struct *mm, pgd_t *pgd, unsigned long address)
{
	if (pgd_none(*pgd))
		return __pmd_alloc(mm, pgd, address);
	return pmd_offset(pgd, address);
}

extern void free_area_init(unsigned long * zones_size);
extern void free_area_init_node(int nid, pg_data_t *pgdat, struct page *pmap,
	unsigned long * zones_size, unsigned long zone_start_pfn, 
	unsigned long *zholes_size);
extern void mem_init(void);
extern void show_mem(void);
extern void si_meminfo(struct sysinfo * val);
extern void swapin_readahead(swp_entry_t);

extern int can_share_swap_page(struct page *);
extern int remove_exclusive_swap_page(struct page *);

/* mmap.c */
extern void lock_vma_mappings(struct vm_area_struct *);
extern void unlock_vma_mappings(struct vm_area_struct *);
extern void insert_vm_struct(struct mm_struct *, struct vm_area_struct *);
extern void __insert_vm_struct(struct mm_struct *, struct vm_area_struct *);
extern void build_mmap_rb(struct mm_struct *);
extern void exit_mmap(struct mm_struct *);

extern unsigned long get_unmapped_area(struct file *, unsigned long, unsigned long, unsigned long, unsigned long);

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

static inline void __vma_unlink(struct mm_struct * mm, struct vm_area_struct * vma, struct vm_area_struct * prev)
{
	prev->vm_next = vma->vm_next;
	rb_erase(&vma->vm_rb, &mm->mm_rb);
	if (mm->mmap_cache == vma)
		mm->mmap_cache = prev;
}

static inline int can_vma_merge(struct vm_area_struct * vma, unsigned long vm_flags)
{
	if (!vma->vm_file && vma->vm_flags == vm_flags)
		return 1;
	else
		return 0;
}

/* filemap.c */
extern unsigned long page_unuse(struct page *);
extern void truncate_inode_pages(struct address_space *, loff_t);

/* generic vm_area_ops exported for stackable file systems */
extern int filemap_sync(struct vm_area_struct *, unsigned long,	size_t, unsigned int);
extern struct page *filemap_nopage(struct vm_area_struct *, unsigned long, int);

/* mm/page-writeback.c */
int write_one_page(struct page *page, int wait);

/* readahead.c */
#define VM_MAX_READAHEAD	128	/* kbytes */
#define VM_MIN_READAHEAD	16	/* kbytes (includes current page) */
int do_page_cache_readahead(struct file *file,
			unsigned long offset, unsigned long nr_to_read);
void page_cache_readahead(struct file *file, unsigned long offset);
void page_cache_readaround(struct file *file, unsigned long offset);
void handle_ra_miss(struct file *file);

/* Do stack extension */
extern int expand_stack(struct vm_area_struct * vma, unsigned long address);

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

extern struct page * vmalloc_to_page(void *addr);
extern unsigned long get_page_cache_size(void);

#endif /* __KERNEL__ */

#endif
