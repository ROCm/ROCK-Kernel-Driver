/* $Id$
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1992 - 1997, 2000 Silicon Graphics, Inc.
 * Copyright (C) 2000 by Colin Ngam
 */
#ifndef _ASM_SN_SN1_HUBMD_NEXT_H
#define _ASM_SN_SN1_HUBMD_NEXT_H

#ifdef BRINGUP
/* XXX moved over from SN/SN0/hubmd.h -- each should be checked for SN1 */
/* In fact, most of this stuff is wrong. Some is correct, such as
 * MD_PAGE_SIZE and MD_PAGE_NUM_SHFT.
 */

#define MD_PERF_COUNTERS        6
#define MD_PERF_SETS            6

#define MD_SIZE_EMPTY           0       
#define MD_SIZE_64MB            1       
#define MD_SIZE_128MB           2       
#define MD_SIZE_256MB           3
#define MD_SIZE_512MB           4      
#define MD_SIZE_1GB             5      

#define MD_SIZE_BYTES(size)     ((size) == 0 ? 0 : 0x2000000L << (size))
#define MD_SIZE_MBYTES(size)    ((size) == 0 ? 0 :   0x20       << (size))
#define MD_NUM_ENABLED(_x)	((_x & 0x1) + ((_x >> 1) & 0x1) + \
				((_x >> 2) & 0x1) + ((_x >> 3) & 0x1))


/* Hardware page size and shift */

#define MD_PAGE_SIZE            16384    /* Page size in bytes              */
#define MD_PAGE_NUM_SHFT        14       /* Address to page number shift    */

#define MMC_IO_PROT 		(UINT64_CAST 1 << 45)

/* Register offsets from LOCAL_HUB or REMOTE_HUB */
#define MD_PERF_SEL             0x210000 /* Select perf monitor events      */

/* MD_MIG_VALUE_THRESH bit definitions */

#define MD_MIG_VALUE_THRES_VALID_MASK (UINT64_CAST 0x1 << 63)
#define MD_MIG_VALUE_THRES_VALUE_MASK (UINT64_CAST 0xfffff)

/* MD_MIG_CANDIDATE bit definitions */

#define MD_MIG_CANDIDATE_VALID_MASK (UINT64_CAST 0x1 << 63)
#define MD_MIG_CANDIDATE_VALID_SHFT 63
#define MD_MIG_CANDIDATE_TYPE_MASK (UINT64_CAST 0x1 << 30)
#define MD_MIG_CANDIDATE_TYPE_SHFT 30
#define MD_MIG_CANDIDATE_OVERRUN_MASK (UINT64_CAST 0x1 << 29)
#define MD_MIG_CANDIDATE_OVERRUN_SHFT 29
#define MD_MIG_CANDIDATE_NODEID_MASK (UINT64_CAST 0x1ff << 20)
#define MD_MIG_CANDIDATE_NODEID_SHFT 20
#define MD_MIG_CANDIDATE_ADDR_MASK (UINT64_CAST 0x3ffff)


/* XXX protection and migration are completely revised on SN1.  On
   SN0, the reference count and protection fields were accessed in the
   same word, but on SN1 they reside at different addresses.  The
   users of these macros will need to be rewritten.  Also, the MD page
   size is 16K on SN1 but 4K on SN0.  */

/* Premium SIMM protection entry shifts and masks. */

#define MD_PPROT_SHFT           0                       /* Prot. field      */
#define MD_PPROT_MASK           0xf
#define MD_PPROT_REFCNT_SHFT    5                       /* Reference count  */
#define MD_PPROT_REFCNT_WIDTH   0x7ffff
#define MD_PPROT_REFCNT_MASK    (MD_PPROT_REFCNT_WIDTH << 5)

#define MD_PPROT_IO_SHFT        8                       /* I/O Prot field   */

/* Standard SIMM protection entry shifts and masks. */

#define MD_SPROT_SHFT           0                       /* Prot. field      */
#define MD_SPROT_MASK           0xf
#define MD_SPROT_IO_SHFT	8
#define MD_SPROT_REFCNT_SHFT    5                       /* Reference count  */
#define MD_SPROT_REFCNT_WIDTH   0x7ff
#define MD_SPROT_REFCNT_MASK    (MD_SPROT_REFCNT_WIDTH << 5)

/* Migration modes used in protection entries */

#define MD_PROT_MIGMD_IREL      (UINT64_CAST 0x3 << 3)
#define MD_PROT_MIGMD_IABS      (UINT64_CAST 0x2 << 3)
#define MD_PROT_MIGMD_PREL      (UINT64_CAST 0x1 << 3)
#define MD_PROT_MIGMD_OFF       (UINT64_CAST 0x0 << 3)

/*
 * Operations on Memory/Directory DIMM control register
 */

#define DIRTYPE_PREMIUM 1
#define DIRTYPE_STANDARD 0

/*
 * Operations on page migration count difference and absolute threshold
 * registers
 */

