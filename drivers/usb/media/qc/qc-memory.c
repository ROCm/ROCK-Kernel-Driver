/* Start of file */

/* {{{ [fold] Comments */
/*
 * qce-ga, linux V4L driver for the QuickCam Express and Dexxa QuickCam
 *
 * memory.c - contains all needed memory management functions
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */
 
/* These routines have been derived from the ov511 driver, into which they
 * were derived from the bttv driver.
 */
/* }}} */
/* {{{ [fold] Includes */
#include <linux/config.h>
#include <linux/version.h>

#ifdef CONFIG_SMP
#define __SMP__
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,0)
#if defined(CONFIG_MODVERSIONS) && !defined(MODVERSIONS)
#define MODVERSIONS
#endif
#ifdef MODVERSIONS
#include <linux/modversions.h>
#endif
#endif

#include "qc-memory.h"
#include <asm/io.h>
#include <linux/mm.h>		/* Required on Alpha, from Bob McElrath <mcelrath@draal.physics.wisc.edu> */
#include <asm/pgtable.h>	/* Required on Alpha */
#include <linux/vmalloc.h>	/* Required on Alpha */
#include <linux/pagemap.h>	/* pmd_offset requires this on SuSE supplied kernels */
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,4,0)
#include <linux/wrapper.h>	/* For proper mem_map_(un)reserve define, the compatibility define below might not work */
#endif
/* }}} */
/* {{{ [fold] Compatibility wrappers */
#ifndef HAVE_VMA
#if LINUX_VERSION_CODE>=KERNEL_VERSION(2,5,3) || (defined(RED_HAT_LINUX_KERNEL) && defined(pte_offset_map))
#define HAVE_VMA 1
#else
#define HAVE_VMA 0
#endif
#endif

#if !HAVE_VMA
static inline int qc_remap_page_range(unsigned long from, unsigned long addr, unsigned long size, pgprot_t prot) { return remap_page_range(from, addr, size, prot); }
#undef remap_page_range
#define remap_page_range(vma, start, addr, size, prot)	qc_remap_page_range((start),(addr),(size),(prot))
#endif

#if LINUX_VERSION_CODE>=KERNEL_VERSION(2,5,3) || defined(pte_offset_map)
#define pte_offset(pmd,adr)	pte_offset_map(pmd,adr)	/* Emulation for a kernel using the new rmap-vm */
#endif							/* Fix by Michele Balistreri <brain87@gmx.net> */

#ifndef SetPageReserved
#define SetPageReserved(p)	mem_map_reserve(p)
#endif
#ifndef ClearPageReserved
#define ClearPageReserved(p)	mem_map_unreserve(p)
#endif
/* }}} */

/* {{{ [fold] kvirt_to_pa(): obtain physical address from virtual address obtained by vmalloc() */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,4,0)
/* {{{ [fold] kvirt_to_pa(), 2.4.x and 2.6.x */
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,4,19)
static struct page *vmalloc_to_page(void * vmalloc_addr);
#endif

/* Here we want the physical address of the memory obtained by vmalloc().
 */
static inline unsigned long kvirt_to_pa(unsigned long adr)
{
	unsigned long kva, ret;

	kva = (unsigned long) page_address(vmalloc_to_page((void *)adr));
	kva |= adr & (PAGE_SIZE-1); /* restore the offset */
	ret = __pa(kva);
	return ret;
}
/* }}} */
#else
/* {{{ [fold] kvirt_to_pa() for 2.2.x */
#define page_address(x)		(x | PAGE_OFFSET)	/* Damn ugly hack from kcomp.h; replaces original page_address() that made different thing! */

/* Given PGD from the address space's page table, return the kernel
 * virtual mapping of the physical memory mapped at ADR.
 */
static inline unsigned long uvirt_to_kva(pgd_t *pgd, unsigned long adr)
{
	unsigned long ret = 0UL;
	pmd_t *pmd;
	pte_t *ptep, pte;

	if (!pgd_none(*pgd)) {
		pmd = pmd_offset(pgd, adr);
		if (!pmd_none(*pmd)) {
			ptep = pte_offset(pmd, adr);
			pte = *ptep;
			if (pte_present(pte)) {
				ret = (unsigned long) page_address(pte_page(pte));
				ret |= (adr & (PAGE_SIZE - 1));
			}
		}
	}

	return ret;
}

