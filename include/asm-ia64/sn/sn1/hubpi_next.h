/* $Id$
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1992 - 1997, 2000 Silicon Graphics, Inc.
 * Copyright (C) 2000 by Colin Ngam
 */
#ifndef _ASM_SN_SN1_HUBPI_NEXT_H
#define _ASM_SN_SN1_HUBPI_NEXT_H


/* define for remote PI_1 space. It is always half of a node_addressspace
 * from PI_0. The normal REMOTE_HUB space for PI registers access
 * the PI_0 space, unless they are qualified by PI_1.
 */
#define PI_0(x)			(x)
#define PI_1(x)			((x) + 0x200000)
#define PIREG(x,sn)		((sn) ? PI_1(x) : PI_0(x))

#define PI_MIN_STACK_SIZE 4096  /* For figuring out the size to set */
#define PI_STACK_SIZE_SHFT      12      /* 4k */

#define PI_STACKADDR_OFFSET     (PI_ERR_STACK_ADDR_B - PI_ERR_STACK_ADDR_A)
#define PI_ERRSTAT_OFFSET       (PI_ERR_STATUS0_B - PI_ERR_STATUS0_A)
#define PI_RDCLR_OFFSET         (PI_ERR_STATUS0_A_RCLR - PI_ERR_STATUS0_A)
/* these macros are correct, but fix their users to understand two PIs
   and 4 CPUs (slices) per bedrock */
#define PI_INT_MASK_OFFSET      (PI_INT_MASK0_B - PI_INT_MASK0_A)
#define PI_INT_SET_OFFSET       (PI_CC_PEND_CLR_B - PI_CC_PEND_CLR_A)
#define PI_NMI_OFFSET		(PI_NMI_B - PI_NMI_A)

#define ERR_STACK_SIZE_BYTES(_sz) \
       ((_sz) ? (PI_MIN_STACK_SIZE << ((_sz) - 1)) : 0)

#define PI_CRB_STS_P	(1 << 9) 	/* "P" (partial word read/write) bit */
#define PI_CRB_STS_V	(1 << 8)	/* "V" (valid) bit */
#define PI_CRB_STS_R	(1 << 7)	/* "R" (response data sent to CPU) */
#define PI_CRB_STS_A	(1 << 6)	/* "A" (data ack. received) bit */
#define PI_CRB_STS_W	(1 << 5)	/* "W" (waiting for write compl.) */
#define PI_CRB_STS_H	(1 << 4)	/* "H" (gathering invalidates) bit */
#define PI_CRB_STS_I	(1 << 3)	/* "I" (targ. inbound invalidate) */
#define PI_CRB_STS_T	(1 << 2)	/* "T" (targ. inbound intervention) */
#define PI_CRB_STS_E	(0x3)		/* "E" (coherent read type) */

/* When the "P" bit is set in the sk_crb_sts field of an error stack
 * entry, the "R," "A," "H," and "I" bits are actually bits 6..3 of
 * the address.  This macro extracts those address bits and shifts
 * them to their proper positions, ready to be ORed in to the rest of
 * the address (which is calculated as sk_addr << 7).
 */
#define PI_CRB_STS_ADDR_BITS(sts) \
    ((sts) & (PI_CRB_STS_I | PI_CRB_STS_H) | \
     ((sts) & (PI_CRB_STS_A | PI_CRB_STS_R)) >> 1)

#ifdef _LANGUAGE_C
/*
 * format of error stack and error status registers.
 */

#ifdef LITTLE_ENDIAN

struct err_stack_format {
        uint64_t      sk_err_type:  3,   /* error type        */
                        sk_suppl   :  3,   /* lowest 3 bit of supplemental */
                        sk_t5_req  :  3,   /* RRB T5 request number */
                        sk_crb_num :  3,   /* WRB (0 to 7) or RRB (0 to 4) */
                        sk_rw_rb   :  1,   /* RRB == 0, WRB == 1 */
                        sk_crb_sts : 10,   /* status from RRB or WRB */
                        sk_cmd     :  8,   /* message command */
			sk_addr    : 33;   /* address */
};

#else

struct err_stack_format {
	uint64_t	sk_addr	   : 33,   /* address */
			sk_cmd	   :  8,   /* message command */
			sk_crb_sts : 10,   /* status from RRB or WRB */
			sk_rw_rb   :  1,   /* RRB == 0, WRB == 1 */
			sk_crb_num :  3,   /* WRB (0 to 7) or RRB (0 to 4) */
			sk_t5_req  :  3,   /* RRB T5 request number */
			sk_suppl   :  3,   /* lowest 3 bit of supplemental */
			sk_err_type:  3;   /* error type	*/
};

