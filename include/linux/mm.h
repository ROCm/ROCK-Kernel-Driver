#ifndef _LINUX_MM_H
#define _LINUX_MM_H

#include <linux/sched.h>
#include <linux/errno.h>

#ifdef __KERNEL__

#include <linux/config.h>
#include <linux/gfp.h>
#include <linux/list.h>
#include <linux/mmzone.h>
#include <linux/rbtree.h>
#include <linux/prio_tree.h>
#include <linux/fs.h>

#ifndef CONFIG_DISCONTIGMEM          /* Don't use mapnrs, do it properly */
extern unsigned long max_mapnr;
#endif

extern unsigned long num_physpages;
extern void * high_memory;
extern int page_cluster;

#include <asm/page.h>
#include <asm/pgtable.h>
#include <asm/processor.h>
#include <asm/atomic.h>

#ifndef MM_VM_SIZE
#define MM_VM_SIZE(mm)	TASK_SIZE
#endif

/*
 * Linux kernel virtual memory manager primitives.
 * The idea being to have a "virtual" mm in the same way
 * we have a virtual fs - giving a cleaner interface to the
 * mm details, and allowing different kinds of memory mappings
 * (from shared memory to executable loading to arbitrary
 * mmap() functions).
 */

typedef struct anon_vma_s {
	/* This serializes the accesses to the vma list. */
	spinlock_t anon_vma_lock;

	/*
	 * This is a list of anonymous "related" vmas,
	 * to scan if one of the pages pointing to this
	 * anon_vma needs to be unmapped.
	 * After we unlink the last vma we must garbage collect
	 * the object if the list is empty because we're
	 * guaranteed no page can be pointing to this anon_vma
	 * if there's no vma anymore.
	 */
	struct list_head anon_vma_head;
} anon_vma_t;

/*
 * This struct defines a memory VMM memory area. There is one of these
 * per VM-area/task.  A VM area is any part of the process virtual memory
 * space that has a special rule for the page-fault handlers (ie a shared
 * library, the executable area etc).
 *
 * This structure is exactly 64 bytes on ia32.  Please think very, very hard
 * before adding anything to it.
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

	struct rb_node vm_rb;

	/*
	 * For areas with an address space and backing store,
	 * one of the address_space->i_mmap{,shared} lists,
	 * for shm areas, the list of attaches, otherwise unused.
	 */
	union {
		struct {
			struct list_head list;
			void *parent;
		} vm_set;

		struct prio_tree_node  prio_tree_node;

		struct {
			void *first;
			void *second;
			void *parent;
		} both;
	} shared;

	/*
	 * shared.vm_set : list of vmas that map exactly the same set of pages
	 * vm_set_head   : head of the vm_set list
	 *
	 * TODO: try to shove the following field into vm_private_data ??
	 */
	struct vm_area_struct *vm_set_head;

	/*
	 * The same vma can be both queued into the i_mmap and in a
	 * anon_vma too, for example after a cow in
	 * a MAP_PRIVATE file mapping. However only the MAP_PRIVATE
	 * will go both in the i_mmap and anon_vma. A MAP_SHARED
	 * will only be in the i_mmap_shared and a MAP_ANONYMOUS (file = 0)
	 * will only be queued only in the anon_vma.
	 * The list is serialized by the anon_vma->lock.
	 */
	struct list_head anon_vma_node;
	/* Serialized by the mmap_sem */
	anon_vma_t * anon_vma;

	/* Function pointers to deal with this struct. */
	struct vm_operations_struct * vm_ops;

	/* Information about our backing store: */
	unsigned long vm_pgoff;		/* Offset (within vm_file) in PAGE_SIZE
					   units, *not* PAGE_CACHE_SIZE */
	struct file * vm_file;		/* File we map to (can be NULL). */
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
#define VM_NONCACHED	0x00200000	/* Noncached access */
#define VM_HUGETLB	0x00400000	/* Huge TLB Page VM */
#define VM_NONLINEAR	0x00800000	/* Is non-linear (remap_file_pages) */

/* It makes sense to apply VM_ACCOUNT to this vma. */
#define VM_MAYACCT(vma) (!!((vma)->vm_flags & VM_HUGETLB))

