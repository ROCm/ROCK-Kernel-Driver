/* 
 * Copyright (C) 2000, 2001, 2002 Jeff Dike (jdike@karaya.com)
 * Licensed under the GPL
 */

#include "linux/config.h"
#include "linux/module.h"
#include "linux/types.h"
#include "linux/mm.h"
#include "linux/fs.h"
#include "linux/init.h"
#include "linux/bootmem.h"
#include "linux/swap.h"
#include "linux/slab.h"
#include "linux/vmalloc.h"
#include "linux/highmem.h"
#include "asm/page.h"
#include "asm/pgtable.h"
#include "asm/pgalloc.h"
#include "asm/bitops.h"
#include "asm/uaccess.h"
#include "asm/tlb.h"
#include "user_util.h"
#include "kern_util.h"
#include "mem_user.h"
#include "mem.h"
#include "kern.h"
#include "init.h"
#include "os.h"
#include "mode_kern.h"
#include "uml_uaccess.h"

/* Changed during early boot */
pgd_t swapper_pg_dir[1024];
unsigned long high_physmem;
unsigned long vm_start;
unsigned long vm_end;
unsigned long highmem;
unsigned long *empty_zero_page = NULL;
unsigned long *empty_bad_page = NULL;

/* Not modified */
const char bad_pmd_string[] = "Bad pmd in pte_alloc: %08lx\n";

extern char __init_begin, __init_end;
extern long physmem_size;

/* Not changed by UML */
DEFINE_PER_CPU(struct mmu_gather, mmu_gathers);

/* Changed during early boot */
int kmalloc_ok = 0;

#define NREGIONS (phys_region_index(0xffffffff) - phys_region_index(0x0) + 1)
struct mem_region *regions[NREGIONS] = { [ 0 ... NREGIONS - 1 ] = NULL };
#define REGION_SIZE ((0xffffffff & ~REGION_MASK) + 1)

/* Changed during early boot */
static unsigned long brk_end;

static void map_cb(void *unused)
{
	map_memory(brk_end, __pa(brk_end), uml_reserved - brk_end, 1, 1, 0);
}

void unmap_physmem(void)
{
	os_unmap_memory((void *) brk_end, uml_reserved - brk_end);
}

extern char __binary_start;

void mem_init(void)
{
	unsigned long start;

	max_low_pfn = (high_physmem - uml_physmem) >> PAGE_SHIFT;
#ifdef CONFIG_HIGHMEM
	highmem_start_page = phys_page(__pa(high_physmem));
#endif

        /* clear the zero-page */
        memset((void *) empty_zero_page, 0, PAGE_SIZE);

	/* Map in the area just after the brk now that kmalloc is about
	 * to be turned on.
	 */
	brk_end = (unsigned long) UML_ROUND_UP(sbrk(0));
	map_cb(NULL);
	initial_thread_cb(map_cb, NULL);
	free_bootmem(__pa(brk_end), uml_reserved - brk_end);
	uml_reserved = brk_end;

	/* Fill in any hole at the start of the binary */
	start = (unsigned long) &__binary_start;
	if(uml_physmem != start){
		map_memory(uml_physmem, __pa(uml_physmem), start - uml_physmem,
			   1, 1, 0);
	}

	/* this will put all low memory onto the freelists */
	totalram_pages = free_all_bootmem();
	totalhigh_pages = highmem >> PAGE_SHIFT;
	totalram_pages += totalhigh_pages;
	num_physpages = totalram_pages;
	max_mapnr = totalram_pages;
	max_pfn = totalram_pages;
	printk(KERN_INFO "Memory: %luk available\n", 
	       (unsigned long) nr_free_pages() << (PAGE_SHIFT-10));
	kmalloc_ok = 1;
}

/* Changed during early boot */
static unsigned long kmem_top = 0;

unsigned long get_kmem_end(void)
{
	if(kmem_top == 0)
		kmem_top = CHOOSE_MODE(kmem_end_tt, kmem_end_skas);
	return(kmem_top);
}

void set_kmem_end(unsigned long new)
{
	kmem_top = new;
}

#ifdef CONFIG_HIGHMEM
/* Changed during early boot */
pte_t *kmap_pte;
pgprot_t kmap_prot;

