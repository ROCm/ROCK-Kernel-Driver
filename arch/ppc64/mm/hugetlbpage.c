/*
 * PPC64 (POWER4) Huge TLB Page Support for Kernel.
 *
 * Copyright (C) 2003 David Gibson, IBM Corporation.
 *
 * Based on the IA-32 version:
 * Copyright (C) 2002, Rohit Seth <rohit.seth@intel.com>
 */

#include <linux/init.h>
#include <linux/fs.h>
#include <linux/mm.h>
#include <linux/hugetlb.h>
#include <linux/pagemap.h>
#include <linux/smp_lock.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/err.h>
#include <linux/sysctl.h>
#include <asm/mman.h>
#include <asm/pgalloc.h>
#include <asm/tlb.h>
#include <asm/tlbflush.h>
#include <asm/mmu_context.h>
#include <asm/machdep.h>
#include <asm/cputable.h>
#include <asm/tlb.h>
#include <asm/rmap.h>

#include <linux/sysctl.h>

int htlbpage_max;

/* This lock protects the two counters and list below */
static spinlock_t htlbpage_lock = SPIN_LOCK_UNLOCKED;

static int htlbpage_free; /* = 0 */
static int htlbpage_total; /* = 0 */
static struct list_head hugepage_freelists[MAX_NUMNODES];

static void enqueue_huge_page(struct page *page)
{
	list_add(&page->lru,
		&hugepage_freelists[page_zone(page)->zone_pgdat->node_id]);
}

/* XXX make this a sysctl */
unsigned long largepage_roundrobin = 1;

static struct page *dequeue_huge_page(void)
{
	static int nid = 0;
	struct page *page = NULL;
	int i;

	if (!largepage_roundrobin)
		nid = numa_node_id();

	for (i = 0; i < numnodes; i++) {
		if (!list_empty(&hugepage_freelists[nid]))
			break;
		nid = (nid + 1) % numnodes;
	}

	if (!list_empty(&hugepage_freelists[nid])) {
		page = list_entry(hugepage_freelists[nid].next, struct page, lru);
		list_del(&page->lru);
	}

	if (largepage_roundrobin)
		nid = (nid + 1) % numnodes;

	return page;
}

static struct page *alloc_fresh_huge_page(void)
{
	static int nid = 0;
	struct page *page;

	page = alloc_pages_node(nid, GFP_HIGHUSER, HUGETLB_PAGE_ORDER);
	if (!page)
		return NULL;

	nid = page_zone(page)->zone_pgdat->node_id;
	nid = (nid + 1) % numnodes;
	return page;
}

/* HugePTE layout:
 *
 * 31 30 ... 15 14 13 12 10 9  8  7   6    5    4    3    2    1    0
 * PFN>>12..... -  -  -  -  -  -  HASH_IX....   2ND  HASH RW   -    HG=1
 */

#define HUGEPTE_SHIFT	15
#define _HUGEPAGE_PFN		0xffff8000
#define _HUGEPAGE_BAD		0x00007f00
#define _HUGEPAGE_HASHPTE	0x00000008
#define _HUGEPAGE_SECONDARY	0x00000010
#define _HUGEPAGE_GROUP_IX	0x000000e0
#define _HUGEPAGE_HPTEFLAGS	(_HUGEPAGE_HASHPTE | _HUGEPAGE_SECONDARY | \
				 _HUGEPAGE_GROUP_IX)
#define _HUGEPAGE_RW		0x00000004

typedef struct {unsigned int val;} hugepte_t;
#define hugepte_val(hugepte)	((hugepte).val)
#define __hugepte(x)		((hugepte_t) { (x) } )
#define hugepte_pfn(x)		\
	((unsigned long)(hugepte_val(x)>>HUGEPTE_SHIFT) << HUGETLB_PAGE_ORDER)
#define mk_hugepte(page,wr)	__hugepte( \
	((page_to_pfn(page)>>HUGETLB_PAGE_ORDER) << HUGEPTE_SHIFT ) \
	| (!!(wr) * _HUGEPAGE_RW) | _PMD_HUGEPAGE )

#define hugepte_bad(x)	( !(hugepte_val(x) & _PMD_HUGEPAGE) || \
			  (hugepte_val(x) & _HUGEPAGE_BAD) )
