/* $Id$
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1992 - 1997, 2000 Silicon Graphics, Inc.
 * Copyright (C) 2000 by Colin Ngam
 */
#ifndef _ASM_SN_SN1_HUBPI_H
#define _ASM_SN_SN1_HUBPI_H

/************************************************************************
 *                                                                      *
 *      WARNING!!!  WARNING!!!  WARNING!!!  WARNING!!!  WARNING!!!      *
 *                                                                      *
 * This file is created by an automated script. Any (minimal) changes   *
 * made manually to this  file should be made with care.                *
 *                                                                      *
 *               MAKE ALL ADDITIONS TO THE END OF THIS FILE             *
 *                                                                      *
 ************************************************************************/


#define    PI_CPU_PROTECT            0x00000000    /* CPU Protection         */



#define    PI_PROT_OVRRD             0x00000008    /*
                                                    * Clear CPU
                                                    * Protection bit in 
                                                    * CPU_PROTECT
                                                    */



#define    PI_IO_PROTECT             0x00000010    /*
                                                    * Interrupt Pending
                                                    * Protection for IO
                                                    * access
                                                    */



#define    PI_REGION_PRESENT         0x00000018    /* Region present         */



#define    PI_CPU_NUM                0x00000020    /* CPU Number ID          */



#define    PI_CALIAS_SIZE            0x00000028    /* Cached Alias Size      */



#define    PI_MAX_CRB_TIMEOUT        0x00000030    /*
                                                    * Maximum Timeout for
                                                    * CRB
                                                    */



#define    PI_CRB_SFACTOR            0x00000038    /*
                                                    * Scale Factor for
                                                    * CRB Timeout
                                                    */



#define    PI_CPU_PRESENT_A          0x00000040    /*
                                                    * CPU Present for
                                                    * CPU_A
                                                    */



#define    PI_CPU_PRESENT_B          0x00000048    /*
                                                    * CPU Present for
                                                    * CPU_B
                                                    */



#define    PI_CPU_ENABLE_A           0x00000050    /*
                                                    * CPU Enable for
                                                    * CPU_A
                                                    */



#define    PI_CPU_ENABLE_B           0x00000058    /*
                                                    * CPU Enable for
                                                    * CPU_B
                                                    */



#define    PI_REPLY_LEVEL            0x00010060    /*
                                                    * Reply FIFO Priority
                                                    * Control
                                                    */



#define    PI_GFX_CREDIT_MODE        0x00020068    /*
                                                    * Graphics Credit
                                                    * Mode
                                                    */



#define    PI_NMI_A                  0x00000070    /*
                                                    * Non-maskable
                                                    * Interrupt to CPU A
                                                    */



#define    PI_NMI_B                  0x00000078    /*
                                                    * Non-maskable
                                                    * Interrupt to CPU B
                                                    */



#define    PI_INT_PEND_MOD           0x00000090    /*
                                                    * Interrupt Pending
                                                    * Modify
                                                    */



#define    PI_INT_PEND0              0x00000098    /* Interrupt Pending 0    */



#define    PI_INT_PEND1              0x000000A0    /* Interrupt Pending 1    */



#define    PI_INT_MASK0_A            0x000000A8    /*
                                                    * Interrupt Mask 0
                                                    * for CPU A
                                                    */



#define    PI_INT_MASK1_A            0x000000B0    /*
                                                    * Interrupt Mask 1
                                                    * for CPU A
                                                    */



#define    PI_INT_MASK0_B            0x000000B8    /*
                                                    * Interrupt Mask 0
                                                    * for CPU B
                                                    */



#define    PI_INT_MASK1_B            0x000000C0    /*
                                                    * Interrupt Mask 1
                                                    * for CPU B
                                                    */



#define    PI_CC_PEND_SET_A          0x000000C8    /*
                                                    * CC Interrupt
                                                    * Pending for CPU A
                                                    */



#define    PI_CC_PEND_SET_B          0x000000D0    /*
                                                    * CC Interrupt
                                                    * Pending for CPU B
                                                    */



#define    PI_CC_PEND_CLR_A          0x000000D8    /*
                                                    * CPU to CPU
                                                    * Interrupt Pending
                                                    * Clear for CPU A
                                                    */



#define    PI_CC_PEND_CLR_B          0x000000E0    /*
                                                    * CPU to CPU
                                                    * Interrupt Pending
                                                    * Clear for CPU B
                                                    */



#define    PI_CC_MASK                0x000000E8    /*
                                                    * Mask of both
                                                    * CC_PENDs
                                                    */



#define    PI_INT_PEND1_REMAP        0x000000F0    /*
                                                    * Remap Interrupt
                                                    * Pending
                                                    */



#define    PI_RT_COUNTER             0x00030100    /* Real Time Counter      */



#define    PI_RT_COMPARE_A           0x00000108    /* Real Time Compare A    */



#define    PI_RT_COMPARE_B           0x00000110    /* Real Time Compare B    */



#define    PI_PROFILE_COMPARE        0x00000118    /* Profiling Compare      */



#define    PI_RT_INT_PEND_A          0x00000120    /*
                                                    * RT interrupt
                                                    * pending
                                                    */



#define    PI_RT_INT_PEND_B          0x00000128    /*
                                                    * RT interrupt
                                                    * pending
                                                    */



#define    PI_PROF_INT_PEND_A        0x00000130    /*
                                                    * Profiling interrupt
                                                    * pending
                                                    */



#define    PI_PROF_INT_PEND_B        0x00000138    /*
                                                    * Profiling interrupt
                                                    * pending
                                                    */



#define    PI_RT_INT_EN_A            0x00000140    /* RT Interrupt Enable    */



#define    PI_RT_INT_EN_B            0x00000148    /* RT Interrupt Enable    */



#define    PI_PROF_INT_EN_A          0x00000150    /*
                                                    * Profiling Interrupt
                                                    * Enable
                                                    */



#define    PI_PROF_INT_EN_B          0x00000158    /*
                                                    * Profiling Interrupt
                                                    * Enable
                                                    */



#define    PI_DEBUG_SEL              0x00000160    /* PI Debug Select        */



#define    PI_INT_PEND_MOD_ALIAS     0x00000180    /*
                                                    * Interrupt Pending
                                                    * Modify
                                                    */



#define    PI_PERF_CNTL_A            0x00040200    /*
                                                    * Performance Counter
                                                    * Control A
                                                    */



#define    PI_PERF_CNTR0_A           0x00040208    /*
                                                    * Performance Counter
                                                    * 0 A
                                                    */



#define    PI_PERF_CNTR1_A           0x00040210    /*
                                                    * Performance Counter
                                                    * 1 A
                                                    */



#define    PI_PERF_CNTL_B            0x00050200    /*
                                                    * Performance Counter
                                                    * Control B
                                                    */



#define    PI_PERF_CNTR0_B           0x00050208    /*
                                                    * Performance Counter
                                                    * 0 B
                                                    */



#define    PI_PERF_CNTR1_B           0x00050210    /*
                                                    * Performance Counter
                                                    * 1 B
                                                    */



#define    PI_GFX_PAGE_A             0x00000300    /* Graphics Page          */



#define    PI_GFX_CREDIT_CNTR_A      0x00000308    /*
                                                    * Graphics Credit
                                                    * Counter
                                                    */



#define    PI_GFX_BIAS_A             0x00000310    /* TRex+ BIAS             */



#define    PI_GFX_INT_CNTR_A         0x00000318    /*
                                                    * Graphics Interrupt
                                                    * Counter
                                                    */



#define    PI_GFX_INT_CMP_A          0x00000320    /*
                                                    * Graphics Interrupt
                                                    * Compare
                                                    */



#define    PI_GFX_PAGE_B             0x00000328    /* Graphics Page          */



#define    PI_GFX_CREDIT_CNTR_B      0x00000330    /*
                                                    * Graphics Credit
                                                    * Counter
                                                    */



#define    PI_GFX_BIAS_B             0x00000338    /* TRex+ BIAS             */



#define    PI_GFX_INT_CNTR_B         0x00000340    /*
                                                    * Graphics Interrupt
                                                    * Counter
                                                    */



#define    PI_GFX_INT_CMP_B          0x00000348    /*
                                                    * Graphics Interrupt
                                                    * Compare
                                                    */



#define    PI_ERR_INT_PEND_WR        0x000003F8    /*
                                                    * Error Interrupt
                                                    * Pending (Writable)
                                                    */



#define    PI_ERR_INT_PEND           0x00000400    /*
                                                    * Error Interrupt
                                                    * Pending
                                                    */



#define    PI_ERR_INT_MASK_A         0x00000408    /*
                                                    * Error Interrupt
                                                    * Mask CPU_A
                                                    */



#define    PI_ERR_INT_MASK_B         0x00000410    /*
                                                    * Error Interrupt
                                                    * Mask CPU_B
                                                    */



#define    PI_ERR_STACK_ADDR_A       0x00000418    /*
                                                    * Error Stack Address
                                                    * Pointer
                                                    */



#define    PI_ERR_STACK_ADDR_B       0x00000420    /*
                                                    * Error Stack Address
                                                    * Pointer
                                                    */



#define    PI_ERR_STACK_SIZE         0x00000428    /* Error Stack Size       */



#define    PI_ERR_STATUS0_A          0x00000430    /* Error Status 0         */



#define    PI_ERR_STATUS0_A_CLR      0x00000438    /* Error Status 0         */



#define    PI_ERR_STATUS1_A          0x00000440    /* Error Status 1         */



#define    PI_ERR_STATUS1_A_CLR      0x00000448    /* Error Status 1         */



#define    PI_ERR_STATUS0_B          0x00000450    /* Error Status 0         */



#define    PI_ERR_STATUS0_B_CLR      0x00000458    /* Error Status 0         */



#define    PI_ERR_STATUS1_B          0x00000460    /* Error Status 1         */



#define    PI_ERR_STATUS1_B_CLR      0x00000468    /* Error Status 1         */



#define    PI_SPOOL_CMP_A            0x00000470    /* Spool Compare          */



#define    PI_SPOOL_CMP_B            0x00000478    /* Spool Compare          */



#define    PI_CRB_TIMEOUT_A          0x00000480    /*
                                                    * CRB entries which
                                                    * have timed out but
                                                    * are still valid
                                                    */



#define    PI_CRB_TIMEOUT_B          0x00000488    /*
                                                    * CRB entries which
                                                    * have timed out but
                                                    * are still valid
                                                    */



#define    PI_SYSAD_ERRCHK_EN        0x00000490    /*
                                                    * enables
                                                    * sysad/cmd/state
                                                    * error checking
                                                    */



#define    PI_FORCE_BAD_CHECK_BIT_A  0x00000498    /*
                                                    * force SysAD Check
                                                    * Bit error
                                                    */



#define    PI_FORCE_BAD_CHECK_BIT_B  0x000004A0    /*
                                                    * force SysAD Check
                                                    * Bit error
                                                    */



#define    PI_NACK_CNT_A             0x000004A8    /*
                                                    * consecutive NACK
                                                    * counter
                                                    */



#define    PI_NACK_CNT_B             0x000004B0    /*
                                                    * consecutive NACK
                                                    * counter
                                                    */



#define    PI_NACK_CMP               0x000004B8    /* NACK count compare     */



#define    PI_SPOOL_MASK             0x000004C0    /* Spool error mask       */



#define    PI_SPURIOUS_HDR_0         0x000004C8    /* Spurious Error 0       */



#define    PI_SPURIOUS_HDR_1         0x000004D0    /* Spurious Error 1       */



#define    PI_ERR_INJECT             0x000004D8    /*
                                                    * SysAD bus error
                                                    * injection
                                                    */





#ifdef _LANGUAGE_C

/************************************************************************
 *                                                                      *
 * Description:  This read/write register determines on a               *
 * bit-per-region basis whether incoming CPU-initiated PIO Read and     *
 * Write to local PI registers are allowed. If access is allowed, the   *
 * PI's response to a partial read is a PRPLY message, and the          *
 * response to a partial write is a PACK message. If access is not      *
 * allowed, the PI's response to a partial read is a PRERR message,     *
 * and the response to a partial write is a PWERR message.              *
 * This register is not reset by a soft reset.                          *
 *                                                                      *
 ************************************************************************/




typedef union pi_cpu_protect_u {
	bdrkreg_t	pi_cpu_protect_regval;
	struct  {
		bdrkreg_t	cp_cpu_protect            :	64;
	} pi_cpu_protect_fld_s;
} pi_cpu_protect_u_t;




/************************************************************************
 *                                                                      *
 *  A write with a special data pattern allows any CPU to set its       *
 * region's bit in CPU_PROTECT. This register has data pattern          *
 * protection.                                                          *
 *                                                                      *
 ************************************************************************/




typedef union pi_prot_ovrrd_u {
	bdrkreg_t	pi_prot_ovrrd_regval;
	struct  {
		bdrkreg_t	po_prot_ovrrd             :	64;
	} pi_prot_ovrrd_fld_s;
} pi_prot_ovrrd_u_t;




/************************************************************************
 *                                                                      *
 * Description:  This read/write register determines on a               *
 * bit-per-region basis whether incoming IO-initiated interrupts are    *
 * allowed to set bits in INT_PEND0 and INT_PEND1. If access is         *
 * allowed, the PI's response to a partial read is a PRPLY message,     *
 * and the response to a partial write is a PACK message. If access     *
 * is not allowed, the PI's response to a partial read is a PRERR       *
 * message, and the response to a partial write is a PWERR message.     *
 * This register is not reset by a soft reset.                          *
 *                                                                      *
 ************************************************************************/




typedef union pi_io_protect_u {
	bdrkreg_t	pi_io_protect_regval;
	struct  {
		bdrkreg_t	ip_io_protect             :	64;
	} pi_io_protect_fld_s;
} pi_io_protect_u_t;




/************************************************************************
 *                                                                      *
 * Description:  This read/write register determines on a               *
 * bit-per-region basis whether read access from a local processor to   *
 * the region is permissible. For example, setting a bit to 0           *
 * prevents speculative reads to that non-existent node. If a read      *
 * request to a non-present region occurs, an ERR response is issued    *
 * to the TRex+ (no PI error registers are modified). It is up to       *
 * software to load this register with the proper contents.             *
 * Region-present checking is only done for coherent read requests -    *
 * partial reads/writes will be issued to a non-present region. The     *
 * setting of these bits does not affect a node's access to its         *
 * CALIAS space.                                                        *
 * This register is not reset by a soft reset.                          *
 *                                                                      *
 ************************************************************************/




typedef union pi_region_present_u {
	bdrkreg_t	pi_region_present_regval;
	struct  {
		bdrkreg_t	rp_region_present         :	64;
	} pi_region_present_fld_s;
} pi_region_present_u_t;




/************************************************************************
 *                                                                      *
 *  A read to the location will allow a CPU to identify itself as       *
 * either CPU_A or CPU_B, and will indicate whether the CPU is          *
 * connected to PI 0 or PI 1.                                           *
 *                                                                      *
 ************************************************************************/




#ifdef LITTLE_ENDIAN

typedef union pi_cpu_num_u {
	bdrkreg_t	pi_cpu_num_regval;
	struct  {
		bdrkreg_t	cn_cpu_num                :	 1;
                bdrkreg_t       cn_pi_id                  :      1;
                bdrkreg_t       cn_rsvd                   :     62;
	} pi_cpu_num_fld_s;
} pi_cpu_num_u_t;

#else

typedef union pi_cpu_num_u {
	bdrkreg_t	pi_cpu_num_regval;
	struct	{
		bdrkreg_t	cn_rsvd			  :	62;
		bdrkreg_t	cn_pi_id		  :	 1;
		bdrkreg_t	cn_cpu_num		  :	 1;
	} pi_cpu_num_fld_s;
} pi_cpu_num_u_t;

#endif




/************************************************************************
 *                                                                      *
 * Description:  This read/write location determines the size of the    *
 * Calias Space.                                                        *
 * This register is not reset by a soft reset.                          *
 * NOTE: For predictable behavior, all Calias spaces in a system must   *
 * be set to the same size.                                             *
 *                                                                      *
 ************************************************************************/




#ifdef LITTLE_ENDIAN

typedef union pi_calias_size_u {
	bdrkreg_t	pi_calias_size_regval;
	struct  {
		bdrkreg_t	cs_calias_size            :	 4;
		bdrkreg_t       cs_rsvd                   :     60;
	} pi_calias_size_fld_s;
} pi_calias_size_u_t;

#else

typedef union pi_calias_size_u {
	bdrkreg_t	pi_calias_size_regval;
	struct	{
		bdrkreg_t	cs_rsvd			  :	60;
		bdrkreg_t	cs_calias_size		  :	 4;
	} pi_calias_size_fld_s;
} pi_calias_size_u_t;

#endif




/************************************************************************
 *                                                                      *
 *  This Read/Write location determines at which value (increment)      *
 * the CRB Timeout Counters cause a timeout error to occur. See         *
 * Section 3.4.2.2, &quot;Time-outs in RRB and WRB&quot; in the         *
 * Processor Interface chapter, volume 1 of this document for more      *
 * details.                                                             *
 *                                                                      *
 ************************************************************************/




#ifdef LITTLE_ENDIAN

typedef union pi_max_crb_timeout_u {
	bdrkreg_t	pi_max_crb_timeout_regval;
	struct  {
		bdrkreg_t	mct_max_timeout           :	 8;
		bdrkreg_t       mct_rsvd                  :     56;
	} pi_max_crb_timeout_fld_s;
} pi_max_crb_timeout_u_t;

#else

