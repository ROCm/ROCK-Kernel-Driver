/* $Id$
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1992 - 1997, 2000 Silicon Graphics, Inc.
 * Copyright (C) 2000 by Colin Ngam
 */
#ifndef _ASM_SN_SN1_HUBMD_H
#define _ASM_SN_SN1_HUBMD_H


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


#define    MD_CURRENT_CELL           0x00780000    /*
                                                    * BDDIR, LREG, LBOOT,
                                                    * RREG, RBOOT
                                                    * protection and mask
                                                    * for using Local
                                                    * Access protection.
                                                    */



#define    MD_MEMORY_CONFIG          0x00780008    /*
                                                    * Memory/Directory
                                                    * DIMM control
                                                    */



#define    MD_ARBITRATION_CONTROL    0x00780010    /*
                                                    * Arbitration
                                                    * Parameters
                                                    */



#define    MD_MIG_CONFIG             0x00780018    /*
                                                    * Page Migration
                                                    * control
                                                    */



#define    MD_FANDOP_CAC_STAT0       0x00780020    /*
                                                    * Fetch-and-op cache
                                                    * 0 status
                                                    */



#define    MD_FANDOP_CAC_STAT1       0x00780028    /*
                                                    * Fetch-and-op cache
                                                    * 1 status
                                                    */



#define    MD_MISC0_ERROR            0x00780040    /*
                                                    * Miscellaneous MD
                                                    * error
                                                    */



#define    MD_MISC1_ERROR            0x00780048    /*
                                                    * Miscellaneous MD
                                                    * error
                                                    */



#define    MD_MISC1_ERROR_CLR        0x00780058    /*
                                                    * Miscellaneous MD
                                                    * error clear
                                                    */



#define    MD_OUTGOING_RP_QUEUE_SIZE 0x00780060    /*
                                                    * MD outgoing reply
                                                    * queues sizing
                                                    */



#define    MD_PERF_SEL0              0x00790000    /*
                                                    * Selects events
                                                    * monitored by
                                                    * MD_PERF_CNT0
                                                    */



#define    MD_PERF_SEL1              0x00790008    /*
                                                    * Selects events
                                                    * monitored by
                                                    * MD_PERF_CNT1
                                                    */



#define    MD_PERF_CNT0              0x00790010    /*
                                                    * Performance counter
                                                    * 0
                                                    */



#define    MD_PERF_CNT1              0x00790018    /*
                                                    * Performance counter
                                                    * 1
                                                    */



#define    MD_REFRESH_CONTROL        0x007A0000    /*
                                                    * Memory/Directory
                                                    * refresh control
                                                    */



#define    MD_JUNK_BUS_TIMING        0x007A0008    /* Junk Bus Timing        */



#define    MD_LED0                   0x007A0010    /* Reads of 8-bit LED0    */



#define    MD_LED1                   0x007A0018    /* Reads of 8-bit LED1    */



#define    MD_LED2                   0x007A0020    /* Reads of 8-bit LED2    */



#define    MD_LED3                   0x007A0028    /* Reads of 8-bit LED3    */



#define    MD_BIST_CTL               0x007A0030    /*
                                                    * BIST general
                                                    * control
                                                    */



#define    MD_BIST_DATA              0x007A0038    /*
                                                    * BIST initial data
                                                    * pattern and
                                                    * variation control
                                                    */



#define    MD_BIST_AB_ERR_ADDR       0x007A0040    /* BIST error address     */



#define    MD_BIST_STATUS            0x007A0048    /* BIST status            */



#define    MD_IB_DEBUG               0x007A0060    /* IB debug select        */



#define    MD_DIR_CONFIG             0x007C0000    /*
                                                    * Directory mode
                                                    * control
                                                    */



#define    MD_DIR_ERROR              0x007C0010    /*
                                                    * Directory DIMM
                                                    * error
                                                    */



#define    MD_DIR_ERROR_CLR          0x007C0018    /*
                                                    * Directory DIMM
                                                    * error clear
                                                    */



#define    MD_PROTOCOL_ERROR         0x007C0020    /*
                                                    * Directory protocol
                                                    * error
                                                    */



#define    MD_PROTOCOL_ERR_CLR       0x007C0028    /*
                                                    * Directory protocol
                                                    * error clear
                                                    */



#define    MD_MIG_CANDIDATE          0x007C0030    /*
                                                    * Page migration
                                                    * candidate
                                                    */



#define    MD_MIG_CANDIDATE_CLR      0x007C0038    /*
                                                    * Page migration
                                                    * candidate clear
                                                    */



#define    MD_MIG_DIFF_THRESH        0x007C0040    /*
                                                    * Page migration
                                                    * count difference
                                                    * threshold
                                                    */



#define    MD_MIG_VALUE_THRESH       0x007C0048    /*
                                                    * Page migration
                                                    * count absolute
                                                    * threshold
                                                    */



#define    MD_OUTGOING_RQ_QUEUE_SIZE 0x007C0050    /*
                                                    * MD outgoing request
                                                    * queues sizing
                                                    */



#define    MD_BIST_DB_ERR_DATA       0x007C0058    /*
                                                    * BIST directory
                                                    * error data
                                                    */



#define    MD_DB_DEBUG               0x007C0060    /* DB debug select        */



#define    MD_MB_ECC_CONFIG          0x007E0000    /*
                                                    * Data ECC
                                                    * Configuration
                                                    */



#define    MD_MEM_ERROR              0x007E0010    /* Memory DIMM error      */



#define    MD_MEM_ERROR_CLR          0x007E0018    /*
                                                    * Memory DIMM error
                                                    * clear
                                                    */



#define    MD_BIST_MB_ERR_DATA_0     0x007E0020    /*
                                                    * BIST memory error
                                                    * data
                                                    */



#define    MD_BIST_MB_ERR_DATA_1     0x007E0028    /*
                                                    * BIST memory error
                                                    * data
                                                    */



#define    MD_BIST_MB_ERR_DATA_2     0x007E0030    /*
                                                    * BIST memory error
                                                    * data
                                                    */



#define    MD_BIST_MB_ERR_DATA_3     0x007E0038    /*
                                                    * BIST memory error
                                                    * data
                                                    */



#define    MD_MB_DEBUG               0x007E0040    /* MB debug select        */





#ifdef _LANGUAGE_C

/************************************************************************
 *                                                                      *
 * Description:  This register shows which regions are in the current   *
 * cell. If a region has its bit set in this register, then it uses     *
 * the Local Access protection in the directory instead of the          *
 * separate per-region protection (which would cause a small            *
 * performance penalty). In addition, writeback and write reply         *
 * commands from outside the current cell will always check the         *
 * directory protection before writing data to memory. Writeback and    *
 * write reply commands from inside the current cell will write         *
 * memory regardless of the protection value.                           *
 * This register is also used as the access-rights bit-vector for       *
 * most of the ASIC-special (HSpec) portion of the address space. It    *
 * covers the BDDIR, LREG, LBOOT, RREG, and RBOOT spaces. It does not   *
 * cover the UALIAS and BDECC spaces, as they are covered by the        *
 * protection in the directory. If a bit in the bit-vector is set,      *
 * the region corresponding to that bit has read/write permission on    *
 * these spaces. If the bit is clear, then that region has read-only    *
 * access to these spaces (except for LREG/RREG which have no access    *
 * when the bit is clear).                                              *
 * The granularity of a region is set by the REGION_SIZE register in    *
 * the NI local register space.                                         *
 * NOTE: This means that no processor outside the current cell can      *
 * write into the BDDIR, LREG, LBOOT, RREG, or RBOOT spaces.            *
 *                                                                      *
 ************************************************************************/




typedef union md_current_cell_u {
	bdrkreg_t	md_current_cell_regval;
	struct  {
		bdrkreg_t	cc_hspec_prot             :	64;
	} md_current_cell_fld_s;
} md_current_cell_u_t;




/************************************************************************
 *                                                                      *
 * Description:  This register contains three sets of information.      *
 * The first set describes the size and configuration of DIMMs that     *
 * are plugged into a system, the second set controls which set of      *
 * protection checks are performed on each access and the third set     *
 * controls various DDR SDRAM timing parameters.                        *
 * In order to config a DIMM bank, three fields must be initialized:    *
 * BANK_SIZE, DRAM_WIDTH, and BANK_ENABLE. The BANK_SIZE field sets     *
 * the address range that the MD unit will accept for that DIMM bank.   *
 * All addresses larger than the specified size will return errors on   *
 * access. In order to read from a DIMM bank, Bedrock must know         *
 * whether or not the bank contains x4 or x8/x16 DRAM. The operating    *
 * system must query the System Controller for this information and     *
 * then set the DRAM_WIDTH field accordingly. The BANK_ENABLE field     *
 * can be used to individually enable the two physical banks located    *
 * on each DIMM bank.                                                   *
 * The contents of this register are preserved through soft-resets.     *
 *                                                                      *
 ************************************************************************/




#ifdef LITTLE_ENDIAN

typedef union md_memory_config_u {
	bdrkreg_t	md_memory_config_regval;
	struct  {
		bdrkreg_t	mc_dimm0_bank_enable      :	 2;
		bdrkreg_t       mc_reserved_7             :      1;
		bdrkreg_t       mc_dimm0_dram_width       :      1;
		bdrkreg_t       mc_dimm0_bank_size        :      4;
		bdrkreg_t       mc_dimm1_bank_enable      :      2;
		bdrkreg_t       mc_reserved_6             :      1;
		bdrkreg_t       mc_dimm1_dram_width       :      1;
		bdrkreg_t       mc_dimm1_bank_size        :      4;
                bdrkreg_t       mc_dimm2_bank_enable      :      2;
                bdrkreg_t       mc_reserved_5             :      1;
                bdrkreg_t       mc_dimm2_dram_width       :      1;
                bdrkreg_t       mc_dimm2_bank_size        :      4;
                bdrkreg_t       mc_dimm3_bank_enable      :      2;
                bdrkreg_t       mc_reserved_4             :      1;
                bdrkreg_t       mc_dimm3_dram_width       :      1;
                bdrkreg_t       mc_dimm3_bank_size        :      4;
                bdrkreg_t       mc_dimm0_sel              :      2;
                bdrkreg_t       mc_reserved_3             :     10;
                bdrkreg_t       mc_cc_enable              :      1;
                bdrkreg_t       mc_io_prot_en             :      1;
                bdrkreg_t       mc_io_prot_ignore         :      1;
                bdrkreg_t       mc_cpu_prot_ignore        :      1;
                bdrkreg_t       mc_db_neg_edge            :      1;
                bdrkreg_t       mc_phase_delay            :      1;
                bdrkreg_t       mc_delay_mux_sel          :      2;
                bdrkreg_t       mc_sample_time            :      2;
                bdrkreg_t       mc_reserved_2             :      2;
                bdrkreg_t       mc_mb_neg_edge            :      3;
                bdrkreg_t       mc_reserved_1             :      1;
                bdrkreg_t       mc_rcd_config             :      1;
                bdrkreg_t       mc_rp_config              :      1;
                bdrkreg_t       mc_reserved               :      2;
	} md_memory_config_fld_s;
} md_memory_config_u_t;