#define hugepte_page(x)	pfn_to_page(hugepte_pfn(x))
#define hugepte_none(x)	(!(hugepte_val(x) & _HUGEPAGE_PFN))


static void free_huge_page(struct page *page);
static void flush_hash_hugepage(mm_context_t context, unsigned long ea,
				hugepte_t pte, int local);

static inline unsigned int hugepte_update(hugepte_t *p, unsigned int clr,
					  unsigned int set)
{
	unsigned int old, tmp;

	__asm__ __volatile__(
	"1:	lwarx	%0,0,%3		# pte_update\n\
	andc	%1,%0,%4 \n\
	or	%1,%1,%5 \n\
	stwcx.	%1,0,%3 \n\
	bne-	1b"
	: "=&r" (old), "=&r" (tmp), "=m" (*p)
	: "r" (p), "r" (clr), "r" (set), "m" (*p)
	: "cc" );
	return old;
}

static inline void set_hugepte(hugepte_t *ptep, hugepte_t pte)
{
	hugepte_update(ptep, ~_HUGEPAGE_HPTEFLAGS,
		       hugepte_val(pte) & ~_HUGEPAGE_HPTEFLAGS);
}

static struct page *alloc_hugetlb_page(void)
{
	int i;
	struct page *page;

	spin_lock(&htlbpage_lock);
	page = dequeue_huge_page();
	if (!page) {
		spin_unlock(&htlbpage_lock);
		return NULL;
	}

	htlbpage_free--;
	spin_unlock(&htlbpage_lock);
	set_page_count(page, 1);
	page->lru.prev = (void *)free_huge_page;
	for (i = 0; i < (HPAGE_SIZE/PAGE_SIZE); ++i)
		clear_highpage(&page[i]);
	return page;
}

static hugepte_t *hugepte_alloc(struct mm_struct *mm, unsigned long addr)
{
	pgd_t *pgd;
	pmd_t *pmd = NULL;

	BUG_ON(!in_hugepage_area(mm->context, addr));

	pgd = pgd_offset(mm, addr);
	pmd = pmd_alloc(mm, pgd, addr);

	/* We shouldn't find a (normal) PTE page pointer here */
	BUG_ON(!pmd_none(*pmd) && !pmd_hugepage(*pmd));
	
	return (hugepte_t *)pmd;
}

static hugepte_t *hugepte_offset(struct mm_struct *mm, unsigned long addr)
{
	pgd_t *pgd;
	pmd_t *pmd = NULL;

	BUG_ON(!in_hugepage_area(mm->context, addr));

	pgd = pgd_offset(mm, addr);
	if (pgd_none(*pgd))
		return NULL;

	pmd = pmd_offset(pgd, addr);

	/* We shouldn't find a (normal) PTE page pointer here */
	BUG_ON(!pmd_none(*pmd) && !pmd_hugepage(*pmd));

	return (hugepte_t *)pmd;
}

static void setup_huge_pte(struct mm_struct *mm, struct page *page,
			   hugepte_t *ptep, int write_access)
{
	hugepte_t entry;
	int i;

	mm->rss += (HPAGE_SIZE / PAGE_SIZE);
	entry = mk_hugepte(page, write_access);
	for (i = 0; i < HUGEPTE_BATCH_SIZE; i++)
		set_hugepte(ptep+i, entry);
}

static void teardown_huge_pte(hugepte_t *ptep)
{
	int i;

	for (i = 0; i < HUGEPTE_BATCH_SIZE; i++)
		pmd_clear((pmd_t *)(ptep+i));
}

/*
 * This function checks for proper alignment of input addr and len parameters.
 */
int is_aligned_hugepage_range(unsigned long addr, unsigned long len)
{
	if (len & ~HPAGE_MASK)
		return -EINVAL;
	if (addr & ~HPAGE_MASK)
		return -EINVAL;
	if (! (within_hugepage_low_range(addr, len)
	       || within_hugepage_high_range(addr, len)) )
		return -EINVAL;
	return 0;
}

static void do_slbia(void *unused)
{
	asm volatile ("isync; slbia; isync":::"memory");
}