#define MD_MIG_VALUE_THRESH_GET(region) (                               \
        REMOTE_HUB_L((region), MD_MIG_VALUE_THRESH) &  \
        MD_MIG_VALUE_THRES_VALUE_MASK)

#define MD_MIG_VALUE_THRESH_SET(region, value) (                        \
        REMOTE_HUB_S((region), MD_MIG_VALUE_THRESH,                     \
                MD_MIG_VALUE_THRES_VALID_MASK | (value)))

#define MD_MIG_VALUE_THRESH_ENABLE(region) (                    \
        REMOTE_HUB_S((region), MD_MIG_VALUE_THRESH,                     \
                REMOTE_HUB_L((region), MD_MIG_VALUE_THRESH)             \
                             | MD_MIG_VALUE_THRES_VALID_MASK))

/*
 * Operations on page migration candidate register
 */

#define MD_MIG_CANDIDATE_GET(my_region_id) ( \
        REMOTE_HUB_L((my_region_id), MD_MIG_CANDIDATE_CLR))

#define MD_MIG_CANDIDATE_HWPFN(value) ((value) & MD_MIG_CANDIDATE_ADDR_MASK)

#define MD_MIG_CANDIDATE_NODEID(value) ( \
        ((value) & MD_MIG_CANDIDATE_NODEID_MASK) >> MD_MIG_CANDIDATE_NODEID_SHFT)

#define MD_MIG_CANDIDATE_TYPE(value) ( \
        ((value) & MD_MIG_CANDIDATE_TYPE_MASK) >> MD_MIG_CANDIDATE_TYPE_SHFT)

#define MD_MIG_CANDIDATE_VALID(value) ( \
        ((value) & MD_MIG_CANDIDATE_VALID_MASK) >> MD_MIG_CANDIDATE_VALID_SHFT)

/*
 * Macros to retrieve fields in the protection entry
 */

/* for Premium SIMM */
#define MD_PPROT_REFCNT_GET(value) ( \
        ((value) & MD_PPROT_REFCNT_MASK) >> MD_PPROT_REFCNT_SHFT)

/* for Standard SIMM */
#define MD_SPROT_REFCNT_GET(value) ( \
        ((value) & MD_SPROT_REFCNT_MASK) >> MD_SPROT_REFCNT_SHFT)

#if _LANGUAGE_C
#ifdef LITTLE_ENDIAN

typedef union md_perf_sel {
        uint64_t      perf_sel_reg;
        struct  {
                uint64_t      perf_sel  :  3,
				perf_en   :  1,
				perf_rsvd : 60;
        } perf_sel_bits;
} md_perf_sel_t;

#else

typedef union md_perf_sel {
	uint64_t	perf_sel_reg;
	struct	{
		uint64_t	perf_rsvd : 60,
				perf_en	  :  1,
				perf_sel  :  3;
	} perf_sel_bits;
} md_perf_sel_t;

#endif
#endif /* _LANGUAGE_C */

#endif /* BRINGUP */

/* Like SN0, SN1 supports a mostly-flat address space with 8
   CPU-visible, evenly spaced, contiguous regions, or "software
   banks".  On SN1, software bank n begins at addresses n * 1GB, 
   0 <= n < 8.

   Physically (and very unlike SN0), each SN1 node board contains 8
   dimm sockets, arranged as 4 "DIMM banks" of 2 dimms each.  DIMM
   size and width (x4/x8) is assigned per dimm bank.  Each DIMM bank
   consists of 2 "physical banks", one on the front sides of the 2
   DIMMs and the other on the back sides.  Therefore a node has a
   total of 8 ( = 4 * 2) physical banks.  They are collectively
   referred to as "locational banks", since the locational bank number
   depends on the physical location of the DIMMs on the board.

	      Dimm bank 0, Phys bank 0a (locational bank 0a)
     Slot D0  ----------------------------------------------
	      Dimm bank 0, Phys bank 1a (locational bank 1a)

	      Dimm bank 1, Phys bank 0a (locational bank 2a)
     Slot D1  ----------------------------------------------
	      Dimm bank 1, Phys bank 1a (locational bank 3a)

	      Dimm bank 2, Phys bank 0a (locational bank 4a)
     Slot D2  ----------------------------------------------
	      Dimm bank 2, Phys bank 1a (locational bank 5a)

	      Dimm bank 3, Phys bank 0a (locational bank 6a)
     Slot D3  ----------------------------------------------
	      Dimm bank 3, Phys bank 1a (locational bank 7a)

	      Dimm bank 0, Phys bank 0b (locational bank 0b)
     Slot D4  ----------------------------------------------
	      Dimm bank 0, Phys bank 1b (locational bank 1b)

	      Dimm bank 1, Phys bank 0b (locational bank 2b)
     Slot D5  ----------------------------------------------
	      Dimm bank 1, Phys bank 1b (locational bank 3b)

	      Dimm bank 2, Phys bank 0b (locational bank 4b)
     Slot D6  ----------------------------------------------
	      Dimm bank 2, Phys bank 1b (locational bank 5b)

	      Dimm bank 3, Phys bank 0b (locational bank 6b)
     Slot D7  ----------------------------------------------
	      Dimm bank 3, Phys bank 1b (locational bank 7b)

   Since bank size is assigned per DIMM bank, each pair of locational
   banks must have the same size.  However, they may be
   enabled/disabled individually.

   The locational banks map to the software banks via the dimm0_sel
   field in MD_MEMORY_CONFIG.  When the field is 0 (the usual case),
   the mapping is direct:  eg. locational bank 1 (dimm bank 0,
   physical bank 1, which is the back side of the first DIMM pair)
   corresponds to software bank 1, at node offset 1GB.  More
   generally, locational bank = software bank XOR dimm0_sel.

   All the PROM's data structures (promlog variables, klconfig, etc.)
   track memory by the locational bank number.  The kernel usually
   tracks memory by the software bank number.
   memsupport.c:slot_psize_compute() performs the mapping.

   (Note:  the terms "locational bank" and "software bank" are not
   offical in any way, but I've tried to make the PROM use them
   consistently -- bjj.)
 */