#endif

typedef union pi_err_stack {
        uint64_t      pi_stk_word;
        struct  err_stack_format pi_stk_fmt;
} pi_err_stack_t;

/* Simplified version of pi_err_status0_a_u_t (PI_ERR_STATUS0_A) */
#ifdef LITTLE_ENDIAN

struct err_status0_format {
	uint64_t	s0_err_type	:  3,	/* Encoded error cause */
                        s0_proc_req_num :  3,   /* Request number for RRB only */
                        s0_supplemental : 11,   /* ncoming message sup field */
                        s0_cmd          :  8,   /* Incoming message command */
                        s0_addr         : 37,   /* Address */
                        s0_over_run     :  1,   /* Subsequent errors spooled */
			s0_valid        :  1;   /* error is valid */
};

#else

struct err_status0_format {
	uint64_t	s0_valid	:  1,	/* error is valid */
			s0_over_run	:  1,	/* Subsequent errors spooled */
			s0_addr		: 37,	/* Address */
			s0_cmd		:  8,	/* Incoming message command */
			s0_supplemental : 11,	/* ncoming message sup field */
			s0_proc_req_num :  3,	/* Request number for RRB only */
			s0_err_type	:  3;	/* Encoded error cause */
};

#endif


typedef union pi_err_stat0 {
	uint64_t	pi_stat0_word;
        struct err_status0_format pi_stat0_fmt;
} pi_err_stat0_t;

/* Simplified version of pi_err_status1_a_u_t (PI_ERR_STATUS1_A) */

#ifdef LITTLE_ENDIAN

struct err_status1_format {
	 uint64_t	s1_spl_cnt : 21,   /* number spooled to memory */
                        s1_to_cnt  :  8,   /* crb timeout counter */
                        s1_inval_cnt:10,   /* signed invalidate counter RRB */
                        s1_crb_num :  3,   /* WRB (0 to 7) or RRB (0 to 4) */
                        s1_rw_rb   :  1,   /* RRB == 0, WRB == 1 */
                        s1_crb_sts : 10,   /* status from RRB or WRB */
			s1_src     : 11;   /* message source */
};

#else

struct err_status1_format {
	uint64_t	s1_src	   : 11,   /* message source */
			s1_crb_sts : 10,   /* status from RRB or WRB */
			s1_rw_rb   :  1,   /* RRB == 0, WRB == 1 */
			s1_crb_num :  3,   /* WRB (0 to 7) or RRB (0 to 4) */
			s1_inval_cnt:10,   /* signed invalidate counter RRB */
			s1_to_cnt  :  8,   /* crb timeout counter */
			s1_spl_cnt : 21;   /* number spooled to memory */
};

#endif

typedef union pi_err_stat1 {
	uint64_t	pi_stat1_word;
	struct err_status1_format pi_stat1_fmt;
} pi_err_stat1_t;
#endif

/* Error stack types (sk_err_type) for reads:	*/
#define PI_ERR_RD_AERR		0	/* Read Access Error */
#define PI_ERR_RD_PRERR         1	/* Uncached Partitial Read */
#define PI_ERR_RD_DERR          2	/* Directory Error */
#define PI_ERR_RD_TERR          3	/* read timeout */
#define PI_ERR_RD_PERR		4	/* Poison Access Violation */
#define PI_ERR_RD_NACK		5	/* Excessive NACKs	*/
#define PI_ERR_RD_RDE		6	/* Response Data Error	*/
#define PI_ERR_RD_PLERR		7	/* Packet Length Error */
/* Error stack types (sk_err_type) for writes:	*/
#define PI_ERR_WR_WERR          0	/* Write Access Error */
#define PI_ERR_WR_PWERR         1	/* Uncached Write Error */
#define PI_ERR_WR_TERR          3	/* write timeout */
#define PI_ERR_WR_RDE		6	/* Response Data Error */
#define PI_ERR_WR_PLERR		7	/* Packet Length Error */


/* For backwards compatibility */
#define PI_RT_COUNT	PI_RT_COUNTER    /* Real Time Counter 		    */
#define PI_RT_EN_A	PI_RT_INT_EN_A   /* RT int for CPU A enable         */
#define PI_RT_EN_B	PI_RT_INT_EN_B   /* RT int for CPU B enable         */
#define PI_PROF_EN_A	PI_PROF_INT_EN_A /* PROF int for CPU A enable       */
#define PI_PROF_EN_B	PI_PROF_INT_EN_B /* PROF int for CPU B enable       */
#define PI_RT_PEND_A    PI_RT_INT_PEND_A /* RT interrupt pending 	    */
#define PI_RT_PEND_B    PI_RT_INT_PEND_B /* RT interrupt pending 	    */
#define PI_PROF_PEND_A  PI_PROF_INT_PEND_A /* Profiling interrupt pending   */
#define PI_PROF_PEND_B  PI_PROF_INT_PEND_B /* Profiling interrupt pending   */


