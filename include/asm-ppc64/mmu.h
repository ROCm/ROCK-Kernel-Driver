/*
 * PowerPC memory management structures
 *
 * Dave Engebretsen & Mike Corrigan <{engebret|mikejc}@us.ibm.com>
 *   PPC64 rework.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#ifndef _PPC64_MMU_H_
#define _PPC64_MMU_H_

#include <linux/config.h>
#include <asm/page.h>

#ifndef __ASSEMBLY__

/* Time to allow for more things here */
typedef unsigned long mm_context_id_t;
typedef struct {
	mm_context_id_t id;
#ifdef CONFIG_HUGETLB_PAGE
	u16 htlb_segs; /* bitmask */
#endif
} mm_context_t;

#ifdef CONFIG_HUGETLB_PAGE
#define KERNEL_LOW_HPAGES	.htlb_segs = 0,
#else
#define KERNEL_LOW_HPAGES
#endif

#define KERNEL_CONTEXT(ea) ({ \
		mm_context_t ctx = { .id = REGION_ID(ea), KERNEL_LOW_HPAGES}; \
		ctx; })

typedef struct {
	unsigned long esid: 36; /* Effective segment ID */
	unsigned long resv0:20; /* Reserved */
	unsigned long v:     1; /* Entry valid (v=1) or invalid */
	unsigned long resv1: 1; /* Reserved */
	unsigned long ks:    1; /* Supervisor (privileged) state storage key */
	unsigned long kp:    1; /* Problem state storage key */
	unsigned long n:     1; /* No-execute if n=1 */
	unsigned long resv2: 3; /* padding to a 64b boundary */
} ste_dword0;

typedef struct {
	unsigned long vsid: 52; /* Virtual segment ID */
	unsigned long resv0:12; /* Padding to a 64b boundary */
} ste_dword1;

typedef struct _STE {
	union {
		unsigned long dword0;
		ste_dword0    dw0;
	} dw0;

	union {
		unsigned long dword1;
		ste_dword1    dw1;
	} dw1;
} STE;

/* Hardware Page Table Entry */

#define HPTES_PER_GROUP 8

typedef struct {
	unsigned long avpn:57; /* vsid | api == avpn  */
	unsigned long :     2; /* Software use */
	unsigned long bolted: 1; /* HPTE is "bolted" */
	unsigned long lock: 1; /* lock on pSeries SMP */
	unsigned long l:    1; /* Virtual page is large (L=1) or 4 KB (L=0) */
	unsigned long h:    1; /* Hash function identifier */
	unsigned long v:    1; /* Valid (v=1) or invalid (v=0) */
} Hpte_dword0;

typedef struct {
	unsigned long pp0:  1; /* Page protection bit 0 */
	unsigned long ts:   1; /* Tag set bit */
	unsigned long rpn: 50; /* Real page number */
	unsigned long :     2; /* Reserved */
	unsigned long ac:   1; /* Address compare */ 
	unsigned long r:    1; /* Referenced */
	unsigned long c:    1; /* Changed */
	unsigned long w:    1; /* Write-thru cache mode */
	unsigned long i:    1; /* Cache inhibited */
	unsigned long m:    1; /* Memory coherence required */
	unsigned long g:    1; /* Guarded */
	unsigned long n:    1; /* No-execute */
	unsigned long pp:   2; /* Page protection bits 1:2 */
} Hpte_dword1;

typedef struct {
	char padding[6];	   	/* padding */
	unsigned long :       6;	/* padding */ 
	unsigned long flags: 10;	/* HPTE flags */
} Hpte_dword1_flags;

typedef struct {
	union {
		unsigned long dword0;
		Hpte_dword0   dw0;
	} dw0;

	union {
		unsigned long dword1;
		Hpte_dword1 dw1;
		Hpte_dword1_flags flags;
	} dw1;
} HPTE; 

/* Values for PP (assumes Ks=0, Kp=1) */
/* pp0 will always be 0 for linux     */
#define PP_RWXX	0	/* Supervisor read/write, User none */
#define PP_RWRX 1	/* Supervisor read/write, User read */
#define PP_RWRW 2	/* Supervisor read/write, User read/write */
#define PP_RXRX 3	/* Supervisor read,       User read */


typedef struct {
	HPTE *		htab;
	unsigned long	htab_num_ptegs;
	unsigned long	htab_hash_mask;
	unsigned long	next_round_robin;
	unsigned long   last_kernel_address;
} HTAB;

extern HTAB htab_data;

void invalidate_hpte( unsigned long slot );
long select_hpte_slot( unsigned long vpn );
void create_valid_hpte( unsigned long slot, unsigned long vpn,
			unsigned long prpn, unsigned hash,
			void * ptep, unsigned hpteflags,
			unsigned bolted );

#define PD_SHIFT (10+12)		/* Page directory */
#define PD_MASK  0x02FF
#define PT_SHIFT (12)			/* Page Table */
#define PT_MASK  0x02FF

#define LARGE_PAGE_SHIFT 24