#define MD_MEM_BANKS 		8
#define MD_MEM_DIMM_BANKS 	4
#define MD_BANK_SHFT            30                     /* log2(1 GB)     */
#define MD_BANK_MASK            (UINT64_CAST 0x7 << 30)
#define MD_BANK_SIZE            (UINT64_CAST 1 << MD_BANK_SHFT)   /*  1 GB */
#define MD_BANK_OFFSET(_b)      (UINT64_CAST (_b) << MD_BANK_SHFT)
#define MD_BANK_GET(addr)	(((addr) & MD_BANK_MASK) >> MD_BANK_SHFT)
#define MD_BANK_TO_DIMM_BANK(_b) (( (_b) >> 1) & 0x3)
#define MD_BANK_TO_PHYS_BANK(_b) (( (_b) >> 0) & 0x1)
#define MD_DIMM_BANK_GET(addr)   MD_BANK_TO_DIMM_BANK(MD_BANK_GET(addr))
#define MD_PHYS_BANK_GET(addr)   MD_BANK_TO_PHYS_BANK(MD_BANK_GET(addr))


/* Split an MD pointer (or message source & suppl. fields) into node, device */

#define MD_PTR_NODE_SHFT	3
#define MD_PTR_DEVICE_MASK	0x7
#define MD_PTR_SUBNODE0_MASK	0x1
#define MD_PTR_SUBNODE1_MASK	0x4


/**********************************************************************

 Backdoor protection and page counter structures

**********************************************************************/

/* Protection entries and page counters are interleaved at 4 separate
   addresses, 0x10 apart.  Software must read/write all four. */

#define BD_ITLV_COUNT		4
#define BD_ITLV_STRIDE		0x10

/* Protection entries */

/* (these macros work for standard (_rgn < 32) or premium DIMMs) */
#define MD_PROT_SHFT(_rgn, _io)	((((_rgn) & 0x20) >> 2 | \
				  ((_rgn) & 0x01) << 2 | \
				  ((_io)  &  0x1) << 1) * 8)
#define MD_PROT_MASK(_rgn, _io)	(0xff << MD_PROT_SHFT(_rgn, _io))
#define MD_PROT_GET(_val, _rgn, _io) \
	(((_val) & MD_PROT_MASK(_rgn, _io)) >> MD_PROT_SHFT(_rgn, _io))

/* Protection field values */

#define MD_PROT_RW              (UINT64_CAST 0xff)
#define MD_PROT_RO              (UINT64_CAST 0x0f)
#define MD_PROT_NO              (UINT64_CAST 0x00)




/**********************************************************************

 Directory format structures

***********************************************************************/

#ifdef _LANGUAGE_C

/* Standard Directory Entries */

#ifdef LITTLE_ENDIAN

struct	md_sdir_pointer_fmt { /* exclusive, busy shared/excl, wait, poisoned */
	bdrkreg_t	sdp_format                :	 2;
        bdrkreg_t       sdp_state                 :      3;
        bdrkreg_t       sdp_priority              :      3;
        bdrkreg_t       sdp_pointer1              :      8;
        bdrkreg_t       sdp_ecc                   :      6;
        bdrkreg_t       sdp_locprot               :      1;
        bdrkreg_t       sdp_reserved              :      1;
        bdrkreg_t       sdp_crit_word_off         :      3;
        bdrkreg_t       sdp_pointer2              :      5;
        bdrkreg_t       sdp_fill                  :     32;
};

#else