typedef union pi_max_crb_timeout_u {
	bdrkreg_t	pi_max_crb_timeout_regval;
	struct	{
		bdrkreg_t	mct_rsvd		  :	56;
		bdrkreg_t	mct_max_timeout		  :	 8;
	} pi_max_crb_timeout_fld_s;
} pi_max_crb_timeout_u_t;

#endif




/************************************************************************
 *                                                                      *
 *  This Read/Write location determines how often a valid CRB's         *
 * Timeout Counter is incremented. See Section 3.4.2.2,                 *
 * &quot;Time-outs in RRB and WRB&quot; in the Processor Interface      *
 * chapter, volume 1 of this document for more details.                 *
 *                                                                      *
 ************************************************************************/




#ifdef LITTLE_ENDIAN

typedef union pi_crb_sfactor_u {
	bdrkreg_t	pi_crb_sfactor_regval;
	struct  {
		bdrkreg_t	cs_sfactor                :	24;
		bdrkreg_t       cs_rsvd                   :     40;
	} pi_crb_sfactor_fld_s;
} pi_crb_sfactor_u_t;

#else

typedef union pi_crb_sfactor_u {
	bdrkreg_t	pi_crb_sfactor_regval;
	struct	{
		bdrkreg_t	cs_rsvd			  :	40;
		bdrkreg_t	cs_sfactor		  :	24;
	} pi_crb_sfactor_fld_s;
} pi_crb_sfactor_u_t;

#endif




/************************************************************************
 *                                                                      *
 *  There is one of these registers for each CPU. The PI sets this      *
 * bit when it sees the first transaction initiated by the associated   *
 * CPU.                                                                 *
 *                                                                      *
 ************************************************************************/




#ifdef LITTLE_ENDIAN

typedef union pi_cpu_present_a_u {
	bdrkreg_t	pi_cpu_present_a_regval;
	struct  {
		bdrkreg_t	cpa_cpu_present           :	 1;
		bdrkreg_t       cpa_rsvd                  :     63;
	} pi_cpu_present_a_fld_s;
} pi_cpu_present_a_u_t;

#else

typedef union pi_cpu_present_a_u {
	bdrkreg_t	pi_cpu_present_a_regval;
	struct	{
		bdrkreg_t	cpa_rsvd		  :	63;
		bdrkreg_t	cpa_cpu_present		  :	 1;
	} pi_cpu_present_a_fld_s;
} pi_cpu_present_a_u_t;

#endif




/************************************************************************
 *                                                                      *
 *  There is one of these registers for each CPU. The PI sets this      *
 * bit when it sees the first transaction initiated by the associated   *
 * CPU.                                                                 *
 *                                                                      *
 ************************************************************************/




#ifdef LITTLE_ENDIAN

typedef union pi_cpu_present_b_u {
	bdrkreg_t	pi_cpu_present_b_regval;
	struct  {
		bdrkreg_t	cpb_cpu_present           :	 1;
		bdrkreg_t       cpb_rsvd                  :     63;
	} pi_cpu_present_b_fld_s;
} pi_cpu_present_b_u_t;

#else

typedef union pi_cpu_present_b_u {
	bdrkreg_t	pi_cpu_present_b_regval;
	struct	{
		bdrkreg_t	cpb_rsvd		  :	63;
		bdrkreg_t	cpb_cpu_present		  :	 1;
	} pi_cpu_present_b_fld_s;
} pi_cpu_present_b_u_t;

#endif




/************************************************************************
 *                                                                      *
 * Description:  There is one of these registers for each CPU. This     *
 * Read/Write location determines whether the associated CPU is         *
 * enabled to issue external requests. When this bit is zero for a      *
 * processor, the PI ignores SysReq_L from that processor, and so       *
 * never grants it the bus.                                             *
 * This register is not reset by a soft reset.                          *
 *                                                                      *
 ************************************************************************/




#ifdef LITTLE_ENDIAN

typedef union pi_cpu_enable_a_u {
	bdrkreg_t	pi_cpu_enable_a_regval;
	struct  {
		bdrkreg_t	cea_cpu_enable            :	 1;
		bdrkreg_t       cea_rsvd                  :     63;
	} pi_cpu_enable_a_fld_s;
} pi_cpu_enable_a_u_t;

#else

typedef union pi_cpu_enable_a_u {
	bdrkreg_t	pi_cpu_enable_a_regval;
	struct	{
		bdrkreg_t	cea_rsvd		  :	63;
		bdrkreg_t	cea_cpu_enable		  :	 1;
	} pi_cpu_enable_a_fld_s;
} pi_cpu_enable_a_u_t;

#endif




/************************************************************************
 *                                                                      *
 * Description:  There is one of these registers for each CPU. This     *
 * Read/Write location determines whether the associated CPU is         *
 * enabled to issue external requests. When this bit is zero for a      *
 * processor, the PI ignores SysReq_L from that processor, and so       *
 * never grants it the bus.                                             *
 * This register is not reset by a soft reset.                          *
 *                                                                      *
 ************************************************************************/




#ifdef LITTLE_ENDIAN

typedef union pi_cpu_enable_b_u {
	bdrkreg_t	pi_cpu_enable_b_regval;
	struct  {
		bdrkreg_t	ceb_cpu_enable            :	 1;
		bdrkreg_t       ceb_rsvd                  :     63;
	} pi_cpu_enable_b_fld_s;
} pi_cpu_enable_b_u_t;

#else

typedef union pi_cpu_enable_b_u {
	bdrkreg_t	pi_cpu_enable_b_regval;
	struct	{
		bdrkreg_t	ceb_rsvd		  :	63;
		bdrkreg_t	ceb_cpu_enable		  :	 1;
	} pi_cpu_enable_b_fld_s;
} pi_cpu_enable_b_u_t;

#endif




/************************************************************************
 *                                                                      *
 *  There is one of these registers for each CPU. A write to this       *
 * location will cause an NMI to be issued to the CPU.                  *
 *                                                                      *
 ************************************************************************/




typedef union pi_nmi_a_u {
	bdrkreg_t	pi_nmi_a_regval;
	struct  {
		bdrkreg_t	na_nmi_cpu                :	64;
	} pi_nmi_a_fld_s;
} pi_nmi_a_u_t;




/************************************************************************
 *                                                                      *
 *  There is one of these registers for each CPU. A write to this       *
 * location will cause an NMI to be issued to the CPU.                  *
 *                                                                      *
 ************************************************************************/




typedef union pi_nmi_b_u {
	bdrkreg_t	pi_nmi_b_regval;
	struct  {
		bdrkreg_t	nb_nmi_cpu                :	64;
	} pi_nmi_b_fld_s;
} pi_nmi_b_u_t;




/************************************************************************
 *                                                                      *
 *  A write to this register allows a single bit in the INT_PEND0 or    *
 * INT_PEND1 registers to be set or cleared. If 6 is clear, a bit is    *
 * modified in INT_PEND0, while if 6 is set, a bit is modified in       *
 * INT_PEND1. The value in 5:0 (ranging from 63 to 0) will determine    *
 * which bit in the register is effected. The value of 8 will           *
 * determine whether the desired bit is set (8=1) or cleared (8=0).     *
 * This is the only register which is accessible by IO issued PWRI      *
 * command and is protected through the IO_PROTECT register. If the     *
 * region bit in the IO_PROTECT is not set then a WERR reply is         *
 * issued. CPU access is controlled through CPU_PROTECT. The contents   *
 * of this register are masked with the contents of INT_MASK_A          *
 * (INT_MASK_B) to determine whether an L2 interrupt is issued to       *
 * CPU_A (CPU_B).                                                       *
 *                                                                      *
 ************************************************************************/




#ifdef LITTLE_ENDIAN

typedef union pi_int_pend_mod_u {
	bdrkreg_t	pi_int_pend_mod_regval;
	struct  {
		bdrkreg_t	ipm_bit_select            :	 6;
                bdrkreg_t       ipm_reg_select            :      1;
                bdrkreg_t       ipm_rsvd_1                :      1;
                bdrkreg_t       ipm_value                 :      1;
                bdrkreg_t       ipm_rsvd                  :     55;
	} pi_int_pend_mod_fld_s;
} pi_int_pend_mod_u_t;

#else

typedef union pi_int_pend_mod_u {
	bdrkreg_t	pi_int_pend_mod_regval;
	struct	{
		bdrkreg_t	ipm_rsvd		  :	55;
		bdrkreg_t	ipm_value		  :	 1;
		bdrkreg_t	ipm_rsvd_1		  :	 1;
		bdrkreg_t	ipm_reg_select		  :	 1;
		bdrkreg_t	ipm_bit_select		  :	 6;
	} pi_int_pend_mod_fld_s;
} pi_int_pend_mod_u_t;

#endif




/************************************************************************
 *                                                                      *
 *  This read-only register provides information about interrupts       *
 * that are currently pending. The interrupts in this register map to   *
 * interrupt level 2 (L2). The GFX_INT_A/B bits are set by hardware     *
 * but must be cleared by software.                                     *
 *                                                                      *
 ************************************************************************/




#ifdef LITTLE_ENDIAN

typedef union pi_int_pend0_u {
	bdrkreg_t	pi_int_pend0_regval;
	struct  {
		bdrkreg_t	ip_int_pend0_lo           :	 1;
                bdrkreg_t       ip_gfx_int_a              :      1;
                bdrkreg_t       ip_gfx_int_b              :      1;
                bdrkreg_t       ip_page_migration         :      1;
                bdrkreg_t       ip_uart_ucntrl            :      1;
                bdrkreg_t       ip_or_cc_pend_a           :      1;
                bdrkreg_t       ip_or_cc_pend_b           :      1;
                bdrkreg_t       ip_int_pend0_hi           :     57;
	} pi_int_pend0_fld_s;
} pi_int_pend0_u_t;

#else

typedef union pi_int_pend0_u {
	bdrkreg_t	pi_int_pend0_regval;
	struct	{
		bdrkreg_t	ip_int_pend0_hi		  :	57;
		bdrkreg_t	ip_or_cc_pend_b		  :	 1;
		bdrkreg_t	ip_or_cc_pend_a		  :	 1;
		bdrkreg_t	ip_uart_ucntrl		  :	 1;
		bdrkreg_t	ip_page_migration	  :	 1;
		bdrkreg_t	ip_gfx_int_b		  :	 1;
		bdrkreg_t	ip_gfx_int_a		  :	 1;
		bdrkreg_t	ip_int_pend0_lo		  :	 1;
	} pi_int_pend0_fld_s;
} pi_int_pend0_u_t;

#endif




/************************************************************************
 *                                                                      *
 *  This read-only register provides information about interrupts       *
 * that are currently pending. The interrupts in this register map to   *
 * interrupt level 3 (L3), unless remapped by the INT_PEND1_REMAP       *
 * register. The SYS_COR_ERR_A/B, RTC_DROP_OUT, and NACK_INT_A/B bits   *
 * are set by hardware but must be cleared by software. The             *
 * SYSTEM_SHUTDOWN, NI_ERROR, LB_ERROR and XB_ERROR bits just reflect   *
 * the value of other logic, and cannot be changed by PI register       *
 * writes.                                                              *
 *                                                                      *
 ************************************************************************/




#ifdef LITTLE_ENDIAN

typedef union pi_int_pend1_u {
	bdrkreg_t	pi_int_pend1_regval;
	struct  {
		bdrkreg_t	ip_int_pend1              :	54;
                bdrkreg_t       ip_xb_error               :      1;
                bdrkreg_t       ip_lb_error               :      1;
                bdrkreg_t       ip_nack_int_a             :      1;
                bdrkreg_t       ip_nack_int_b             :      1;
                bdrkreg_t       ip_perf_cntr_oflow        :      1;
                bdrkreg_t       ip_sys_cor_err_b          :      1;
                bdrkreg_t       ip_sys_cor_err_a          :      1;
                bdrkreg_t       ip_md_corr_error          :      1;
                bdrkreg_t       ip_ni_error               :      1;
                bdrkreg_t       ip_system_shutdown        :      1;
	} pi_int_pend1_fld_s;
} pi_int_pend1_u_t;

#else

typedef union pi_int_pend1_u {
	bdrkreg_t	pi_int_pend1_regval;
	struct	{
		bdrkreg_t	ip_system_shutdown	  :	 1;
		bdrkreg_t	ip_ni_error		  :	 1;
		bdrkreg_t	ip_md_corr_error	  :	 1;
		bdrkreg_t	ip_sys_cor_err_a	  :	 1;
		bdrkreg_t	ip_sys_cor_err_b	  :	 1;
		bdrkreg_t	ip_perf_cntr_oflow	  :	 1;
		bdrkreg_t	ip_nack_int_b		  :	 1;
		bdrkreg_t	ip_nack_int_a		  :	 1;
		bdrkreg_t	ip_lb_error		  :	 1;
		bdrkreg_t	ip_xb_error		  :	 1;
		bdrkreg_t	ip_int_pend1		  :	54;
	} pi_int_pend1_fld_s;
} pi_int_pend1_u_t;

#endif




/************************************************************************
 *                                                                      *
 *  This read/write register masks the contents of INT_PEND0 to         *
 * determine whether an L2 interrupt (bit 10 of the processor's Cause   *
 * register) is sent to CPU_A if the same bit in the INT_PEND0          *
 * register is also set. Only one processor in a Bedrock should         *
 * enable the PAGE_MIGRATION bit/interrupt.                             *
 *                                                                      *
 ************************************************************************/




#ifdef LITTLE_ENDIAN

typedef union pi_int_mask0_a_u {
	bdrkreg_t	pi_int_mask0_a_regval;
	struct  {
		bdrkreg_t	ima_int_mask0_lo          :	 1;
                bdrkreg_t       ima_gfx_int_a             :      1;
                bdrkreg_t       ima_gfx_int_b             :      1;
                bdrkreg_t       ima_page_migration        :      1;
                bdrkreg_t       ima_uart_ucntrl           :      1;
                bdrkreg_t       ima_or_ccp_mask_a         :      1;
                bdrkreg_t       ima_or_ccp_mask_b         :      1;
                bdrkreg_t       ima_int_mask0_hi          :     57;
	} pi_int_mask0_a_fld_s;
} pi_int_mask0_a_u_t;

#else

typedef union pi_int_mask0_a_u {
	bdrkreg_t	pi_int_mask0_a_regval;
	struct	{
		bdrkreg_t	ima_int_mask0_hi	  :	57;
		bdrkreg_t	ima_or_ccp_mask_b	  :	 1;
		bdrkreg_t	ima_or_ccp_mask_a	  :	 1;
		bdrkreg_t	ima_uart_ucntrl		  :	 1;
		bdrkreg_t	ima_page_migration	  :	 1;
		bdrkreg_t	ima_gfx_int_b		  :	 1;
		bdrkreg_t	ima_gfx_int_a		  :	 1;
		bdrkreg_t	ima_int_mask0_lo	  :	 1;
	} pi_int_mask0_a_fld_s;
} pi_int_mask0_a_u_t;

#endif




/************************************************************************
 *                                                                      *
 *  This read/write register masks the contents of INT_PEND1 to         *
 * determine whether an interrupt should be sent. Bits 63:32 always     *
 * generate an L3 interrupt (bit 11 of the processor's Cause            *
 * register) is sent to CPU_A if the same bit in the INT_PEND1          *
 * register is set. Bits 31:0 can generate either an L3 or L2           *
 * interrupt, depending on the value of INT_PEND1_REMAP[3:0]. Only      *
 * one processor in a Bedrock should enable the NI_ERROR, LB_ERROR,     *
 * XB_ERROR and MD_CORR_ERROR bits.                                     *
 *                                                                      *
 ************************************************************************/




typedef union pi_int_mask1_a_u {
	bdrkreg_t	pi_int_mask1_a_regval;
	struct  {
		bdrkreg_t	ima_int_mask1             :	64;
	} pi_int_mask1_a_fld_s;
} pi_int_mask1_a_u_t;




/************************************************************************
 *                                                                      *
 *  This read/write register masks the contents of INT_PEND0 to         *
 * determine whether an L2 interrupt (bit 10 of the processor's Cause   *
 * register) is sent to CPU_B if the same bit in the INT_PEND0          *
 * register is also set. Only one processor in a Bedrock should         *
 * enable the PAGE_MIGRATION bit/interrupt.                             *
 *                                                                      *
 ************************************************************************/




#ifdef LITTLE_ENDIAN

typedef union pi_int_mask0_b_u {
	bdrkreg_t	pi_int_mask0_b_regval;
	struct  {
		bdrkreg_t	imb_int_mask0_lo          :	 1;
                bdrkreg_t       imb_gfx_int_a             :      1;
                bdrkreg_t       imb_gfx_int_b             :      1;
                bdrkreg_t       imb_page_migration        :      1;
                bdrkreg_t       imb_uart_ucntrl           :      1;
                bdrkreg_t       imb_or_ccp_mask_a         :      1;
                bdrkreg_t       imb_or_ccp_mask_b         :      1;
                bdrkreg_t       imb_int_mask0_hi          :     57;
	} pi_int_mask0_b_fld_s;
} pi_int_mask0_b_u_t;

#else

typedef union pi_int_mask0_b_u {
	bdrkreg_t	pi_int_mask0_b_regval;
	struct	{
		bdrkreg_t	imb_int_mask0_hi	  :	57;
		bdrkreg_t	imb_or_ccp_mask_b	  :	 1;
		bdrkreg_t	imb_or_ccp_mask_a	  :	 1;
		bdrkreg_t	imb_uart_ucntrl		  :	 1;
		bdrkreg_t	imb_page_migration	  :	 1;
		bdrkreg_t	imb_gfx_int_b		  :	 1;
		bdrkreg_t	imb_gfx_int_a		  :	 1;
		bdrkreg_t	imb_int_mask0_lo	  :	 1;
	} pi_int_mask0_b_fld_s;
} pi_int_mask0_b_u_t;