#else

typedef union md_memory_config_u {
	bdrkreg_t	md_memory_config_regval;
	struct	{
		bdrkreg_t	mc_reserved		  :	 2;
		bdrkreg_t	mc_rp_config		  :	 1;
		bdrkreg_t	mc_rcd_config		  :	 1;
		bdrkreg_t	mc_reserved_1		  :	 1;
		bdrkreg_t	mc_mb_neg_edge		  :	 3;
		bdrkreg_t	mc_reserved_2		  :	 2;
		bdrkreg_t	mc_sample_time		  :	 2;
		bdrkreg_t	mc_delay_mux_sel	  :	 2;
		bdrkreg_t	mc_phase_delay		  :	 1;
		bdrkreg_t	mc_db_neg_edge		  :	 1;
		bdrkreg_t	mc_cpu_prot_ignore	  :	 1;
		bdrkreg_t	mc_io_prot_ignore	  :	 1;
		bdrkreg_t	mc_io_prot_en		  :	 1;
		bdrkreg_t	mc_cc_enable		  :	 1;
		bdrkreg_t	mc_reserved_3		  :	10;
		bdrkreg_t	mc_dimm0_sel		  :	 2;
		bdrkreg_t	mc_dimm3_bank_size	  :	 4;
		bdrkreg_t	mc_dimm3_dram_width	  :	 1;
		bdrkreg_t	mc_reserved_4		  :	 1;
		bdrkreg_t	mc_dimm3_bank_enable	  :	 2;
		bdrkreg_t	mc_dimm2_bank_size	  :	 4;
		bdrkreg_t	mc_dimm2_dram_width	  :	 1;
		bdrkreg_t	mc_reserved_5		  :	 1;
		bdrkreg_t	mc_dimm2_bank_enable	  :	 2;
		bdrkreg_t	mc_dimm1_bank_size	  :	 4;
		bdrkreg_t	mc_dimm1_dram_width	  :	 1;
		bdrkreg_t	mc_reserved_6		  :	 1;
		bdrkreg_t	mc_dimm1_bank_enable	  :	 2;
		bdrkreg_t	mc_dimm0_bank_size	  :	 4;
		bdrkreg_t	mc_dimm0_dram_width	  :	 1;
		bdrkreg_t	mc_reserved_7		  :	 1;
		bdrkreg_t	mc_dimm0_bank_enable	  :	 2;
	} md_memory_config_fld_s;
} md_memory_config_u_t;

#endif






#ifdef LITTLE_ENDIAN

typedef union md_arbitration_control_u {
	bdrkreg_t	md_arbitration_control_regval;
	struct  {
		bdrkreg_t	ac_reply_guar             :	 4;
		bdrkreg_t       ac_write_guar             :      4;
		bdrkreg_t       ac_reserved               :     56;
	} md_arbitration_control_fld_s;
} md_arbitration_control_u_t;

#else

typedef union md_arbitration_control_u {
	bdrkreg_t	md_arbitration_control_regval;
	struct	{
		bdrkreg_t	ac_reserved		  :	56;
		bdrkreg_t	ac_write_guar		  :	 4;
		bdrkreg_t	ac_reply_guar		  :	 4;
	} md_arbitration_control_fld_s;
} md_arbitration_control_u_t;

#endif




/************************************************************************
 *                                                                      *
 *  Contains page migration control fields.                             *
 *                                                                      *
 ************************************************************************/




#ifdef LITTLE_ENDIAN

typedef union md_mig_config_u {
	bdrkreg_t	md_mig_config_regval;
	struct  {
		bdrkreg_t	mc_mig_interval           :	10;
		bdrkreg_t       mc_reserved_2             :      6;
		bdrkreg_t       mc_mig_node_mask          :      8;
		bdrkreg_t       mc_reserved_1             :      8;
		bdrkreg_t       mc_mig_enable             :      1;
		bdrkreg_t       mc_reserved               :     31;
	} md_mig_config_fld_s;
} md_mig_config_u_t;

#else

typedef union md_mig_config_u {
	bdrkreg_t	md_mig_config_regval;
	struct	{
		bdrkreg_t	mc_reserved		  :	31;
		bdrkreg_t	mc_mig_enable		  :	 1;
		bdrkreg_t	mc_reserved_1		  :	 8;
		bdrkreg_t	mc_mig_node_mask	  :	 8;
		bdrkreg_t	mc_reserved_2		  :	 6;
		bdrkreg_t	mc_mig_interval		  :	10;
	} md_mig_config_fld_s;
} md_mig_config_u_t;

#endif




/************************************************************************
 *                                                                      *
 *  Each register contains the valid bit and address of the entry in    *
 * the fetch-and-op for cache 0 (or 1).                                 *
 *                                                                      *
 ************************************************************************/




#ifdef LITTLE_ENDIAN

typedef union md_fandop_cac_stat0_u {
	bdrkreg_t	md_fandop_cac_stat0_regval;
	struct  {
		bdrkreg_t	fcs_reserved_1            :	 6;
		bdrkreg_t       fcs_addr                  :     27;
		bdrkreg_t       fcs_reserved              :     30;
		bdrkreg_t       fcs_valid                 :      1;
	} md_fandop_cac_stat0_fld_s;
} md_fandop_cac_stat0_u_t;

#else

typedef union md_fandop_cac_stat0_u {
	bdrkreg_t	md_fandop_cac_stat0_regval;
	struct	{
		bdrkreg_t	fcs_valid		  :	 1;
		bdrkreg_t	fcs_reserved		  :	30;
		bdrkreg_t	fcs_addr		  :	27;
		bdrkreg_t	fcs_reserved_1		  :	 6;
	} md_fandop_cac_stat0_fld_s;
} md_fandop_cac_stat0_u_t;

#endif




/************************************************************************
 *                                                                      *
 *  Each register contains the valid bit and address of the entry in    *
 * the fetch-and-op for cache 0 (or 1).                                 *
 *                                                                      *
 ************************************************************************/




#ifdef LITTLE_ENDIAN

typedef union md_fandop_cac_stat1_u {
	bdrkreg_t	md_fandop_cac_stat1_regval;
	struct  {
		bdrkreg_t	fcs_reserved_1            :	 6;
		bdrkreg_t       fcs_addr                  :     27;
		bdrkreg_t       fcs_reserved              :     30;
		bdrkreg_t       fcs_valid                 :      1;
	} md_fandop_cac_stat1_fld_s;
} md_fandop_cac_stat1_u_t;

#else

typedef union md_fandop_cac_stat1_u {
	bdrkreg_t	md_fandop_cac_stat1_regval;
	struct	{
		bdrkreg_t	fcs_valid		  :	 1;
		bdrkreg_t	fcs_reserved		  :	30;
		bdrkreg_t	fcs_addr		  :	27;
		bdrkreg_t	fcs_reserved_1		  :	 6;
	} md_fandop_cac_stat1_fld_s;
} md_fandop_cac_stat1_u_t;

#endif




/************************************************************************
 *                                                                      *
 * Description:  Contains a number of fields to capture various         *
 * random memory/directory errors. For each 2-bit field, the LSB        *
 * indicates that additional information has been captured for the      *
 * error and the MSB indicates overrun, thus:                           *
 *  x1: bits 51...0 of this register contain additional information     *
 * for the message that caused this error                               *
 *  1x: overrun occurred                                                *
 *                                                                      *
 ************************************************************************/




#ifdef LITTLE_ENDIAN

typedef union md_misc0_error_u {
	bdrkreg_t	md_misc0_error_regval;
	struct	{
		bdrkreg_t	me_command		  :	 7;
                bdrkreg_t       me_reserved_4             :      1;
                bdrkreg_t       me_source                 :     11;
                bdrkreg_t       me_reserved_3             :      1;
                bdrkreg_t       me_suppl                  :     11;
                bdrkreg_t       me_reserved_2             :      1;
                bdrkreg_t       me_virtual_channel        :      2;
                bdrkreg_t       me_reserved_1             :      2;
                bdrkreg_t       me_tail                   :      1;
                bdrkreg_t       me_reserved               :     11;
                bdrkreg_t       me_xb_error               :      4;
                bdrkreg_t       me_bad_partial_data       :      2;
                bdrkreg_t       me_missing_dv             :      2;
                bdrkreg_t       me_short_pack             :      2;
                bdrkreg_t       me_long_pack              :      2;
                bdrkreg_t       me_ill_msg                :      2;
                bdrkreg_t       me_ill_revision           :      2;
	} md_misc0_error_fld_s;
} md_misc0_error_u_t;

#else

typedef union md_misc0_error_u {
	bdrkreg_t	md_misc0_error_regval;
	struct  {
		bdrkreg_t	me_ill_revision           :	 2;
		bdrkreg_t	me_ill_msg                :	 2;
		bdrkreg_t	me_long_pack              :	 2;
		bdrkreg_t	me_short_pack             :	 2;
		bdrkreg_t	me_missing_dv             :	 2;
		bdrkreg_t	me_bad_partial_data       :	 2;
		bdrkreg_t	me_xb_error               :	 4;
		bdrkreg_t	me_reserved               :	11;
		bdrkreg_t	me_tail                   :	 1;
		bdrkreg_t	me_reserved_1             :	 2;
		bdrkreg_t	me_virtual_channel        :	 2;
		bdrkreg_t	me_reserved_2             :	 1;
		bdrkreg_t	me_suppl                  :	11;
		bdrkreg_t	me_reserved_3             :	 1;
		bdrkreg_t	me_source                 :	11;
		bdrkreg_t	me_reserved_4             :	 1;
		bdrkreg_t	me_command                :	 7;
	} md_misc0_error_fld_s;
} md_misc0_error_u_t;