EXPORT_SYMBOL(kmap_prot);
EXPORT_SYMBOL(kmap_pte);

#define kmap_get_fixmap_pte(vaddr)					\
	pte_offset_kernel(pmd_offset(pgd_offset_k(vaddr), (vaddr)), (vaddr))

void __init kmap_init(void)
{
	unsigned long kmap_vstart;

	/* cache the first kmap pte */
	kmap_vstart = __fix_to_virt(FIX_KMAP_BEGIN);
	kmap_pte = kmap_get_fixmap_pte(kmap_vstart);

	kmap_prot = PAGE_KERNEL;
}
#endif /* CONFIG_HIGHMEM */

static void __init fixrange_init(unsigned long start, unsigned long end, 
				 pgd_t *pgd_base)
{
	pgd_t *pgd;
	pmd_t *pmd;
	pte_t *pte;
	int i, j;
	unsigned long vaddr;

	vaddr = start;
	i = pgd_index(vaddr);
	j = pmd_index(vaddr);
	pgd = pgd_base + i;

	for ( ; (i < PTRS_PER_PGD) && (vaddr < end); pgd++, i++) {
		pmd = (pmd_t *)pgd;
		for (; (j < PTRS_PER_PMD) && (vaddr != end); pmd++, j++) {
			if (pmd_none(*pmd)) {
				pte = (pte_t *) alloc_bootmem_low_pages(PAGE_SIZE);
				set_pmd(pmd, __pmd(_KERNPG_TABLE + 
						   (unsigned long) __pa(pte)));
				if (pte != pte_offset_kernel(pmd, 0))
					BUG();
			}
			vaddr += PMD_SIZE;
		}
		j = 0;
	}
}

int init_maps(struct mem_region *region)
{
	struct page *p, *map;
	int i, n, len;

	if(region == &physmem_region){
		region->mem_map = mem_map;
		return(0);
	}
	else if(region->mem_map != NULL) return(0);

	n = region->len >> PAGE_SHIFT;
	len = n * sizeof(struct page);
	if(kmalloc_ok){
		map = kmalloc(len, GFP_KERNEL);
		if(map == NULL) map = vmalloc(len);
	}
	else map = alloc_bootmem_low_pages(len);

	if(map == NULL)
		return(-ENOMEM);
	for(i = 0; i < n; i++){
		p = &map[i];
		set_page_count(p, 0);
		SetPageReserved(p);
		INIT_LIST_HEAD(&p->list);
	}
	region->mem_map = map;
	return(0);
}

DECLARE_MUTEX(regions_sem);

static int setup_one_range(int fd, char *driver, unsigned long start, 
			   unsigned long pfn, int len, 
			   struct mem_region *region)
{
	int i;

	down(&regions_sem);
	for(i = 0; i < NREGIONS; i++){
		if(regions[i] == NULL) break;		
	}
	if(i == NREGIONS){
		printk("setup_range : no free regions\n");
		i = -1;
		goto out;
	}

	if(fd == -1)
		fd = create_mem_file(len);

	if(region == NULL){
		region = alloc_bootmem_low_pages(sizeof(*region));
		if(region == NULL)
			panic("Failed to allocating mem_region");
	}

	*region = ((struct mem_region) { .driver 	= driver,
					 .start_pfn 	= pfn,
					 .start 	= start, 
					 .len 		= len, 
					 .fd 		= fd } );
	regions[i] = region;
 out:
	up(&regions_sem);
	return(i);
}

#ifdef CONFIG_HIGHMEM
static void init_highmem(void)
{
	pgd_t *pgd;
	pmd_t *pmd;
	pte_t *pte;
	unsigned long vaddr;

	/*
	 * Permanent kmaps:
	 */
	vaddr = PKMAP_BASE;
	fixrange_init(vaddr, vaddr + PAGE_SIZE*LAST_PKMAP, swapper_pg_dir);

	pgd = swapper_pg_dir + pgd_index(vaddr);
	pmd = pmd_offset(pgd, vaddr);
	pte = pte_offset_kernel(pmd, vaddr);
	pkmap_page_table = pte;

	kmap_init();
}