#endif




/************************************************************************
 *                                                                      *
 *  This read/write register masks the contents of INT_PEND1 to         *
 * determine whether an interrupt should be sent. Bits 63:32 always     *
 * generate an L3 interrupt (bit 11 of the processor's Cause            *
 * register) is sent to CPU_B if the same bit in the INT_PEND1          *
 * register is set. Bits 31:0 can generate either an L3 or L2           *
 * interrupt, depending on the value of INT_PEND1_REMAP[3:0]. Only      *
 * one processor in a Bedrock should enable the NI_ERROR, LB_ERROR,     *
 * XB_ERROR and MD_CORR_ERROR bits.                                     *
 *                                                                      *
 ************************************************************************/




typedef union pi_int_mask1_b_u {
	bdrkreg_t	pi_int_mask1_b_regval;
	struct  {
		bdrkreg_t	imb_int_mask1             :	64;
	} pi_int_mask1_b_fld_s;
} pi_int_mask1_b_u_t;




/************************************************************************
 *                                                                      *
 *  There is one of these registers for each CPU. These registers do    *
 * not have access protection. A store to this location by a CPU will   *
 * cause the bit corresponding to the source's region to be set in      *
 * CC_PEND_A (or CC_PEND_B). The contents of CC_PEND_A (or CC_PEND_B)   *
 * determines on a bit-per-region basis whether a CPU-to-CPU            *
 * interrupt is pending CPU_A (or CPU_B).                               *
 *                                                                      *
 ************************************************************************/




typedef union pi_cc_pend_set_a_u {
	bdrkreg_t	pi_cc_pend_set_a_regval;
	struct  {
		bdrkreg_t	cpsa_cc_pend              :	64;
	} pi_cc_pend_set_a_fld_s;
} pi_cc_pend_set_a_u_t;




/************************************************************************
 *                                                                      *
 *  There is one of these registers for each CPU. These registers do    *
 * not have access protection. A store to this location by a CPU will   *
 * cause the bit corresponding to the source's region to be set in      *
 * CC_PEND_A (or CC_PEND_B). The contents of CC_PEND_A (or CC_PEND_B)   *
 * determines on a bit-per-region basis whether a CPU-to-CPU            *
 * interrupt is pending CPU_A (or CPU_B).                               *
 *                                                                      *
 ************************************************************************/




typedef union pi_cc_pend_set_b_u {
	bdrkreg_t	pi_cc_pend_set_b_regval;
	struct  {
		bdrkreg_t	cpsb_cc_pend              :	64;
	} pi_cc_pend_set_b_fld_s;
} pi_cc_pend_set_b_u_t;




/************************************************************************
 *                                                                      *
 *  There is one of these registers for each CPU. Reading this          *
 * location will return the contents of CC_PEND_A (or CC_PEND_B).       *
 * Writing this location will clear the bits corresponding to which     *
 * data bits are driven high during the store; therefore, storing all   *
 * ones would clear all bits.                                           *
 *                                                                      *
 ************************************************************************/




typedef union pi_cc_pend_clr_a_u {
	bdrkreg_t	pi_cc_pend_clr_a_regval;
	struct  {
		bdrkreg_t	cpca_cc_pend              :	64;
	} pi_cc_pend_clr_a_fld_s;
} pi_cc_pend_clr_a_u_t;




/************************************************************************
 *                                                                      *
 *  There is one of these registers for each CPU. Reading this          *
 * location will return the contents of CC_PEND_A (or CC_PEND_B).       *
 * Writing this location will clear the bits corresponding to which     *
 * data bits are driven high during the store; therefore, storing all   *
 * ones would clear all bits.                                           *
 *                                                                      *
 ************************************************************************/




typedef union pi_cc_pend_clr_b_u {
	bdrkreg_t	pi_cc_pend_clr_b_regval;
	struct  {
		bdrkreg_t	cpcb_cc_pend              :	64;
	} pi_cc_pend_clr_b_fld_s;
} pi_cc_pend_clr_b_u_t;




/************************************************************************
 *                                                                      *
 *  This read/write register masks the contents of both CC_PEND_A and   *
 * CC_PEND_B.                                                           *
 *                                                                      *
 ************************************************************************/




typedef union pi_cc_mask_u {
	bdrkreg_t	pi_cc_mask_regval;
	struct  {
		bdrkreg_t	cm_cc_mask                :	64;
	} pi_cc_mask_fld_s;
} pi_cc_mask_u_t;




/************************************************************************
 *                                                                      *
 *  This read/write register redirects INT_PEND1[31:0] from L3 to L2    *
 * interrupt level.Bit 4 in this register is used to enable error       *
 * interrupt forwarding to the II. When this bit is set, if any of      *
 * the three memory interrupts (correctable error, uncorrectable        *
 * error, or page migration), or the NI, LB or XB error interrupts      *
 * are set, the PI_II_ERROR_INT wire will be asserted. When this wire   *
 * is asserted, the II will send an interrupt to the node specified     *
 * in its IIDSR (Interrupt Destination Register). This allows these     *
 * interrupts to be forwarded to another node.                          *
 *                                                                      *
 ************************************************************************/




#ifdef LITTLE_ENDIAN

typedef union pi_int_pend1_remap_u {
	bdrkreg_t	pi_int_pend1_remap_regval;
	struct  {
		bdrkreg_t	ipr_remap_0               :	 1;
                bdrkreg_t       ipr_remap_1               :      1;
                bdrkreg_t       ipr_remap_2               :      1;
                bdrkreg_t       ipr_remap_3               :      1;
                bdrkreg_t       ipr_error_forward         :      1;
                bdrkreg_t       ipr_reserved              :     59;
	} pi_int_pend1_remap_fld_s;
} pi_int_pend1_remap_u_t;

#else

typedef union pi_int_pend1_remap_u {
	bdrkreg_t	pi_int_pend1_remap_regval;
	struct	{
		bdrkreg_t	ipr_reserved		  :	59;
		bdrkreg_t	ipr_error_forward	  :	 1;
		bdrkreg_t	ipr_remap_3		  :	 1;
		bdrkreg_t	ipr_remap_2		  :	 1;
		bdrkreg_t	ipr_remap_1		  :	 1;
		bdrkreg_t	ipr_remap_0		  :	 1;
	} pi_int_pend1_remap_fld_s;
} pi_int_pend1_remap_u_t;

#endif




/************************************************************************
 *                                                                      *
 *  There is one of these registers for each CPU. When the real time    *
 * counter (RT_Counter) is equal to the value in this register, the     *
 * RT_INT_PEND register is set, which causes a Level-4 interrupt to     *
 * be sent to the processor.                                            *
 *                                                                      *
 ************************************************************************/




#ifdef LITTLE_ENDIAN

typedef union pi_rt_compare_a_u {
	bdrkreg_t	pi_rt_compare_a_regval;
	struct  {
		bdrkreg_t	rca_rt_compare            :	55;
		bdrkreg_t       rca_rsvd                  :      9;
	} pi_rt_compare_a_fld_s;
} pi_rt_compare_a_u_t;

#else

typedef union pi_rt_compare_a_u {
        bdrkreg_t       pi_rt_compare_a_regval;
        struct  {
                bdrkreg_t       rca_rsvd                  :      9;
                bdrkreg_t       rca_rt_compare            :     55;
        } pi_rt_compare_a_fld_s;
} pi_rt_compare_a_u_t;

#endif




/************************************************************************
 *                                                                      *
 *  There is one of these registers for each CPU. When the real time    *
 * counter (RT_Counter) is equal to the value in this register, the     *
 * RT_INT_PEND register is set, which causes a Level-4 interrupt to     *
 * be sent to the processor.                                            *
 *                                                                      *
 ************************************************************************/




#ifdef LITTLE_ENDIAN

typedef union pi_rt_compare_b_u {
	bdrkreg_t	pi_rt_compare_b_regval;
	struct  {
		bdrkreg_t	rcb_rt_compare            :	55;
		bdrkreg_t       rcb_rsvd                  :      9;
	} pi_rt_compare_b_fld_s;
} pi_rt_compare_b_u_t;

#else

typedef union pi_rt_compare_b_u {
	bdrkreg_t	pi_rt_compare_b_regval;
	struct	{
		bdrkreg_t	rcb_rsvd		  :	 9;
		bdrkreg_t	rcb_rt_compare		  :	55;
	} pi_rt_compare_b_fld_s;
} pi_rt_compare_b_u_t;

#endif




/************************************************************************
 *                                                                      *
 *  When the least significant 32 bits of the real time counter         *
 * (RT_Counter) are equal to the value in this register, the            *
 * PROF_INT_PEND_A and PROF_INT_PEND_B registers are set to 0x1.        *
 *                                                                      *
 ************************************************************************/




#ifdef LITTLE_ENDIAN

typedef union pi_profile_compare_u {
	bdrkreg_t	pi_profile_compare_regval;
	struct  {
		bdrkreg_t	pc_profile_compare        :	32;
		bdrkreg_t       pc_rsvd                   :     32;
	} pi_profile_compare_fld_s;
} pi_profile_compare_u_t;

#else

typedef union pi_profile_compare_u {
	bdrkreg_t	pi_profile_compare_regval;
	struct	{
		bdrkreg_t	pc_rsvd			  :	32;
		bdrkreg_t	pc_profile_compare	  :	32;
	} pi_profile_compare_fld_s;
} pi_profile_compare_u_t;

#endif




/************************************************************************
 *                                                                      *
 *  There is one of these registers for each CPU. If the bit in the     *
 * corresponding RT_INT_EN_A/B register is set, the processor's level   *
 * 5 interrupt is set to the value of the RTC_INT_PEND bit in this      *
 * register. Storing any value to this location will clear the          *
 * RTC_INT_PEND bit in the register.                                    *
 *                                                                      *
 ************************************************************************/




#ifdef LITTLE_ENDIAN

typedef union pi_rt_int_pend_a_u {
	bdrkreg_t	pi_rt_int_pend_a_regval;
	struct  {
		bdrkreg_t	ripa_rtc_int_pend         :	 1;
		bdrkreg_t       ripa_rsvd                 :     63;
	} pi_rt_int_pend_a_fld_s;
} pi_rt_int_pend_a_u_t;

#else

typedef union pi_rt_int_pend_a_u {
	bdrkreg_t	pi_rt_int_pend_a_regval;
	struct	{
		bdrkreg_t	ripa_rsvd		  :	63;
		bdrkreg_t	ripa_rtc_int_pend	  :	 1;
	} pi_rt_int_pend_a_fld_s;
} pi_rt_int_pend_a_u_t;

#endif




/************************************************************************
 *                                                                      *
 *  There is one of these registers for each CPU. If the bit in the     *
 * corresponding RT_INT_EN_A/B register is set, the processor's level   *
 * 5 interrupt is set to the value of the RTC_INT_PEND bit in this      *
 * register. Storing any value to this location will clear the          *
 * RTC_INT_PEND bit in the register.                                    *
 *                                                                      *
 ************************************************************************/




#ifdef LITTLE_ENDIAN

typedef union pi_rt_int_pend_b_u {
	bdrkreg_t	pi_rt_int_pend_b_regval;
	struct  {
		bdrkreg_t	ripb_rtc_int_pend         :	 1;
		bdrkreg_t       ripb_rsvd                 :     63;
	} pi_rt_int_pend_b_fld_s;
} pi_rt_int_pend_b_u_t;

#else

typedef union pi_rt_int_pend_b_u {
	bdrkreg_t	pi_rt_int_pend_b_regval;
	struct	{
		bdrkreg_t	ripb_rsvd		  :	63;
		bdrkreg_t	ripb_rtc_int_pend	  :	 1;
	} pi_rt_int_pend_b_fld_s;
} pi_rt_int_pend_b_u_t;

#endif




/************************************************************************
 *                                                                      *
 *  There is one of these registers for each CPU. Both registers are    *
 * set when the PROFILE_COMPARE register is equal to bits [31:0] of     *
 * the RT_Counter. If the bit in the corresponding PROF_INT_EN_A/B      *
 * register is set, the processor's level 5 interrupt is set to the     *
 * value of the PROF_INT_PEND bit in this register. Storing any value   *
 * to this location will clear the PROF_INT_PEND bit in the register.   *
 * The reason for having A and B versions of this register is that      *
 * they need to be cleared independently.                               *
 *                                                                      *
 ************************************************************************/




#ifdef LITTLE_ENDIAN

typedef union pi_prof_int_pend_a_u {
	bdrkreg_t	pi_prof_int_pend_a_regval;
	struct  {
		bdrkreg_t	pipa_prof_int_pend        :	 1;
		bdrkreg_t       pipa_rsvd                 :     63;
	} pi_prof_int_pend_a_fld_s;
} pi_prof_int_pend_a_u_t;

#else

typedef union pi_prof_int_pend_a_u {
	bdrkreg_t	pi_prof_int_pend_a_regval;
	struct	{
		bdrkreg_t	pipa_rsvd		  :	63;
		bdrkreg_t	pipa_prof_int_pend	  :	 1;
	} pi_prof_int_pend_a_fld_s;
} pi_prof_int_pend_a_u_t;

#endif




/************************************************************************
 *                                                                      *
 *  There is one of these registers for each CPU. Both registers are    *
 * set when the PROFILE_COMPARE register is equal to bits [31:0] of     *
 * the RT_Counter. If the bit in the corresponding PROF_INT_EN_A/B      *
 * register is set, the processor's level 5 interrupt is set to the     *
 * value of the PROF_INT_PEND bit in this register. Storing any value   *
 * to this location will clear the PROF_INT_PEND bit in the register.   *
 * The reason for having A and B versions of this register is that      *
 * they need to be cleared independently.                               *
 *                                                                      *
 ************************************************************************/




#ifdef LITTLE_ENDIAN

typedef union pi_prof_int_pend_b_u {
	bdrkreg_t	pi_prof_int_pend_b_regval;
	struct  {
		bdrkreg_t	pipb_prof_int_pend        :	 1;
		bdrkreg_t       pipb_rsvd                 :     63;
	} pi_prof_int_pend_b_fld_s;
} pi_prof_int_pend_b_u_t;

#else

typedef union pi_prof_int_pend_b_u {
	bdrkreg_t	pi_prof_int_pend_b_regval;
	struct	{
		bdrkreg_t	pipb_rsvd		  :	63;
		bdrkreg_t	pipb_prof_int_pend	  :	 1;
	} pi_prof_int_pend_b_fld_s;
} pi_prof_int_pend_b_u_t;

#endif




/************************************************************************
 *                                                                      *
 *  There is one of these registers for each CPU. Enables RTC           *
 * interrupt to the associated CPU.                                     *
 *                                                                      *
 ************************************************************************/




#ifdef LITTLE_ENDIAN

typedef union pi_rt_int_en_a_u {
	bdrkreg_t	pi_rt_int_en_a_regval;
	struct  {
		bdrkreg_t	riea_rtc_int_en           :	 1;
		bdrkreg_t       riea_rsvd                 :     63;
	} pi_rt_int_en_a_fld_s;
} pi_rt_int_en_a_u_t;

#else

typedef union pi_rt_int_en_a_u {
        bdrkreg_t       pi_rt_int_en_a_regval;
        struct  {
                bdrkreg_t       riea_rsvd                 :     63;
                bdrkreg_t       riea_rtc_int_en           :      1;
        } pi_rt_int_en_a_fld_s;
} pi_rt_int_en_a_u_t;

#endif




/************************************************************************
 *                                                                      *
 *  There is one of these registers for each CPU. Enables RTC           *
 * interrupt to the associated CPU.                                     *
 *                                                                      *
 ************************************************************************/




#ifdef LITTLE_ENDIAN

typedef union pi_rt_int_en_b_u {
	bdrkreg_t	pi_rt_int_en_b_regval;
	struct  {
		bdrkreg_t	rieb_rtc_int_en           :	 1;
		bdrkreg_t       rieb_rsvd                 :     63;
	} pi_rt_int_en_b_fld_s;
} pi_rt_int_en_b_u_t;

#else

typedef union pi_rt_int_en_b_u {
        bdrkreg_t       pi_rt_int_en_b_regval;
        struct  {
                bdrkreg_t       rieb_rsvd                 :     63;
                bdrkreg_t       rieb_rtc_int_en           :      1;
        } pi_rt_int_en_b_fld_s;
} pi_rt_int_en_b_u_t;

#endif




/************************************************************************
 *                                                                      *
 *  There is one of these registers for each CPU. Enables profiling     *
 * interrupt to the associated CPU.                                     *
 *                                                                      *
 ************************************************************************/




#ifdef LITTLE_ENDIAN

typedef union pi_prof_int_en_a_u {
	bdrkreg_t	pi_prof_int_en_a_regval;
	struct  {
		bdrkreg_t	piea_prof_int_en          :	 1;
		bdrkreg_t       piea_rsvd                 :     63;
	} pi_prof_int_en_a_fld_s;
} pi_prof_int_en_a_u_t;

#else

typedef union pi_prof_int_en_a_u {
	bdrkreg_t	pi_prof_int_en_a_regval;
	struct	{
		bdrkreg_t	piea_rsvd		  :	63;
		bdrkreg_t	piea_prof_int_en	  :	 1;
	} pi_prof_int_en_a_fld_s;
} pi_prof_int_en_a_u_t;

#endif




/************************************************************************
 *                                                                      *
 *  There is one of these registers for each CPU. Enables profiling     *
 * interrupt to the associated CPU.                                     *
 *                                                                      *
 ************************************************************************/




#ifdef LITTLE_ENDIAN