/* Bits in PI_SYSAD_ERRCHK_EN */
#define PI_SYSAD_ERRCHK_ECCGEN  0x01    /* Enable ECC generation            */
#define PI_SYSAD_ERRCHK_QUALGEN 0x02    /* Enable data quality signal gen.  */
#define PI_SYSAD_ERRCHK_SADP    0x04    /* Enable SysAD parity checking     */
#define PI_SYSAD_ERRCHK_CMDP    0x08    /* Enable SysCmd parity checking    */
#define PI_SYSAD_ERRCHK_STATE   0x10    /* Enable SysState parity checking  */
#define PI_SYSAD_ERRCHK_QUAL    0x20    /* Enable data quality checking     */
#define PI_SYSAD_CHECK_ALL      0x3f    /* Generate and check all signals.  */

/* CALIAS values */
#define PI_CALIAS_SIZE_0        0
#define PI_CALIAS_SIZE_4K       1
#define PI_CALIAS_SIZE_8K       2
#define PI_CALIAS_SIZE_16K      3
#define PI_CALIAS_SIZE_32K      4
#define PI_CALIAS_SIZE_64K      5
#define PI_CALIAS_SIZE_128K     6
#define PI_CALIAS_SIZE_256K     7
#define PI_CALIAS_SIZE_512K     8
#define PI_CALIAS_SIZE_1M       9
#define PI_CALIAS_SIZE_2M       10
#define PI_CALIAS_SIZE_4M       11
#define PI_CALIAS_SIZE_8M       12
#define PI_CALIAS_SIZE_16M      13
#define PI_CALIAS_SIZE_32M      14
#define PI_CALIAS_SIZE_64M      15

/* Fields in PI_ERR_STATUS0_[AB] */
#define PI_ERR_ST0_VALID_MASK	0x8000000000000000
#define PI_ERR_ST0_VALID_SHFT	63

/* Fields in PI_SPURIOUS_HDR_0 */
#define PI_SPURIOUS_HDR_VALID_MASK	0x8000000000000000
#define PI_SPURIOUS_HDR_VALID_SHFT	63

/* Fields in PI_NACK_CNT_A/B */
#define PI_NACK_CNT_EN_SHFT	20
#define PI_NACK_CNT_EN_MASK	0x100000
#define PI_NACK_CNT_MASK	0x0fffff
#define PI_NACK_CNT_MAX		0x0fffff

/* Bits in PI_ERR_INT_PEND */
#define PI_ERR_SPOOL_CMP_B	0x000000001	/* Spool end hit high water */
#define PI_ERR_SPOOL_CMP_A	0x000000002
#define PI_ERR_SPUR_MSG_B	0x000000004	/* Spurious message intr.   */
#define PI_ERR_SPUR_MSG_A	0x000000008
#define PI_ERR_WRB_TERR_B	0x000000010	/* WRB TERR		    */
#define PI_ERR_WRB_TERR_A	0x000000020
#define PI_ERR_WRB_WERR_B	0x000000040	/* WRB WERR 		    */
#define PI_ERR_WRB_WERR_A	0x000000080
#define PI_ERR_SYSSTATE_B	0x000000100	/* SysState parity error    */
#define PI_ERR_SYSSTATE_A	0x000000200
#define PI_ERR_SYSAD_DATA_B	0x000000400	/* SysAD data parity error  */
#define PI_ERR_SYSAD_DATA_A	0x000000800
#define PI_ERR_SYSAD_ADDR_B	0x000001000	/* SysAD addr parity error  */
#define PI_ERR_SYSAD_ADDR_A	0x000002000
#define PI_ERR_SYSCMD_DATA_B	0x000004000	/* SysCmd data parity error */
#define PI_ERR_SYSCMD_DATA_A	0x000008000
#define PI_ERR_SYSCMD_ADDR_B	0x000010000	/* SysCmd addr parity error */
#define PI_ERR_SYSCMD_ADDR_A	0x000020000
#define PI_ERR_BAD_SPOOL_B	0x000040000	/* Error spooling to memory */
#define PI_ERR_BAD_SPOOL_A	0x000080000
#define PI_ERR_UNCAC_UNCORR_B	0x000100000	/* Uncached uncorrectable   */
#define PI_ERR_UNCAC_UNCORR_A	0x000200000
#define PI_ERR_SYSSTATE_TAG_B	0x000400000	/* SysState tag parity error */
#define PI_ERR_SYSSTATE_TAG_A	0x000800000
#define PI_ERR_MD_UNCORR	0x001000000	/* Must be cleared in MD    */
#define PI_ERR_SYSAD_BAD_DATA_B	0x002000000	/* SysAD Data quality bad   */
#define PI_ERR_SYSAD_BAD_DATA_A	0x004000000
#define PI_ERR_UE_CACHED_B	0x008000000	/* UE during cached load    */
#define PI_ERR_UE_CACHED_A	0x010000000
#define PI_ERR_PKT_LEN_ERR_B	0x020000000	/* Xbar data too long/short */
#define PI_ERR_PKT_LEN_ERR_A	0x040000000
#define PI_ERR_IRB_ERR_B	0x080000000	/* Protocol error           */
#define PI_ERR_IRB_ERR_A	0x100000000
#define PI_ERR_IRB_TIMEOUT_B	0x200000000	/* IRB_B got a timeout      */
#define PI_ERR_IRB_TIMEOUT_A	0x400000000

