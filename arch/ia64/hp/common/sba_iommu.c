/*
**  IA64 System Bus Adapter (SBA) I/O MMU manager
**
**	(c) Copyright 2002-2003 Alex Williamson
**	(c) Copyright 2002-2003 Grant Grundler
**	(c) Copyright 2002-2003 Hewlett-Packard Company
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
#include <linux/seq_file.h>
#include <linux/acpi.h>
#include <linux/efi.h>

#include <asm/delay.h>		/* ia64_get_itc() */
#include <asm/io.h>
#include <asm/page.h>		/* PAGE_OFFSET */
#include <asm/dma.h>
#include <asm/system.h>		/* wmb() */

#include <asm/acpi-ext.h>

#define PFX "IOC: "

/*
** This option allows cards capable of 64bit DMA to bypass the IOMMU.  If
** not defined, all DMA will be 32bit and go through the TLB.
** There's potentially a conflict in the bio merge code with us
** advertising an iommu, but then bypassing it.  Since I/O MMU bypassing
** appears to give more performance than bio-level virtual merging, we'll
** do the former for now.
*/
#define ALLOW_IOV_BYPASS

#ifdef CONFIG_PROC_FS
  /* turn it off for now; without per-CPU counters, it's too much of a scalability bottleneck: */
# define SBA_PROC_FS 0
#endif

/*
** If a device prefetches beyond the end of a valid pdir entry, it will cause
** a hard failure, ie. MCA.  Version 3.0 and later of the zx1 LBA should
** disconnect on 4k boundaries and prevent such issues.  If the device is
** particularly agressive, this option will keep the entire pdir valid such
** that prefetching will hit a valid address.  This could severely impact
** error containment, and is therefore off by default.  The page that is
** used for spill-over is poisoned, so that should help debugging somewhat.
*/
#undef FULL_VALID_PDIR

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

#if defined(FULL_VALID_PDIR) && defined(ASSERT_PDIR_SANITY)
#error FULL_VALID_PDIR and ASSERT_PDIR_SANITY are mutually exclusive
#endif

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

/*
** The number of pdir entries to "free" before issuing
** a read to PCOM register to flush out PCOM writes.
** Interacts with allocation granularity (ie 4 or 8 entries
** allocated and free'd/purged at a time might make this
** less interesting).
*/
#define DELAYED_RESOURCE_CNT	16

#define DEFAULT_DMA_HINT_REG	0

#define ZX1_IOC_ID	((PCI_DEVICE_ID_HP_ZX1_IOC << 16) | PCI_VENDOR_ID_HP)
#define REO_IOC_ID	((PCI_DEVICE_ID_HP_REO_IOC << 16) | PCI_VENDOR_ID_HP)
#define SX1000_IOC_ID	((PCI_DEVICE_ID_HP_SX1000_IOC << 16) | PCI_VENDOR_ID_HP)

#define ZX1_IOC_OFFSET	0x1000	/* ACPI reports SBA, we want IOC */

#define IOC_FUNC_ID	0x000
#define IOC_FCLASS	0x008	/* function class, bist, header, rev... */
#define IOC_IBASE	0x300	/* IO TLB */
#define IOC_IMASK	0x308
#define IOC_PCOM	0x310
#define IOC_TCNFG	0x318
#define IOC_PDIR_BASE	0x320

/* AGP GART driver looks for this */
#define ZX1_SBA_IOMMU_COOKIE	0x0000badbadc0ffeeUL

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
	void		*ioc_hpa;	/* I/O MMU base address */
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

#if SBA_PROC_FS
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

	/* Stuff we don't need in performance path */
	struct ioc	*next;		/* list of IOC's in system */
	acpi_handle	handle;		/* for multiple IOC's */
	const char 	*name;
	unsigned int	func_id;
	unsigned int	rev;		/* HW revision of chip */
	u32		iov_size;
	unsigned int	pdir_size;	/* in bytes, determined by IOV Space size */
	struct pci_dev	*sac_only_dev;
};

static struct ioc *ioc_list;
static int reserve_sba_gart = 1;

#define sba_sg_address(sg)	(page_address((sg)->page) + (sg)->offset)

#ifdef FULL_VALID_PDIR
static u64 prefetch_spill_page;
#endif

#ifdef CONFIG_PCI
# define GET_IOC(dev)	(((dev)->bus == &pci_bus_type)						\
			 ? ((struct ioc *) PCI_CONTROLLER(to_pci_dev(dev))->iommu) : NULL)
#else
# define GET_IOC(dev)	NULL
#endif

/*
** DMA_CHUNK_SIZE is used by the SCSI mid-layer to break up
** (or rather not merge) DMA's into managable chunks.
** On parisc, this is more of the software/tuning constraint
** rather than the HW. I/O MMU allocation alogorithms can be
** faster with smaller size is (to some degree).
*/
#define DMA_CHUNK_SIZE  (BITS_PER_LONG*PAGE_SIZE)

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
 * Print the size/location of the IO MMU PDIR.
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
 * sba_dump_pdir_entry - debugging only - print one IOMMU PDIR entry
 * @ioc: IO MMU structure which owns the pdir we are interested in.
 * @msg: text to print ont the output line.
 * @pide: pdir index.
 *
 * Print one entry of the IO MMU PDIR in human readable form.
 */