typedef union pi_prof_int_en_b_u {
	bdrkreg_t	pi_prof_int_en_b_regval;
	struct  {
		bdrkreg_t	pieb_prof_int_en          :	 1;
		bdrkreg_t       pieb_rsvd                 :     63;
	} pi_prof_int_en_b_fld_s;
} pi_prof_int_en_b_u_t;

#else

typedef union pi_prof_int_en_b_u {
	bdrkreg_t	pi_prof_int_en_b_regval;
	struct	{
		bdrkreg_t	pieb_rsvd		  :	63;
		bdrkreg_t	pieb_prof_int_en	  :	 1;
	} pi_prof_int_en_b_fld_s;
} pi_prof_int_en_b_u_t;

#endif




/************************************************************************
 *                                                                      *
 *  This register controls operation of the debug data from the PI,     *
 * along with Debug_Sel[2:0] from the Debug module. For some values     *
 * of Debug_Sel[2:0], the B_SEL bit selects whether the debug bits      *
 * are looking at the processor A or processor B logic. The remaining   *
 * bits select which signal(s) are ORed to create DebugData bits 31     *
 * and 30 for all of the PI debug selections.                           *
 *                                                                      *
 ************************************************************************/




#ifdef LITTLE_ENDIAN

typedef union pi_debug_sel_u {
	bdrkreg_t	pi_debug_sel_regval;
	struct  {
		bdrkreg_t	ds_low_t5cc_a             :	 1;
                bdrkreg_t       ds_low_t5cc_b             :      1;
                bdrkreg_t       ds_low_totcc_a            :      1;
                bdrkreg_t       ds_low_totcc_b            :      1;
                bdrkreg_t       ds_low_reqcc_a            :      1;
                bdrkreg_t       ds_low_reqcc_b            :      1;
                bdrkreg_t       ds_low_rplcc_a            :      1;
                bdrkreg_t       ds_low_rplcc_b            :      1;
                bdrkreg_t       ds_low_intcc              :      1;
                bdrkreg_t       ds_low_perf_inc_a_0       :      1;
                bdrkreg_t       ds_low_perf_inc_a_1       :      1;
                bdrkreg_t       ds_low_perf_inc_b_0       :      1;
                bdrkreg_t       ds_low_perf_inc_b_1       :      1;
                bdrkreg_t       ds_high_t5cc_a            :      1;
                bdrkreg_t       ds_high_t5cc_b            :      1;
                bdrkreg_t       ds_high_totcc_a           :      1;
                bdrkreg_t       ds_high_totcc_b           :      1;
                bdrkreg_t       ds_high_reqcc_a           :      1;
                bdrkreg_t       ds_high_reqcc_b           :      1;
                bdrkreg_t       ds_high_rplcc_a           :      1;
                bdrkreg_t       ds_high_rplcc_b           :      1;
                bdrkreg_t       ds_high_intcc             :      1;
                bdrkreg_t       ds_high_perf_inc_a_0      :      1;
                bdrkreg_t       ds_high_perf_inc_a_1      :      1;
                bdrkreg_t       ds_high_perf_inc_b_0      :      1;
                bdrkreg_t       ds_high_perf_inc_b_1      :      1;
                bdrkreg_t       ds_b_sel                  :      1;
                bdrkreg_t       ds_rsvd                   :     37;
	} pi_debug_sel_fld_s;
} pi_debug_sel_u_t;

#else

typedef union pi_debug_sel_u {
	bdrkreg_t	pi_debug_sel_regval;
	struct	{
		bdrkreg_t	ds_rsvd			  :	37;
		bdrkreg_t	ds_b_sel		  :	 1;
		bdrkreg_t	ds_high_perf_inc_b_1	  :	 1;
		bdrkreg_t	ds_high_perf_inc_b_0	  :	 1;
		bdrkreg_t	ds_high_perf_inc_a_1	  :	 1;
		bdrkreg_t	ds_high_perf_inc_a_0	  :	 1;
		bdrkreg_t	ds_high_intcc		  :	 1;
		bdrkreg_t	ds_high_rplcc_b		  :	 1;
		bdrkreg_t	ds_high_rplcc_a		  :	 1;
		bdrkreg_t	ds_high_reqcc_b		  :	 1;
		bdrkreg_t	ds_high_reqcc_a		  :	 1;
		bdrkreg_t	ds_high_totcc_b		  :	 1;
		bdrkreg_t	ds_high_totcc_a		  :	 1;
		bdrkreg_t	ds_high_t5cc_b		  :	 1;
		bdrkreg_t	ds_high_t5cc_a		  :	 1;
		bdrkreg_t	ds_low_perf_inc_b_1	  :	 1;
		bdrkreg_t	ds_low_perf_inc_b_0	  :	 1;
		bdrkreg_t	ds_low_perf_inc_a_1	  :	 1;
		bdrkreg_t	ds_low_perf_inc_a_0	  :	 1;
		bdrkreg_t	ds_low_intcc		  :	 1;
		bdrkreg_t	ds_low_rplcc_b		  :	 1;
		bdrkreg_t	ds_low_rplcc_a		  :	 1;
		bdrkreg_t	ds_low_reqcc_b		  :	 1;
		bdrkreg_t	ds_low_reqcc_a		  :	 1;
		bdrkreg_t	ds_low_totcc_b		  :	 1;
		bdrkreg_t	ds_low_totcc_a		  :	 1;
		bdrkreg_t	ds_low_t5cc_b		  :	 1;
		bdrkreg_t	ds_low_t5cc_a		  :	 1;
	} pi_debug_sel_fld_s;
} pi_debug_sel_u_t;

#endif


/************************************************************************
 *                                                                      *
 *  A write to this register allows a single bit in the INT_PEND0 or    *
 * INT_PEND1 registers to be set or cleared. If 6 is clear, a bit is    *
 * modified in INT_PEND0, while if 6 is set, a bit is modified in       *
 * INT_PEND1. The value in 5:0 (ranging from 63 to 0) will determine    *
 * which bit in the register is effected. The value of 8 will           *
 * determine whether the desired bit is set (8=1) or cleared (8=0).     *
 * This is the only register which is accessible by IO issued PWRI      *
 * command and is protected through the IO_PROTECT register. If the     *
 * region bit in the IO_PROTECT is not set then a WERR reply is         *
 * issued. CPU access is controlled through CPU_PROTECT. The contents   *
 * of this register are masked with the contents of INT_MASK_A          *
 * (INT_MASK_B) to determine whether an L2 interrupt is issued to       *
 * CPU_A (CPU_B).                                                       *
 *                                                                      *
 ************************************************************************/




#ifdef LITTLE_ENDIAN

typedef union pi_int_pend_mod_alias_u {
	bdrkreg_t	pi_int_pend_mod_alias_regval;
	struct  {
		bdrkreg_t	ipma_bit_select           :	 6;
                bdrkreg_t       ipma_reg_select           :      1;
                bdrkreg_t       ipma_rsvd_1               :      1;
                bdrkreg_t       ipma_value                :      1;
                bdrkreg_t       ipma_rsvd                 :     55;
	} pi_int_pend_mod_alias_fld_s;
} pi_int_pend_mod_alias_u_t;

#else

typedef union pi_int_pend_mod_alias_u {
	bdrkreg_t	pi_int_pend_mod_alias_regval;
	struct	{
		bdrkreg_t	ipma_rsvd		  :	55;
		bdrkreg_t	ipma_value		  :	 1;
		bdrkreg_t	ipma_rsvd_1		  :	 1;
		bdrkreg_t	ipma_reg_select		  :	 1;
		bdrkreg_t	ipma_bit_select		  :	 6;
	} pi_int_pend_mod_alias_fld_s;
} pi_int_pend_mod_alias_u_t;

#endif




/************************************************************************
 *                                                                      *
 *  There is one of these registers for each CPU. This register         *
 * specifies the value of the Graphics Page. Uncached writes into the   *
 * Graphics Page (with uncached attribute of IO) are done with GFXWS    *
 * commands rather than the normal PWRI commands. GFXWS commands are    *
 * tracked with the graphics credit counters.                           *
 *                                                                      *
 ************************************************************************/




#ifdef LITTLE_ENDIAN

typedef union pi_gfx_page_a_u {
	bdrkreg_t	pi_gfx_page_a_regval;
	struct  {
		bdrkreg_t	gpa_rsvd_1                :	17;
                bdrkreg_t       gpa_gfx_page_addr         :     23;
                bdrkreg_t       gpa_en_gfx_page           :      1;
                bdrkreg_t       gpa_rsvd                  :     23;
	} pi_gfx_page_a_fld_s;
} pi_gfx_page_a_u_t;

#else

typedef union pi_gfx_page_a_u {
	bdrkreg_t	pi_gfx_page_a_regval;
	struct	{
		bdrkreg_t	gpa_rsvd		  :	23;
		bdrkreg_t	gpa_en_gfx_page		  :	 1;
		bdrkreg_t	gpa_gfx_page_addr	  :	23;
		bdrkreg_t	gpa_rsvd_1		  :	17;
	} pi_gfx_page_a_fld_s;
} pi_gfx_page_a_u_t;

#endif




/************************************************************************
 *                                                                      *
 *  There is one of these registers for each CPU. This register         *
 * counts graphics credits. This counter is decremented for each        *
 * doubleword sent to graphics with GFXWS or GFXWL commands. It is      *
 * incremented for each doubleword acknowledge from graphics. When      *
 * this counter has a smaller value than the GFX_BIAS register,         *
 * SysWrRdy_L is deasserted, an interrupt is sent to the processor,     *
 * and SysWrRdy_L is allowed to be asserted again. This is the basic    *
 * mechanism for flow-controlling graphics writes.                      *
 *                                                                      *
 ************************************************************************/




#ifdef LITTLE_ENDIAN

typedef union pi_gfx_credit_cntr_a_u {
	bdrkreg_t	pi_gfx_credit_cntr_a_regval;
	struct  {
		bdrkreg_t	gcca_gfx_credit_cntr      :	12;
		bdrkreg_t       gcca_rsvd                 :     52;
	} pi_gfx_credit_cntr_a_fld_s;
} pi_gfx_credit_cntr_a_u_t;

#else

typedef union pi_gfx_credit_cntr_a_u {
	bdrkreg_t	pi_gfx_credit_cntr_a_regval;
	struct	{
		bdrkreg_t	gcca_rsvd		  :	52;
		bdrkreg_t	gcca_gfx_credit_cntr	  :	12;
	} pi_gfx_credit_cntr_a_fld_s;
} pi_gfx_credit_cntr_a_u_t;

#endif




/************************************************************************
 *                                                                      *
 *  There is one of these registers for each CPU. When the graphics     *
 * credit counter is less than or equal to this value, a flow control   *
 * interrupt is sent.                                                   *
 *                                                                      *
 ************************************************************************/




#ifdef LITTLE_ENDIAN

typedef union pi_gfx_bias_a_u {
	bdrkreg_t	pi_gfx_bias_a_regval;
	struct  {
		bdrkreg_t	gba_gfx_bias              :	12;
		bdrkreg_t       gba_rsvd                  :     52;
	} pi_gfx_bias_a_fld_s;
} pi_gfx_bias_a_u_t;

#else

typedef union pi_gfx_bias_a_u {
	bdrkreg_t	pi_gfx_bias_a_regval;
	struct	{
		bdrkreg_t	gba_rsvd		  :	52;
		bdrkreg_t	gba_gfx_bias		  :	12;
	} pi_gfx_bias_a_fld_s;
} pi_gfx_bias_a_u_t;

#endif




/************************************************************************
 *                                                                      *
 * Description:  There is one of these registers for each CPU. When     *
 * this counter reaches the value of the GFX_INT_CMP register, an       *
 * interrupt is sent to the associated processor. At each clock         *
 * cycle, the value in this register can be changed by any one of the   *
 * following actions:                                                   *
 * - Written by software.                                               *
 * - Loaded with the value of GFX_INT_CMP, when an interrupt, NMI, or   *
 * soft reset occurs, thus preventing an additional interrupt.          *
 * - Zeroed, when the GFX_CREDIT_CNTR rises above the bias value.       *
 * - Incremented (by one at each clock) for each clock that the         *
 * GFX_CREDIT_CNTR is less than or equal to zero.                       *
 *                                                                      *
 ************************************************************************/




#ifdef LITTLE_ENDIAN

typedef union pi_gfx_int_cntr_a_u {
	bdrkreg_t	pi_gfx_int_cntr_a_regval;
	struct  {
		bdrkreg_t	gica_gfx_int_cntr         :	26;
		bdrkreg_t       gica_rsvd                 :     38;
	} pi_gfx_int_cntr_a_fld_s;
} pi_gfx_int_cntr_a_u_t;

#else

typedef union pi_gfx_int_cntr_a_u {
	bdrkreg_t	pi_gfx_int_cntr_a_regval;
	struct	{
		bdrkreg_t	gica_rsvd		  :	38;
		bdrkreg_t	gica_gfx_int_cntr	  :	26;
	} pi_gfx_int_cntr_a_fld_s;
} pi_gfx_int_cntr_a_u_t;

#endif




/************************************************************************
 *                                                                      *
 *  There is one of these registers for each CPU. The value in this     *
 * register is loaded into the GFX_INT_CNTR register when an            *
 * interrupt, NMI, or soft reset is sent to the processor. The value    *
 * in this register is compared to the value of GFX_INT_CNTR and an     *
 * interrupt is sent when they become equal.                            *
 *                                                                      *
 ************************************************************************/




#ifdef LINUX

typedef union pi_gfx_int_cmp_a_u {
	bdrkreg_t	pi_gfx_int_cmp_a_regval;
	struct  {
		bdrkreg_t	gica_gfx_int_cmp          :	26;
		bdrkreg_t       gica_rsvd                 :     38;
	} pi_gfx_int_cmp_a_fld_s;
} pi_gfx_int_cmp_a_u_t;

#else

typedef union pi_gfx_int_cmp_a_u {
	bdrkreg_t	pi_gfx_int_cmp_a_regval;
	struct	{
		bdrkreg_t	gica_rsvd		  :	38;
		bdrkreg_t	gica_gfx_int_cmp	  :	26;
	} pi_gfx_int_cmp_a_fld_s;
} pi_gfx_int_cmp_a_u_t;

#endif




/************************************************************************
 *                                                                      *
 *  There is one of these registers for each CPU. This register         *
 * specifies the value of the Graphics Page. Uncached writes into the   *
 * Graphics Page (with uncached attribute of IO) are done with GFXWS    *
 * commands rather than the normal PWRI commands. GFXWS commands are    *
 * tracked with the graphics credit counters.                           *
 *                                                                      *
 ************************************************************************/




#ifdef LITTLE_ENDIAN

typedef union pi_gfx_page_b_u {
	bdrkreg_t	pi_gfx_page_b_regval;
	struct  {
		bdrkreg_t	gpb_rsvd_1                :	17;
                bdrkreg_t       gpb_gfx_page_addr         :     23;
                bdrkreg_t       gpb_en_gfx_page           :      1;
                bdrkreg_t       gpb_rsvd                  :     23;
	} pi_gfx_page_b_fld_s;
} pi_gfx_page_b_u_t;

#else

typedef union pi_gfx_page_b_u {
	bdrkreg_t	pi_gfx_page_b_regval;
	struct	{
		bdrkreg_t	gpb_rsvd		  :	23;
		bdrkreg_t	gpb_en_gfx_page		  :	 1;
		bdrkreg_t	gpb_gfx_page_addr	  :	23;
		bdrkreg_t	gpb_rsvd_1		  :	17;
	} pi_gfx_page_b_fld_s;
} pi_gfx_page_b_u_t;

#endif




/************************************************************************
 *                                                                      *
 *  There is one of these registers for each CPU. This register         *
 * counts graphics credits. This counter is decremented for each        *
 * doubleword sent to graphics with GFXWS or GFXWL commands. It is      *
 * incremented for each doubleword acknowledge from graphics. When      *
 * this counter has a smaller value than the GFX_BIAS register,         *
 * SysWrRdy_L is deasserted, an interrupt is sent to the processor,     *
 * and SysWrRdy_L is allowed to be asserted again. This is the basic    *
 * mechanism for flow-controlling graphics writes.                      *
 *                                                                      *
 ************************************************************************/




#ifdef LITTLE_ENDIAN

typedef union pi_gfx_credit_cntr_b_u {
	bdrkreg_t	pi_gfx_credit_cntr_b_regval;
	struct  {
		bdrkreg_t	gccb_gfx_credit_cntr      :	12;
		bdrkreg_t       gccb_rsvd                 :     52;
	} pi_gfx_credit_cntr_b_fld_s;
} pi_gfx_credit_cntr_b_u_t;

#else

typedef union pi_gfx_credit_cntr_b_u {
	bdrkreg_t	pi_gfx_credit_cntr_b_regval;
	struct	{
		bdrkreg_t	gccb_rsvd		  :	52;
		bdrkreg_t	gccb_gfx_credit_cntr	  :	12;
	} pi_gfx_credit_cntr_b_fld_s;
} pi_gfx_credit_cntr_b_u_t;

#endif




/************************************************************************
 *                                                                      *
 *  There is one of these registers for each CPU. When the graphics     *
 * credit counter is less than or equal to this value, a flow control   *
 * interrupt is sent.                                                   *
 *                                                                      *
 ************************************************************************/




#ifdef LITTLE_ENDIAN

typedef union pi_gfx_bias_b_u {
	bdrkreg_t	pi_gfx_bias_b_regval;
	struct  {
		bdrkreg_t	gbb_gfx_bias              :	12;
		bdrkreg_t       gbb_rsvd                  :     52;
	} pi_gfx_bias_b_fld_s;
} pi_gfx_bias_b_u_t;