struct	md_sdir_pointer_fmt { /* exclusive, busy shared/excl, wait, poisoned */
	bdrkreg_t	sdp_fill		  :	32;
	bdrkreg_t	sdp_pointer2		  :	 5;
	bdrkreg_t	sdp_crit_word_off	  :	 3;
	bdrkreg_t	sdp_reserved		  :	 1;
	bdrkreg_t	sdp_locprot		  :	 1;
	bdrkreg_t	sdp_ecc			  :	 6;
	bdrkreg_t	sdp_pointer1		  :	 8;
	bdrkreg_t	sdp_priority		  :	 3;
	bdrkreg_t	sdp_state		  :	 3;
	bdrkreg_t	sdp_format		  :	 2;
};

#endif

#ifdef LITTLE_ENDIAN

struct	md_sdir_fine_fmt { /* shared (fine) */
	bdrkreg_t	sdf_format                :	 2;
        bdrkreg_t       sdf_tag1                  :      3;
        bdrkreg_t       sdf_tag2                  :      3;
        bdrkreg_t       sdf_vector1               :      8;
        bdrkreg_t       sdf_ecc                   :      6;
        bdrkreg_t       sdf_locprot               :      1;
        bdrkreg_t       sdf_tag2valid             :      1;
        bdrkreg_t       sdf_vector2               :      8;
        bdrkreg_t       sdf_fill                  :     32;
};

#else

struct	md_sdir_fine_fmt { /* shared (fine) */
	bdrkreg_t	sdf_fill		  :	32;
	bdrkreg_t	sdf_vector2		  :	 8;
	bdrkreg_t	sdf_tag2valid		  :	 1;
	bdrkreg_t	sdf_locprot		  :	 1;
	bdrkreg_t	sdf_ecc			  :	 6;
	bdrkreg_t	sdf_vector1		  :	 8;
	bdrkreg_t	sdf_tag2		  :	 3;
	bdrkreg_t	sdf_tag1		  :	 3;
	bdrkreg_t	sdf_format		  :	 2;
};

#endif

#ifdef LITTLE_ENDIAN

struct	md_sdir_coarse_fmt { /* shared (coarse) */
	bdrkreg_t	sdc_format                :	 2;
        bdrkreg_t       sdc_reserved_1            :      6;
        bdrkreg_t       sdc_vector_a              :      8;
        bdrkreg_t       sdc_ecc                   :      6;
        bdrkreg_t       sdc_locprot               :      1;
        bdrkreg_t       sdc_reserved              :      1;
        bdrkreg_t       sdc_vector_b              :      8;
        bdrkreg_t       sdc_fill                  :     32;
};

#else

struct	md_sdir_coarse_fmt { /* shared (coarse) */
	bdrkreg_t	sdc_fill		  :	32;
	bdrkreg_t	sdc_vector_b		  :	 8;
	bdrkreg_t	sdc_reserved		  :	 1;
	bdrkreg_t	sdc_locprot		  :	 1;
	bdrkreg_t	sdc_ecc			  :	 6;
	bdrkreg_t	sdc_vector_a		  :	 8;
	bdrkreg_t	sdc_reserved_1		  :	 6;
	bdrkreg_t	sdc_format		  :	 2;
};

#endif

typedef union md_sdir {
	/* The 32 bits of standard directory, in bits 31:0 */
	uint64_t	sd_val;
	struct	md_sdir_pointer_fmt	sdp_fmt;
	struct	md_sdir_fine_fmt	sdf_fmt;
	struct	md_sdir_coarse_fmt	sdc_fmt;
} md_sdir_t;


/* Premium Directory Entries */

#ifdef LITTLE_ENDIAN

struct	md_pdir_pointer_fmt { /* exclusive, busy shared/excl, wait, poisoned */
	bdrkreg_t	pdp_format                :	 2;
        bdrkreg_t       pdp_state                 :      3;
        bdrkreg_t       pdp_priority              :      3;
        bdrkreg_t       pdp_pointer1_a            :      8;
        bdrkreg_t       pdp_reserved_4            :      6;
        bdrkreg_t       pdp_pointer1_b            :      3;
        bdrkreg_t       pdp_reserved_3            :      7;
        bdrkreg_t       pdp_ecc_a                 :      6;
        bdrkreg_t       pdp_locprot               :      1;
        bdrkreg_t       pdp_reserved_2            :      1;
        bdrkreg_t       pdp_crit_word_off         :      3;
        bdrkreg_t       pdp_pointer2_a            :      5;
        bdrkreg_t       pdp_ecc_b                 :      1;
        bdrkreg_t       pdp_reserved_1            :      5;
        bdrkreg_t       pdp_pointer2_b            :      3;
        bdrkreg_t       pdp_reserved              :      7;
};

#else

