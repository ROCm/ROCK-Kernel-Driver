/*
** ccio-dma.c:
**	DMA management routines for first generation cache-coherent machines.
**	Program U2/Uturn in "Virtual Mode" and use the I/O MMU.
**
**	(c) Copyright 2000 Grant Grundler
**	(c) Copyright 2000 Ryan Bradetich
**	(c) Copyright 2000 Hewlett-Packard Company
**
** This program is free software; you can redistribute it and/or modify
** it under the terms of the GNU General Public License as published by
** the Free Software Foundation; either version 2 of the License, or
** (at your option) any later version.
**
**
**  "Real Mode" operation refers to U2/Uturn chip operation.
**  U2/Uturn were designed to perform coherency checks w/o using
**  the I/O MMU - basically what x86 does.
**
**  Philipp Rumpf has a "Real Mode" driver for PCX-W machines at:
**      CVSROOT=:pserver:anonymous@198.186.203.37:/cvsroot/linux-parisc
**      cvs -z3 co linux/arch/parisc/kernel/dma-rm.c
**
**  I've rewritten his code to work under TPG's tree. See ccio-rm-dma.c.
**
**  Drawbacks of using Real Mode are:
**	o outbound DMA is slower - U2 won't prefetch data (GSC+ XQL signal).
**      o Inbound DMA less efficient - U2 can't use DMA_FAST attribute.
**	o Ability to do scatter/gather in HW is lost.
**	o Doesn't work under PCX-U/U+ machines since they didn't follow
**        the coherency design originally worked out. Only PCX-W does.
*/

#include <linux/config.h>
#include <linux/types.h>
#include <linux/init.h>
#include <linux/mm.h>
#include <linux/spinlock.h>
#include <linux/malloc.h>
#include <linux/string.h>
#include <linux/pci.h>

#include <asm/byteorder.h>
#include <asm/cache.h>		/* for L1_CACHE_BYTES */
#include <asm/uaccess.h>
#include <asm/pgalloc.h>
#include <asm/page.h>

#include <asm/io.h>
#include <asm/gsc.h>		/* for gsc_writeN()... */

/* 
** Choose "ccio" since that's what HP-UX calls it.
** Make it easier for folks to migrate from one to the other :^)
*/
#define MODULE_NAME "ccio"

/*
#define DEBUG_CCIO_RES
#define DEBUG_CCIO_RUN
#define DEBUG_CCIO_INIT
#define DUMP_RESMAP
*/

#include <linux/proc_fs.h>
#include <asm/runway.h>		/* for proc_runway_root */

#ifdef DEBUG_CCIO_INIT
#define DBG_INIT(x...)  printk(x)
#else
#define DBG_INIT(x...)
#endif

#ifdef DEBUG_CCIO_RUN
#define DBG_RUN(x...)   printk(x)
#else
#define DBG_RUN(x...)
#endif

#ifdef DEBUG_CCIO_RES
#define DBG_RES(x...)   printk(x)
#else
#define DBG_RES(x...)
#endif

#define CCIO_INLINE	/* inline */
#define WRITE_U32(value, addr) gsc_writel(value, (u32 *) (addr))

#define U2_IOA_RUNWAY 0x580
#define U2_BC_GSC     0x501
#define UTURN_IOA_RUNWAY 0x581
#define UTURN_BC_GSC     0x502
/* We *can't* support JAVA (T600). Venture there at your own risk. */

static void dump_resmap(void);

static int ccio_driver_callback(struct hp_device *, struct pa_iodc_driver *);

static struct pa_iodc_driver ccio_drivers_for[] = {

   {HPHW_IOA, U2_IOA_RUNWAY, 0x0, 0xb, 0, 0x10,
		DRIVER_CHECK_HVERSION +
		DRIVER_CHECK_SVERSION + DRIVER_CHECK_HWTYPE,
                MODULE_NAME, "U2 I/O MMU", (void *) ccio_driver_callback},

   {HPHW_IOA, UTURN_IOA_RUNWAY, 0x0, 0xb, 0, 0x10,
		DRIVER_CHECK_HVERSION +
		DRIVER_CHECK_SVERSION + DRIVER_CHECK_HWTYPE,
                MODULE_NAME, "Uturn I/O MMU", (void *) ccio_driver_callback},

/*
** FIXME: The following claims the GSC bus port, not the IOA.
**        And there are two busses below a single I/O TLB.
**
** These should go away once we have a real PA bus walk.
** Firmware wants to tell the PA bus walk code about the GSC ports
** since they are not "architected" PA I/O devices. Ie a PA bus walk
** wouldn't discover them. But the PA bus walk code could check
** the "fixed module table" to add such devices to an I/O Tree
** and proceed with the recursive, depth first bus walk.
*/
   {HPHW_BCPORT, U2_BC_GSC, 0x0, 0xc, 0, 0x10,
		DRIVER_CHECK_HVERSION +
		DRIVER_CHECK_SVERSION + DRIVER_CHECK_HWTYPE,
                MODULE_NAME, "U2 GSC+ BC", (void *) ccio_driver_callback},

   {HPHW_BCPORT, UTURN_BC_GSC, 0x0, 0xc, 0, 0x10,
		DRIVER_CHECK_HVERSION +
		DRIVER_CHECK_SVERSION + DRIVER_CHECK_HWTYPE,
                MODULE_NAME, "Uturn GSC+ BC", (void *) ccio_driver_callback},

   {0,0,0,0,0,0,
   0,
   (char *) NULL, (char *) NULL, (void *) NULL }
};


#define IS_U2(id) ( \
    (((id)->hw_type == HPHW_IOA) && ((id)->hversion == U2_IOA_RUNWAY)) || \
    (((id)->hw_type == HPHW_BCPORT) && ((id)->hversion == U2_BC_GSC))  \
)

