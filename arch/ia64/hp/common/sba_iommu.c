/*
**  IA64 System Bus Adapter (SBA) I/O MMU manager
**
**	(c) Copyright 2002 Alex Williamson
**	(c) Copyright 2002 Grant Grundler
**	(c) Copyright 2002 Hewlett-Packard Company
**
**	Portions (c) 2000 Grant Grundler (from parisc I/O MMU code)
**	Portions (c) 1999 Dave S. Miller (from sparc64 I/O MMU code)
**
**	This program is free software; you can redistribute it and/or modify
**	it under the terms of the GNU General Public License as published by
**      the Free Software Foundation; either version 2 of the License, or
**      (at your option) any later version.
**
**
** This module initializes the IOC (I/O Controller) found on HP
** McKinley machines and their successors.
**
*/

#include <linux/config.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/spinlock.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/mm.h>
#include <linux/string.h>
#include <linux/pci.h>
#include <linux/proc_fs.h>
#include <linux/efi.h>

#include <asm/delay.h>		/* ia64_get_itc() */
#include <asm/io.h>
#include <asm/page.h>		/* PAGE_OFFSET */


#define DRIVER_NAME "SBA"

#define ALLOW_IOV_BYPASS
#define ENABLE_MARK_CLEAN
/*
** The number of debug flags is a clue - this code is fragile.
*/
#undef DEBUG_SBA_INIT
#undef DEBUG_SBA_RUN
#undef DEBUG_SBA_RUN_SG
#undef DEBUG_SBA_RESOURCE
#undef ASSERT_PDIR_SANITY
#undef DEBUG_LARGE_SG_ENTRIES
#undef DEBUG_BYPASS

#define SBA_INLINE	__inline__
/* #define SBA_INLINE */

#ifdef DEBUG_SBA_INIT
#define DBG_INIT(x...)	printk(x)
#else
#define DBG_INIT(x...)
#endif

#ifdef DEBUG_SBA_RUN
#define DBG_RUN(x...)	printk(x)
#else
#define DBG_RUN(x...)
#endif

#ifdef DEBUG_SBA_RUN_SG
#define DBG_RUN_SG(x...)	printk(x)
#else
#define DBG_RUN_SG(x...)
#endif


#ifdef DEBUG_SBA_RESOURCE
#define DBG_RES(x...)	printk(x)
#else
#define DBG_RES(x...)
#endif

#ifdef DEBUG_BYPASS
#define DBG_BYPASS(x...)	printk(x)
#else
#define DBG_BYPASS(x...)
#endif