#endif




/************************************************************************
 *                                                                      *
 *  Address for error captured in MISC0_ERROR. Error valid bits are     *
 * repeated in both MISC0_ERROR and MISC1_ERROR (allowing them to be    *
 * read sequentially without missing any errors).                       *
 *                                                                      *
 ************************************************************************/




#ifdef LITTLE_ENDIAN

typedef union md_misc1_error_u {
	bdrkreg_t	md_misc1_error_regval;
	struct  {
		bdrkreg_t	me_reserved_1             :	 3;
		bdrkreg_t       me_address                :     38;
		bdrkreg_t       me_reserved               :      7;
		bdrkreg_t       me_xb_error               :      4;
		bdrkreg_t       me_bad_partial_data       :      2;
		bdrkreg_t       me_missing_dv             :      2;
		bdrkreg_t       me_short_pack             :      2;
		bdrkreg_t       me_long_pack              :      2;
		bdrkreg_t       me_ill_msg                :      2;
		bdrkreg_t       me_ill_revision           :      2;
	} md_misc1_error_fld_s;
} md_misc1_error_u_t;

#else

typedef union md_misc1_error_u {
	bdrkreg_t	md_misc1_error_regval;
	struct	{
		bdrkreg_t	me_ill_revision		  :	 2;
		bdrkreg_t	me_ill_msg		  :	 2;
		bdrkreg_t	me_long_pack		  :	 2;
		bdrkreg_t	me_short_pack		  :	 2;
		bdrkreg_t	me_missing_dv		  :	 2;
		bdrkreg_t	me_bad_partial_data	  :	 2;
		bdrkreg_t	me_xb_error		  :	 4;
		bdrkreg_t	me_reserved		  :	 7;
		bdrkreg_t	me_address		  :	38;
		bdrkreg_t	me_reserved_1		  :	 3;
	} md_misc1_error_fld_s;
} md_misc1_error_u_t;

#endif




/************************************************************************
 *                                                                      *
 *  Address for error captured in MISC0_ERROR. Error valid bits are     *
 * repeated in both MISC0_ERROR and MISC1_ERROR (allowing them to be    *
 * read sequentially without missing any errors).                       *
 *                                                                      *
 ************************************************************************/




#ifdef LITTLE_ENDIAN

typedef union md_misc1_error_clr_u {
	bdrkreg_t	md_misc1_error_clr_regval;
	struct  {
		bdrkreg_t	mec_reserved_1            :	 3;
		bdrkreg_t       mec_address               :     38;
		bdrkreg_t       mec_reserved              :      7;
		bdrkreg_t       mec_xb_error              :      4;
		bdrkreg_t       mec_bad_partial_data      :      2;
		bdrkreg_t       mec_missing_dv            :      2;
		bdrkreg_t       mec_short_pack            :      2;
		bdrkreg_t       mec_long_pack             :      2;
		bdrkreg_t       mec_ill_msg               :      2;
		bdrkreg_t       mec_ill_revision          :      2;
	} md_misc1_error_clr_fld_s;
} md_misc1_error_clr_u_t;

#else

typedef union md_misc1_error_clr_u {
	bdrkreg_t	md_misc1_error_clr_regval;
	struct	{
		bdrkreg_t	mec_ill_revision	  :	 2;
		bdrkreg_t	mec_ill_msg		  :	 2;
		bdrkreg_t	mec_long_pack		  :	 2;
		bdrkreg_t	mec_short_pack		  :	 2;
		bdrkreg_t	mec_missing_dv		  :	 2;
		bdrkreg_t	mec_bad_partial_data	  :	 2;
		bdrkreg_t	mec_xb_error		  :	 4;
		bdrkreg_t	mec_reserved		  :	 7;
		bdrkreg_t	mec_address		  :	38;
		bdrkreg_t	mec_reserved_1		  :	 3;
	} md_misc1_error_clr_fld_s;
} md_misc1_error_clr_u_t;

#endif




/************************************************************************
 *                                                                      *
 * Description:  The MD no longer allows for arbitrarily sizing the     *
 * reply queues, so all of the fields in this register are read-only    *
 * and contain the reset default value of 12 for the MOQHs (for         *
 * headers) and 24 for the MOQDs (for data).                            *
 * Reading from this register returns the values currently held in      *
 * the MD's credit counters. Writing to the register resets the         *
 * counters to the default reset values specified in the table below.   *
 *                                                                      *
 ************************************************************************/




#ifdef LITTLE_ENDIAN

typedef union md_outgoing_rp_queue_size_u {
	bdrkreg_t	md_outgoing_rp_queue_size_regval;
	struct  {
		bdrkreg_t	orqs_reserved_6           :	 8;
		bdrkreg_t       orqs_moqh_p0_rp_size      :      4;
		bdrkreg_t       orqs_reserved_5           :      4;
		bdrkreg_t       orqs_moqh_p1_rp_size      :      4;
		bdrkreg_t       orqs_reserved_4           :      4;
		bdrkreg_t       orqs_moqh_np_rp_size      :      4;
		bdrkreg_t       orqs_reserved_3           :      4;
		bdrkreg_t       orqs_moqd_pi0_rp_size     :      5;
		bdrkreg_t       orqs_reserved_2           :      3;
		bdrkreg_t       orqs_moqd_pi1_rp_size     :      5;
		bdrkreg_t       orqs_reserved_1           :      3;
		bdrkreg_t       orqs_moqd_np_rp_size      :      5;
		bdrkreg_t       orqs_reserved             :     11;
	} md_outgoing_rp_queue_size_fld_s;
} md_outgoing_rp_queue_size_u_t;

#else

typedef union md_outgoing_rp_queue_size_u {
	bdrkreg_t	md_outgoing_rp_queue_size_regval;
	struct	{
		bdrkreg_t	orqs_reserved		  :	11;
		bdrkreg_t	orqs_moqd_np_rp_size	  :	 5;
		bdrkreg_t	orqs_reserved_1		  :	 3;
		bdrkreg_t	orqs_moqd_pi1_rp_size	  :	 5;
		bdrkreg_t	orqs_reserved_2		  :	 3;
		bdrkreg_t	orqs_moqd_pi0_rp_size	  :	 5;
		bdrkreg_t	orqs_reserved_3		  :	 4;
		bdrkreg_t	orqs_moqh_np_rp_size	  :	 4;
		bdrkreg_t	orqs_reserved_4		  :	 4;
		bdrkreg_t	orqs_moqh_p1_rp_size	  :	 4;
		bdrkreg_t	orqs_reserved_5		  :	 4;
		bdrkreg_t	orqs_moqh_p0_rp_size	  :	 4;
		bdrkreg_t	orqs_reserved_6		  :	 8;
	} md_outgoing_rp_queue_size_fld_s;
} md_outgoing_rp_queue_size_u_t;

#endif






#ifdef LITTLE_ENDIAN

typedef union md_perf_sel0_u {
	bdrkreg_t	md_perf_sel0_regval;
	struct  {
		bdrkreg_t	ps_cnt_mode               :	 2;
		bdrkreg_t       ps_reserved_2             :      2;
		bdrkreg_t       ps_activity               :      4;
		bdrkreg_t       ps_source                 :      7;
		bdrkreg_t       ps_reserved_1             :      1;
		bdrkreg_t       ps_channel                :      4;
		bdrkreg_t       ps_command                :     40;
		bdrkreg_t       ps_reserved               :      3;
		bdrkreg_t       ps_interrupt              :      1;
	} md_perf_sel0_fld_s;
} md_perf_sel0_u_t;

#else

typedef union md_perf_sel0_u {
	bdrkreg_t	md_perf_sel0_regval;
	struct	{
		bdrkreg_t	ps_interrupt		  :	 1;
		bdrkreg_t	ps_reserved		  :	 3;
		bdrkreg_t	ps_command		  :	40;
		bdrkreg_t	ps_channel		  :	 4;
		bdrkreg_t	ps_reserved_1		  :	 1;
		bdrkreg_t	ps_source		  :	 7;
		bdrkreg_t	ps_activity		  :	 4;
		bdrkreg_t	ps_reserved_2		  :	 2;
		bdrkreg_t	ps_cnt_mode		  :	 2;
	} md_perf_sel0_fld_s;
} md_perf_sel0_u_t;

#endif




#ifdef LITTLE_ENDIAN

typedef union md_perf_sel1_u {
	bdrkreg_t	md_perf_sel1_regval;
	struct  {
		bdrkreg_t	ps_cnt_mode               :	 2;
		bdrkreg_t       ps_reserved_2             :      2;
		bdrkreg_t       ps_activity               :      4;
		bdrkreg_t       ps_source                 :      7;
		bdrkreg_t       ps_reserved_1             :      1;
		bdrkreg_t       ps_channel                :      4;
		bdrkreg_t       ps_command                :     40;
		bdrkreg_t       ps_reserved               :      3;
		bdrkreg_t       ps_interrupt              :      1;
	} md_perf_sel1_fld_s;
} md_perf_sel1_u_t;

#else

typedef union md_perf_sel1_u {
	bdrkreg_t	md_perf_sel1_regval;
	struct	{
		bdrkreg_t	ps_interrupt		  :	 1;
		bdrkreg_t	ps_reserved		  :	 3;
		bdrkreg_t	ps_command		  :	40;
		bdrkreg_t	ps_channel		  :	 4;
		bdrkreg_t	ps_reserved_1		  :	 1;
		bdrkreg_t	ps_source		  :	 7;
		bdrkreg_t	ps_activity		  :	 4;
		bdrkreg_t	ps_reserved_2		  :	 2;
		bdrkreg_t	ps_cnt_mode		  :	 2;
	} md_perf_sel1_fld_s;
} md_perf_sel1_u_t;

#endif




/************************************************************************
 *                                                                      *
 *  Performance counter.                                                *
 *                                                                      *
 ************************************************************************/




#ifdef LITTLE_ENDIAN

typedef union md_perf_cnt0_u {
	bdrkreg_t	md_perf_cnt0_regval;
	struct  {
		bdrkreg_t	pc_perf_cnt               :	41;
		bdrkreg_t	pc_reserved		  :	23;
	} md_perf_cnt0_fld_s;
} md_perf_cnt0_u_t;

#else