struct	md_pdir_pointer_fmt { /* exclusive, busy shared/excl, wait, poisoned */
	bdrkreg_t	pdp_reserved		  :	 7;
	bdrkreg_t	pdp_pointer2_b		  :	 3;
	bdrkreg_t	pdp_reserved_1		  :	 5;
	bdrkreg_t	pdp_ecc_b		  :	 1;
	bdrkreg_t	pdp_pointer2_a		  :	 5;
	bdrkreg_t	pdp_crit_word_off	  :	 3;
	bdrkreg_t	pdp_reserved_2		  :	 1;
	bdrkreg_t	pdp_locprot		  :	 1;
	bdrkreg_t	pdp_ecc_a		  :	 6;
	bdrkreg_t	pdp_reserved_3		  :	 7;
	bdrkreg_t	pdp_pointer1_b		  :	 3;
	bdrkreg_t	pdp_reserved_4		  :	 6;
	bdrkreg_t	pdp_pointer1_a		  :	 8;
	bdrkreg_t	pdp_priority		  :	 3;
	bdrkreg_t	pdp_state		  :	 3;
	bdrkreg_t	pdp_format		  :	 2;
};

#endif

#ifdef LITTLE_ENDIAN

struct	md_pdir_fine_fmt { /* shared (fine) */
	bdrkreg_t	pdf_format                :	 2;
        bdrkreg_t       pdf_tag1_a                :      3;
        bdrkreg_t       pdf_tag2_a                :      3;
        bdrkreg_t       pdf_vector1_a             :      8;
        bdrkreg_t       pdf_reserved_1            :      6;
        bdrkreg_t       pdf_tag1_b                :      2;
        bdrkreg_t       pdf_vector1_b             :      8;
        bdrkreg_t       pdf_ecc_a                 :      6;
        bdrkreg_t       pdf_locprot               :      1;
        bdrkreg_t       pdf_tag2valid             :      1;
        bdrkreg_t       pdf_vector2_a             :      8;
        bdrkreg_t       pdf_ecc_b                 :      1;
        bdrkreg_t       pdf_reserved              :      5;
        bdrkreg_t       pdf_tag2_b                :      2;
        bdrkreg_t       pdf_vector2_b             :      8;
};

#else

struct	md_pdir_fine_fmt { /* shared (fine) */
	bdrkreg_t	pdf_vector2_b		  :	 8;
	bdrkreg_t	pdf_tag2_b		  :	 2;
	bdrkreg_t	pdf_reserved		  :	 5;
	bdrkreg_t	pdf_ecc_b		  :	 1;
	bdrkreg_t	pdf_vector2_a		  :	 8;
	bdrkreg_t	pdf_tag2valid		  :	 1;
	bdrkreg_t	pdf_locprot		  :	 1;
	bdrkreg_t	pdf_ecc_a		  :	 6;
	bdrkreg_t	pdf_vector1_b		  :	 8;
	bdrkreg_t	pdf_tag1_b		  :	 2;
	bdrkreg_t	pdf_reserved_1		  :	 6;
	bdrkreg_t	pdf_vector1_a		  :	 8;
	bdrkreg_t	pdf_tag2_a		  :	 3;
	bdrkreg_t	pdf_tag1_a		  :	 3;
	bdrkreg_t	pdf_format		  :	 2;
};

#endif

#ifdef LITTLE_ENDIAN

struct	md_pdir_sparse_fmt { /* shared (sparse) */
	bdrkreg_t	pds_format                :	 2;
        bdrkreg_t       pds_column_a              :      6;
        bdrkreg_t       pds_row_a                 :      8;
        bdrkreg_t       pds_column_b              :     16;
        bdrkreg_t       pds_ecc_a                 :      6;
        bdrkreg_t       pds_locprot               :      1;
        bdrkreg_t       pds_reserved_1            :      1;
        bdrkreg_t       pds_row_b                 :      8;
        bdrkreg_t       pds_ecc_b                 :      1;
        bdrkreg_t       pds_column_c              :     10;
        bdrkreg_t       pds_reserved              :      5;
};

#else

struct	md_pdir_sparse_fmt { /* shared (sparse) */
	bdrkreg_t	pds_reserved		  :	 5;
	bdrkreg_t	pds_column_c		  :	10;
	bdrkreg_t	pds_ecc_b		  :	 1;
	bdrkreg_t	pds_row_b		  :	 8;
	bdrkreg_t	pds_reserved_1		  :	 1;
	bdrkreg_t	pds_locprot		  :	 1;
	bdrkreg_t	pds_ecc_a		  :	 6;
	bdrkreg_t	pds_column_b		  :	16;
	bdrkreg_t	pds_row_a		  :	 8;
	bdrkreg_t	pds_column_a		  :	 6;
	bdrkreg_t	pds_format		  :	 2;
};

#endif

typedef union md_pdir {
	/* The 64 bits of premium directory */
	uint64_t	pd_val;
	struct	md_pdir_pointer_fmt	pdp_fmt;
	struct	md_pdir_fine_fmt	pdf_fmt;
	struct	md_pdir_sparse_fmt	pds_fmt;
} md_pdir_t;

#endif /* _LANGUAGE_C */


/**********************************************************************

 The defines for backdoor directory and backdoor ECC.

***********************************************************************/

/* Directory formats, for each format's "format" field */