#ifndef VM_STACK_DEFAULT_FLAGS		/* arch can override this */
#define VM_STACK_DEFAULT_FLAGS VM_DATA_DEFAULT_FLAGS
#endif

#ifdef CONFIG_STACK_GROWSUP
#define VM_STACK_FLAGS	(VM_GROWSUP | VM_STACK_DEFAULT_FLAGS | VM_ACCOUNT)
#else
#define VM_STACK_FLAGS	(VM_GROWSDOWN | VM_STACK_DEFAULT_FLAGS | VM_ACCOUNT)
#endif

#define VM_READHINTMASK			(VM_SEQ_READ | VM_RAND_READ)
#define VM_ClearReadHint(v)		(v)->vm_flags &= ~VM_READHINTMASK
#define VM_NormalReadHint(v)		(!((v)->vm_flags & VM_READHINTMASK))
#define VM_SequentialReadHint(v)	((v)->vm_flags & VM_SEQ_READ)
#define VM_RandomReadHint(v)		((v)->vm_flags & VM_RAND_READ)

/*
 * The following macros are used for implementing prio_tree for i_mmap{_shared}
 */

#define	RADIX_INDEX(vma)  ((vma)->vm_pgoff)
#define	VMA_SIZE(vma)	  (((vma)->vm_end - (vma)->vm_start) >> PAGE_SHIFT)
/* avoid overflow */
#define	HEAP_INDEX(vma)	  ((vma)->vm_pgoff + (VMA_SIZE(vma) - 1))

#define GET_INDEX_VMA(vma, radix, heap)		\
do {						\
	radix = RADIX_INDEX(vma);		\
	heap = HEAP_INDEX(vma);			\
} while (0)

#define GET_INDEX(node, radix, heap)		\
do { 						\
	struct vm_area_struct *__tmp = 		\
	  prio_tree_entry(node, struct vm_area_struct, shared.prio_tree_node);\
	GET_INDEX_VMA(__tmp, radix, heap); 	\
} while (0)

#define	INIT_VMA_SHARED_LIST(vma)			\
do {							\
	INIT_LIST_HEAD(&(vma)->shared.vm_set.list);	\
	(vma)->shared.vm_set.parent = NULL;		\
	(vma)->vm_set_head = NULL;			\
} while (0)

#define INIT_VMA_SHARED(vma)			\
do {						\
	(vma)->shared.both.first = NULL;	\
	(vma)->shared.both.second = NULL;	\
	(vma)->shared.both.parent = NULL;	\
	(vma)->vm_set_head = NULL;		\
} while (0)

extern void __vma_prio_tree_insert(struct prio_tree_root *,
	struct vm_area_struct *);

extern void __vma_prio_tree_remove(struct prio_tree_root *,
	struct vm_area_struct *);

static inline int vma_shared_empty(struct vm_area_struct *vma)
{
	return vma->shared.both.first == NULL;
}

/*
 * Helps to add a new vma that maps the same (identical) set of pages as the
 * old vma to an i_mmap tree.
 */
static inline void __vma_prio_tree_add(struct vm_area_struct *vma,
	struct vm_area_struct *old)
{
	INIT_VMA_SHARED_LIST(vma);

	/* Leave these BUG_ONs till prio_tree patch stabilizes */
	BUG_ON(RADIX_INDEX(vma) != RADIX_INDEX(old));
	BUG_ON(HEAP_INDEX(vma) != HEAP_INDEX(old));

	if (old->shared.both.parent) {
		if (old->vm_set_head) {
			list_add_tail(&vma->shared.vm_set.list,
					&old->vm_set_head->shared.vm_set.list);
			return;
		}
		else {
			old->vm_set_head = vma;
			vma->vm_set_head = old;
		}
	}
	else
		list_add(&vma->shared.vm_set.list, &old->shared.vm_set.list);
}

/*
 * We cannot modify vm_start, vm_end, vm_pgoff fields of a vma that has been
 * already present in an i_mmap{_shared} tree without modifying the tree. The
 * following helper function should be used when such modifications are
 * necessary. We should hold the mapping's i_shared_sem.
 *
 * This function can be (micro)optimized for some special cases (maybe later).
 */
