/*
 * parisc mmu structures 
 */

#ifndef _PARISC_MMU_H_
#define _PARISC_MMU_H_

#ifndef __ASSEMBLY__

/* Default "unsigned long" context */
typedef unsigned long mm_context_t;

/* Hardware Page Table Entry */
typedef struct _PTE {
	unsigned long v:1;	/* Entry is valid */
	unsigned long tag:31;	/* Unique Tag */

	unsigned long r:1;	/* referenced */
	unsigned long os_1:1;	/*  */
	unsigned long t:1;	/* page reference trap */
	unsigned long d:1;	/* dirty */
	unsigned long b:1;	/* break */
	unsigned long type:3;	/* access type */
	unsigned long pl1:2;	/* PL1 (execute) */
	unsigned long pl2:2;	/* PL2 (write) */
	unsigned long u:1;	/* uncacheable */
	unsigned long id:1;	/* access id */
	unsigned long os_2:1;	/*  */

	unsigned long os_3:3;	/*  */
	unsigned long res_1:4;	/*  */
	unsigned long phys:20;	/* physical page number */
	unsigned long os_4:2;	/*  */
	unsigned long res_2:3;	/*  */

	unsigned long next;	/* pointer to next page */
} PTE; 

/*
 * Simulated two-level MMU.  This structure is used by the kernel
 * to keep track of MMU mappings and is used to update/maintain
 * the hardware HASH table which is really a cache of mappings.
 *
 * The simulated structures mimic the hardware available on other
 * platforms, notably the 80x86 and 680x0.
 */

typedef struct _pte {
   	unsigned long page_num:20;
   	unsigned long flags:12;		/* Page flags (some unused bits) */
} pte;

#define PD_SHIFT (10+12)		/* Page directory */
#define PD_MASK  0x02FF
#define PT_SHIFT (12)			/* Page Table */
#define PT_MASK  0x02FF
#define PG_SHIFT (12)			/* Page Entry */

/* MMU context */

typedef struct _MMU_context {
	long	pid[4];
	pte	**pmap;		/* Two-level page-map structure */
} MMU_context;

#endif /* __ASSEMBLY__ */

#endif /* _PARISC_MMU_H_ */
