/* 
 * Copyright (C) 2000 - 2003 Jeff Dike (jdike@addtoit.com)
 * Licensed under the GPL
 */

#include "linux/mm.h"
#include "linux/ghash.h"
#include "linux/slab.h"
#include "linux/vmalloc.h"
#include "linux/bootmem.h"
#include "asm/types.h"
#include "asm/pgtable.h"
#include "kern_util.h"
#include "user_util.h"
#include "mode_kern.h"
#include "mem.h"
#include "mem_user.h"
#include "os.h"
#include "kern.h"
#include "init.h"

#define PHYS_HASHSIZE (8192)

struct phys_desc;

DEF_HASH_STRUCTS(virtmem, PHYS_HASHSIZE, struct phys_desc);

struct phys_desc {
	struct virtmem_ptrs virt_ptrs;
	int fd;
	__u64 offset;
	void *virt;
	unsigned long phys;
};

struct virtmem_table virtmem_hash;

static int virt_cmp(void *virt1, void *virt2)
{
	return(virt1 != virt2);
}

static int virt_hash(void *virt)
{
	unsigned long addr = ((unsigned long) virt) >> PAGE_SHIFT;
	return(addr % PHYS_HASHSIZE);
}

DEF_HASH(static, virtmem, struct phys_desc, virt_ptrs, void *, virt, virt_cmp, 
	 virt_hash);

int physmem_subst_mapping(void *virt, int fd, __u64 offset, int w)
{
	struct phys_desc *desc;
	unsigned long phys;
	int err;

	virt = (void *) ((unsigned long) virt & PAGE_MASK);
	err = os_map_memory(virt, fd, offset, PAGE_SIZE, 1, w, 0);
	if(err)
		goto out;

	phys = __pa(virt);
	if(find_virtmem_hash(&virtmem_hash, virt) != NULL)
		panic("Address 0x%p is already substituted\n", virt);

	err = -ENOMEM;
	desc = kmalloc(sizeof(*desc), GFP_ATOMIC);
	if(desc == NULL)
		goto out;

	*desc = ((struct phys_desc) { .virt_ptrs =	{ NULL, NULL },
				      .fd =		fd,
				      .offset =		offset,
				      .virt =		virt,
				      .phys =		__pa(virt) });
	insert_virtmem_hash(&virtmem_hash, desc);
	err = 0;
 out:
	return(err);
}

static int physmem_fd = -1;

int physmem_remove_mapping(void *virt)
{
	struct phys_desc *desc;
	int err;

	virt = (void *) ((unsigned long) virt & PAGE_MASK);
	desc = find_virtmem_hash(&virtmem_hash, virt);
	if(desc == NULL)
		return(0);

	remove_virtmem_hash(&virtmem_hash, desc);
	kfree(desc);

	err = os_map_memory(virt, physmem_fd, __pa(virt), PAGE_SIZE, 1, 1, 0);
	if(err)
		panic("Failed to unmap block device page from physical memory, "
		      "errno = %d", -err);
	return(1);
}

void arch_free_page(struct page *page, int order)
{
	void *virt;
	int i;

	for(i = 0; i < 1 << order; i++){
		virt = __va(page_to_phys(page + i));
		physmem_remove_mapping(virt);
	}
}

int is_remapped(void *virt)
{
	return(find_virtmem_hash(&virtmem_hash, virt) != NULL);
}

/* Changed during early boot */
unsigned long high_physmem;

extern unsigned long physmem_size;

void *to_virt(unsigned long phys)
{
	return((void *) uml_physmem + phys);
}

unsigned long to_phys(void *virt)
{
	return(((unsigned long) virt) - uml_physmem);
}

int init_maps(unsigned long physmem, unsigned long iomem, unsigned long highmem)
{
	struct page *p, *map;
	unsigned long phys_len, phys_pages, highmem_len, highmem_pages;
	unsigned long iomem_len, iomem_pages, total_len, total_pages;
	int i;

	phys_pages = physmem >> PAGE_SHIFT;
	phys_len = phys_pages * sizeof(struct page);

	iomem_pages = iomem >> PAGE_SHIFT;
	iomem_len = iomem_pages * sizeof(struct page);

	highmem_pages = highmem >> PAGE_SHIFT;
	highmem_len = highmem_pages * sizeof(struct page);

	total_pages = phys_pages + iomem_pages + highmem_pages;
	total_len = phys_len + iomem_pages + highmem_len;

	if(kmalloc_ok){
		map = kmalloc(total_len, GFP_KERNEL);
		if(map == NULL) 
			map = vmalloc(total_len);
	}
	else map = alloc_bootmem_low_pages(total_len);

	if(map == NULL)
		return(-ENOMEM);

	for(i = 0; i < total_pages; i++){
		p = &map[i];
		set_page_count(p, 0);
		SetPageReserved(p);
		INIT_LIST_HEAD(&p->list);
	}

	mem_map = map;
	max_mapnr = total_pages;
	return(0);
}