static inline unsigned long hpt_hash(unsigned long vpn, int large)
{
	unsigned long vsid;
	unsigned long page;

	if (large) {
		vsid = vpn >> 4;
		page = vpn & 0xf;
	} else {
		vsid = vpn >> 16;
		page = vpn & 0xffff;
	}

	return (vsid & 0x7fffffffffUL) ^ page;
}

static inline void __tlbie(unsigned long va, int large)
{
	/* clear top 16 bits, non SLS segment */
	va &= ~(0xffffULL << 48);

	if (large)
		asm volatile("tlbie %0,1" : : "r"(va) : "memory");
	else
		asm volatile("tlbie %0,0" : : "r"(va) : "memory");
}

static inline void tlbie(unsigned long va, int large)
{
	asm volatile("ptesync": : :"memory");
	__tlbie(va, large);
	asm volatile("eieio; tlbsync; ptesync": : :"memory");
}

static inline void __tlbiel(unsigned long va)
{
	/* clear top 16 bits, non SLS segment */
	va &= ~(0xffffULL << 48);

	/* 
	 * Thanks to Alan Modra we are now able to use machine specific 
	 * assembly instructions (like tlbiel) by using the gas -many flag.
	 * However we have to support older toolchains so for the moment 
	 * we hardwire it.
	 */
#if 0
	asm volatile("tlbiel %0" : : "r"(va) : "memory");
#else
	asm volatile(".long 0x7c000224 | (%0 << 11)" : : "r"(va) : "memory");
#endif
}

static inline void tlbiel(unsigned long va)
{
	asm volatile("ptesync": : :"memory");
	__tlbiel(va);
	asm volatile("ptesync": : :"memory");
}

/*
 * Handle a fault by adding an HPTE. If the address can't be determined
 * to be valid via Linux page tables, return 1. If handled return 0
 */
extern int __hash_page(unsigned long ea, unsigned long access,
		       unsigned long vsid, pte_t *ptep, unsigned long trap,
		       int local);

extern void htab_finish_init(void);

#endif /* __ASSEMBLY__ */

/*
 * Location of cpu0's segment table
 */
#define STAB0_PAGE	0x9
#define STAB0_PHYS_ADDR	(STAB0_PAGE<<PAGE_SHIFT)
#define STAB0_VIRT_ADDR	(KERNELBASE+STAB0_PHYS_ADDR)

#define SLB_NUM_BOLTED		2
#define SLB_CACHE_ENTRIES	8

/* Bits in the SLB ESID word */
#define SLB_ESID_V		0x0000000008000000	/* entry is valid */

/* Bits in the SLB VSID word */
#define SLB_VSID_SHIFT		12
#define SLB_VSID_KS		0x0000000000000800
#define SLB_VSID_KP		0x0000000000000400
#define SLB_VSID_N		0x0000000000000200	/* no-execute */
#define SLB_VSID_L		0x0000000000000100	/* largepage (4M) */
#define SLB_VSID_C		0x0000000000000080	/* class */

#define SLB_VSID_KERNEL		(SLB_VSID_KP|SLB_VSID_C)
#define SLB_VSID_USER		(SLB_VSID_KP|SLB_VSID_KS)

#define VSID_RANDOMIZER ASM_CONST(42470972311)
#define VSID_MASK	0xfffffffffUL
/* Because we never access addresses below KERNELBASE as kernel
 * addresses, this VSID is never used for anything real, and will
 * never have pages hashed into it */
#define BAD_VSID	ASM_CONST(0)

/* Block size masks */
#define BL_128K	0x000
#define BL_256K 0x001
#define BL_512K 0x003
#define BL_1M   0x007
#define BL_2M   0x00F
#define BL_4M   0x01F
#define BL_8M   0x03F
#define BL_16M  0x07F
#define BL_32M  0x0FF
#define BL_64M  0x1FF
#define BL_128M 0x3FF
#define BL_256M 0x7FF

/* Used to set up SDR1 register */
#define HASH_TABLE_SIZE_64K	0x00010000
#define HASH_TABLE_SIZE_128K	0x00020000
#define HASH_TABLE_SIZE_256K	0x00040000
#define HASH_TABLE_SIZE_512K	0x00080000
#define HASH_TABLE_SIZE_1M	0x00100000
#define HASH_TABLE_SIZE_2M	0x00200000
#define HASH_TABLE_SIZE_4M	0x00400000
#define HASH_TABLE_MASK_64K	0x000   
#define HASH_TABLE_MASK_128K	0x001   
#define HASH_TABLE_MASK_256K	0x003   
#define HASH_TABLE_MASK_512K	0x007
#define HASH_TABLE_MASK_1M	0x00F   
#define HASH_TABLE_MASK_2M	0x01F   
#define HASH_TABLE_MASK_4M	0x03F   

/* These are the Ks and Kp from the PowerPC books.  For proper operation,
 * Ks = 0, Kp = 1.
 */
#define MI_AP		786
#define MI_Ks		0x80000000	/* Should not be set */
#define MI_Kp		0x40000000	/* Should always be set */

