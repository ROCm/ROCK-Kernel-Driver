/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (c) 1992-1999,2001-2004 Silicon Graphics, Inc. All rights reserved.
 */

#ifndef _ASM_IA64_SN_ADDRS_H
#define _ASM_IA64_SN_ADDRS_H


/* McKinley Address Format:
 *
 *   4 4       3 3  3 3
 *   9 8       8 7  6 5             0
 *  +-+---------+----+--------------+
 *  |0| Node ID | AS | Node Offset  |
 *  +-+---------+----+--------------+
 *
 *   Node ID: If bit 38 = 1, is ICE, else is SHUB
 *   AS: Address Space Identifier. Used only if bit 38 = 0.
 *     b'00: Local Resources and MMR space
 *           bit 35
 *               0: Local resources space
 *                  node id:
 *                        0: IA64/NT compatibility space
 *                        2: Local MMR Space
 *                        4: Local memory, regardless of local node id
 *               1: Global MMR space
 *     b'01: GET space.
 *     b'10: AMO space.
 *     b'11: Cacheable memory space.
 *
 *   NodeOffset: byte offset
 */

/* TIO address format:
 *  4 4        3 3 3 3 3             0
 *  9 8        8 7 6 5 4
 * +-+----------+-+---+--------------+
 * |0| Node ID  |0|CID| Node offset  |
 * +-+----------+-+---+--------------+
 *
 * Node ID: if bit 38 == 1, is ICE.
 * Bit 37: Must be zero.
 * CID: Chiplet ID:
 *     b'01: TIO LB (Indicates TIO MMR access.)
 *     b'11: TIO ICE (indicates coretalk space access.)
 * Node offset: byte offest.
 */

/*
 * Note that in both of the above address formats, bit
 * 35 set indicates that the reference is to the 
 * shub or tio MMRs.
 */

#ifndef __ASSEMBLY__
typedef union ia64_sn2_pa {
	struct {
		unsigned long off  : 36;
		unsigned long as   : 2;
		unsigned long nasid: 11;
		unsigned long fill : 15;
	} f;
	unsigned long l;
	void *p;
} ia64_sn2_pa_t;
#endif

#define TO_PHYS_MASK		0x0001ffcfffffffffUL	/* Note - clear AS bits */


/* Regions determined by AS */
#define LOCAL_MMR_SPACE		0xc000008000000000UL	/* Local MMR space */
#define LOCAL_PHYS_MMR_SPACE	0x8000008000000000UL	/* Local PhysicalMMR space */
#define LOCAL_MEM_SPACE		0xc000010000000000UL	/* Local Memory space */
/* It so happens that setting bit 35 indicates a reference to the SHUB or TIO
 * MMR space.  
 */
#define GLOBAL_MMR_SPACE	0xc000000800000000UL	/* Global MMR space */
#define TIO_MMR_SPACE		0xc000000800000000UL	/* TIO MMR space */
#define ICE_MMR_SPACE		0xc000000000000000UL	/* ICE MMR space */
#define GLOBAL_PHYS_MMR_SPACE	0x0000000800000000UL	/* Global Physical MMR space */
#define GET_SPACE		0xe000001000000000UL	/* GET space */
#define AMO_SPACE		0xc000002000000000UL	/* AMO space */
#define CACHEABLE_MEM_SPACE	0xe000003000000000UL	/* Cacheable memory space */
#define UNCACHED                0xc000000000000000UL	/* UnCacheable memory space */
#define UNCACHED_PHYS           0x8000000000000000UL	/* UnCacheable physical memory space */

#define PHYS_MEM_SPACE		0x0000003000000000UL	/* physical memory space */