static inline void __vma_modify(struct prio_tree_root *root,
	struct vm_area_struct *vma, unsigned long start, unsigned long end,
	unsigned long pgoff)
{
	if (root)
		__vma_prio_tree_remove(root, vma);
	vma->vm_start = start;
	vma->vm_end = end;
	vma->vm_pgoff = pgoff;
	if (root)
		__vma_prio_tree_insert(root, vma);
}

/*
 * Helper functions to enumerate vmas that map a given file page or a set of
 * contiguous file pages. The functions return vmas that at least map a single
 * page in the given range of contiguous file pages.
 */
static inline struct vm_area_struct *__vma_prio_tree_first(
	struct prio_tree_root *root, struct prio_tree_iter *iter,
	unsigned long begin, unsigned long end)
{
	struct prio_tree_node *ptr;

	ptr = prio_tree_first(root, iter, begin, end);

	if (ptr)
		return prio_tree_entry(ptr, struct vm_area_struct,
				shared.prio_tree_node);
	else
		return NULL;
}

static inline struct vm_area_struct *__vma_prio_tree_next(
	struct vm_area_struct *vma, struct prio_tree_root *root,
	struct prio_tree_iter *iter, unsigned long begin, unsigned long end)
{
	struct prio_tree_node *ptr;
	struct vm_area_struct *next;

	if (vma->shared.both.parent) {
		if (vma->vm_set_head)
			return vma->vm_set_head;
	}
	else {
		next = list_entry(vma->shared.vm_set.list.next,
				struct vm_area_struct, shared.vm_set.list);
		if (!(next->vm_set_head))
			return next;
	}

	ptr = prio_tree_next(root, iter, begin, end);

	if (ptr)
		return prio_tree_entry(ptr, struct vm_area_struct,
				shared.prio_tree_node);
	else
		return NULL;
}

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
	struct page * (*nopage)(struct vm_area_struct * area, unsigned long address, int *type);
	int (*populate)(struct vm_area_struct * area, unsigned long address, unsigned long len, pgprot_t prot, unsigned long pgoff, int nonblock);
};

/* forward declaration; pte_chain is meant to be internal to rmap.c */
struct pte_chain;
struct mmu_gather;
struct inode;

#ifdef ARCH_HAS_ATOMIC_UNSIGNED
typedef unsigned page_flags_t;
#else
typedef unsigned long page_flags_t;
#endif

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
	page_flags_t flags;		/* atomic flags, some possibly
					   updated asynchronously */
	atomic_t count;			/* Usage count, see below. */
	/*
	 * Number of ptes mapping this page.
	 * It's serialized by PG_maplock.
	 * This is needed only to maintain the nr_mapped global info
	 * so it would be nice to drop it.
	 */
	unsigned int mapcount;		

	pgoff_t index;			/* Our offset within mapping. */
	struct list_head lru;		/* Pageout list, eg. active_list;
					   protected by zone->lru_lock !! */

	/*
	 * Address space of this page.
	 * A page can be either mapped to a file or to be anonymous
	 * memory, so using the union is optimal here. The PG_anon
	 * bitflag tells if this is anonymous or a file-mapping.
	 * If PG_anon is clear we use the as.mapping otherwise we
	 * use the as.anon_vma.
	 * The inode address space if it's a file mapping.
	 * An anon_vma object if it's an anymous mapping.
	 * The anon_vma can't go away under us if we hold the
	 * PG_maplock.
	 */
	struct address_space * mapping;

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
#if defined(WANT_PAGE_VIRTUAL)
	void *virtual;			/* Kernel virtual address (NULL if
					   not kmapped, ie. highmem) */
#endif /* WANT_PAGE_VIRTUAL */
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
#define put_page_testzero(p)				\
	({						\
		BUG_ON(page_count(p) == 0);		\
		atomic_dec_and_test(&(p)->count);	\
	})

#define set_page_count(p,v) 	atomic_set(&(p)->count, v)
#define __put_page(p)		atomic_dec(&(p)->count)

extern void FASTCALL(__page_cache_release(struct page *));

static inline int page_count(struct page *p)
{
	if (PageCompound(p))
		p = (struct page *)p->private;
	return atomic_read(&(p)->count);
}

static inline void get_page(struct page *page)
{
	if (PageCompound(page))
		page = (struct page *)page->private;
	atomic_inc(&page->count);
}