/* Here we want the physical address of the memory obtained by vmalloc().
 * This is used when initializing the contents of the
 * area and marking the pages as reserved.
 */
static unsigned long kvirt_to_pa(unsigned long adr)
{
	unsigned long va, kva, ret;

	va = VMALLOC_VMADDR(adr);
	kva = uvirt_to_kva(pgd_offset_k(va), va);
	ret = __pa(kva);
	return ret;
}
/* }}} */
#endif
/* }}} */
/* {{{ [fold] vmalloc_to_page(): obtain pointer to struct page from virtual address obtained by vmalloc() */
#if LINUX_VERSION_CODE>=KERNEL_VERSION(2,4,0) && LINUX_VERSION_CODE<KERNEL_VERSION(2,4,19)
/* {{{ [fold] vmalloc_to_page() for 2.4.x (returns pointer to page) */
static struct page *vmalloc_to_page(void * vmalloc_addr)
{
	unsigned long addr = (unsigned long) vmalloc_addr;
	struct page *page = NULL;
	pmd_t *pmd;
	pte_t *pte;
	pgd_t *pgd;

	pgd = pgd_offset_k(addr);
	if (!pgd_none(*pgd)) {
		pmd = pmd_offset(pgd, addr);
		if (!pmd_none(*pmd)) {
			pte = pte_offset(pmd, addr);
			if (pte_present(*pte)) {
				page = pte_page(*pte);
			}
		}
	}
	return page;
}
/* }}} */
#elif LINUX_VERSION_CODE < KERNEL_VERSION(2,4,0)
/* {{{ [fold] vmalloc_to_page() for 2.2.x (returns page index in mem_map[]) */
static inline unsigned long vmalloc_to_page(void * vmalloc_addr)
{
	unsigned long pagenum;
	unsigned long physaddr;
	physaddr = kvirt_to_pa((unsigned long)vmalloc_addr);
	pagenum = MAP_NR(__va(physaddr));
	return pagenum;
}
/* }}} */
#endif
/* }}} */

/* {{{ [fold] qc_mm_rvmalloc(size) */
void *qc_mm_rvmalloc(unsigned long size)
{
	void *mem;
	unsigned long adr;

	size = PAGE_ALIGN(size);
	mem = vmalloc_32(size);
	if (!mem)
		return NULL;

	memset(mem, 0, size); /* Clear the ram out, no junk to the user */
	adr = (unsigned long) mem;
	while ((long)size > 0) {
		SetPageReserved(vmalloc_to_page((void *)adr));
		adr += PAGE_SIZE;
		size -= PAGE_SIZE;
	}

	return mem;
}
/* }}} */
/* {{{ [fold] qc_mm_rvfree(mem, size) */
void qc_mm_rvfree(void *mem, unsigned long size)
{
	unsigned long adr;

	if (!mem)
		return;

	adr = (unsigned long) mem;
	while ((long) size > 0) {
		ClearPageReserved(vmalloc_to_page((void *)adr));
		adr += PAGE_SIZE;
		size -= PAGE_SIZE;
	}
	vfree(mem);
}
/* }}} */
/* {{{ [fold] qc_mm_remap(vma, src, src_size, dst, dst_size) */
int qc_mm_remap(struct vm_area_struct *vma, void *src, unsigned long src_size, const void *dst, unsigned long dst_size)
{
	unsigned long start = (unsigned long)dst;
	unsigned long size  = dst_size;
	unsigned long physaddr, pos;

	pos = (unsigned long)src;
	while ((long)size > 0) {
		physaddr = kvirt_to_pa(pos);
		if (remap_page_range(vma, start, physaddr, PAGE_SIZE, PAGE_SHARED))
			return -EAGAIN;
		start += PAGE_SIZE;
		pos += PAGE_SIZE;
		size -= PAGE_SIZE;
	}
	return 0;
}
/* }}} */

/* End of file */