/* SN2 address macros */
/* NID_SHFT has the right value for both SHUB and TIO addresses.*/
#define NID_SHFT		38
#define LOCAL_MMR_ADDR(a)	(UNCACHED | LOCAL_MMR_SPACE | (a))
#define LOCAL_MMR_PHYS_ADDR(a)	(UNCACHED_PHYS | LOCAL_PHYS_MMR_SPACE | (a))
#define LOCAL_MEM_ADDR(a)	(LOCAL_MEM_SPACE | (a))
#define REMOTE_ADDR(n,a)	((((unsigned long)(n))<<NID_SHFT) | (a))
#define GLOBAL_MMR_ADDR(n,a)	(UNCACHED | GLOBAL_MMR_SPACE | REMOTE_ADDR(n,a))
#define GLOBAL_MMR_PHYS_ADDR(n,a) (UNCACHED_PHYS | GLOBAL_PHYS_MMR_SPACE | REMOTE_ADDR(n,a))
#define GET_ADDR(n,a)		(GET_SPACE | REMOTE_ADDR(n,a))
#define AMO_ADDR(n,a)		(UNCACHED | AMO_SPACE | REMOTE_ADDR(n,a))
#define GLOBAL_MEM_ADDR(n,a)	(CACHEABLE_MEM_SPACE | REMOTE_ADDR(n,a))

/* non-II mmr's start at top of big window space (4G) */
#define BWIN_TOP		0x0000000100000000UL

/*
 * general address defines - for code common to SN0/SN1/SN2
 */
#define CAC_BASE		CACHEABLE_MEM_SPACE			/* cacheable memory space */
#define IO_BASE			(UNCACHED | GLOBAL_MMR_SPACE)		/* lower 4G maps II's XIO space */
#define TIO_BASE		(UNCACHED | ICE_MMR_SPACE)		/* lower 4G maps TIO space */
#define AMO_BASE		(UNCACHED | AMO_SPACE)			/* fetch & op space */
#define MSPEC_BASE		AMO_BASE				/* fetch & op space */
#define UNCAC_BASE		(UNCACHED | CACHEABLE_MEM_SPACE)	/* uncached global memory */
#define GET_BASE		GET_SPACE				/* momentarily coherent remote mem. */
#define CALIAS_BASE             LOCAL_CACHEABLE_BASE			/* cached node-local memory */
#define UALIAS_BASE             (UNCACHED | LOCAL_CACHEABLE_BASE)	/* uncached node-local memory */

#define TO_PHYS(x)              (              ((x) & TO_PHYS_MASK))
#define TO_CAC(x)               (CAC_BASE    | ((x) & TO_PHYS_MASK))
#define TO_UNCAC(x)             (UNCAC_BASE  | ((x) & TO_PHYS_MASK))
#define TO_MSPEC(x)             (MSPEC_BASE  | ((x) & TO_PHYS_MASK))
#define TO_GET(x)		(GET_BASE    | ((x) & TO_PHYS_MASK))
#define TO_CALIAS(x)            (CALIAS_BASE | TO_NODE_ADDRSPACE(x))
#define TO_UALIAS(x)            (UALIAS_BASE | TO_NODE_ADDRSPACE(x))
#define NODE_SIZE_BITS		36	/* node offset : bits <35:0> */
#define BWIN_SIZE_BITS		29	/* big window size: 512M */
#define TIO_BWIN_SIZE_BITS	30	/* big window size: 1G */
#define NASID_BITS		11	/* bits <48:38> */
#define NASID_BITMASK		(0x7ffULL)
#define NASID_SHFT		NID_SHFT
#define NASID_META_BITS		0	/* ???? */
#define NASID_LOCAL_BITS	7	/* same router as SN1 */

#define NODE_ADDRSPACE_SIZE     (1UL << NODE_SIZE_BITS)
#define NASID_MASK              ((uint64_t) NASID_BITMASK << NASID_SHFT)
#define NASID_GET(_pa)          (int) (((uint64_t) (_pa) >>            \
                                        NASID_SHFT) & NASID_BITMASK)
#define PHYS_TO_DMA(x)          ( ((x & NASID_MASK) >> 2) |             \
                                  (x & (NODE_ADDRSPACE_SIZE - 1)) )

/*
 * This address requires a chiplet id in bits 38-39.  For DMA to memory,
 * the chiplet id is zero.  If we implement TIO-TIO dma, we might need
 * to insert a chiplet id into this macro.  However, it is our belief
 * right now that this chiplet id will be ICE, which is also zero.
 */
#define PHYS_TO_TIODMA(x)     ( ((x & NASID_MASK) << 2) |             \
                                 (x & (NODE_ADDRSPACE_SIZE - 1)) )