static int prepare_low_seg_for_htlb(struct mm_struct *mm, unsigned long seg)
{
	unsigned long start = seg << SID_SHIFT;
	unsigned long end = (seg+1) << SID_SHIFT;
	struct vm_area_struct *vma;
	unsigned long addr;
	struct mmu_gather *tlb;

	BUG_ON(seg >= 16);

	/* Check no VMAs are in the region */
	vma = find_vma(mm, start);
	if (vma && (vma->vm_start < end))
		return -EBUSY;

	/* Clean up any leftover PTE pages in the region */
	spin_lock(&mm->page_table_lock);
	tlb = tlb_gather_mmu(mm, 0);
	for (addr = start; addr < end; addr += PMD_SIZE) {
		pgd_t *pgd = pgd_offset(mm, addr);
		pmd_t *pmd;
		struct page *page;
		pte_t *pte;
		int i;

		if (pgd_none(*pgd))
			continue;
		pmd = pmd_offset(pgd, addr);
		if (!pmd || pmd_none(*pmd))
			continue;
		if (pmd_bad(*pmd)) {
			pmd_ERROR(*pmd);
			pmd_clear(pmd);
			continue;
		}
		pte = (pte_t *)pmd_page_kernel(*pmd);
		/* No VMAs, so there should be no PTEs, check just in case. */
		for (i = 0; i < PTRS_PER_PTE; i++) {
			BUG_ON(!pte_none(*pte));
			pte++;
		}
		page = pmd_page(*pmd);
		pmd_clear(pmd);
		pgtable_remove_rmap(page);
		pte_free_tlb(tlb, page);
	}
	tlb_finish_mmu(tlb, start, end);
	spin_unlock(&mm->page_table_lock);

	return 0;
}

static int open_low_hpage_segs(struct mm_struct *mm, u16 newsegs)
{
	unsigned long i;

	newsegs &= ~(mm->context.htlb_segs);
	if (! newsegs)
		return 0; /* The segments we want are already open */

	for (i = 0; i < 16; i++)
		if ((1 << i) & newsegs)
			if (prepare_low_seg_for_htlb(mm, i) != 0)
				return -EBUSY;

	mm->context.htlb_segs |= newsegs;
	/* the context change must make it to memory before the slbia,
	 * so that further SLB misses do the right thing. */
	mb();
	on_each_cpu(do_slbia, NULL, 0, 1);

	return 0;
}

int prepare_hugepage_range(unsigned long addr, unsigned long len)
{
	if (within_hugepage_high_range(addr, len))
		return 0;
	else if ((addr < 0x100000000) && ((addr+len) < 0x100000000)) {
		int err;
		/* Yes, we need both tests, in case addr+len overflows
		 * 64-bit arithmetic */
		err = open_low_hpage_segs(current->mm,
					  LOW_ESID_MASK(addr, len));
		if (err)
			printk(KERN_DEBUG "prepare_hugepage_range(%lx, %lx)"
			       " failed (segs: 0x%04hx)\n", addr, len,
			       LOW_ESID_MASK(addr, len));
		return err;
	}

	return -EINVAL;
}

int copy_hugetlb_page_range(struct mm_struct *dst, struct mm_struct *src,
			struct vm_area_struct *vma)
{
	hugepte_t *src_pte, *dst_pte, entry;
	struct page *ptepage;
	unsigned long addr = vma->vm_start;
	unsigned long end = vma->vm_end;

	while (addr < end) {
		BUG_ON(! in_hugepage_area(src->context, addr));
		BUG_ON(! in_hugepage_area(dst->context, addr));

		dst_pte = hugepte_alloc(dst, addr);
		if (!dst_pte)
			return -ENOMEM;

		src_pte = hugepte_offset(src, addr);
		entry = *src_pte;
		
		if ((addr % HPAGE_SIZE) == 0) {
			/* This is the first hugepte in a batch */
			ptepage = hugepte_page(entry);
			get_page(ptepage);
			dst->rss += (HPAGE_SIZE / PAGE_SIZE);
		}
		set_hugepte(dst_pte, entry);


		addr += PMD_SIZE;
	}
	return 0;
}