static void
sba_dump_pdir_entry(struct ioc *ioc, char *msg, uint pide)
{
	/* start printing from lowest pde in rval */
	u64 *ptr = &ioc->pdir_base[pide  & ~(BITS_PER_LONG - 1)];
	unsigned long *rptr = (unsigned long *) &ioc->res_map[(pide >>3) & -sizeof(unsigned long)];
	uint rcnt;

	printk(KERN_DEBUG "SBA: %s rp %p bit %d rval 0x%lx\n",
		 msg, rptr, pide & (BITS_PER_LONG - 1), *rptr);

	rcnt = 0;
	while (rcnt < BITS_PER_LONG) {
		printk(KERN_DEBUG "%s %2d %p %016Lx\n",
		       (rcnt == (pide & (BITS_PER_LONG - 1)))
		       ? "    -->" : "       ",
		       rcnt, ptr, (unsigned long long) *ptr );
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
sba_dump_sg( struct ioc *ioc, struct scatterlist *startsg, int nents)
{
	while (nents-- > 0) {
		printk(KERN_DEBUG " %d : DMA %08lx/%05x CPU %p\n", nents,
		       startsg->dma_address, startsg->dma_length,
		       sba_sg_address(startsg));
		startsg++;
	}
}

static void
sba_check_sg( struct ioc *ioc, struct scatterlist *startsg, int nents)
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
#define SBA_IOVA(ioc,iovp,offset,hint_reg) ((ioc->ibase) | (iovp) | (offset) |		\
					    ((hint_reg)<<(ioc->hint_shift_pdir)))
#define SBA_IOVP(ioc,iova) (((iova) & ioc->hint_mask_pdir) & ~(ioc->ibase))

/* FIXME : review these macros to verify correctness and usage */
#define PDIR_INDEX(iovp)   ((iovp)>>IOVP_SHIFT)

#define RESMAP_MASK(n)    ~(~0UL << (n))
#define RESMAP_IDX_MASK   (sizeof(unsigned long) - 1)


/**
 * sba_search_bitmap - find free space in IO PDIR resource bitmap
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
		unsigned long o = 1 << get_order(bits_wanted << PAGE_SHIFT);
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
 * sba_alloc_range - find free bits and mark them in IO PDIR resource bitmap
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
#if SBA_PROC_FS
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
			panic(__FILE__ ": I/O MMU @ %p is out of mapping resources\n",
			      ioc->ioc_hpa);
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

#if SBA_PROC_FS
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
 * sba_free_range - unmark bits in IO PDIR resource bitmap
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

#if SBA_PROC_FS
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
 * sba_io_pdir_entry - fill in one IO PDIR entry
 * @pdir_ptr:  pointer to IO PDIR entry
 * @vba: Virtual CPU address of buffer to map
 *
 * SBA Mapping Routine
 *
 * Given a virtual address (vba, arg1) sba_io_pdir_entry()
 * loads the I/O PDIR entry pointed to by pdir_ptr (arg0).
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
 *
 * The physical address fields are filled with the results of virt_to_phys()
 * on the vba.
 */

#if 1
#define sba_io_pdir_entry(pdir_ptr, vba) *pdir_ptr = ((vba & ~0xE000000000000FFFULL)	\
						      | 0x8000000000000000ULL)
#else
void SBA_INLINE
sba_io_pdir_entry(u64 *pdir_ptr, unsigned long vba)
{
	*pdir_ptr = ((vba & ~0xE000000000000FFFULL) | 0x80000000000000FFULL);
}
#endif

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
		struct page *page = virt_to_page((void *)pg_addr);
		set_bit(PG_arch_1, &page->flags);
		pg_addr += PAGE_SIZE;
	}
}
#endif

/**
 * sba_mark_invalid - invalidate one or more IO PDIR entries
 * @ioc: IO MMU structure which owns the pdir we are interested in.
 * @iova:  IO Virtual Address mapped earlier
 * @byte_cnt:  number of bytes this mapping covers.
 *
 * Marking the IO PDIR entry(ies) as Invalid and invalidate
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

#ifndef FULL_VALID_PDIR
		/*
		** clear I/O PDIR entry "valid" bit
		** Do NOT clear the rest - save it for debugging.
		** We should only clear bits that have previously
		** been enabled.
		*/
		ioc->pdir_base[off] &= ~(0x80000000000000FFULL);
#else
		/*
  		** If we want to maintain the PDIR as valid, put in
		** the spill page so devices prefetching won't
		** cause a hard fail.
		*/
		ioc->pdir_base[off] = (0x80000000000000FFULL | prefetch_spill_page);
#endif
	} else {
		u32 t = get_order(byte_cnt) + PAGE_SHIFT;

		iovp |= t;
		ASSERT(t <= 31);   /* 2GB! Max value of "size" field */

		do {
			/* verify this pdir entry is enabled */
			ASSERT(ioc->pdir_base[off]  >> 63);
#ifndef FULL_VALID_PDIR
			/* clear I/O Pdir entry "valid" bit first */
			ioc->pdir_base[off] &= ~(0x80000000000000FFULL);
#else
			ioc->pdir_base[off] = (0x80000000000000FFULL | prefetch_spill_page);
#endif
			off++;
			byte_cnt -= IOVP_SIZE;
		} while (byte_cnt > 0);
	}

	WRITE_REG(iovp | ioc->ibase, ioc->ioc_hpa+IOC_PCOM);
}

/**
 * sba_map_single - map one buffer and return IOVA for DMA
 * @dev: instance of PCI owned by the driver that's asking.
 * @addr:  driver buffer to map.
 * @size:  number of bytes to map in driver buffer.
 * @dir:  R/W or both.
 *
 * See Documentation/DMA-mapping.txt
 */
dma_addr_t
sba_map_single(struct device *dev, void *addr, size_t size, int dir)
{
	struct ioc *ioc;
	unsigned long flags;
	dma_addr_t iovp;
	dma_addr_t offset;
	u64 *pdir_start;
	int pide;
#ifdef ALLOW_IOV_BYPASS
	unsigned long pci_addr = virt_to_phys(addr);
#endif

	ioc = GET_IOC(dev);
	ASSERT(ioc);

#ifdef ALLOW_IOV_BYPASS
	/*
 	** Check if the PCI device can DMA to ptr... if so, just return ptr
 	*/
	if (dev && dev->dma_mask && (pci_addr & ~*dev->dma_mask) == 0) {
		/*
 		** Device is bit capable of DMA'ing to the buffer...
		** just return the PCI address of ptr
 		*/
#if SBA_PROC_FS
		spin_lock_irqsave(&ioc->res_lock, flags);
		ioc->msingle_bypass++;
		spin_unlock_irqrestore(&ioc->res_lock, flags);
#endif
		DBG_BYPASS("sba_map_single() bypass mask/addr: 0x%lx/0x%lx\n",
		           *dev->dma_mask, pci_addr);
		return pci_addr;
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

#if SBA_PROC_FS
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
		sba_io_pdir_entry(pdir_start, (unsigned long) addr);

		DBG_RUN("     pdir 0x%p %lx\n", pdir_start, *pdir_start);

		addr += IOVP_SIZE;
		size -= IOVP_SIZE;
		pdir_start++;
	}
	/* force pdir update */
	wmb();

	/* form complete address */
#ifdef ASSERT_PDIR_SANITY
	sba_check_pdir(ioc,"Check after sba_map_single()");
#endif
	spin_unlock_irqrestore(&ioc->res_lock, flags);
	return SBA_IOVA(ioc, iovp, offset, DEFAULT_DMA_HINT_REG);
}