#define CHANGE_NASID(n,x)	({ia64_sn2_pa_t _v; _v.l = (long) (x); _v.f.nasid = n; _v.p;})


#ifndef __ASSEMBLY__
#define NODE_SWIN_BASE(nasid, widget)                                   \
        ((widget == 0) ? NODE_BWIN_BASE((nasid), SWIN0_BIGWIN)          \
        : RAW_NODE_SWIN_BASE(nasid, widget))
#else
#define NODE_SWIN_BASE(nasid, widget) \
     (NODE_IO_BASE(nasid) + ((uint64_t) (widget) << SWIN_SIZE_BITS))
#define LOCAL_SWIN_BASE(widget) \
	(UNCACHED | LOCAL_MMR_SPACE | (((uint64_t) (widget) << SWIN_SIZE_BITS)))
#endif /* __ASSEMBLY__ */

/*
 * The following definitions pertain to the IO special address
 * space.  They define the location of the big and little windows
 * of any given node.
 */

#define BWIN_SIZE               (1UL << BWIN_SIZE_BITS)
#define BWIN_SIZEMASK           (BWIN_SIZE - 1)
#define BWIN_WIDGET_MASK        0x7
#define NODE_BWIN_BASE0(nasid)  (NODE_IO_BASE(nasid) + BWIN_SIZE)
#define NODE_BWIN_BASE(nasid, bigwin)   (NODE_BWIN_BASE0(nasid) +       \
                        ((uint64_t) (bigwin) << BWIN_SIZE_BITS))

#define BWIN_WIDGETADDR(addr)   ((addr) & BWIN_SIZEMASK)
#define BWIN_WINDOWNUM(addr)    (((addr) >> BWIN_SIZE_BITS) & BWIN_WIDGET_MASK)

#define TIO_BWIN_WINDOW_SELECT_MASK 0x7
#define TIO_BWIN_WINDOWNUM(addr)    (((addr) >> TIO_BWIN_SIZE_BITS) & TIO_BWIN_WINDOW_SELECT_MASK)


#ifndef __ASSEMBLY__
#include <asm/sn/types.h>
#endif 

/*
 * The following macros are used to index to the beginning of a specific
 * node's address space.
 */

#define NODE_OFFSET(_n)		((uint64_t) (_n) << NASID_SHFT)

#define NODE_CAC_BASE(_n)	(CAC_BASE  + NODE_OFFSET(_n))
#define NODE_HSPEC_BASE(_n)	(HSPEC_BASE + NODE_OFFSET(_n))
#define NODE_IO_BASE(_n)	(IO_BASE    + NODE_OFFSET(_n))
#define NODE_MSPEC_BASE(_n)	(MSPEC_BASE + NODE_OFFSET(_n))
#define NODE_UNCAC_BASE(_n)	(UNCAC_BASE + NODE_OFFSET(_n))

#define TO_NODE_CAC(_n, _x)	(NODE_CAC_BASE(_n) | ((_x) & TO_PHYS_MASK))

#define RAW_NODE_SWIN_BASE(nasid, widget)				\
	(NODE_IO_BASE(nasid) + ((uint64_t) (widget) << SWIN_SIZE_BITS))


/*
 * The following definitions pertain to the IO special address
 * space.  They define the location of the big and little windows
 * of any given node.
 */

#define SWIN_SIZE_BITS		24
#define SWIN_SIZE		(1UL << 24)
#define	SWIN_SIZEMASK		(SWIN_SIZE - 1)
#define	SWIN_WIDGET_MASK	0xF

#define TIO_SWIN_SIZE_BITS	28
#define TIO_SWIN_SIZE		(1UL << 28)
#define TIO_SWIN_SIZEMASK	(SWIN_SIZE - 1)
#define TIO_SWIN_WIDGET_MASK	0x3

/*
 * Convert smallwindow address to xtalk address.
 *
 * 'addr' can be physical or virtual address, but will be converted
 * to Xtalk address in the range 0 -> SWINZ_SIZEMASK
 */
#define	SWIN_WIDGETNUM(addr)	(((addr)  >> SWIN_SIZE_BITS) & SWIN_WIDGET_MASK)