int
follow_hugetlb_page(struct mm_struct *mm, struct vm_area_struct *vma,
		    struct page **pages, struct vm_area_struct **vmas,
		    unsigned long *position, int *length, int i)
{
	unsigned long vpfn, vaddr = *position;
	int remainder = *length;

	WARN_ON(!is_vm_hugetlb_page(vma));

	vpfn = vaddr/PAGE_SIZE;
	while (vaddr < vma->vm_end && remainder) {
		BUG_ON(!in_hugepage_area(mm->context, vaddr));

		if (pages) {
			hugepte_t *pte;
			struct page *page;

			pte = hugepte_offset(mm, vaddr);

			/* hugetlb should be locked, and hence, prefaulted */
			WARN_ON(!pte || hugepte_none(*pte));

			page = &hugepte_page(*pte)[vpfn % (HPAGE_SIZE/PAGE_SIZE)];

			WARN_ON(!PageCompound(page));

			get_page(page);
			pages[i] = page;
		}

		if (vmas)
			vmas[i] = vma;

		vaddr += PAGE_SIZE;
		++vpfn;
		--remainder;
		++i;
	}

	*length = remainder;
	*position = vaddr;

	return i;
}

struct page *
follow_huge_addr(struct mm_struct *mm,
	struct vm_area_struct *vma, unsigned long address, int write)
{
	return NULL;
}

struct vm_area_struct *hugepage_vma(struct mm_struct *mm, unsigned long addr)
{
	return NULL;
}

int pmd_huge(pmd_t pmd)
{
	return pmd_hugepage(pmd);
}

struct page *
follow_huge_pmd(struct mm_struct *mm, unsigned long address,
		pmd_t *pmd, int write)
{
	struct page *page;

	BUG_ON(! pmd_hugepage(*pmd));

	page = hugepte_page(*(hugepte_t *)pmd);
	if (page) {
		page += ((address & ~HPAGE_MASK) >> PAGE_SHIFT);
		get_page(page);
	}
	return page;
}

static void free_huge_page(struct page *page)
{
	BUG_ON(page_count(page));
	BUG_ON(page->mapping);

	INIT_LIST_HEAD(&page->lru);

	spin_lock(&htlbpage_lock);
	enqueue_huge_page(page);
	htlbpage_free++;
	spin_unlock(&htlbpage_lock);
}

void huge_page_release(struct page *page)
{
	if (!put_page_testzero(page))
		return;

	free_huge_page(page);
}

void unmap_hugepage_range(struct vm_area_struct *vma,
			  unsigned long start, unsigned long end)
{
	struct mm_struct *mm = vma->vm_mm;
	unsigned long addr;
	hugepte_t *ptep;
	struct page *page;
	int local = 0;
	cpumask_t tmp;

	WARN_ON(!is_vm_hugetlb_page(vma));
	BUG_ON((start % HPAGE_SIZE) != 0);
	BUG_ON((end % HPAGE_SIZE) != 0);

	/* XXX are there races with checking cpu_vm_mask? - Anton */
	tmp = cpumask_of_cpu(smp_processor_id());
	if (cpus_equal(vma->vm_mm->cpu_vm_mask, tmp))
		local = 1;

	for (addr = start; addr < end; addr += HPAGE_SIZE) {
		hugepte_t pte;

		BUG_ON(!in_hugepage_area(mm->context, addr));

		ptep = hugepte_offset(mm, addr);
		if (!ptep || hugepte_none(*ptep))
			continue;

		pte = *ptep;
		page = hugepte_page(pte);
		teardown_huge_pte(ptep);
		
		if (hugepte_val(pte) & _HUGEPAGE_HASHPTE)
			flush_hash_hugepage(mm->context, addr,
					    pte, local);

		huge_page_release(page);
	}

	mm->rss -= (end - start) >> PAGE_SHIFT;
}

void zap_hugepage_range(struct vm_area_struct *vma,
			unsigned long start, unsigned long length)
{
	struct mm_struct *mm = vma->vm_mm;

	spin_lock(&mm->page_table_lock);
	unmap_hugepage_range(vma, start, start + length);
	spin_unlock(&mm->page_table_lock);
}

