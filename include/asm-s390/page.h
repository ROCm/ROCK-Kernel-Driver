/*
 *  include/asm-s390/page.h
 *
 *  S390 version
 *    Copyright (C) 1999,2000 IBM Deutschland Entwicklung GmbH, IBM Corporation
 *    Author(s): Hartmut Penner (hp@de.ibm.com)
 */

#ifndef _S390_PAGE_H
#define _S390_PAGE_H

/* PAGE_SHIFT determines the page size */
#define PAGE_SHIFT      12
#define PAGE_SIZE       (1UL << PAGE_SHIFT)
#define PAGE_MASK       (~(PAGE_SIZE-1))

#ifdef __KERNEL__
#ifndef __ASSEMBLY__

#define STRICT_MM_TYPECHECKS

#define BUG() do { \
        printk("kernel BUG at %s:%d!\n", __FILE__, __LINE__); \
        __asm__ __volatile__(".word 0x0000"); \
} while (0)                                       

#define PAGE_BUG(page) do { \
        BUG(); \
} while (0)                      

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

/*
 * gcc uses builtin, i.e. MVCLE for both operations
 */

#define clear_page(page)        memset((void *)(page), 0, PAGE_SIZE)
#define copy_page(to,from)      memcpy((void *)(to), (void *)(from), PAGE_SIZE)

#ifdef STRICT_MM_TYPECHECKS
/*
 * These are used to make use of C type-checking..
 */
typedef struct { unsigned long pte; } pte_t;
typedef struct { unsigned long pmd; } pmd_t;
typedef struct {
        unsigned long pgd0;
        unsigned long pgd1;
        unsigned long pgd2;
        unsigned long pgd3;
        } pgd_t;
typedef struct { unsigned long pgprot; } pgprot_t;

#define pte_val(x)      ((x).pte)
#define pmd_val(x)      ((x).pmd)
#define pgd_val(x)      ((x).pgd0)
#define pgprot_val(x)   ((x).pgprot)

#define __pte(x)        ((pte_t) { (x) } )
#define __pmd(x)        ((pmd_t) { (x) } )
#define __pgd(x)        ((pgd_t) { (x) } )
#define __pgprot(x)     ((pgprot_t) { (x) } )

#else
/*
 * .. while these make it easier on the compiler
 */
typedef unsigned long pte_t;
typedef unsigned long pmd_t;
typedef struct {
        unsigned long pgd0;
        unsigned long pgd1;
        unsigned long pgd2;
        unsigned long pgd3;
        } pgd_t;
typedef unsigned long pgprot_t;

#define pte_val(x)      (x)
#define pmd_val(x)      (x)
#define pgd_val(x)      (x)
#define pgprot_val(x)   (x)

#define __pte(x)        (x)
#define __pmd(x)        (x)
#define __pgd(x)        (x)
#define __pgprot(x)     (x)

#endif
#endif                                 /* !__ASSEMBLY__                    */

/* to align the pointer to the (next) page boundary */
#define PAGE_ALIGN(addr)        (((addr)+PAGE_SIZE-1)&PAGE_MASK)

/*
 *
 *
 */

#define __PAGE_OFFSET           (0x0)
#define PAGE_OFFSET             ((unsigned long)__PAGE_OFFSET)
#define __pa(x)                 ((unsigned long)(x)-PAGE_OFFSET)
#define __va(x)                 ((void *)((unsigned long)(x)+PAGE_OFFSET))
#define virt_to_page(kaddr)	(mem_map + (__pa(kaddr) >> PAGE_SHIFT))
#define VALID_PAGE(page)	((page - mem_map) < max_mapnr)

#endif                                 /* __KERNEL__                       */

#endif                                 /* _S390_PAGE_H                     */