#else

typedef union pi_gfx_bias_b_u {
	bdrkreg_t	pi_gfx_bias_b_regval;
	struct	{
		bdrkreg_t	gbb_rsvd		  :	52;
		bdrkreg_t	gbb_gfx_bias		  :	12;
	} pi_gfx_bias_b_fld_s;
} pi_gfx_bias_b_u_t;

#endif




/************************************************************************
 *                                                                      *
 * Description:  There is one of these registers for each CPU. When     *
 * this counter reaches the value of the GFX_INT_CMP register, an       *
 * interrupt is sent to the associated processor. At each clock         *
 * cycle, the value in this register can be changed by any one of the   *
 * following actions:                                                   *
 * - Written by software.                                               *
 * - Loaded with the value of GFX_INT_CMP, when an interrupt, NMI, or   *
 * soft reset occurs, thus preventing an additional interrupt.          *
 * - Zeroed, when the GFX_CREDIT_CNTR rises above the bias value.       *
 * - Incremented (by one at each clock) for each clock that the         *
 * GFX_CREDIT_CNTR is less than or equal to zero.                       *
 *                                                                      *
 ************************************************************************/




#ifdef LITTLE_ENDIAN

typedef union pi_gfx_int_cntr_b_u {
	bdrkreg_t	pi_gfx_int_cntr_b_regval;
	struct  {
		bdrkreg_t	gicb_gfx_int_cntr         :	26;
		bdrkreg_t       gicb_rsvd                 :     38;
	} pi_gfx_int_cntr_b_fld_s;
} pi_gfx_int_cntr_b_u_t;

#else

typedef union pi_gfx_int_cntr_b_u {
	bdrkreg_t	pi_gfx_int_cntr_b_regval;
	struct	{
		bdrkreg_t	gicb_rsvd		  :	38;
		bdrkreg_t	gicb_gfx_int_cntr	  :	26;
	} pi_gfx_int_cntr_b_fld_s;
} pi_gfx_int_cntr_b_u_t;

#endif




/************************************************************************
 *                                                                      *
 *  There is one of these registers for each CPU. The value in this     *
 * register is loaded into the GFX_INT_CNTR register when an            *
 * interrupt, NMI, or soft reset is sent to the processor. The value    *
 * in this register is compared to the value of GFX_INT_CNTR and an     *
 * interrupt is sent when they become equal.                            *
 *                                                                      *
 ************************************************************************/




#ifdef LITTLE_ENDIAN

typedef union pi_gfx_int_cmp_b_u {
	bdrkreg_t	pi_gfx_int_cmp_b_regval;
	struct  {
		bdrkreg_t	gicb_gfx_int_cmp          :	26;
		bdrkreg_t       gicb_rsvd                 :     38;
	} pi_gfx_int_cmp_b_fld_s;
} pi_gfx_int_cmp_b_u_t;

#else

typedef union pi_gfx_int_cmp_b_u {
	bdrkreg_t	pi_gfx_int_cmp_b_regval;
	struct	{
		bdrkreg_t	gicb_rsvd		  :	38;
		bdrkreg_t	gicb_gfx_int_cmp	  :	26;
	} pi_gfx_int_cmp_b_fld_s;
} pi_gfx_int_cmp_b_u_t;

#endif




/************************************************************************
 *                                                                      *
 * Description:  A read of this register returns all sources of         *
 * Bedrock Error Interrupts. Storing to the write-with-clear location   *
 * clears any bit for which a one appears on the data bus. Storing to   *
 * the writable location does a direct write to all unreserved bits     *
 * (except for MEM_UNC).                                                *
 * In Synergy mode, the processor that is the source of the command     *
 * that got an error is independent of the A or B SysAD bus. So in      *
 * Synergy mode, Synergy provides the source processor number in bit    *
 * 52 of the SysAD bus in all commands. The PI saves this in the RRB    *
 * or WRB entry, and uses that value to determine which error bit (A    *
 * or B) to set, as well as which ERR_STATUS and spool registers to     *
 * use, for all error types in this register that are specified as an   *
 * error to CPU_A or CPU_B.                                             *
 * This register is not cleared at reset.                               *
 *                                                                      *
 ************************************************************************/




#ifdef LITTLE_ENDIAN

typedef union pi_err_int_pend_wr_u {
	bdrkreg_t	pi_err_int_pend_wr_regval;
	struct  {
		bdrkreg_t	eipw_spool_comp_b         :	 1;
                bdrkreg_t       eipw_spool_comp_a         :      1;
                bdrkreg_t       eipw_spurious_b           :      1;
                bdrkreg_t       eipw_spurious_a           :      1;
                bdrkreg_t       eipw_wrb_terr_b           :      1;
                bdrkreg_t       eipw_wrb_terr_a           :      1;
                bdrkreg_t       eipw_wrb_werr_b           :      1;
                bdrkreg_t       eipw_wrb_werr_a           :      1;
                bdrkreg_t       eipw_sysstate_par_b       :      1;
                bdrkreg_t       eipw_sysstate_par_a       :      1;
                bdrkreg_t       eipw_sysad_data_ecc_b     :      1;
                bdrkreg_t       eipw_sysad_data_ecc_a     :      1;
                bdrkreg_t       eipw_sysad_addr_ecc_b     :      1;
                bdrkreg_t       eipw_sysad_addr_ecc_a     :      1;
                bdrkreg_t       eipw_syscmd_data_par_b    :      1;
                bdrkreg_t       eipw_syscmd_data_par_a    :      1;
                bdrkreg_t       eipw_syscmd_addr_par_b    :      1;
                bdrkreg_t       eipw_syscmd_addr_par_a    :      1;
                bdrkreg_t       eipw_spool_err_b          :      1;
                bdrkreg_t       eipw_spool_err_a          :      1;
                bdrkreg_t       eipw_ue_uncached_b        :      1;
                bdrkreg_t       eipw_ue_uncached_a        :      1;
                bdrkreg_t       eipw_sysstate_tag_b       :      1;
                bdrkreg_t       eipw_sysstate_tag_a       :      1;
                bdrkreg_t       eipw_mem_unc              :      1;
                bdrkreg_t       eipw_sysad_bad_data_b     :      1;
                bdrkreg_t       eipw_sysad_bad_data_a     :      1;
                bdrkreg_t       eipw_ue_cached_b          :      1;
                bdrkreg_t       eipw_ue_cached_a          :      1;
                bdrkreg_t       eipw_pkt_len_err_b        :      1;
                bdrkreg_t       eipw_pkt_len_err_a        :      1;
                bdrkreg_t       eipw_irb_err_b            :      1;
                bdrkreg_t       eipw_irb_err_a            :      1;
                bdrkreg_t       eipw_irb_timeout_b        :      1;
                bdrkreg_t       eipw_irb_timeout_a        :      1;
                bdrkreg_t       eipw_rsvd                 :     29;
	} pi_err_int_pend_wr_fld_s;
} pi_err_int_pend_wr_u_t;

#else

typedef union pi_err_int_pend_wr_u {
	bdrkreg_t	pi_err_int_pend_wr_regval;
	struct	{
		bdrkreg_t	eipw_rsvd		  :	29;
		bdrkreg_t	eipw_irb_timeout_a	  :	 1;
		bdrkreg_t	eipw_irb_timeout_b	  :	 1;
		bdrkreg_t	eipw_irb_err_a		  :	 1;
		bdrkreg_t	eipw_irb_err_b		  :	 1;
		bdrkreg_t	eipw_pkt_len_err_a	  :	 1;
		bdrkreg_t	eipw_pkt_len_err_b	  :	 1;
		bdrkreg_t	eipw_ue_cached_a	  :	 1;
		bdrkreg_t	eipw_ue_cached_b	  :	 1;
		bdrkreg_t	eipw_sysad_bad_data_a	  :	 1;
		bdrkreg_t	eipw_sysad_bad_data_b	  :	 1;
		bdrkreg_t	eipw_mem_unc		  :	 1;
		bdrkreg_t	eipw_sysstate_tag_a	  :	 1;
		bdrkreg_t	eipw_sysstate_tag_b	  :	 1;
		bdrkreg_t	eipw_ue_uncached_a	  :	 1;
		bdrkreg_t	eipw_ue_uncached_b	  :	 1;
		bdrkreg_t	eipw_spool_err_a	  :	 1;
		bdrkreg_t	eipw_spool_err_b	  :	 1;
		bdrkreg_t	eipw_syscmd_addr_par_a	  :	 1;
		bdrkreg_t	eipw_syscmd_addr_par_b	  :	 1;
		bdrkreg_t	eipw_syscmd_data_par_a	  :	 1;
		bdrkreg_t	eipw_syscmd_data_par_b	  :	 1;
		bdrkreg_t	eipw_sysad_addr_ecc_a	  :	 1;
		bdrkreg_t	eipw_sysad_addr_ecc_b	  :	 1;
		bdrkreg_t	eipw_sysad_data_ecc_a	  :	 1;
		bdrkreg_t	eipw_sysad_data_ecc_b	  :	 1;
		bdrkreg_t	eipw_sysstate_par_a	  :	 1;
		bdrkreg_t	eipw_sysstate_par_b	  :	 1;
		bdrkreg_t	eipw_wrb_werr_a		  :	 1;
		bdrkreg_t	eipw_wrb_werr_b		  :	 1;
		bdrkreg_t	eipw_wrb_terr_a		  :	 1;
		bdrkreg_t	eipw_wrb_terr_b		  :	 1;
		bdrkreg_t	eipw_spurious_a		  :	 1;
		bdrkreg_t	eipw_spurious_b		  :	 1;
		bdrkreg_t	eipw_spool_comp_a	  :	 1;
		bdrkreg_t	eipw_spool_comp_b	  :	 1;
	} pi_err_int_pend_wr_fld_s;
} pi_err_int_pend_wr_u_t;

#endif




/************************************************************************
 *                                                                      *
 * Description:  A read of this register returns all sources of         *
 * Bedrock Error Interrupts. Storing to the write-with-clear location   *
 * clears any bit for which a one appears on the data bus. Storing to   *
 * the writable location does a direct write to all unreserved bits     *
 * (except for MEM_UNC).                                                *
 * In Synergy mode, the processor that is the source of the command     *
 * that got an error is independent of the A or B SysAD bus. So in      *
 * Synergy mode, Synergy provides the source processor number in bit    *
 * 52 of the SysAD bus in all commands. The PI saves this in the RRB    *
 * or WRB entry, and uses that value to determine which error bit (A    *
 * or B) to set, as well as which ERR_STATUS and spool registers to     *
 * use, for all error types in this register that are specified as an   *
 * error to CPU_A or CPU_B.                                             *
 * This register is not cleared at reset.                               *
 *                                                                      *
 ************************************************************************/




#ifdef LITTLE_ENDIAN

typedef union pi_err_int_pend_u {
	bdrkreg_t	pi_err_int_pend_regval;
	struct  {
		bdrkreg_t	eip_spool_comp_b          :	 1;
                bdrkreg_t       eip_spool_comp_a          :      1;
                bdrkreg_t       eip_spurious_b            :      1;
                bdrkreg_t       eip_spurious_a            :      1;
                bdrkreg_t       eip_wrb_terr_b            :      1;
                bdrkreg_t       eip_wrb_terr_a            :      1;
                bdrkreg_t       eip_wrb_werr_b            :      1;
                bdrkreg_t       eip_wrb_werr_a            :      1;
                bdrkreg_t       eip_sysstate_par_b        :      1;
                bdrkreg_t       eip_sysstate_par_a        :      1;
                bdrkreg_t       eip_sysad_data_ecc_b      :      1;
                bdrkreg_t       eip_sysad_data_ecc_a      :      1;
                bdrkreg_t       eip_sysad_addr_ecc_b      :      1;
                bdrkreg_t       eip_sysad_addr_ecc_a      :      1;
                bdrkreg_t       eip_syscmd_data_par_b     :      1;
                bdrkreg_t       eip_syscmd_data_par_a     :      1;
                bdrkreg_t       eip_syscmd_addr_par_b     :      1;
                bdrkreg_t       eip_syscmd_addr_par_a     :      1;
                bdrkreg_t       eip_spool_err_b           :      1;
                bdrkreg_t       eip_spool_err_a           :      1;
                bdrkreg_t       eip_ue_uncached_b         :      1;
                bdrkreg_t       eip_ue_uncached_a         :      1;
                bdrkreg_t       eip_sysstate_tag_b        :      1;
                bdrkreg_t       eip_sysstate_tag_a        :      1;
                bdrkreg_t       eip_mem_unc               :      1;
                bdrkreg_t       eip_sysad_bad_data_b      :      1;
                bdrkreg_t       eip_sysad_bad_data_a      :      1;
                bdrkreg_t       eip_ue_cached_b           :      1;
                bdrkreg_t       eip_ue_cached_a           :      1;
                bdrkreg_t       eip_pkt_len_err_b         :      1;
                bdrkreg_t       eip_pkt_len_err_a         :      1;
                bdrkreg_t       eip_irb_err_b             :      1;
                bdrkreg_t       eip_irb_err_a             :      1;
                bdrkreg_t       eip_irb_timeout_b         :      1;
                bdrkreg_t       eip_irb_timeout_a         :      1;
                bdrkreg_t       eip_rsvd                  :     29;
	} pi_err_int_pend_fld_s;
} pi_err_int_pend_u_t;

#else

typedef union pi_err_int_pend_u {
	bdrkreg_t	pi_err_int_pend_regval;
	struct	{
		bdrkreg_t	eip_rsvd		  :	29;
		bdrkreg_t	eip_irb_timeout_a	  :	 1;
		bdrkreg_t	eip_irb_timeout_b	  :	 1;
		bdrkreg_t	eip_irb_err_a		  :	 1;
		bdrkreg_t	eip_irb_err_b		  :	 1;
		bdrkreg_t	eip_pkt_len_err_a	  :	 1;
		bdrkreg_t	eip_pkt_len_err_b	  :	 1;
		bdrkreg_t	eip_ue_cached_a		  :	 1;
		bdrkreg_t	eip_ue_cached_b		  :	 1;
		bdrkreg_t	eip_sysad_bad_data_a	  :	 1;
		bdrkreg_t	eip_sysad_bad_data_b	  :	 1;
		bdrkreg_t	eip_mem_unc		  :	 1;
		bdrkreg_t	eip_sysstate_tag_a	  :	 1;
		bdrkreg_t	eip_sysstate_tag_b	  :	 1;
		bdrkreg_t	eip_ue_uncached_a	  :	 1;
		bdrkreg_t	eip_ue_uncached_b	  :	 1;
		bdrkreg_t	eip_spool_err_a		  :	 1;
		bdrkreg_t	eip_spool_err_b		  :	 1;
		bdrkreg_t	eip_syscmd_addr_par_a	  :	 1;
		bdrkreg_t	eip_syscmd_addr_par_b	  :	 1;
		bdrkreg_t	eip_syscmd_data_par_a	  :	 1;
		bdrkreg_t	eip_syscmd_data_par_b	  :	 1;
		bdrkreg_t	eip_sysad_addr_ecc_a	  :	 1;
		bdrkreg_t	eip_sysad_addr_ecc_b	  :	 1;
		bdrkreg_t	eip_sysad_data_ecc_a	  :	 1;
		bdrkreg_t	eip_sysad_data_ecc_b	  :	 1;
		bdrkreg_t	eip_sysstate_par_a	  :	 1;
		bdrkreg_t	eip_sysstate_par_b	  :	 1;
		bdrkreg_t	eip_wrb_werr_a		  :	 1;
		bdrkreg_t	eip_wrb_werr_b		  :	 1;
		bdrkreg_t	eip_wrb_terr_a		  :	 1;
		bdrkreg_t	eip_wrb_terr_b		  :	 1;
		bdrkreg_t	eip_spurious_a		  :	 1;
		bdrkreg_t	eip_spurious_b		  :	 1;
		bdrkreg_t	eip_spool_comp_a	  :	 1;
		bdrkreg_t	eip_spool_comp_b	  :	 1;
	} pi_err_int_pend_fld_s;
} pi_err_int_pend_u_t;

#endif





/************************************************************************
 *                                                                      *
 *  There is one of these registers for each CPU. This read/write       *
 * register masks the contents of ERR_INT_PEND to determine which       *
 * conditions cause a Level-6 interrupt to CPU_A or CPU_B. A bit set    *
 * allows the interrupt. Only one processor in a Bedrock should         *
 * enable the Memory/Directory Uncorrectable Error bit.                 *
 *                                                                      *
 ************************************************************************/




#ifdef LITTLE_ENDIAN

typedef union pi_err_int_mask_a_u {
	bdrkreg_t	pi_err_int_mask_a_regval;
	struct  {
		bdrkreg_t	eima_mask                 :	35;
		bdrkreg_t       eima_rsvd                 :     29;
	} pi_err_int_mask_a_fld_s;
} pi_err_int_mask_a_u_t;

#else

typedef union pi_err_int_mask_a_u {
	bdrkreg_t	pi_err_int_mask_a_regval;
	struct	{
		bdrkreg_t	eima_rsvd		  :	29;
		bdrkreg_t	eima_mask		  :	35;
	} pi_err_int_mask_a_fld_s;
} pi_err_int_mask_a_u_t;

#endif




/************************************************************************
 *                                                                      *
 *  There is one of these registers for each CPU. This read/write       *
 * register masks the contents of ERR_INT_PEND to determine which       *
 * conditions cause a Level-6 interrupt to CPU_A or CPU_B. A bit set    *
 * allows the interrupt. Only one processor in a Bedrock should         *
 * enable the Memory/Directory Uncorrectable Error bit.                 *
 *                                                                      *
 ************************************************************************/