int hugetlb_prefault(struct address_space *mapping, struct vm_area_struct *vma)
{
	struct mm_struct *mm = current->mm;
	unsigned long addr;
	int ret = 0;

	WARN_ON(!is_vm_hugetlb_page(vma));
	BUG_ON((vma->vm_start % HPAGE_SIZE) != 0);
	BUG_ON((vma->vm_end % HPAGE_SIZE) != 0);

	spin_lock(&mm->page_table_lock);
	for (addr = vma->vm_start; addr < vma->vm_end; addr += HPAGE_SIZE) {
		unsigned long idx;
		hugepte_t *pte = hugepte_alloc(mm, addr);
		struct page *page;

		BUG_ON(!in_hugepage_area(mm->context, addr));

		if (!pte) {
			ret = -ENOMEM;
			goto out;
		}
		if (!hugepte_none(*pte))
			continue;

		idx = ((addr - vma->vm_start) >> HPAGE_SHIFT)
			+ (vma->vm_pgoff >> (HPAGE_SHIFT - PAGE_SHIFT));
		page = find_get_page(mapping, idx);
		if (!page) {
			/* charge the fs quota first */
			if (hugetlb_get_quota(mapping)) {
				ret = -ENOMEM;
				goto out;
			}
			page = alloc_hugetlb_page();
			if (!page) {
				hugetlb_put_quota(mapping);
				ret = -ENOMEM;
				goto out;
			}
			ret = add_to_page_cache(page, mapping, idx, GFP_ATOMIC);
			unlock_page(page);
			if (ret) {
				hugetlb_put_quota(mapping);
				free_huge_page(page);
				goto out;
			}
		}
		setup_huge_pte(mm, page, pte, vma->vm_flags & VM_WRITE);
	}
out:
	spin_unlock(&mm->page_table_lock);
	return ret;
}

/* Because we have an exclusive hugepage region which lies within the
 * normal user address space, we have to take special measures to make
 * non-huge mmap()s evade the hugepage reserved regions. */
unsigned long arch_get_unmapped_area(struct file *filp, unsigned long addr,
				     unsigned long len, unsigned long pgoff,
				     unsigned long flags)
{
	struct mm_struct *mm = current->mm;
	struct vm_area_struct *vma;
	unsigned long start_addr;

	if (len > TASK_SIZE)
		return -ENOMEM;

	if (addr) {
		addr = PAGE_ALIGN(addr);
		vma = find_vma(mm, addr);
		if (((TASK_SIZE - len) >= addr)
		    && (!vma || (addr+len) <= vma->vm_start)
		    && !is_hugepage_only_range(addr,len))
			return addr;
	}
	start_addr = addr = mm->free_area_cache;

full_search:
	vma = find_vma(mm, addr);
	while (TASK_SIZE - len >= addr) {
		BUG_ON(vma && (addr >= vma->vm_end));

		if (touches_hugepage_low_range(addr, len)) {
			addr = ALIGN(addr+1, 1<<SID_SHIFT);
			vma = find_vma(mm, addr);
			continue;
		}
		if (touches_hugepage_high_range(addr, len)) {
			addr = TASK_HPAGE_END;
			vma = find_vma(mm, addr);
			continue;
		}
		if (!vma || addr + len <= vma->vm_start) {
			/*
			 * Remember the place where we stopped the search:
			 */
			mm->free_area_cache = addr + len;
			return addr;
		}
		addr = vma->vm_end;
		vma = vma->vm_next;
	}

	/* Make sure we didn't miss any holes */
	if (start_addr != TASK_UNMAPPED_BASE) {
		start_addr = addr = TASK_UNMAPPED_BASE;
		goto full_search;
	}
	return -ENOMEM;
}

static unsigned long htlb_get_low_area(unsigned long len, u16 segmask)
{
	unsigned long addr = 0;
	struct vm_area_struct *vma;

	vma = find_vma(current->mm, addr);
	while (addr + len <= 0x100000000UL) {
		BUG_ON(vma && (addr >= vma->vm_end)); /* invariant */

		if (! __within_hugepage_low_range(addr, len, segmask)) {
			addr = ALIGN(addr+1, 1<<SID_SHIFT);
			vma = find_vma(current->mm, addr);
			continue;
		}

		if (!vma || (addr + len) <= vma->vm_start)
			return addr;
		addr = ALIGN(vma->vm_end, HPAGE_SIZE);
		/* Depending on segmask this might not be a confirmed
		 * hugepage region, so the ALIGN could have skipped
		 * some VMAs */
		vma = find_vma(current->mm, addr);
	}

	return -ENOMEM;
}