typedef union md_perf_cnt0_u {
	bdrkreg_t	md_perf_cnt0_regval;
	struct	{
		bdrkreg_t	pc_reserved		  :	23;
		bdrkreg_t	pc_perf_cnt		  :	41;
	} md_perf_cnt0_fld_s;
} md_perf_cnt0_u_t;

#endif




/************************************************************************
 *                                                                      *
 *  Performance counter.                                                *
 *                                                                      *
 ************************************************************************/




#ifdef LITTLE_ENDIAN

typedef union md_perf_cnt1_u {
	bdrkreg_t	md_perf_cnt1_regval;
	struct  {
		bdrkreg_t	pc_perf_cnt               :	41;
		bdrkreg_t	pc_reserved		  :	23;
	} md_perf_cnt1_fld_s;
} md_perf_cnt1_u_t;

#else

typedef union md_perf_cnt1_u {
	bdrkreg_t	md_perf_cnt1_regval;
	struct	{
		bdrkreg_t	pc_reserved		  :	23;
		bdrkreg_t	pc_perf_cnt		  :	41;
	} md_perf_cnt1_fld_s;
} md_perf_cnt1_u_t;

#endif




/************************************************************************
 *                                                                      *
 * Description:  This register contains the control for                 *
 * memory/directory refresh. Once the MEMORY_CONFIG register contains   *
 * the correct DIMM information, the hardware takes care of             *
 * refreshing all the banks in the system. Therefore, the value in      *
 * the counter threshold is corresponds exactly to the refresh value    *
 * required by the SDRAM parts (expressed in Bedrock clock cycles).     *
 * The refresh will execute whenever there is a free cycle and there    *
 * are still banks that have not been refreshed in the current          *
 * window. If the window expires with banks still waiting to be         *
 * refreshed, all other transactions are halted until the banks are     *
 * refreshed.                                                           *
 * The upper order bit contains an enable, which may be needed for      *
 * correct initialization of the DIMMs (according to the specs, the     *
 * first operation to the DIMMs should be a mode register write, not    *
 * a refresh, so this bit is cleared on reset) and is also useful for   *
 * diagnostic purposes.                                                 *
 * For the SDRAM parts used by Bedrock, 4096 refreshes need to be       *
 * issued during every 64 ms window, resulting in a refresh threshold   *
 * of 3125 Bedrock cycles.                                              *
 * The ENABLE and CNT_THRESH fields of this register are preserved      *
 * through soft-resets.                                                 *
 *                                                                      *
 ************************************************************************/




#ifdef LITTLE_ENDIAN

typedef union md_refresh_control_u {
	bdrkreg_t	md_refresh_control_regval;
	struct  {
		bdrkreg_t	rc_cnt_thresh             :	12;
		bdrkreg_t       rc_counter                :     12;
		bdrkreg_t       rc_reserved               :     39;
		bdrkreg_t       rc_enable                 :      1;
	} md_refresh_control_fld_s;
} md_refresh_control_u_t;

#else

typedef union md_refresh_control_u {
	bdrkreg_t	md_refresh_control_regval;
	struct	{
		bdrkreg_t	rc_enable		  :	 1;
		bdrkreg_t	rc_reserved		  :	39;
		bdrkreg_t	rc_counter		  :	12;
		bdrkreg_t	rc_cnt_thresh		  :	12;
	} md_refresh_control_fld_s;
} md_refresh_control_u_t;

#endif




/************************************************************************
 *                                                                      *
 *  This register controls the read and write timing for Flash PROM,    *
 * UART and Synergy junk bus devices.                                   *
 *                                                                      *
 ************************************************************************/




#ifdef LITTLE_ENDIAN

typedef union md_junk_bus_timing_u {
	bdrkreg_t	md_junk_bus_timing_regval;
	struct  {
		bdrkreg_t	jbt_fprom_setup_hold      :	 8;
		bdrkreg_t       jbt_fprom_enable          :      8;
		bdrkreg_t       jbt_uart_setup_hold       :      8;
		bdrkreg_t       jbt_uart_enable           :      8;
		bdrkreg_t       jbt_synergy_setup_hold    :      8;
		bdrkreg_t       jbt_synergy_enable        :      8;
		bdrkreg_t       jbt_reserved              :     16;
	} md_junk_bus_timing_fld_s;
} md_junk_bus_timing_u_t;

#else

typedef union md_junk_bus_timing_u {
	bdrkreg_t	md_junk_bus_timing_regval;
	struct	{
		bdrkreg_t	jbt_reserved		  :	16;
		bdrkreg_t	jbt_synergy_enable	  :	 8;
		bdrkreg_t	jbt_synergy_setup_hold	  :	 8;
		bdrkreg_t	jbt_uart_enable		  :	 8;
		bdrkreg_t	jbt_uart_setup_hold	  :	 8;
		bdrkreg_t	jbt_fprom_enable	  :	 8;
		bdrkreg_t	jbt_fprom_setup_hold	  :	 8;
	} md_junk_bus_timing_fld_s;
} md_junk_bus_timing_u_t;

#endif




/************************************************************************
 *                                                                      *
 *  Each of these addresses allows the value on one 8-bit bank of       *
 * LEDs to be read.                                                     *
 *                                                                      *
 ************************************************************************/




#ifdef LITTLE_ENDIAN

typedef union md_led0_u {
	bdrkreg_t	md_led0_regval;
	struct  {
		bdrkreg_t	l_data                    :	 8;
		bdrkreg_t       l_reserved                :     56;
	} md_led0_fld_s;
} md_led0_u_t;

#else

typedef union md_led0_u {
	bdrkreg_t	md_led0_regval;
	struct	{
		bdrkreg_t	l_reserved		  :	56;
		bdrkreg_t	l_data			  :	 8;
	} md_led0_fld_s;
} md_led0_u_t;

#endif




/************************************************************************
 *                                                                      *
 *  Each of these addresses allows the value on one 8-bit bank of       *
 * LEDs to be read.                                                     *
 *                                                                      *
 ************************************************************************/




#ifdef LITTLE_ENDIAN

typedef union md_led1_u {
	bdrkreg_t	md_led1_regval;
	struct  {
		bdrkreg_t	l_data                    :	 8;
		bdrkreg_t       l_reserved                :     56;
	} md_led1_fld_s;
} md_led1_u_t;

#else

typedef union md_led1_u {
	bdrkreg_t	md_led1_regval;
	struct	{
		bdrkreg_t	l_reserved		  :	56;
		bdrkreg_t	l_data			  :	 8;
	} md_led1_fld_s;
} md_led1_u_t;

#endif




/************************************************************************
 *                                                                      *
 *  Each of these addresses allows the value on one 8-bit bank of       *
 * LEDs to be read.                                                     *
 *                                                                      *
 ************************************************************************/




#ifdef LITTLE_ENDIAN

typedef union md_led2_u {
	bdrkreg_t	md_led2_regval;
	struct  {
		bdrkreg_t	l_data                    :	 8;
		bdrkreg_t       l_reserved                :     56;
	} md_led2_fld_s;
} md_led2_u_t;

#else

typedef union md_led2_u {
	bdrkreg_t	md_led2_regval;
	struct	{
		bdrkreg_t	l_reserved		  :	56;
		bdrkreg_t	l_data			  :	 8;
	} md_led2_fld_s;
} md_led2_u_t;

#endif




/************************************************************************
 *                                                                      *
 *  Each of these addresses allows the value on one 8-bit bank of       *
 * LEDs to be read.                                                     *
 *                                                                      *
 ************************************************************************/




#ifdef LITTLE_ENDIAN

typedef union md_led3_u {
	bdrkreg_t	md_led3_regval;
	struct  {
		bdrkreg_t	l_data                    :	 8;
		bdrkreg_t       l_reserved                :     56;
	} md_led3_fld_s;
} md_led3_u_t;

#else

typedef union md_led3_u {
	bdrkreg_t	md_led3_regval;
	struct	{
		bdrkreg_t	l_reserved		  :	56;
		bdrkreg_t	l_data			  :	 8;
	} md_led3_fld_s;
} md_led3_u_t;

#endif




/************************************************************************
 *                                                                      *
 *  Core control for the BIST function. Start and stop BIST at any      *
 * time.                                                                *
 *                                                                      *
 ************************************************************************/




#ifdef LITTLE_ENDIAN

typedef union md_bist_ctl_u {
	bdrkreg_t	md_bist_ctl_regval;
	struct  {
		bdrkreg_t	bc_bist_start             :	 1;
		bdrkreg_t       bc_bist_stop              :      1;
		bdrkreg_t       bc_bist_reset             :      1;
		bdrkreg_t       bc_reserved_1             :      1;
		bdrkreg_t       bc_bank_num               :      1;
		bdrkreg_t       bc_dimm_num               :      2;
		bdrkreg_t       bc_reserved               :     57;
	} md_bist_ctl_fld_s;
} md_bist_ctl_u_t;

#else

typedef union md_bist_ctl_u {
	bdrkreg_t	md_bist_ctl_regval;
	struct	{
		bdrkreg_t	bc_reserved		  :	57;
		bdrkreg_t	bc_dimm_num		  :	 2;
		bdrkreg_t	bc_bank_num		  :	 1;
		bdrkreg_t	bc_reserved_1		  :	 1;
		bdrkreg_t	bc_bist_reset		  :	 1;
		bdrkreg_t	bc_bist_stop		  :	 1;
		bdrkreg_t	bc_bist_start		  :	 1;
	} md_bist_ctl_fld_s;
} md_bist_ctl_u_t;

#endif




/************************************************************************
 *                                                                      *
 *  Contain the initial BIST data nibble and the 4-bit data control     *
 * field..                                                              *
 *                                                                      *
 ************************************************************************/



#ifdef LITTLE_ENDIAN

typedef union md_bist_data_u {
	bdrkreg_t	md_bist_data_regval;
	struct  {
		bdrkreg_t	bd_bist_data              :	 4;
		bdrkreg_t	bd_bist_nibble		  :	 1;
		bdrkreg_t       bd_bist_byte              :      1;
		bdrkreg_t       bd_bist_cycle             :      1;
		bdrkreg_t       bd_bist_write             :      1;
		bdrkreg_t       bd_reserved               :     56;
	} md_bist_data_fld_s;
} md_bist_data_u_t;

#else