#ifdef LITTLE_ENDIAN

typedef union pi_err_int_mask_b_u {
	bdrkreg_t	pi_err_int_mask_b_regval;
	struct  {
		bdrkreg_t	eimb_mask                 :	35;
		bdrkreg_t       eimb_rsvd                 :     29;
	} pi_err_int_mask_b_fld_s;
} pi_err_int_mask_b_u_t;

#else

typedef union pi_err_int_mask_b_u {
	bdrkreg_t	pi_err_int_mask_b_regval;
	struct	{
		bdrkreg_t	eimb_rsvd		  :	29;
		bdrkreg_t	eimb_mask		  :	35;
	} pi_err_int_mask_b_fld_s;
} pi_err_int_mask_b_u_t;

#endif




/************************************************************************
 *                                                                      *
 * Description:  There is one of these registers for each CPU. This     *
 * register is the address of the next write to the error stack. This   *
 * register is incremented after each such write. Only the low N bits   *
 * are incremented, where N is defined by the size of the error stack   *
 * specified in the ERR_STACK_SIZE register.                            *
 * This register is not reset by a soft reset.                          *
 *                                                                      *
 ************************************************************************/




#ifdef LITTLE_ENDIAN

typedef union pi_err_stack_addr_a_u {
	bdrkreg_t	pi_err_stack_addr_a_regval;
	struct  {
		bdrkreg_t	esaa_rsvd_1               :	 3;
                bdrkreg_t       esaa_addr                 :     30;
                bdrkreg_t       esaa_rsvd                 :     31;
	} pi_err_stack_addr_a_fld_s;
} pi_err_stack_addr_a_u_t;

#else

typedef union pi_err_stack_addr_a_u {
	bdrkreg_t	pi_err_stack_addr_a_regval;
	struct	{
		bdrkreg_t	esaa_rsvd		  :	31;
		bdrkreg_t	esaa_addr		  :	30;
		bdrkreg_t	esaa_rsvd_1		  :	 3;
	} pi_err_stack_addr_a_fld_s;
} pi_err_stack_addr_a_u_t;

#endif




/************************************************************************
 *                                                                      *
 * Description:  There is one of these registers for each CPU. This     *
 * register is the address of the next write to the error stack. This   *
 * register is incremented after each such write. Only the low N bits   *
 * are incremented, where N is defined by the size of the error stack   *
 * specified in the ERR_STACK_SIZE register.                            *
 * This register is not reset by a soft reset.                          *
 *                                                                      *
 ************************************************************************/




#ifdef LITTLE_ENDIAN

typedef union pi_err_stack_addr_b_u {
	bdrkreg_t	pi_err_stack_addr_b_regval;
	struct  {
		bdrkreg_t	esab_rsvd_1               :	 3;
                bdrkreg_t       esab_addr                 :     30;
                bdrkreg_t       esab_rsvd                 :     31;
	} pi_err_stack_addr_b_fld_s;
} pi_err_stack_addr_b_u_t;

#else

typedef union pi_err_stack_addr_b_u {
	bdrkreg_t	pi_err_stack_addr_b_regval;
	struct	{
		bdrkreg_t	esab_rsvd		  :	31;
		bdrkreg_t	esab_addr		  :	30;
		bdrkreg_t	esab_rsvd_1		  :	 3;
	} pi_err_stack_addr_b_fld_s;
} pi_err_stack_addr_b_u_t;

#endif




/************************************************************************
 *                                                                      *
 * Description:  Sets the size (number of 64-bit entries) in the        *
 * error stack that is spooled to local memory when an error occurs.    *
 * Table16 defines the format of each entry in the spooled error        *
 * stack.                                                               *
 * This register is not reset by a soft reset.                          *
 *                                                                      *
 ************************************************************************/




#ifdef LITTLE_ENDIAN

typedef union pi_err_stack_size_u {
	bdrkreg_t	pi_err_stack_size_regval;
	struct  {
		bdrkreg_t	ess_size                  :	 4;
                bdrkreg_t       ess_rsvd                  :     60;
	} pi_err_stack_size_fld_s;
} pi_err_stack_size_u_t;

#else

typedef union pi_err_stack_size_u {
	bdrkreg_t	pi_err_stack_size_regval;
	struct	{
		bdrkreg_t	ess_rsvd		  :	60;
		bdrkreg_t	ess_size		  :	 4;
	} pi_err_stack_size_fld_s;
} pi_err_stack_size_u_t;

#endif




/************************************************************************
 *                                                                      *
 *  This register is not cleared at reset. Writing this register with   *
 * the Write-clear address (with any data) clears both the              *
 * ERR_STATUS0_A and ERR_STATUS1_A registers.                           *
 *                                                                      *
 ************************************************************************/




#ifdef LITTLE_ENDIAN

typedef union pi_err_status0_a_u {
	bdrkreg_t	pi_err_status0_a_regval;
	struct  {
		bdrkreg_t	esa_error_type            :	 3;
                bdrkreg_t       esa_proc_req_num          :      3;
                bdrkreg_t       esa_supplemental          :     11;
                bdrkreg_t       esa_cmd                   :      8;
                bdrkreg_t       esa_addr                  :     37;
                bdrkreg_t       esa_over_run              :      1;
                bdrkreg_t       esa_valid                 :      1;
	} pi_err_status0_a_fld_s;
} pi_err_status0_a_u_t;

#else

typedef union pi_err_status0_a_u {
	bdrkreg_t	pi_err_status0_a_regval;
	struct	{
		bdrkreg_t	esa_valid		  :	 1;
		bdrkreg_t	esa_over_run		  :	 1;
		bdrkreg_t	esa_addr		  :	37;
		bdrkreg_t	esa_cmd			  :	 8;
		bdrkreg_t	esa_supplemental	  :	11;
		bdrkreg_t	esa_proc_req_num	  :	 3;
		bdrkreg_t	esa_error_type		  :	 3;
	} pi_err_status0_a_fld_s;
} pi_err_status0_a_u_t;

#endif




/************************************************************************
 *                                                                      *
 *  This register is not cleared at reset. Writing this register with   *
 * the Write-clear address (with any data) clears both the              *
 * ERR_STATUS0_A and ERR_STATUS1_A registers.                           *
 *                                                                      *
 ************************************************************************/




#ifdef LITTLE_ENDIAN

typedef union pi_err_status0_a_clr_u {
	bdrkreg_t	pi_err_status0_a_clr_regval;
	struct  {
		bdrkreg_t	esac_error_type           :	 3;
                bdrkreg_t       esac_proc_req_num         :      3;
                bdrkreg_t       esac_supplemental         :     11;
                bdrkreg_t       esac_cmd                  :      8;
                bdrkreg_t       esac_addr                 :     37;
                bdrkreg_t       esac_over_run             :      1;
                bdrkreg_t       esac_valid                :      1;
	} pi_err_status0_a_clr_fld_s;
} pi_err_status0_a_clr_u_t;

#else

typedef union pi_err_status0_a_clr_u {
	bdrkreg_t	pi_err_status0_a_clr_regval;
	struct	{
		bdrkreg_t	esac_valid		  :	 1;
		bdrkreg_t	esac_over_run		  :	 1;
		bdrkreg_t	esac_addr		  :	37;
		bdrkreg_t	esac_cmd		  :	 8;
		bdrkreg_t	esac_supplemental	  :	11;
		bdrkreg_t	esac_proc_req_num	  :	 3;
		bdrkreg_t	esac_error_type		  :	 3;
	} pi_err_status0_a_clr_fld_s;
} pi_err_status0_a_clr_u_t;

#endif




/************************************************************************
 *                                                                      *
 *  This register is not cleared at reset. Writing this register with   *
 * the Write-clear address (with any data) clears both the              *
 * ERR_STATUS0_A and ERR_STATUS1_A registers.                           *
 *                                                                      *
 ************************************************************************/




#ifdef LITTLE_ENDIAN

typedef union pi_err_status1_a_u {
	bdrkreg_t	pi_err_status1_a_regval;
	struct  {
		bdrkreg_t	esa_spool_count           :	21;
                bdrkreg_t       esa_time_out_count        :      8;
                bdrkreg_t       esa_inval_count           :     10;
                bdrkreg_t       esa_crb_num               :      3;
                bdrkreg_t       esa_wrb                   :      1;
                bdrkreg_t       esa_e_bits                :      2;
                bdrkreg_t       esa_t_bit                 :      1;
                bdrkreg_t       esa_i_bit                 :      1;
                bdrkreg_t       esa_h_bit                 :      1;
                bdrkreg_t       esa_w_bit                 :      1;
                bdrkreg_t       esa_a_bit                 :      1;
                bdrkreg_t       esa_r_bit                 :      1;
                bdrkreg_t       esa_v_bit                 :      1;
                bdrkreg_t       esa_p_bit                 :      1;
                bdrkreg_t       esa_source                :     11;
	} pi_err_status1_a_fld_s;
} pi_err_status1_a_u_t;

#else

typedef union pi_err_status1_a_u {
	bdrkreg_t	pi_err_status1_a_regval;
	struct	{
		bdrkreg_t	esa_source		  :	11;
		bdrkreg_t	esa_p_bit		  :	 1;
		bdrkreg_t	esa_v_bit		  :	 1;
		bdrkreg_t	esa_r_bit		  :	 1;
		bdrkreg_t	esa_a_bit		  :	 1;
		bdrkreg_t	esa_w_bit		  :	 1;
		bdrkreg_t	esa_h_bit		  :	 1;
		bdrkreg_t	esa_i_bit		  :	 1;
		bdrkreg_t	esa_t_bit		  :	 1;
		bdrkreg_t	esa_e_bits		  :	 2;
		bdrkreg_t	esa_wrb			  :	 1;
		bdrkreg_t	esa_crb_num		  :	 3;
		bdrkreg_t	esa_inval_count		  :	10;
		bdrkreg_t	esa_time_out_count	  :	 8;
		bdrkreg_t	esa_spool_count		  :	21;
	} pi_err_status1_a_fld_s;
} pi_err_status1_a_u_t;

#endif




/************************************************************************
 *                                                                      *
 *  This register is not cleared at reset. Writing this register with   *
 * the Write-clear address (with any data) clears both the              *
 * ERR_STATUS0_A and ERR_STATUS1_A registers.                           *
 *                                                                      *
 ************************************************************************/




#ifdef LITTLE_ENDIAN

typedef union pi_err_status1_a_clr_u {
	bdrkreg_t	pi_err_status1_a_clr_regval;
	struct  {
		bdrkreg_t	esac_spool_count          :	21;
                bdrkreg_t       esac_time_out_count       :      8;
                bdrkreg_t       esac_inval_count          :     10;
                bdrkreg_t       esac_crb_num              :      3;
                bdrkreg_t       esac_wrb                  :      1;
                bdrkreg_t       esac_e_bits               :      2;
                bdrkreg_t       esac_t_bit                :      1;
                bdrkreg_t       esac_i_bit                :      1;
                bdrkreg_t       esac_h_bit                :      1;
                bdrkreg_t       esac_w_bit                :      1;
                bdrkreg_t       esac_a_bit                :      1;
                bdrkreg_t       esac_r_bit                :      1;
                bdrkreg_t       esac_v_bit                :      1;
                bdrkreg_t       esac_p_bit                :      1;
                bdrkreg_t       esac_source               :     11;
	} pi_err_status1_a_clr_fld_s;
} pi_err_status1_a_clr_u_t;

#else

typedef union pi_err_status1_a_clr_u {
	bdrkreg_t	pi_err_status1_a_clr_regval;
	struct	{
		bdrkreg_t	esac_source		  :	11;
		bdrkreg_t	esac_p_bit		  :	 1;
		bdrkreg_t	esac_v_bit		  :	 1;
		bdrkreg_t	esac_r_bit		  :	 1;
		bdrkreg_t	esac_a_bit		  :	 1;
		bdrkreg_t	esac_w_bit		  :	 1;
		bdrkreg_t	esac_h_bit		  :	 1;
		bdrkreg_t	esac_i_bit		  :	 1;
		bdrkreg_t	esac_t_bit		  :	 1;
		bdrkreg_t	esac_e_bits		  :	 2;
		bdrkreg_t	esac_wrb		  :	 1;
		bdrkreg_t	esac_crb_num		  :	 3;
		bdrkreg_t	esac_inval_count	  :	10;
		bdrkreg_t	esac_time_out_count	  :	 8;
		bdrkreg_t	esac_spool_count	  :	21;
	} pi_err_status1_a_clr_fld_s;
} pi_err_status1_a_clr_u_t;

#endif




/************************************************************************
 *                                                                      *
 *  This register is not cleared at reset. Writing this register with   *
 * the Write-clear address (with any data) clears both the              *
 * ERR_STATUS0_B and ERR_STATUS1_B registers.                           *
 *                                                                      *
 ************************************************************************/




#ifdef LITTLE_ENDIAN

typedef union pi_err_status0_b_u {
	bdrkreg_t	pi_err_status0_b_regval;
	struct  {
		bdrkreg_t	esb_error_type            :	 3;
                bdrkreg_t       esb_proc_request_number   :      3;
                bdrkreg_t       esb_supplemental          :     11;
                bdrkreg_t       esb_cmd                   :      8;
                bdrkreg_t       esb_addr                  :     37;
                bdrkreg_t       esb_over_run              :      1;
                bdrkreg_t       esb_valid                 :      1;
	} pi_err_status0_b_fld_s;
} pi_err_status0_b_u_t;

#else

typedef union pi_err_status0_b_u {
	bdrkreg_t	pi_err_status0_b_regval;
	struct	{
		bdrkreg_t	esb_valid		  :	 1;
		bdrkreg_t	esb_over_run		  :	 1;
		bdrkreg_t	esb_addr		  :	37;
		bdrkreg_t	esb_cmd			  :	 8;
		bdrkreg_t	esb_supplemental	  :	11;
		bdrkreg_t	esb_proc_request_number	  :	 3;
		bdrkreg_t	esb_error_type		  :	 3;
	} pi_err_status0_b_fld_s;
} pi_err_status0_b_u_t;

#endif




/************************************************************************
 *                                                                      *
 *  This register is not cleared at reset. Writing this register with   *
 * the Write-clear address (with any data) clears both the              *
 * ERR_STATUS0_B and ERR_STATUS1_B registers.                           *
 *                                                                      *
 ************************************************************************/




#ifdef LITTLE_ENDIAN

typedef union pi_err_status0_b_clr_u {
	bdrkreg_t	pi_err_status0_b_clr_regval;
	struct  {
		bdrkreg_t	esbc_error_type           :	 3;
                bdrkreg_t       esbc_proc_request_number  :      3;
                bdrkreg_t       esbc_supplemental         :     11;
                bdrkreg_t       esbc_cmd                  :      8;
                bdrkreg_t       esbc_addr                 :     37;
                bdrkreg_t       esbc_over_run             :      1;
                bdrkreg_t       esbc_valid                :      1;
	} pi_err_status0_b_clr_fld_s;
} pi_err_status0_b_clr_u_t;

#else

typedef union pi_err_status0_b_clr_u {
	bdrkreg_t	pi_err_status0_b_clr_regval;
	struct	{
		bdrkreg_t	esbc_valid		  :	 1;
		bdrkreg_t	esbc_over_run		  :	 1;
		bdrkreg_t	esbc_addr		  :	37;
		bdrkreg_t	esbc_cmd		  :	 8;
		bdrkreg_t	esbc_supplemental	  :	11;
		bdrkreg_t	esbc_proc_request_number  :	 3;
		bdrkreg_t	esbc_error_type		  :	 3;
	} pi_err_status0_b_clr_fld_s;
} pi_err_status0_b_clr_u_t;

#endif




/************************************************************************
 *                                                                      *
 *  This register is not cleared at reset. Writing this register with   *
 * the Write-clear address (with any data) clears both the              *
 * ERR_STATUS0_B and ERR_STATUS1_B registers.                           *
 *                                                                      *
 ************************************************************************/




#ifdef LITTLE_ENDIAN

typedef union pi_err_status1_b_u {
	bdrkreg_t	pi_err_status1_b_regval;
	struct  {
		bdrkreg_t	esb_spool_count           :	21;
                bdrkreg_t       esb_time_out_count        :      8;
                bdrkreg_t       esb_inval_count           :     10;
                bdrkreg_t       esb_crb_num               :      3;
                bdrkreg_t       esb_wrb                   :      1;
                bdrkreg_t       esb_e_bits                :      2;
                bdrkreg_t       esb_t_bit                 :      1;
                bdrkreg_t       esb_i_bit                 :      1;
                bdrkreg_t       esb_h_bit                 :      1;
                bdrkreg_t       esb_w_bit                 :      1;
                bdrkreg_t       esb_a_bit                 :      1;
                bdrkreg_t       esb_r_bit                 :      1;
                bdrkreg_t       esb_v_bit                 :      1;
                bdrkreg_t       esb_p_bit                 :      1;
                bdrkreg_t       esb_source                :     11;
	} pi_err_status1_b_fld_s;
} pi_err_status1_b_u_t;

#else