void setup_highmem(unsigned long len)
{
	struct mem_region *region;
	struct page *page, *map;
	unsigned long phys;
	int i, cur, index;

	phys = physmem_size;
	do {
		cur = min(len, (unsigned long) REGION_SIZE);
		i = setup_one_range(-1, NULL, -1, phys >> PAGE_SHIFT, cur, 
				    NULL);
		if(i == -1){
			printk("setup_highmem - setup_one_range failed\n");
			return;
		}
		region = regions[i];
		index = phys / PAGE_SIZE;
		region->mem_map = &mem_map[index];

		map = region->mem_map;
		for(i = 0; i < (cur >> PAGE_SHIFT); i++){
			page = &map[i];
			ClearPageReserved(page);
			set_bit(PG_highmem, &page->flags);
			set_page_count(page, 1);
			__free_page(page);
		}
		phys += cur;
		len -= cur;
	} while(len > 0);
}
#endif

void paging_init(void)
{
	struct mem_region *region;
	unsigned long zones_size[MAX_NR_ZONES], start, end, vaddr;
	int i, index;

	empty_zero_page = (unsigned long *) alloc_bootmem_low_pages(PAGE_SIZE);
	empty_bad_page = (unsigned long *) alloc_bootmem_low_pages(PAGE_SIZE);
	for(i=0;i<sizeof(zones_size)/sizeof(zones_size[0]);i++) 
		zones_size[i] = 0;
	zones_size[0] = (high_physmem >> PAGE_SHIFT) - 
		(uml_physmem >> PAGE_SHIFT);
	zones_size[2] = highmem >> PAGE_SHIFT;
	free_area_init(zones_size);
	start = phys_region_index(__pa(uml_physmem));
	end = phys_region_index(__pa(high_physmem - 1));
	for(i = start; i <= end; i++){
		region = regions[i];
		index = (region->start - uml_physmem) / PAGE_SIZE;
		region->mem_map = &mem_map[index];
		if(i > start) free_bootmem(__pa(region->start), region->len);
	}

	/*
	 * Fixed mappings, only the page table structure has to be
	 * created - mappings will be set by set_fixmap():
	 */
	vaddr = __fix_to_virt(__end_of_fixed_addresses - 1) & PMD_MASK;
	fixrange_init(vaddr, FIXADDR_TOP, swapper_pg_dir);

#ifdef CONFIG_HIGHMEM
	init_highmem();
	setup_highmem(highmem);
#endif
}

pte_t __bad_page(void)
{
	clear_page(empty_bad_page);
        return pte_mkdirty(mk_pte((struct page *) empty_bad_page, 
				  PAGE_SHARED));
}

/* This can't do anything because nothing in the kernel image can be freed
 * since it's not in kernel physical memory.
 */

void free_initmem(void)
{
}

#ifdef CONFIG_BLK_DEV_INITRD

void free_initrd_mem(unsigned long start, unsigned long end)
{
	if (start < end)
		printk ("Freeing initrd memory: %ldk freed\n", 
			(end - start) >> 10);
	for (; start < end; start += PAGE_SIZE) {
		ClearPageReserved(virt_to_page(start));
		set_page_count(virt_to_page(start), 1);
		free_page(start);
		totalram_pages++;
	}
}
	
#endif

void show_mem(void)
{
        int pfn, total = 0, reserved = 0;
        int shared = 0, cached = 0;
        int highmem = 0;
	struct page *page;

        printk("Mem-info:\n");
        show_free_areas();
        printk("Free swap:       %6dkB\n", nr_swap_pages<<(PAGE_SHIFT-10));
        pfn = max_mapnr;
        while(pfn-- > 0) {
		page = pfn_to_page(pfn);
                total++;
                if(PageHighMem(page))
                        highmem++;
                if(PageReserved(page))
                        reserved++;
                else if(PageSwapCache(page))
                        cached++;
                else if(page_count(page))
                        shared += page_count(page) - 1;
        }
        printk("%d pages of RAM\n", total);
        printk("%d pages of HIGHMEM\n", highmem);
        printk("%d reserved pages\n", reserved);
        printk("%d pages shared\n", shared);
        printk("%d pages swap cached\n", cached);
}