#define IS_UTURN(id) ( \
    (((id)->hw_type == HPHW_IOA) && ((id)->hversion == UTURN_IOA_RUNWAY)) || \
    (((id)->hw_type == HPHW_BCPORT) && ((id)->hversion == UTURN_BC_GSC))  \
)


#define IOA_NORMAL_MODE      0x00020080 /* IO_CONTROL to turn on CCIO        */
#define CMD_TLB_DIRECT_WRITE 35         /* IO_COMMAND for I/O TLB Writes     */
#define CMD_TLB_PURGE        33         /* IO_COMMAND to Purge I/O TLB entry */

struct ioa_registers {
        /* Runway Supervisory Set */
        volatile int32_t    unused1[12];
        volatile uint32_t   io_command;             /* Offset 12 */
        volatile uint32_t   io_status;              /* Offset 13 */
        volatile uint32_t   io_control;             /* Offset 14 */
        volatile int32_t    unused2[1];

        /* Runway Auxiliary Register Set */
        volatile uint32_t   io_err_resp;            /* Offset  0 */
        volatile uint32_t   io_err_info;            /* Offset  1 */
        volatile uint32_t   io_err_req;             /* Offset  2 */
        volatile uint32_t   io_err_resp_hi;         /* Offset  3 */
        volatile uint32_t   io_tlb_entry_m;         /* Offset  4 */
        volatile uint32_t   io_tlb_entry_l;         /* Offset  5 */
        volatile uint32_t   unused3[1];
        volatile uint32_t   io_pdir_base;           /* Offset  7 */
        volatile uint32_t   io_io_low_hv;           /* Offset  8 */
        volatile uint32_t   io_io_high_hv;          /* Offset  9 */
        volatile uint32_t   unused4[1];
        volatile uint32_t   io_chain_id_mask;       /* Offset 11 */
        volatile uint32_t   unused5[2];
        volatile uint32_t   io_io_low;              /* Offset 14 */
        volatile uint32_t   io_io_high;             /* Offset 15 */
};


struct ccio_device {
	struct ccio_device    *next;  /* list of LBA's in system */
	struct hp_device      *iodc;  /* data about dev from firmware */
	spinlock_t            ccio_lock;

	struct ioa_registers   *ccio_hpa; /* base address */
	u64   *pdir_base;   /* physical base address */
	char  *res_map;     /* resource map, bit == pdir entry */

	int   res_hint;	/* next available IOVP - circular search */
	int   res_size;	/* size of resource map in bytes */
	int   chainid_shift; /* specify bit location of chain_id */
	int   flags;         /* state/functionality enabled */
#ifdef DELAYED_RESOURCE_CNT
	dma_addr_t res_delay[DELAYED_RESOURCE_CNT];
#endif

	/* STUFF We don't need in performance path */
	int  pdir_size;     /* in bytes, determined by IOV Space size */
	int  hw_rev;        /* HW revision of chip */
};


/* Ratio of Host MEM to IOV Space size */
static unsigned long ccio_mem_ratio = 4;
static struct ccio_device *ccio_list = NULL;

static int ccio_proc_info(char *buffer, char **start, off_t offset, int length);
static unsigned long ccio_used_bytes = 0;
static unsigned long ccio_used_pages = 0;
static int ccio_cujo_bug = 0;

static unsigned long ccio_alloc_size = 0;
static unsigned long ccio_free_size = 0;

/**************************************************************
*
*   I/O Pdir Resource Management
*
*   Bits set in the resource map are in use.
*   Each bit can represent a number of pages.
*   LSbs represent lower addresses (IOVA's).
*
*   This was was copied from sba_iommu.c. Don't try to unify
*   the two resource managers unless a way to have different
*   allocation policies is also adjusted. We'd like to avoid
*   I/O TLB thrashing by having resource allocation policy
*   match the I/O TLB replacement policy.
*
***************************************************************/
#define PAGES_PER_RANGE 1	/* could increase this to 4 or 8 if needed */
#define IOVP_SIZE PAGE_SIZE
#define IOVP_SHIFT PAGE_SHIFT
#define IOVP_MASK PAGE_MASK

/* Convert from IOVP to IOVA and vice versa. */
#define CCIO_IOVA(iovp,offset) ((iovp) | (offset))
#define CCIO_IOVP(iova) ((iova) & ~(IOVP_SIZE-1) )

#define PDIR_INDEX(iovp)    ((iovp)>>IOVP_SHIFT)
#define MKIOVP(pdir_idx)    ((long)(pdir_idx) << IOVP_SHIFT)
#define MKIOVA(iovp,offset) (dma_addr_t)((long)iovp | (long)offset)

/* CUJO20 KLUDGE start */
#define CUJO_20_BITMASK    0x0ffff000	/* upper nibble is a don't care   */
#define CUJO_20_STEP       0x10000000	/* inc upper nibble */
#define CUJO_20_BADPAGE1   0x01003000	/* pages that hpmc on raven U+    */
#define CUJO_20_BADPAGE2   0x01607000	/* pages that hpmc on firehawk U+ */
#define CUJO_20_BADHVERS   0x6821	/* low nibble 1 is cujo rev 2.0 */
#define CUJO_RAVEN_LOC     0xf1000000UL	/* cujo location on raven U+ */
#define CUJO_FIREHAWK_LOC  0xf1604000UL	/* cujo location on firehawk U+ */
/* CUJO20 KLUDGE end */

/*
** Don't worry about the 150% average search length on a miss.
** If the search wraps around, and passes the res_hint, it will
** cause the kernel to panic anyhow.
*/

/* ioa->res_hint = idx + (size >> 3); \ */

#define CCIO_SEARCH_LOOP(ioa, idx, mask, size)  \
       for(; res_ptr < res_end; ++res_ptr) \
       { \
               if(0 == ((*res_ptr) & mask)) { \
                       *res_ptr |= mask; \
                       idx = (int)((unsigned long)res_ptr - (unsigned long)ioa->res_map); \
                       ioa->res_hint = 0;\
                       goto resource_found; \
               } \
       }