#define MD_FORMAT_UNOWNED	(UINT64_CAST 0x0)	/* 00 */
#define MD_FORMAT_POINTER	(UINT64_CAST 0x1)	/* 01 */
#define MD_FORMAT_SHFINE	(UINT64_CAST 0x2)	/* 10 */
#define MD_FORMAT_SHCOARSE	(UINT64_CAST 0x3)	/* 11 */
  /* Shared coarse (standard) and shared sparse (premium) both use fmt 0x3 */


/*
 * Cacheline state values.
 *
 * These are really *software* notions of the "state" of a cacheline; but the
 * actual values have been carefully chosen to align with some hardware values!
 * The MD_FMT_ST_TO_STATE macro is used to convert from hardware format/state
 * pairs in the directory entried into one of these cacheline state values.
 */

#define MD_DIR_EXCLUSIVE	(UINT64_CAST 0x0)	/* ptr format, hw-defined */
#define MD_DIR_UNOWNED		(UINT64_CAST 0x1)	/* format=0 */
#define MD_DIR_SHARED		(UINT64_CAST 0x2)	/* format=2,3 */
#define MD_DIR_BUSY_SHARED	(UINT64_CAST 0x4)	/* ptr format, hw-defined */
#define MD_DIR_BUSY_EXCL	(UINT64_CAST 0x5)	/* ptr format, hw-defined */
#define MD_DIR_WAIT		(UINT64_CAST 0x6)	/* ptr format, hw-defined */
#define MD_DIR_POISONED		(UINT64_CAST 0x7)	/* ptr format, hw-defined */

#ifdef _LANGUAGE_C

/* Convert format and state fields into a single "cacheline state" value, defined above */

#define MD_FMT_ST_TO_STATE(fmt, state) \
  ((fmt) == MD_FORMAT_POINTER ? (state) : \
   (fmt) == MD_FORMAT_UNOWNED ? MD_DIR_UNOWNED : \
   MD_DIR_SHARED)
#define MD_DIR_STATE(x) MD_FMT_ST_TO_STATE(MD_DIR_FORMAT(x), MD_DIR_STVAL(x))

#endif /* _LANGUAGE_C */



/* Directory field shifts and masks */

/* Standard */

#define MD_SDIR_FORMAT_SHFT	0			/* All formats */
#define MD_SDIR_FORMAT_MASK	(0x3 << 0)
#define MD_SDIR_STATE_SHFT	2			/* Pointer fmt. only */
#define MD_SDIR_STATE_MASK	(0x7 << 2)

/* Premium */

#define MD_PDIR_FORMAT_SHFT	0			/* All formats */
#define MD_PDIR_FORMAT_MASK	(0x3 << 0)
#define MD_PDIR_STATE_SHFT	2			/* Pointer fmt. only */
#define MD_PDIR_STATE_MASK	(0x7 << 2)

/* Generic */

#define MD_FORMAT_SHFT	0				/* All formats */
#define MD_FORMAT_MASK	(0x3 << 0)
#define MD_STATE_SHFT	2				/* Pointer fmt. only */
#define MD_STATE_MASK	(0x7 << 2)


/* Special shifts to reconstruct fields from the _a and _b parts */

/* Standard:  only shared coarse has split fields */

#define MD_SDC_VECTORB_SHFT	8	/* eg: sdc_vector_a is 8 bits */

/* Premium:  pointer, shared fine, shared sparse */

#define MD_PDP_POINTER1A_MASK	0xFF
#define MD_PDP_POINTER1B_SHFT	8
#define MD_PDP_POINTER2B_SHFT	5
#define MD_PDP_ECCB_SHFT	6

#define MD_PDF_VECTOR1B_SHFT	8
#define MD_PDF_VECTOR2B_SHFT	8
#define MD_PDF_TAG1B_SHFT	3
#define MD_PDF_TAG2B_SHFT	3
#define MD_PDF_ECC_SHFT		6

#define MD_PDS_ROWB_SHFT	8
#define MD_PDS_COLUMNB_SHFT	6
#define MD_PDS_COLUMNC_SHFT	(MD_PDS_COLUMNB_SHFT + 16)
#define MD_PDS_ECC_SHFT		6



/*
 * Directory/protection/counter initialization values, premium and standard
 */

#define MD_PDIR_INIT		0
#define MD_PDIR_INIT_CNT	0
#define MD_PDIR_INIT_PROT	0

#define MD_SDIR_INIT		0
#define MD_SDIR_INIT_CNT	0
#define MD_SDIR_INIT_PROT	0

#define MD_PDIR_MASK            0xffffffffffffffff
#define MD_SDIR_MASK            0xffffffff

/* When premium mode is on for probing but standard directory memory
   is installed, the vaild directory bits depend on the phys. bank */
#define MD_PDIR_PROBE_MASK(pb)  0xffffffffffffffff
#define MD_SDIR_PROBE_MASK(pb)  (0xffff0000ffff << ((pb) ? 16 : 0))