typedef union pi_err_status1_b_u {
	bdrkreg_t	pi_err_status1_b_regval;
	struct	{
		bdrkreg_t	esb_source		  :	11;
		bdrkreg_t	esb_p_bit		  :	 1;
		bdrkreg_t	esb_v_bit		  :	 1;
		bdrkreg_t	esb_r_bit		  :	 1;
		bdrkreg_t	esb_a_bit		  :	 1;
		bdrkreg_t	esb_w_bit		  :	 1;
		bdrkreg_t	esb_h_bit		  :	 1;
		bdrkreg_t	esb_i_bit		  :	 1;
		bdrkreg_t	esb_t_bit		  :	 1;
		bdrkreg_t	esb_e_bits		  :	 2;
		bdrkreg_t	esb_wrb			  :	 1;
		bdrkreg_t	esb_crb_num		  :	 3;
		bdrkreg_t	esb_inval_count		  :	10;
		bdrkreg_t	esb_time_out_count	  :	 8;
		bdrkreg_t	esb_spool_count		  :	21;
	} pi_err_status1_b_fld_s;
} pi_err_status1_b_u_t;

#endif




/************************************************************************
 *                                                                      *
 *  This register is not cleared at reset. Writing this register with   *
 * the Write-clear address (with any data) clears both the              *
 * ERR_STATUS0_B and ERR_STATUS1_B registers.                           *
 *                                                                      *
 ************************************************************************/




#ifdef LITTLE_ENDIAN

typedef union pi_err_status1_b_clr_u {
	bdrkreg_t	pi_err_status1_b_clr_regval;
	struct  {
		bdrkreg_t	esbc_spool_count          :	21;
                bdrkreg_t       esbc_time_out_count       :      8;
                bdrkreg_t       esbc_inval_count          :     10;
                bdrkreg_t       esbc_crb_num              :      3;
                bdrkreg_t       esbc_wrb                  :      1;
                bdrkreg_t       esbc_e_bits               :      2;
                bdrkreg_t       esbc_t_bit                :      1;
                bdrkreg_t       esbc_i_bit                :      1;
                bdrkreg_t       esbc_h_bit                :      1;
                bdrkreg_t       esbc_w_bit                :      1;
                bdrkreg_t       esbc_a_bit                :      1;
                bdrkreg_t       esbc_r_bit                :      1;
                bdrkreg_t       esbc_v_bit                :      1;
                bdrkreg_t       esbc_p_bit                :      1;
                bdrkreg_t       esbc_source               :     11;
	} pi_err_status1_b_clr_fld_s;
} pi_err_status1_b_clr_u_t;

#else

typedef union pi_err_status1_b_clr_u {
	bdrkreg_t	pi_err_status1_b_clr_regval;
	struct	{
		bdrkreg_t	esbc_source		  :	11;
		bdrkreg_t	esbc_p_bit		  :	 1;
		bdrkreg_t	esbc_v_bit		  :	 1;
		bdrkreg_t	esbc_r_bit		  :	 1;
		bdrkreg_t	esbc_a_bit		  :	 1;
		bdrkreg_t	esbc_w_bit		  :	 1;
		bdrkreg_t	esbc_h_bit		  :	 1;
		bdrkreg_t	esbc_i_bit		  :	 1;
		bdrkreg_t	esbc_t_bit		  :	 1;
		bdrkreg_t	esbc_e_bits		  :	 2;
		bdrkreg_t	esbc_wrb		  :	 1;
		bdrkreg_t	esbc_crb_num		  :	 3;
		bdrkreg_t	esbc_inval_count	  :	10;
		bdrkreg_t	esbc_time_out_count	  :	 8;
		bdrkreg_t	esbc_spool_count	  :	21;
	} pi_err_status1_b_clr_fld_s;
} pi_err_status1_b_clr_u_t;

#endif




/************************************************************************
 *                                                                      *
 *  There is one of these registers for each CPU.                       *
 *                                                                      *
 ************************************************************************/




#ifdef LITTLE_ENDIAN

typedef union pi_spool_cmp_a_u {
	bdrkreg_t	pi_spool_cmp_a_regval;
	struct  {
		bdrkreg_t	sca_compare               :	20;
		bdrkreg_t       sca_rsvd                  :     44;
	} pi_spool_cmp_a_fld_s;
} pi_spool_cmp_a_u_t;

#else

typedef union pi_spool_cmp_a_u {
	bdrkreg_t	pi_spool_cmp_a_regval;
	struct	{
		bdrkreg_t	sca_rsvd		  :	44;
		bdrkreg_t	sca_compare		  :	20;
	} pi_spool_cmp_a_fld_s;
} pi_spool_cmp_a_u_t;

#endif




/************************************************************************
 *                                                                      *
 *  There is one of these registers for each CPU.                       *
 *                                                                      *
 ************************************************************************/




#ifdef LITTLE_ENDIAN

typedef union pi_spool_cmp_b_u {
	bdrkreg_t	pi_spool_cmp_b_regval;
	struct  {
		bdrkreg_t	scb_compare               :	20;
		bdrkreg_t       scb_rsvd                  :     44;
	} pi_spool_cmp_b_fld_s;
} pi_spool_cmp_b_u_t;

#else

typedef union pi_spool_cmp_b_u {
	bdrkreg_t	pi_spool_cmp_b_regval;
	struct	{
		bdrkreg_t	scb_rsvd		  :	44;
		bdrkreg_t	scb_compare		  :	20;
	} pi_spool_cmp_b_fld_s;
} pi_spool_cmp_b_u_t;

#endif




/************************************************************************
 *                                                                      *
 *  There is one of these registers for each CPU. A timeout can be      *
 * forced by writing one(s).                                            *
 *                                                                      *
 ************************************************************************/




#ifdef LITTLE_ENDIAN

typedef union pi_crb_timeout_a_u {
	bdrkreg_t	pi_crb_timeout_a_regval;
	struct  {
		bdrkreg_t	cta_rrb                   :	 4;
                bdrkreg_t       cta_wrb                   :      8;
                bdrkreg_t       cta_rsvd                  :     52;
	} pi_crb_timeout_a_fld_s;
} pi_crb_timeout_a_u_t;

#else

typedef union pi_crb_timeout_a_u {
	bdrkreg_t	pi_crb_timeout_a_regval;
	struct	{
		bdrkreg_t	cta_rsvd		  :	52;
		bdrkreg_t	cta_wrb			  :	 8;
		bdrkreg_t	cta_rrb			  :	 4;
	} pi_crb_timeout_a_fld_s;
} pi_crb_timeout_a_u_t;

#endif




/************************************************************************
 *                                                                      *
 *  There is one of these registers for each CPU. A timeout can be      *
 * forced by writing one(s).                                            *
 *                                                                      *
 ************************************************************************/




#ifdef LITTLE_ENDIAN

typedef union pi_crb_timeout_b_u {
	bdrkreg_t	pi_crb_timeout_b_regval;
	struct  {
		bdrkreg_t	ctb_rrb                   :	 4;
                bdrkreg_t       ctb_wrb                   :      8;
                bdrkreg_t       ctb_rsvd                  :     52;
	} pi_crb_timeout_b_fld_s;
} pi_crb_timeout_b_u_t;

#else

typedef union pi_crb_timeout_b_u {
	bdrkreg_t	pi_crb_timeout_b_regval;
	struct	{
		bdrkreg_t	ctb_rsvd		  :	52;
		bdrkreg_t	ctb_wrb			  :	 8;
		bdrkreg_t	ctb_rrb			  :	 4;
	} pi_crb_timeout_b_fld_s;
} pi_crb_timeout_b_u_t;

#endif




/************************************************************************
 *                                                                      *
 *  This register controls error checking and forwarding of SysAD       *
 * errors.                                                              *
 *                                                                      *
 ************************************************************************/




#ifdef LITTLE_ENDIAN

typedef union pi_sysad_errchk_en_u {
	bdrkreg_t	pi_sysad_errchk_en_regval;
	struct  {
		bdrkreg_t	see_ecc_gen_en            :	 1;
                bdrkreg_t       see_qual_gen_en           :      1;
                bdrkreg_t       see_sadp_chk_en           :      1;
                bdrkreg_t       see_cmdp_chk_en           :      1;
                bdrkreg_t       see_state_chk_en          :      1;
                bdrkreg_t       see_qual_chk_en           :      1;
                bdrkreg_t       see_rsvd                  :     58;
	} pi_sysad_errchk_en_fld_s;
} pi_sysad_errchk_en_u_t;

#else

typedef union pi_sysad_errchk_en_u {
	bdrkreg_t	pi_sysad_errchk_en_regval;
	struct	{
		bdrkreg_t	see_rsvd		  :	58;
		bdrkreg_t	see_qual_chk_en		  :	 1;
		bdrkreg_t	see_state_chk_en	  :	 1;
		bdrkreg_t	see_cmdp_chk_en		  :	 1;
		bdrkreg_t	see_sadp_chk_en		  :	 1;
		bdrkreg_t	see_qual_gen_en		  :	 1;
		bdrkreg_t	see_ecc_gen_en		  :	 1;
	} pi_sysad_errchk_en_fld_s;
} pi_sysad_errchk_en_u_t;

#endif




/************************************************************************
 *                                                                      *
 *  There is one of these registers for each CPU. If any bit in this    *
 * register is set, then whenever reply data arrives with the UE        *
 * (uncorrectable error) indication set, the check-bits that are        *
 * generated and sent to the SysAD will be inverted corresponding to    *
 * the bits set in the register. This will also prevent the assertion   *
 * of the data quality indicator.                                       *
 *                                                                      *
 ************************************************************************/




#ifdef LITTLE_ENDIAN

typedef union pi_force_bad_check_bit_a_u {
	bdrkreg_t	pi_force_bad_check_bit_a_regval;
	struct  {
		bdrkreg_t	fbcba_bad_check_bit       :	 8;
		bdrkreg_t       fbcba_rsvd                :     56;
	} pi_force_bad_check_bit_a_fld_s;
} pi_force_bad_check_bit_a_u_t;

#else

typedef union pi_force_bad_check_bit_a_u {
	bdrkreg_t	pi_force_bad_check_bit_a_regval;
	struct	{
		bdrkreg_t	fbcba_rsvd		  :	56;
		bdrkreg_t	fbcba_bad_check_bit	  :	 8;
	} pi_force_bad_check_bit_a_fld_s;
} pi_force_bad_check_bit_a_u_t;

#endif




/************************************************************************
 *                                                                      *
 *  There is one of these registers for each CPU. If any bit in this    *
 * register is set, then whenever reply data arrives with the UE        *
 * (uncorrectable error) indication set, the check-bits that are        *
 * generated and sent to the SysAD will be inverted corresponding to    *
 * the bits set in the register. This will also prevent the assertion   *
 * of the data quality indicator.                                       *
 *                                                                      *
 ************************************************************************/




#ifdef LITTLE_ENDIAN

typedef union pi_force_bad_check_bit_b_u {
	bdrkreg_t	pi_force_bad_check_bit_b_regval;
	struct  {
		bdrkreg_t	fbcbb_bad_check_bit       :	 8;
		bdrkreg_t       fbcbb_rsvd                :     56;
	} pi_force_bad_check_bit_b_fld_s;
} pi_force_bad_check_bit_b_u_t;

#else

typedef union pi_force_bad_check_bit_b_u {
	bdrkreg_t	pi_force_bad_check_bit_b_regval;
	struct	{
		bdrkreg_t	fbcbb_rsvd		  :	56;
		bdrkreg_t	fbcbb_bad_check_bit	  :	 8;
	} pi_force_bad_check_bit_b_fld_s;
} pi_force_bad_check_bit_b_u_t;

#endif




/************************************************************************
 *                                                                      *
 *  There is one of these registers for each CPU. When a counter is     *
 * enabled, it increments each time a DNACK reply is received. The      *
 * counter is cleared when any other reply is received. The register    *
 * is cleared when the CNT_EN bit is zero. If a DNACK reply is          *
 * received when the counter equals the value in the NACK_CMP           *
 * register, the counter is cleared, an error response is sent to the   *
 * CPU instead of a nack response, and the NACK_INT_A/B bit is set in   *
 * INT_PEND1.                                                           *
 *                                                                      *
 ************************************************************************/




#ifdef LITTLE_ENDIAN

typedef union pi_nack_cnt_a_u {
	bdrkreg_t	pi_nack_cnt_a_regval;
	struct  {
		bdrkreg_t	nca_nack_cnt              :	20;
                bdrkreg_t       nca_cnt_en                :      1;
                bdrkreg_t       nca_rsvd                  :     43;
	} pi_nack_cnt_a_fld_s;
} pi_nack_cnt_a_u_t;

#else

typedef union pi_nack_cnt_a_u {
	bdrkreg_t	pi_nack_cnt_a_regval;
	struct	{
		bdrkreg_t	nca_rsvd		  :	43;
		bdrkreg_t	nca_cnt_en		  :	 1;
		bdrkreg_t	nca_nack_cnt		  :	20;
	} pi_nack_cnt_a_fld_s;
} pi_nack_cnt_a_u_t;

#endif




/************************************************************************
 *                                                                      *
 *  There is one of these registers for each CPU. When a counter is     *
 * enabled, it increments each time a DNACK reply is received. The      *
 * counter is cleared when any other reply is received. The register    *
 * is cleared when the CNT_EN bit is zero. If a DNACK reply is          *
 * received when the counter equals the value in the NACK_CMP           *
 * register, the counter is cleared, an error response is sent to the   *
 * CPU instead of a nack response, and the NACK_INT_A/B bit is set in   *
 * INT_PEND1.                                                           *
 *                                                                      *
 ************************************************************************/




#ifdef LITTLE_ENDIAN

typedef union pi_nack_cnt_b_u {
	bdrkreg_t	pi_nack_cnt_b_regval;
	struct  {
		bdrkreg_t	ncb_nack_cnt              :	20;
                bdrkreg_t       ncb_cnt_en                :      1;
                bdrkreg_t       ncb_rsvd                  :     43;
	} pi_nack_cnt_b_fld_s;
} pi_nack_cnt_b_u_t;

#else

typedef union pi_nack_cnt_b_u {
	bdrkreg_t	pi_nack_cnt_b_regval;
	struct	{
		bdrkreg_t	ncb_rsvd		  :	43;
		bdrkreg_t	ncb_cnt_en		  :	 1;
		bdrkreg_t	ncb_nack_cnt		  :	20;
	} pi_nack_cnt_b_fld_s;
} pi_nack_cnt_b_u_t;

#endif




/************************************************************************
 *                                                                      *
 *  The setting of this register affects both CPUs on this PI.          *
 *                                                                      *
 ************************************************************************/




#ifdef LITTLE_ENDIAN

typedef union pi_nack_cmp_u {
	bdrkreg_t	pi_nack_cmp_regval;
	struct  {
		bdrkreg_t	nc_nack_cmp               :	20;
		bdrkreg_t       nc_rsvd                   :     44;
	} pi_nack_cmp_fld_s;
} pi_nack_cmp_u_t;

#else

typedef union pi_nack_cmp_u {
	bdrkreg_t	pi_nack_cmp_regval;
	struct	{
		bdrkreg_t	nc_rsvd			  :	44;
		bdrkreg_t	nc_nack_cmp		  :	20;
	} pi_nack_cmp_fld_s;
} pi_nack_cmp_u_t;

#endif




/************************************************************************
 *                                                                      *
 *  This register controls which errors are spooled. When a bit in      *
 * this register is set, the corresponding error is spooled. The        *
 * setting of this register affects both CPUs on this PI.               *
 *                                                                      *
 ************************************************************************/




#ifdef LITTLE_ENDIAN

typedef union pi_spool_mask_u {
	bdrkreg_t	pi_spool_mask_regval;
	struct  {
		bdrkreg_t	sm_access_err             :	 1;
                bdrkreg_t       sm_uncached_err           :      1;
                bdrkreg_t       sm_dir_err                :      1;
                bdrkreg_t       sm_timeout_err            :      1;
                bdrkreg_t       sm_poison_err             :      1;
                bdrkreg_t       sm_nack_oflow_err         :      1;
                bdrkreg_t       sm_rsvd                   :     58;
	} pi_spool_mask_fld_s;
} pi_spool_mask_u_t;

#else

typedef union pi_spool_mask_u {
	bdrkreg_t	pi_spool_mask_regval;
	struct	{
		bdrkreg_t	sm_rsvd			  :	58;
		bdrkreg_t	sm_nack_oflow_err	  :	 1;
		bdrkreg_t	sm_poison_err		  :	 1;
		bdrkreg_t	sm_timeout_err		  :	 1;
		bdrkreg_t	sm_dir_err		  :	 1;
		bdrkreg_t	sm_uncached_err		  :	 1;
		bdrkreg_t	sm_access_err		  :	 1;
	} pi_spool_mask_fld_s;
} pi_spool_mask_u_t;

#endif




/************************************************************************
 *                                                                      *
 *  This register is not cleared at reset. When the VALID bit is        *
 * zero, this register (along with SPURIOUS_HDR_1) will capture the     *
 * header of an incoming spurious message received from the XBar. A     *
 * spurious message is a message that does not match up with any of     *
 * the CRB entries. This is a read/write register, so it is cleared     *
 * by writing of all zeros.                                             *
 *                                                                      *
 ************************************************************************/




#ifdef LITTLE_ENDIAN

typedef union pi_spurious_hdr_0_u {
	bdrkreg_t	pi_spurious_hdr_0_regval;
	struct  {
		bdrkreg_t	sh0_prev_valid_b          :	 1;
                bdrkreg_t       sh0_prev_valid_a          :      1;
                bdrkreg_t       sh0_rsvd                  :      4;
                bdrkreg_t       sh0_supplemental          :     11;
                bdrkreg_t       sh0_cmd                   :      8;
                bdrkreg_t       sh0_addr                  :     37;
                bdrkreg_t       sh0_tail                  :      1;
                bdrkreg_t       sh0_valid                 :      1;
	} pi_spurious_hdr_0_fld_s;
} pi_spurious_hdr_0_u_t;

#else