#define CCIO_FIND_FREE_MAPPING(ioa, idx, mask, size)  { \
       u##size *res_ptr = (u##size *)&((ioa)->res_map[ioa->res_hint & ~((size >> 3) - 1)]); \
       u##size *res_end = (u##size *)&(ioa)->res_map[ioa->res_size]; \
       CCIO_SEARCH_LOOP(ioa, idx, mask, size); \
       res_ptr = (u##size *)&(ioa)->res_map[0]; \
       CCIO_SEARCH_LOOP(ioa, idx, mask, size); \
}

/*
** Find available bit in this ioa's resource map.
** Use a "circular" search:
**   o Most IOVA's are "temporary" - avg search time should be small.
** o keep a history of what happened for debugging
** o KISS.
**
** Perf optimizations:
** o search for log2(size) bits at a time.
** o search for available resource bits using byte/word/whatever.
** o use different search for "large" (eg > 4 pages) or "very large"
**   (eg > 16 pages) mappings.
*/
static int
ccio_alloc_range(struct ccio_device *ioa, size_t size)
{
	int res_idx;
	unsigned long mask, flags;
	unsigned int pages_needed = size >> PAGE_SHIFT;

	ASSERT(pages_needed);
	ASSERT((pages_needed * IOVP_SIZE) < DMA_CHUNK_SIZE);
	ASSERT(pages_needed < (BITS_PER_LONG - IOVP_SHIFT));

	mask = (unsigned long) -1L;
 	mask >>= BITS_PER_LONG - pages_needed;

	DBG_RES(__FUNCTION__ " size: %d pages_needed %d pages_mask 0x%08lx\n", 
		size, pages_needed, mask);

	spin_lock_irqsave(&ioa->ccio_lock, flags);

	/*
	** "seek and ye shall find"...praying never hurts either...
	** ggg sacrafices another 710 to the computer gods.
	*/

	if(pages_needed <= 8) {
		CCIO_FIND_FREE_MAPPING(ioa, res_idx, mask, 8);
	} else if(pages_needed <= 16) {
		CCIO_FIND_FREE_MAPPING(ioa, res_idx, mask, 16);
	} else if(pages_needed <= 32) {
		CCIO_FIND_FREE_MAPPING(ioa, res_idx, mask, 32);
#ifdef __LP64__
	} else if(pages_needed <= 64) {
		CCIO_FIND_FREE_MAPPING(ioa, res_idx, mask, 64)
#endif
	} else {
		panic(__FILE__ ":" __FUNCTION__ "() Too many pages to map.\n");
	}

#ifdef DUMP_RESMAP
	dump_resmap();
#endif
	panic(__FILE__ ":" __FUNCTION__ "() I/O MMU is out of mapping resources\n");
	
resource_found:
	
	DBG_RES(__FUNCTION__ " res_idx %d mask 0x%08lx res_hint: %d\n",
		res_idx, mask, ioa->res_hint);

	ccio_used_pages += pages_needed;
	ccio_used_bytes += ((pages_needed >> 3) ? (pages_needed >> 3) : 1);

	spin_unlock_irqrestore(&ioa->ccio_lock, flags);

#ifdef DUMP_RESMAP
	dump_resmap();
#endif

	/* 
	** return the bit address (convert from byte to bit).
	*/
	return (res_idx << 3);
}


#define CCIO_FREE_MAPPINGS(ioa, idx, mask, size) \
        u##size *res_ptr = (u##size *)&((ioa)->res_map[idx + (((size >> 3) - 1) & ~((size >> 3) - 1))]); \
        ASSERT((*res_ptr & mask) == mask); \
        *res_ptr &= ~mask;

/*
** clear bits in the ioa's resource map
*/
static void
ccio_free_range(struct ccio_device *ioa, dma_addr_t iova, size_t size)
{
	unsigned long mask, flags;
	unsigned long iovp = CCIO_IOVP(iova);
	unsigned int res_idx = PDIR_INDEX(iovp)>>3;
	unsigned int pages_mapped = (size >> IOVP_SHIFT) + !!(size & ~IOVP_MASK);

	ASSERT(pages_needed);
	ASSERT((pages_needed * IOVP_SIZE) < DMA_CHUNK_SIZE);
	ASSERT(pages_needed < (BITS_PER_LONG - IOVP_SHIFT));

	mask = (unsigned long) -1L;
 	mask >>= BITS_PER_LONG - pages_mapped;

	DBG_RES(__FUNCTION__ " res_idx: %d size: %d pages_mapped %d mask 0x%08lx\n", 
		res_idx, size, pages_mapped, mask);

	spin_lock_irqsave(&ioa->ccio_lock, flags);

	if(pages_mapped <= 8) {
		CCIO_FREE_MAPPINGS(ioa, res_idx, mask, 8);
	} else if(pages_mapped <= 16) {
		CCIO_FREE_MAPPINGS(ioa, res_idx, mask, 16);
	} else if(pages_mapped <= 32) {
		CCIO_FREE_MAPPINGS(ioa, res_idx, mask, 32);
#ifdef __LP64__
	} else if(pages_mapped <= 64) {
		CCIO_FREE_MAPPINGS(ioa, res_idx, mask, 64);
#endif
	} else {
		panic(__FILE__ ":" __FUNCTION__ "() Too many pages to unmap.\n");
	}
	
	ccio_used_pages -= (pages_mapped ? pages_mapped : 1);
	ccio_used_bytes -= ((pages_mapped >> 3) ? (pages_mapped >> 3) : 1);

	spin_unlock_irqrestore(&ioa->ccio_lock, flags);

#ifdef DUMP_RESMAP
	dump_resmap();
#endif
}


/****************************************************************
**
**          CCIO dma_ops support routines
**
*****************************************************************/

typedef unsigned long space_t;
#define KERNEL_SPACE 0