static int __init uml_mem_setup(char *line, int *add)
{
	char *retptr;
	physmem_size = memparse(line,&retptr);
	return 0;
}
__uml_setup("mem=", uml_mem_setup,
"mem=<Amount of desired ram>\n"
"    This controls how much \"physical\" memory the kernel allocates\n"
"    for the system. The size is specified as a number followed by\n"
"    one of 'k', 'K', 'm', 'M', which have the obvious meanings.\n"
"    This is not related to the amount of memory in the physical\n"
"    machine. It can be more, and the excess, if it's ever used, will\n"
"    just be swapped out.\n        Example: mem=64M\n\n"
);

struct page *arch_validate(struct page *page, int mask, int order)
{
	unsigned long addr, zero = 0;
	int i;

 again:
	if(page == NULL) return(page);
	if(PageHighMem(page)) return(page);

	addr = (unsigned long) page_address(page);
	for(i = 0; i < (1 << order); i++){
		current->thread.fault_addr = (void *) addr;
		if(__do_copy_to_user((void *) addr, &zero, 
				     sizeof(zero),
				     &current->thread.fault_addr,
				     &current->thread.fault_catcher)){
			if(!(mask & __GFP_WAIT)) return(NULL);
			else break;
		}
		addr += PAGE_SIZE;
	}
	if(i == (1 << order)) return(page);
	page = alloc_pages(mask, order);
	goto again;
}

DECLARE_MUTEX(vm_reserved_sem);
static struct list_head vm_reserved = LIST_HEAD_INIT(vm_reserved);

/* Static structures, linked in to the list in early boot */
static struct vm_reserved head = {
	.list 		= LIST_HEAD_INIT(head.list),
	.start 		= 0,
	.end 		= 0xffffffff
};

static struct vm_reserved tail = {
	.list 		= LIST_HEAD_INIT(tail.list),
	.start 		= 0,
	.end 		= 0xffffffff
};

void set_usable_vm(unsigned long start, unsigned long end)
{
	list_add(&head.list, &vm_reserved);
	list_add(&tail.list, &head.list);
	head.end = start;
	tail.start = end;
}

int reserve_vm(unsigned long start, unsigned long end, void *e)
	       
{
	struct vm_reserved *entry = e, *reserved, *prev;
	struct list_head *ele;
	int err;

	down(&vm_reserved_sem);
	list_for_each(ele, &vm_reserved){
		reserved = list_entry(ele, struct vm_reserved, list);
		if(reserved->start >= end) goto found;
	}
	panic("Reserved vm out of range");
 found:
	prev = list_entry(ele->prev, struct vm_reserved, list);
	if(prev->end > start)
		panic("Can't reserve vm");
	if(entry == NULL)
		entry = kmalloc(sizeof(*entry), GFP_KERNEL);
	if(entry == NULL){
		printk("reserve_vm : Failed to allocate entry\n");
		err = -ENOMEM;
		goto out;
	}
	*entry = ((struct vm_reserved) 
		{ .list 	= LIST_HEAD_INIT(entry->list),
		  .start 	= start,
		  .end 		= end });
	list_add(&entry->list, &prev->list);
	err = 0;
 out:
	up(&vm_reserved_sem);
	return(0);
}

unsigned long get_vm(unsigned long len)
{
	struct vm_reserved *this, *next;
	struct list_head *ele;
	unsigned long start;
	int err;
	
	down(&vm_reserved_sem);
	list_for_each(ele, &vm_reserved){
		this = list_entry(ele, struct vm_reserved, list);
		next = list_entry(ele->next, struct vm_reserved, list);
		if((this->start < next->start) && 
		   (this->end + len + PAGE_SIZE <= next->start))
			goto found;
	}
	up(&vm_reserved_sem);
	return(0);
 found:
	up(&vm_reserved_sem);
	start = (unsigned long) UML_ROUND_UP(this->end) + PAGE_SIZE;
	err = reserve_vm(start, start + len, NULL);
	if(err) return(0);
	return(start);
}

int nregions(void)
{
	return(NREGIONS);
}

void setup_range(int fd, char *driver, unsigned long start, unsigned long pfn,
		 unsigned long len, int need_vm, struct mem_region *region, 
		 void *reserved)
{
	int i, cur;

	do {
		cur = min(len, (unsigned long) REGION_SIZE);
		i = setup_one_range(fd, driver, start, pfn, cur, region);
		region = regions[i];
		if(need_vm && setup_region(region, reserved)){
			kfree(region);
			regions[i] = NULL;
			return;
		}
		start += cur;
		if(pfn != -1) pfn += cur;
		len -= cur;
	} while(len > 0);
}