/* The effective page number register.  When read, contains the information
 * about the last instruction TLB miss.  When MI_RPN is written, bits in
 * this register are used to create the TLB entry.
 */
#define MI_EPN		787
#define MI_EPNMASK	0xfffff000	/* Effective page number for entry */
#define MI_EVALID	0x00000200	/* Entry is valid */
#define MI_ASIDMASK	0x0000000f	/* ASID match value */
					/* Reset value is undefined */

/* A "level 1" or "segment" or whatever you want to call it register.
 * For the instruction TLB, it contains bits that get loaded into the
 * TLB entry when the MI_RPN is written.
 */
#define MI_TWC		789
#define MI_APG		0x000001e0	/* Access protection group (0) */
#define MI_GUARDED	0x00000010	/* Guarded storage */
#define MI_PSMASK	0x0000000c	/* Mask of page size bits */
#define MI_PS8MEG	0x0000000c	/* 8M page size */
#define MI_PS512K	0x00000004	/* 512K page size */
#define MI_PS4K_16K	0x00000000	/* 4K or 16K page size */
#define MI_SVALID	0x00000001	/* Segment entry is valid */
					/* Reset value is undefined */

/* Real page number.  Defined by the pte.  Writing this register
 * causes a TLB entry to be created for the instruction TLB, using
 * additional information from the MI_EPN, and MI_TWC registers.
 */
#define MI_RPN		790

/* Define an RPN value for mapping kernel memory to large virtual
 * pages for boot initialization.  This has real page number of 0,
 * large page size, shared page, cache enabled, and valid.
 * Also mark all subpages valid and write access.
 */
#define MI_BOOTINIT	0x000001fd

#define MD_CTR		792	/* Data TLB control register */
#define MD_GPM		0x80000000	/* Set domain manager mode */
#define MD_PPM		0x40000000	/* Set subpage protection */
#define MD_CIDEF	0x20000000	/* Set cache inhibit when MMU dis */
#define MD_WTDEF	0x10000000	/* Set writethrough when MMU dis */
#define MD_RSV4I	0x08000000	/* Reserve 4 TLB entries */
#define MD_TWAM		0x04000000	/* Use 4K page hardware assist */
#define MD_PPCS		0x02000000	/* Use MI_RPN prob/priv state */
#define MD_IDXMASK	0x00001f00	/* TLB index to be loaded */
#define MD_RESETVAL	0x04000000	/* Value of register at reset */

#define M_CASID		793	/* Address space ID (context) to match */
#define MC_ASIDMASK	0x0000000f	/* Bits used for ASID value */


/* These are the Ks and Kp from the PowerPC books.  For proper operation,
 * Ks = 0, Kp = 1.
 */
#define MD_AP		794
#define MD_Ks		0x80000000	/* Should not be set */
#define MD_Kp		0x40000000	/* Should always be set */

/* The effective page number register.  When read, contains the information
 * about the last instruction TLB miss.  When MD_RPN is written, bits in
 * this register are used to create the TLB entry.
 */
#define MD_EPN		795
#define MD_EPNMASK	0xfffff000	/* Effective page number for entry */
#define MD_EVALID	0x00000200	/* Entry is valid */
#define MD_ASIDMASK	0x0000000f	/* ASID match value */
					/* Reset value is undefined */

/* The pointer to the base address of the first level page table.
 * During a software tablewalk, reading this register provides the address
 * of the entry associated with MD_EPN.
 */
#define M_TWB		796
#define	M_L1TB		0xfffff000	/* Level 1 table base address */
#define M_L1INDX	0x00000ffc	/* Level 1 index, when read */
					/* Reset value is undefined */

/* A "level 1" or "segment" or whatever you want to call it register.
 * For the data TLB, it contains bits that get loaded into the TLB entry
 * when the MD_RPN is written.  It is also provides the hardware assist
 * for finding the PTE address during software tablewalk.
 */
#define MD_TWC		797
#define MD_L2TB		0xfffff000	/* Level 2 table base address */
#define MD_L2INDX	0xfffffe00	/* Level 2 index (*pte), when read */
#define MD_APG		0x000001e0	/* Access protection group (0) */
#define MD_GUARDED	0x00000010	/* Guarded storage */
#define MD_PSMASK	0x0000000c	/* Mask of page size bits */
#define MD_PS8MEG	0x0000000c	/* 8M page size */
#define MD_PS512K	0x00000004	/* 512K page size */
#define MD_PS4K_16K	0x00000000	/* 4K or 16K page size */
#define MD_WT		0x00000002	/* Use writethrough page attribute */
#define MD_SVALID	0x00000001	/* Segment entry is valid */
					/* Reset value is undefined */


/* Real page number.  Defined by the pte.  Writing this register
 * causes a TLB entry to be created for the data TLB, using
 * additional information from the MD_EPN, and MD_TWC registers.
 */
#define MD_RPN		798

/* This is a temporary storage register that could be used to save
 * a processor working register during a tablewalk.
 */
#define M_TW		799

#endif /* _PPC64_MMU_H_ */