#define PI_ERR_CLEAR_ALL_A	0x554aaaaaa
#define PI_ERR_CLEAR_ALL_B	0x2aa555555


/*
 * The following three macros define all possible error int pends. 
 */

#define PI_FATAL_ERR_CPU_A	(PI_ERR_SYSAD_BAD_DATA_A | \
				 PI_ERR_SYSSTATE_TAG_A 	| \
				 PI_ERR_BAD_SPOOL_A 	| \
				 PI_ERR_SYSCMD_ADDR_A 	| \
				 PI_ERR_SYSCMD_DATA_A 	| \
				 PI_ERR_SYSAD_ADDR_A 	| \
				 PI_ERR_SYSAD_DATA_A	| \
				 PI_ERR_SYSSTATE_A)

#define PI_MISC_ERR_CPU_A	(PI_ERR_IRB_TIMEOUT_A   | \
				 PI_ERR_IRB_ERR_A       | \
				 PI_ERR_PKT_LEN_ERR_A   | \
				 PI_ERR_UE_CACHED_A     | \
				 PI_ERR_UNCAC_UNCORR_A 	| \
				 PI_ERR_WRB_WERR_A 	| \
				 PI_ERR_WRB_TERR_A 	| \
				 PI_ERR_SPUR_MSG_A 	| \
				 PI_ERR_SPOOL_CMP_A)

#define PI_FATAL_ERR_CPU_B	(PI_ERR_SYSAD_BAD_DATA_B | \
				 PI_ERR_SYSSTATE_TAG_B 	| \
				 PI_ERR_BAD_SPOOL_B 	| \
				 PI_ERR_SYSCMD_ADDR_B 	| \
				 PI_ERR_SYSCMD_DATA_B 	| \
				 PI_ERR_SYSAD_ADDR_B 	| \
				 PI_ERR_SYSAD_DATA_B	| \
				 PI_ERR_SYSSTATE_B)

#define PI_MISC_ERR_CPU_B 	(PI_ERR_IRB_TIMEOUT_B   | \
				 PI_ERR_IRB_ERR_B       | \
				 PI_ERR_PKT_LEN_ERR_B   | \
				 PI_ERR_UE_CACHED_B     | \
				 PI_ERR_UNCAC_UNCORR_B  | \
				 PI_ERR_WRB_WERR_B 	| \
				 PI_ERR_WRB_TERR_B 	| \
				 PI_ERR_SPUR_MSG_B 	| \
				 PI_ERR_SPOOL_CMP_B)

#define PI_ERR_GENERIC	(PI_ERR_MD_UNCORR)

/* Values for PI_MAX_CRB_TIMEOUT and PI_CRB_SFACTOR */
#define PMCT_MAX	0xff
#define PCS_MAX		0xffffff

/* pi_err_status0_a_u_t address shift */
#define ERR_STAT0_ADDR_SHFT     3

/* PI error read/write bit (RRB == 0, WRB == 1)	*/
/* pi_err_status1_a_u_t.pi_err_status1_a_fld_s.esa_wrb */
#define PI_ERR_RRB	0
#define PI_ERR_WRB	1

/* Error stack address shift, for use with pi_stk_fmt.sk_addr */
#define ERR_STK_ADDR_SHFT	3

#endif /* _ASM_SN_SN1_HUBPI_NEXT_H */