typedef union pi_spurious_hdr_0_u {
	bdrkreg_t	pi_spurious_hdr_0_regval;
	struct	{
		bdrkreg_t	sh0_valid		  :	 1;
		bdrkreg_t	sh0_tail		  :	 1;
		bdrkreg_t	sh0_addr		  :	37;
		bdrkreg_t	sh0_cmd			  :	 8;
		bdrkreg_t	sh0_supplemental	  :	11;
		bdrkreg_t	sh0_rsvd		  :	 4;
		bdrkreg_t	sh0_prev_valid_a	  :	 1;
		bdrkreg_t	sh0_prev_valid_b	  :	 1;
	} pi_spurious_hdr_0_fld_s;
} pi_spurious_hdr_0_u_t;

#endif




/************************************************************************
 *                                                                      *
 *  This register is not cleared at reset. When the VALID bit in        *
 * SPURIOUS_HDR_0 is zero, this register (along with SPURIOUS_HDR_0)    *
 * will capture the header of an incoming spurious message received     *
 * from the XBar. A spurious message is a message that does not match   *
 * up with any of the CRB entries. This is a read/write register, so    *
 * it is cleared by writing of all zeros.                               *
 *                                                                      *
 ************************************************************************/




#ifdef LITTLE_ENDIAN

typedef union pi_spurious_hdr_1_u {
	bdrkreg_t	pi_spurious_hdr_1_regval;
	struct  {
		bdrkreg_t	sh1_rsvd                  :	53;
		bdrkreg_t       sh1_source                :     11;
	} pi_spurious_hdr_1_fld_s;
} pi_spurious_hdr_1_u_t;

#else

typedef union pi_spurious_hdr_1_u {
	bdrkreg_t	pi_spurious_hdr_1_regval;
	struct	{
		bdrkreg_t	sh1_source		  :	11;
		bdrkreg_t	sh1_rsvd		  :	53;
	} pi_spurious_hdr_1_fld_s;
} pi_spurious_hdr_1_u_t;

#endif




/************************************************************************
 *                                                                      *
 * Description:  This register controls the injection of errors in      *
 * outbound SysAD transfers. When a write sets a bit in this            *
 * register, the PI logic is "armed" to inject that error. At the       *
 * first transfer of the specified type, the error is injected and      *
 * the bit in this register is cleared. Writing to this register does   *
 * not cause a transaction to occur. A bit in this register will        *
 * remain set until a transaction of the specified type occurs as a     *
 * result of normal system activity. This register can be polled to     *
 * determine if an error has been injected or is still "armed".         *
 * This register does not control injection of data quality bad         *
 * indicator on a data cycle. This type of error can be created by      *
 * reading from a memory location that has an uncorrectable ECC         *
 * error.                                                               *
 *                                                                      *
 ************************************************************************/




#ifdef LITTLE_ENDIAN

typedef union pi_err_inject_u {
	bdrkreg_t	pi_err_inject_regval;
	struct  {
		bdrkreg_t	ei_cmd_syscmd_par_a       :	 1;
                bdrkreg_t       ei_data_syscmd_par_a      :      1;
                bdrkreg_t       ei_cmd_sysad_corecc_a     :      1;
                bdrkreg_t       ei_data_sysad_corecc_a    :      1;
                bdrkreg_t       ei_cmd_sysad_uncecc_a     :      1;
                bdrkreg_t       ei_data_sysad_uncecc_a    :      1;
                bdrkreg_t       ei_sysresp_par_a          :      1;
                bdrkreg_t       ei_reserved_1             :     25;
                bdrkreg_t       ei_cmd_syscmd_par_b       :      1;
                bdrkreg_t       ei_data_syscmd_par_b      :      1;
                bdrkreg_t       ei_cmd_sysad_corecc_b     :      1;
                bdrkreg_t       ei_data_sysad_corecc_b    :      1;
                bdrkreg_t       ei_cmd_sysad_uncecc_b     :      1;
                bdrkreg_t       ei_data_sysad_uncecc_b    :      1;
                bdrkreg_t       ei_sysresp_par_b          :      1;
                bdrkreg_t       ei_reserved               :     25;
	} pi_err_inject_fld_s;
} pi_err_inject_u_t;

#else

typedef union pi_err_inject_u {
	bdrkreg_t	pi_err_inject_regval;
	struct	{
		bdrkreg_t	ei_reserved		  :	25;
		bdrkreg_t	ei_sysresp_par_b	  :	 1;
		bdrkreg_t	ei_data_sysad_uncecc_b	  :	 1;
		bdrkreg_t	ei_cmd_sysad_uncecc_b	  :	 1;
		bdrkreg_t	ei_data_sysad_corecc_b	  :	 1;
		bdrkreg_t	ei_cmd_sysad_corecc_b	  :	 1;
		bdrkreg_t	ei_data_syscmd_par_b	  :	 1;
		bdrkreg_t	ei_cmd_syscmd_par_b	  :	 1;
		bdrkreg_t	ei_reserved_1		  :	25;
		bdrkreg_t	ei_sysresp_par_a	  :	 1;
		bdrkreg_t	ei_data_sysad_uncecc_a	  :	 1;
		bdrkreg_t	ei_cmd_sysad_uncecc_a	  :	 1;
		bdrkreg_t	ei_data_sysad_corecc_a	  :	 1;
		bdrkreg_t	ei_cmd_sysad_corecc_a	  :	 1;
		bdrkreg_t	ei_data_syscmd_par_a	  :	 1;
		bdrkreg_t	ei_cmd_syscmd_par_a	  :	 1;
	} pi_err_inject_fld_s;
} pi_err_inject_u_t;

#endif




/************************************************************************
 *                                                                      *
 *  This Read/Write location determines at what point the TRex+ is      *
 * stopped from issuing requests, based on the number of entries in     *
 * the incoming reply FIFO. When the number of entries in the Reply     *
 * FIFO is greater than the value of this register, the PI will         *
 * deassert both SysWrRdy and SysRdRdy to both processors. The Reply    *
 * FIFO has a depth of 0x3F entries, so setting this register to 0x3F   *
 * effectively disables this feature, allowing requests to be issued    *
 * always. Setting this register to 0x00 effectively lowers the         *
 * TRex+'s priority below the reply FIFO, disabling TRex+ requests      *
 * any time there is an entry waiting in the incoming FIFO.This         *
 * register is in its own 64KB page so that it can be mapped to user    *
 * space.                                                               *
 *                                                                      *
 ************************************************************************/




#ifdef LITTLE_ENDIAN

typedef union pi_reply_level_u {
	bdrkreg_t	pi_reply_level_regval;
	struct  {
		bdrkreg_t	rl_reply_level            :	 6;
		bdrkreg_t	rl_rsvd			  :	58;
	} pi_reply_level_fld_s;
} pi_reply_level_u_t;

#else

typedef union pi_reply_level_u {
	bdrkreg_t	pi_reply_level_regval;
	struct	{
		bdrkreg_t	rl_rsvd			  :	58;
		bdrkreg_t	rl_reply_level		  :	 6;
	} pi_reply_level_fld_s;
} pi_reply_level_u_t;

#endif




/************************************************************************
 *                                                                      *
 *  This register is used to change the graphics credit counter         *
 * operation from "Doubleword" mode to "Transaction" mode. This         *
 * register is in its own 64KB page so that it can be mapped to user    *
 * space.                                                               *
 *                                                                      *
 ************************************************************************/




#ifdef LITTLE_ENDIAN

typedef union pi_gfx_credit_mode_u {
	bdrkreg_t	pi_gfx_credit_mode_regval;
	struct  {
		bdrkreg_t	gcm_trans_mode            :	 1;
		bdrkreg_t       gcm_rsvd                  :     63;
	} pi_gfx_credit_mode_fld_s;
} pi_gfx_credit_mode_u_t;

#else

typedef union pi_gfx_credit_mode_u {
	bdrkreg_t	pi_gfx_credit_mode_regval;
	struct	{
		bdrkreg_t	gcm_rsvd		  :	63;
		bdrkreg_t	gcm_trans_mode		  :	 1;
	} pi_gfx_credit_mode_fld_s;
} pi_gfx_credit_mode_u_t;

#endif



/************************************************************************
 *                                                                      *
 *  This location contains a 55-bit read/write counter that wraps to    *
 * zero when the maximum value is reached. This counter is              *
 * incremented at each rising edge of the global clock (GCLK). This     *
 * register is in its own 64KB page so that it can be mapped to user    *
 * space.                                                               *
 *                                                                      *
 ************************************************************************/




#ifdef LITTLE_ENDIAN

typedef union pi_rt_counter_u {
	bdrkreg_t	pi_rt_counter_regval;
	struct  {
		bdrkreg_t	rc_count                  :	55;
		bdrkreg_t       rc_rsvd                   :      9;
	} pi_rt_counter_fld_s;
} pi_rt_counter_u_t;

#else

typedef union pi_rt_counter_u {
	bdrkreg_t	pi_rt_counter_regval;
	struct	{
		bdrkreg_t	rc_rsvd			  :	 9;
		bdrkreg_t	rc_count		  :	55;
	} pi_rt_counter_fld_s;
} pi_rt_counter_u_t;

#endif




/************************************************************************
 *                                                                      *
 *  This register controls the performance counters for one CPU.        *
 * There are two counters for each CPU. Each counter can be             *
 * configured to count a variety of events. The performance counter     *
 * registers for each processor are in their own 64KB page so that      *
 * they can be mapped to user space.                                    *
 *                                                                      *
 ************************************************************************/




#ifdef LITTLE_ENDIAN

typedef union pi_perf_cntl_a_u {
	bdrkreg_t	pi_perf_cntl_a_regval;
	struct  {
		bdrkreg_t	pca_cntr_0_select         :	28;
                bdrkreg_t       pca_cntr_0_mode           :      3;
                bdrkreg_t       pca_cntr_0_enable         :      1;
                bdrkreg_t       pca_cntr_1_select         :     28;
                bdrkreg_t       pca_cntr_1_mode           :      3;
                bdrkreg_t       pca_cntr_1_enable         :      1;
	} pi_perf_cntl_a_fld_s;
} pi_perf_cntl_a_u_t;

#else

typedef union pi_perf_cntl_a_u {
	bdrkreg_t	pi_perf_cntl_a_regval;
	struct	{
		bdrkreg_t	pca_cntr_1_enable	  :	 1;
		bdrkreg_t	pca_cntr_1_mode		  :	 3;
		bdrkreg_t	pca_cntr_1_select	  :	28;
		bdrkreg_t	pca_cntr_0_enable	  :	 1;
		bdrkreg_t	pca_cntr_0_mode		  :	 3;
		bdrkreg_t	pca_cntr_0_select	  :	28;
	} pi_perf_cntl_a_fld_s;
} pi_perf_cntl_a_u_t;

#endif




/************************************************************************
 *                                                                      *
 *  This register accesses the performance counter 0 for each CPU.      *
 * Each performance counter is 40-bits wide. On overflow, It wraps to   *
 * zero, sets the overflow bit in this register, and sets the           *
 * PERF_CNTR_OFLOW bit in the INT_PEND1 register.                       *
 *                                                                      *
 ************************************************************************/




#ifdef LITTLE_ENDIAN

typedef union pi_perf_cntr0_a_u {
	bdrkreg_t	pi_perf_cntr0_a_regval;
	struct  {
		bdrkreg_t	pca_count_value           :	40;
                bdrkreg_t       pca_overflow              :      1;
                bdrkreg_t       pca_rsvd                  :     23;
	} pi_perf_cntr0_a_fld_s;
} pi_perf_cntr0_a_u_t;

#else

typedef union pi_perf_cntr0_a_u {
	bdrkreg_t	pi_perf_cntr0_a_regval;
	struct	{
		bdrkreg_t	pca_rsvd		  :	23;
		bdrkreg_t	pca_overflow		  :	 1;
		bdrkreg_t	pca_count_value		  :	40;
	} pi_perf_cntr0_a_fld_s;
} pi_perf_cntr0_a_u_t;

#endif




/************************************************************************
 *                                                                      *
 *  This register accesses the performance counter 1for each CPU.       *
 * Each performance counter is 40-bits wide. On overflow, It wraps to   *
 * zero, sets the overflow bit in this register, and sets the           *
 * PERF_CNTR_OFLOW bit in the INT_PEND1 register.                       *
 *                                                                      *
 ************************************************************************/




#ifdef LITTLE_ENDIAN

typedef union pi_perf_cntr1_a_u {
	bdrkreg_t	pi_perf_cntr1_a_regval;
	struct  {
		bdrkreg_t	pca_count_value           :	40;
                bdrkreg_t       pca_overflow              :      1;
                bdrkreg_t       pca_rsvd                  :     23;
	} pi_perf_cntr1_a_fld_s;
} pi_perf_cntr1_a_u_t;

#else

typedef union pi_perf_cntr1_a_u {
	bdrkreg_t	pi_perf_cntr1_a_regval;
	struct	{
		bdrkreg_t	pca_rsvd		  :	23;
		bdrkreg_t	pca_overflow		  :	 1;
		bdrkreg_t	pca_count_value		  :	40;
	} pi_perf_cntr1_a_fld_s;
} pi_perf_cntr1_a_u_t;

#endif





/************************************************************************
 *                                                                      *
 *  This register controls the performance counters for one CPU.        *
 * There are two counters for each CPU. Each counter can be             *
 * configured to count a variety of events. The performance counter     *
 * registers for each processor are in their own 64KB page so that      *
 * they can be mapped to user space.                                    *
 *                                                                      *
 ************************************************************************/




#ifdef LITTLE_ENDIAN

typedef union pi_perf_cntl_b_u {
	bdrkreg_t	pi_perf_cntl_b_regval;
	struct  {
		bdrkreg_t	pcb_cntr_0_select         :	28;
                bdrkreg_t       pcb_cntr_0_mode           :      3;
                bdrkreg_t       pcb_cntr_0_enable         :      1;
                bdrkreg_t       pcb_cntr_1_select         :     28;
                bdrkreg_t       pcb_cntr_1_mode           :      3;
                bdrkreg_t       pcb_cntr_1_enable         :      1;
	} pi_perf_cntl_b_fld_s;
} pi_perf_cntl_b_u_t;

#else

typedef union pi_perf_cntl_b_u {
	bdrkreg_t	pi_perf_cntl_b_regval;
	struct	{
		bdrkreg_t	pcb_cntr_1_enable	  :	 1;
		bdrkreg_t	pcb_cntr_1_mode		  :	 3;
		bdrkreg_t	pcb_cntr_1_select	  :	28;
		bdrkreg_t	pcb_cntr_0_enable	  :	 1;
		bdrkreg_t	pcb_cntr_0_mode		  :	 3;
		bdrkreg_t	pcb_cntr_0_select	  :	28;
	} pi_perf_cntl_b_fld_s;
} pi_perf_cntl_b_u_t;

#endif




/************************************************************************
 *                                                                      *
 *  This register accesses the performance counter 0 for each CPU.      *
 * Each performance counter is 40-bits wide. On overflow, It wraps to   *
 * zero, sets the overflow bit in this register, and sets the           *
 * PERF_CNTR_OFLOW bit in the INT_PEND1 register.                       *
 *                                                                      *
 ************************************************************************/




#ifdef LITTLE_ENDIAN

typedef union pi_perf_cntr0_b_u {
	bdrkreg_t	pi_perf_cntr0_b_regval;
	struct  {
		bdrkreg_t	pcb_count_value           :	40;
                bdrkreg_t       pcb_overflow              :      1;
                bdrkreg_t       pcb_rsvd                  :     23;
	} pi_perf_cntr0_b_fld_s;
} pi_perf_cntr0_b_u_t;

#else

typedef union pi_perf_cntr0_b_u {
	bdrkreg_t	pi_perf_cntr0_b_regval;
	struct	{
		bdrkreg_t	pcb_rsvd		  :	23;
		bdrkreg_t	pcb_overflow		  :	 1;
		bdrkreg_t	pcb_count_value		  :	40;
	} pi_perf_cntr0_b_fld_s;
} pi_perf_cntr0_b_u_t;

#endif




/************************************************************************
 *                                                                      *
 *  This register accesses the performance counter 1for each CPU.       *
 * Each performance counter is 40-bits wide. On overflow, It wraps to   *
 * zero, sets the overflow bit in this register, and sets the           *
 * PERF_CNTR_OFLOW bit in the INT_PEND1 register.                       *
 *                                                                      *
 ************************************************************************/




#ifdef LITTLE_ENDIAN

typedef union pi_perf_cntr1_b_u {
	bdrkreg_t	pi_perf_cntr1_b_regval;
	struct  {
		bdrkreg_t	pcb_count_value           :	40;
                bdrkreg_t       pcb_overflow              :      1;
                bdrkreg_t       pcb_rsvd                  :     23;
	} pi_perf_cntr1_b_fld_s;
} pi_perf_cntr1_b_u_t;

#else

typedef union pi_perf_cntr1_b_u {
	bdrkreg_t	pi_perf_cntr1_b_regval;
	struct	{
		bdrkreg_t	pcb_rsvd		  :	23;
		bdrkreg_t	pcb_overflow		  :	 1;
		bdrkreg_t	pcb_count_value		  :	40;
	} pi_perf_cntr1_b_fld_s;
} pi_perf_cntr1_b_u_t;

#endif






#endif /* _LANGUAGE_C */

/************************************************************************
 *                                                                      *
 *               MAKE ALL ADDITIONS AFTER THIS LINE                     *
 *                                                                      *
 ************************************************************************/


#define PI_GFX_OFFSET		(PI_GFX_PAGE_B - PI_GFX_PAGE_A)
#define PI_GFX_PAGE_ENABLE	0x0000010000000000LL


#endif /* _ASM_SN_SN1_HUBPI_H */