#define TIO_SWIN_WIDGETNUM(addr)	(((addr)  >> TIO_SWIN_SIZE_BITS) & TIO_SWIN_WIDGET_MASK)

/*
 * The following macros produce the correct base virtual address for
 * the hub registers.  The LOCAL_HUB_* macros produce the appropriate
 * address for the local registers.  The REMOTE_HUB_* macro produce
 * the address for the specified hub's registers.  The intent is
 * that the appropriate PI, MD, NI, or II register would be substituted
 * for _x.
 */


/*
 * SN2 has II mmr's located inside small window space.
 * As all other non-II mmr's located at the top of big window
 * space.
 */
#define REMOTE_HUB_BASE(_x)						\
        (UNCACHED | GLOBAL_MMR_SPACE |                                  \
        (((~(_x)) & BWIN_TOP)>>8)    |                                       \
        (((~(_x)) & BWIN_TOP)>>9)    | (_x))

#define REMOTE_HUB(_n, _x)						\
	((uint64_t *)(REMOTE_HUB_BASE(_x) | ((((long)(_n))<<NASID_SHFT))))


/*
 * WARNING:
 *	When certain Hub chip workaround are defined, it's not sufficient
 *	to dereference the *_HUB_ADDR() macros.  You should instead use
 *	HUB_L() and HUB_S() if you must deal with pointers to hub registers.
 *	Otherwise, the recommended approach is to use *_HUB_L() and *_HUB_S().
 *	They're always safe.
 */
/*
 * LOCAL_HUB_ADDR doesn't need to be changed for TIO, since, by definition,
 * there are no "local" TIOs.
 */
#define LOCAL_HUB_ADDR(_x)							\
	(((_x) & BWIN_TOP) ? ((volatile uint64_t *)(LOCAL_MMR_ADDR(_x)))		\
	: ((volatile uint64_t *)(IALIAS_BASE + (_x))))
#define REMOTE_HUB_ADDR(_n, _x)						\
	((_n & 1) ?							\
	/* TIO: */							\
	((volatile uint64_t *)(GLOBAL_MMR_ADDR(_n, _x)))				\
	: /* SHUB: */							\
	(((_x) & BWIN_TOP) ? ((volatile uint64_t *)(GLOBAL_MMR_ADDR(_n, _x)))	\
	: ((volatile uint64_t *)(NODE_SWIN_BASE(_n, 1) + 0x800000 + (_x)))))

#ifndef __ASSEMBLY__

#define HUB_L(_a)			(*((volatile typeof(*_a) *)_a))
#define	HUB_S(_a, _d)			(*((volatile typeof(*_a) *)_a) = (_d))

#define LOCAL_HUB_L(_r)			HUB_L(LOCAL_HUB_ADDR(_r))
#define LOCAL_HUB_S(_r, _d)		HUB_S(LOCAL_HUB_ADDR(_r), (_d))
#define REMOTE_HUB_L(_n, _r)		HUB_L(REMOTE_HUB_ADDR((_n), (_r)))
#define REMOTE_HUB_S(_n, _r, _d)	HUB_S(REMOTE_HUB_ADDR((_n), (_r)), (_d))
#define REMOTE_HUB_PI_L(_n, _sn, _r)	HUB_L(REMOTE_HUB_PI_ADDR((_n), (_sn), (_r)))
#define REMOTE_HUB_PI_S(_n, _sn, _r, _d) HUB_S(REMOTE_HUB_PI_ADDR((_n), (_sn), (_r)), (_d))

#endif /* __ASSEMBLY__ */

/*
 * The following macros are used to get to a hub/bridge register, given
 * the base of the register space.
 */
#define HUB_REG_PTR(_base, _off)	\
	(volatile uint64_t *)((unsigned long)(_base) + (__psunsigned_t)(_off)))

#define HUB_REG_PTR_L(_base, _off)	\
	HUB_L(HUB_REG_PTR((_base), (_off)))

#define HUB_REG_PTR_S(_base, _off, _data)	\
	HUB_S(HUB_REG_PTR((_base), (_off)), (_data))

#endif /* _ASM_IA64_SN_ADDRS_H */