typedef union md_bist_data_u {
	bdrkreg_t	md_bist_data_regval;
	struct	{
		bdrkreg_t	bd_reserved		  :	56;
		bdrkreg_t	bd_bist_write		  :	 1;
		bdrkreg_t	bd_bist_cycle		  :	 1;
		bdrkreg_t	bd_bist_byte		  :	 1;
		bdrkreg_t	bd_bist_nibble		  :	 1;
		bdrkreg_t	bd_bist_data		  :	 4;
	} md_bist_data_fld_s;
} md_bist_data_u_t;

#endif




/************************************************************************
 *                                                                      *
 *  Captures the BIST error address and indicates whether it is an MB   *
 * error or DB error.                                                   *
 *                                                                      *
 ************************************************************************/




#ifdef LITTLE_ENDIAN

typedef union md_bist_ab_err_addr_u {
	bdrkreg_t	md_bist_ab_err_addr_regval;
	struct  {
		bdrkreg_t	baea_be_db_cas_addr       :	15;
		bdrkreg_t       baea_reserved_3           :      1;
		bdrkreg_t       baea_be_mb_cas_addr       :     15;
		bdrkreg_t       baea_reserved_2           :      1;
		bdrkreg_t       baea_be_ras_addr          :     15;
		bdrkreg_t       baea_reserved_1           :      1;
		bdrkreg_t       baea_bist_mb_error        :      1;
		bdrkreg_t       baea_bist_db_error        :      1;
		bdrkreg_t       baea_reserved             :     14;
	} md_bist_ab_err_addr_fld_s;
} md_bist_ab_err_addr_u_t;

#else

typedef union md_bist_ab_err_addr_u {
	bdrkreg_t	md_bist_ab_err_addr_regval;
	struct	{
		bdrkreg_t	baea_reserved		  :	14;
		bdrkreg_t	baea_bist_db_error	  :	 1;
		bdrkreg_t	baea_bist_mb_error	  :	 1;
		bdrkreg_t	baea_reserved_1		  :	 1;
		bdrkreg_t	baea_be_ras_addr	  :	15;
		bdrkreg_t	baea_reserved_2		  :	 1;
		bdrkreg_t	baea_be_mb_cas_addr	  :	15;
		bdrkreg_t	baea_reserved_3		  :	 1;
		bdrkreg_t	baea_be_db_cas_addr	  :	15;
	} md_bist_ab_err_addr_fld_s;
} md_bist_ab_err_addr_u_t;

#endif



/************************************************************************
 *                                                                      *
 *  Contains information on BIST progress and memory bank currently     *
 * under BIST.                                                          *
 *                                                                      *
 ************************************************************************/




#ifdef LITTLE_ENDIAN

typedef union md_bist_status_u {
	bdrkreg_t	md_bist_status_regval;
	struct  {
		bdrkreg_t	bs_bist_passed            :	 1;
		bdrkreg_t       bs_bist_done              :      1;
		bdrkreg_t       bs_reserved               :     62;
	} md_bist_status_fld_s;
} md_bist_status_u_t;

#else

typedef union md_bist_status_u {
	bdrkreg_t	md_bist_status_regval;
	struct	{
		bdrkreg_t	bs_reserved		  :	62;
		bdrkreg_t	bs_bist_done		  :	 1;
		bdrkreg_t	bs_bist_passed		  :	 1;
	} md_bist_status_fld_s;
} md_bist_status_u_t;

#endif




/************************************************************************
 *                                                                      *
 *  Contains 3 bits that allow the selection of IB debug information    *
 * at the debug port (see design specification for available debug      *
 * information).                                                        *
 *                                                                      *
 ************************************************************************/




#ifdef LITTLE_ENDIAN

typedef union md_ib_debug_u {
	bdrkreg_t	md_ib_debug_regval;
	struct  {
		bdrkreg_t	id_ib_debug_sel           :	 2;
		bdrkreg_t       id_reserved               :     62;
	} md_ib_debug_fld_s;
} md_ib_debug_u_t;

#else

typedef union md_ib_debug_u {
	bdrkreg_t	md_ib_debug_regval;
	struct	{
		bdrkreg_t	id_reserved		  :	62;
		bdrkreg_t	id_ib_debug_sel		  :	 2;
	} md_ib_debug_fld_s;
} md_ib_debug_u_t;

#endif




/************************************************************************
 *                                                                      *
 *  Contains the directory specific mode bits. The contents of this     *
 * register are preserved through soft-resets.                          *
 *                                                                      *
 ************************************************************************/




#ifdef LITTLE_ENDIAN

typedef union md_dir_config_u {
	bdrkreg_t	md_dir_config_regval;
	struct  {
		bdrkreg_t	dc_dir_flavor             :	 1;
		bdrkreg_t       dc_ignore_dir_ecc         :      1;
		bdrkreg_t       dc_reserved               :     62;
	} md_dir_config_fld_s;
} md_dir_config_u_t;

#else

typedef union md_dir_config_u {
	bdrkreg_t	md_dir_config_regval;
	struct	{
		bdrkreg_t	dc_reserved		  :	62;
		bdrkreg_t	dc_ignore_dir_ecc	  :	 1;
		bdrkreg_t	dc_dir_flavor		  :	 1;
	} md_dir_config_fld_s;
} md_dir_config_u_t;

#endif




/************************************************************************
 *                                                                      *
 * Description:  Contains information on uncorrectable and              *
 * correctable directory ECC errors, along with protection ECC          *
 * errors. The priority of ECC errors latched is: uncorrectable         *
 * directory, protection error, correctable directory. Thus the valid   *
 * bits signal:                                                         *
 * 1xxx: uncorrectable directory ECC error (UCE)                        *
 * 01xx: access protection double bit error (AE)                        *
 * 001x: correctable directory ECC error (CE)                           *
 * 0001: access protection correctable error (ACE)                      *
 * If the UCE valid bit is set, the address field contains a pointer    *
 * to the Hspec address of the offending directory entry, the           *
 * syndrome field contains the bad syndrome, and the UCE overrun bit    *
 * indicates whether multiple double-bit errors were received.          *
 * If the UCE valid bit is clear but the AE valid bit is set, the       *
 * address field contains a pointer to the Hspec address of the         *
 * offending protection entry, the Bad Protection field contains the    *
 * 4-bit bad protection value, the PROT_INDEX field shows which of      *
 * the 8 protection values in the word was bad and the AE overrun bit   *
 * indicates whether multiple AE errors were received.                  *
 * If the UCE and AE valid bits are clear, but the CE valid bit is      *
 * set, the address field contains a pointer to the Hspec address of    *
 * the offending directory entry, the syndrome field contains the bad   *
 * syndrome, and the CE overrun bit indicates whether multiple          *
 * single-bit errors were received.                                     *
 *                                                                      *
 ************************************************************************/




#ifdef LITTLE_ENDIAN

typedef union md_dir_error_u {
	bdrkreg_t	md_dir_error_regval;
	struct  {
		bdrkreg_t	de_reserved_3             :	 3;
		bdrkreg_t       de_hspec_addr             :     30;
		bdrkreg_t       de_reserved_2             :      7;
		bdrkreg_t       de_bad_syn                :      7;
		bdrkreg_t       de_reserved_1             :      1;
                bdrkreg_t       de_bad_protect            :      4;
                bdrkreg_t       de_prot_index             :      3;
                bdrkreg_t       de_reserved               :      1;
                bdrkreg_t       de_ace_overrun            :      1;
                bdrkreg_t       de_ce_overrun             :      1;
                bdrkreg_t       de_ae_overrun             :      1;
                bdrkreg_t       de_uce_overrun            :      1;
                bdrkreg_t       de_ace_valid              :      1;
                bdrkreg_t       de_ce_valid               :      1;
                bdrkreg_t       de_ae_valid               :      1;
                bdrkreg_t       de_uce_valid              :      1;
	} md_dir_error_fld_s;
} md_dir_error_u_t;

#else

typedef union md_dir_error_u {
	bdrkreg_t	md_dir_error_regval;
	struct	{
		bdrkreg_t	de_uce_valid		  :	 1;
		bdrkreg_t	de_ae_valid		  :	 1;
		bdrkreg_t	de_ce_valid		  :	 1;
		bdrkreg_t	de_ace_valid		  :	 1;
		bdrkreg_t	de_uce_overrun		  :	 1;
		bdrkreg_t	de_ae_overrun		  :	 1;
		bdrkreg_t	de_ce_overrun		  :	 1;
		bdrkreg_t	de_ace_overrun		  :	 1;
		bdrkreg_t	de_reserved		  :	 1;
		bdrkreg_t	de_prot_index		  :	 3;
		bdrkreg_t	de_bad_protect		  :	 4;
		bdrkreg_t	de_reserved_1		  :	 1;
		bdrkreg_t	de_bad_syn		  :	 7;
		bdrkreg_t	de_reserved_2		  :	 7;
		bdrkreg_t	de_hspec_addr		  :	30;
		bdrkreg_t	de_reserved_3		  :	 3;
	} md_dir_error_fld_s;
} md_dir_error_u_t;

#endif




/************************************************************************
 *                                                                      *
 * Description:  Contains information on uncorrectable and              *
 * correctable directory ECC errors, along with protection ECC          *
 * errors. The priority of ECC errors latched is: uncorrectable         *
 * directory, protection error, correctable directory. Thus the valid   *
 * bits signal:                                                         *
 * 1xxx: uncorrectable directory ECC error (UCE)                        *
 * 01xx: access protection double bit error (AE)                        *
 * 001x: correctable directory ECC error (CE)                           *
 * 0001: access protection correctable error (ACE)                      *
 * If the UCE valid bit is set, the address field contains a pointer    *
 * to the Hspec address of the offending directory entry, the           *
 * syndrome field contains the bad syndrome, and the UCE overrun bit    *
 * indicates whether multiple double-bit errors were received.          *
 * If the UCE valid bit is clear but the AE valid bit is set, the       *
 * address field contains a pointer to the Hspec address of the         *
 * offending protection entry, the Bad Protection field contains the    *
 * 4-bit bad protection value, the PROT_INDEX field shows which of      *
 * the 8 protection values in the word was bad and the AE overrun bit   *
 * indicates whether multiple AE errors were received.                  *
 * If the UCE and AE valid bits are clear, but the CE valid bit is      *
 * set, the address field contains a pointer to the Hspec address of    *
 * the offending directory entry, the syndrome field contains the bad   *
 * syndrome, and the CE overrun bit indicates whether multiple          *
 * single-bit errors were received.                                     *
 *                                                                      *
 ************************************************************************/