static unsigned long htlb_get_high_area(unsigned long len)
{
	unsigned long addr = TASK_HPAGE_BASE;
	struct vm_area_struct *vma;

	vma = find_vma(current->mm, addr);
	for (vma = find_vma(current->mm, addr);
	     addr + len <= TASK_HPAGE_END;
	     vma = vma->vm_next) {
		BUG_ON(vma && (addr >= vma->vm_end)); /* invariant */
		BUG_ON(! within_hugepage_high_range(addr, len));

		if (!vma || (addr + len) <= vma->vm_start)
			return addr;
		addr = ALIGN(vma->vm_end, HPAGE_SIZE);
		/* Because we're in a hugepage region, this alignment
		 * should not skip us over any VMAs */
	}

	return -ENOMEM;
}

unsigned long hugetlb_get_unmapped_area(struct file *file, unsigned long addr,
					unsigned long len, unsigned long pgoff,
					unsigned long flags)
{
	if (len & ~HPAGE_MASK)
		return -EINVAL;

	if (!(cur_cpu_spec->cpu_features & CPU_FTR_16M_PAGE))
		return -EINVAL;

	if (test_thread_flag(TIF_32BIT)) {
		int lastshift = 0;
		u16 segmask, cursegs = current->mm->context.htlb_segs;

		/* First see if we can do the mapping in the existing
		 * low hpage segments */
		addr = htlb_get_low_area(len, cursegs);
		if (addr != -ENOMEM)
			return addr;

		for (segmask = LOW_ESID_MASK(0x100000000UL-len, len);
		     ! lastshift; segmask >>=1) {
			if (segmask & 1)
				lastshift = 1;

			addr = htlb_get_low_area(len, cursegs | segmask);
			if ((addr != -ENOMEM)
			    && open_low_hpage_segs(current->mm, segmask) == 0)
				return addr;
		}
		printk(KERN_DEBUG "hugetlb_get_unmapped_area() unable to open"
		       " enough segments\n");
		return -ENOMEM;
	} else {
		return htlb_get_high_area(len);
	}
}

static inline unsigned long computeHugeHptePP(unsigned int hugepte)
{
	unsigned long flags = 0x2;

	if (! (hugepte & _HUGEPAGE_RW))
		flags |= 0x1;
	return flags;
}