/**
 * sba_unmap_single - unmap one IOVA and free resources
 * @dev: instance of PCI owned by the driver that's asking.
 * @iova:  IOVA of driver buffer previously mapped.
 * @size:  number of bytes mapped in driver buffer.
 * @dir:  R/W or both.
 *
 * See Documentation/DMA-mapping.txt
 */
void sba_unmap_single(struct device *dev, dma_addr_t iova, size_t size, int dir)
{
	struct ioc *ioc;
#if DELAYED_RESOURCE_CNT > 0
	struct sba_dma_pair *d;
#endif
	unsigned long flags;
	dma_addr_t offset;

	ioc = GET_IOC(dev);
	ASSERT(ioc);

#ifdef ALLOW_IOV_BYPASS
	if ((iova & ioc->imask) != ioc->ibase) {
		/*
		** Address does not fall w/in IOVA, must be bypassing
		*/
#if SBA_PROC_FS
		spin_lock_irqsave(&ioc->res_lock, flags);
		ioc->usingle_bypass++;
		spin_unlock_irqrestore(&ioc->res_lock, flags);
#endif
		DBG_BYPASS("sba_unmap_single() bypass addr: 0x%lx\n", iova);

#ifdef ENABLE_MARK_CLEAN
		if (dir == DMA_FROM_DEVICE) {
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

	spin_lock_irqsave(&ioc->res_lock, flags);
#if SBA_PROC_FS
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
#ifdef ENABLE_MARK_CLEAN
	if (dir == DMA_FROM_DEVICE) {
		u32 iovp = (u32) SBA_IOVP(ioc,iova);
		int off = PDIR_INDEX(iovp);
		void *addr;

		if (size <= IOVP_SIZE) {
			addr = phys_to_virt(ioc->pdir_base[off] &
					    ~0xE000000000000FFFULL);
			mark_clean(addr, size);
		} else {
			size_t byte_cnt = size;

			do {
				addr = phys_to_virt(ioc->pdir_base[off] &
				                    ~0xE000000000000FFFULL);
				mark_clean(addr, min(byte_cnt, IOVP_SIZE));
				off++;
				byte_cnt -= IOVP_SIZE;

			   } while (byte_cnt > 0);
		}
	}
#endif
	spin_unlock_irqrestore(&ioc->res_lock, flags);

	/* XXX REVISIT for 2.5 Linux - need syncdma for zero-copy support.
	** For Astro based systems this isn't a big deal WRT performance.
	** As long as 2.4 kernels copyin/copyout data from/to userspace,
	** we don't need the syncdma. The issue here is I/O MMU cachelines
	** are *not* coherent in all cases.  May be hwrev dependent.
	** Need to investigate more.
	asm volatile("syncdma");
	*/
}


/**
 * sba_alloc_coherent - allocate/map shared mem for DMA
 * @dev: instance of PCI owned by the driver that's asking.
 * @size:  number of bytes mapped in driver buffer.
 * @dma_handle:  IOVA of new buffer.
 *
 * See Documentation/DMA-mapping.txt
 */
void *
sba_alloc_coherent (struct device *dev, size_t size, dma_addr_t *dma_handle, int flags)
{
	struct ioc *ioc;
	void *addr;

	addr = (void *) __get_free_pages(flags, get_order(size));
	if (!addr)
		return NULL;

	/*
	 * REVISIT: if sba_map_single starts needing more than dma_mask from the
	 * device, this needs to be updated.
	 */
	ioc = GET_IOC(dev);
	ASSERT(ioc);
	*dma_handle = sba_map_single(&ioc->sac_only_dev->dev, addr, size, 0);

	memset(addr, 0, size);
	return addr;
}


/**
 * sba_free_coherent - free/unmap shared mem for DMA
 * @dev: instance of PCI owned by the driver that's asking.
 * @size:  number of bytes mapped in driver buffer.
 * @vaddr:  virtual address IOVA of "consistent" buffer.
 * @dma_handler:  IO virtual address of "consistent" buffer.
 *
 * See Documentation/DMA-mapping.txt
 */
void sba_free_coherent (struct device *dev, size_t size, void *vaddr, dma_addr_t dma_handle)
{
	sba_unmap_single(dev, dma_handle, size, 0);
	free_pages((unsigned long) vaddr, get_order(size));
}


/*
** Since 0 is a valid pdir_base index value, can't use that
** to determine if a value is valid or not. Use a flag to indicate
** the SG list entry contains a valid pdir index.
*/
#define PIDE_FLAG 0x1UL

#ifdef DEBUG_LARGE_SG_ENTRIES
int dump_run_sg = 0;
#endif


/**
 * sba_fill_pdir - write allocated SG entries into IO PDIR
 * @ioc: IO MMU structure which owns the pdir we are interested in.
 * @startsg:  list of IOVA/size pairs
 * @nents: number of entries in startsg list
 *
 * Take preprocessed SG list and write corresponding entries
 * in the IO PDIR.
 */

static SBA_INLINE int
sba_fill_pdir(
	struct ioc *ioc,
	struct scatterlist *startsg,
	int nents)
{
	struct scatterlist *dma_sg = startsg;	/* pointer to current DMA */
	int n_mappings = 0;
	u64 *pdirp = 0;
	unsigned long dma_offset = 0;

	dma_sg--;
	while (nents-- > 0) {
		int     cnt = startsg->dma_length;
		startsg->dma_length = 0;

#ifdef DEBUG_LARGE_SG_ENTRIES
		if (dump_run_sg)
			printk(" %2d : %08lx/%05x %p\n",
				nents, startsg->dma_address, cnt,
				sba_sg_address(startsg));
#else
		DBG_RUN_SG(" %d : %08lx/%05x %p\n",
				nents, startsg->dma_address, cnt,
				sba_sg_address(startsg));
#endif
		/*
		** Look for the start of a new DMA stream
		*/
		if (startsg->dma_address & PIDE_FLAG) {
			u32 pide = startsg->dma_address & ~PIDE_FLAG;
			dma_offset = (unsigned long) pide & ~IOVP_MASK;
			startsg->dma_address = 0;
			dma_sg++;
			dma_sg->dma_address = pide | ioc->ibase;
			pdirp = &(ioc->pdir_base[pide >> IOVP_SHIFT]);
			n_mappings++;
		}

		/*
		** Look for a VCONTIG chunk
		*/
		if (cnt) {
			unsigned long vaddr = (unsigned long) sba_sg_address(startsg);
			ASSERT(pdirp);

			/* Since multiple Vcontig blocks could make up
			** one DMA stream, *add* cnt to dma_len.
			*/
			dma_sg->dma_length += cnt;
			cnt += dma_offset;
			dma_offset=0;	/* only want offset on first chunk */
			cnt = ROUNDUP(cnt, IOVP_SIZE);
#if SBA_PROC_FS
			ioc->msg_pages += cnt >> IOVP_SHIFT;
#endif
			do {
				sba_io_pdir_entry(pdirp, vaddr);
				vaddr += IOVP_SIZE;
				cnt -= IOVP_SIZE;
				pdirp++;
			} while (cnt > 0);
		}
		startsg++;
	}
	/* force pdir update */
	wmb();

#ifdef DEBUG_LARGE_SG_ENTRIES
	dump_run_sg = 0;
#endif
	return(n_mappings);
}


/*
** Two address ranges are DMA contiguous *iff* "end of prev" and
** "start of next" are both on a page boundry.
**
** (shift left is a quick trick to mask off upper bits)
*/
#define DMA_CONTIG(__X, __Y) \
	(((((unsigned long) __X) | ((unsigned long) __Y)) << (BITS_PER_LONG - PAGE_SHIFT)) == 0UL)


/**
 * sba_coalesce_chunks - preprocess the SG list
 * @ioc: IO MMU structure which owns the pdir we are interested in.
 * @startsg:  list of IOVA/size pairs
 * @nents: number of entries in startsg list
 *
 * First pass is to walk the SG list and determine where the breaks are
 * in the DMA stream. Allocates PDIR entries but does not fill them.
 * Returns the number of DMA chunks.
 *
 * Doing the fill separate from the coalescing/allocation keeps the
 * code simpler. Future enhancement could make one pass through
 * the sglist do both.
 */
static SBA_INLINE int
sba_coalesce_chunks( struct ioc *ioc,
	struct scatterlist *startsg,
	int nents)
{
	struct scatterlist *vcontig_sg;    /* VCONTIG chunk head */
	unsigned long vcontig_len;         /* len of VCONTIG chunk */
	unsigned long vcontig_end;
	struct scatterlist *dma_sg;        /* next DMA stream head */
	unsigned long dma_offset, dma_len; /* start/len of DMA stream */
	int n_mappings = 0;

	while (nents > 0) {
		unsigned long vaddr = (unsigned long) sba_sg_address(startsg);

		/*
		** Prepare for first/next DMA stream
		*/
		dma_sg = vcontig_sg = startsg;
		dma_len = vcontig_len = vcontig_end = startsg->length;
		vcontig_end +=  vaddr;
		dma_offset = vaddr & ~IOVP_MASK;

		/* PARANOID: clear entries */
		startsg->dma_address = startsg->dma_length = 0;

		/*
		** This loop terminates one iteration "early" since
		** it's always looking one "ahead".
		*/
		while (--nents > 0) {
			unsigned long vaddr;	/* tmp */

			startsg++;

			/* PARANOID */
			startsg->dma_address = startsg->dma_length = 0;

			/* catch brokenness in SCSI layer */
			ASSERT(startsg->length <= DMA_CHUNK_SIZE);

			/*
			** First make sure current dma stream won't
			** exceed DMA_CHUNK_SIZE if we coalesce the
			** next entry.
			*/
			if (((dma_len + dma_offset + startsg->length + ~IOVP_MASK) & IOVP_MASK)
			    > DMA_CHUNK_SIZE)
				break;

			/*
			** Then look for virtually contiguous blocks.
			**
			** append the next transaction?
			*/
			vaddr = (unsigned long) sba_sg_address(startsg);
			if  (vcontig_end == vaddr)
			{
				vcontig_len += startsg->length;
				vcontig_end += startsg->length;
				dma_len     += startsg->length;
				continue;
			}

#ifdef DEBUG_LARGE_SG_ENTRIES
			dump_run_sg = (vcontig_len > IOVP_SIZE);
#endif

			/*
			** Not virtually contigous.
			** Terminate prev chunk.
			** Start a new chunk.
			**
			** Once we start a new VCONTIG chunk, dma_offset
			** can't change. And we need the offset from the first
			** chunk - not the last one. Ergo Successive chunks
			** must start on page boundaries and dove tail
			** with it's predecessor.
			*/
			vcontig_sg->dma_length = vcontig_len;

			vcontig_sg = startsg;
			vcontig_len = startsg->length;

			/*
			** 3) do the entries end/start on page boundaries?
			**    Don't update vcontig_end until we've checked.
			*/
			if (DMA_CONTIG(vcontig_end, vaddr))
			{
				vcontig_end = vcontig_len + vaddr;
				dma_len += vcontig_len;
				continue;
			} else {
				break;
			}
		}

		/*
		** End of DMA Stream
		** Terminate last VCONTIG block.
		** Allocate space for DMA stream.
		*/
		vcontig_sg->dma_length = vcontig_len;
		dma_len = (dma_len + dma_offset + ~IOVP_MASK) & IOVP_MASK;
		ASSERT(dma_len <= DMA_CHUNK_SIZE);
		dma_sg->dma_address = (dma_addr_t) (PIDE_FLAG
			| (sba_alloc_range(ioc, dma_len) << IOVP_SHIFT)
			| dma_offset);
		n_mappings++;
	}

	return n_mappings;
}


/**
 * sba_map_sg - map Scatter/Gather list
 * @dev: instance of PCI owned by the driver that's asking.
 * @sglist:  array of buffer/length pairs
 * @nents:  number of entries in list
 * @dir:  R/W or both.
 *
 * See Documentation/DMA-mapping.txt
 */
int sba_map_sg(struct device *dev, struct scatterlist *sglist, int nents, int dir)
{
	struct ioc *ioc;
	int coalesced, filled = 0;
	unsigned long flags;
#ifdef ALLOW_IOV_BYPASS
	struct scatterlist *sg;
#endif

	DBG_RUN_SG("%s() START %d entries\n", __FUNCTION__, nents);
	ioc = GET_IOC(dev);
	ASSERT(ioc);

#ifdef ALLOW_IOV_BYPASS
	if (dev && dev->dma_mask && (ioc->dma_mask & ~*dev->dma_mask) == 0) {
		for (sg = sglist ; filled < nents ; filled++, sg++){
			sg->dma_length = sg->length;
			sg->dma_address = virt_to_phys(sba_sg_address(sg));
		}
#if SBA_PROC_FS
		spin_lock_irqsave(&ioc->res_lock, flags);
		ioc->msg_bypass++;
		spin_unlock_irqrestore(&ioc->res_lock, flags);
#endif
		return filled;
	}
#endif
	/* Fast path single entry scatterlists. */
	if (nents == 1) {
		sglist->dma_length = sglist->length;
		sglist->dma_address = sba_map_single(dev, sba_sg_address(sglist), sglist->length,
						     dir);
#if SBA_PROC_FS
		/*
		** Should probably do some stats counting, but trying to
		** be precise quickly starts wasting CPU time.
		*/
#endif
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

#if SBA_PROC_FS
	ioc->msg_calls++;
#endif

	/*
	** First coalesce the chunks and allocate I/O pdir space
	**
	** If this is one DMA stream, we can properly map using the
	** correct virtual address associated with each DMA page.
	** w/o this association, we wouldn't have coherent DMA!
	** Access to the virtual address is what forces a two pass algorithm.
	*/
	coalesced = sba_coalesce_chunks(ioc, sglist, nents);

	/*
	** Program the I/O Pdir
	**
	** map the virtual addresses to the I/O Pdir
	** o dma_address will contain the pdir index
	** o dma_len will contain the number of bytes to map
	** o address contains the virtual address.
	*/
	filled = sba_fill_pdir(ioc, sglist, nents);

#ifdef ASSERT_PDIR_SANITY
	if (sba_check_pdir(ioc,"Check after sba_map_sg()"))
	{
		sba_dump_sg(ioc, sglist, nents);
		panic("Check after sba_map_sg()\n");
	}
#endif

	spin_unlock_irqrestore(&ioc->res_lock, flags);

	ASSERT(coalesced == filled);
	DBG_RUN_SG("%s() DONE %d mappings\n", __FUNCTION__, filled);

	return filled;
}


/**
 * sba_unmap_sg - unmap Scatter/Gather list
 * @dev: instance of PCI owned by the driver that's asking.
 * @sglist:  array of buffer/length pairs
 * @nents:  number of entries in list
 * @dir:  R/W or both.
 *
 * See Documentation/DMA-mapping.txt
 */
void sba_unmap_sg (struct device *dev, struct scatterlist *sglist, int nents, int dir)
{
	struct ioc *ioc;
#ifdef ASSERT_PDIR_SANITY
	unsigned long flags;
#endif

	DBG_RUN_SG("%s() START %d entries,  %p,%x\n",
		__FUNCTION__, nents, sba_sg_address(sglist), sglist->length);

	ioc = GET_IOC(dev);
	ASSERT(ioc);

#if SBA_PROC_FS
	ioc->usg_calls++;
#endif

#ifdef ASSERT_PDIR_SANITY
	spin_lock_irqsave(&ioc->res_lock, flags);
	sba_check_pdir(ioc,"Check before sba_unmap_sg()");
	spin_unlock_irqrestore(&ioc->res_lock, flags);
#endif

	while (nents && sglist->dma_length) {

		sba_unmap_single(dev, sglist->dma_address, sglist->dma_length, dir);
#if SBA_PROC_FS
		/*
		** This leaves inconsistent data in the stats, but we can't
		** tell which sg lists were mapped by map_single and which
		** were coalesced to a single entry.  The stats are fun,
		** but speed is more important.
		*/
		ioc->usg_pages += ((sglist->dma_address & ~IOVP_MASK) + sglist->dma_length
				   + IOVP_SIZE - 1) >> PAGE_SHIFT;
#endif
		sglist++;
		nents--;
	}

	DBG_RUN_SG("%s() DONE (nents %d)\n", __FUNCTION__,  nents);

#ifdef ASSERT_PDIR_SANITY
	spin_lock_irqsave(&ioc->res_lock, flags);
	sba_check_pdir(ioc,"Check after sba_unmap_sg()");
	spin_unlock_irqrestore(&ioc->res_lock, flags);
#endif

}

/**************************************************************
*
*   Initialization and claim
*
***************************************************************/

static void __init
ioc_iova_init(struct ioc *ioc)
{
	u32 iova_space_mask;
	int iov_order, tcnfg;
	int agp_found = 0;
	struct pci_dev *device = NULL;
#ifdef FULL_VALID_PDIR
	unsigned long index;
#endif

	/*
	** Firmware programs the base and size of a "safe IOVA space"
	** (one that doesn't overlap memory or LMMIO space) in the
	** IBASE and IMASK registers.
	*/
	ioc->ibase = READ_REG(ioc->ioc_hpa + IOC_IBASE) & ~0x1UL;
	ioc->iov_size = ~(READ_REG(ioc->ioc_hpa + IOC_IMASK) & 0xFFFFFFFFUL) + 1;

	/*
	** iov_order is always based on a 1GB IOVA space since we want to
	** turn on the other half for AGP GART.
	*/
	iov_order = get_order(ioc->iov_size >> (IOVP_SHIFT - PAGE_SHIFT));
	ioc->pdir_size = (ioc->iov_size / IOVP_SIZE) * sizeof(u64);

	DBG_INIT("%s() hpa %p IOV %dMB (%d bits) PDIR size 0x%x\n",
		 __FUNCTION__, ioc->ioc_hpa, ioc->iov_size >> 20,
		 iov_order + PAGE_SHIFT, ioc->pdir_size);

	/* FIXME : DMA HINTs not used */
	ioc->hint_shift_pdir = iov_order + PAGE_SHIFT;
	ioc->hint_mask_pdir = ~(0x3 << (iov_order + PAGE_SHIFT));

	ioc->pdir_base = (void *) __get_free_pages(GFP_KERNEL,
						   get_order(ioc->pdir_size));
	if (!ioc->pdir_base)
		panic(PFX "Couldn't allocate I/O Page Table\n");

	memset(ioc->pdir_base, 0, ioc->pdir_size);

	DBG_INIT("%s() pdir %p size %x hint_shift_pdir %x hint_mask_pdir %lx\n",
		__FUNCTION__, ioc->pdir_base, ioc->pdir_size,
		ioc->hint_shift_pdir, ioc->hint_mask_pdir);

	ASSERT((((unsigned long) ioc->pdir_base) & PAGE_MASK) == (unsigned long) ioc->pdir_base);
	WRITE_REG(virt_to_phys(ioc->pdir_base), ioc->ioc_hpa + IOC_PDIR_BASE);

	DBG_INIT(" base %p\n", ioc->pdir_base);

	/* build IMASK for IOC and Elroy */
	iova_space_mask =  0xffffffff;
	iova_space_mask <<= (iov_order + PAGE_SHIFT);
	ioc->imask = iova_space_mask;

	DBG_INIT("%s() IOV base 0x%lx mask 0x%0lx\n",
		__FUNCTION__, ioc->ibase, ioc->imask);

	/*
	** FIXME: Hint registers are programmed with default hint
	** values during boot, so hints should be sane even if we
	** can't reprogram them the way drivers want.
	*/
	WRITE_REG(ioc->imask, ioc->ioc_hpa + IOC_IMASK);

	/*
	** Setting the upper bits makes checking for bypass addresses
	** a little faster later on.
	*/
	ioc->imask |= 0xFFFFFFFF00000000UL;

	/* Set I/O PDIR Page size to system page size */
	switch (PAGE_SHIFT) {
		case 12: tcnfg = 0; break;	/*  4K */
		case 13: tcnfg = 1; break;	/*  8K */
		case 14: tcnfg = 2; break;	/* 16K */
		case 16: tcnfg = 3; break;	/* 64K */
		default:
			panic(PFX "Unsupported system page size %d",
				1 << PAGE_SHIFT);
			break;
	}
	WRITE_REG(tcnfg, ioc->ioc_hpa + IOC_TCNFG);

	/*
	** Program the IOC's ibase and enable IOVA translation
	** Bit zero == enable bit.
	*/
	WRITE_REG(ioc->ibase | 1, ioc->ioc_hpa + IOC_IBASE);

	/*
	** Clear I/O TLB of any possible entries.
	** (Yes. This is a bit paranoid...but so what)
	*/
	WRITE_REG(ioc->ibase | (iov_order+PAGE_SHIFT), ioc->ioc_hpa + IOC_PCOM);

	/*
	** If an AGP device is present, only use half of the IOV space
	** for PCI DMA.  Unfortunately we can't know ahead of time
	** whether GART support will actually be used, for now we
	** can just key on an AGP device found in the system.
	** We program the next pdir index after we stop w/ a key for
	** the GART code to handshake on.
	*/
	while ((device = pci_find_device(PCI_ANY_ID, PCI_ANY_ID, device)) != NULL)
		agp_found |= pci_find_capability(device, PCI_CAP_ID_AGP);

	if (agp_found && reserve_sba_gart) {
		DBG_INIT("%s: AGP device found, reserving half of IOVA for GART support\n",
			 __FUNCTION__);
		ioc->pdir_size /= 2;
		((u64 *)ioc->pdir_base)[PDIR_INDEX(ioc->iov_size/2)] = ZX1_SBA_IOMMU_COOKIE;
	}
#ifdef FULL_VALID_PDIR
	/*
  	** Check to see if the spill page has been allocated, we don't need more than
	** one across multiple SBAs.
	*/
	if (!prefetch_spill_page) {
		char *spill_poison = "SBAIOMMU POISON";
		int poison_size = 16;
		void *poison_addr, *addr;

		addr = (void *)__get_free_pages(GFP_KERNEL, get_order(IOVP_SIZE));
		if (!addr)
			panic(PFX "Couldn't allocate PDIR spill page\n");

		poison_addr = addr;
		for ( ; (u64) poison_addr < addr + IOVP_SIZE; poison_addr += poison_size)
			memcpy(poison_addr, spill_poison, poison_size);

		prefetch_spill_page = virt_to_phys(addr);

		DBG_INIT("%s() prefetch spill addr: 0x%lx\n", __FUNCTION__, prefetch_spill_page);
	}
	/*
  	** Set all the PDIR entries valid w/ the spill page as the target
	*/
	for (index = 0 ; index < (ioc->pdir_size / sizeof(u64)) ; index++)
		((u64 *)ioc->pdir_base)[index] = (0x80000000000000FF | prefetch_spill_page);
#endif

}

static void __init
ioc_resource_init(struct ioc *ioc)
{
	spin_lock_init(&ioc->res_lock);

	/* resource map size dictated by pdir_size */
	ioc->res_size = ioc->pdir_size / sizeof(u64); /* entries */
	ioc->res_size >>= 3;  /* convert bit count to byte count */
	DBG_INIT("%s() res_size 0x%x\n", __FUNCTION__, ioc->res_size);

	ioc->res_map = (char *) __get_free_pages(GFP_KERNEL,
						 get_order(ioc->res_size));
	if (!ioc->res_map)
		panic(PFX "Couldn't allocate resource map\n");

	memset(ioc->res_map, 0, ioc->res_size);
	/* next available IOVP - circular search */
	ioc->res_hint = (unsigned long *) ioc->res_map;

#ifdef ASSERT_PDIR_SANITY
	/* Mark first bit busy - ie no IOVA 0 */
	ioc->res_map[0] = 0x1;
	ioc->pdir_base[0] = 0x8000000000000000ULL | ZX1_SBA_IOMMU_COOKIE;
#endif
#ifdef FULL_VALID_PDIR
	/* Mark the last resource used so we don't prefetch beyond IOVA space */
	ioc->res_map[ioc->res_size - 1] |= 0x80UL; /* res_map is chars */
	ioc->pdir_base[(ioc->pdir_size / sizeof(u64)) - 1] = (0x80000000000000FF
							      | prefetch_spill_page);
#endif

	DBG_INIT("%s() res_map %x %p\n", __FUNCTION__,
		 ioc->res_size, (void *) ioc->res_map);
}

static void __init
ioc_sac_init(struct ioc *ioc)
{
	struct pci_dev *sac = NULL;
	struct pci_controller *controller = NULL;

	/*
	 * pci_alloc_coherent() must return a DMA address which is
	 * SAC (single address cycle) addressable, so allocate a
	 * pseudo-device to enforce that.
	 */
	sac = kmalloc(sizeof(*sac), GFP_KERNEL);
	if (!sac)
		panic(PFX "Couldn't allocate struct pci_dev");
	memset(sac, 0, sizeof(*sac));

	controller = kmalloc(sizeof(*controller), GFP_KERNEL);
	if (!controller)
		panic(PFX "Couldn't allocate struct pci_controller");
	memset(controller, 0, sizeof(*controller));

	controller->iommu = ioc;
	sac->sysdata = controller;
	sac->dma_mask = 0xFFFFFFFFUL;
#ifdef CONFIG_PCI
	sac->dev.bus = &pci_bus_type;
#endif
	ioc->sac_only_dev = sac;
}

static void __init
ioc_zx1_init(struct ioc *ioc)
{
	if (ioc->rev < 0x20)
		panic(PFX "IOC 2.0 or later required for IOMMU support\n");

	ioc->dma_mask = 0xFFFFFFFFFFUL;
}

typedef void (initfunc)(struct ioc *);

struct ioc_iommu {
	u32 func_id;
	char *name;
	initfunc *init;
};

static struct ioc_iommu ioc_iommu_info[] __initdata = {
	{ ZX1_IOC_ID, "zx1", ioc_zx1_init },
	{ REO_IOC_ID, "REO" },
	{ SX1000_IOC_ID, "sx1000" },
};

static struct ioc * __init
ioc_init(u64 hpa, void *handle)
{
	struct ioc *ioc;
	struct ioc_iommu *info;

	ioc = kmalloc(sizeof(*ioc), GFP_KERNEL);
	if (!ioc)
		return NULL;

	memset(ioc, 0, sizeof(*ioc));

	ioc->next = ioc_list;
	ioc_list = ioc;

	ioc->handle = handle;
	ioc->ioc_hpa = ioremap(hpa, 0x1000);

	ioc->func_id = READ_REG(ioc->ioc_hpa + IOC_FUNC_ID);
	ioc->rev = READ_REG(ioc->ioc_hpa + IOC_FCLASS) & 0xFFUL;
	ioc->dma_mask = 0xFFFFFFFFFFFFFFFFUL;	/* conservative */

	for (info = ioc_iommu_info; info < ioc_iommu_info + ARRAY_SIZE(ioc_iommu_info); info++) {
		if (ioc->func_id == info->func_id) {
			ioc->name = info->name;
			if (info->init)
				(info->init)(ioc);
		}
	}

	if (!ioc->name) {
		ioc->name = kmalloc(24, GFP_KERNEL);
		if (ioc->name)
			sprintf((char *) ioc->name, "Unknown (%04x:%04x)",
				ioc->func_id & 0xFFFF, (ioc->func_id >> 16) & 0xFFFF);
		else
			ioc->name = "Unknown";
	}

	ioc_iova_init(ioc);
	ioc_resource_init(ioc);
	ioc_sac_init(ioc);

	if ((long) ~IOVP_MASK > (long) ia64_max_iommu_merge_mask)
		ia64_max_iommu_merge_mask = ~IOVP_MASK;
	MAX_DMA_ADDRESS = ~0UL;

	printk(KERN_INFO PFX
		"%s %d.%d HPA 0x%lx IOVA space %dMb at 0x%lx\n",
		ioc->name, (ioc->rev >> 4) & 0xF, ioc->rev & 0xF,
		hpa, ioc->iov_size >> 20, ioc->ibase);

	return ioc;
}



/**************************************************************************
**
**   SBA initialization code (HW and SW)
**
**   o identify SBA chip itself
**   o FIXME: initialize DMA hints for reasonable defaults
**
**************************************************************************/

#if SBA_PROC_FS
static void *
ioc_start(struct seq_file *s, loff_t *pos)
{
	struct ioc *ioc;
	loff_t n = *pos;

	for (ioc = ioc_list; ioc; ioc = ioc->next)
		if (!n--)
			return ioc;

	return NULL;
}

static void *
ioc_next(struct seq_file *s, void *v, loff_t *pos)
{
	struct ioc *ioc = v;

	++*pos;
	return ioc->next;
}

static void
ioc_stop(struct seq_file *s, void *v)
{
}

static int
ioc_show(struct seq_file *s, void *v)
{
	struct ioc *ioc = v;
	int total_pages = (int) (ioc->res_size << 3); /* 8 bits per byte */
	unsigned long i = 0, avg = 0, min, max;

	seq_printf(s, "Hewlett Packard %s IOC rev %d.%d\n",
		ioc->name, ((ioc->rev >> 4) & 0xF), (ioc->rev & 0xF));
	seq_printf(s, "IO PDIR size    : %d bytes (%d entries)\n",
		(int) ((ioc->res_size << 3) * sizeof(u64)), /* 8 bits/byte */
		total_pages);

	seq_printf(s, "IO PDIR entries : %ld free  %ld used (%d%%)\n",
		total_pages - ioc->used_pages, ioc->used_pages,
		(int) (ioc->used_pages * 100 / total_pages));

	seq_printf(s, "Resource bitmap : %d bytes (%d pages)\n",
		ioc->res_size, ioc->res_size << 3);   /* 8 bits per byte */

	min = max = ioc->avg_search[0];
	for (i = 0; i < SBA_SEARCH_SAMPLE; i++) {
		avg += ioc->avg_search[i];
		if (ioc->avg_search[i] > max) max = ioc->avg_search[i];
		if (ioc->avg_search[i] < min) min = ioc->avg_search[i];
	}
	avg /= SBA_SEARCH_SAMPLE;
	seq_printf(s, "  Bitmap search : %ld/%ld/%ld (min/avg/max CPU Cycles)\n", min, avg, max);

	seq_printf(s, "pci_map_single(): %12ld calls  %12ld pages (avg %d/1000)\n",
		   ioc->msingle_calls, ioc->msingle_pages,
		   (int) ((ioc->msingle_pages * 1000)/ioc->msingle_calls));
#ifdef ALLOW_IOV_BYPASS
	seq_printf(s, "pci_map_single(): %12ld bypasses\n", ioc->msingle_bypass);
#endif

	seq_printf(s, "pci_unmap_single: %12ld calls  %12ld pages (avg %d/1000)\n",
		   ioc->usingle_calls, ioc->usingle_pages,
		   (int) ((ioc->usingle_pages * 1000)/ioc->usingle_calls));
#ifdef ALLOW_IOV_BYPASS
	seq_printf(s, "pci_unmap_single: %12ld bypasses\n", ioc->usingle_bypass);
#endif

	seq_printf(s, "pci_map_sg()    : %12ld calls  %12ld pages (avg %d/1000)\n",
		   ioc->msg_calls, ioc->msg_pages,
		   (int) ((ioc->msg_pages * 1000)/ioc->msg_calls));
#ifdef ALLOW_IOV_BYPASS
	seq_printf(s, "pci_map_sg()    : %12ld bypasses\n", ioc->msg_bypass);
#endif

	seq_printf(s, "pci_unmap_sg()  : %12ld calls  %12ld pages (avg %d/1000)\n",
		   ioc->usg_calls, ioc->usg_pages, (int) ((ioc->usg_pages * 1000)/ioc->usg_calls));

	return 0;
}

static struct seq_operations ioc_seq_ops = {
	.start = ioc_start,
	.next  = ioc_next,
	.stop  = ioc_stop,
	.show  = ioc_show
};

static int
ioc_open(struct inode *inode, struct file *file)
{
	return seq_open(file, &ioc_seq_ops);
}

static struct file_operations ioc_fops = {
	.open    = ioc_open,
	.read    = seq_read,
	.llseek  = seq_lseek,
	.release = seq_release
};

static int
ioc_map_show(struct seq_file *s, void *v)
{
	struct ioc *ioc = v;
	unsigned int i, *res_ptr = (unsigned int *)ioc->res_map;

	for (i = 0; i < ioc->res_size / sizeof(unsigned int); ++i, ++res_ptr)
		seq_printf(s, "%s%08x", (i & 7) ? " " : "\n   ", *res_ptr);
	seq_printf(s, "\n");

	return 0;
}

static struct seq_operations ioc_map_ops = {
	.start = ioc_start,
	.next  = ioc_next,
	.stop  = ioc_stop,
	.show  = ioc_map_show
};

static int
ioc_map_open(struct inode *inode, struct file *file)
{
	return seq_open(file, &ioc_map_ops);
}

static struct file_operations ioc_map_fops = {
	.open    = ioc_map_open,
	.read    = seq_read,
	.llseek  = seq_lseek,
	.release = seq_release
};

static void __init
ioc_proc_init(void)
{
	struct proc_dir_entry *dir, *entry;

	dir = proc_mkdir("bus/mckinley", 0);
	if (!dir)
		return;

	entry = create_proc_entry(ioc_list->name, 0, dir);
	if (entry)
		entry->proc_fops = &ioc_fops;

	entry = create_proc_entry("bitmap", 0, dir);
	if (entry)
		entry->proc_fops = &ioc_map_fops;
}
#endif

static void
sba_connect_bus(struct pci_bus *bus)
{
	acpi_handle handle, parent;
	acpi_status status;
	struct ioc *ioc;

	if (!PCI_CONTROLLER(bus))
		panic(PFX "no sysdata on bus %d!\n", bus->number);

	if (PCI_CONTROLLER(bus)->iommu)
		return;

	handle = PCI_CONTROLLER(bus)->acpi_handle;
	if (!handle)
		return;

	/*
	 * The IOC scope encloses PCI root bridges in the ACPI
	 * namespace, so work our way out until we find an IOC we
	 * claimed previously.
	 */
	do {
		for (ioc = ioc_list; ioc; ioc = ioc->next)
			if (ioc->handle == handle) {
				PCI_CONTROLLER(bus)->iommu = ioc;
				return;
			}

		status = acpi_get_parent(handle, &parent);
		handle = parent;
	} while (ACPI_SUCCESS(status));

	printk(KERN_WARNING "No IOC for PCI Bus %04x:%02x in ACPI\n", pci_domain_nr(bus), bus->number);
}

static int __init
acpi_sba_ioc_add(struct acpi_device *device)
{
	struct ioc *ioc;
	acpi_status status;
	u64 hpa, length;
	struct acpi_buffer buffer;
	struct acpi_device_info *dev_info;

	status = hp_acpi_csr_space(device->handle, &hpa, &length);
	if (ACPI_FAILURE(status))
		return 1;

	buffer.length = ACPI_ALLOCATE_LOCAL_BUFFER;
	status = acpi_get_object_info(device->handle, &buffer);
	if (ACPI_FAILURE(status))
		return 1;
	dev_info = buffer.pointer;

	/*
	 * For HWP0001, only SBA appears in ACPI namespace.  It encloses the PCI
	 * root bridges, and its CSR space includes the IOC function.
	 */
	if (strncmp("HWP0001", dev_info->hardware_id.value, 7) == 0)
		hpa += ZX1_IOC_OFFSET;
	ACPI_MEM_FREE(dev_info);

	ioc = ioc_init(hpa, device->handle);
	if (!ioc)
		return 1;

	return 0;
}

static struct acpi_driver acpi_sba_ioc_driver = {
	.name		= "IOC IOMMU Driver",
	.ids		= "HWP0001,HWP0004",
	.ops		= {
		.add	= acpi_sba_ioc_add,
	},
};

static int __init
sba_init(void)
{
	acpi_bus_register_driver(&acpi_sba_ioc_driver);
	if (!ioc_list)
		return 0;

#ifdef CONFIG_PCI
	{
		struct pci_bus *b = NULL;
		while ((b = pci_find_next_bus(b)) != NULL)
			sba_connect_bus(b);
	}
#endif

#if SBA_PROC_FS
	ioc_proc_init();
#endif
	return 0;
}

subsys_initcall(sba_init); /* must be initialized after ACPI etc., but before any drivers... */

static int __init
nosbagart(char *str)
{
	reserve_sba_gart = 0;
	return 1;
}

int
sba_dma_supported (struct device *dev, u64 mask)
{
	/* make sure it's at least 32bit capable */
	return ((mask & 0xFFFFFFFFUL) == 0xFFFFFFFFUL);
}

__setup("nosbagart", nosbagart);

EXPORT_SYMBOL(sba_map_single);
EXPORT_SYMBOL(sba_unmap_single);
EXPORT_SYMBOL(sba_map_sg);
EXPORT_SYMBOL(sba_unmap_sg);
EXPORT_SYMBOL(sba_dma_supported);
EXPORT_SYMBOL(sba_alloc_coherent);
EXPORT_SYMBOL(sba_free_coherent);