#ifdef LITTLE_ENDIAN

typedef union md_dir_error_clr_u {
	bdrkreg_t	md_dir_error_clr_regval;
	struct  {
		bdrkreg_t	dec_reserved_3            :	 3;
                bdrkreg_t       dec_hspec_addr            :     30;
                bdrkreg_t       dec_reserved_2            :      7;
                bdrkreg_t       dec_bad_syn               :      7;
                bdrkreg_t       dec_reserved_1            :      1;
                bdrkreg_t       dec_bad_protect           :      4;
                bdrkreg_t       dec_prot_index            :      3;
                bdrkreg_t       dec_reserved              :      1;
                bdrkreg_t       dec_ace_overrun           :      1;
                bdrkreg_t       dec_ce_overrun            :      1;
                bdrkreg_t       dec_ae_overrun            :      1;
                bdrkreg_t       dec_uce_overrun           :      1;
                bdrkreg_t       dec_ace_valid             :      1;
                bdrkreg_t       dec_ce_valid              :      1;
                bdrkreg_t       dec_ae_valid              :      1;
                bdrkreg_t       dec_uce_valid             :      1;
	} md_dir_error_clr_fld_s;
} md_dir_error_clr_u_t;

#else

typedef union md_dir_error_clr_u {
	bdrkreg_t	md_dir_error_clr_regval;
	struct	{
		bdrkreg_t	dec_uce_valid		  :	 1;
		bdrkreg_t	dec_ae_valid		  :	 1;
		bdrkreg_t	dec_ce_valid		  :	 1;
		bdrkreg_t	dec_ace_valid		  :	 1;
		bdrkreg_t	dec_uce_overrun		  :	 1;
		bdrkreg_t	dec_ae_overrun		  :	 1;
		bdrkreg_t	dec_ce_overrun		  :	 1;
		bdrkreg_t	dec_ace_overrun		  :	 1;
		bdrkreg_t	dec_reserved		  :	 1;
		bdrkreg_t	dec_prot_index		  :	 3;
		bdrkreg_t	dec_bad_protect		  :	 4;
		bdrkreg_t	dec_reserved_1		  :	 1;
		bdrkreg_t	dec_bad_syn		  :	 7;
		bdrkreg_t	dec_reserved_2		  :	 7;
		bdrkreg_t	dec_hspec_addr		  :	30;
		bdrkreg_t	dec_reserved_3		  :	 3;
	} md_dir_error_clr_fld_s;
} md_dir_error_clr_u_t;

#endif




/************************************************************************
 *                                                                      *
 *  Contains information on requests that encounter no valid protocol   *
 * table entry.                                                         *
 *                                                                      *
 ************************************************************************/




#ifdef LITTLE_ENDIAN

typedef union md_protocol_error_u {
	bdrkreg_t	md_protocol_error_regval;
	struct  {
		bdrkreg_t	pe_overrun                :	 1;
                bdrkreg_t       pe_pointer_me             :      1;
                bdrkreg_t       pe_reserved_1             :      1;
                bdrkreg_t       pe_address                :     30;
                bdrkreg_t       pe_reserved               :      1;
                bdrkreg_t       pe_ptr1_btmbits           :      3;
                bdrkreg_t       pe_dir_format             :      2;
                bdrkreg_t       pe_dir_state              :      3;
                bdrkreg_t       pe_priority               :      1;
                bdrkreg_t       pe_access                 :      1;
                bdrkreg_t       pe_msg_type               :      8;
                bdrkreg_t       pe_initiator              :     11;
                bdrkreg_t       pe_valid                  :      1;
	} md_protocol_error_fld_s;
} md_protocol_error_u_t;

#else

typedef union md_protocol_error_u {
	bdrkreg_t	md_protocol_error_regval;
	struct	{
		bdrkreg_t	pe_valid		  :	 1;
		bdrkreg_t	pe_initiator		  :	11;
		bdrkreg_t	pe_msg_type		  :	 8;
		bdrkreg_t	pe_access		  :	 1;
		bdrkreg_t	pe_priority		  :	 1;
		bdrkreg_t	pe_dir_state		  :	 3;
		bdrkreg_t	pe_dir_format		  :	 2;
		bdrkreg_t	pe_ptr1_btmbits		  :	 3;
		bdrkreg_t	pe_reserved		  :	 1;
		bdrkreg_t	pe_address		  :	30;
		bdrkreg_t	pe_reserved_1		  :	 1;
		bdrkreg_t	pe_pointer_me		  :	 1;
		bdrkreg_t	pe_overrun		  :	 1;
	} md_protocol_error_fld_s;
} md_protocol_error_u_t;

#endif




/************************************************************************
 *                                                                      *
 *  Contains information on requests that encounter no valid protocol   *
 * table entry.                                                         *
 *                                                                      *
 ************************************************************************/




#ifdef LITTLE_ENDIAN

typedef union md_protocol_err_clr_u {
	bdrkreg_t	md_protocol_err_clr_regval;
	struct  {
		bdrkreg_t	pec_overrun               :	 1;
                bdrkreg_t       pec_pointer_me            :      1;
                bdrkreg_t       pec_reserved_1            :      1;
                bdrkreg_t       pec_address               :     30;
                bdrkreg_t       pec_reserved              :      1;
                bdrkreg_t       pec_ptr1_btmbits          :      3;
                bdrkreg_t       pec_dir_format            :      2;
                bdrkreg_t       pec_dir_state             :      3;
                bdrkreg_t       pec_priority              :      1;
                bdrkreg_t       pec_access                :      1;
                bdrkreg_t       pec_msg_type              :      8;
                bdrkreg_t       pec_initiator             :     11;
                bdrkreg_t       pec_valid                 :      1;
	} md_protocol_err_clr_fld_s;
} md_protocol_err_clr_u_t;

#else

typedef union md_protocol_err_clr_u {
	bdrkreg_t	md_protocol_err_clr_regval;
	struct	{
		bdrkreg_t	pec_valid		  :	 1;
		bdrkreg_t	pec_initiator		  :	11;
		bdrkreg_t	pec_msg_type		  :	 8;
		bdrkreg_t	pec_access		  :	 1;
		bdrkreg_t	pec_priority		  :	 1;
		bdrkreg_t	pec_dir_state		  :	 3;
		bdrkreg_t	pec_dir_format		  :	 2;
		bdrkreg_t	pec_ptr1_btmbits	  :	 3;
		bdrkreg_t	pec_reserved		  :	 1;
		bdrkreg_t	pec_address		  :	30;
		bdrkreg_t	pec_reserved_1		  :	 1;
		bdrkreg_t	pec_pointer_me		  :	 1;
		bdrkreg_t	pec_overrun		  :	 1;
	} md_protocol_err_clr_fld_s;
} md_protocol_err_clr_u_t;

#endif




/************************************************************************
 *                                                                      *
 *  Contains the address of the page and the requestor which caused a   *
 * migration threshold to be exceeded. Also contains the type of        *
 * threshold exceeded and an overrun bit. For Value mode type           *
 * interrupts, it indicates whether the local or the remote counter     *
 * triggered the interrupt. Unlike most registers, when the overrun     *
 * bit is set the register contains information on the most recent      *
 * (the last) migration candidate.                                      *
 *                                                                      *
 ************************************************************************/




#ifdef LITTLE_ENDIAN

typedef union md_mig_candidate_u {
	bdrkreg_t	md_mig_candidate_regval;
	struct  {
		bdrkreg_t	mc_address                :	21;
                bdrkreg_t       mc_initiator              :     11;
                bdrkreg_t       mc_overrun                :      1;
                bdrkreg_t       mc_type                   :      1;
                bdrkreg_t       mc_local                  :      1;
                bdrkreg_t       mc_reserved               :     28;
                bdrkreg_t       mc_valid                  :      1;
	} md_mig_candidate_fld_s;
} md_mig_candidate_u_t;

#else

typedef union md_mig_candidate_u {
	bdrkreg_t	md_mig_candidate_regval;
	struct	{
		bdrkreg_t	mc_valid		  :	 1;
		bdrkreg_t	mc_reserved		  :	28;
		bdrkreg_t	mc_local		  :	 1;
		bdrkreg_t	mc_type			  :	 1;
		bdrkreg_t	mc_overrun		  :	 1;
		bdrkreg_t	mc_initiator		  :	11;
		bdrkreg_t	mc_address		  :	21;
	} md_mig_candidate_fld_s;
} md_mig_candidate_u_t;

#endif




/************************************************************************
 *                                                                      *
 *  Contains the address of the page and the requestor which caused a   *
 * migration threshold to be exceeded. Also contains the type of        *
 * threshold exceeded and an overrun bit. For Value mode type           *
 * interrupts, it indicates whether the local or the remote counter     *
 * triggered the interrupt. Unlike most registers, when the overrun     *
 * bit is set the register contains information on the most recent      *
 * (the last) migration candidate.                                      *
 *                                                                      *
 ************************************************************************/




#ifdef LITTLE_ENDIAN

typedef union md_mig_candidate_clr_u {
	bdrkreg_t	md_mig_candidate_clr_regval;
	struct  {
		bdrkreg_t	mcc_address               :	21;
                bdrkreg_t       mcc_initiator             :     11;
                bdrkreg_t       mcc_overrun               :      1;
                bdrkreg_t       mcc_type                  :      1;
                bdrkreg_t       mcc_local                 :      1;
                bdrkreg_t       mcc_reserved              :     28;
                bdrkreg_t       mcc_valid                 :      1;
	} md_mig_candidate_clr_fld_s;
} md_mig_candidate_clr_u_t;

#else

typedef union md_mig_candidate_clr_u {
	bdrkreg_t	md_mig_candidate_clr_regval;
	struct	{
		bdrkreg_t	mcc_valid		  :	 1;
		bdrkreg_t	mcc_reserved		  :	28;
		bdrkreg_t	mcc_local		  :	 1;
		bdrkreg_t	mcc_type		  :	 1;
		bdrkreg_t	mcc_overrun		  :	 1;
		bdrkreg_t	mcc_initiator		  :	11;
		bdrkreg_t	mcc_address		  :	21;
	} md_mig_candidate_clr_fld_s;
} md_mig_candidate_clr_u_t;