struct iomem {
	char *name;
	int fd;
	unsigned long size;
};

/* iomem regions can only be added on the command line at the moment.  
 * Locking will be needed when they can be added via mconsole.
 */

struct iomem iomem_regions[NREGIONS] = { [ 0 ... NREGIONS - 1 ] =
					 { .name  	= NULL,
					   .fd  	= -1,
					   .size 	= 0 } };

int num_iomem_regions = 0;

void add_iomem(char *name, int fd, unsigned long size)
{
	if(num_iomem_regions == sizeof(iomem_regions)/sizeof(iomem_regions[0]))
		return;
	size = (size + PAGE_SIZE - 1) & PAGE_MASK;
	iomem_regions[num_iomem_regions++] = 
		((struct iomem) { .name 	= name,
				  .fd 		= fd,
				  .size 	= size } );
}

int setup_iomem(void)
{
	struct iomem *iomem;
	int i;

	for(i = 0; i < num_iomem_regions; i++){
		iomem = &iomem_regions[i];
		setup_range(iomem->fd, iomem->name, -1, -1, iomem->size, 1, 
			    NULL, NULL);
	}
	return(0);
}

__initcall(setup_iomem);

#define PFN_UP(x)	(((x) + PAGE_SIZE-1) >> PAGE_SHIFT)
#define PFN_DOWN(x)	((x) >> PAGE_SHIFT)

/* Changed during early boot */
static struct mem_region physmem_region;
static struct vm_reserved physmem_reserved;

void setup_physmem(unsigned long start, unsigned long reserve_end,
		   unsigned long len)
{
	struct mem_region *region = &physmem_region;
	struct vm_reserved *reserved = &physmem_reserved;
	unsigned long cur, pfn = 0;
	int do_free = 1, bootmap_size;

	do {
		cur = min(len, (unsigned long) REGION_SIZE);
		if(region == NULL) 
			region = alloc_bootmem_low_pages(sizeof(*region));
		if(reserved == NULL) 
			reserved = alloc_bootmem_low_pages(sizeof(*reserved));
		if((region == NULL) || (reserved == NULL))
			panic("Couldn't allocate physmem region or vm "
			      "reservation\n");
		setup_range(-1, NULL, start, pfn, cur, 1, region, reserved);

		if(do_free){
			unsigned long reserve = reserve_end - start;
			int pfn = PFN_UP(__pa(reserve_end));
			int delta = (len - reserve) >> PAGE_SHIFT;

			bootmap_size = init_bootmem(pfn, pfn + delta);
			free_bootmem(__pa(reserve_end) + bootmap_size,
				     cur - bootmap_size - reserve);
			do_free = 0;
		}
		start += cur;
		pfn += cur >> PAGE_SHIFT;
		len -= cur;
		region = NULL;
		reserved = NULL;
	} while(len > 0);
}

struct mem_region *phys_region(unsigned long phys)
{
	unsigned int n = phys_region_index(phys);

	if(regions[n] == NULL) 
		panic("Physical address in uninitialized region");
	return(regions[n]);
}

unsigned long phys_offset(unsigned long phys)
{
	return(phys_addr(phys));
}

struct page *phys_mem_map(unsigned long phys)
{
	return((struct page *) phys_region(phys)->mem_map);
}

struct page *pte_mem_map(pte_t pte)
{
	return(phys_mem_map(pte_val(pte)));
}

struct mem_region *page_region(struct page *page, int *index_out)
{
	int i;
	struct mem_region *region;
	struct page *map;

	for(i = 0; i < NREGIONS; i++){
		region = regions[i];
		if(region == NULL) continue;
		map = region->mem_map;
		if((page >= map) && (page < &map[region->len >> PAGE_SHIFT])){
			if(index_out != NULL) *index_out = i;
			return(region);
		}
	}
	panic("No region found for page");
	return(NULL);
}

unsigned long page_to_pfn(struct page *page)
{
	struct mem_region *region = page_region(page, NULL);

	return(region->start_pfn + (page - (struct page *) region->mem_map));
}