/*
 * Misc. field extractions and conversions
 */

/* Convert an MD pointer (or message source, supplemental fields) */

#define MD_PTR_NODE(x)		((x) >> MD_PTR_NODE_SHFT)
#define MD_PTR_DEVICE(x)	((x) & MD_PTR_DEVICE_MASK)
#define MD_PTR_SLICE(x)		(((x) & MD_PTR_SUBNODE0_MASK) | \
				 ((x) & MD_PTR_SUBNODE1_MASK) >> 1)
#define MD_PTR_OWNER_CPU(x)	(! ((x) & 2))
#define MD_PTR_OWNER_IO(x)	((x) & 2)

/* Extract format and raw state from a directory entry */

#define MD_DIR_FORMAT(x)	((x) >> MD_SDIR_FORMAT_SHFT & \
				 MD_SDIR_FORMAT_MASK >> MD_SDIR_FORMAT_SHFT)
#define MD_DIR_STVAL(x)		((x) >> MD_SDIR_STATE_SHFT & \
				 MD_SDIR_STATE_MASK >> MD_SDIR_STATE_SHFT)

/* Mask & Shift to get HSPEC_ADDR from MD DIR_ERROR register */
#define ERROR_ADDR_SHFT         3
#define ERROR_HSPEC_SHFT        3
#define DIR_ERR_HSPEC_MASK      0x1fffffff8

/*
 *  DIR_ERR* and MEM_ERR* defines are used to avoid ugly
 *  #ifdefs for SN0 and SN1 in memerror.c code.  See SN0/hubmd.h
 *  for corresponding SN0 definitions.
 */
#define md_dir_error_t  md_dir_error_u_t
#define md_mem_error_t  md_mem_error_u_t
#define derr_reg        md_dir_error_regval
#define merr_reg        md_mem_error_regval

#define DIR_ERR_UCE_VALID       dir_err.md_dir_error_fld_s.de_uce_valid
#define DIR_ERR_AE_VALID        dir_err.md_dir_error_fld_s.de_ae_valid
#define DIR_ERR_BAD_SYN         dir_err.md_dir_error_fld_s.de_bad_syn
#define DIR_ERR_CE_OVERRUN      dir_err.md_dir_error_fld_s.de_ce_overrun
#define MEM_ERR_ADDRESS         mem_err.md_mem_error_fld_s.me_address
        /* BRINGUP Can the overrun bit be set without the valid bit? */
#define MEM_ERR_CE_OVERRUN      (mem_err.md_mem_error_fld_s.me_read_ce >> 1)
#define MEM_ERR_BAD_SYN         mem_err.md_mem_error_fld_s.me_bad_syn
#define MEM_ERR_UCE_VALID       (mem_err.md_mem_error_fld_s.me_read_uce & 1)



/*********************************************************************

 We have the shift and masks of various fields defined below.

 *********************************************************************/

/* MD_REFRESH_CONTROL fields */

#define MRC_ENABLE_SHFT         63
#define MRC_ENABLE_MASK         (UINT64_CAST 1 << 63)
#define MRC_ENABLE              (UINT64_CAST 1 << 63)
#define MRC_COUNTER_SHFT        12
#define MRC_COUNTER_MASK        (UINT64_CAST 0xfff << 12)
#define MRC_CNT_THRESH_MASK     0xfff
#define MRC_RESET_DEFAULTS      (UINT64_CAST 0x800)

/* MD_DIR_CONFIG fields */

#define MDC_DIR_PREMIUM		(UINT64_CAST 1 << 0)
#define MDC_IGNORE_ECC_SHFT      1
#define MDC_IGNORE_ECC_MASK     (UINT64_CAST 1 << 1)

/* MD_MEMORY_CONFIG fields */