/*
** DMA "Page Type" and Hints 
** o if SAFE_DMA isn't set, mapping is for FAST_DMA. SAFE_DMA should be
**   set for subcacheline DMA transfers since we don't want to damage the
**   other part of a cacheline.
** o SAFE_DMA must be set for "memory" allocated via pci_alloc_consistent().
**   This bit tells U2 to do R/M/W for partial cachelines. "Streaming"
**   data can avoid this if the mapping covers full cache lines.
** o STOP_MOST is needed for atomicity across cachelines.
**   Apperently only "some EISA devices" need this.
**   Using CONFIG_ISA is hack. Only the IOA with EISA under it needs
**   to use this hint iff the EISA devices needs this feature.
**   According to the U2 ERS, STOP_MOST enabled pages hurt performance.
** o PREFETCH should *not* be set for cases like Multiple PCI devices
**   behind GSCtoPCI (dino) bus converter. Only one cacheline per GSC
**   device can be fetched and multiply DMA streams will thrash the
**   prefetch buffer and burn memory bandwidth. See 6.7.3 "Prefetch Rules
**   and Invalidation of Prefetch Entries".
**
** FIXME: the default hints need to be per GSC device - not global.
** 
** HP-UX dorks: linux device driver programming model is totally different
**    than HP-UX's. HP-UX always sets HINT_PREFETCH since it's drivers
**    do special things to work on non-coherent platforms...linux has to
**    be much more careful with this.
*/
#define IOPDIR_VALID    0x01UL
#define HINT_SAFE_DMA   0x02UL	/* used for pci_alloc_consistent() pages */
#ifdef CONFIG_ISA	/* EISA support really */
#define HINT_STOP_MOST  0x04UL	/* LSL support */
#else
#define HINT_STOP_MOST  0x00UL	/* only needed for "some EISA devices" */
#endif
#define HINT_UDPATE_ENB 0x08UL  /* not used/supported by U2 */
#define HINT_PREFETCH   0x10UL	/* for outbound pages which are not SAFE */


/*
** Use direction (ie PCI_DMA_TODEVICE) to pick hint.
** ccio_alloc_consistent() depends on this to get SAFE_DMA
** when it passes in BIDIRECTIONAL flag.
*/
static u32 hint_lookup[] = {
	[PCI_DMA_BIDIRECTIONAL]  HINT_STOP_MOST | HINT_SAFE_DMA | IOPDIR_VALID,
	[PCI_DMA_TODEVICE]       HINT_STOP_MOST | HINT_PREFETCH | IOPDIR_VALID,
	[PCI_DMA_FROMDEVICE]     HINT_STOP_MOST | IOPDIR_VALID,
	[PCI_DMA_NONE]           0,            /* not valid */
};

/*
** Initialize an I/O Pdir entry
**
** Given a virtual address (vba, arg2) and space id, (sid, arg1),
** load the I/O PDIR entry pointed to by pdir_ptr (arg0). Each IO Pdir
** entry consists of 8 bytes as shown below (MSB == bit 0):
**
**
** WORD 0:
** +------+----------------+-----------------------------------------------+
** | Phys | Virtual Index  |               Phys                            |
** | 0:3  |     0:11       |               4:19                            |
** |4 bits|   12 bits      |              16 bits                          |
** +------+----------------+-----------------------------------------------+
** WORD 1:
** +-----------------------+-----------------------------------------------+
** |      Phys    |  Rsvd  | Prefetch |Update |Rsvd  |Lock  |Safe  |Valid  |
** |     20:39    |        | Enable   |Enable |      |Enable|DMA   |       |
** |    20 bits   | 5 bits | 1 bit    |1 bit  |2 bits|1 bit |1 bit |1 bit  |
** +-----------------------+-----------------------------------------------+
**
** The virtual index field is filled with the results of the LCI
** (Load Coherence Index) instruction.  The 8 bits used for the virtual
** index are bits 12:19 of the value returned by LCI.
*/

void CCIO_INLINE
ccio_io_pdir_entry(u64 *pdir_ptr, space_t sid, void * vba, unsigned long hints)
{
	register unsigned long pa = (volatile unsigned long) vba;
	register unsigned long ci; /* coherent index */

	/* We currently only support kernel addresses */
	ASSERT(sid == 0);
	ASSERT(((unsigned long) vba & 0xf0000000UL) == 0xc0000000UL);

	mtsp(sid,1);

	/*
	** WORD 1 - low order word
	** "hints" parm includes the VALID bit!
	** "dep" clobbers the physical address offset bits as well.
	*/
	pa = virt_to_phys(vba);
	asm volatile("depw  %1,31,12,%0" : "+r" (pa) : "r" (hints));
	((u32 *)pdir_ptr)[1] = (u32) pa;

	/*
	** WORD 0 - high order word
	*/

#ifdef __LP64__
	/*
	** get bits 12:15 of physical address
	** shift bits 16:31 of physical address
	** and deposit them
	*/
	asm volatile ("extrd,u %1,15,4,%0" : "=r" (ci) : "r" (pa));
	asm volatile ("extrd,u %1,31,16,%0" : "+r" (ci) : "r" (ci));
	asm volatile ("depd  %1,35,4,%0" : "+r" (pa) : "r" (ci));
#else
	pa = 0;
#endif
	/*
	** get CPU coherency index bits
	** Grab virtual index [0:11]
	** Deposit virt_idx bits into I/O PDIR word
	*/
	asm volatile ("lci 0(%%sr1, %1), %0" : "=r" (ci) : "r" (vba));
	asm volatile ("extru %1,19,12,%0" : "+r" (ci) : "r" (ci));
	asm volatile ("depw  %1,15,12,%0" : "+r" (pa) : "r" (ci));

	((u32 *)pdir_ptr)[0] = (u32) pa;


	/* FIXME: PCX_W platforms don't need FDC/SYNC. (eg C360)
	**        PCX-U/U+ do. (eg C200/C240)
	**        PCX-T'? Don't know. (eg C110 or similar K-class)
	**
	** See PDC_MODEL/option 0/SW_CAP word for "Non-coherent IO-PDIR bit".
	** Hopefully we can patch (NOP) these out at boot time somehow.
	**
	** "Since PCX-U employs an offset hash that is incompatible with
	** the real mode coherence index generation of U2, the PDIR entry
	** must be flushed to memory to retain coherence."
	*/
	asm volatile("fdc 0(%0)" : : "r" (pdir_ptr));
	asm volatile("sync");
}