int hash_huge_page(struct mm_struct *mm, unsigned long access,
		   unsigned long ea, unsigned long vsid, int local)
{
	hugepte_t *ptep;
	unsigned long va, vpn;
	int is_write;
	hugepte_t old_pte, new_pte;
	unsigned long hpteflags, prpn, flags;
	long slot;

	/* We have to find the first hugepte in the batch, since
	 * that's the one that will store the HPTE flags */
	ea &= HPAGE_MASK;
	ptep = hugepte_offset(mm, ea);

	/* Search the Linux page table for a match with va */
	va = (vsid << 28) | (ea & 0x0fffffff);
	vpn = va >> HPAGE_SHIFT;

	/*
	 * If no pte found or not present, send the problem up to
	 * do_page_fault
	 */
	if (unlikely(!ptep || hugepte_none(*ptep)))
		return 1;

	BUG_ON(hugepte_bad(*ptep));

	/* 
	 * Check the user's access rights to the page.  If access should be
	 * prevented then send the problem up to do_page_fault.
	 */
	is_write = access & _PAGE_RW;
	if (unlikely(is_write && !(hugepte_val(*ptep) & _HUGEPAGE_RW)))
		return 1;

	/*
	 * At this point, we have a pte (old_pte) which can be used to build
	 * or update an HPTE. There are 2 cases:
	 *
	 * 1. There is a valid (present) pte with no associated HPTE (this is 
	 *	the most common case)
	 * 2. There is a valid (present) pte with an associated HPTE. The
	 *	current values of the pp bits in the HPTE prevent access
	 *	because we are doing software DIRTY bit management and the
	 *	page is currently not DIRTY. 
	 */

	spin_lock_irqsave(&mm->page_table_lock, flags);

	old_pte = *ptep;
	new_pte = old_pte;

	hpteflags = computeHugeHptePP(hugepte_val(new_pte));

	/* Check if pte already has an hpte (case 2) */
	if (unlikely(hugepte_val(old_pte) & _HUGEPAGE_HASHPTE)) {
		/* There MIGHT be an HPTE for this pte */
		unsigned long hash, slot;

		hash = hpt_hash(vpn, 1);
		if (hugepte_val(old_pte) & _HUGEPAGE_SECONDARY)
			hash = ~hash;
		slot = (hash & htab_data.htab_hash_mask) * HPTES_PER_GROUP;
		slot += (hugepte_val(old_pte) & _HUGEPAGE_GROUP_IX) >> 5;

		if (ppc_md.hpte_updatepp(slot, hpteflags, va, 1, local) == -1)
			hugepte_val(old_pte) &= ~_HUGEPAGE_HPTEFLAGS;
	}

	if (likely(!(hugepte_val(old_pte) & _HUGEPAGE_HASHPTE))) {
		unsigned long hash = hpt_hash(vpn, 1);
		unsigned long hpte_group;

		prpn = hugepte_pfn(old_pte);

repeat:
		hpte_group = ((hash & htab_data.htab_hash_mask) *
			      HPTES_PER_GROUP) & ~0x7UL;

		/* Update the linux pte with the HPTE slot */
		hugepte_val(new_pte) &= ~_HUGEPAGE_HPTEFLAGS;
		hugepte_val(new_pte) |= _HUGEPAGE_HASHPTE;

		/* Add in WIMG bits */
		/* XXX We should store these in the pte */
		hpteflags |= _PAGE_COHERENT;

		slot = ppc_md.hpte_insert(hpte_group, va, prpn, 0,
					  hpteflags, 0, 1);

		/* Primary is full, try the secondary */
		if (unlikely(slot == -1)) {
			hugepte_val(new_pte) |= _HUGEPAGE_SECONDARY;
			hpte_group = ((~hash & htab_data.htab_hash_mask) *
				      HPTES_PER_GROUP) & ~0x7UL; 
			slot = ppc_md.hpte_insert(hpte_group, va, prpn,
						  1, hpteflags, 0, 1);
			if (slot == -1) {
				if (mftb() & 0x1)
					hpte_group = ((hash & htab_data.htab_hash_mask) * HPTES_PER_GROUP) & ~0x7UL;

				ppc_md.hpte_remove(hpte_group);
				goto repeat;
                        }
		}

		if (unlikely(slot == -2))
			panic("hash_huge_page: pte_insert failed\n");

		hugepte_val(new_pte) |= (slot<<5) & _HUGEPAGE_GROUP_IX;

		/* 
		 * No need to use ldarx/stdcx here because all who
		 * might be updating the pte will hold the
		 * page_table_lock or the hash_table_lock
		 * (we hold both)
		 */
		*ptep = new_pte;
	}

	spin_unlock_irqrestore(&mm->page_table_lock, flags);

	return 0;
}

static void flush_hash_hugepage(mm_context_t context, unsigned long ea,
				hugepte_t pte, int local)
{
	unsigned long vsid, vpn, va, hash, secondary, slot;

	BUG_ON(hugepte_bad(pte));
	BUG_ON(!in_hugepage_area(context, ea));

	vsid = get_vsid(context.id, ea);

	va = (vsid << 28) | (ea & 0x0fffffff);
	vpn = va >> LARGE_PAGE_SHIFT;
	hash = hpt_hash(vpn, 1);
	secondary = !!(hugepte_val(pte) & _HUGEPAGE_SECONDARY);
	if (secondary)
		hash = ~hash;
	slot = (hash & htab_data.htab_hash_mask) * HPTES_PER_GROUP;
	slot += (hugepte_val(pte) & _HUGEPAGE_GROUP_IX) >> 5;

	ppc_md.hpte_invalidate(slot, va, 1, local);
}

