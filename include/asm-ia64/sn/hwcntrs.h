/* $Id$
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1992 - 1997, 2000 Silicon Graphics, Inc.
 * Copyright (C) 2000 by Colin Ngam
 */
#ifndef _ASM_SN_HWCNTRS_H
#define _ASM_SN_HWCNTRS_H


typedef  uint64_t refcnt_t;

#define SN0_REFCNT_MAX_COUNTERS 64

typedef struct sn0_refcnt_set {
	refcnt_t    refcnt[SN0_REFCNT_MAX_COUNTERS];
        uint64_t  flags;
        uint64_t  reserved[4];
} sn0_refcnt_set_t;

typedef struct sn0_refcnt_buf {
	sn0_refcnt_set_t   refcnt_set;
	uint64_t         paddr;
        uint64_t         page_size;
        cnodeid_t          cnodeid;         /* cnodeid + pad[3] use 64 bits */
        uint16_t           pad[3];
        uint64_t         reserved[4];
} sn0_refcnt_buf_t;

typedef struct sn0_refcnt_args {
	uint64_t          vaddr;
	uint64_t          len;
	sn0_refcnt_buf_t*   buf;
        uint64_t          reserved[4];
} sn0_refcnt_args_t;

/*
 * Info needed by the user level program
 * to mmap the refcnt buffer
 */

#define RCB_INFO_GET  1
#define RCB_SLOT_GET  2

typedef struct rcb_info {
        uint64_t  rcb_len;                  /* total refcnt buffer len in bytes */

        int         rcb_sw_sets;              /* number of sw counter sets in buffer */
        int         rcb_sw_counters_per_set;  /* sw counters per set -- numnodes */
        int         rcb_sw_counter_size;      /* sizeof(refcnt_t) -- size of sw cntr */

        int         rcb_base_pages;           /* number of base pages in node */
        int         rcb_base_page_size;       /* sw base page size */        
        uint64_t  rcb_base_paddr;           /* base physical address for this node */

        int         rcb_cnodeid;              /* cnodeid for this node */
        int         rcb_granularity;          /* hw page size used for counter sets */
        uint        rcb_hw_counter_max;       /* max hwcounter count (width mask) */
        int         rcb_diff_threshold;       /* current node differential threshold */
        int         rcb_abs_threshold;        /* current node absolute threshold */
        int         rcb_num_slots;            /* physmem slots */
        
        int         rcb_reserved[512];
        
} rcb_info_t;

typedef struct rcb_slot {
        uint64_t  base;
        uint64_t  size;
} rcb_slot_t;

#if defined(__KERNEL__)
// #include <sys/immu.h>
typedef struct sn0_refcnt_args_32 {
	uint64_t    vaddr;
	uint64_t    len;
	app32_ptr_t   buf;
        uint64_t    reserved[4];
} sn0_refcnt_args_32_t;

/* Defines and Macros  */
/* A set of reference counts are for 4k bytes of physical memory */
#define	NBPREFCNTP	0x1000	
#define	BPREFCNTPSHIFT	12
#define bytes_to_refcntpages(x)	(((__psunsigned_t)(x)+(NBPREFCNTP-1))>>BPREFCNTPSHIFT)
#define refcntpage_offset(x)	((__psunsigned_t)(x)&((NBPP-1)&~(NBPREFCNTP-1)))
#define align_to_refcntpage(x)	((__psunsigned_t)(x)&(~(NBPREFCNTP-1)))

extern void migr_refcnt_read(sn0_refcnt_buf_t*);
extern void migr_refcnt_read_extended(sn0_refcnt_buf_t*);
extern int migr_refcnt_enabled(void);

#endif /* __KERNEL__ */

#endif /* _ASM_SN_HWCNTRS_H */