/*
** Remove stale entries from the I/O TLB.
** Need to do this whenever an entry in the PDIR is marked invalid.
*/
static CCIO_INLINE void
ccio_clear_io_tlb( struct ccio_device *d, dma_addr_t iovp, size_t byte_cnt)
{
	u32 chain_size = 1 << d->chainid_shift;

	iovp &= ~(IOVP_SIZE-1);	/* clear offset bits, just want pagenum */
	byte_cnt += chain_size;

        while (byte_cnt > chain_size) {
		WRITE_U32(CMD_TLB_PURGE | iovp, &d->ccio_hpa->io_command);
                iovp += chain_size;
		byte_cnt -= chain_size;
        }
}


/***********************************************************
 *
 * Mark the I/O Pdir entries invalid and blow away the
 * corresponding I/O TLB entries.
 *
 * FIXME: at some threshhold it might be "cheaper" to just blow
 *        away the entire I/O TLB instead of individual entries.
 *
 * FIXME: Uturn has 256 TLB entries. We don't need to purge every
 *        PDIR entry - just once for each possible TLB entry.
 *        (We do need to maker I/O PDIR entries invalid regardless).
 ***********************************************************/
static CCIO_INLINE void
ccio_mark_invalid(struct ccio_device *d, dma_addr_t iova, size_t byte_cnt)
{
	u32 iovp = (u32) CCIO_IOVP(iova);
	size_t saved_byte_cnt;

	/* round up to nearest page size */
	saved_byte_cnt = byte_cnt = (byte_cnt + IOVP_SIZE - 1) & IOVP_MASK;

	while (byte_cnt > 0) {
		/* invalidate one page at a time */
		unsigned int idx = PDIR_INDEX(iovp);
		char *pdir_ptr = (char *) &(d->pdir_base[idx]);

		ASSERT( idx < (d->pdir_size/sizeof(u64)));

		pdir_ptr[7] = 0;	/* clear only VALID bit */

		/*
		** FIXME: PCX_W platforms don't need FDC/SYNC. (eg C360)
		**   PCX-U/U+ do. (eg C200/C240)
		** See PDC_MODEL/option 0/SW_CAP for "Non-coherent IO-PDIR bit".
		**
		** Hopefully someone figures out how to patch (NOP) the
		** FDC/SYNC out at boot time.
		*/
		asm volatile("fdc 0(%0)" : : "r" (pdir_ptr[7]));

		iovp     += IOVP_SIZE;
		byte_cnt -= IOVP_SIZE;
	}

	asm volatile("sync");
	ccio_clear_io_tlb(d, CCIO_IOVP(iova), saved_byte_cnt);
}


/****************************************************************
**
**          CCIO dma_ops
**
*****************************************************************/

void __init ccio_init(void)
{
	register_driver(ccio_drivers_for);
}


static int ccio_dma_supported( struct pci_dev *dev, dma_addr_t mask)
{
	if (dev == NULL) {
		printk(MODULE_NAME ": EISA/ISA/et al not supported\n");
		BUG();
		return(0);
	}

	dev->dma_mask = mask;   /* save it */

	/* only support 32-bit devices (ie PCI/GSC) */
	return((int) (mask >= 0xffffffffUL));
}

/*
** Dump a hex representation of the resource map.
*/

#ifdef DUMP_RESMAP
static 
void dump_resmap()
{
	struct ccio_device *ioa = ccio_list;
	unsigned long *res_ptr = (unsigned long *)ioa->res_map;
	unsigned long i = 0;

	printk("res_map: ");
	for(; i < (ioa->res_size / sizeof(unsigned long)); ++i, ++res_ptr)
		printk("%08lx ", *res_ptr);

	printk("\n");
}
#endif

/*
** map_single returns a fully formed IOVA
*/
static dma_addr_t ccio_map_single(struct pci_dev *dev, void *addr, size_t size, int direction)
{
	struct ccio_device *ioa = ccio_list;  /* FIXME : see Multi-IOC below */
	dma_addr_t iovp;
	dma_addr_t offset;
	u64 *pdir_start;
	unsigned long hint = hint_lookup[direction];
	int idx;

	ASSERT(size > 0);

	/* save offset bits */
	offset = ((dma_addr_t) addr) & ~IOVP_MASK;

	/* round up to nearest IOVP_SIZE */
	size = (size + offset + IOVP_SIZE - 1) & IOVP_MASK;

	idx = ccio_alloc_range(ioa, size);
	iovp = (dma_addr_t) MKIOVP(idx);

	DBG_RUN(__FUNCTION__ " 0x%p -> 0x%lx", addr, (long) iovp | offset);

	pdir_start = &(ioa->pdir_base[idx]);

	/* If not cacheline aligned, force SAFE_DMA on the whole mess */
	if ((size % L1_CACHE_BYTES) || ((unsigned long) addr % L1_CACHE_BYTES))
		hint |= HINT_SAFE_DMA;

	/* round up to nearest IOVP_SIZE */
	size = (size + IOVP_SIZE - 1) & IOVP_MASK;

	while (size > 0) {

		ccio_io_pdir_entry(pdir_start, KERNEL_SPACE, addr, hint);

		DBG_RUN(" pdir %p %08x%08x\n",
			pdir_start,
			(u32) (((u32 *) pdir_start)[0]),
			(u32) (((u32 *) pdir_start)[1])
			);
		addr += IOVP_SIZE;
		size -= IOVP_SIZE;
		pdir_start++;
	}
	/* form complete address */
	return CCIO_IOVA(iovp, offset);
}