#ifdef ASSERT_PDIR_SANITY
#define ASSERT(expr) \
        if(!(expr)) { \
                printk( "\n" __FILE__ ":%d: Assertion " #expr " failed!\n",__LINE__); \
                panic(#expr); \
        }
#else
#define ASSERT(expr)
#endif

#define KB(x) ((x) * 1024)
#define MB(x) (KB (KB (x)))
#define GB(x) (MB (KB (x)))

/*
** The number of pdir entries to "free" before issueing
** a read to PCOM register to flush out PCOM writes.
** Interacts with allocation granularity (ie 4 or 8 entries
** allocated and free'd/purged at a time might make this
** less interesting).
*/
#define DELAYED_RESOURCE_CNT	16

#define DEFAULT_DMA_HINT_REG(d)	0

#define ZX1_FUNC_ID_VALUE    ((PCI_DEVICE_ID_HP_ZX1_SBA << 16) | PCI_VENDOR_ID_HP)
#define ZX1_MC_ID    ((PCI_DEVICE_ID_HP_ZX1_MC << 16) | PCI_VENDOR_ID_HP)

#define SBA_FUNC_ID	0x0000	/* function id */
#define SBA_FCLASS	0x0008	/* function class, bist, header, rev... */

#define SBA_FUNC_SIZE	0x10000   /* SBA configuration function reg set */

unsigned int __initdata zx1_func_offsets[] = {0x1000, 0x4000, 0x8000,
                                              0x9000, 0xa000, -1};

#define SBA_IOC_OFFSET	0x1000

#define MAX_IOC		1	/* we only have 1 for now*/

#define IOC_IBASE	0x300	/* IO TLB */
#define IOC_IMASK	0x308
#define IOC_PCOM	0x310
#define IOC_TCNFG	0x318
#define IOC_PDIR_BASE	0x320

#define IOC_IOVA_SPACE_BASE	0x40000000 /* IOVA ranges start at 1GB */

/*
** IOC supports 4/8/16/64KB page sizes (see TCNFG register)
** It's safer (avoid memory corruption) to keep DMA page mappings
** equivalently sized to VM PAGE_SIZE.
**
** We really can't avoid generating a new mapping for each
** page since the Virtual Coherence Index has to be generated
** and updated for each page.
**
** IOVP_SIZE could only be greater than PAGE_SIZE if we are
** confident the drivers really only touch the next physical
** page iff that driver instance owns it.
*/
#define IOVP_SIZE	PAGE_SIZE
#define IOVP_SHIFT	PAGE_SHIFT
#define IOVP_MASK	PAGE_MASK

struct ioc {
	unsigned long	ioc_hpa;	/* I/O MMU base address */
	char		*res_map;	/* resource map, bit == pdir entry */
	u64		*pdir_base;	/* physical base address */
	unsigned long	ibase;		/* pdir IOV Space base */
	unsigned long	imask;		/* pdir IOV Space mask */

	unsigned long	*res_hint;	/* next avail IOVP - circular search */
	spinlock_t	res_lock;
	unsigned long	hint_mask_pdir;	/* bits used for DMA hints */
	unsigned int	res_bitshift;	/* from the RIGHT! */
	unsigned int	res_size;	/* size of resource map in bytes */
	unsigned int	hint_shift_pdir;
	unsigned long	dma_mask;
#if DELAYED_RESOURCE_CNT > 0
	int saved_cnt;
	struct sba_dma_pair {
		dma_addr_t	iova;
		size_t		size;
	} saved[DELAYED_RESOURCE_CNT];
#endif

#ifdef CONFIG_PROC_FS
#define SBA_SEARCH_SAMPLE	0x100
	unsigned long avg_search[SBA_SEARCH_SAMPLE];
	unsigned long avg_idx;	/* current index into avg_search */
	unsigned long used_pages;
	unsigned long msingle_calls;
	unsigned long msingle_pages;
	unsigned long msg_calls;
	unsigned long msg_pages;
	unsigned long usingle_calls;
	unsigned long usingle_pages;
	unsigned long usg_calls;
	unsigned long usg_pages;
#ifdef ALLOW_IOV_BYPASS
	unsigned long msingle_bypass;
	unsigned long usingle_bypass;
	unsigned long msg_bypass;
#endif
#endif

	/* STUFF We don't need in performance path */
	unsigned int	pdir_size;	/* in bytes, determined by IOV Space size */
};

struct sba_device {
	struct sba_device	*next;	/* list of SBA's in system */
	const char 		*name;
	unsigned long		sba_hpa; /* base address */
	spinlock_t		sba_lock;
	unsigned int		flags;  /* state/functionality enabled */
	unsigned int		hw_rev;  /* HW revision of chip */

	unsigned int		num_ioc;  /* number of on-board IOC's */
	struct ioc		ioc[MAX_IOC];
};


static struct sba_device *sba_list;
static int sba_count;
static int reserve_sba_gart = 1;
static struct pci_dev sac_only_dev;

#define sba_sg_address(sg) (page_address((sg)->page) + (sg)->offset)
#define sba_sg_len(sg) (sg->length)
#define sba_sg_iova(sg) (sg->dma_address)
#define sba_sg_iova_len(sg) (sg->dma_length)

/* REVISIT - fix me for multiple SBAs/IOCs */
#define GET_IOC(dev) (sba_list->ioc)
#define SBA_SET_AGP(sba_dev) (sba_dev->flags |= 0x1)
#define SBA_GET_AGP(sba_dev) (sba_dev->flags & 0x1)

/*
** DMA_CHUNK_SIZE is used by the SCSI mid-layer to break up
** (or rather not merge) DMA's into managable chunks.
** On parisc, this is more of the software/tuning constraint
** rather than the HW. I/O MMU allocation alogorithms can be
** faster with smaller size is (to some degree).
*/
#define DMA_CHUNK_SIZE  (BITS_PER_LONG*IOVP_SIZE)

/* Looks nice and keeps the compiler happy */
#define SBA_DEV(d) ((struct sba_device *) (d))

#define ROUNDUP(x,y) ((x + ((y)-1)) & ~((y)-1))

/************************************
** SBA register read and write support
**
** BE WARNED: register writes are posted.
**  (ie follow writes which must reach HW with a read)
**
*/
#define READ_REG(addr)       __raw_readq(addr)
#define WRITE_REG(val, addr) __raw_writeq(val, addr)

#ifdef DEBUG_SBA_INIT

/**
 * sba_dump_tlb - debugging only - print IOMMU operating parameters
 * @hpa: base address of the IOMMU
 *
 * Print the size/location of the IO MMU Pdir.
 */
static void
sba_dump_tlb(char *hpa)
{
	DBG_INIT("IO TLB at 0x%p\n", (void *)hpa);
	DBG_INIT("IOC_IBASE    : %016lx\n", READ_REG(hpa+IOC_IBASE));
	DBG_INIT("IOC_IMASK    : %016lx\n", READ_REG(hpa+IOC_IMASK));
	DBG_INIT("IOC_TCNFG    : %016lx\n", READ_REG(hpa+IOC_TCNFG));
	DBG_INIT("IOC_PDIR_BASE: %016lx\n", READ_REG(hpa+IOC_PDIR_BASE));
	DBG_INIT("\n");
}
#endif


#ifdef ASSERT_PDIR_SANITY

/**
 * sba_dump_pdir_entry - debugging only - print one IOMMU Pdir entry
 * @ioc: IO MMU structure which owns the pdir we are interested in.
 * @msg: text to print ont the output line.
 * @pide: pdir index.
 *
 * Print one entry of the IO MMU Pdir in human readable form.
 */
static void
sba_dump_pdir_entry(struct ioc *ioc, char *msg, uint pide)
{
	/* start printing from lowest pde in rval */
	u64 *ptr = &(ioc->pdir_base[pide  & ~(BITS_PER_LONG - 1)]);
	unsigned long *rptr = (unsigned long *) &(ioc->res_map[(pide >>3) & ~(sizeof(unsigned long) - 1)]);
	uint rcnt;

	printk(KERN_DEBUG "SBA: %s rp %p bit %d rval 0x%lx\n",
		 msg, rptr, pide & (BITS_PER_LONG - 1), *rptr);

	rcnt = 0;
	while (rcnt < BITS_PER_LONG) {
		printk(KERN_DEBUG "%s %2d %p %016Lx\n",
		       (rcnt == (pide & (BITS_PER_LONG - 1)))
		       ? "    -->" : "       ",
		       rcnt, ptr, *ptr );
		rcnt++;
		ptr++;
	}
	printk(KERN_DEBUG "%s", msg);
}


/**
 * sba_check_pdir - debugging only - consistency checker
 * @ioc: IO MMU structure which owns the pdir we are interested in.
 * @msg: text to print ont the output line.
 *
 * Verify the resource map and pdir state is consistent
 */
static int
sba_check_pdir(struct ioc *ioc, char *msg)
{
	u64 *rptr_end = (u64 *) &(ioc->res_map[ioc->res_size]);
	u64 *rptr = (u64 *) ioc->res_map;	/* resource map ptr */
	u64 *pptr = ioc->pdir_base;	/* pdir ptr */
	uint pide = 0;

	while (rptr < rptr_end) {
		u64 rval;
		int rcnt; /* number of bits we might check */

		rval = *rptr;
		rcnt = 64;

		while (rcnt) {
			/* Get last byte and highest bit from that */
			u32 pde = ((u32)((*pptr >> (63)) & 0x1));
			if ((rval & 0x1) ^ pde)
			{
				/*
				** BUMMER!  -- res_map != pdir --
				** Dump rval and matching pdir entries
				*/
				sba_dump_pdir_entry(ioc, msg, pide);
				return(1);
			}
			rcnt--;
			rval >>= 1;	/* try the next bit */
			pptr++;
			pide++;
		}
		rptr++;	/* look at next word of res_map */
	}
	/* It'd be nice if we always got here :^) */
	return 0;
}


/**
 * sba_dump_sg - debugging only - print Scatter-Gather list
 * @ioc: IO MMU structure which owns the pdir we are interested in.
 * @startsg: head of the SG list
 * @nents: number of entries in SG list
 *
 * print the SG list so we can verify it's correct by hand.
 */
static void
sba_dump_sg(struct ioc *ioc, struct scatterlist *startsg, int nents)
{
	while (nents-- > 0) {
		printk(KERN_DEBUG " %d : DMA %08lx/%05x CPU %p\n", nents,
		       (unsigned long) sba_sg_iova(startsg), sba_sg_iova_len(startsg),
		       sba_sg_address(startsg));
		startsg++;
	}
}
static void
sba_check_sg(struct ioc *ioc, struct scatterlist *startsg, int nents)
{
	struct scatterlist *the_sg = startsg;
	int the_nents = nents;

	while (the_nents-- > 0) {
		if (sba_sg_address(the_sg) == 0x0UL)
			sba_dump_sg(NULL, startsg, nents);
		the_sg++;
	}
}

#endif /* ASSERT_PDIR_SANITY */




/**************************************************************
*
*   I/O Pdir Resource Management
*
*   Bits set in the resource map are in use.
*   Each bit can represent a number of pages.
*   LSbs represent lower addresses (IOVA's).
*
***************************************************************/
#define PAGES_PER_RANGE 1	/* could increase this to 4 or 8 if needed */

/* Convert from IOVP to IOVA and vice versa. */
#define SBA_IOVA(ioc,iovp,offset,hint_reg) ((ioc->ibase) | (iovp) | (offset) | ((hint_reg)<<(ioc->hint_shift_pdir)))
#define SBA_IOVP(ioc,iova) (((iova) & ioc->hint_mask_pdir) & ~(ioc->ibase))

#define PDIR_INDEX(iovp)   ((iovp)>>IOVP_SHIFT)

#define RESMAP_MASK(n)    ~(~0UL << (n))
#define RESMAP_IDX_MASK   (sizeof(unsigned long) - 1)


/**
 * sba_search_bitmap - find free space in IO Pdir resource bitmap
 * @ioc: IO MMU structure which owns the pdir we are interested in.
 * @bits_wanted: number of entries we need.
 *
 * Find consecutive free bits in resource bitmap.
 * Each bit represents one entry in the IO Pdir.
 * Cool perf optimization: search for log2(size) bits at a time.
 */
static SBA_INLINE unsigned long
sba_search_bitmap(struct ioc *ioc, unsigned long bits_wanted)
{
	unsigned long *res_ptr = ioc->res_hint;
	unsigned long *res_end = (unsigned long *) &(ioc->res_map[ioc->res_size]);
	unsigned long pide = ~0UL;

	ASSERT(((unsigned long) ioc->res_hint & (sizeof(unsigned long) - 1UL)) == 0);
	ASSERT(res_ptr < res_end);
	if (bits_wanted > (BITS_PER_LONG/2)) {
		/* Search word at a time - no mask needed */
		for(; res_ptr < res_end; ++res_ptr) {
			if (*res_ptr == 0) {
				*res_ptr = RESMAP_MASK(bits_wanted);
				pide = ((unsigned long)res_ptr - (unsigned long)ioc->res_map);
				pide <<= 3;	/* convert to bit address */
				break;
			}
		}
		/* point to the next word on next pass */
		res_ptr++;
		ioc->res_bitshift = 0;
	} else {
		/*
		** Search the resource bit map on well-aligned values.
		** "o" is the alignment.
		** We need the alignment to invalidate I/O TLB using
		** SBA HW features in the unmap path.
		*/
		unsigned long o = 1UL << get_order(bits_wanted << IOVP_SHIFT);
		uint bitshiftcnt = ROUNDUP(ioc->res_bitshift, o);
		unsigned long mask;

		if (bitshiftcnt >= BITS_PER_LONG) {
			bitshiftcnt = 0;
			res_ptr++;
		}
		mask = RESMAP_MASK(bits_wanted) << bitshiftcnt;

		DBG_RES("%s() o %ld %p", __FUNCTION__, o, res_ptr);
		while(res_ptr < res_end)
		{ 
			DBG_RES("    %p %lx %lx\n", res_ptr, mask, *res_ptr);
			ASSERT(0 != mask);
			if(0 == ((*res_ptr) & mask)) {
				*res_ptr |= mask;     /* mark resources busy! */
				pide = ((unsigned long)res_ptr - (unsigned long)ioc->res_map);
				pide <<= 3;	/* convert to bit address */
				pide += bitshiftcnt;
				break;
			}
			mask <<= o;
			bitshiftcnt += o;
			if (0 == mask) {
				mask = RESMAP_MASK(bits_wanted);
				bitshiftcnt=0;
				res_ptr++;
			}
		}
		/* look in the same word on the next pass */
		ioc->res_bitshift = bitshiftcnt + bits_wanted;
	}

	/* wrapped ? */
	if (res_end <= res_ptr) {
		ioc->res_hint = (unsigned long *) ioc->res_map;
		ioc->res_bitshift = 0;
	} else {
		ioc->res_hint = res_ptr;
	}
	return (pide);
}


/**
 * sba_alloc_range - find free bits and mark them in IO Pdir resource bitmap
 * @ioc: IO MMU structure which owns the pdir we are interested in.
 * @size: number of bytes to create a mapping for
 *
 * Given a size, find consecutive unmarked and then mark those bits in the
 * resource bit map.
 */
static int
sba_alloc_range(struct ioc *ioc, size_t size)
{
	unsigned int pages_needed = size >> IOVP_SHIFT;
#ifdef CONFIG_PROC_FS
	unsigned long itc_start = ia64_get_itc();
#endif
	unsigned long pide;

	ASSERT(pages_needed);
	ASSERT((pages_needed * IOVP_SIZE) <= DMA_CHUNK_SIZE);
	ASSERT(pages_needed <= BITS_PER_LONG);
	ASSERT(0 == (size & ~IOVP_MASK));

	/*
	** "seek and ye shall find"...praying never hurts either...
	*/

	pide = sba_search_bitmap(ioc, pages_needed);
	if (pide >= (ioc->res_size << 3)) {
		pide = sba_search_bitmap(ioc, pages_needed);
		if (pide >= (ioc->res_size << 3))
			panic(__FILE__ ": I/O MMU @ %lx is out of mapping resources\n", ioc->ioc_hpa);
	}

#ifdef ASSERT_PDIR_SANITY
	/* verify the first enable bit is clear */
	if(0x00 != ((u8 *) ioc->pdir_base)[pide*sizeof(u64) + 7]) {
		sba_dump_pdir_entry(ioc, "sba_search_bitmap() botched it?", pide);
	}
#endif

	DBG_RES("%s(%x) %d -> %lx hint %x/%x\n",
		__FUNCTION__, size, pages_needed, pide,
		(uint) ((unsigned long) ioc->res_hint - (unsigned long) ioc->res_map),
		ioc->res_bitshift );

#ifdef CONFIG_PROC_FS
	{
		unsigned long itc_end = ia64_get_itc();
		unsigned long tmp = itc_end - itc_start;
		/* check for roll over */
		itc_start = (itc_end < itc_start) ?  -(tmp) : (tmp);
	}
	ioc->avg_search[ioc->avg_idx++] = itc_start;
	ioc->avg_idx &= SBA_SEARCH_SAMPLE - 1;

	ioc->used_pages += pages_needed;
#endif

	return (pide);
}


/**
 * sba_free_range - unmark bits in IO Pdir resource bitmap
 * @ioc: IO MMU structure which owns the pdir we are interested in.
 * @iova: IO virtual address which was previously allocated.
 * @size: number of bytes to create a mapping for
 *
 * clear bits in the ioc's resource map
 */
static SBA_INLINE void
sba_free_range(struct ioc *ioc, dma_addr_t iova, size_t size)
{
	unsigned long iovp = SBA_IOVP(ioc, iova);
	unsigned int pide = PDIR_INDEX(iovp);
	unsigned int ridx = pide >> 3;	/* convert bit to byte address */
	unsigned long *res_ptr = (unsigned long *) &((ioc)->res_map[ridx & ~RESMAP_IDX_MASK]);

	int bits_not_wanted = size >> IOVP_SHIFT;

	/* 3-bits "bit" address plus 2 (or 3) bits for "byte" == bit in word */
	unsigned long m = RESMAP_MASK(bits_not_wanted) << (pide & (BITS_PER_LONG - 1));

	DBG_RES("%s( ,%x,%x) %x/%lx %x %p %lx\n",
		__FUNCTION__, (uint) iova, size,
		bits_not_wanted, m, pide, res_ptr, *res_ptr);

#ifdef CONFIG_PROC_FS
	ioc->used_pages -= bits_not_wanted;
#endif

	ASSERT(m != 0);
	ASSERT(bits_not_wanted);
	ASSERT((bits_not_wanted * IOVP_SIZE) <= DMA_CHUNK_SIZE);
	ASSERT(bits_not_wanted <= BITS_PER_LONG);
	ASSERT((*res_ptr & m) == m); /* verify same bits are set */
	*res_ptr &= ~m;
}


/**************************************************************
*
*   "Dynamic DMA Mapping" support (aka "Coherent I/O")
*
***************************************************************/

#define SBA_DMA_HINT(ioc, val) ((val) << (ioc)->hint_shift_pdir)


/**
 * sba_io_pdir_entry - fill in one IO Pdir entry
 * @pdir_ptr:  pointer to IO Pdir entry
 * @phys_page: phys CPU address of page to map
 *
 * SBA Mapping Routine
 *
 * Given a physical address (phys_page, arg1) sba_io_pdir_entry()
 * loads the I/O Pdir entry pointed to by pdir_ptr (arg0).
 * Each IO Pdir entry consists of 8 bytes as shown below
 * (LSB == bit 0):
 *
 *  63                    40                                 11    7        0
 * +-+---------------------+----------------------------------+----+--------+
 * |V|        U            |            PPN[39:12]            | U  |   FF   |
 * +-+---------------------+----------------------------------+----+--------+
 *
 *  V  == Valid Bit
 *  U  == Unused
 * PPN == Physical Page Number
 */

#define SBA_VALID_MASK	0x80000000000000FFULL
#define sba_io_pdir_entry(pdir_ptr, phys_page) *pdir_ptr = (phys_page | SBA_VALID_MASK)
#define sba_io_page(pdir_ptr) (*pdir_ptr & ~SBA_VALID_MASK)


#ifdef ENABLE_MARK_CLEAN
/**
 * Since DMA is i-cache coherent, any (complete) pages that were written via
 * DMA can be marked as "clean" so that update_mmu_cache() doesn't have to
 * flush them when they get mapped into an executable vm-area.
 */
static void
mark_clean (void *addr, size_t size)
{
	unsigned long pg_addr, end;

	pg_addr = PAGE_ALIGN((unsigned long) addr);
	end = (unsigned long) addr + size;
	while (pg_addr + PAGE_SIZE <= end) {
		struct page *page = virt_to_page(pg_addr);
		set_bit(PG_arch_1, &page->flags);
		pg_addr += PAGE_SIZE;
	}
}
#endif

/**
 * sba_mark_invalid - invalidate one or more IO Pdir entries
 * @ioc: IO MMU structure which owns the pdir we are interested in.
 * @iova:  IO Virtual Address mapped earlier
 * @byte_cnt:  number of bytes this mapping covers.
 *
 * Marking the IO Pdir entry(ies) as Invalid and invalidate
 * corresponding IO TLB entry. The PCOM (Purge Command Register)
 * is to purge stale entries in the IO TLB when unmapping entries.
 *
 * The PCOM register supports purging of multiple pages, with a minium
 * of 1 page and a maximum of 2GB. Hardware requires the address be
 * aligned to the size of the range being purged. The size of the range
 * must be a power of 2. The "Cool perf optimization" in the
 * allocation routine helps keep that true.
 */
static SBA_INLINE void
sba_mark_invalid(struct ioc *ioc, dma_addr_t iova, size_t byte_cnt)
{
	u32 iovp = (u32) SBA_IOVP(ioc,iova);

	int off = PDIR_INDEX(iovp);

	/* Must be non-zero and rounded up */
	ASSERT(byte_cnt > 0);
	ASSERT(0 == (byte_cnt & ~IOVP_MASK));

#ifdef ASSERT_PDIR_SANITY
	/* Assert first pdir entry is set */
	if (!(ioc->pdir_base[off] >> 60)) {
		sba_dump_pdir_entry(ioc,"sba_mark_invalid()", PDIR_INDEX(iovp));
	}
#endif

	if (byte_cnt <= IOVP_SIZE)
	{
		ASSERT(off < ioc->pdir_size);

		iovp |= IOVP_SHIFT;     /* set "size" field for PCOM */

		/*
		** clear I/O Pdir entry "valid" bit
		** Do NOT clear the rest - save it for debugging.
		** We should only clear bits that have previously
		** been enabled.
		*/
		ioc->pdir_base[off] &= ~SBA_VALID_MASK;
	} else {
		u32 t = get_order(byte_cnt) + IOVP_SHIFT;

		iovp |= t;
		ASSERT(t <= 31);   /* 2GB! Max value of "size" field */

		do {
			/* verify this pdir entry is enabled */
			ASSERT(ioc->pdir_base[off]  >> 63);
			/* clear I/O Pdir entry "valid" bit first */
			ioc->pdir_base[off] &= ~SBA_VALID_MASK;
			off++;
			byte_cnt -= IOVP_SIZE;
		} while (byte_cnt > 0);
	}

	WRITE_REG(iovp, ioc->ioc_hpa+IOC_PCOM);
}

/**
 * sba_map_single - map one buffer and return IOVA for DMA
 * @dev: instance of PCI owned by the driver that's asking.
 * @addr:  driver buffer to map.
 * @size:  number of bytes to map in driver buffer.
 * @direction:  R/W or both.
 *
 * See Documentation/DMA-mapping.txt
 */
dma_addr_t
sba_map_single(struct pci_dev *dev, void *addr, size_t size, int direction)
{
	struct ioc *ioc;
	unsigned long flags; 
	dma_addr_t iovp;
	dma_addr_t offset;
	u64 *pdir_start;
	int pide;
#ifdef ALLOW_IOV_BYPASS
	unsigned long phys_addr = virt_to_phys(addr);
#endif

	if (!sba_list)
		panic("sba_map_single: no SBA found!\n");

	ioc = GET_IOC(dev);
	ASSERT(ioc);

#ifdef ALLOW_IOV_BYPASS
	/*
 	** Check if the PCI device can DMA to ptr... if so, just return ptr
 	*/
	if ((phys_addr & ~dev->dma_mask) == 0) {
		/*
 		** Device is bit capable of DMA'ing to the buffer...
		** just return the PCI address of ptr
 		*/
#ifdef CONFIG_PROC_FS
		spin_lock_irqsave(&ioc->res_lock, flags);
		ioc->msingle_bypass++;
		spin_unlock_irqrestore(&ioc->res_lock, flags);
#endif
		DBG_BYPASS("sba_map_single() bypass mask/addr: 0x%lx/0x%lx\n",
		           dev->dma_mask, phys_addr);
		return phys_addr;
	}
#endif

	ASSERT(size > 0);
	ASSERT(size <= DMA_CHUNK_SIZE);

	/* save offset bits */
	offset = ((dma_addr_t) (long) addr) & ~IOVP_MASK;

	/* round up to nearest IOVP_SIZE */
	size = (size + offset + ~IOVP_MASK) & IOVP_MASK;

	spin_lock_irqsave(&ioc->res_lock, flags);
#ifdef ASSERT_PDIR_SANITY
	if (sba_check_pdir(ioc,"Check before sba_map_single()"))
		panic("Sanity check failed");
#endif

#ifdef CONFIG_PROC_FS
	ioc->msingle_calls++;
	ioc->msingle_pages += size >> IOVP_SHIFT;
#endif
	pide = sba_alloc_range(ioc, size);
	iovp = (dma_addr_t) pide << IOVP_SHIFT;

	DBG_RUN("%s() 0x%p -> 0x%lx\n",
		__FUNCTION__, addr, (long) iovp | offset);

	pdir_start = &(ioc->pdir_base[pide]);

	while (size > 0) {
		ASSERT(((u8 *)pdir_start)[7] == 0); /* verify availability */

		sba_io_pdir_entry(pdir_start, virt_to_phys(addr));

		DBG_RUN("     pdir 0x%p %lx\n", pdir_start, *pdir_start);

		addr += IOVP_SIZE;
		size -= IOVP_SIZE;
		pdir_start++;
	}
	/* form complete address */
#ifdef ASSERT_PDIR_SANITY
	sba_check_pdir(ioc,"Check after sba_map_single()");
#endif
	spin_unlock_irqrestore(&ioc->res_lock, flags);
	return SBA_IOVA(ioc, iovp, offset, DEFAULT_DMA_HINT_REG(direction));
}

/**
 * sba_unmap_single - unmap one IOVA and free resources
 * @dev: instance of PCI owned by the driver that's asking.
 * @iova:  IOVA of driver buffer previously mapped.
 * @size:  number of bytes mapped in driver buffer.
 * @direction:  R/W or both.
 *
 * See Documentation/DMA-mapping.txt
 */
void sba_unmap_single(struct pci_dev *dev, dma_addr_t iova, size_t size,
		int direction)
{
	struct ioc *ioc;
#if DELAYED_RESOURCE_CNT > 0
	struct sba_dma_pair *d;
#endif
	unsigned long flags; 
	dma_addr_t offset;

	if (!sba_list)
		panic("sba_map_single: no SBA found!\n");

	ioc = GET_IOC(dev);
	ASSERT(ioc);

#ifdef ALLOW_IOV_BYPASS
	if ((iova & ioc->imask) != ioc->ibase) {
		/*
		** Address does not fall w/in IOVA, must be bypassing
		*/
#ifdef CONFIG_PROC_FS
		spin_lock_irqsave(&ioc->res_lock, flags);
		ioc->usingle_bypass++;
		spin_unlock_irqrestore(&ioc->res_lock, flags);
#endif
		DBG_BYPASS("sba_unmap_single() bypass addr: 0x%lx\n", iova);

#ifdef ENABLE_MARK_CLEAN
		if (direction == PCI_DMA_FROMDEVICE) {
			mark_clean(phys_to_virt(iova), size);
		}
#endif
		return;
	}
#endif
	offset = iova & ~IOVP_MASK;

	DBG_RUN("%s() iovp 0x%lx/%x\n",
		__FUNCTION__, (long) iova, size);

	iova ^= offset;        /* clear offset bits */
	size += offset;
	size = ROUNDUP(size, IOVP_SIZE);

#ifdef ENABLE_MARK_CLEAN
	/*
	** Don't need to hold the spinlock while telling VM pages are "clean".
	** The pages are "busy" in the resource map until we mark them free.
	** But tell VM pages are clean *before* releasing the resource
	** in order to avoid race conditions.
	*/
	if (direction == PCI_DMA_FROMDEVICE) {
		u32 iovp = (u32) SBA_IOVP(ioc,iova);
		unsigned int pide = PDIR_INDEX(iovp);
		u64 *pdirp = &(ioc->pdir_base[pide]);
		size_t byte_cnt = size;
		void *addr;

		do {
			addr = phys_to_virt(sba_io_page(pdirp));
			mark_clean(addr, min(byte_cnt, IOVP_SIZE));
			pdirp++;
			byte_cnt -= IOVP_SIZE;
		} while (byte_cnt > 0);
	}
#endif

	spin_lock_irqsave(&ioc->res_lock, flags);
#ifdef CONFIG_PROC_FS
	ioc->usingle_calls++;
	ioc->usingle_pages += size >> IOVP_SHIFT;
#endif

#if DELAYED_RESOURCE_CNT > 0
	d = &(ioc->saved[ioc->saved_cnt]);
	d->iova = iova;
	d->size = size;
	if (++(ioc->saved_cnt) >= DELAYED_RESOURCE_CNT) {
		int cnt = ioc->saved_cnt;
		while (cnt--) {
			sba_mark_invalid(ioc, d->iova, d->size);
			sba_free_range(ioc, d->iova, d->size);
			d--;
		}
		ioc->saved_cnt = 0;
		READ_REG(ioc->ioc_hpa+IOC_PCOM);	/* flush purges */
	}
#else /* DELAYED_RESOURCE_CNT == 0 */
	sba_mark_invalid(ioc, iova, size);
	sba_free_range(ioc, iova, size);
	READ_REG(ioc->ioc_hpa+IOC_PCOM);	/* flush purges */
#endif /* DELAYED_RESOURCE_CNT == 0 */
	spin_unlock_irqrestore(&ioc->res_lock, flags);
}


/**
 * sba_alloc_consistent - allocate/map shared mem for DMA
 * @hwdev: instance of PCI owned by the driver that's asking.
 * @size:  number of bytes mapped in driver buffer.
 * @dma_handle:  IOVA of new buffer.
 *
 * See Documentation/DMA-mapping.txt
 */
void *
sba_alloc_consistent(struct pci_dev *hwdev, size_t size, dma_addr_t *dma_handle)
{
	void *ret;

	if (!hwdev) {
		/* only support PCI */
		*dma_handle = 0;
		return 0;
	}

        ret = (void *) __get_free_pages(GFP_ATOMIC, get_order(size));

	if (ret) {
		memset(ret, 0, size);
		/*
		 * REVISIT: if sba_map_single starts needing more
		 * than dma_mask from the device, this needs to be
		 * updated.
		 */
		*dma_handle = sba_map_single(&sac_only_dev, ret, size, 0);
	}

	return ret;
}


/**
 * sba_free_consistent - free/unmap shared mem for DMA
 * @hwdev: instance of PCI owned by the driver that's asking.
 * @size:  number of bytes mapped in driver buffer.
 * @vaddr:  virtual address IOVA of "consistent" buffer.
 * @dma_handler:  IO virtual address of "consistent" buffer.
 *
 * See Documentation/DMA-mapping.txt
 */
void sba_free_consistent(struct pci_dev *hwdev, size_t size, void *vaddr,
		dma_addr_t dma_handle)
{
	sba_unmap_single(hwdev, dma_handle, size, 0);
	free_pages((unsigned long) vaddr, get_order(size));
}


#ifdef DEBUG_LARGE_SG_ENTRIES
int dump_run_sg = 0;
#endif

#define SG_ENT_VIRT_PAGE(sg) page_address((sg)->page)
#define SG_ENT_PHYS_PAGE(SG) virt_to_phys(SG_ENT_VIRT_PAGE(SG))


/**
 * sba_coalesce_chunks - preprocess the SG list
 * @ioc: IO MMU structure which owns the pdir we are interested in.
 * @startsg:  input=SG list	output=DMA addr/len pairs filled in
 * @nents: number of entries in startsg list
 * @direction: R/W or both.
 *
 * Walk the SG list and determine where the breaks are in the DMA stream.
 * Allocate IO Pdir resources and fill them in separate loop.
 * Returns the number of DMA streams used for output IOVA list.
 * Note each DMA stream can consume multiple IO Pdir entries.
 *
 * Code is written assuming some coalescing is possible.
 */
static SBA_INLINE int
sba_coalesce_chunks(struct ioc *ioc, struct scatterlist *startsg,
	int nents, int direction)
{
	struct scatterlist *dma_sg = startsg;	/* return array */
	int n_mappings = 0;

	ASSERT(nents > 1);

	do {
		unsigned int dma_cnt = 1; /* number of pages in DMA stream */
		unsigned int pide;	/* index into IO Pdir array */
		u64 *pdirp;		/* pointer into IO Pdir array */
		unsigned long dma_offset, dma_len; /* cumulative DMA stream */

		/*
		** Prepare for first/next DMA stream
		*/
		dma_len = sba_sg_len(startsg);
		dma_offset = (unsigned long) sba_sg_address(startsg);
		startsg++;
		nents--;

		/*
		** We want to know how many entries can be coalesced
		** before trying to allocate IO Pdir space.
		** IOVAs can then be allocated "naturally" aligned
		** to take advantage of the block IO TLB flush.
		*/
		while (nents) {
			unsigned long end_offset = dma_offset + dma_len;

			/* prev entry must end on a page boundary */
			if (end_offset & IOVP_MASK)
				break;

			/* next entry start on a page boundary? */
			if (startsg->offset)
				break;

			/*
			** make sure current dma stream won't exceed
			** DMA_CHUNK_SIZE if coalescing entries.
			*/
			if (((end_offset + startsg->length + ~IOVP_MASK)
								& IOVP_MASK)
					> DMA_CHUNK_SIZE)
				break;

			dma_len += sba_sg_len(startsg);
			startsg++;
			nents--;
			dma_cnt++;
		}

		ASSERT(dma_len <= DMA_CHUNK_SIZE);

		/* allocate IO Pdir resource.
		** returns index into (u64) IO Pdir array.
		** IOVA is formed from this.
		*/
		pide = sba_alloc_range(ioc, dma_cnt << IOVP_SHIFT);
		pdirp = &(ioc->pdir_base[pide]);

		/* fill_pdir: write stream into IO Pdir */
		while (dma_cnt--) {
			sba_io_pdir_entry(pdirp, SG_ENT_PHYS_PAGE(startsg));
			startsg++;
			pdirp++;
		}

		/* "output" IOVA */
		sba_sg_iova(dma_sg) = SBA_IOVA(ioc,
					((dma_addr_t) pide << IOVP_SHIFT),
					dma_offset,
					DEFAULT_DMA_HINT_REG(direction));
		sba_sg_iova_len(dma_sg) = dma_len;

		dma_sg++;
		n_mappings++;
	} while (nents);

	return n_mappings;
}


/**
 * sba_map_sg - map Scatter/Gather list
 * @dev: instance of PCI device owned by the driver that's asking.
 * @sglist:  array of buffer/length pairs
 * @nents:  number of entries in list
 * @direction:  R/W or both.
 *
 * See Documentation/DMA-mapping.txt
 */
int sba_map_sg(struct pci_dev *dev, struct scatterlist *sglist, int nents,
		int direction)
{
	struct ioc *ioc;
	int filled = 0;
	unsigned long flags;
#ifdef ALLOW_IOV_BYPASS
	struct scatterlist *sg;
#endif

	DBG_RUN_SG("%s() START %d entries, 0x%p,0x%x\n", __FUNCTION__, nents,
		sba_sg_address(sglist), sba_sg_len(sglist));

	if (!sba_list)
		panic("sba_map_single: no SBA found!\n");

	ioc = GET_IOC(dev);
	ASSERT(ioc);

#ifdef ALLOW_IOV_BYPASS
	if (dev->dma_mask >= ioc->dma_mask) {
		for (sg = sglist ; filled < nents ; filled++, sg++) {
			sba_sg_iova(sg) = virt_to_phys(sba_sg_address(sg));
			sba_sg_iova_len(sg) = sba_sg_len(sg);
		}
#ifdef CONFIG_PROC_FS
		spin_lock_irqsave(&ioc->res_lock, flags);
		ioc->msg_bypass++;
		spin_unlock_irqrestore(&ioc->res_lock, flags);
#endif
		DBG_RUN_SG("%s() DONE %d mappings bypassed\n", __FUNCTION__, filled);
		return filled;
	}
#endif
	/* Fast path single entry scatterlists. */
	if (nents == 1) {
		sba_sg_iova(sglist) = sba_map_single(dev,
						     (void *) sba_sg_iova(sglist),
						     sba_sg_len(sglist), direction);
		sba_sg_iova_len(sglist) = sba_sg_len(sglist);
#ifdef CONFIG_PROC_FS
		/*
		** Should probably do some stats counting, but trying to
		** be precise quickly starts wasting CPU time.
		*/
#endif
		DBG_RUN_SG("%s() DONE 1 mapping\n", __FUNCTION__);
		return 1;
	}

	spin_lock_irqsave(&ioc->res_lock, flags);

#ifdef ASSERT_PDIR_SANITY
	if (sba_check_pdir(ioc,"Check before sba_map_sg()"))
	{
		sba_dump_sg(ioc, sglist, nents);
		panic("Check before sba_map_sg()");
	}
#endif

#ifdef CONFIG_PROC_FS
	ioc->msg_calls++;
#endif
 
	/*
	** coalesce and program the I/O Pdir
	*/
	filled = sba_coalesce_chunks(ioc, sglist, nents, direction);

#ifdef ASSERT_PDIR_SANITY
	if (sba_check_pdir(ioc,"Check after sba_map_sg()"))
	{
		sba_dump_sg(ioc, sglist, nents);
		panic("Check after sba_map_sg()\n");
	}
#endif

	spin_unlock_irqrestore(&ioc->res_lock, flags);

	DBG_RUN_SG("%s() DONE %d mappings\n", __FUNCTION__, filled);

	return filled;
}


/**
 * sba_unmap_sg - unmap Scatter/Gather list
 * @dev: instance of PCI owned by the driver that's asking.
 * @sglist:  array of buffer/length pairs
 * @nents:  number of entries in list
 * @direction:  R/W or both.
 *
 * See Documentation/DMA-mapping.txt
 */
void sba_unmap_sg(struct pci_dev *dev, struct scatterlist *sglist, int nents,
		int direction)
{
	struct ioc *ioc;
#ifdef ASSERT_PDIR_SANITY
	unsigned long flags;
#endif

	DBG_RUN_SG("%s() START %d entries, 0x%p,0x%x\n",
		__FUNCTION__, nents, sba_sg_address(sglist), sba_sg_len(sglist));

	if (!sba_list)
		panic("sba_map_single: no SBA found!\n");

	ioc = GET_IOC(dev);
	ASSERT(ioc);

#ifdef CONFIG_PROC_FS
	ioc->usg_calls++;
#endif

#ifdef ASSERT_PDIR_SANITY
	spin_lock_irqsave(&ioc->res_lock, flags);
	sba_check_pdir(ioc,"Check before sba_unmap_sg()");
	spin_unlock_irqrestore(&ioc->res_lock, flags);
#endif

	while (sba_sg_len(sglist) && nents--) {

		sba_unmap_single(dev, (dma_addr_t)sba_sg_iova(sglist),
		                 sba_sg_iova_len(sglist), direction);
#ifdef CONFIG_PROC_FS
		/*
		** This leaves inconsistent data in the stats, but we can't
		** tell which sg lists were mapped by map_single and which
		** were coalesced to a single entry.  The stats are fun,
		** but speed is more important.
		*/
		ioc->usg_pages += (((u64)sba_sg_iova(sglist) & ~IOVP_MASK) + sba_sg_len(sglist) + IOVP_SIZE - 1) >> IOVP_SHIFT;
#endif
		++sglist;
	}

	DBG_RUN_SG("%s() DONE (nents %d)\n", __FUNCTION__,  nents);

#ifdef ASSERT_PDIR_SANITY
	spin_lock_irqsave(&ioc->res_lock, flags);
	sba_check_pdir(ioc,"Check after sba_unmap_sg()");
	spin_unlock_irqrestore(&ioc->res_lock, flags);
#endif

}

unsigned long
sba_dma_address (struct scatterlist *sg)
{
	return ((unsigned long)sba_sg_iova(sg));
}

int
sba_dma_supported (struct pci_dev *dev, u64 mask)
{
	return 1;
}

/**************************************************************
*
*   Initialization and claim
*
***************************************************************/


static void
sba_ioc_init(struct sba_device *sba_dev, struct ioc *ioc, int ioc_num)
{
	u32 iova_space_size, iova_space_mask;
	void * pdir_base;
	int pdir_size, iov_order, tcnfg;

	/*
	** Firmware programs the maximum IOV space size into the imask reg
	*/
	iova_space_size = ~(READ_REG(ioc->ioc_hpa + IOC_IMASK) & 0xFFFFFFFFUL) + 1;

	/*
	** iov_order is always based on a 1GB IOVA space since we want to
	** turn on the other half for AGP GART.
	*/
	iov_order = get_order(iova_space_size >> (IOVP_SHIFT-PAGE_SHIFT));
	ioc->pdir_size = pdir_size = (iova_space_size/IOVP_SIZE) * sizeof(u64);

	DBG_INIT("%s() hpa 0x%lx IOV %dMB (%d bits) PDIR size 0x%0x\n",
		__FUNCTION__, ioc->ioc_hpa, iova_space_size>>20,
		iov_order + PAGE_SHIFT, ioc->pdir_size);

	/* XXX DMA HINTs not used */
	ioc->hint_shift_pdir = iov_order + PAGE_SHIFT;
	ioc->hint_mask_pdir = ~(0x3 << (iov_order + PAGE_SHIFT));

	ioc->pdir_base = pdir_base =
		(void *) __get_free_pages(GFP_KERNEL, get_order(pdir_size));
	if (NULL == pdir_base)
	{
		panic(__FILE__ ":%s() could not allocate I/O Page Table\n", __FUNCTION__);
	}
	memset(pdir_base, 0, pdir_size);

	DBG_INIT("%s() pdir %p size %x hint_shift_pdir %x hint_mask_pdir %lx\n",
		__FUNCTION__, pdir_base, pdir_size,
		ioc->hint_shift_pdir, ioc->hint_mask_pdir);

	ASSERT((((unsigned long) pdir_base) & PAGE_MASK) == (unsigned long) pdir_base);
	WRITE_REG(virt_to_phys(pdir_base), ioc->ioc_hpa + IOC_PDIR_BASE);

	DBG_INIT(" base %p\n", pdir_base);

	/* build IMASK for IOC and Elroy */
	iova_space_mask =  0xffffffff;
	iova_space_mask <<= (iov_order + IOVP_SHIFT);

	ioc->ibase = READ_REG(ioc->ioc_hpa + IOC_IBASE) & 0xFFFFFFFEUL;

	ioc->imask = iova_space_mask;	/* save it */

	DBG_INIT("%s() IOV base 0x%lx mask 0x%0lx\n",
		__FUNCTION__, ioc->ibase, ioc->imask);

	/*
	** XXX DMA HINT registers are programmed with default hint
	** values during boot, so hints should be sane even if we
	** can't reprogram them the way drivers want.
	*/

	WRITE_REG(ioc->imask, ioc->ioc_hpa+IOC_IMASK);

	/*
	** Setting the upper bits makes checking for bypass addresses
	** a little faster later on.
	*/
	ioc->imask |= 0xFFFFFFFF00000000UL;

	/* Set I/O Pdir page size to system page size */
	switch (IOVP_SHIFT) {
		case 12: /* 4K */
			tcnfg = 0;
			break;
		case 13: /* 8K */
			tcnfg = 1;
			break;
		case 14: /* 16K */
			tcnfg = 2;
			break;
		case 16: /* 64K */
			tcnfg = 3;
			break;
	}
	WRITE_REG(tcnfg, ioc->ioc_hpa+IOC_TCNFG);

	/*
	** Program the IOC's ibase and enable IOVA translation
	** Bit zero == enable bit.
	*/
	WRITE_REG(ioc->ibase | 1, ioc->ioc_hpa+IOC_IBASE);

	/*
	** Clear I/O TLB of any possible entries.
	** (Yes. This is a bit paranoid...but so what)
	*/
	WRITE_REG(0 | 31, ioc->ioc_hpa+IOC_PCOM);

	/*
	** If an AGP device is present, only use half of the IOV space
	** for PCI DMA.  Unfortunately we can't know ahead of time
	** whether GART support will actually be used, for now we
	** can just key on an AGP device found in the system.
	** We program the next pdir index after we stop w/ a key for
	** the GART code to handshake on.
	*/
	if (SBA_GET_AGP(sba_dev)) {
		DBG_INIT("%s() AGP Device found, reserving 512MB for GART support\n", __FUNCTION__);
		ioc->pdir_size /= 2;
		((u64 *)pdir_base)[PDIR_INDEX(iova_space_size/2)] = 0x0000badbadc0ffeeULL;
	}

	DBG_INIT("%s() DONE\n", __FUNCTION__);
}



/**************************************************************************
**
**   SBA initialization code (HW and SW)
**
**   o identify SBA chip itself
**   o FIXME: initialize DMA hints for reasonable defaults
**
**************************************************************************/

static void
sba_hw_init(struct sba_device *sba_dev)
{ 
	int i;
	int num_ioc;
	u64 dma_mask;
	u32 func_id;

	/*
	** Identify the SBA so we can set the dma_mask.  We can make a virtual
	** dma_mask of the memory subsystem such that devices not implmenting
	** a full 64bit mask might still be able to bypass efficiently.
	*/
	func_id = READ_REG(sba_dev->sba_hpa + SBA_FUNC_ID);

	if (func_id == ZX1_FUNC_ID_VALUE) {
		dma_mask = 0xFFFFFFFFFFUL;
	} else {
		dma_mask = 0xFFFFFFFFFFFFFFFFUL;
	}

	DBG_INIT("%s(): ioc->dma_mask == 0x%lx\n", __FUNCTION__, dma_mask);
	
	/*
	** Leaving in the multiple ioc code from parisc for the future,
	** currently there are no muli-ioc mckinley sbas
	*/
	sba_dev->ioc[0].ioc_hpa = SBA_IOC_OFFSET;
	num_ioc = 1;

	sba_dev->num_ioc = num_ioc;
	for (i = 0; i < num_ioc; i++) {
		sba_dev->ioc[i].dma_mask = dma_mask;
		sba_dev->ioc[i].ioc_hpa += sba_dev->sba_hpa;
		sba_ioc_init(sba_dev, &(sba_dev->ioc[i]), i);
	}
}

static void
sba_common_init(struct sba_device *sba_dev)
{
	int i;

	/* add this one to the head of the list (order doesn't matter)
	** This will be useful for debugging - especially if we get coredumps
	*/
	sba_dev->next = sba_list;
	sba_list = sba_dev;
	sba_count++;

	for(i=0; i< sba_dev->num_ioc; i++) {
		int res_size;

		/* resource map size dictated by pdir_size */
		res_size = sba_dev->ioc[i].pdir_size/sizeof(u64); /* entries */
		res_size >>= 3;  /* convert bit count to byte count */
		DBG_INIT("%s() res_size 0x%x\n",
			__FUNCTION__, res_size);

		sba_dev->ioc[i].res_size = res_size;
		sba_dev->ioc[i].res_map = (char *) __get_free_pages(GFP_KERNEL, get_order(res_size));

		if (NULL == sba_dev->ioc[i].res_map)
		{
			panic(__FILE__ ":%s() could not allocate resource map\n", __FUNCTION__ );
		}

		memset(sba_dev->ioc[i].res_map, 0, res_size);
		/* next available IOVP - circular search */
		if ((sba_dev->hw_rev & 0xFF) >= 0x20) {
			sba_dev->ioc[i].res_hint = (unsigned long *)
			    sba_dev->ioc[i].res_map;
		} else {
			u64 reserved_iov;

			/* Yet another 1.x hack */
			printk(KERN_DEBUG "zx1 1.x: Starting resource hint offset into "
			       "IOV space to avoid initial zero value IOVA\n");
			sba_dev->ioc[i].res_hint = (unsigned long *)
			    &(sba_dev->ioc[i].res_map[L1_CACHE_BYTES]);

			sba_dev->ioc[i].res_map[0] = 0x1;
			sba_dev->ioc[i].pdir_base[0] = 0x8000badbadc0ffeeULL;

			for (reserved_iov = 0xA0000 ; reserved_iov < 0xC0000 ; reserved_iov += IOVP_SIZE) {
				u64 *res_ptr = (u64 *) sba_dev->ioc[i].res_map;
				int index = PDIR_INDEX(reserved_iov);
				int res_word;
				u64 mask;

				res_word = (int)(index / BITS_PER_LONG);
				mask =  0x1UL << (index - (res_word * BITS_PER_LONG));
				res_ptr[res_word] |= mask;
				sba_dev->ioc[i].pdir_base[PDIR_INDEX(reserved_iov)] = (SBA_VALID_MASK | reserved_iov);

			}
		}

#ifdef ASSERT_PDIR_SANITY
		/* Mark first bit busy - ie no IOVA 0 */
		sba_dev->ioc[i].res_map[0] = 0x1;
		sba_dev->ioc[i].pdir_base[0] = 0x8000badbadc0ffeeULL;
#endif

		DBG_INIT("%s() %d res_map %x %p\n", __FUNCTION__,
		         i, res_size, (void *)sba_dev->ioc[i].res_map);
	}

	sba_dev->sba_lock = SPIN_LOCK_UNLOCKED;
}

#ifdef CONFIG_PROC_FS
static int sba_proc_info(char *buf, char **start, off_t offset, int len)
{
	struct sba_device *sba_dev;
	struct ioc *ioc;
	int total_pages;
	unsigned long i = 0, avg = 0, min, max;

	for (sba_dev = sba_list; sba_dev; sba_dev = sba_dev->next) {
		ioc = &sba_dev->ioc[0];	/* FIXME: Multi-IOC support! */
		total_pages = (int) (ioc->res_size << 3); /* 8 bits per byte */

		sprintf(buf, "%s rev %d.%d\n", "Hewlett-Packard zx1 SBA",
			((sba_dev->hw_rev >> 4) & 0xF), (sba_dev->hw_rev & 0xF));
		sprintf(buf, "%sIO PDIR size    : %d bytes (%d entries)\n", buf,
			(int) ((ioc->res_size << 3) * sizeof(u64)), /* 8 bits/byte */ total_pages);

		sprintf(buf, "%sIO PDIR entries : %ld free  %ld used (%d%%)\n", buf,
			total_pages - ioc->used_pages, ioc->used_pages,
			(int) (ioc->used_pages * 100 / total_pages));

		sprintf(buf, "%sResource bitmap : %d bytes (%d pages)\n", 
			buf, ioc->res_size, ioc->res_size << 3);   /* 8 bits per byte */

		min = max = ioc->avg_search[0];
		for (i = 0; i < SBA_SEARCH_SAMPLE; i++) {
			avg += ioc->avg_search[i];
			if (ioc->avg_search[i] > max) max = ioc->avg_search[i];
			if (ioc->avg_search[i] < min) min = ioc->avg_search[i];
		}
		avg /= SBA_SEARCH_SAMPLE;
		sprintf(buf, "%s  Bitmap search : %ld/%ld/%ld (min/avg/max CPU Cycles)\n",
			buf, min, avg, max);

		sprintf(buf, "%spci_map_single(): %12ld calls  %12ld pages (avg %d/1000)\n",
			buf, ioc->msingle_calls, ioc->msingle_pages,
			(int) ((ioc->msingle_pages * 1000)/ioc->msingle_calls));
#ifdef ALLOW_IOV_BYPASS
		sprintf(buf, "%spci_map_single(): %12ld bypasses\n",
			buf, ioc->msingle_bypass);
#endif

		sprintf(buf, "%spci_unmap_single: %12ld calls  %12ld pages (avg %d/1000)\n",
			buf, ioc->usingle_calls, ioc->usingle_pages,
			(int) ((ioc->usingle_pages * 1000)/ioc->usingle_calls));
#ifdef ALLOW_IOV_BYPASS
		sprintf(buf, "%spci_unmap_single: %12ld bypasses\n",
			buf, ioc->usingle_bypass);
#endif

		sprintf(buf, "%spci_map_sg()    : %12ld calls  %12ld pages (avg %d/1000)\n",
			buf, ioc->msg_calls, ioc->msg_pages,
			(int) ((ioc->msg_pages * 1000)/ioc->msg_calls));
#ifdef ALLOW_IOV_BYPASS
		sprintf(buf, "%spci_map_sg()    : %12ld bypasses\n",
			buf, ioc->msg_bypass);
#endif

		sprintf(buf, "%spci_unmap_sg()  : %12ld calls  %12ld pages (avg %d/1000)\n",
			buf, ioc->usg_calls, ioc->usg_pages,
			(int) ((ioc->usg_pages * 1000)/ioc->usg_calls));
	}
	return strlen(buf);
}

static int
sba_resource_map(char *buf, char **start, off_t offset, int len)
{
	struct ioc *ioc = sba_list->ioc;	/* FIXME: Multi-IOC support! */
	unsigned int *res_ptr;
	int i;

	if (!ioc)
		return 0;

	res_ptr = (unsigned int *)ioc->res_map;
	buf[0] = '\0';
	for(i = 0; i < (ioc->res_size / sizeof(unsigned int)); ++i, ++res_ptr) {
		if ((i & 7) == 0)
		    strcat(buf,"\n   ");
		sprintf(buf, "%s %08x", buf, *res_ptr);
	}
	strcat(buf, "\n");

	return strlen(buf);
}
#endif

/*
** Determine if sba should claim this chip (return 0) or not (return 1).
** If so, initialize the chip and tell other partners in crime they
** have work to do.
*/
void __init sba_init(void)
{
	struct sba_device *sba_dev;
	u32 func_id, hw_rev;
	u32 *func_offset = NULL;
	int i, agp_found = 0;
	static char sba_rev[6];
	struct pci_dev *device = NULL;
	u64 hpa = 0;

	if (!(device = pci_find_device(PCI_VENDOR_ID_HP, PCI_DEVICE_ID_HP_ZX1_SBA, NULL)))
		return;

	for (i = 0; i < PCI_NUM_RESOURCES; i++) {
		if (pci_resource_flags(device, i) == IORESOURCE_MEM) {
			hpa = (u64) ioremap(pci_resource_start(device, i),
					    pci_resource_len(device, i));
			break;
		}
	}

	func_id = READ_REG(hpa + SBA_FUNC_ID);
	if (func_id != ZX1_FUNC_ID_VALUE)
		return;

	strcpy(sba_rev, "zx1");
	func_offset = zx1_func_offsets;

	/* Read HW Rev First */
	hw_rev = READ_REG(hpa + SBA_FCLASS) & 0xFFUL;

	/*
	 * Not all revision registers of the chipset are updated on every
	 * turn.  Must scan through all functions looking for the highest rev
	 */
	if (func_offset) {
		for (i = 0 ; func_offset[i] != -1 ; i++) {
			u32 func_rev;

			func_rev = READ_REG(hpa + SBA_FCLASS + func_offset[i]) & 0xFFUL;
			DBG_INIT("%s() func offset: 0x%x rev: 0x%x\n",
			         __FUNCTION__, func_offset[i], func_rev);
			if (func_rev > hw_rev)
				hw_rev = func_rev;
		}
	}

	printk(KERN_INFO "%s found %s %d.%d at %s, HPA 0x%lx\n", DRIVER_NAME,
	       sba_rev, ((hw_rev >> 4) & 0xF), (hw_rev & 0xF),
	       device->slot_name, hpa);

	if ((hw_rev & 0xFF) < 0x20) {
		printk(KERN_INFO "%s: SBA rev less than 2.0 not supported", DRIVER_NAME);
		return;
	}

	sba_dev = kmalloc(sizeof(struct sba_device), GFP_KERNEL);
	if (NULL == sba_dev) {
		printk(KERN_ERR DRIVER_NAME " - couldn't alloc sba_device\n");
		return;
	}

	memset(sba_dev, 0, sizeof(struct sba_device));

	for(i=0; i<MAX_IOC; i++)
		spin_lock_init(&(sba_dev->ioc[i].res_lock));

	sba_dev->hw_rev = hw_rev;
	sba_dev->sba_hpa = hpa;

	/*
	 * We pass this fake device from alloc_consistent to ensure
	 * we only use SAC for alloc_consistent mappings.
	 */
	sac_only_dev.dma_mask = 0xFFFFFFFFUL;

	/*
	 * We need to check for an AGP device, if we find one, then only
	 * use part of the IOVA space for PCI DMA, the rest is for GART.
	 * REVISIT for multiple IOC.
	 */
	pci_for_each_dev(device)
		agp_found |= pci_find_capability(device, PCI_CAP_ID_AGP);

	if (agp_found && reserve_sba_gart)
		SBA_SET_AGP(sba_dev);

	sba_hw_init(sba_dev);
	sba_common_init(sba_dev);

#ifdef CONFIG_PROC_FS
	{
		struct proc_dir_entry * proc_mckinley_root;

		proc_mckinley_root = proc_mkdir("bus/mckinley",0);
		create_proc_info_entry(sba_rev, 0, proc_mckinley_root, sba_proc_info);
		create_proc_info_entry("bitmap", 0, proc_mckinley_root, sba_resource_map);
	}
#endif
}

static int __init
nosbagart (char *str)
{
	reserve_sba_gart = 0;
	return 1;
}

__setup("nosbagart",nosbagart);

EXPORT_SYMBOL(sba_init);
EXPORT_SYMBOL(sba_map_single);
EXPORT_SYMBOL(sba_unmap_single);
EXPORT_SYMBOL(sba_map_sg);
EXPORT_SYMBOL(sba_unmap_sg);
EXPORT_SYMBOL(sba_dma_address);
EXPORT_SYMBOL(sba_dma_supported);
EXPORT_SYMBOL(sba_alloc_consistent);
EXPORT_SYMBOL(sba_free_consistent);