#endif




/************************************************************************
 *                                                                      *
 *  Controls the generation of page-migration interrupts and loading    *
 * of the MIGRATION_CANDIDATE register for pages which are using the    *
 * difference between the requestor and home counts. If the             *
 * difference is greater-than or equal to than the threshold            *
 * contained in the register, and the valid bit is set, the migration   *
 * candidate is loaded (and an interrupt generated if enabled by the    *
 * page migration mode).                                                *
 *                                                                      *
 ************************************************************************/




#ifdef LITTLE_ENDIAN

typedef union md_mig_diff_thresh_u {
	bdrkreg_t	md_mig_diff_thresh_regval;
	struct  {
		bdrkreg_t	mdt_threshold             :	15;
                bdrkreg_t       mdt_reserved_1            :     17;
                bdrkreg_t       mdt_th_action             :      3;
                bdrkreg_t       mdt_sat_action            :      3;
                bdrkreg_t       mdt_reserved              :     25;
                bdrkreg_t       mdt_valid                 :      1;
	} md_mig_diff_thresh_fld_s;
} md_mig_diff_thresh_u_t;

#else

typedef union md_mig_diff_thresh_u {
	bdrkreg_t	md_mig_diff_thresh_regval;
	struct	{
		bdrkreg_t	mdt_valid		  :	 1;
		bdrkreg_t	mdt_reserved		  :	25;
		bdrkreg_t	mdt_sat_action		  :	 3;
		bdrkreg_t	mdt_th_action		  :	 3;
		bdrkreg_t	mdt_reserved_1		  :	17;
		bdrkreg_t	mdt_threshold		  :	15;
	} md_mig_diff_thresh_fld_s;
} md_mig_diff_thresh_u_t;

#endif




/************************************************************************
 *                                                                      *
 *  Controls the generation of page-migration interrupts and loading    *
 * of the MIGRATION_CANDIDATE register for pages that are using the     *
 * absolute value of the requestor count. If the value is               *
 * greater-than or equal to the threshold contained in the register,    *
 * and the register valid bit is set, the migration candidate is        *
 * loaded and an interrupt generated. For the value mode of page        *
 * migration, there are two variations. In the first variation,         *
 * interrupts are only generated when the remote counter reaches the    *
 * threshold, not when the local counter reaches the threshold. In      *
 * the second mode, both the local counter and the remote counter       *
 * generate interrupts if they reach the threshold. This second mode    *
 * is useful for performance monitoring, to track the number of local   *
 * and remote references to a page. LOCAL_INT determines whether we     *
 * will generate interrupts when the local counter reaches the          *
 * threshold.                                                           *
 *                                                                      *
 ************************************************************************/




#ifdef LITTLE_ENDIAN

typedef union md_mig_value_thresh_u {
	bdrkreg_t	md_mig_value_thresh_regval;
	struct  {
		bdrkreg_t	mvt_threshold             :	15;
                bdrkreg_t       mvt_reserved_1            :     17;
                bdrkreg_t       mvt_th_action             :      3;
                bdrkreg_t       mvt_sat_action            :      3;
                bdrkreg_t       mvt_reserved              :     24;
                bdrkreg_t       mvt_local_int             :      1;
                bdrkreg_t       mvt_valid                 :      1;
	} md_mig_value_thresh_fld_s;
} md_mig_value_thresh_u_t;

#else

typedef union md_mig_value_thresh_u {
        bdrkreg_t       md_mig_value_thresh_regval;
        struct  {
                bdrkreg_t       mvt_valid                 :      1;
                bdrkreg_t       mvt_local_int             :      1;
                bdrkreg_t       mvt_reserved              :     24;
                bdrkreg_t       mvt_sat_action            :      3;
                bdrkreg_t       mvt_th_action             :      3;
                bdrkreg_t       mvt_reserved_1            :     17;
                bdrkreg_t       mvt_threshold             :     15;
        } md_mig_value_thresh_fld_s;
} md_mig_value_thresh_u_t;

#endif




/************************************************************************
 *                                                                      *
 *  Contains the controls for the sizing of the three MOQH request      *
 * queues. The maximum (and default) value is 4. Queue sizes are in     *
 * flits. One header equals one flit.                                   *
 *                                                                      *
 ************************************************************************/




#ifdef LITTLE_ENDIAN

typedef union md_outgoing_rq_queue_size_u {
	bdrkreg_t	md_outgoing_rq_queue_size_regval;
	struct  {
		bdrkreg_t	orqs_reserved_3           :	 8;
                bdrkreg_t       orqs_moqh_p0_rq_size      :      3;
                bdrkreg_t       orqs_reserved_2           :      5;
                bdrkreg_t       orqs_moqh_p1_rq_size      :      3;
                bdrkreg_t       orqs_reserved_1           :      5;
                bdrkreg_t       orqs_moqh_np_rq_size      :      3;
                bdrkreg_t       orqs_reserved             :     37;
	} md_outgoing_rq_queue_size_fld_s;
} md_outgoing_rq_queue_size_u_t;

#else

typedef union md_outgoing_rq_queue_size_u {
	bdrkreg_t	md_outgoing_rq_queue_size_regval;
	struct	{
		bdrkreg_t	orqs_reserved		  :	37;
		bdrkreg_t	orqs_moqh_np_rq_size	  :	 3;
		bdrkreg_t	orqs_reserved_1		  :	 5;
		bdrkreg_t	orqs_moqh_p1_rq_size	  :	 3;
		bdrkreg_t	orqs_reserved_2		  :	 5;
		bdrkreg_t	orqs_moqh_p0_rq_size	  :	 3;
		bdrkreg_t	orqs_reserved_3		  :	 8;
	} md_outgoing_rq_queue_size_fld_s;
} md_outgoing_rq_queue_size_u_t;

#endif




/************************************************************************
 *                                                                      *
 *  Contains the 32-bit directory word failing BIST.                    *
 *                                                                      *
 ************************************************************************/




#ifdef LITTLE_ENDIAN

typedef union md_bist_db_err_data_u {
	bdrkreg_t	md_bist_db_err_data_regval;
	struct  {
		bdrkreg_t	bded_db_er_d              :	32;
		bdrkreg_t       bded_reserved             :     32;
	} md_bist_db_err_data_fld_s;
} md_bist_db_err_data_u_t;

#else

typedef union md_bist_db_err_data_u {
	bdrkreg_t	md_bist_db_err_data_regval;
	struct	{
		bdrkreg_t	bded_reserved		  :	32;
		bdrkreg_t	bded_db_er_d		  :	32;
	} md_bist_db_err_data_fld_s;
} md_bist_db_err_data_u_t;

#endif



/************************************************************************
 *                                                                      *
 *  Contains 2 bits that allow the selection of DB debug information    *
 * at the debug port (see the design specification for descrition of    *
 * the available debug information).                                    *
 *                                                                      *
 ************************************************************************/




#ifdef LITTLE_ENDIAN

typedef union md_db_debug_u {
	bdrkreg_t	md_db_debug_regval;
	struct  {
		bdrkreg_t	dd_db_debug_sel           :	 2;
		bdrkreg_t       dd_reserved               :     62;
	} md_db_debug_fld_s;
} md_db_debug_u_t;

#else

typedef union md_db_debug_u {
	bdrkreg_t	md_db_debug_regval;
	struct	{
		bdrkreg_t	dd_reserved		  :	62;
		bdrkreg_t	dd_db_debug_sel		  :	 2;
	} md_db_debug_fld_s;
} md_db_debug_u_t;

#endif




/************************************************************************
 *                                                                      *
 *  Contains the IgnoreECC bit. When this bit is set, all ECC errors    *
 * are ignored. ECC bits will still be generated on writebacks.         *
 *                                                                      *
 ************************************************************************/




#ifdef LITTLE_ENDIAN

typedef union md_mb_ecc_config_u {
	bdrkreg_t	md_mb_ecc_config_regval;
	struct  {
		bdrkreg_t	mec_ignore_dataecc        :	 1;
		bdrkreg_t       mec_reserved              :     63;
	} md_mb_ecc_config_fld_s;
} md_mb_ecc_config_u_t;

#else

typedef union md_mb_ecc_config_u {
	bdrkreg_t	md_mb_ecc_config_regval;
	struct	{
		bdrkreg_t	mec_reserved		  :	63;
		bdrkreg_t	mec_ignore_dataecc	  :	 1;
	} md_mb_ecc_config_fld_s;
} md_mb_ecc_config_u_t;

#endif




/************************************************************************
 *                                                                      *
 * Description:  Contains information on read memory errors (both       *
 * correctable and uncorrectable) and write memory errors (always       *
 * uncorrectable). The errors are prioritized as follows:               *
 *  highest: uncorrectable read error (READ_UCE)                        *
 *  middle: write error (WRITE_UCE)                                     *
 *  lowest: correctable read error (READ_CE)                            *
 * Each type of error maintains a two-bit valid/overrun field           *
 * (READ_UCE, WRITE_UCE, or READ_CE). Bit 0 of each two-bit field       *
 * corresponds to the valid bit, and bit 1 of each two-bit field        *
 * corresponds to the overrun bit.                                      *
 * The rule for the valid bit is that it gets set whenever that error   *
 * occurs, regardless of whether a higher priority error has occured.   *
 * The rule for the overrun bit is that it gets set whenever we are     *
 * unable to record the address information for this particular         *
 * error, due to a previous error of the same or higher priority.       *
 * Note that the syndrome and address information always corresponds    *
 * to the earliest, highest priority error.                             *
 *  Finally, the UCE_DIFF_ADDR bit is set whenever there have been      *
 * several uncorrectable errors, to different cache line addresses.     *
 * If all the UCEs were to the same cache line address, then            *
 * UCE_DIFF_ADDR will be 0. This allows the operating system to         *
 * detect the case where a UCE error is read exclusively, and then      *
 * written back by the processor. If the bit is 0, it indicates that    *
 * no information has been lost about UCEs on other cache lines. In     *
 * particular, partial writes do a read modify write of the cache       *
 * line. A UCE read error will be set when the cache line is read,      *
 * and a UCE write error will occur when the cache line is written      *
 * back, but the UCE_DIFF_ADDR will not be set.                         *
 *                                                                      *
 ************************************************************************/




#ifdef LITTLE_ENDIAN