static void ccio_unmap_single(struct pci_dev *dev, dma_addr_t iova, size_t size, int direction)
{
#ifdef FIXME
/* Multi-IOC (ie N-class) :  need to lookup IOC from dev
** o If we can't know about lba PCI data structs, that eliminates ->sysdata.
** o walking up pcidev->parent dead ends at elroy too
** o leaves hashing dev->bus->number into some lookup.
**   (may only work for N-class)
*/
	struct ccio_device *ioa = dev->sysdata
#else
	struct ccio_device *ioa = ccio_list;
#endif
	dma_addr_t offset;
	
	offset = iova & ~IOVP_MASK;

	/* round up to nearest IOVP_SIZE */
	size = (size + offset + IOVP_SIZE - 1) & IOVP_MASK;

	/* Mask off offset */
	iova &= IOVP_MASK;

	DBG_RUN(__FUNCTION__ " iovp 0x%lx\n", (long) iova);

#ifdef DELAYED_RESOURCE_CNT
	if (ioa->saved_cnt < DELAYED_RESOURCE_CNT) {
		ioa->saved_iova[ioa->saved_cnt] = iova;
		ioa->saved_size[ioa->saved_cnt] = size;
		ccio_saved_cnt++;
	} else {
		do {
#endif
			ccio_mark_invalid(ioa, iova, size);
			ccio_free_range(ioa, iova, size);

#ifdef DELAYED_RESOURCE_CNT
			d->saved_cnt--;
			iova = ioa->saved_iova[ioa->saved_cnt];
			size = ioa->saved_size[ioa->saved_cnt];
		} while (ioa->saved_cnt)
	}
#endif
}


static void * ccio_alloc_consistent (struct pci_dev *hwdev, size_t size, dma_addr_t *dma_handle)
{
	void *ret;
	unsigned long flags;
	struct ccio_device *ioa = ccio_list;

	DBG_RUN(__FUNCTION__ " size 0x%x\n",  size);

#if 0
/* GRANT Need to establish hierarchy for non-PCI devs as well
** and then provide matching gsc_map_xxx() functions for them as well.
*/
	if (!hwdev) {
		/* only support PCI */
		*dma_handle = 0;
		return 0;
	}
#endif
	spin_lock_irqsave(&ioa->ccio_lock, flags);
	ccio_alloc_size += get_order(size);
	spin_unlock_irqrestore(&ioa->ccio_lock, flags);

        ret = (void *) __get_free_pages(GFP_ATOMIC, get_order(size));

	if (ret) {
		memset(ret, 0, size);
		*dma_handle = ccio_map_single(hwdev, ret, size, PCI_DMA_BIDIRECTIONAL);
	}
	DBG_RUN(__FUNCTION__ " ret %p\n",  ret);

	return ret;
}


static void ccio_free_consistent (struct pci_dev *hwdev, size_t size, void *vaddr, dma_addr_t dma_handle)
{
	unsigned long flags;
	struct ccio_device *ioa = ccio_list;

	spin_lock_irqsave(&ioa->ccio_lock, flags);
	ccio_free_size += get_order(size);
	spin_unlock_irqrestore(&ioa->ccio_lock, flags);

	ccio_unmap_single(hwdev, dma_handle, size, 0);
	free_pages((unsigned long) vaddr, get_order(size));
}


static int ccio_map_sg(struct pci_dev *dev, struct scatterlist *sglist, int nents, int direction)
{
	int tmp = nents;

	DBG_RUN(KERN_WARNING __FUNCTION__ " START\n");

        /* KISS: map each buffer seperately. */
	while (nents) {
		sg_dma_address(sglist) = ccio_map_single(dev, sglist->address, sglist->length, direction);
		sg_dma_len(sglist) = sglist->length;
		nents--;
		sglist++;
	}

	DBG_RUN(KERN_WARNING __FUNCTION__ " DONE\n");
	return tmp;
}


static void ccio_unmap_sg(struct pci_dev *dev, struct scatterlist *sglist, int nents, int direction)
{
	DBG_RUN(KERN_WARNING __FUNCTION__ " : unmapping %d entries\n", nents);
	while (nents) {
		ccio_unmap_single(dev, sg_dma_address(sglist), sg_dma_len(sglist), direction);
		nents--;
		sglist++;
	}
	return;
}


static struct pci_dma_ops ccio_ops = {
	ccio_dma_supported,
	ccio_alloc_consistent,
	ccio_free_consistent,
	ccio_map_single,
	ccio_unmap_single,
	ccio_map_sg,
	ccio_unmap_sg,
	NULL,                   /* dma_sync_single : NOP for U2/Uturn */
	NULL,                   /* dma_sync_sg     : ditto */
};

#if 0
/* GRANT -  is this needed for U2 or not? */

/*
** Get the size of the I/O TLB for this I/O MMU.
**
** If spa_shift is non-zero (ie probably U2),
** then calculate the I/O TLB size using spa_shift.
**
** Otherwise we are supposed to get the IODC entry point ENTRY TLB
** and execute it. However, both U2 and Uturn firmware supplies spa_shift.
** I think only Java (K/D/R-class too?) systems don't do this.
*/
static int
ccio_get_iotlb_size(struct hp_device *d)
{
	if(d->spa_shift == 0) {
		panic(__FUNCTION__ ": Can't determine I/O TLB size.\n");
	}
	return(1 << d->spa_shift);
}
#else

/* Uturn supports 256 TLB entries */
#define CCIO_CHAINID_SHIFT	8
#define CCIO_CHAINID_MASK	0xff

#endif /* 0 */


