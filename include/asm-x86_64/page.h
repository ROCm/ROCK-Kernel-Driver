#ifndef _X86_64_PAGE_H
#define _X86_64_PAGE_H

/* PAGE_SHIFT determines the page size */
#define PAGE_SHIFT	12
#ifdef __ASSEMBLY__
#define PAGE_SIZE	(0x1 << PAGE_SHIFT)
#else
#define PAGE_SIZE	(1UL << PAGE_SHIFT)
#endif
#define PAGE_MASK	(~(PAGE_SIZE-1))
#define THREAD_SIZE (2*PAGE_SIZE)
#define CURRENT_MASK (~(THREAD_SIZE-1))

#ifdef __KERNEL__
#ifndef __ASSEMBLY__

#if 0 
#include <asm/mmx.h>
#define clear_page(page)	mmx_clear_page((void *)(page))
#define copy_page(to,from)	mmx_copy_page(to,from)
#else
#define clear_page(page)	memset((void *)(page), 0, PAGE_SIZE)
#define copy_page(to,from)	memcpy((void *)(to), (void *)(from), PAGE_SIZE)
#endif

#define clear_user_page(page, vaddr)	clear_page(page)
#define copy_user_page(to, from, vaddr)	copy_page(to, from)

/*
 * These are used to make use of C type-checking..
 */
typedef struct { unsigned long pte; } pte_t;
typedef struct { unsigned long pmd; } pmd_t;
typedef struct { unsigned long pgd; } pgd_t;
typedef struct { unsigned long pml4; } pml4_t;
#define PTE_MASK	PAGE_MASK

typedef struct { unsigned long pgprot; } pgprot_t;

#define pte_val(x)	((x).pte)
#define pmd_val(x)	((x).pmd)
#define pgd_val(x)	((x).pgd)
#define pml4_val(x)	((x).pml4)
#define pgprot_val(x)	((x).pgprot)

#define __pte(x) ((pte_t) { (x) } )
#define __pmd(x) ((pmd_t) { (x) } )
#define __pgd(x) ((pgd_t) { (x) } )
#define __level4(x) ((level4_t) { (x) } )
#define __pgprot(x)	((pgprot_t) { (x) } )

#endif /* !__ASSEMBLY__ */

/* to align the pointer to the (next) page boundary */
#define PAGE_ALIGN(addr)	(((addr)+PAGE_SIZE-1)&PAGE_MASK)

/* See Documentation/x86_64/mm.txt for a description of the layout. */
#define __START_KERNEL		0xffffffff80100000
#define __START_KERNEL_map	0xffffffff80000000
#define __PAGE_OFFSET           0x0000010000000000

#ifndef __ASSEMBLY__

/*
 * Tell the user there is some problem.  The exception handler decodes this frame.
 */ 
struct bug_frame { 
       unsigned char ud2[2];          
	char *filename;    /* should use 32bit offset instead, but the assembler doesn't like it */ 
	unsigned short line; 
} __attribute__((packed)); 
#define BUG() asm volatile("ud2 ; .quad %c1 ; .short %c0" :: "i"(__LINE__), "i" (__FILE__))
#define PAGE_BUG(page) BUG()
void out_of_line_bug(void);

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

#endif /* __ASSEMBLY__ */

#define PAGE_OFFSET		((unsigned long)__PAGE_OFFSET)

/* Note: __pa(&symbol_visible_to_c) should be always replaced with __pa_symbol.
   Otherwise you risk miscompilation. */ 
#define __pa(x)			(((unsigned long)(x)>=__START_KERNEL_map)?(unsigned long)(x) - (unsigned long)__START_KERNEL_map:(unsigned long)(x) - PAGE_OFFSET)
/* __pa_symbol should be used for C visible symbols.
   This seems to be the official gcc blessed way to do such arithmetic. */ 
#define __pa_symbol(x)		\
	({unsigned long v;  \
	  asm("" : "=r" (v) : "0" (x)); \
	  __pa(v); })

#define __va(x)			((void *)((unsigned long)(x)+PAGE_OFFSET))
#define virt_to_page(kaddr)	(mem_map + (__pa(kaddr) >> PAGE_SHIFT))
#define VALID_PAGE(page)	((page - mem_map) < max_mapnr)


#define VM_DATA_DEFAULT_FLAGS  (VM_READ | VM_WRITE | VM_EXEC | \
                                VM_MAYREAD | VM_MAYWRITE | VM_MAYEXEC)

#endif /* __KERNEL__ */

#endif /* _X86_64_PAGE_H */