struct mem_region *pfn_to_region(unsigned long pfn, int *index_out)
{
	struct mem_region *region;
	int i;

	for(i = 0; i < NREGIONS; i++){
		region = regions[i];
		if(region == NULL)
			continue;

		if((region->start_pfn <= pfn) &&
		   (region->start_pfn + (region->len >> PAGE_SHIFT) > pfn)){
			if(index_out != NULL) 
				*index_out = i;
			return(region);
		}
	}
	return(NULL);
}

struct page *pfn_to_page(unsigned long pfn)
{
	struct mem_region *region = pfn_to_region(pfn, NULL);
	struct page *mem_map = (struct page *) region->mem_map;

	return(&mem_map[pfn - region->start_pfn]);
}

unsigned long phys_to_pfn(unsigned long p)
{
	struct mem_region *region = regions[phys_region_index(p)];

	return(region->start_pfn + (phys_addr(p) >> PAGE_SHIFT));
}

unsigned long pfn_to_phys(unsigned long pfn)
{
	int n;
	struct mem_region *region = pfn_to_region(pfn, &n);

	return(mk_phys((pfn - region->start_pfn) << PAGE_SHIFT, n));
}

struct page *page_mem_map(struct page *page)
{
	return((struct page *) page_region(page, NULL)->mem_map);
}

extern unsigned long region_pa(void *virt)
{
	struct mem_region *region;
	unsigned long addr = (unsigned long) virt;
	int i;

	for(i = 0; i < NREGIONS; i++){
		region = regions[i];
		if(region == NULL) continue;
		if((region->start <= addr) && 
		   (addr <= region->start + region->len))
			return(mk_phys(addr - region->start, i));
	}
	panic("region_pa : no region for virtual address");
	return(0);
}

extern void *region_va(unsigned long phys)
{
	return((void *) (phys_region(phys)->start + phys_addr(phys)));
}

unsigned long page_to_phys(struct page *page)
{
	int n;
	struct mem_region *region = page_region(page, &n);
	struct page *map = region->mem_map;
	return(mk_phys((page - map) << PAGE_SHIFT, n));
}

struct page *phys_to_page(unsigned long phys)
{
	struct page *mem_map;

	mem_map = phys_mem_map(phys);
	return(mem_map + (phys_offset(phys) >> PAGE_SHIFT));
}

static int setup_mem_maps(void)
{
	struct mem_region *region;
	int i;

	for(i = 0; i < NREGIONS; i++){
		region = regions[i];
		if((region != NULL) && (region->fd > 0)) init_maps(region);
	}
	return(0);
}

__initcall(setup_mem_maps);

/*
 * Allocate and free page tables.
 */

pgd_t *pgd_alloc(struct mm_struct *mm)
{
	pgd_t *pgd = (pgd_t *)__get_free_page(GFP_KERNEL);

	if (pgd) {
		memset(pgd, 0, USER_PTRS_PER_PGD * sizeof(pgd_t));
		memcpy(pgd + USER_PTRS_PER_PGD, 
		       swapper_pg_dir + USER_PTRS_PER_PGD, 
		       (PTRS_PER_PGD - USER_PTRS_PER_PGD) * sizeof(pgd_t));
	}
	return pgd;
}

void pgd_free(pgd_t *pgd)
{
	free_page((unsigned long) pgd);
}

pte_t *pte_alloc_one_kernel(struct mm_struct *mm, unsigned long address)
{
	pte_t *pte;

	pte = (pte_t *)__get_free_page(GFP_KERNEL|__GFP_REPEAT);
	if (pte)
		clear_page(pte);
	return pte;
}

struct page *pte_alloc_one(struct mm_struct *mm, unsigned long address)
{
	struct page *pte;
   
	pte = alloc_pages(GFP_KERNEL|__GFP_REPEAT, 0);
	if (pte)
		clear_highpage(pte);
	return pte;
}

/*
 * Overrides for Emacs so that we follow Linus's tabbing style.
 * Emacs will notice this stuff at the end of the file and automatically
 * adjust the settings for this buffer only.  This must remain at the end
 * of the file.
 * ---------------------------------------------------------------------------
 * Local variables:
 * c-file-style: "linux"
 * End:
 */
