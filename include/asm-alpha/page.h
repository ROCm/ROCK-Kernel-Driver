#ifndef _ALPHA_PAGE_H
#define _ALPHA_PAGE_H

/* PAGE_SHIFT determines the page size */
#define PAGE_SHIFT	13
#define PAGE_SIZE	(1UL << PAGE_SHIFT)
#define PAGE_MASK	(~(PAGE_SIZE-1))

#ifdef __KERNEL__

#ifndef __ASSEMBLY__

#define STRICT_MM_TYPECHECKS

/*
 * A _lot_ of the kernel time is spent clearing pages, so
 * do this as fast as we possibly can. Also, doing this
 * as a separate inline function (rather than memset())
 * results in clearer kernel profiles as we see _who_ is
 * doing page clearing or copying.
 */
static inline void clear_page(void * page)
{
	unsigned long count = PAGE_SIZE/64;
	unsigned long *ptr = (unsigned long *)page;

	do {
		ptr[0] = 0;
		ptr[1] = 0;
		ptr[2] = 0;
		ptr[3] = 0;
		count--;
		ptr[4] = 0;
		ptr[5] = 0;
		ptr[6] = 0;
		ptr[7] = 0;
		ptr += 8;
	} while (count);
}

#define clear_user_page(page, vaddr)	clear_page(page)

static inline void copy_page(void * _to, void * _from)
{
	unsigned long count = PAGE_SIZE/64;
	unsigned long *to = (unsigned long *)_to;
	unsigned long *from = (unsigned long *)_from;

	do {
		unsigned long a,b,c,d,e,f,g,h;
		a = from[0];
		b = from[1];
		c = from[2];
		d = from[3];
		e = from[4];
		f = from[5];
		g = from[6];
		h = from[7];
		count--;
		from += 8;
		to[0] = a;
		to[1] = b;
		to[2] = c;
		to[3] = d;
		to[4] = e;
		to[5] = f;
		to[6] = g;
		to[7] = h;
		to += 8;
	} while (count);
}

#define copy_user_page(to, from, vaddr)	copy_page(to, from)

#ifdef STRICT_MM_TYPECHECKS
/*
 * These are used to make use of C type-checking..
 */
typedef struct { unsigned long pte; } pte_t;
typedef struct { unsigned long pmd; } pmd_t;
typedef struct { unsigned long pgd; } pgd_t;
typedef struct { unsigned long pgprot; } pgprot_t;

#define pte_val(x)	((x).pte)
#define pmd_val(x)	((x).pmd)
#define pgd_val(x)	((x).pgd)
#define pgprot_val(x)	((x).pgprot)

#define __pte(x)	((pte_t) { (x) } )
#define __pgd(x)	((pgd_t) { (x) } )
#define __pgprot(x)	((pgprot_t) { (x) } )

#else
/*
 * .. while these make it easier on the compiler
 */
typedef unsigned long pte_t;
typedef unsigned long pmd_t;
typedef unsigned long pgd_t;
typedef unsigned long pgprot_t;

#define pte_val(x)	(x)
#define pmd_val(x)	(x)
#define pgd_val(x)	(x)
#define pgprot_val(x)	(x)

#define __pte(x)	(x)
#define __pgd(x)	(x)
#define __pgprot(x)	(x)

#endif /* STRICT_MM_TYPECHECKS */

#define BUG()		__asm__ __volatile__("call_pal 129 # bugchk")
#define PAGE_BUG(page)	BUG()

/* Pure 2^n version of get_order */
extern __inline__ int get_order(unsigned long size)
{
	int order;

	size = (size-1) >> (PAGE_SHIFT-1);
	order = -1;
	do {
		size >>= 1;
		order++;
	} while (size);
	return order;
}

#endif /* !ASSEMBLY */

/* to align the pointer to the (next) page boundary */
#define PAGE_ALIGN(addr)	(((addr)+PAGE_SIZE-1)&PAGE_MASK)

#ifdef USE_48_BIT_KSEG
#define PAGE_OFFSET		0xffff800000000000
#else
#define PAGE_OFFSET		0xfffffc0000000000
#endif

#define __pa(x)			((unsigned long) (x) - PAGE_OFFSET)
#define __va(x)			((void *)((unsigned long) (x) + PAGE_OFFSET))
#define virt_to_page(kaddr)	(mem_map + (__pa(kaddr) >> PAGE_SHIFT))
#define VALID_PAGE(page)	((page - mem_map) < max_mapnr)

#endif /* __KERNEL__ */

#endif /* _ALPHA_PAGE_H */