static void split_and_free_hugepage(struct page *page)
{
	int j;
	struct page *map;

	map = page;
	htlbpage_total--;
	for (j = 0; j < (HPAGE_SIZE / PAGE_SIZE); j++) {
		map->flags &= ~(1 << PG_locked | 1 << PG_error | 1 << PG_referenced |
				1 << PG_dirty | 1 << PG_active | 1 << PG_reserved |
				1 << PG_private | 1<< PG_writeback);
		set_page_count(map, 0);
		map++;
	}
	set_page_count(page, 1);
	__free_pages(page, HUGETLB_PAGE_ORDER);
}

int set_hugetlb_mem_size(int count)
{
	int lcount;
	struct page *page;

	if (!(cur_cpu_spec->cpu_features & CPU_FTR_16M_PAGE))
		return 0;
	
	if (count < 0)
		lcount = count;
	else
		lcount = count - htlbpage_total;

	if (lcount == 0)
		return htlbpage_total;
	if (lcount > 0) {	/* Increase the mem size. */
		while (lcount--) {
			page = alloc_fresh_huge_page();
			if (page == NULL)
				break;
			spin_lock(&htlbpage_lock);
			enqueue_huge_page(page);
			htlbpage_free++;
			htlbpage_total++;
			spin_unlock(&htlbpage_lock);
		}
		return htlbpage_total;
	}
	/* Shrink the memory size. */
	while (lcount++) {
		page = alloc_hugetlb_page();
		if (page == NULL)
			break;
		spin_lock(&htlbpage_lock);
		split_and_free_hugepage(page);
		spin_unlock(&htlbpage_lock);
	}
	return htlbpage_total;
}

int hugetlb_sysctl_handler(ctl_table *table, int write,
		struct file *file, void *buffer, size_t *length)
{
	proc_dointvec(table, write, file, buffer, length);
	htlbpage_max = set_hugetlb_mem_size(htlbpage_max);
	return 0;
}

static int __init hugetlb_setup(char *s)
{
	if (sscanf(s, "%d", &htlbpage_max) <= 0)
		htlbpage_max = 0;
	return 1;
}
__setup("hugepages=", hugetlb_setup);

static int __init hugetlb_init(void)
{
	int i;
	struct page *page;

	if (cur_cpu_spec->cpu_features & CPU_FTR_16M_PAGE) {
		for (i = 0; i < MAX_NUMNODES; ++i)
			INIT_LIST_HEAD(&hugepage_freelists[i]);

		for (i = 0; i < htlbpage_max; ++i) {
			page = alloc_fresh_huge_page();
			if (!page)
				break;
			spin_lock(&htlbpage_lock);
			enqueue_huge_page(page);
			spin_unlock(&htlbpage_lock);
		}
		htlbpage_max = htlbpage_free = htlbpage_total = i;
		printk(KERN_INFO "Total HugeTLB memory allocated, %d\n",
		       htlbpage_free);
	} else {
		htlbpage_max = 0;
		printk(KERN_INFO "CPU does not support HugeTLB\n");
	}

	return 0;
}
module_init(hugetlb_init);

int hugetlb_report_meminfo(char *buf)
{
	return sprintf(buf,
			"HugePages_Total: %5d\n"
			"HugePages_Free:  %5d\n"
			"Hugepagesize:    %5lu kB\n",
			htlbpage_total,
			htlbpage_free,
			HPAGE_SIZE/1024);
}

/* This is advisory only, so we can get away with accesing
 * htlbpage_free without taking the lock. */
int is_hugepage_mem_enough(size_t size)
{
	return (size + ~HPAGE_MASK)/HPAGE_SIZE <= htlbpage_free;
}

/* Return the number pages of memory we physically have, in PAGE_SIZE units. */
unsigned long hugetlb_total_pages(void)
{
	return htlbpage_total * (HPAGE_SIZE / PAGE_SIZE);
}
EXPORT_SYMBOL(hugetlb_total_pages);

/*
 * We cannot handle pagefaults against hugetlb pages at all.  They cause
 * handle_mm_fault() to try to instantiate regular-sized pages in the
 * hugegpage VMA.  do_page_fault() is supposed to trap this, so BUG is we get
 * this far.
 */
static struct page *hugetlb_nopage(struct vm_area_struct *vma,
				unsigned long address, int *unused)
{
	BUG();
	return NULL;
}

struct vm_operations_struct hugetlb_vm_ops = {
	.nopage = hugetlb_nopage,
};