#define MMC_RP_CONFIG_SHFT	61
#define MMC_RP_CONFIG_MASK	(UINT64_CAST 1 << 61)
#define MMC_RCD_CONFIG_SHFT	60
#define MMC_RCD_CONFIG_MASK	(UINT64_CAST 1 << 60)
#define MMC_MB_NEG_EDGE_SHFT	56
#define MMC_MB_NEG_EDGE_MASK	(UINT64_CAST 0x7 << 56)
#define MMC_SAMPLE_TIME_SHFT	52
#define MMC_SAMPLE_TIME_MASK	(UINT64_CAST 0x3 << 52)
#define MMC_DELAY_MUX_SEL_SHFT	50
#define MMC_DELAY_MUX_SEL_MASK	(UINT64_CAST 0x3 << 50)
#define MMC_PHASE_DELAY_SHFT	49
#define MMC_PHASE_DELAY_MASK	(UINT64_CAST 1 << 49)
#define MMC_DB_NEG_EDGE_SHFT	48
#define MMC_DB_NEG_EDGE_MASK	(UINT64_CAST 1 << 48)
#define MMC_CPU_PROT_IGNORE_SHFT	 47
#define MMC_CPU_PROT_IGNORE_MASK	(UINT64_CAST 1 << 47)
#define MMC_IO_PROT_IGNORE_SHFT 46
#define MMC_IO_PROT_IGNORE_MASK	(UINT64_CAST 1 << 46)
#define MMC_IO_PROT_EN_SHFT	45
#define MMC_IO_PROT_EN_MASK	(UINT64_CAST 1 << 45)
#define MMC_CC_ENABLE_SHFT	44
#define MMC_CC_ENABLE_MASK	(UINT64_CAST 1 << 44)
#define MMC_DIMM0_SEL_SHFT	32
#define MMC_DIMM0_SEL_MASK     (UINT64_CAST 0x3 << 32)
#define MMC_DIMM_SIZE_SHFT(_dimm)    ((_dimm << 3) + 4)
#define MMC_DIMM_SIZE_MASK(_dimm)    (UINT64_CAST 0xf << MMC_DIMM_SIZE_SHFT(_dimm))
#define MMC_DIMM_WIDTH_SHFT(_dimm)    ((_dimm << 3) + 3)
#define MMC_DIMM_WIDTH_MASK(_dimm)    (UINT64_CAST 0x1 << MMC_DIMM_WIDTH_SHFT(_dimm))
#define MMC_DIMM_BANKS_SHFT(_dimm)    (_dimm << 3)
#define MMC_DIMM_BANKS_MASK(_dimm)    (UINT64_CAST 0x3 << MMC_DIMM_BANKS_SHFT(_dimm))
#define MMC_BANK_ALL_MASK	0xffffffffLL
/* Default values for write-only bits in MD_MEMORY_CONFIG */
#define MMC_DEFAULT_BITS	(UINT64_CAST 0x7 << MMC_MB_NEG_EDGE_SHFT)

/* MD_MB_ECC_CONFIG fields */

#define MEC_IGNORE_ECC		(UINT64_CAST 0x1 << 0)

/* MD_BIST_DATA fields */

#define MBD_BIST_WRITE		(UINT64_CAST 1 << 7)
#define MBD_BIST_CYCLE		(UINT64_CAST 1 << 6)
#define MBD_BIST_BYTE		(UINT64_CAST 1 << 5)
#define MBD_BIST_NIBBLE		(UINT64_CAST 1 << 4)
#define MBD_BIST_DATA_MASK	0xf

/* MD_BIST_CTL fields */

#define MBC_DIMM_SHFT		5
#define MBC_DIMM_MASK		(UINT64_CAST 0x3 << 5)
#define MBC_BANK_SHFT		4
#define MBC_BANK_MASK		(UINT64_CAST 0x1 << 4)
#define MBC_BIST_RESET		(UINT64_CAST 0x1 << 2)
#define MBC_BIST_STOP		(UINT64_CAST 0x1 << 1)
#define MBC_BIST_START		(UINT64_CAST 0x1 << 0)

#define MBC_GO(dimm, bank) \
    (((dimm) << MBC_DIMM_SHFT) & MBC_DIMM_MASK | \
     ((bank) << MBC_BANK_SHFT) & MBC_BANK_MASK | \
     MBC_BIST_START)

/* MD_BIST_STATUS fields */

#define MBS_BIST_DONE		(UINT64_CAST 0X1 << 1)
#define MBS_BIST_PASSED		(UINT64_CAST 0X1 << 0)

/* MD_JUNK_BUS_TIMING fields */

#define MJT_SYNERGY_ENABLE_SHFT	40
#define MJT_SYNERGY_ENABLE_MASK	(UINT64_CAST 0Xff << MJT_SYNERGY_ENABLE_SHFT)
#define MJT_SYNERGY_SETUP_SHFT	32
#define MJT_SYNERGY_SETUP_MASK	(UINT64_CAST 0Xff << MJT_SYNERGY_SETUP_SHFT)
#define MJT_UART_ENABLE_SHFT	24
#define MJT_UART_ENABLE_MASK	(UINT64_CAST 0Xff << MJT_UART_ENABLE_SHFT)
#define MJT_UART_SETUP_SHFT	16
#define MJT_UART_SETUP_MASK	(UINT64_CAST 0Xff << MJT_UART_SETUP_SHFT)
#define MJT_FPROM_ENABLE_SHFT	8
#define MJT_FPROM_ENABLE_MASK	(UINT64_CAST 0Xff << MJT_FPROM_ENABLE_SHFT)
#define MJT_FPROM_SETUP_SHFT	0
#define MJT_FPROM_SETUP_MASK	(UINT64_CAST 0Xff << MJT_FPROM_SETUP_SHFT)

#define MEM_ERROR_VALID_CE      1


/* MD_FANDOP_CAC_STAT0, MD_FANDOP_CAC_STAT1 addr field shift */

#define MFC_ADDR_SHFT		6

#endif  /* _ASM_SN_SN1_HUBMD_NEXT_H */