struct page *phys_to_page(const unsigned long phys)
{
	return(&mem_map[phys >> PAGE_SHIFT]);
}

struct page *__virt_to_page(const unsigned long virt)
{
	return(&mem_map[__pa(virt) >> PAGE_SHIFT]);
}

unsigned long page_to_phys(struct page *page)
{
	return((page - mem_map) << PAGE_SHIFT);
}

pte_t mk_pte(struct page *page, pgprot_t pgprot)
{
	pte_t pte;

	pte_val(pte) = page_to_phys(page) + pgprot_val(pgprot);
	if(pte_present(pte)) pte_mknewprot(pte_mknewpage(pte));
	return(pte);
}

/* Changed during early boot */
static unsigned long kmem_top = 0;

unsigned long get_kmem_end(void)
{
	if(kmem_top == 0) 
		kmem_top = CHOOSE_MODE(kmem_end_tt, kmem_end_skas);
	return(kmem_top);
}

void map_memory(unsigned long virt, unsigned long phys, unsigned long len, 
		int r, int w, int x)
{
	__u64 offset;
	int fd, err;

	fd = phys_mapping(phys, &offset);
	err = os_map_memory((void *) virt, fd, offset, len, r, w, x);
	if(err)
		panic("map_memory(0x%lx, %d, 0x%llx, %ld, %d, %d, %d) failed, "
		      "err = %d\n", virt, fd, offset, len, r, w, x, err);
}

#define PFN_UP(x) (((x) + PAGE_SIZE-1) >> PAGE_SHIFT)

void setup_physmem(unsigned long start, unsigned long reserve_end,
		   unsigned long len, unsigned long highmem)
{
	unsigned long reserve = reserve_end - start;
	int pfn = PFN_UP(__pa(reserve_end));
	int delta = (len - reserve) >> PAGE_SHIFT;
	int err, offset, bootmap_size;

	physmem_fd = create_mem_file(len + highmem);

	offset = uml_reserved - uml_physmem;
	err = os_map_memory((void *) uml_reserved, physmem_fd, offset, 
			    len - offset, 1, 1, 0);
	if(err < 0){
		os_print_error(err, "Mapping memory");
		exit(1);
	}

	bootmap_size = init_bootmem(pfn, pfn + delta);
	free_bootmem(__pa(reserve_end) + bootmap_size,
		     len - bootmap_size - reserve);
}

int phys_mapping(unsigned long phys, __u64 *offset_out)
{
	struct phys_desc *desc = find_virtmem_hash(&virtmem_hash, 
						   __va(phys & PAGE_MASK));
	int fd = -1;

	if(desc != NULL){
		fd = desc->fd;
		*offset_out = desc->offset;
	}
	else if(phys < physmem_size){
		fd = physmem_fd;
		*offset_out = phys;
	}
	else if(phys < __pa(end_iomem)){
		struct iomem_region *region = iomem_regions;
	
		while(region != NULL){
			if((phys >= region->phys) && 
			   (phys < region->phys + region->size)){
				fd = region->fd;
				*offset_out = phys - region->phys;
				break;
			}
			region = region->next;
		}
	}
	else if(phys < __pa(end_iomem) + highmem){
		fd = physmem_fd;
		*offset_out = phys - iomem_size;
	}

	return(fd);
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
"    This is not related to the amount of memory in the host.  It can\n"
"    be more, and the excess, if it's ever used, will just be swapped out.\n"
"	Example: mem=64M\n\n"
);

unsigned long find_iomem(char *driver, unsigned long *len_out)
{
	struct iomem_region *region = iomem_regions;
	
	while(region != NULL){
		if(!strcmp(region->driver, driver)){
			*len_out = region->size;
			return(region->virt);
		}
	}

	return(0);
}

int setup_iomem(void)
{
	struct iomem_region *region = iomem_regions;
	unsigned long iomem_start = high_physmem + PAGE_SIZE;
	int err;

	while(region != NULL){
		err = os_map_memory((void *) iomem_start, region->fd, 0, 
				    region->size, 1, 1, 0);
		if(err)
			printk("Mapping iomem region for driver '%s' failed, "
			       "errno = %d\n", region->driver, -err);
		else {
			region->virt = iomem_start;
			region->phys = __pa(region->virt);
		}

		iomem_start += region->size + PAGE_SIZE;
		region = region->next;
	}

	return(0);
}

__initcall(setup_iomem);

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