/*
** Figure out how big the I/O PDIR should be and alloc it.
** Also sets variables which depend on pdir size.
*/
static void
ccio_alloc_pdir(struct ccio_device *ioa)
{
	extern unsigned long mem_max;          /* arch.../setup.c */

	u32 iova_space_size = 0;
	void * pdir_base;
	int pdir_size, iov_order;

	/*
	** Determine IOVA Space size from memory size.
	** Using "mem_max" is a kluge.
	**
	** Ideally, PCI drivers would register the maximum number
	** of DMA they can have outstanding for each device they
	** own.  Next best thing would be to guess how much DMA
	** can be outstanding based on PCI Class/sub-class. Both
	** methods still require some "extra" to support PCI
	** Hot-Plug/Removal of PCI cards. (aka PCI OLARD).
	*/
	/* limit IOVA space size to 1MB-1GB */
	if (mem_max < (ccio_mem_ratio*1024*1024)) {
		iova_space_size = 1024*1024;
#ifdef __LP64__
	} else if (mem_max > (ccio_mem_ratio*512*1024*1024)) {
		iova_space_size = 512*1024*1024;
#endif
	} else {
		iova_space_size = (u32) (mem_max/ccio_mem_ratio);
	}

	/*
	** iova space must be log2() in size.
	** thus, pdir/res_map will also be log2().
	*/

	/* We could use larger page sizes in order to *decrease* the number
	** of mappings needed.  (ie 8k pages means 1/2 the mappings).
        **
	** Note: Grant Grunder says "Using 8k I/O pages isn't trivial either
	**   since the pages must also be physically contiguous - typically
	**   this is the case under linux."
	*/

	iov_order = get_order(iova_space_size);
	ASSERT(iov_order <= (30 - IOVP_SHIFT));   /* iova_space_size <= 1GB */
	ASSERT(iov_order >= (20 - IOVP_SHIFT));   /* iova_space_size >= 1MB */
	iova_space_size = 1 << (iov_order + IOVP_SHIFT);

	ioa->pdir_size = pdir_size = (iova_space_size/IOVP_SIZE) * sizeof(u64);

	ASSERT(pdir_size < 4*1024*1024);   /* max pdir size < 4MB */

	/* Verify it's a power of two */
	ASSERT((1 << get_order(pdir_size)) == (pdir_size >> PAGE_SHIFT));

	DBG_INIT(__FUNCTION__ " hpa 0x%p mem %dMB IOV %dMB (%d bits)\n    PDIR size 0x%0x",
		ioa->ccio_hpa, (int) (mem_max>>20), iova_space_size>>20,
		iov_order + PAGE_SHIFT, pdir_size);

	ioa->pdir_base =
	pdir_base = (void *) __get_free_pages(GFP_KERNEL, get_order(pdir_size));
	if (NULL == pdir_base)
	{
		panic(__FILE__ ":" __FUNCTION__ "() could not allocate I/O Page Table\n");
	}
	memset(pdir_base, 0, pdir_size);

	ASSERT((((unsigned long) pdir_base) & PAGE_MASK) == (unsigned long) pdir_base);

	DBG_INIT(" base %p", pdir_base);

	/*
	** Chainid is the upper most bits of an IOVP used to determine
	** which TLB entry an IOVP will use.
	*/
	ioa->chainid_shift = get_order(iova_space_size)+PAGE_SHIFT-CCIO_CHAINID_SHIFT;

	DBG_INIT(" chainid_shift 0x%x\n", ioa->chainid_shift);
}


static void
ccio_hw_init(struct ccio_device *ioa)
{
	int i;

	/*
	** Initialize IOA hardware
	*/
	WRITE_U32(CCIO_CHAINID_MASK << ioa->chainid_shift, &ioa->ccio_hpa->io_chain_id_mask);
	WRITE_U32(virt_to_phys(ioa->pdir_base), &ioa->ccio_hpa->io_pdir_base);


	/*
	** Go to "Virtual Mode"
	*/
	WRITE_U32(IOA_NORMAL_MODE, &ioa->ccio_hpa->io_control);

	/*
	** Initialize all I/O TLB entries to 0 (Valid bit off).
	*/
	WRITE_U32(0, &ioa->ccio_hpa->io_tlb_entry_m);
	WRITE_U32(0, &ioa->ccio_hpa->io_tlb_entry_l);

	for (i = 1 << CCIO_CHAINID_SHIFT; i ; i--) {
		WRITE_U32((CMD_TLB_DIRECT_WRITE | (i << ioa->chainid_shift)),
					&ioa->ccio_hpa->io_command);
	}

}


static void
ccio_resmap_init(struct ccio_device *ioa)
{
	u32 res_size;

	/*
	** Ok...we do more than just init resource map
	*/
	ioa->ccio_lock = SPIN_LOCK_UNLOCKED;

	ioa->res_hint = 16;    /* next available IOVP - circular search */

	/* resource map size dictated by pdir_size */
	res_size = ioa->pdir_size/sizeof(u64); /* entries */
	res_size >>= 3;	/* convert bit count to byte count */
	DBG_INIT(__FUNCTION__ "() res_size 0x%x\n", res_size);

	ioa->res_size = res_size;
	ioa->res_map = (char *) __get_free_pages(GFP_KERNEL, get_order(res_size));
	if (NULL == ioa->res_map)
	{
		panic(__FILE__ ":" __FUNCTION__ "() could not allocate resource map\n");
	}
	memset(ioa->res_map, 0, res_size);
}

/* CUJO20 KLUDGE start */
static struct {
		u16 hversion;
		u8  spa;
		u8  type;
		u32     foo[3];	/* 16 bytes total */
} cujo_iodc __attribute__ ((aligned (64)));
static unsigned long cujo_result[32] __attribute__ ((aligned (16))) = {0,0,0,0};