typedef union md_mem_error_u {
	bdrkreg_t	md_mem_error_regval;
	struct  {
		bdrkreg_t	me_reserved_5             :	 3;
                bdrkreg_t       me_address                :     30;
                bdrkreg_t       me_reserved_4             :      7;
                bdrkreg_t       me_bad_syn                :      8;
                bdrkreg_t       me_reserved_3             :      4;
                bdrkreg_t       me_read_ce                :      2;
                bdrkreg_t       me_reserved_2             :      2;
                bdrkreg_t       me_write_uce              :      2;
                bdrkreg_t       me_reserved_1             :      2;
                bdrkreg_t       me_read_uce               :      2;
                bdrkreg_t       me_reserved               :      1;
                bdrkreg_t       me_uce_diff_addr          :      1;
	} md_mem_error_fld_s;
} md_mem_error_u_t;

#else

typedef union md_mem_error_u {
	bdrkreg_t	md_mem_error_regval;
	struct	{
		bdrkreg_t	me_uce_diff_addr	  :	 1;
		bdrkreg_t	me_reserved		  :	 1;
		bdrkreg_t	me_read_uce		  :	 2;
		bdrkreg_t	me_reserved_1		  :	 2;
		bdrkreg_t	me_write_uce		  :	 2;
		bdrkreg_t	me_reserved_2		  :	 2;
		bdrkreg_t	me_read_ce		  :	 2;
		bdrkreg_t	me_reserved_3		  :	 4;
		bdrkreg_t	me_bad_syn		  :	 8;
		bdrkreg_t	me_reserved_4		  :	 7;
		bdrkreg_t	me_address		  :	30;
		bdrkreg_t	me_reserved_5		  :	 3;
	} md_mem_error_fld_s;
} md_mem_error_u_t;

#endif




/************************************************************************
 *                                                                      *
 * Description:  Contains information on read memory errors (both       *
 * correctable and uncorrectable) and write memory errors (always       *
 * uncorrectable). The errors are prioritized as follows:               *
 *  highest: uncorrectable read error (READ_UCE)                        *
 *  middle: write error (WRITE_UCE)                                     *
 *  lowest: correctable read error (READ_CE)                            *
 * Each type of error maintains a two-bit valid/overrun field           *
 * (READ_UCE, WRITE_UCE, or READ_CE). Bit 0 of each two-bit field       *
 * corresponds to the valid bit, and bit 1 of each two-bit field        *
 * corresponds to the overrun bit.                                      *
 * The rule for the valid bit is that it gets set whenever that error   *
 * occurs, regardless of whether a higher priority error has occured.   *
 * The rule for the overrun bit is that it gets set whenever we are     *
 * unable to record the address information for this particular         *
 * error, due to a previous error of the same or higher priority.       *
 * Note that the syndrome and address information always corresponds    *
 * to the earliest, highest priority error.                             *
 *  Finally, the UCE_DIFF_ADDR bit is set whenever there have been      *
 * several uncorrectable errors, to different cache line addresses.     *
 * If all the UCEs were to the same cache line address, then            *
 * UCE_DIFF_ADDR will be 0. This allows the operating system to         *
 * detect the case where a UCE error is read exclusively, and then      *
 * written back by the processor. If the bit is 0, it indicates that    *
 * no information has been lost about UCEs on other cache lines. In     *
 * particular, partial writes do a read modify write of the cache       *
 * line. A UCE read error will be set when the cache line is read,      *
 * and a UCE write error will occur when the cache line is written      *
 * back, but the UCE_DIFF_ADDR will not be set.                         *
 *                                                                      *
 ************************************************************************/




#ifdef LITTLE_ENDIAN

typedef union md_mem_error_clr_u {
	bdrkreg_t	md_mem_error_clr_regval;
	struct  {
		bdrkreg_t	mec_reserved_5            :	 3;
                bdrkreg_t       mec_address               :     30;
                bdrkreg_t       mec_reserved_4            :      7;
                bdrkreg_t       mec_bad_syn               :      8;
                bdrkreg_t       mec_reserved_3            :      4;
                bdrkreg_t       mec_read_ce               :      2;
                bdrkreg_t       mec_reserved_2            :      2;
                bdrkreg_t       mec_write_uce             :      2;
                bdrkreg_t       mec_reserved_1            :      2;
                bdrkreg_t       mec_read_uce              :      2;
                bdrkreg_t       mec_reserved              :      1;
                bdrkreg_t       mec_uce_diff_addr         :      1;
	} md_mem_error_clr_fld_s;
} md_mem_error_clr_u_t;

#else

typedef union md_mem_error_clr_u {
	bdrkreg_t	md_mem_error_clr_regval;
	struct	{
		bdrkreg_t	mec_uce_diff_addr	  :	 1;
		bdrkreg_t	mec_reserved		  :	 1;
		bdrkreg_t	mec_read_uce		  :	 2;
		bdrkreg_t	mec_reserved_1		  :	 2;
		bdrkreg_t	mec_write_uce		  :	 2;
		bdrkreg_t	mec_reserved_2		  :	 2;
		bdrkreg_t	mec_read_ce		  :	 2;
		bdrkreg_t	mec_reserved_3		  :	 4;
		bdrkreg_t	mec_bad_syn		  :	 8;
		bdrkreg_t	mec_reserved_4		  :	 7;
		bdrkreg_t	mec_address		  :	30;
		bdrkreg_t	mec_reserved_5		  :	 3;
	} md_mem_error_clr_fld_s;
} md_mem_error_clr_u_t;

#endif




/************************************************************************
 *                                                                      *
 *  Contains one-quarter of the error memory line failing BIST.         *
 *                                                                      *
 ************************************************************************/




#ifdef LITTLE_ENDIAN

typedef union md_bist_mb_err_data_0_u {
	bdrkreg_t	md_bist_mb_err_data_0_regval;
	struct  {
		bdrkreg_t	bmed0_mb_er_d             :	36;
		bdrkreg_t       bmed0_reserved            :     28;
	} md_bist_mb_err_data_0_fld_s;
} md_bist_mb_err_data_0_u_t;

#else

typedef union md_bist_mb_err_data_0_u {
	bdrkreg_t	md_bist_mb_err_data_0_regval;
	struct	{
		bdrkreg_t	bmed0_reserved		  :	28;
		bdrkreg_t	bmed0_mb_er_d		  :	36;
	} md_bist_mb_err_data_0_fld_s;
} md_bist_mb_err_data_0_u_t;

#endif




/************************************************************************
 *                                                                      *
 *  Contains one-quarter of the error memory line failing BIST.         *
 *                                                                      *
 ************************************************************************/




#ifdef LITTLE_ENDIAN

typedef union md_bist_mb_err_data_1_u {
	bdrkreg_t	md_bist_mb_err_data_1_regval;
	struct  {
		bdrkreg_t	bmed1_mb_er_d             :	36;
		bdrkreg_t       bmed1_reserved            :     28;
	} md_bist_mb_err_data_1_fld_s;
} md_bist_mb_err_data_1_u_t;

#else

typedef union md_bist_mb_err_data_1_u {
	bdrkreg_t	md_bist_mb_err_data_1_regval;
	struct	{
		bdrkreg_t	bmed1_reserved		  :	28;
		bdrkreg_t	bmed1_mb_er_d		  :	36;
	} md_bist_mb_err_data_1_fld_s;
} md_bist_mb_err_data_1_u_t;

#endif




/************************************************************************
 *                                                                      *
 *  Contains one-quarter of the error memory line failing BIST.         *
 *                                                                      *
 ************************************************************************/




#ifdef LITTLE_ENDIAN

typedef union md_bist_mb_err_data_2_u {
	bdrkreg_t	md_bist_mb_err_data_2_regval;
	struct  {
		bdrkreg_t	bmed2_mb_er_d             :	36;
		bdrkreg_t       bmed2_reserved            :     28;
	} md_bist_mb_err_data_2_fld_s;
} md_bist_mb_err_data_2_u_t;

#else

typedef union md_bist_mb_err_data_2_u {
	bdrkreg_t	md_bist_mb_err_data_2_regval;
	struct	{
		bdrkreg_t	bmed2_reserved		  :	28;
		bdrkreg_t	bmed2_mb_er_d		  :	36;
	} md_bist_mb_err_data_2_fld_s;
} md_bist_mb_err_data_2_u_t;

#endif




/************************************************************************
 *                                                                      *
 *  Contains one-quarter of the error memory line failing BIST.         *
 *                                                                      *
 ************************************************************************/




#ifdef LITTLE_ENDIAN

typedef union md_bist_mb_err_data_3_u {
	bdrkreg_t	md_bist_mb_err_data_3_regval;
	struct  {
		bdrkreg_t	bmed3_mb_er_d             :	36;
		bdrkreg_t       bmed3_reserved            :     28;
	} md_bist_mb_err_data_3_fld_s;
} md_bist_mb_err_data_3_u_t;

#else

typedef union md_bist_mb_err_data_3_u {
	bdrkreg_t	md_bist_mb_err_data_3_regval;
	struct	{
		bdrkreg_t	bmed3_reserved		  :	28;
		bdrkreg_t	bmed3_mb_er_d		  :	36;
	} md_bist_mb_err_data_3_fld_s;
} md_bist_mb_err_data_3_u_t;

#endif




/************************************************************************
 *                                                                      *
 *  Contains 1 bit that allow the selection of MB debug information     *
 * at the debug port (see the design specification for the available    *
 * debug information).                                                  *
 *                                                                      *
 ************************************************************************/




#ifdef LITTLE_ENDIAN

typedef union md_mb_debug_u {
	bdrkreg_t	md_mb_debug_regval;
	struct  {
		bdrkreg_t	md_mb_debug_sel           :	 1;
		bdrkreg_t       md_reserved               :     63;
	} md_mb_debug_fld_s;
} md_mb_debug_u_t;

#else

typedef union md_mb_debug_u {
	bdrkreg_t	md_mb_debug_regval;
	struct	{
		bdrkreg_t	md_reserved		  :	63;
		bdrkreg_t	md_mb_debug_sel		  :	 1;
	} md_mb_debug_fld_s;
} md_mb_debug_u_t;

#endif






#endif /* _LANGUAGE_C */

/************************************************************************
 *                                                                      *
 *               MAKE ALL ADDITIONS AFTER THIS LINE                     *
 *                                                                      *
 ************************************************************************/




#endif /* _ASM_SN_SN1_HUBMD_H */