static inline void put_page(struct page *page)
{
	if (PageCompound(page)) {
		page = (struct page *)page->private;
		if (put_page_testzero(page)) {
			if (page[1].mapping) {	/* destructor? */
				(*(void (*)(struct page *))page[1].mapping)(page);
			} else {
				__page_cache_release(page);
			}
		}
		return;
	}
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
 * We'll have up to (MAX_NUMNODES * MAX_NR_ZONES) zones total,
 * so we use (MAX_NODES_SHIFT + MAX_ZONES_SHIFT) here to get enough bits.
 */
#define NODEZONE_SHIFT (sizeof(page_flags_t)*8 - MAX_NODES_SHIFT - MAX_ZONES_SHIFT)
#define NODEZONE(node, zone)	((node << ZONES_SHIFT) | zone)

static inline unsigned long page_zonenum(struct page *page)
{
	return (page->flags >> NODEZONE_SHIFT) & (~(~0UL << ZONES_SHIFT));
}
static inline unsigned long page_nodenum(struct page *page)
{
	return (page->flags >> (NODEZONE_SHIFT + ZONES_SHIFT));
}

struct zone;
extern struct zone *zone_table[];

static inline struct zone *page_zone(struct page *page)
{
	return zone_table[page->flags >> NODEZONE_SHIFT];
}

static inline void set_page_zone(struct page *page, unsigned long nodezone_num)
{
	page->flags &= ~(~0UL << NODEZONE_SHIFT);
	page->flags |= nodezone_num << NODEZONE_SHIFT;
}

#ifndef CONFIG_DISCONTIGMEM
/* The array of struct pages - for discontigmem use pgdat->lmem_map */
extern struct page *mem_map;
#endif

static inline void *lowmem_page_address(struct page *page)
{
	return __va(page_to_pfn(page) << PAGE_SHIFT);
}

#if defined(CONFIG_HIGHMEM) && !defined(WANT_PAGE_VIRTUAL)
#define HASHED_PAGE_VIRTUAL
#endif

#if defined(WANT_PAGE_VIRTUAL)
#define page_address(page) ((page)->virtual)
#define set_page_address(page, address)			\
	do {						\
		(page)->virtual = (address);		\
	} while(0)
#define page_address_init()  do { } while(0)
#endif

#if defined(HASHED_PAGE_VIRTUAL)
void *page_address(struct page *page);
void set_page_address(struct page *page, void *virtual);
void page_address_init(void);
#endif

#if !defined(HASHED_PAGE_VIRTUAL) && !defined(WANT_PAGE_VIRTUAL)
#define page_address(page) lowmem_page_address(page)
#define set_page_address(page, address)  do { } while(0)
#define page_address_init()  do { } while(0)
#endif

/*
 * Return true if this page is mapped into pagetables.
 */
static inline int page_mapped(struct page *page)
{
	return page->mapcount;
}

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

#define offset_in_page(p)	((unsigned long)(p) & ~PAGE_MASK)

extern void show_free_areas(void);

struct page *shmem_nopage(struct vm_area_struct * vma,
			unsigned long address, int *type);
struct file *shmem_file_setup(char * name, loff_t size, unsigned long flags);
void shmem_lock(struct file * file, int lock);
int shmem_zero_setup(struct vm_area_struct *);

void zap_page_range(struct vm_area_struct *vma, unsigned long address,
			unsigned long size);
int unmap_vmas(struct mmu_gather **tlbp, struct mm_struct *mm,
		struct vm_area_struct *start_vma, unsigned long start_addr,
		unsigned long end_addr, unsigned long *nr_accounted);
void unmap_page_range(struct mmu_gather *tlb, struct vm_area_struct *vma,
			unsigned long address, unsigned long size);
void clear_page_tables(struct mmu_gather *tlb, unsigned long first, int nr);
int copy_page_range(struct mm_struct *dst, struct mm_struct *src,
			struct vm_area_struct *vma);
int zeromap_page_range(struct vm_area_struct *vma, unsigned long from,
			unsigned long size, pgprot_t prot);

extern void invalidate_mmap_range(struct address_space *mapping,
				  loff_t const holebegin,
				  loff_t const holelen);
extern int vmtruncate(struct inode * inode, loff_t offset);
extern pmd_t *FASTCALL(__pmd_alloc(struct mm_struct *mm, pgd_t *pgd, unsigned long address));
extern pte_t *FASTCALL(pte_alloc_kernel(struct mm_struct *mm, pmd_t *pmd, unsigned long address));
extern pte_t *FASTCALL(pte_alloc_map(struct mm_struct *mm, pmd_t *pmd, unsigned long address));
extern int install_page(struct mm_struct *mm, struct vm_area_struct *vma, unsigned long addr, struct page *page, pgprot_t prot);
extern int install_file_pte(struct mm_struct *mm, struct vm_area_struct *vma, unsigned long addr, unsigned long pgoff, pgprot_t prot);
extern int handle_mm_fault(struct mm_struct *mm,struct vm_area_struct *vma, unsigned long address, int write_access);
extern int make_pages_present(unsigned long addr, unsigned long end);
extern int access_process_vm(struct task_struct *tsk, unsigned long addr, void *buf, int len, int write);
void put_dirty_page(struct task_struct *tsk, struct page *page,
		    unsigned long address, pgprot_t prot,
		    struct vm_area_struct *vma);

int get_user_pages(struct task_struct *tsk, struct mm_struct *mm, unsigned long start,
		int len, int write, int force, struct page **pages, struct vm_area_struct **vmas);

extern long do_mprotect(struct mm_struct *mm, unsigned long start, 
			size_t len, unsigned long prot);

int __set_page_dirty_buffers(struct page *page);
int __set_page_dirty_nobuffers(struct page *page);
int set_page_dirty_lock(struct page *page);
int FASTCALL(set_page_dirty(struct page *page));
int clear_page_dirty_for_io(struct page *page);

/*
 * Prototype to add a shrinker callback for ageable caches.
 * 
 * These functions are passed a count `nr_to_scan' and a gfpmask.  They should
 * scan `nr_to_scan' objects, attempting to free them.
 *
 * The callback must the number of objects which remain in the cache.
 *
 * The callback will be passes nr_to_scan == 0 when the VM is querying the
 * cache size, so a fastpath for that case is appropriate.
 */
typedef int (*shrinker_t)(int nr_to_scan, unsigned int gfp_mask);

/*
 * Add an aging callback.  The int is the number of 'seeks' it takes
 * to recreate one of the objects that these functions age.
 */

#define DEFAULT_SEEKS 2
struct shrinker;
extern struct shrinker *set_shrinker(int, shrinker_t);
extern void remove_shrinker(struct shrinker *shrinker);

static inline struct address_space * page_mapping(struct page * page)
{
	extern struct address_space swapper_space;
	struct address_space * mapping = NULL;

	if (unlikely(PageSwapCache(page)))
		mapping = &swapper_space;
	else if (!PageAnon(page))
		mapping = page->mapping;
	return mapping;
}

extern long do_mprotect(struct mm_struct *mm, unsigned long start, 
			size_t len, unsigned long prot);

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
extern void memmap_init_zone(struct page *, unsigned long, int,
	unsigned long, unsigned long);
extern void mem_init(void);
extern void show_mem(void);
extern void si_meminfo(struct sysinfo * val);
extern void si_meminfo_node(struct sysinfo *val, int nid);

/* mmap.c */
extern void insert_vm_struct(struct mm_struct *, struct vm_area_struct *);
extern void __vma_link_rb(struct mm_struct *, struct vm_area_struct *,
	struct rb_node **, struct rb_node *);
extern void exit_mmap(struct mm_struct *);

extern unsigned long get_unmapped_area(struct file *, unsigned long, unsigned long, unsigned long, unsigned long);

extern unsigned long __do_mmap_pgoff(struct mm_struct *mm,
	struct file *file, unsigned long addr,
	unsigned long len, unsigned long prot,
	unsigned long flag, unsigned long pgoff);

static inline unsigned long do_mmap_pgoff(struct file *file, unsigned long addr,
	unsigned long len, unsigned long prot,
	unsigned long flag, unsigned long pgoff)
{
	return __do_mmap_pgoff(current->mm, file, addr, len,
		               prot, flag, pgoff);
}

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

/* vma merging helpers */
static inline void
__vma_unlink(struct mm_struct *mm, struct vm_area_struct *vma,
		struct vm_area_struct *prev)
{
	prev->vm_next = vma->vm_next;
	rb_erase(&vma->vm_rb, &mm->mm_rb);
	if (mm->mmap_cache == vma)
		mm->mmap_cache = prev;
}

extern void __remove_shared_vm_struct(struct vm_area_struct *, struct inode *,
				      struct address_space *);

/*
 * If the vma has a ->close operation then the driver probably needs to release
 * per-vma resources, so we don't attempt to merge those.
 */
#define VM_SPECIAL (VM_IO | VM_DONTCOPY | VM_DONTEXPAND | VM_RESERVED)

static inline int is_mergeable_vma(struct vm_area_struct *vma,
				   struct file *file,
				   unsigned long vm_flags,
				   unsigned long pgoff,
				   anon_vma_t ** anon_vma_cache)
{
	if (vma->vm_ops && vma->vm_ops->close)
		return 0;
	if (vma->vm_file != file)
		return 0;
	if (vma->vm_pgoff != pgoff)
		return 0;
	if (vma->vm_private_data)
		return 0;
	if (vma->vm_flags != vm_flags) {
		/*
		 * If the only difference between two adiacent
		 * vmas is the page protection we try to
		 * share the same anon_vma to maximize the
		 * merging in mprotect.
		 */
		if (anon_vma_cache && !*anon_vma_cache)
			*anon_vma_cache = vma->anon_vma;
		return 0;
	}
	return 1;
}

static inline int is_mergeable_anon_vma(struct vm_area_struct *prev,
					struct vm_area_struct *next)
{
	return ((!next->anon_vma || !prev->anon_vma) ||
		(next->anon_vma == prev->anon_vma));
}

/* filemap.c */
extern unsigned long page_unuse(struct page *);
extern void truncate_inode_pages(struct address_space *, loff_t);

/* generic vm_area_ops exported for stackable file systems */
struct page *filemap_nopage(struct vm_area_struct *, unsigned long, int *);

/* mm/page-writeback.c */
int write_one_page(struct page *page, int wait);

/* readahead.c */
#define VM_MAX_READAHEAD	128	/* kbytes */
#define VM_MIN_READAHEAD	16	/* kbytes (includes current page) */

int do_page_cache_readahead(struct address_space *mapping, struct file *filp,
			unsigned long offset, unsigned long nr_to_read);
int force_page_cache_readahead(struct address_space *mapping, struct file *filp,
			unsigned long offset, unsigned long nr_to_read);
void page_cache_readahead(struct address_space *mapping, 
			  struct file_ra_state *ra,
			  struct file *filp,
			  unsigned long offset);
void handle_ra_miss(struct address_space *mapping, 
		    struct file_ra_state *ra, pgoff_t offset);
unsigned long max_sane_readahead(unsigned long nr);

/* Do stack extension */
extern int expand_stack(struct vm_area_struct * vma, unsigned long address);

/* Look up the first VMA which satisfies  addr < vm_end,  NULL if none. */
extern struct vm_area_struct * find_vma(struct mm_struct * mm, unsigned long addr);
extern struct vm_area_struct * find_vma_prev(struct mm_struct * mm, unsigned long addr,
					     struct vm_area_struct **pprev);
extern int split_vma(struct mm_struct * mm, struct vm_area_struct * vma,
		     unsigned long addr, int new_below);

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

extern unsigned int nr_used_zone_pages(void);

extern struct page * vmalloc_to_page(void *addr);
extern struct page * follow_page(struct mm_struct *mm, unsigned long address,
		int write);
extern int remap_page_range(struct vm_area_struct *vma, unsigned long from,
		unsigned long to, unsigned long size, pgprot_t prot);

#ifndef CONFIG_DEBUG_PAGEALLOC
static inline void
kernel_map_pages(struct page *page, int numpages, int enable)
{
}
#endif

#ifndef CONFIG_ARCH_GATE_AREA
extern struct vm_area_struct *get_gate_vma(struct task_struct *tsk);
int in_gate_area(struct task_struct *task, unsigned long addr);
#endif

#endif /* __KERNEL__ */
#endif /* _LINUX_MM_H */