/*
** CUJO 2.0 incorrectly decodes a memory access for specific
** pages (every page at specific iotlb locations dependent
** upon where the cujo is flexed - diff on raven/firehawk.
** resulting in an hpmc and/or silent data corruption.
** Workaround is to prevent use of those I/O TLB entries
** by marking the suspect bitmap range entries as busy.
*/
static void
ccio_cujo20_hack(struct ccio_device *ioa)
{
	unsigned long status;
	unsigned int idx;
	u8 *res_ptr = ioa->res_map;
	u32 iovp=0x0;
	unsigned long mask;

	status = pdc_iodc_read( &cujo_result, (void *) CUJO_RAVEN_LOC, 0, &cujo_iodc, 16);
	if (status == 0) {
		if (cujo_iodc.hversion==CUJO_20_BADHVERS)
			iovp = CUJO_20_BADPAGE1;
	} else {
		status = pdc_iodc_read( &cujo_result, (void *) CUJO_FIREHAWK_LOC, 0, &cujo_iodc, 16);
		if (status == 0) {
			if (cujo_iodc.hversion==CUJO_20_BADHVERS)
				iovp = CUJO_20_BADPAGE2;
		} else {
			/* not a defective system */
			return;
		}
	}

	printk(MODULE_NAME ": Cujo 2.0 bug needs a work around\n");
	ccio_cujo_bug = 1;

	/*
	** mark bit entries that match "bad page"
	*/
	idx = PDIR_INDEX(iovp)>>3;
	mask = 0xff;
	
	while(idx * sizeof(u8) < ioa->res_size) {
		res_ptr[idx] |= mask;
		idx += (PDIR_INDEX(CUJO_20_STEP)>>3);
		ccio_used_pages += 8;
		ccio_used_bytes += 1;
	}
}
/* CUJO20 KLUDGE end */

#ifdef CONFIG_PROC_FS
static int ccio_proc_info(char *buf, char **start, off_t offset, int len)
{
	unsigned long i = 0;
	struct ccio_device *ioa = ccio_list;
	unsigned long *res_ptr = (unsigned long *)ioa->res_map;
	unsigned long total_pages = ioa->res_size << 3;            /* 8 bits per byte */

	sprintf(buf, "%s\nCujo 2.0 bug    : %s\n",
		parisc_getHWdescription(ioa->iodc->hw_type, ioa->iodc->hversion,
					ioa->iodc->sversion),
		(ccio_cujo_bug ? "yes" : "no"));

	sprintf(buf, "%sIO pdir size    : %d bytes (%d entries)\n",
		buf, ((ioa->res_size << 3) * sizeof(u64)), /* 8 bits per byte */
		ioa->res_size << 3);                       /* 8 bits per byte */
	
	sprintf(buf, "%sResource bitmap : %d bytes (%d pages)\n", 
		buf, ioa->res_size, ioa->res_size << 3);   /* 8 bits per byte */

	strcat(buf,  "     	  total:    free:    used:   % used:\n");
	sprintf(buf, "%sblocks  %8d %8ld %8ld %8ld%%\n", buf, ioa->res_size,
		ioa->res_size - ccio_used_bytes, ccio_used_bytes,
		(ccio_used_bytes * 100) / ioa->res_size);

	sprintf(buf, "%spages   %8ld %8ld %8ld %8ld%%\n", buf, total_pages,
		total_pages - ccio_used_pages, ccio_used_pages,
		(ccio_used_pages * 100 / total_pages));

	sprintf(buf, "%sconsistent       %8ld %8ld\n", buf,
		ccio_alloc_size, ccio_free_size);
 
	strcat(buf, "\nResource bitmap:\n");

	for(; i < (ioa->res_size / sizeof(unsigned long)); ++i, ++res_ptr)
		len += sprintf(buf, "%s%08lx ", buf, *res_ptr);

	strcat(buf, "\n");
	return strlen(buf);
}
#endif

/*
** Determine if ccio should claim this chip (return 0) or not (return 1).
** If so, initialize the chip and tell other partners in crime they
** have work to do.
*/
static int
ccio_driver_callback(struct hp_device *d, struct pa_iodc_driver *dri)
{
	struct ccio_device *ioa;

	printk("%s found %s at 0x%p\n", dri->name, dri->version, d->hpa);

	if (ccio_list) {
		printk(MODULE_NAME ": already initialized one device\n");
		return(0);
	}

	ioa = kmalloc(sizeof(struct ccio_device), GFP_KERNEL);
	if (NULL == ioa)
	{
		printk(MODULE_NAME " - couldn't alloc ccio_device\n");
		return(1);
	}
	memset(ioa, 0, sizeof(struct ccio_device));

	/*
	** ccio list is used mainly as a kluge to support a single instance. 
	** Eventually, with core dumps, it'll be useful for debugging.
	*/
	ccio_list = ioa;
	ioa->iodc = d;

#if 1
/* KLUGE: determine IOA hpa based on GSC port value.
** Needed until we have a PA bus walk. Can only discover IOA via
** walking the architected PA MMIO space as described by the I/O ACD.
** "Legacy" PA Firmware only tells us about unarchitected devices
** that can't be detected by PA/EISA/PCI bus walks.
*/
	switch((long) d->hpa) {
	case 0xf3fbf000L:       /* C110 IOA0 LBC (aka GSC port) */
		/* ccio_hpa same as C200 IOA0 */
	case 0xf203f000L:       /* C180/C200/240/C360 IOA0 LBC (aka GSC port) */
		ioa->ccio_hpa = (struct ioa_registers *) 0xfff88000L;
		break;
	case 0xf103f000L:       /* C180/C200/240/C360 IOA1 LBC (aka GSC port) */
		ioa->ccio_hpa = (struct ioa_registers *) 0xfff8A000L;
		break;
	default:
		panic("ccio-dma.c doesn't know this GSC port Address!\n");
		break;
	};
#else
	ioa->ccio_hpa = d->hpa;
#endif

	ccio_alloc_pdir(ioa);
	ccio_hw_init(ioa);
	ccio_resmap_init(ioa);

	/* CUJO20 KLUDGE start */
	ccio_cujo20_hack(ioa);
	/* CUJO20 KLUDGE end */

	hppa_dma_ops = &ccio_ops;

	create_proc_info_entry(MODULE_NAME, 0, proc_runway_root, ccio_proc_info);
	return(0);
}



