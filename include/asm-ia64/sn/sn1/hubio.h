/* $Id$
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1992 - 1997, 2000 Silicon Graphics, Inc.
 * Copyright (C) 2000 by Colin Ngam
 */

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


#ifndef _ASM_SN_SN1_HUBIO_H
#define _ASM_SN_SN1_HUBIO_H


#define    IIO_WID                   0x00400000    /*
                                                    * Crosstalk Widget
                                                    * Identification This
                                                    * register is also
                                                    * accessible from
                                                    * Crosstalk at
                                                    * address 0x0.
                                                    */



#define    IIO_WSTAT                 0x00400008    /*
                                                    * Crosstalk Widget
                                                    * Status
                                                    */



#define    IIO_WCR                   0x00400020    /*
                                                    * Crosstalk Widget
                                                    * Control Register
                                                    */



#define    IIO_ILAPR                 0x00400100    /*
                                                    * IO Local Access
                                                    * Protection Register
                                                    */



#define    IIO_ILAPO                 0x00400108    /*
                                                    * IO Local Access
                                                    * Protection Override
                                                    */



#define    IIO_IOWA                  0x00400110    /*
                                                    * IO Outbound Widget
                                                    * Access
                                                    */



#define    IIO_IIWA                  0x00400118    /*
                                                    * IO Inbound Widget
                                                    * Access
                                                    */



#define    IIO_IIDEM                 0x00400120    /*
                                                    * IO Inbound Device
                                                    * Error Mask
                                                    */



#define    IIO_ILCSR                 0x00400128    /*
                                                    * IO LLP Control and
                                                    * Status Register
                                                    */



#define    IIO_ILLR                  0x00400130    /* IO LLP Log Register    */



#define    IIO_IIDSR                 0x00400138    /*
                                                    * IO Interrupt
                                                    * Destination
                                                    */



#define    IIO_IGFX0                 0x00400140    /*
                                                    * IO Graphics
                                                    * Node-Widget Map 0
                                                    */



#define    IIO_IGFX1                 0x00400148    /*
                                                    * IO Graphics
                                                    * Node-Widget Map 1
                                                    */



#define    IIO_ISCR0                 0x00400150    /*
                                                    * IO Scratch Register
                                                    * 0
                                                    */



#define    IIO_ISCR1                 0x00400158    /*
                                                    * IO Scratch Register
                                                    * 1
                                                    */



#define    IIO_ITTE1                 0x00400160    /*
                                                    * IO Translation
                                                    * Table Entry 1
                                                    */



#define    IIO_ITTE2                 0x00400168    /*
                                                    * IO Translation
                                                    * Table Entry 2
                                                    */



#define    IIO_ITTE3                 0x00400170    /*
                                                    * IO Translation
                                                    * Table Entry 3
                                                    */



#define    IIO_ITTE4                 0x00400178    /*
                                                    * IO Translation
                                                    * Table Entry 4
                                                    */



#define    IIO_ITTE5                 0x00400180    /*
                                                    * IO Translation
                                                    * Table Entry 5
                                                    */



#define    IIO_ITTE6                 0x00400188    /*
                                                    * IO Translation
                                                    * Table Entry 6
                                                    */



#define    IIO_ITTE7                 0x00400190    /*
                                                    * IO Translation
                                                    * Table Entry 7
                                                    */



#define    IIO_IPRB0                 0x00400198    /* IO PRB Entry 0         */



#define    IIO_IPRB8                 0x004001A0    /* IO PRB Entry 8         */



#define    IIO_IPRB9                 0x004001A8    /* IO PRB Entry 9         */



#define    IIO_IPRBA                 0x004001B0    /* IO PRB Entry A         */



#define    IIO_IPRBB                 0x004001B8    /* IO PRB Entry B         */



#define    IIO_IPRBC                 0x004001C0    /* IO PRB Entry C         */



#define    IIO_IPRBD                 0x004001C8    /* IO PRB Entry D         */



#define    IIO_IPRBE                 0x004001D0    /* IO PRB Entry E         */



#define    IIO_IPRBF                 0x004001D8    /* IO PRB Entry F         */



#define    IIO_IXCC                  0x004001E0    /*
                                                    * IO Crosstalk Credit
                                                    * Count Timeout
                                                    */



#define    IIO_IMEM                  0x004001E8    /*
                                                    * IO Miscellaneous
                                                    * Error Mask
                                                    */



#define    IIO_IXTT                  0x004001F0    /*
                                                    * IO Crosstalk
                                                    * Timeout Threshold
                                                    */



#define    IIO_IECLR                 0x004001F8    /*
                                                    * IO Error Clear
                                                    * Register
                                                    */



#define    IIO_IBCR                  0x00400200    /*
                                                    * IO BTE Control
                                                    * Register
                                                    */



#define    IIO_IXSM                  0x00400208    /*
                                                    * IO Crosstalk
                                                    * Spurious Message
                                                    */



#define    IIO_IXSS                  0x00400210    /*
                                                    * IO Crosstalk
                                                    * Spurious Sideband
                                                    */



#define    IIO_ILCT                  0x00400218    /* IO LLP Channel Test    */



#define    IIO_IIEPH1                0x00400220    /*
                                                    * IO Incoming Error
                                                    * Packet Header, Part
                                                    * 1
                                                    */



#define    IIO_IIEPH2                0x00400228    /*
                                                    * IO Incoming Error
                                                    * Packet Header, Part
                                                    * 2
                                                    */



#define    IIO_IPCA                  0x00400300    /*
                                                    * IO PRB Counter
                                                    * Adjust
                                                    */



#define    IIO_IPRTE0                0x00400308    /*
                                                    * IO PIO Read Address
                                                    * Table Entry 0
                                                    */



#define    IIO_IPRTE1                0x00400310    /*
                                                    * IO PIO Read Address
                                                    * Table Entry 1
                                                    */



#define    IIO_IPRTE2                0x00400318    /*
                                                    * IO PIO Read Address
                                                    * Table Entry 2
                                                    */



#define    IIO_IPRTE3                0x00400320    /*
                                                    * IO PIO Read Address
                                                    * Table Entry 3
                                                    */



#define    IIO_IPRTE4                0x00400328    /*
                                                    * IO PIO Read Address
                                                    * Table Entry 4
                                                    */



#define    IIO_IPRTE5                0x00400330    /*
                                                    * IO PIO Read Address
                                                    * Table Entry 5
                                                    */



#define    IIO_IPRTE6                0x00400338    /*
                                                    * IO PIO Read Address
                                                    * Table Entry 6
                                                    */



#define    IIO_IPRTE7                0x00400340    /*
                                                    * IO PIO Read Address
                                                    * Table Entry 7
                                                    */



#define    IIO_IPDR                  0x00400388    /*
                                                    * IO PIO Deallocation
                                                    * Register
                                                    */



#define    IIO_ICDR                  0x00400390    /*
                                                    * IO CRB Entry
                                                    * Deallocation
                                                    * Register
                                                    */



#define    IIO_IFDR                  0x00400398    /*
                                                    * IO IOQ FIFO Depth
                                                    * Register
                                                    */



#define    IIO_IIAP                  0x004003A0    /*
                                                    * IO IIQ Arbitration
                                                    * Parameters
                                                    */



#define    IIO_ICMR                  0x004003A8    /*
                                                    * IO CRB Management
                                                    * Register
                                                    */



#define    IIO_ICCR                  0x004003B0    /*
                                                    * IO CRB Control
                                                    * Register
                                                    */



#define    IIO_ICTO                  0x004003B8    /* IO CRB Timeout         */



#define    IIO_ICTP                  0x004003C0    /*
                                                    * IO CRB Timeout
                                                    * Prescalar
                                                    */



#define    IIO_ICRB0_A               0x00400400    /* IO CRB Entry 0_A       */



#define    IIO_ICRB0_B               0x00400408    /* IO CRB Entry 0_B       */



#define    IIO_ICRB0_C               0x00400410    /* IO CRB Entry 0_C       */



#define    IIO_ICRB0_D               0x00400418    /* IO CRB Entry 0_D       */



#define    IIO_ICRB1_A               0x00400420    /* IO CRB Entry 1_A       */



#define    IIO_ICRB1_B               0x00400428    /* IO CRB Entry 1_B       */



#define    IIO_ICRB1_C               0x00400430    /* IO CRB Entry 1_C       */



#define    IIO_ICRB1_D               0x00400438    /* IO CRB Entry 1_D       */



#define    IIO_ICRB2_A               0x00400440    /* IO CRB Entry 2_A       */



#define    IIO_ICRB2_B               0x00400448    /* IO CRB Entry 2_B       */



#define    IIO_ICRB2_C               0x00400450    /* IO CRB Entry 2_C       */



#define    IIO_ICRB2_D               0x00400458    /* IO CRB Entry 2_D       */



#define    IIO_ICRB3_A               0x00400460    /* IO CRB Entry 3_A       */



#define    IIO_ICRB3_B               0x00400468    /* IO CRB Entry 3_B       */



#define    IIO_ICRB3_C               0x00400470    /* IO CRB Entry 3_C       */



#define    IIO_ICRB3_D               0x00400478    /* IO CRB Entry 3_D       */



#define    IIO_ICRB4_A               0x00400480    /* IO CRB Entry 4_A       */



#define    IIO_ICRB4_B               0x00400488    /* IO CRB Entry 4_B       */



#define    IIO_ICRB4_C               0x00400490    /* IO CRB Entry 4_C       */



#define    IIO_ICRB4_D               0x00400498    /* IO CRB Entry 4_D       */



#define    IIO_ICRB5_A               0x004004A0    /* IO CRB Entry 5_A       */



#define    IIO_ICRB5_B               0x004004A8    /* IO CRB Entry 5_B       */



#define    IIO_ICRB5_C               0x004004B0    /* IO CRB Entry 5_C       */



#define    IIO_ICRB5_D               0x004004B8    /* IO CRB Entry 5_D       */



#define    IIO_ICRB6_A               0x004004C0    /* IO CRB Entry 6_A       */



#define    IIO_ICRB6_B               0x004004C8    /* IO CRB Entry 6_B       */



#define    IIO_ICRB6_C               0x004004D0    /* IO CRB Entry 6_C       */



#define    IIO_ICRB6_D               0x004004D8    /* IO CRB Entry 6_D       */



#define    IIO_ICRB7_A               0x004004E0    /* IO CRB Entry 7_A       */



#define    IIO_ICRB7_B               0x004004E8    /* IO CRB Entry 7_B       */



#define    IIO_ICRB7_C               0x004004F0    /* IO CRB Entry 7_C       */



#define    IIO_ICRB7_D               0x004004F8    /* IO CRB Entry 7_D       */



#define    IIO_ICRB8_A               0x00400500    /* IO CRB Entry 8_A       */



#define    IIO_ICRB8_B               0x00400508    /* IO CRB Entry 8_B       */



#define    IIO_ICRB8_C               0x00400510    /* IO CRB Entry 8_C       */



#define    IIO_ICRB8_D               0x00400518    /* IO CRB Entry 8_D       */



#define    IIO_ICRB9_A               0x00400520    /* IO CRB Entry 9_A       */



#define    IIO_ICRB9_B               0x00400528    /* IO CRB Entry 9_B       */



#define    IIO_ICRB9_C               0x00400530    /* IO CRB Entry 9_C       */



#define    IIO_ICRB9_D               0x00400538    /* IO CRB Entry 9_D       */



#define    IIO_ICRBA_A               0x00400540    /* IO CRB Entry A_A       */



#define    IIO_ICRBA_B               0x00400548    /* IO CRB Entry A_B       */



#define    IIO_ICRBA_C               0x00400550    /* IO CRB Entry A_C       */



#define    IIO_ICRBA_D               0x00400558    /* IO CRB Entry A_D       */



#define    IIO_ICRBB_A               0x00400560    /* IO CRB Entry B_A       */



#define    IIO_ICRBB_B               0x00400568    /* IO CRB Entry B_B       */



#define    IIO_ICRBB_C               0x00400570    /* IO CRB Entry B_C       */



#define    IIO_ICRBB_D               0x00400578    /* IO CRB Entry B_D       */



#define    IIO_ICRBC_A               0x00400580    /* IO CRB Entry C_A       */



#define    IIO_ICRBC_B               0x00400588    /* IO CRB Entry C_B       */



#define    IIO_ICRBC_C               0x00400590    /* IO CRB Entry C_C       */



#define    IIO_ICRBC_D               0x00400598    /* IO CRB Entry C_D       */



#define    IIO_ICRBD_A               0x004005A0    /* IO CRB Entry D_A       */



#define    IIO_ICRBD_B               0x004005A8    /* IO CRB Entry D_B       */



#define    IIO_ICRBD_C               0x004005B0    /* IO CRB Entry D_C       */



#define    IIO_ICRBD_D               0x004005B8    /* IO CRB Entry D_D       */



#define    IIO_ICRBE_A               0x004005C0    /* IO CRB Entry E_A       */



#define    IIO_ICRBE_B               0x004005C8    /* IO CRB Entry E_B       */



#define    IIO_ICRBE_C               0x004005D0    /* IO CRB Entry E_C       */



#define    IIO_ICRBE_D               0x004005D8    /* IO CRB Entry E_D       */



#define    IIO_ICSML                 0x00400600    /*
                                                    * IO CRB Spurious
                                                    * Message Low
                                                    */



#define    IIO_ICSMH                 0x00400608    /*
                                                    * IO CRB Spurious
                                                    * Message High
                                                    */



#define    IIO_IDBSS                 0x00400610    /*
                                                    * IO Debug Submenu
                                                    * Select
                                                    */



#define    IIO_IBLS0                 0x00410000    /*
                                                    * IO BTE Length
                                                    * Status 0
                                                    */



#define    IIO_IBSA0                 0x00410008    /*
                                                    * IO BTE Source
                                                    * Address 0
                                                    */



#define    IIO_IBDA0                 0x00410010    /*
                                                    * IO BTE Destination
                                                    * Address 0
                                                    */



#define    IIO_IBCT0                 0x00410018    /*
                                                    * IO BTE Control
                                                    * Terminate 0
                                                    */



#define    IIO_IBNA0                 0x00410020    /*
                                                    * IO BTE Notification
                                                    * Address 0
                                                    */



#define    IIO_IBIA0                 0x00410028    /*
                                                    * IO BTE Interrupt
                                                    * Address 0
                                                    */



#define    IIO_IBLS1                 0x00420000    /*
                                                    * IO BTE Length
                                                    * Status 1
                                                    */



#define    IIO_IBSA1                 0x00420008    /*
                                                    * IO BTE Source
                                                    * Address 1
                                                    */



#define    IIO_IBDA1                 0x00420010    /*
                                                    * IO BTE Destination
                                                    * Address 1
                                                    */



#define    IIO_IBCT1                 0x00420018    /*
                                                    * IO BTE Control
                                                    * Terminate 1
                                                    */



#define    IIO_IBNA1                 0x00420020    /*
                                                    * IO BTE Notification
                                                    * Address 1
                                                    */



#define    IIO_IBIA1                 0x00420028    /*
                                                    * IO BTE Interrupt
                                                    * Address 1
                                                    */



#define    IIO_IPCR                  0x00430000    /*
                                                    * IO Performance
                                                    * Control
                                                    */



#define    IIO_IPPR                  0x00430008    /*
                                                    * IO Performance
                                                    * Profiling
                                                    */





#ifdef _LANGUAGE_C

/************************************************************************
 *                                                                      *
 * Description:  This register echoes some information from the         *
 * LB_REV_ID register. It is available through Crosstalk as described   *
 * above. The REV_NUM and MFG_NUM fields receive their values from      *
 * the REVISION and MANUFACTURER fields in the LB_REV_ID register.      *
 * The PART_NUM field's value is the Crosstalk device ID number that    *
 * Steve Miller assigned to the Bedrock chip.                           *
 *                                                                      *
 ************************************************************************/




#ifdef LITTLE_ENDIAN

typedef union ii_wid_u {
	bdrkreg_t	ii_wid_regval;
	struct	{
		bdrkreg_t	w_rsvd_1		  :	 1;
		bdrkreg_t	w_mfg_num		  :	11;
		bdrkreg_t	w_part_num		  :	16;
		bdrkreg_t	w_rev_num		  :	 4;
		bdrkreg_t	w_rsvd			  :	32;
	} ii_wid_fld_s;
} ii_wid_u_t;

#else

typedef union ii_wid_u {
	bdrkreg_t	ii_wid_regval;
	struct  {
		bdrkreg_t	w_rsvd                    :	32;
		bdrkreg_t	w_rev_num                 :	 4;
		bdrkreg_t	w_part_num                :	16;
		bdrkreg_t	w_mfg_num                 :	11;
		bdrkreg_t	w_rsvd_1                  :	 1;
	} ii_wid_fld_s;
} ii_wid_u_t;

#endif




/************************************************************************
 *                                                                      *
 *  The fields in this register are set upon detection of an error      *
 * and cleared by various mechanisms, as explained in the               *
 * description.                                                         *
 *                                                                      *
 ************************************************************************/




#ifdef LITTLE_ENDIAN

typedef union ii_wstat_u {
	bdrkreg_t	ii_wstat_regval;
	struct	{
		bdrkreg_t	w_pending		  :	 4;
		bdrkreg_t	w_xt_crd_to		  :	 1;
		bdrkreg_t	w_xt_tail_to		  :	 1;
		bdrkreg_t	w_rsvd_3		  :	 3;
		bdrkreg_t       w_tx_mx_rty               :      1;
		bdrkreg_t	w_rsvd_2		  :	 6;
		bdrkreg_t	w_llp_tx_cnt		  :	 8;
		bdrkreg_t	w_rsvd_1		  :	 8;
		bdrkreg_t	w_crazy			  :	 1;
		bdrkreg_t	w_rsvd			  :	31;
	} ii_wstat_fld_s;
} ii_wstat_u_t;

#else

typedef union ii_wstat_u {
	bdrkreg_t	ii_wstat_regval;
	struct  {
		bdrkreg_t	w_rsvd                    :	31;
		bdrkreg_t	w_crazy                   :	 1;
		bdrkreg_t	w_rsvd_1                  :	 8;
		bdrkreg_t	w_llp_tx_cnt              :	 8;
		bdrkreg_t	w_rsvd_2                  :	 6;
		bdrkreg_t	w_tx_mx_rty               :	 1;
		bdrkreg_t	w_rsvd_3                  :	 3;
		bdrkreg_t	w_xt_tail_to              :	 1;
		bdrkreg_t	w_xt_crd_to               :	 1;
		bdrkreg_t	w_pending                 :	 4;
	} ii_wstat_fld_s;
} ii_wstat_u_t;

#endif




/************************************************************************
 *                                                                      *
 * Description:  This is a read-write enabled register. It controls     *
 * various aspects of the Crosstalk flow control.                       *
 *                                                                      *
 ************************************************************************/




#ifdef LITTLE_ENDIAN

typedef union ii_wcr_u {
	bdrkreg_t	ii_wcr_regval;
	struct	{
		bdrkreg_t	w_wid			  :	 4;
		bdrkreg_t	w_tag			  :	 1;
		bdrkreg_t	w_rsvd_1		  :	 8;
		bdrkreg_t	w_dst_crd		  :	 3;
		bdrkreg_t	w_f_bad_pkt		  :	 1;
		bdrkreg_t	w_dir_con		  :	 1;
		bdrkreg_t	w_e_thresh		  :	 5;
		bdrkreg_t	w_rsvd			  :	41;
	} ii_wcr_fld_s;
} ii_wcr_u_t;

#else

typedef union ii_wcr_u {
	bdrkreg_t	ii_wcr_regval;
	struct  {
		bdrkreg_t	w_rsvd                    :	41;
		bdrkreg_t	w_e_thresh                :	 5;
		bdrkreg_t	w_dir_con                 :	 1;
		bdrkreg_t	w_f_bad_pkt               :	 1;
		bdrkreg_t	w_dst_crd                 :	 3;
		bdrkreg_t	w_rsvd_1                  :	 8;
		bdrkreg_t	w_tag                     :	 1;
		bdrkreg_t	w_wid                     :	 4;
	} ii_wcr_fld_s;
} ii_wcr_u_t;

#endif




/************************************************************************
 *                                                                      *
 * Description:  This register's value is a bit vector that guards      *
 * access to local registers within the II as well as to external       *
 * Crosstalk widgets. Each bit in the register corresponds to a         *
 * particular region in the system; a region consists of one, two or    *
 * four nodes (depending on the value of the REGION_SIZE field in the   *
 * LB_REV_ID register, which is documented in Section 8.3.1.1). The     *
 * protection provided by this register applies to PIO read             *
 * operations as well as PIO write operations. The II will perform a    *
 * PIO read or write request only if the bit for the requestor's        *
 * region is set; otherwise, the II will not perform the requested      *
 * operation and will return an error response. When a PIO read or      *
 * write request targets an external Crosstalk widget, then not only    *
 * must the bit for the requestor's region be set in the ILAPR, but     *
 * also the target widget's bit in the IOWA register must be set in     *
 * order for the II to perform the requested operation; otherwise,      *
 * the II will return an error response. Hence, the protection          *
 * provided by the IOWA register supplements the protection provided    *
 * by the ILAPR for requests that target external Crosstalk widgets.    *
 * This register itself can be accessed only by the nodes whose         *
 * region ID bits are enabled in this same register. It can also be     *
 * accessed through the IAlias space by the local processors.           *
 * The reset value of this register allows access by all nodes.         *
 *                                                                      *
 ************************************************************************/




typedef union ii_ilapr_u {
	bdrkreg_t	ii_ilapr_regval;
	struct  {
		bdrkreg_t	i_region                  :	64;
	} ii_ilapr_fld_s;
} ii_ilapr_u_t;




/************************************************************************
 *                                                                      *
 * Description:  A write to this register of the 64-bit value           *
 * "SGIrules" in ASCII, will cause the bit in the ILAPR register        *
 * corresponding to the region of the requestor to be set (allow        *
 * access). A write of any other value will be ignored. Access          *
 * protection for this register is "SGIrules".                          *
 * This register can also be accessed through the IAlias space.         *
 * However, this access will not change the access permissions in the   *
 * ILAPR.                                                               *
 *                                                                      *
 ************************************************************************/




#ifdef LITTLE_ENDIAN

typedef union ii_ilapo_u {
	bdrkreg_t	ii_ilapo_regval;
	struct	{
		bdrkreg_t	i_io_ovrride		  :	 9;
		bdrkreg_t	i_rsvd			  :	55;
	} ii_ilapo_fld_s;
} ii_ilapo_u_t;

#else

typedef union ii_ilapo_u {
	bdrkreg_t	ii_ilapo_regval;
	struct  {
		bdrkreg_t	i_rsvd                    :	55;
		bdrkreg_t	i_io_ovrride              :	 9;
	} ii_ilapo_fld_s;
} ii_ilapo_u_t;

#endif




/************************************************************************
 *                                                                      *
 *  This register qualifies all the PIO and Graphics writes launched    *
 * from the Bedrock towards a widget.                                   *
 *                                                                      *
 ************************************************************************/




#ifdef LITTLE_ENDIAN

typedef union ii_iowa_u {
	bdrkreg_t	ii_iowa_regval;
	struct	{
		bdrkreg_t	i_w0_oac		  :	 1;
		bdrkreg_t	i_rsvd_1		  :	 7;
                bdrkreg_t       i_wx_oac                  :      8;
		bdrkreg_t	i_rsvd			  :	48;
	} ii_iowa_fld_s;
} ii_iowa_u_t;

#else

typedef union ii_iowa_u {
	bdrkreg_t	ii_iowa_regval;
	struct  {
		bdrkreg_t	i_rsvd                    :	48;
		bdrkreg_t	i_wx_oac                  :	 8;
		bdrkreg_t	i_rsvd_1                  :	 7;
		bdrkreg_t	i_w0_oac                  :	 1;
	} ii_iowa_fld_s;
} ii_iowa_u_t;

#endif




/************************************************************************
 *                                                                      *
 * Description:  This register qualifies all the requests launched      *
 * from a widget towards the Bedrock. This register is intended to be   *
 * used by software in case of misbehaving widgets.                     *
 *                                                                      *
 *                                                                      *
 ************************************************************************/




#ifdef LITTLE_ENDIAN

typedef union ii_iiwa_u {
	bdrkreg_t	ii_iiwa_regval;
	struct  {
		bdrkreg_t	i_w0_iac                  :	 1;
		bdrkreg_t	i_rsvd_1		  :	 7;
		bdrkreg_t	i_wx_iac		  :	 8;
		bdrkreg_t	i_rsvd			  :	48;
	} ii_iiwa_fld_s;
} ii_iiwa_u_t;

#else

typedef union ii_iiwa_u {
	bdrkreg_t	ii_iiwa_regval;
	struct	{
		bdrkreg_t	i_rsvd			  :	48;
		bdrkreg_t	i_wx_iac		  :	 8;
		bdrkreg_t	i_rsvd_1		  :	 7;
		bdrkreg_t	i_w0_iac		  :	 1;
	} ii_iiwa_fld_s;
} ii_iiwa_u_t;

#endif




/************************************************************************
 *                                                                      *
 * Description:  This register qualifies all the operations launched    *
 * from a widget towards the Bedrock. It allows individual access       *
 * control for up to 8 devices per widget. A device refers to           *
 * individual DMA master hosted by a widget.                            *
 * The bits in each field of this register are cleared by the Bedrock   *
 * upon detection of an error which requires the device to be           *
 * disabled. These fields assume that 0=TNUM=7 (i.e., Bridge-centric    *
 * Crosstalk). Whether or not a device has access rights to this        *
 * Bedrock is determined by an AND of the device enable bit in the      *
 * appropriate field of this register and the corresponding bit in      *
 * the Wx_IAC field (for the widget which this device belongs to).      *
 * The bits in this field are set by writing a 1 to them. Incoming      *
 * replies from Crosstalk are not subject to this access control        *
 * mechanism.                                                           *
 *                                                                      *
 ************************************************************************/




#ifdef LITTLE_ENDIAN

typedef union ii_iidem_u {
	bdrkreg_t	ii_iidem_regval;
	struct	{
		bdrkreg_t	i_w8_dxs		  :	 8;
		bdrkreg_t	i_w9_dxs		  :	 8;
		bdrkreg_t	i_wa_dxs		  :	 8;
		bdrkreg_t	i_wb_dxs		  :	 8;
		bdrkreg_t	i_wc_dxs		  :	 8;
		bdrkreg_t	i_wd_dxs		  :	 8;
		bdrkreg_t	i_we_dxs		  :	 8;
		bdrkreg_t	i_wf_dxs		  :	 8;
	} ii_iidem_fld_s;
} ii_iidem_u_t;

#else

typedef union ii_iidem_u {
	bdrkreg_t	ii_iidem_regval;
	struct  {
		bdrkreg_t	i_wf_dxs                  :	 8;
		bdrkreg_t	i_we_dxs                  :	 8;
		bdrkreg_t	i_wd_dxs                  :	 8;
		bdrkreg_t	i_wc_dxs                  :	 8;
		bdrkreg_t	i_wb_dxs                  :	 8;
		bdrkreg_t	i_wa_dxs                  :	 8;
		bdrkreg_t	i_w9_dxs                  :	 8;
		bdrkreg_t	i_w8_dxs                  :	 8;
	} ii_iidem_fld_s;
} ii_iidem_u_t;

#endif




/************************************************************************
 *                                                                      *
 *  This register contains the various programmable fields necessary    *
 * for controlling and observing the LLP signals.                       *
 *                                                                      *
 ************************************************************************/




#ifdef LITTLE_ENDIAN

typedef union ii_ilcsr_u {
	bdrkreg_t	ii_ilcsr_regval;
	struct  {
		bdrkreg_t	i_nullto                  :	 6;
		bdrkreg_t	i_rsvd_4		  :	 2;
		bdrkreg_t	i_wrmrst		  :	 1;
		bdrkreg_t	i_rsvd_3		  :	 1;
		bdrkreg_t	i_llp_en		  :	 1;
		bdrkreg_t	i_bm8			  :	 1;
		bdrkreg_t	i_llp_stat		  :	 2;
		bdrkreg_t	i_remote_power		  :	 1;
		bdrkreg_t	i_rsvd_2		  :	 1;
		bdrkreg_t	i_maxrtry		  :	10;
		bdrkreg_t	i_d_avail_sel		  :	 2;
		bdrkreg_t	i_rsvd_1		  :	 4;
		bdrkreg_t	i_maxbrst		  :	10;
                bdrkreg_t       i_rsvd                    :     22;

	} ii_ilcsr_fld_s;
} ii_ilcsr_u_t;

#else

typedef union ii_ilcsr_u {
	bdrkreg_t	ii_ilcsr_regval;
	struct	{
		bdrkreg_t	i_rsvd			  :	22;
		bdrkreg_t	i_maxbrst		  :	10;
		bdrkreg_t	i_rsvd_1		  :	 4;
		bdrkreg_t	i_d_avail_sel		  :	 2;
		bdrkreg_t	i_maxrtry		  :	10;
		bdrkreg_t	i_rsvd_2		  :	 1;
		bdrkreg_t	i_remote_power		  :	 1;
		bdrkreg_t	i_llp_stat		  :	 2;
		bdrkreg_t	i_bm8			  :	 1;
		bdrkreg_t	i_llp_en		  :	 1;
		bdrkreg_t	i_rsvd_3		  :	 1;
		bdrkreg_t	i_wrmrst		  :	 1;
		bdrkreg_t	i_rsvd_4		  :	 2;
		bdrkreg_t	i_nullto		  :	 6;
	} ii_ilcsr_fld_s;
} ii_ilcsr_u_t;

#endif




/************************************************************************
 *                                                                      *
 *  This is simply a status registers that monitors the LLP error       *
 * rate.                                                                *
 *                                                                      *
 ************************************************************************/




#ifdef LITTLE_ENDIAN

typedef union ii_illr_u {
	bdrkreg_t	ii_illr_regval;
	struct	{
		bdrkreg_t	i_sn_cnt		  :	16;
		bdrkreg_t	i_cb_cnt		  :	16;
		bdrkreg_t	i_rsvd			  :	32;
	} ii_illr_fld_s;
} ii_illr_u_t;

#else

typedef union ii_illr_u {
	bdrkreg_t	ii_illr_regval;
	struct  {
		bdrkreg_t	i_rsvd                    :	32;
		bdrkreg_t	i_cb_cnt                  :	16;
		bdrkreg_t	i_sn_cnt                  :	16;
	} ii_illr_fld_s;
} ii_illr_u_t;

#endif




/************************************************************************
 *                                                                      *
 * Description:  All II-detected non-BTE error interrupts are           *
 * specified via this register.                                         *
 * NOTE: The PI interrupt register address is hardcoded in the II. If   *
 * PI_ID==0, then the II sends an interrupt request (Duplonet PWRI      *
 * packet) to address offset 0x0180_0090 within the local register      *
 * address space of PI0 on the node specified by the NODE field. If     *
 * PI_ID==1, then the II sends the interrupt request to address         *
 * offset 0x01A0_0090 within the local register address space of PI1    *
 * on the node specified by the NODE field.                             *
 *                                                                      *
 ************************************************************************/




#ifdef LITTLE_ENDIAN

typedef union ii_iidsr_u {
	bdrkreg_t	ii_iidsr_regval;
	struct  {
		bdrkreg_t	i_level                   :	 7;
		bdrkreg_t	i_rsvd_4		  :	 1;
		bdrkreg_t       i_pi_id                   :      1;
		bdrkreg_t	i_node			  :	 8;
		bdrkreg_t       i_rsvd_3                  :      7;
		bdrkreg_t	i_enable		  :	 1;
		bdrkreg_t	i_rsvd_2		  :	 3;
		bdrkreg_t	i_int_sent		  :	 1;
		bdrkreg_t       i_rsvd_1                  :      3;
		bdrkreg_t	i_pi0_forward_int	  :	 1;
		bdrkreg_t	i_pi1_forward_int	  :	 1;
		bdrkreg_t	i_rsvd			  :	30;
	} ii_iidsr_fld_s;
} ii_iidsr_u_t;

#else

typedef union ii_iidsr_u {
	bdrkreg_t	ii_iidsr_regval;
	struct	{
		bdrkreg_t	i_rsvd			  :	30;
		bdrkreg_t	i_pi1_forward_int	  :	 1;
		bdrkreg_t	i_pi0_forward_int	  :	 1;
		bdrkreg_t	i_rsvd_1		  :	 3;
		bdrkreg_t	i_int_sent		  :	 1;
		bdrkreg_t	i_rsvd_2		  :	 3;
		bdrkreg_t	i_enable		  :	 1;
		bdrkreg_t	i_rsvd_3		  :	 7;
		bdrkreg_t	i_node			  :	 8;
		bdrkreg_t	i_pi_id			  :	 1;
		bdrkreg_t	i_rsvd_4		  :	 1;
		bdrkreg_t	i_level			  :	 7;
	} ii_iidsr_fld_s;
} ii_iidsr_u_t;

#endif




/************************************************************************
 *                                                                      *
 *  There are two instances of this register. This register is used     *
 * for matching up the incoming responses from the graphics widget to   *
 * the processor that initiated the graphics operation. The             *
 * write-responses are converted to graphics credits and returned to    *
 * the processor so that the processor interface can manage the flow    *
 * control.                                                             *
 *                                                                      *
 ************************************************************************/




#ifdef LITTLE_ENDIAN

typedef union ii_igfx0_u {
	bdrkreg_t	ii_igfx0_regval;
	struct	{
		bdrkreg_t	i_w_num			  :	 4;
		bdrkreg_t       i_pi_id                   :      1;
		bdrkreg_t	i_n_num			  :	 8;
		bdrkreg_t       i_rsvd_1                  :      3;
		bdrkreg_t       i_p_num                   :      1;
		bdrkreg_t       i_rsvd                    :     47;
	} ii_igfx0_fld_s;
} ii_igfx0_u_t;

#else

typedef union ii_igfx0_u {
	bdrkreg_t	ii_igfx0_regval;
	struct  {
		bdrkreg_t	i_rsvd                    :	47;
		bdrkreg_t	i_p_num                   :	 1;
		bdrkreg_t	i_rsvd_1                  :	 3;
		bdrkreg_t	i_n_num                   :	 8;
		bdrkreg_t	i_pi_id                   :	 1;
		bdrkreg_t	i_w_num                   :	 4;
	} ii_igfx0_fld_s;
} ii_igfx0_u_t;

#endif




/************************************************************************
 *                                                                      *
 *  There are two instances of this register. This register is used     *
 * for matching up the incoming responses from the graphics widget to   *
 * the processor that initiated the graphics operation. The             *
 * write-responses are converted to graphics credits and returned to    *
 * the processor so that the processor interface can manage the flow    *
 * control.                                                             *
 *                                                                      *
 ************************************************************************/




#ifdef LITTLE_ENDIAN

typedef union ii_igfx1_u {
	bdrkreg_t	ii_igfx1_regval;
	struct  {
		bdrkreg_t	i_w_num                   :	 4;
		bdrkreg_t	i_pi_id			  :	 1;
		bdrkreg_t	i_n_num			  :	 8;
		bdrkreg_t	i_rsvd_1		  :	 3;
		bdrkreg_t	i_p_num			  :	 1;
		bdrkreg_t	i_rsvd			  :	47;
	} ii_igfx1_fld_s;
} ii_igfx1_u_t;

#else

typedef union ii_igfx1_u {
	bdrkreg_t	ii_igfx1_regval;
	struct	{
		bdrkreg_t	i_rsvd			  :	47;
		bdrkreg_t	i_p_num			  :	 1;
		bdrkreg_t	i_rsvd_1		  :	 3;
		bdrkreg_t	i_n_num			  :	 8;
		bdrkreg_t	i_pi_id			  :	 1;
		bdrkreg_t	i_w_num			  :	 4;
	} ii_igfx1_fld_s;
} ii_igfx1_u_t;

#endif




/************************************************************************
 *                                                                      *
 *  There are two instances of this registers. These registers are      *
 * used as scratch registers for software use.                          *
 *                                                                      *
 ************************************************************************/




typedef union ii_iscr0_u {
	bdrkreg_t	ii_iscr0_regval;
	struct  {
		bdrkreg_t	i_scratch                 :	64;
	} ii_iscr0_fld_s;
} ii_iscr0_u_t;




/************************************************************************
 *                                                                      *
 *  There are two instances of this registers. These registers are      *
 * used as scratch registers for software use.                          *
 *                                                                      *
 ************************************************************************/




typedef union ii_iscr1_u {
	bdrkreg_t	ii_iscr1_regval;
	struct  {
		bdrkreg_t	i_scratch                 :	64;
	} ii_iscr1_fld_s;
} ii_iscr1_u_t;




/************************************************************************
 *                                                                      *
 * Description:  There are seven instances of translation table entry   *
 * registers. Each register maps a Bedrock Big Window to a 48-bit       *
 * address on Crosstalk.                                                *
 * For M-mode (128 nodes, 8 GBytes/node), SysAD[31:29] (Big Window      *
 * number) are used to select one of these 7 registers. The Widget      *
 * number field is then derived from the W_NUM field for synthesizing   *
 * a Crosstalk packet. The 5 bits of OFFSET are concatenated with       *
 * SysAD[28:0] to form Crosstalk[33:0]. The upper Crosstalk[47:34]      *
 * are padded with zeros. Although the maximum Crosstalk space          *
 * addressable by the Bedrock is thus the lower 16 GBytes per widget    *
 * (M-mode), however only <SUP >7</SUP>/<SUB >32nds</SUB> of this       *
 * space can be accessed.                                               *
 * For the N-mode (256 nodes, 4 GBytes/node), SysAD[30:28] (Big         *
 * Window number) are used to select one of these 7 registers. The      *
 * Widget number field is then derived from the W_NUM field for         *
 * synthesizing a Crosstalk packet. The 5 bits of OFFSET are            *
 * concatenated with SysAD[27:0] to form Crosstalk[33:0]. The IOSP      *
 * field is used as Crosstalk[47], and remainder of the Crosstalk       *
 * address bits (Crosstalk[46:34]) are always zero. While the maximum   *
 * Crosstalk space addressable by the Bedrock is thus the lower         *
 * 8-GBytes per widget (N-mode), only <SUP >7</SUP>/<SUB >32nds</SUB>   *
 * of this space can be accessed.                                       *
 *                                                                      *
 ************************************************************************/




#ifdef LITTLE_ENDIAN

typedef union ii_itte1_u {
	bdrkreg_t	ii_itte1_regval;
	struct  {
		bdrkreg_t	i_offset                  :	 5;
		bdrkreg_t	i_rsvd_1		  :	 3;
		bdrkreg_t	i_w_num			  :	 4;
		bdrkreg_t	i_iosp			  :	 1;
		bdrkreg_t	i_rsvd			  :	51;
	} ii_itte1_fld_s;
} ii_itte1_u_t;

#else

typedef union ii_itte1_u {
	bdrkreg_t	ii_itte1_regval;
	struct	{
		bdrkreg_t	i_rsvd			  :	51;
		bdrkreg_t	i_iosp			  :	 1;
		bdrkreg_t	i_w_num			  :	 4;
		bdrkreg_t	i_rsvd_1		  :	 3;
		bdrkreg_t	i_offset		  :	 5;
	} ii_itte1_fld_s;
} ii_itte1_u_t;

#endif




/************************************************************************
 *                                                                      *
 * Description:  There are seven instances of translation table entry   *
 * registers. Each register maps a Bedrock Big Window to a 48-bit       *
 * address on Crosstalk.                                                *
 * For M-mode (128 nodes, 8 GBytes/node), SysAD[31:29] (Big Window      *
 * number) are used to select one of these 7 registers. The Widget      *
 * number field is then derived from the W_NUM field for synthesizing   *
 * a Crosstalk packet. The 5 bits of OFFSET are concatenated with       *
 * SysAD[28:0] to form Crosstalk[33:0]. The upper Crosstalk[47:34]      *
 * are padded with zeros. Although the maximum Crosstalk space          *
 * addressable by the Bedrock is thus the lower 16 GBytes per widget    *
 * (M-mode), however only <SUP >7</SUP>/<SUB >32nds</SUB> of this       *
 * space can be accessed.                                               *
 * For the N-mode (256 nodes, 4 GBytes/node), SysAD[30:28] (Big         *
 * Window number) are used to select one of these 7 registers. The      *
 * Widget number field is then derived from the W_NUM field for         *
 * synthesizing a Crosstalk packet. The 5 bits of OFFSET are            *
 * concatenated with SysAD[27:0] to form Crosstalk[33:0]. The IOSP      *
 * field is used as Crosstalk[47], and remainder of the Crosstalk       *
 * address bits (Crosstalk[46:34]) are always zero. While the maximum   *
 * Crosstalk space addressable by the Bedrock is thus the lower         *
 * 8-GBytes per widget (N-mode), only <SUP >7</SUP>/<SUB >32nds</SUB>   *
 * of this space can be accessed.                                       *
 *                                                                      *
 ************************************************************************/




#ifdef LITTLE_ENDIAN

typedef union ii_itte2_u {
	bdrkreg_t	ii_itte2_regval;
	struct	{
		bdrkreg_t	i_offset		  :	 5;
		bdrkreg_t	i_rsvd_1		  :	 3;
		bdrkreg_t	i_w_num			  :	 4;
		bdrkreg_t	i_iosp			  :	 1;
		bdrkreg_t       i_rsvd                    :     51;
	} ii_itte2_fld_s;
} ii_itte2_u_t;

#else
typedef union ii_itte2_u {
	bdrkreg_t	ii_itte2_regval;
	struct  {
		bdrkreg_t	i_rsvd                    :	51;
		bdrkreg_t	i_iosp                    :	 1;
		bdrkreg_t	i_w_num                   :	 4;
		bdrkreg_t	i_rsvd_1                  :	 3;
		bdrkreg_t	i_offset                  :	 5;
	} ii_itte2_fld_s;
} ii_itte2_u_t;

#endif




/************************************************************************
 *                                                                      *
 * Description:  There are seven instances of translation table entry   *
 * registers. Each register maps a Bedrock Big Window to a 48-bit       *
 * address on Crosstalk.                                                *
 * For M-mode (128 nodes, 8 GBytes/node), SysAD[31:29] (Big Window      *
 * number) are used to select one of these 7 registers. The Widget      *
 * number field is then derived from the W_NUM field for synthesizing   *
 * a Crosstalk packet. The 5 bits of OFFSET are concatenated with       *
 * SysAD[28:0] to form Crosstalk[33:0]. The upper Crosstalk[47:34]      *
 * are padded with zeros. Although the maximum Crosstalk space          *
 * addressable by the Bedrock is thus the lower 16 GBytes per widget    *
 * (M-mode), however only <SUP >7</SUP>/<SUB >32nds</SUB> of this       *
 * space can be accessed.                                               *
 * For the N-mode (256 nodes, 4 GBytes/node), SysAD[30:28] (Big         *
 * Window number) are used to select one of these 7 registers. The      *
 * Widget number field is then derived from the W_NUM field for         *
 * synthesizing a Crosstalk packet. The 5 bits of OFFSET are            *
 * concatenated with SysAD[27:0] to form Crosstalk[33:0]. The IOSP      *
 * field is used as Crosstalk[47], and remainder of the Crosstalk       *
 * address bits (Crosstalk[46:34]) are always zero. While the maximum   *
 * Crosstalk space addressable by the Bedrock is thus the lower         *
 * 8-GBytes per widget (N-mode), only <SUP >7</SUP>/<SUB >32nds</SUB>   *
 * of this space can be accessed.                                       *
 *                                                                      *
 ************************************************************************/




#ifdef LITTLE_ENDIAN

typedef union ii_itte3_u {
	bdrkreg_t	ii_itte3_regval;
	struct  {
		bdrkreg_t	i_offset                  :	 5;
		bdrkreg_t       i_rsvd_1                  :      3;
		bdrkreg_t       i_w_num                   :      4;
		bdrkreg_t       i_iosp                    :      1;
		bdrkreg_t       i_rsvd                    :     51;
	} ii_itte3_fld_s;
} ii_itte3_u_t;

#else

typedef union ii_itte3_u {
	bdrkreg_t	ii_itte3_regval;
	struct	{
		bdrkreg_t	i_rsvd			  :	51;
		bdrkreg_t	i_iosp			  :	 1;
		bdrkreg_t	i_w_num			  :	 4;
		bdrkreg_t	i_rsvd_1		  :	 3;
		bdrkreg_t	i_offset		  :	 5;
	} ii_itte3_fld_s;
} ii_itte3_u_t;

#endif




/************************************************************************
 *                                                                      *
 * Description:  There are seven instances of translation table entry   *
 * registers. Each register maps a Bedrock Big Window to a 48-bit       *
 * address on Crosstalk.                                                *
 * For M-mode (128 nodes, 8 GBytes/node), SysAD[31:29] (Big Window      *
 * number) are used to select one of these 7 registers. The Widget      *
 * number field is then derived from the W_NUM field for synthesizing   *
 * a Crosstalk packet. The 5 bits of OFFSET are concatenated with       *
 * SysAD[28:0] to form Crosstalk[33:0]. The upper Crosstalk[47:34]      *
 * are padded with zeros. Although the maximum Crosstalk space          *
 * addressable by the Bedrock is thus the lower 16 GBytes per widget    *
 * (M-mode), however only <SUP >7</SUP>/<SUB >32nds</SUB> of this       *
 * space can be accessed.                                               *
 * For the N-mode (256 nodes, 4 GBytes/node), SysAD[30:28] (Big         *
 * Window number) are used to select one of these 7 registers. The      *
 * Widget number field is then derived from the W_NUM field for         *
 * synthesizing a Crosstalk packet. The 5 bits of OFFSET are            *
 * concatenated with SysAD[27:0] to form Crosstalk[33:0]. The IOSP      *
 * field is used as Crosstalk[47], and remainder of the Crosstalk       *
 * address bits (Crosstalk[46:34]) are always zero. While the maximum   *
 * Crosstalk space addressable by the Bedrock is thus the lower         *
 * 8-GBytes per widget (N-mode), only <SUP >7</SUP>/<SUB >32nds</SUB>   *
 * of this space can be accessed.                                       *
 *                                                                      *
 ************************************************************************/




#ifdef LITTLE_ENDIAN

typedef union ii_itte4_u {
	bdrkreg_t	ii_itte4_regval;
	struct  {
		bdrkreg_t	i_offset                  :	 5;
		bdrkreg_t	i_rsvd_1		  :	 3;
		bdrkreg_t       i_w_num                   :      4;
		bdrkreg_t       i_iosp                    :      1;
		bdrkreg_t       i_rsvd                    :     51;
	} ii_itte4_fld_s;
} ii_itte4_u_t;

#else

typedef union ii_itte4_u {
	bdrkreg_t	ii_itte4_regval;
	struct	{
		bdrkreg_t	i_rsvd			  :	51;
		bdrkreg_t	i_iosp			  :	 1;
		bdrkreg_t	i_w_num			  :	 4;
		bdrkreg_t	i_rsvd_1		  :	 3;
		bdrkreg_t	i_offset		  :	 5;
	} ii_itte4_fld_s;
} ii_itte4_u_t;

#endif




/************************************************************************
 *                                                                      *
 * Description:  There are seven instances of translation table entry   *
 * registers. Each register maps a Bedrock Big Window to a 48-bit       *
 * address on Crosstalk.                                                *
 * For M-mode (128 nodes, 8 GBytes/node), SysAD[31:29] (Big Window      *
 * number) are used to select one of these 7 registers. The Widget      *
 * number field is then derived from the W_NUM field for synthesizing   *
 * a Crosstalk packet. The 5 bits of OFFSET are concatenated with       *
 * SysAD[28:0] to form Crosstalk[33:0]. The upper Crosstalk[47:34]      *
 * are padded with zeros. Although the maximum Crosstalk space          *
 * addressable by the Bedrock is thus the lower 16 GBytes per widget    *
 * (M-mode), however only <SUP >7</SUP>/<SUB >32nds</SUB> of this       *
 * space can be accessed.                                               *
 * For the N-mode (256 nodes, 4 GBytes/node), SysAD[30:28] (Big         *
 * Window number) are used to select one of these 7 registers. The      *
 * Widget number field is then derived from the W_NUM field for         *
 * synthesizing a Crosstalk packet. The 5 bits of OFFSET are            *
 * concatenated with SysAD[27:0] to form Crosstalk[33:0]. The IOSP      *
 * field is used as Crosstalk[47], and remainder of the Crosstalk       *
 * address bits (Crosstalk[46:34]) are always zero. While the maximum   *
 * Crosstalk space addressable by the Bedrock is thus the lower         *
 * 8-GBytes per widget (N-mode), only <SUP >7</SUP>/<SUB >32nds</SUB>   *
 * of this space can be accessed.                                       *
 *                                                                      *
 ************************************************************************/




#ifdef LITTLE_ENDIAN

typedef union ii_itte5_u {
	bdrkreg_t	ii_itte5_regval;
	struct  {
		bdrkreg_t	i_offset                  :	 5;
		bdrkreg_t       i_rsvd_1                  :      3;
		bdrkreg_t       i_w_num                   :      4;
		bdrkreg_t       i_iosp                    :      1;
		bdrkreg_t       i_rsvd                    :     51;
	} ii_itte5_fld_s;
} ii_itte5_u_t;

#else

typedef union ii_itte5_u {
	bdrkreg_t	ii_itte5_regval;
	struct	{
		bdrkreg_t	i_rsvd			  :	51;
		bdrkreg_t	i_iosp			  :	 1;
		bdrkreg_t	i_w_num			  :	 4;
		bdrkreg_t	i_rsvd_1		  :	 3;
		bdrkreg_t	i_offset		  :	 5;
	} ii_itte5_fld_s;
} ii_itte5_u_t;

#endif




/************************************************************************
 *                                                                      *
 * Description:  There are seven instances of translation table entry   *
 * registers. Each register maps a Bedrock Big Window to a 48-bit       *
 * address on Crosstalk.                                                *
 * For M-mode (128 nodes, 8 GBytes/node), SysAD[31:29] (Big Window      *
 * number) are used to select one of these 7 registers. The Widget      *
 * number field is then derived from the W_NUM field for synthesizing   *
 * a Crosstalk packet. The 5 bits of OFFSET are concatenated with       *
 * SysAD[28:0] to form Crosstalk[33:0]. The upper Crosstalk[47:34]      *
 * are padded with zeros. Although the maximum Crosstalk space          *
 * addressable by the Bedrock is thus the lower 16 GBytes per widget    *
 * (M-mode), however only <SUP >7</SUP>/<SUB >32nds</SUB> of this       *
 * space can be accessed.                                               *
 * For the N-mode (256 nodes, 4 GBytes/node), SysAD[30:28] (Big         *
 * Window number) are used to select one of these 7 registers. The      *
 * Widget number field is then derived from the W_NUM field for         *
 * synthesizing a Crosstalk packet. The 5 bits of OFFSET are            *
 * concatenated with SysAD[27:0] to form Crosstalk[33:0]. The IOSP      *
 * field is used as Crosstalk[47], and remainder of the Crosstalk       *
 * address bits (Crosstalk[46:34]) are always zero. While the maximum   *
 * Crosstalk space addressable by the Bedrock is thus the lower         *
 * 8-GBytes per widget (N-mode), only <SUP >7</SUP>/<SUB >32nds</SUB>   *
 * of this space can be accessed.                                       *
 *                                                                      *
 ************************************************************************/




#ifdef LITTLE_ENDIAN

typedef union ii_itte6_u {
	bdrkreg_t	ii_itte6_regval;
	struct  {
		bdrkreg_t	i_offset                  :	 5;
		bdrkreg_t       i_rsvd_1                  :      3;
		bdrkreg_t       i_w_num                   :      4;
		bdrkreg_t       i_iosp                    :      1;
		bdrkreg_t       i_rsvd                    :     51;
	} ii_itte6_fld_s;
} ii_itte6_u_t;

#else

typedef union ii_itte6_u {
	bdrkreg_t	ii_itte6_regval;
	struct	{
		bdrkreg_t	i_rsvd			  :	51;
		bdrkreg_t	i_iosp			  :	 1;
		bdrkreg_t	i_w_num			  :	 4;
		bdrkreg_t	i_rsvd_1		  :	 3;
		bdrkreg_t	i_offset		  :	 5;
	} ii_itte6_fld_s;
} ii_itte6_u_t;

#endif




/************************************************************************
 *                                                                      *
 * Description:  There are seven instances of translation table entry   *
 * registers. Each register maps a Bedrock Big Window to a 48-bit       *
 * address on Crosstalk.                                                *
 * For M-mode (128 nodes, 8 GBytes/node), SysAD[31:29] (Big Window      *
 * number) are used to select one of these 7 registers. The Widget      *
 * number field is then derived from the W_NUM field for synthesizing   *
 * a Crosstalk packet. The 5 bits of OFFSET are concatenated with       *
 * SysAD[28:0] to form Crosstalk[33:0]. The upper Crosstalk[47:34]      *
 * are padded with zeros. Although the maximum Crosstalk space          *
 * addressable by the Bedrock is thus the lower 16 GBytes per widget    *
 * (M-mode), however only <SUP >7</SUP>/<SUB >32nds</SUB> of this       *
 * space can be accessed.                                               *
 * For the N-mode (256 nodes, 4 GBytes/node), SysAD[30:28] (Big         *
 * Window number) are used to select one of these 7 registers. The      *
 * Widget number field is then derived from the W_NUM field for         *
 * synthesizing a Crosstalk packet. The 5 bits of OFFSET are            *
 * concatenated with SysAD[27:0] to form Crosstalk[33:0]. The IOSP      *
 * field is used as Crosstalk[47], and remainder of the Crosstalk       *
 * address bits (Crosstalk[46:34]) are always zero. While the maximum   *
 * Crosstalk space addressable by the Bedrock is thus the lower         *
 * 8-GBytes per widget (N-mode), only <SUP >7</SUP>/<SUB >32nds</SUB>   *
 * of this space can be accessed.                                       *
 *                                                                      *
 ************************************************************************/




#ifdef LITTLE_ENDIAN

typedef union ii_itte7_u {
	bdrkreg_t	ii_itte7_regval;
	struct  {
		bdrkreg_t	i_offset                  :	 5;
		bdrkreg_t	i_rsvd_1		  :	 3;
		bdrkreg_t       i_w_num                   :      4;
		bdrkreg_t       i_iosp                    :      1;
		bdrkreg_t       i_rsvd                    :     51;
	} ii_itte7_fld_s;
} ii_itte7_u_t;

#else

typedef union ii_itte7_u {
	bdrkreg_t	ii_itte7_regval;
	struct	{
		bdrkreg_t	i_rsvd			  :	51;
		bdrkreg_t	i_iosp			  :	 1;
		bdrkreg_t	i_w_num			  :	 4;
		bdrkreg_t	i_rsvd_1		  :	 3;
		bdrkreg_t	i_offset		  :	 5;
	} ii_itte7_fld_s;
} ii_itte7_u_t;

#endif




/************************************************************************
 *                                                                      *
 * Description:  There are 9 instances of this register, one per        *
 * actual widget in this implementation of Bedrock and Crossbow.        *
 * Note: Crossbow only has ports for Widgets 8 through F, widget 0      *
 * refers to Crossbow's internal space.                                 *
 * This register contains the state elements per widget that are        *
 * necessary to manage the PIO flow control on Crosstalk and on the     *
 * Router Network. See the PIO Flow Control chapter for a complete      *
 * description of this register                                         *
 * The SPUR_WR bit requires some explanation. When this register is     *
 * written, the new value of the C field is captured in an internal     *
 * register so the hardware can remember what the programmer wrote      *
 * into the credit counter. The SPUR_WR bit sets whenever the C field   *
 * increments above this stored value, which indicates that there       *
 * have been more responses received than requests sent. The SPUR_WR    *
 * bit cannot be cleared until a value is written to the IPRBx          *
 * register; the write will correct the C field and capture its new     *
 * value in the internal register. Even if IECLR[E_PRB_x] is set, the   *
 * SPUR_WR bit will persist if IPRBx hasn't yet been written.           *
 * .                                                                    *
 *                                                                      *
 ************************************************************************/




#ifdef LITTLE_ENDIAN

typedef union ii_iprb0_u {
	bdrkreg_t	ii_iprb0_regval;
	struct  {
		bdrkreg_t	i_c                       :	 8;
		bdrkreg_t	i_na			  :	14;
		bdrkreg_t       i_rsvd_2                  :      2;
		bdrkreg_t	i_nb			  :	14;
		bdrkreg_t	i_rsvd_1		  :	 2;
		bdrkreg_t	i_m			  :	 2;
		bdrkreg_t	i_f			  :	 1;
		bdrkreg_t	i_of_cnt		  :	 5;
		bdrkreg_t	i_error			  :	 1;
		bdrkreg_t	i_rd_to			  :	 1;
		bdrkreg_t	i_spur_wr		  :	 1;
		bdrkreg_t	i_spur_rd		  :	 1;
		bdrkreg_t	i_rsvd			  :	11;
		bdrkreg_t	i_mult_err		  :	 1;
	} ii_iprb0_fld_s;
} ii_iprb0_u_t;

#else

typedef union ii_iprb0_u {
	bdrkreg_t	ii_iprb0_regval;
	struct	{
		bdrkreg_t	i_mult_err		  :	 1;
		bdrkreg_t	i_rsvd			  :	11;
		bdrkreg_t	i_spur_rd		  :	 1;
		bdrkreg_t	i_spur_wr		  :	 1;
		bdrkreg_t	i_rd_to			  :	 1;
		bdrkreg_t	i_error			  :	 1;
		bdrkreg_t	i_of_cnt		  :	 5;
		bdrkreg_t	i_f			  :	 1;
		bdrkreg_t	i_m			  :	 2;
		bdrkreg_t	i_rsvd_1		  :	 2;
		bdrkreg_t	i_nb			  :	14;
		bdrkreg_t	i_rsvd_2		  :	 2;
		bdrkreg_t	i_na			  :	14;
		bdrkreg_t	i_c			  :	 8;
	} ii_iprb0_fld_s;
} ii_iprb0_u_t;

#endif




/************************************************************************
 *                                                                      *
 * Description:  There are 9 instances of this register, one per        *
 * actual widget in this implementation of Bedrock and Crossbow.        *
 * Note: Crossbow only has ports for Widgets 8 through F, widget 0      *
 * refers to Crossbow's internal space.                                 *
 * This register contains the state elements per widget that are        *
 * necessary to manage the PIO flow control on Crosstalk and on the     *
 * Router Network. See the PIO Flow Control chapter for a complete      *
 * description of this register                                         *
 * The SPUR_WR bit requires some explanation. When this register is     *
 * written, the new value of the C field is captured in an internal     *
 * register so the hardware can remember what the programmer wrote      *
 * into the credit counter. The SPUR_WR bit sets whenever the C field   *
 * increments above this stored value, which indicates that there       *
 * have been more responses received than requests sent. The SPUR_WR    *
 * bit cannot be cleared until a value is written to the IPRBx          *
 * register; the write will correct the C field and capture its new     *
 * value in the internal register. Even if IECLR[E_PRB_x] is set, the   *
 * SPUR_WR bit will persist if IPRBx hasn't yet been written.           *
 * .                                                                    *
 *                                                                      *
 ************************************************************************/




#ifdef LITTLE_ENDIAN

typedef union ii_iprb8_u {
	bdrkreg_t	ii_iprb8_regval;
	struct  {
		bdrkreg_t	i_c                       :	 8;
		bdrkreg_t	i_na			  :	14;
		bdrkreg_t       i_rsvd_2                  :      2;
		bdrkreg_t	i_nb			  :	14;
		bdrkreg_t       i_rsvd_1                  :      2;
		bdrkreg_t       i_m                       :      2;
		bdrkreg_t       i_f                       :      1;
		bdrkreg_t       i_of_cnt                  :      5;
		bdrkreg_t       i_error                   :      1;
		bdrkreg_t       i_rd_to                   :      1;
		bdrkreg_t       i_spur_wr                 :      1;
		bdrkreg_t	i_spur_rd		  :	 1;
		bdrkreg_t       i_rsvd                    :     11;
		bdrkreg_t	i_mult_err		  :	 1;
	} ii_iprb8_fld_s;
} ii_iprb8_u_t;

#else


typedef union ii_iprb8_u {
	bdrkreg_t	ii_iprb8_regval;
	struct	{
		bdrkreg_t	i_mult_err		  :	 1;
		bdrkreg_t	i_rsvd			  :	11;
		bdrkreg_t	i_spur_rd		  :	 1;
		bdrkreg_t	i_spur_wr		  :	 1;
		bdrkreg_t	i_rd_to			  :	 1;
		bdrkreg_t	i_error			  :	 1;
		bdrkreg_t	i_of_cnt		  :	 5;
		bdrkreg_t	i_f			  :	 1;
		bdrkreg_t	i_m			  :	 2;
		bdrkreg_t	i_rsvd_1		  :	 2;
		bdrkreg_t	i_nb			  :	14;
		bdrkreg_t	i_rsvd_2		  :	 2;
		bdrkreg_t	i_na			  :	14;
		bdrkreg_t	i_c			  :	 8;
	} ii_iprb8_fld_s;
} ii_iprb8_u_t;

#endif




/************************************************************************
 *                                                                      *
 * Description:  There are 9 instances of this register, one per        *
 * actual widget in this implementation of Bedrock and Crossbow.        *
 * Note: Crossbow only has ports for Widgets 8 through F, widget 0      *
 * refers to Crossbow's internal space.                                 *
 * This register contains the state elements per widget that are        *
 * necessary to manage the PIO flow control on Crosstalk and on the     *
 * Router Network. See the PIO Flow Control chapter for a complete      *
 * description of this register                                         *
 * The SPUR_WR bit requires some explanation. When this register is     *
 * written, the new value of the C field is captured in an internal     *
 * register so the hardware can remember what the programmer wrote      *
 * into the credit counter. The SPUR_WR bit sets whenever the C field   *
 * increments above this stored value, which indicates that there       *
 * have been more responses received than requests sent. The SPUR_WR    *
 * bit cannot be cleared until a value is written to the IPRBx          *
 * register; the write will correct the C field and capture its new     *
 * value in the internal register. Even if IECLR[E_PRB_x] is set, the   *
 * SPUR_WR bit will persist if IPRBx hasn't yet been written.           *
 * .                                                                    *
 *                                                                      *
 ************************************************************************/




#ifdef LITTLE_ENDIAN

typedef union ii_iprb9_u {
	bdrkreg_t	ii_iprb9_regval;
	struct	{
		bdrkreg_t	i_c			  :	 8;
		bdrkreg_t	i_na			  :	14;
		bdrkreg_t	i_rsvd_2		  :	 2;
		bdrkreg_t	i_nb			  :	14;
		bdrkreg_t	i_rsvd_1		  :	 2;
		bdrkreg_t	i_m			  :	 2;
		bdrkreg_t	i_f			  :	 1;
		bdrkreg_t	i_of_cnt		  :	 5;
		bdrkreg_t	i_error			  :	 1;
		bdrkreg_t	i_rd_to			  :	 1;
		bdrkreg_t	i_spur_wr		  :	 1;
		bdrkreg_t	i_spur_rd		  :	 1;
		bdrkreg_t	i_rsvd			  :	11;
		bdrkreg_t	i_mult_err		  :	 1;
	} ii_iprb9_fld_s;
} ii_iprb9_u_t;

#else

typedef union ii_iprb9_u {
	bdrkreg_t	ii_iprb9_regval;
	struct  {
		bdrkreg_t	i_mult_err                :	 1;
		bdrkreg_t	i_rsvd                    :	11;
		bdrkreg_t	i_spur_rd                 :	 1;
		bdrkreg_t	i_spur_wr                 :	 1;
		bdrkreg_t	i_rd_to                   :	 1;
		bdrkreg_t	i_error                   :	 1;
		bdrkreg_t	i_of_cnt                  :	 5;
		bdrkreg_t	i_f                       :	 1;
		bdrkreg_t	i_m                       :	 2;
		bdrkreg_t	i_rsvd_1                  :	 2;
		bdrkreg_t	i_nb                      :	14;
		bdrkreg_t	i_rsvd_2                  :	 2;
		bdrkreg_t	i_na                      :	14;
		bdrkreg_t	i_c                       :	 8;
	} ii_iprb9_fld_s;
} ii_iprb9_u_t;

#endif



/************************************************************************
 *                                                                      *
 * Description:  There are 9 instances of this register, one per        *
 * actual widget in this implementation of Bedrock and Crossbow.        *
 * Note: Crossbow only has ports for Widgets 8 through F, widget 0      *
 * refers to Crossbow's internal space.                                 *
 * This register contains the state elements per widget that are        *
 * necessary to manage the PIO flow control on Crosstalk and on the     *
 * Router Network. See the PIO Flow Control chapter for a complete      *
 * description of this register                                         *
 * The SPUR_WR bit requires some explanation. When this register is     *
 * written, the new value of the C field is captured in an internal     *
 * register so the hardware can remember what the programmer wrote      *
 * into the credit counter. The SPUR_WR bit sets whenever the C field   *
 * increments above this stored value, which indicates that there       *
 * have been more responses received than requests sent. The SPUR_WR    *
 * bit cannot be cleared until a value is written to the IPRBx          *
 * register; the write will correct the C field and capture its new     *
 * value in the internal register. Even if IECLR[E_PRB_x] is set, the   *
 * SPUR_WR bit will persist if IPRBx hasn't yet been written.           *
 * .                                                                    *
 *                                                                      *
 ************************************************************************/




#ifdef LITTLE_ENDIAN

typedef union ii_iprba_u {
	bdrkreg_t	ii_iprba_regval;
	struct  {
		bdrkreg_t	i_c                       :	 8;
		bdrkreg_t	i_na			  :	14;
		bdrkreg_t       i_rsvd_2                  :      2;
		bdrkreg_t	i_nb			  :	14;
		bdrkreg_t	i_rsvd_1		  :	 2;
		bdrkreg_t	i_m			  :	 2;
		bdrkreg_t	i_f			  :	 1;
		bdrkreg_t	i_of_cnt		  :	 5;
		bdrkreg_t	i_error			  :	 1;
		bdrkreg_t	i_rd_to			  :	 1;
		bdrkreg_t	i_spur_wr		  :	 1;
		bdrkreg_t	i_spur_rd		  :	 1;
		bdrkreg_t	i_rsvd			  :	11;
		bdrkreg_t	i_mult_err		  :	 1;
	} ii_iprba_fld_s;
} ii_iprba_u_t;

#else

typedef union ii_iprba_u {
	bdrkreg_t	ii_iprba_regval;
	struct	{
		bdrkreg_t	i_mult_err		  :	 1;
		bdrkreg_t	i_rsvd			  :	11;
		bdrkreg_t	i_spur_rd		  :	 1;
		bdrkreg_t	i_spur_wr		  :	 1;
		bdrkreg_t	i_rd_to			  :	 1;
		bdrkreg_t	i_error			  :	 1;
		bdrkreg_t	i_of_cnt		  :	 5;
		bdrkreg_t	i_f			  :	 1;
		bdrkreg_t	i_m			  :	 2;
		bdrkreg_t	i_rsvd_1		  :	 2;
		bdrkreg_t	i_nb			  :	14;
		bdrkreg_t	i_rsvd_2		  :	 2;
		bdrkreg_t	i_na			  :	14;
		bdrkreg_t	i_c			  :	 8;
	} ii_iprba_fld_s;
} ii_iprba_u_t;

#endif




/************************************************************************
 *                                                                      *
 * Description:  There are 9 instances of this register, one per        *
 * actual widget in this implementation of Bedrock and Crossbow.        *
 * Note: Crossbow only has ports for Widgets 8 through F, widget 0      *
 * refers to Crossbow's internal space.                                 *
 * This register contains the state elements per widget that are        *
 * necessary to manage the PIO flow control on Crosstalk and on the     *
 * Router Network. See the PIO Flow Control chapter for a complete      *
 * description of this register                                         *
 * The SPUR_WR bit requires some explanation. When this register is     *
 * written, the new value of the C field is captured in an internal     *
 * register so the hardware can remember what the programmer wrote      *
 * into the credit counter. The SPUR_WR bit sets whenever the C field   *
 * increments above this stored value, which indicates that there       *
 * have been more responses received than requests sent. The SPUR_WR    *
 * bit cannot be cleared until a value is written to the IPRBx          *
 * register; the write will correct the C field and capture its new     *
 * value in the internal register. Even if IECLR[E_PRB_x] is set, the   *
 * SPUR_WR bit will persist if IPRBx hasn't yet been written.           *
 * .                                                                    *
 *                                                                      *
 ************************************************************************/




#ifdef LITTLE_ENDIAN

typedef union ii_iprbb_u {
	bdrkreg_t	ii_iprbb_regval;
	struct	{
		bdrkreg_t	i_c			  :	 8;
		bdrkreg_t	i_na			  :	14;
		bdrkreg_t	i_rsvd_2		  :	 2;
		bdrkreg_t	i_nb			  :	14;
		bdrkreg_t	i_rsvd_1		  :	 2;
		bdrkreg_t	i_m			  :	 2;
		bdrkreg_t	i_f			  :	 1;
		bdrkreg_t	i_of_cnt		  :	 5;
		bdrkreg_t	i_error			  :	 1;
		bdrkreg_t	i_rd_to			  :	 1;
		bdrkreg_t	i_spur_wr		  :	 1;
		bdrkreg_t	i_spur_rd		  :	 1;
		bdrkreg_t	i_rsvd			  :	11;
		bdrkreg_t	i_mult_err		  :	 1;
	} ii_iprbb_fld_s;
} ii_iprbb_u_t;

#else

typedef union ii_iprbb_u {
	bdrkreg_t	ii_iprbb_regval;
	struct  {
		bdrkreg_t	i_mult_err                :	 1;
		bdrkreg_t	i_rsvd                    :	11;
		bdrkreg_t	i_spur_rd                 :	 1;
		bdrkreg_t	i_spur_wr                 :	 1;
		bdrkreg_t	i_rd_to                   :	 1;
		bdrkreg_t	i_error                   :	 1;
		bdrkreg_t	i_of_cnt                  :	 5;
		bdrkreg_t	i_f                       :	 1;
		bdrkreg_t	i_m                       :	 2;
		bdrkreg_t	i_rsvd_1                  :	 2;
		bdrkreg_t	i_nb                      :	14;
		bdrkreg_t	i_rsvd_2                  :	 2;
		bdrkreg_t	i_na                      :	14;
		bdrkreg_t	i_c                       :	 8;
	} ii_iprbb_fld_s;
} ii_iprbb_u_t;

#endif




/************************************************************************
 *                                                                      *
 * Description:  There are 9 instances of this register, one per        *
 * actual widget in this implementation of Bedrock and Crossbow.        *
 * Note: Crossbow only has ports for Widgets 8 through F, widget 0      *
 * refers to Crossbow's internal space.                                 *
 * This register contains the state elements per widget that are        *
 * necessary to manage the PIO flow control on Crosstalk and on the     *
 * Router Network. See the PIO Flow Control chapter for a complete      *
 * description of this register                                         *
 * The SPUR_WR bit requires some explanation. When this register is     *
 * written, the new value of the C field is captured in an internal     *
 * register so the hardware can remember what the programmer wrote      *
 * into the credit counter. The SPUR_WR bit sets whenever the C field   *
 * increments above this stored value, which indicates that there       *
 * have been more responses received than requests sent. The SPUR_WR    *
 * bit cannot be cleared until a value is written to the IPRBx          *
 * register; the write will correct the C field and capture its new     *
 * value in the internal register. Even if IECLR[E_PRB_x] is set, the   *
 * SPUR_WR bit will persist if IPRBx hasn't yet been written.           *
 * .                                                                    *
 *                                                                      *
 ************************************************************************/




#ifdef LITTLE_ENDIAN

typedef union ii_iprbc_u {
	bdrkreg_t	ii_iprbc_regval;
	struct	{
		bdrkreg_t	i_c			  :	 8;
		bdrkreg_t	i_na			  :	14;
		bdrkreg_t	i_rsvd_2		  :	 2;
		bdrkreg_t	i_nb			  :	14;
		bdrkreg_t	i_rsvd_1		  :	 2;
		bdrkreg_t	i_m			  :	 2;
		bdrkreg_t	i_f			  :	 1;
		bdrkreg_t	i_of_cnt		  :	 5;
		bdrkreg_t	i_error			  :	 1;
		bdrkreg_t	i_rd_to			  :	 1;
		bdrkreg_t	i_spur_wr		  :	 1;
		bdrkreg_t	i_spur_rd		  :	 1;
		bdrkreg_t	i_rsvd			  :	11;
		bdrkreg_t	i_mult_err		  :	 1;
	} ii_iprbc_fld_s;
} ii_iprbc_u_t;

#else

typedef union ii_iprbc_u {
	bdrkreg_t	ii_iprbc_regval;
	struct  {
		bdrkreg_t	i_mult_err                :	 1;
		bdrkreg_t	i_rsvd                    :	11;
		bdrkreg_t	i_spur_rd                 :	 1;
		bdrkreg_t	i_spur_wr                 :	 1;
		bdrkreg_t	i_rd_to                   :	 1;
		bdrkreg_t	i_error                   :	 1;
		bdrkreg_t	i_of_cnt                  :	 5;
		bdrkreg_t	i_f                       :	 1;
		bdrkreg_t	i_m                       :	 2;
		bdrkreg_t	i_rsvd_1                  :	 2;
		bdrkreg_t	i_nb                      :	14;
		bdrkreg_t	i_rsvd_2                  :	 2;
		bdrkreg_t	i_na                      :	14;
		bdrkreg_t	i_c                       :	 8;
	} ii_iprbc_fld_s;
} ii_iprbc_u_t;

#endif




/************************************************************************
 *                                                                      *
 * Description:  There are 9 instances of this register, one per        *
 * actual widget in this implementation of Bedrock and Crossbow.        *
 * Note: Crossbow only has ports for Widgets 8 through F, widget 0      *
 * refers to Crossbow's internal space.                                 *
 * This register contains the state elements per widget that are        *
 * necessary to manage the PIO flow control on Crosstalk and on the     *
 * Router Network. See the PIO Flow Control chapter for a complete      *
 * description of this register                                         *
 * The SPUR_WR bit requires some explanation. When this register is     *
 * written, the new value of the C field is captured in an internal     *
 * register so the hardware can remember what the programmer wrote      *
 * into the credit counter. The SPUR_WR bit sets whenever the C field   *
 * increments above this stored value, which indicates that there       *
 * have been more responses received than requests sent. The SPUR_WR    *
 * bit cannot be cleared until a value is written to the IPRBx          *
 * register; the write will correct the C field and capture its new     *
 * value in the internal register. Even if IECLR[E_PRB_x] is set, the   *
 * SPUR_WR bit will persist if IPRBx hasn't yet been written.           *
 * .                                                                    *
 *                                                                      *
 ************************************************************************/




#ifdef LITTLE_ENDIAN

typedef union ii_iprbd_u {
	bdrkreg_t	ii_iprbd_regval;
	struct	{
		bdrkreg_t	i_c			  :	 8;
		bdrkreg_t	i_na			  :	14;
		bdrkreg_t	i_rsvd_2		  :	 2;
		bdrkreg_t	i_nb			  :	14;
		bdrkreg_t	i_rsvd_1		  :	 2;
		bdrkreg_t	i_m			  :	 2;
		bdrkreg_t	i_f			  :	 1;
		bdrkreg_t	i_of_cnt		  :	 5;
		bdrkreg_t	i_error			  :	 1;
		bdrkreg_t	i_rd_to			  :	 1;
		bdrkreg_t	i_spur_wr		  :	 1;
		bdrkreg_t	i_spur_rd		  :	 1;
		bdrkreg_t	i_rsvd			  :	11;
		bdrkreg_t	i_mult_err		  :	 1;
	} ii_iprbd_fld_s;
} ii_iprbd_u_t;

#else

typedef union ii_iprbd_u {
	bdrkreg_t	ii_iprbd_regval;
	struct  {
		bdrkreg_t	i_mult_err                :	 1;
		bdrkreg_t	i_rsvd                    :	11;
		bdrkreg_t	i_spur_rd                 :	 1;
		bdrkreg_t	i_spur_wr                 :	 1;
		bdrkreg_t	i_rd_to                   :	 1;
		bdrkreg_t	i_error                   :	 1;
		bdrkreg_t	i_of_cnt                  :	 5;
		bdrkreg_t	i_f                       :	 1;
		bdrkreg_t	i_m                       :	 2;
		bdrkreg_t	i_rsvd_1                  :	 2;
		bdrkreg_t	i_nb                      :	14;
		bdrkreg_t	i_rsvd_2                  :	 2;
		bdrkreg_t	i_na                      :	14;
		bdrkreg_t	i_c                       :	 8;
	} ii_iprbd_fld_s;
} ii_iprbd_u_t;

#endif




/************************************************************************
 *                                                                      *
 * Description:  There are 9 instances of this register, one per        *
 * actual widget in this implementation of Bedrock and Crossbow.        *
 * Note: Crossbow only has ports for Widgets 8 through F, widget 0      *
 * refers to Crossbow's internal space.                                 *
 * This register contains the state elements per widget that are        *
 * necessary to manage the PIO flow control on Crosstalk and on the     *
 * Router Network. See the PIO Flow Control chapter for a complete      *
 * description of this register                                         *
 * The SPUR_WR bit requires some explanation. When this register is     *
 * written, the new value of the C field is captured in an internal     *
 * register so the hardware can remember what the programmer wrote      *
 * into the credit counter. The SPUR_WR bit sets whenever the C field   *
 * increments above this stored value, which indicates that there       *
 * have been more responses received than requests sent. The SPUR_WR    *
 * bit cannot be cleared until a value is written to the IPRBx          *
 * register; the write will correct the C field and capture its new     *
 * value in the internal register. Even if IECLR[E_PRB_x] is set, the   *
 * SPUR_WR bit will persist if IPRBx hasn't yet been written.           *
 * .                                                                    *
 *                                                                      *
 ************************************************************************/




#ifdef LITTLE_ENDIAN

typedef union ii_iprbe_u {
	bdrkreg_t	ii_iprbe_regval;
	struct	{
		bdrkreg_t	i_c			  :	 8;
		bdrkreg_t	i_na			  :	14;
		bdrkreg_t	i_rsvd_2		  :	 2;
		bdrkreg_t	i_nb			  :	14;
		bdrkreg_t	i_rsvd_1		  :	 2;
		bdrkreg_t	i_m			  :	 2;
		bdrkreg_t	i_f			  :	 1;
		bdrkreg_t	i_of_cnt		  :	 5;
		bdrkreg_t	i_error			  :	 1;
		bdrkreg_t	i_rd_to			  :	 1;
		bdrkreg_t	i_spur_wr		  :	 1;
		bdrkreg_t	i_spur_rd		  :	 1;
		bdrkreg_t	i_rsvd			  :	11;
		bdrkreg_t	i_mult_err		  :	 1;
	} ii_iprbe_fld_s;
} ii_iprbe_u_t;

#else

typedef union ii_iprbe_u {
	bdrkreg_t	ii_iprbe_regval;
	struct  {
		bdrkreg_t	i_mult_err                :	 1;
		bdrkreg_t	i_rsvd                    :	11;
		bdrkreg_t	i_spur_rd                 :	 1;
		bdrkreg_t	i_spur_wr                 :	 1;
		bdrkreg_t	i_rd_to                   :	 1;
		bdrkreg_t	i_error                   :	 1;
		bdrkreg_t	i_of_cnt                  :	 5;
		bdrkreg_t	i_f                       :	 1;
		bdrkreg_t	i_m                       :	 2;
		bdrkreg_t	i_rsvd_1                  :	 2;
		bdrkreg_t	i_nb                      :	14;
		bdrkreg_t	i_rsvd_2                  :	 2;
		bdrkreg_t	i_na                      :	14;
		bdrkreg_t	i_c                       :	 8;
	} ii_iprbe_fld_s;
} ii_iprbe_u_t;

#endif




/************************************************************************
 *                                                                      *
 * Description:  There are 9 instances of this register, one per        *
 * actual widget in this implementation of Bedrock and Crossbow.        *
 * Note: Crossbow only has ports for Widgets 8 through F, widget 0      *
 * refers to Crossbow's internal space.                                 *
 * This register contains the state elements per widget that are        *
 * necessary to manage the PIO flow control on Crosstalk and on the     *
 * Router Network. See the PIO Flow Control chapter for a complete      *
 * description of this register                                         *
 * The SPUR_WR bit requires some explanation. When this register is     *
 * written, the new value of the C field is captured in an internal     *
 * register so the hardware can remember what the programmer wrote      *
 * into the credit counter. The SPUR_WR bit sets whenever the C field   *
 * increments above this stored value, which indicates that there       *
 * have been more responses received than requests sent. The SPUR_WR    *
 * bit cannot be cleared until a value is written to the IPRBx          *
 * register; the write will correct the C field and capture its new     *
 * value in the internal register. Even if IECLR[E_PRB_x] is set, the   *
 * SPUR_WR bit will persist if IPRBx hasn't yet been written.           *
 * .                                                                    *
 *                                                                      *
 ************************************************************************/




#ifdef LITTLE_ENDIAN

typedef union ii_iprbf_u {
        bdrkreg_t       ii_iprbf_regval;
        struct  {
                bdrkreg_t       i_c                       :      8;
                bdrkreg_t       i_na                      :     14;
                bdrkreg_t       i_rsvd_2                  :      2;
                bdrkreg_t       i_nb                      :     14;
                bdrkreg_t       i_rsvd_1                  :      2;
                bdrkreg_t       i_m                       :      2;
                bdrkreg_t       i_f                       :      1;
                bdrkreg_t       i_of_cnt                  :      5;
                bdrkreg_t       i_error                   :      1;
                bdrkreg_t       i_rd_to                   :      1;
                bdrkreg_t       i_spur_wr                 :      1;
                bdrkreg_t       i_spur_rd                 :      1;
                bdrkreg_t       i_rsvd                    :     11;
                bdrkreg_t       i_mult_err                :      1;
        } ii_iprbe_fld_s;
} ii_iprbf_u_t;

#else

typedef union ii_iprbf_u {
	bdrkreg_t	ii_iprbf_regval;
	struct  {
		bdrkreg_t	i_mult_err                :	 1;
		bdrkreg_t	i_rsvd                    :	11;
		bdrkreg_t	i_spur_rd                 :	 1;
		bdrkreg_t	i_spur_wr                 :	 1;
		bdrkreg_t	i_rd_to                   :	 1;
		bdrkreg_t	i_error                   :	 1;
		bdrkreg_t	i_of_cnt                  :	 5;
		bdrkreg_t	i_f                       :	 1;
		bdrkreg_t	i_m                       :	 2;
		bdrkreg_t	i_rsvd_1                  :	 2;
		bdrkreg_t	i_nb                      :	14;
		bdrkreg_t	i_rsvd_2                  :	 2;
		bdrkreg_t	i_na                      :	14;
		bdrkreg_t	i_c                       :	 8;
	} ii_iprbf_fld_s;
} ii_iprbf_u_t;

#endif




/************************************************************************
 *                                                                      *
 *  This register specifies the timeout value to use for monitoring     *
 * Crosstalk credits which are used outbound to Crosstalk. An           *
 * internal counter called the Crosstalk Credit Timeout Counter         *
 * increments every 128 II clocks. The counter starts counting          *
 * anytime the credit count drops below a threshold, and resets to      *
 * zero (stops counting) anytime the credit count is at or above the    *
 * threshold. The threshold is 1 credit in direct connect mode and 2    *
 * in Crossbow connect mode. When the internal Crosstalk Credit         *
 * Timeout Counter reaches the value programmed in this register, a     *
 * Crosstalk Credit Timeout has occurred. The internal counter is not   *
 * readable from software, and stops counting at its maximum value,     *
 * so it cannot cause more than one interrupt.                          *
 *                                                                      *
 ************************************************************************/




#ifdef LITTLE_ENDIAN

typedef union ii_ixcc_u {
	bdrkreg_t	ii_ixcc_regval;
	struct  {
		bdrkreg_t	i_time_out                :	26;
		bdrkreg_t	i_rsvd			  :	38;
	} ii_ixcc_fld_s;
} ii_ixcc_u_t;

#else

typedef union ii_ixcc_u {
	bdrkreg_t	ii_ixcc_regval;
	struct	{
		bdrkreg_t	i_rsvd			  :	38;
		bdrkreg_t	i_time_out		  :	26;
	} ii_ixcc_fld_s;
} ii_ixcc_u_t;

#endif



/************************************************************************
 *                                                                      *
 * Description:  This register qualifies all the PIO and DMA            *
 * operations launched from widget 0 towards the Bedrock. In            *
 * addition, it also qualifies accesses by the BTE streams.             *
 * The bits in each field of this register are cleared by the Bedrock   *
 * upon detection of an error which requires widget 0 or the BTE        *
 * streams to be terminated. Whether or not widget x has access         *
 * rights to this Bedrock is determined by an AND of the device         *
 * enable bit in the appropriate field of this register and bit 0 in    *
 * the Wx_IAC field. The bits in this field are set by writing a 1 to   *
 * them. Incoming replies from Crosstalk are not subject to this        *
 * access control mechanism.                                            *
 *                                                                      *
 ************************************************************************/




#ifdef LITTLE_ENDIAN

typedef union ii_imem_u {
	bdrkreg_t	ii_imem_regval;
	struct  {
		bdrkreg_t	i_w0_esd                  :	 1;
		bdrkreg_t	i_rsvd_3		  :	 3;
		bdrkreg_t	i_b0_esd		  :	 1;
		bdrkreg_t	i_rsvd_2		  :	 3;
		bdrkreg_t	i_b1_esd		  :	 1;
		bdrkreg_t	i_rsvd_1		  :	 3;
		bdrkreg_t	i_clr_precise		  :	 1;
		bdrkreg_t       i_rsvd                    :     51;
	} ii_imem_fld_s;
} ii_imem_u_t;

#else

typedef union ii_imem_u {
	bdrkreg_t	ii_imem_regval;
	struct	{
		bdrkreg_t	i_rsvd			  :	51;
		bdrkreg_t	i_clr_precise		  :	 1;
		bdrkreg_t	i_rsvd_1		  :	 3;
		bdrkreg_t	i_b1_esd		  :	 1;
		bdrkreg_t	i_rsvd_2		  :	 3;
		bdrkreg_t	i_b0_esd		  :	 1;
		bdrkreg_t	i_rsvd_3		  :	 3;
		bdrkreg_t	i_w0_esd		  :	 1;
	} ii_imem_fld_s;
} ii_imem_u_t;

#endif




/************************************************************************
 *                                                                      *
 * Description:  This register specifies the timeout value to use for   *
 * monitoring Crosstalk tail flits coming into the Bedrock in the       *
 * TAIL_TO field. An internal counter associated with this register     *
 * is incremented every 128 II internal clocks (7 bits). The counter    *
 * starts counting anytime a header micropacket is received and stops   *
 * counting (and resets to zero) any time a micropacket with a Tail     *
 * bit is received. Once the counter reaches the threshold value        *
 * programmed in this register, it generates an interrupt to the        *
 * processor that is programmed into the IIDSR. The counter saturates   *
 * (does not roll over) at its maximum value, so it cannot cause        *
 * another interrupt until after it is cleared.                         *
 * The register also contains the Read Response Timeout values. The     *
 * Prescalar is 23 bits, and counts II clocks. An internal counter      *
 * increments on every II clock and when it reaches the value in the    *
 * Prescalar field, all IPRTE registers with their valid bits set       *
 * have their Read Response timers bumped. Whenever any of them match   *
 * the value in the RRSP_TO field, a Read Response Timeout has          *
 * occurred, and error handling occurs as described in the Error        *
 * Handling section of this document.                                   *
 *                                                                      *
 ************************************************************************/




#ifdef LITTLE_ENDIAN

typedef union ii_ixtt_u {
	bdrkreg_t	ii_ixtt_regval;
	struct  {
		bdrkreg_t	i_tail_to                 :	26;
		bdrkreg_t	i_rsvd_1		  :	 6;
		bdrkreg_t	i_rrsp_ps		  :	23;
		bdrkreg_t	i_rrsp_to		  :	 5;
		bdrkreg_t	i_rsvd			  :	 4;
	} ii_ixtt_fld_s;
} ii_ixtt_u_t;

#else

typedef union ii_ixtt_u {
	bdrkreg_t	ii_ixtt_regval;
	struct	{
		bdrkreg_t	i_rsvd			  :	 4;
		bdrkreg_t	i_rrsp_to		  :	 5;
		bdrkreg_t	i_rrsp_ps		  :	23;
		bdrkreg_t	i_rsvd_1		  :	 6;
		bdrkreg_t	i_tail_to		  :	26;
	} ii_ixtt_fld_s;
} ii_ixtt_u_t;

#endif




/************************************************************************
 *                                                                      *
 *  Writing a 1 to the fields of this register clears the appropriate   *
 * error bits in other areas of Bedrock_II. Note that when the          *
 * E_PRB_x bits are used to clear error bits in PRB registers,          *
 * SPUR_RD and SPUR_WR may persist, because they require additional     *
 * action to clear them. See the IPRBx and IXSS Register                *
 * specifications.                                                      *
 *                                                                      *
 ************************************************************************/




#ifdef LITTLE_ENDIAN

typedef union ii_ieclr_u {
	bdrkreg_t	ii_ieclr_regval;
	struct  {
		bdrkreg_t	i_e_prb_0                 :	 1;
		bdrkreg_t	i_rsvd			  :	 7;
		bdrkreg_t	i_e_prb_8		  :	 1;
		bdrkreg_t	i_e_prb_9		  :	 1;
		bdrkreg_t	i_e_prb_a		  :	 1;
		bdrkreg_t	i_e_prb_b		  :	 1;
		bdrkreg_t	i_e_prb_c		  :	 1;
		bdrkreg_t	i_e_prb_d		  :	 1;
		bdrkreg_t	i_e_prb_e		  :	 1;
		bdrkreg_t	i_e_prb_f		  :	 1;
		bdrkreg_t	i_e_crazy		  :	 1;
		bdrkreg_t	i_e_bte_0		  :	 1;
		bdrkreg_t	i_e_bte_1		  :	 1;
		bdrkreg_t	i_reserved_1		  :	 9;
		bdrkreg_t	i_ii_internal		  :	 1;
		bdrkreg_t	i_spur_rd_hdr		  :	 1;
		bdrkreg_t	i_pi0_forward_int	  :	 1;
		bdrkreg_t	i_pi1_forward_int	  :	 1;
		bdrkreg_t       i_reserved                :     32;
	} ii_ieclr_fld_s;
} ii_ieclr_u_t;

#else

typedef union ii_ieclr_u {
	bdrkreg_t	ii_ieclr_regval;
	struct	{
		bdrkreg_t	i_reserved		  :	32;
		bdrkreg_t	i_pi1_forward_int	  :	 1;
		bdrkreg_t	i_pi0_forward_int	  :	 1;
		bdrkreg_t	i_spur_rd_hdr		  :	 1;
		bdrkreg_t	i_ii_internal		  :	 1;
		bdrkreg_t	i_reserved_1		  :	 9;
		bdrkreg_t	i_e_bte_1		  :	 1;
		bdrkreg_t	i_e_bte_0		  :	 1;
		bdrkreg_t	i_e_crazy		  :	 1;
		bdrkreg_t	i_e_prb_f		  :	 1;
		bdrkreg_t	i_e_prb_e		  :	 1;
		bdrkreg_t	i_e_prb_d		  :	 1;
		bdrkreg_t	i_e_prb_c		  :	 1;
		bdrkreg_t	i_e_prb_b		  :	 1;
		bdrkreg_t	i_e_prb_a		  :	 1;
		bdrkreg_t	i_e_prb_9		  :	 1;
		bdrkreg_t	i_e_prb_8		  :	 1;
		bdrkreg_t	i_rsvd			  :	 7;
		bdrkreg_t	i_e_prb_0		  :	 1;
	} ii_ieclr_fld_s;
} ii_ieclr_u_t;

#endif





/************************************************************************
 *                                                                      *
 *  This register controls both BTEs. SOFT_RESET is intended for        *
 * recovery after an error. COUNT controls the total number of CRBs     *
 * that both BTEs (combined) can use, which affects total BTE           *
 * bandwidth.                                                           *
 *                                                                      *
 ************************************************************************/




#ifdef LITTLE_ENDIAN

typedef union ii_ibcr_u {
	bdrkreg_t	ii_ibcr_regval;
	struct  {
		bdrkreg_t	i_count                   :	 4;
		bdrkreg_t	i_rsvd_1		  :	 4;
		bdrkreg_t	i_soft_reset		  :	 1;
		bdrkreg_t	i_rsvd			  :	55;
	} ii_ibcr_fld_s;
} ii_ibcr_u_t;

#else

typedef union ii_ibcr_u {
	bdrkreg_t	ii_ibcr_regval;
	struct	{
		bdrkreg_t	i_rsvd			  :	55;
		bdrkreg_t	i_soft_reset		  :	 1;
		bdrkreg_t	i_rsvd_1		  :	 4;
		bdrkreg_t	i_count			  :	 4;
	} ii_ibcr_fld_s;
} ii_ibcr_u_t;

#endif




/************************************************************************
 *                                                                      *
 *  This register contains the header of a spurious read response       *
 * received from Crosstalk. A spurious read response is defined as a    *
 * read response received by II from a widget for which (1) the SIDN    *
 * has a value between 1 and 7, inclusive (II never sends requests to   *
 * these widgets (2) there is no valid IPRTE register which             *
 * corresponds to the TNUM, or (3) the widget indicated in SIDN is      *
 * not the same as the widget recorded in the IPRTE register            *
 * referenced by the TNUM. If this condition is true, and if the        *
 * IXSS[VALID] bit is clear, then the header of the spurious read       *
 * response is capture in IXSM and IXSS, and IXSS[VALID] is set. The    *
 * errant header is thereby captured, and no further spurious read      *
 * respones are captured until IXSS[VALID] is cleared by setting the    *
 * appropriate bit in IECLR.Everytime a spurious read response is       *
 * detected, the SPUR_RD bit of the PRB corresponding to the incoming   *
 * message's SIDN field is set. This always happens, regarless of       *
 * whether a header is captured. The programmer should check            *
 * IXSM[SIDN] to determine which widget sent the spurious response,     *
 * because there may be more than one SPUR_RD bit set in the PRB        *
 * registers. The widget indicated by IXSM[SIDN] was the first          *
 * spurious read response to be received since the last time            *
 * IXSS[VALID] was clear. The SPUR_RD bit of the corresponding PRB      *
 * will be set. Any SPUR_RD bits in any other PRB registers indicate    *
 * spurious messages from other widets which were detected after the    *
 * header was captured..                                                *
 *                                                                      *
 ************************************************************************/




#ifdef LITTLE_ENDIAN

typedef union ii_ixsm_u {
	bdrkreg_t	ii_ixsm_regval;
	struct  {
		bdrkreg_t	i_byte_en                 :	32;
		bdrkreg_t	i_reserved		  :	 1;
		bdrkreg_t	i_tag			  :	 3;
		bdrkreg_t	i_alt_pactyp		  :	 4;
		bdrkreg_t	i_bo			  :	 1;
		bdrkreg_t	i_error			  :	 1;
		bdrkreg_t	i_vbpm			  :	 1;
		bdrkreg_t	i_gbr			  :	 1;
		bdrkreg_t	i_ds			  :	 2;
		bdrkreg_t	i_ct			  :	 1;
		bdrkreg_t	i_tnum			  :	 5;
		bdrkreg_t	i_pactyp		  :	 4;
		bdrkreg_t	i_sidn			  :	 4;
		bdrkreg_t	i_didn			  :	 4;
	} ii_ixsm_fld_s;
} ii_ixsm_u_t;

#else

typedef union ii_ixsm_u {
	bdrkreg_t	ii_ixsm_regval;
	struct	{
		bdrkreg_t	i_didn			  :	 4;
		bdrkreg_t	i_sidn			  :	 4;
		bdrkreg_t	i_pactyp		  :	 4;
		bdrkreg_t	i_tnum			  :	 5;
		bdrkreg_t	i_ct			  :	 1;
		bdrkreg_t	i_ds			  :	 2;
		bdrkreg_t	i_gbr			  :	 1;
		bdrkreg_t	i_vbpm			  :	 1;
		bdrkreg_t	i_error			  :	 1;
		bdrkreg_t	i_bo			  :	 1;
		bdrkreg_t	i_alt_pactyp		  :	 4;
		bdrkreg_t	i_tag			  :	 3;
		bdrkreg_t	i_reserved		  :	 1;
		bdrkreg_t	i_byte_en		  :	32;
	} ii_ixsm_fld_s;
} ii_ixsm_u_t;

#endif




/************************************************************************
 *                                                                      *
 *  This register contains the sideband bits of a spurious read         *
 * response received from Crosstalk.                                    *
 *                                                                      *
 ************************************************************************/




#ifdef LITTLE_ENDIAN

typedef union ii_ixss_u {
	bdrkreg_t	ii_ixss_regval;
	struct  {
		bdrkreg_t	i_sideband                :	 8;
		bdrkreg_t	i_rsvd			  :	55;
		bdrkreg_t	i_valid			  :	 1;
	} ii_ixss_fld_s;
} ii_ixss_u_t;

#else

typedef union ii_ixss_u {
	bdrkreg_t	ii_ixss_regval;
	struct	{
		bdrkreg_t	i_valid			  :	 1;
		bdrkreg_t	i_rsvd			  :	55;
		bdrkreg_t	i_sideband		  :	 8;
	} ii_ixss_fld_s;
} ii_ixss_u_t;

#endif




/************************************************************************
 *                                                                      *
 *  This register enables software to access the II LLP's test port.    *
 * Refer to the LLP 2.5 documentation for an explanation of the test    *
 * port. Software can write to this register to program the values      *
 * for the control fields (TestErrCapture, TestClear, TestFlit,         *
 * TestMask and TestSeed). Similarly, software can read from this       *
 * register to obtain the values of the test port's status outputs      *
 * (TestCBerr, TestValid and TestData).                                 *
 *                                                                      *
 ************************************************************************/




#ifdef LITTLE_ENDIAN

typedef union ii_ilct_u {
	bdrkreg_t	ii_ilct_regval;
	struct  {
		bdrkreg_t	i_rsvd                    :	 9;
		bdrkreg_t	i_test_err_capture        :	 1;
		bdrkreg_t	i_test_clear              :	 1;
		bdrkreg_t	i_test_flit               :	 3;
		bdrkreg_t	i_test_cberr              :	 1;
		bdrkreg_t	i_test_valid              :	 1;
		bdrkreg_t	i_test_data               :	20;
		bdrkreg_t	i_test_mask               :	 8;
		bdrkreg_t	i_test_seed               :	20;
	} ii_ilct_fld_s;
} ii_ilct_u_t;

#else

typedef union ii_ilct_u {
	bdrkreg_t	ii_ilct_regval;
	struct	{
		bdrkreg_t	i_rsvd			  :	 9;
		bdrkreg_t	i_test_err_capture	  :	 1;
		bdrkreg_t	i_test_clear		  :	 1;
		bdrkreg_t	i_test_flit		  :	 3;
		bdrkreg_t	i_test_cberr		  :	 1;
		bdrkreg_t	i_test_valid		  :	 1;
		bdrkreg_t	i_test_data		  :	20;
		bdrkreg_t	i_test_mask		  :	 8;
		bdrkreg_t	i_test_seed		  :	20;
	} ii_ilct_fld_s;
} ii_ilct_u_t;

#endif




/************************************************************************
 *                                                                      *
 *  If the II detects an illegal incoming Duplonet packet (request or   *
 * reply) when VALID==0 in the IIEPH1 register, then it saves the       *
 * contents of the packet's header flit in the IIEPH1 and IIEPH2        *
 * registers, sets the VALID bit in IIEPH1, clears the OVERRUN bit,     *
 * and assigns a value to the ERR_TYPE field which indicates the        *
 * specific nature of the error. The II recognizes four different       *
 * types of errors: short request packets (ERR_TYPE==2), short reply    *
 * packets (ERR_TYPE==3), long request packets (ERR_TYPE==4) and long   *
 * reply packets (ERR_TYPE==5). The encodings for these types of        *
 * errors were chosen to be consistent with the same types of errors    *
 * indicated by the ERR_TYPE field in the LB_ERROR_HDR1 register (in    *
 * the LB unit). If the II detects an illegal incoming Duplonet         *
 * packet when VALID==1 in the IIEPH1 register, then it merely sets     *
 * the OVERRUN bit to indicate that a subsequent error has happened,    *
 * and does nothing further.                                            *
 *                                                                      *
 ************************************************************************/




#ifdef LITTLE_ENDIAN

typedef union ii_iieph1_u {
	bdrkreg_t	ii_iieph1_regval;
	struct	{
		bdrkreg_t	i_command		  :	 7;
		bdrkreg_t	i_rsvd_5		  :	 1;
		bdrkreg_t	i_suppl			  :	11;
		bdrkreg_t	i_rsvd_4		  :	 1;
		bdrkreg_t	i_source		  :	11;
		bdrkreg_t	i_rsvd_3		  :	 1;
		bdrkreg_t	i_err_type		  :	 4;
		bdrkreg_t	i_rsvd_2		  :	 4;
		bdrkreg_t	i_overrun		  :	 1;
		bdrkreg_t	i_rsvd_1		  :	 3;
		bdrkreg_t	i_valid			  :	 1;
		bdrkreg_t	i_rsvd			  :	19;
	} ii_iieph1_fld_s;
} ii_iieph1_u_t;

#else

typedef union ii_iieph1_u {
	bdrkreg_t	ii_iieph1_regval;
	struct  {
		bdrkreg_t	i_rsvd                    :	19;
		bdrkreg_t	i_valid                   :	 1;
		bdrkreg_t	i_rsvd_1                  :	 3;
		bdrkreg_t	i_overrun                 :	 1;
		bdrkreg_t	i_rsvd_2                  :	 4;
		bdrkreg_t	i_err_type                :	 4;
		bdrkreg_t	i_rsvd_3                  :	 1;
		bdrkreg_t	i_source                  :	11;
		bdrkreg_t	i_rsvd_4                  :	 1;
		bdrkreg_t	i_suppl                   :	11;
		bdrkreg_t	i_rsvd_5                  :	 1;
		bdrkreg_t	i_command                 :	 7;
	} ii_iieph1_fld_s;
} ii_iieph1_u_t;

#endif




/************************************************************************
 *                                                                      *
 *  This register holds the Address field from the header flit of an    *
 * incoming erroneous Duplonet packet, along with the tail bit which    *
 * accompanied this header flit. This register is essentially an        *
 * extension of IIEPH1. Two registers were necessary because the 64     *
 * bits available in only a single register were insufficient to        *
 * capture the entire header flit of an erroneous packet.               *
 *                                                                      *
 ************************************************************************/




#ifdef LITTLE_ENDIAN

typedef union ii_iieph2_u {
	bdrkreg_t	ii_iieph2_regval;
	struct  {
		bdrkreg_t	i_address                 :	38;
		bdrkreg_t	i_rsvd_1		  :	 2;
		bdrkreg_t	i_tail			  :	 1;
		bdrkreg_t	i_rsvd			  :	23;
	} ii_iieph2_fld_s;
} ii_iieph2_u_t;

#else

typedef union ii_iieph2_u {
	bdrkreg_t	ii_iieph2_regval;
	struct	{
		bdrkreg_t	i_rsvd			  :	23;
		bdrkreg_t	i_tail			  :	 1;
		bdrkreg_t	i_rsvd_1		  :	 2;
		bdrkreg_t	i_address		  :	38;
	} ii_iieph2_fld_s;
} ii_iieph2_u_t;

#endif




/************************************************************************
 *                                                                      *
 *  A write to this register causes a particular field in the           *
 * corresponding widget's PRB entry to be adjusted up or down by 1.     *
 * This counter should be used when recovering from error and reset     *
 * conditions. Note that software would be capable of causing           *
 * inadvertent overflow or underflow of these counters.                 *
 *                                                                      *
 ************************************************************************/




#ifdef LITTLE_ENDIAN

typedef union ii_ipca_u {
	bdrkreg_t	ii_ipca_regval;
	struct  {
		bdrkreg_t	i_wid                     :	 4;
		bdrkreg_t	i_adjust		  :	 1;
		bdrkreg_t	i_rsvd_1		  :	 3;
		bdrkreg_t	i_field			  :	 2;
		bdrkreg_t	i_rsvd			  :	54;
	} ii_ipca_fld_s;
} ii_ipca_u_t;

#else

typedef union ii_ipca_u {
	bdrkreg_t	ii_ipca_regval;
	struct	{
		bdrkreg_t	i_rsvd			  :	54;
		bdrkreg_t	i_field			  :	 2;
		bdrkreg_t	i_rsvd_1		  :	 3;
		bdrkreg_t	i_adjust		  :	 1;
		bdrkreg_t	i_wid			  :	 4;
	} ii_ipca_fld_s;
} ii_ipca_u_t;

#endif




/************************************************************************
 *                                                                      *
 *  There are 8 instances of this register. This register contains      *
 * the information that the II has to remember once it has launched a   *
 * PIO Read operation. The contents are used to form the correct        *
 * Router Network packet and direct the Crosstalk reply to the          *
 * appropriate processor.                                               *
 *                                                                      *
 ************************************************************************/




#ifdef LITTLE_ENDIAN

typedef union ii_iprte0_u {
	bdrkreg_t	ii_iprte0_regval;
	struct  {
		bdrkreg_t	i_rsvd_1                  :	 3;
		bdrkreg_t	i_addr			  :	38;
		bdrkreg_t	i_init			  :	 3;
		bdrkreg_t	i_source		  :	 8;
		bdrkreg_t	i_rsvd			  :	 2;
		bdrkreg_t	i_widget		  :	 4;
		bdrkreg_t	i_to_cnt		  :	 5;
		bdrkreg_t       i_vld                     :      1;
	} ii_iprte0_fld_s;
} ii_iprte0_u_t;

#else

typedef union ii_iprte0_u {
	bdrkreg_t	ii_iprte0_regval;
	struct	{
		bdrkreg_t	i_vld			  :	 1;
		bdrkreg_t	i_to_cnt		  :	 5;
		bdrkreg_t	i_widget		  :	 4;
		bdrkreg_t	i_rsvd			  :	 2;
		bdrkreg_t	i_source		  :	 8;
		bdrkreg_t	i_init			  :	 3;
		bdrkreg_t	i_addr			  :	38;
		bdrkreg_t	i_rsvd_1		  :	 3;
	} ii_iprte0_fld_s;
} ii_iprte0_u_t;

#endif




/************************************************************************
 *                                                                      *
 *  There are 8 instances of this register. This register contains      *
 * the information that the II has to remember once it has launched a   *
 * PIO Read operation. The contents are used to form the correct        *
 * Router Network packet and direct the Crosstalk reply to the          *
 * appropriate processor.                                               *
 *                                                                      *
 ************************************************************************/




#ifdef LITTLE_ENDIAN

typedef union ii_iprte1_u {
	bdrkreg_t	ii_iprte1_regval;
	struct  {
		bdrkreg_t	i_rsvd_1                  :	 3;
		bdrkreg_t	i_addr			  :	38;
		bdrkreg_t	i_init			  :	 3;
		bdrkreg_t	i_source		  :	 8;
		bdrkreg_t	i_rsvd			  :	 2;
		bdrkreg_t	i_widget		  :	 4;
		bdrkreg_t	i_to_cnt		  :	 5;
		bdrkreg_t       i_vld                     :      1;
	} ii_iprte1_fld_s;
} ii_iprte1_u_t;

#else

typedef union ii_iprte1_u {
	bdrkreg_t	ii_iprte1_regval;
	struct	{
		bdrkreg_t	i_vld			  :	 1;
		bdrkreg_t	i_to_cnt		  :	 5;
		bdrkreg_t	i_widget		  :	 4;
		bdrkreg_t	i_rsvd			  :	 2;
		bdrkreg_t	i_source		  :	 8;
		bdrkreg_t	i_init			  :	 3;
		bdrkreg_t	i_addr			  :	38;
		bdrkreg_t	i_rsvd_1		  :	 3;
	} ii_iprte1_fld_s;
} ii_iprte1_u_t;

#endif




/************************************************************************
 *                                                                      *
 *  There are 8 instances of this register. This register contains      *
 * the information that the II has to remember once it has launched a   *
 * PIO Read operation. The contents are used to form the correct        *
 * Router Network packet and direct the Crosstalk reply to the          *
 * appropriate processor.                                               *
 *                                                                      *
 ************************************************************************/




#ifdef LITTLE_ENDIAN

typedef union ii_iprte2_u {
	bdrkreg_t	ii_iprte2_regval;
	struct  {
		bdrkreg_t	i_rsvd_1                  :	 3;
		bdrkreg_t	i_addr			  :	38;
		bdrkreg_t	i_init			  :	 3;
		bdrkreg_t	i_source		  :	 8;
		bdrkreg_t	i_rsvd			  :	 2;
		bdrkreg_t	i_widget		  :	 4;
		bdrkreg_t	i_to_cnt		  :	 5;
		bdrkreg_t       i_vld                     :      1;
	} ii_iprte2_fld_s;
} ii_iprte2_u_t;

#else

typedef union ii_iprte2_u {
	bdrkreg_t	ii_iprte2_regval;
	struct	{
		bdrkreg_t	i_vld			  :	 1;
		bdrkreg_t	i_to_cnt		  :	 5;
		bdrkreg_t	i_widget		  :	 4;
		bdrkreg_t	i_rsvd			  :	 2;
		bdrkreg_t	i_source		  :	 8;
		bdrkreg_t	i_init			  :	 3;
		bdrkreg_t	i_addr			  :	38;
		bdrkreg_t	i_rsvd_1		  :	 3;
	} ii_iprte2_fld_s;
} ii_iprte2_u_t;

#endif




/************************************************************************
 *                                                                      *
 *  There are 8 instances of this register. This register contains      *
 * the information that the II has to remember once it has launched a   *
 * PIO Read operation. The contents are used to form the correct        *
 * Router Network packet and direct the Crosstalk reply to the          *
 * appropriate processor.                                               *
 *                                                                      *
 ************************************************************************/




#ifdef LITTLE_ENDIAN

typedef union ii_iprte3_u {
	bdrkreg_t	ii_iprte3_regval;
	struct  {
		bdrkreg_t	i_rsvd_1                  :	 3;
		bdrkreg_t	i_addr			  :	38;
		bdrkreg_t	i_init			  :	 3;
		bdrkreg_t	i_source		  :	 8;
		bdrkreg_t	i_rsvd			  :	 2;
		bdrkreg_t	i_widget		  :	 4;
		bdrkreg_t	i_to_cnt		  :	 5;
		bdrkreg_t	i_vld			  :	 1;
	} ii_iprte3_fld_s;
} ii_iprte3_u_t;

#else

typedef union ii_iprte3_u {
	bdrkreg_t	ii_iprte3_regval;
	struct	{
		bdrkreg_t	i_vld			  :	 1;
		bdrkreg_t	i_to_cnt		  :	 5;
		bdrkreg_t	i_widget		  :	 4;
		bdrkreg_t	i_rsvd			  :	 2;
		bdrkreg_t	i_source		  :	 8;
		bdrkreg_t	i_init			  :	 3;
		bdrkreg_t	i_addr			  :	38;
		bdrkreg_t	i_rsvd_1		  :	 3;
	} ii_iprte3_fld_s;
} ii_iprte3_u_t;

#endif




/************************************************************************
 *                                                                      *
 *  There are 8 instances of this register. This register contains      *
 * the information that the II has to remember once it has launched a   *
 * PIO Read operation. The contents are used to form the correct        *
 * Router Network packet and direct the Crosstalk reply to the          *
 * appropriate processor.                                               *
 *                                                                      *
 ************************************************************************/




#ifdef LITTLE_ENDIAN

typedef union ii_iprte4_u {
	bdrkreg_t	ii_iprte4_regval;
	struct	{
		bdrkreg_t	i_rsvd_1		  :	 3;
		bdrkreg_t	i_addr			  :	38;
		bdrkreg_t	i_init			  :	 3;
		bdrkreg_t	i_source		  :	 8;
		bdrkreg_t	i_rsvd			  :	 2;
		bdrkreg_t	i_widget		  :	 4;
		bdrkreg_t	i_to_cnt		  :	 5;
		bdrkreg_t	i_vld			  :	 1;
	} ii_iprte4_fld_s;
} ii_iprte4_u_t;

#else

typedef union ii_iprte4_u {
	bdrkreg_t	ii_iprte4_regval;
	struct  {
		bdrkreg_t	i_vld                     :	 1;
		bdrkreg_t	i_to_cnt                  :	 5;
		bdrkreg_t	i_widget                  :	 4;
		bdrkreg_t	i_rsvd                    :	 2;
		bdrkreg_t	i_source                  :	 8;
		bdrkreg_t	i_init                    :	 3;
		bdrkreg_t	i_addr                    :	38;
		bdrkreg_t	i_rsvd_1                  :	 3;
	} ii_iprte4_fld_s;
} ii_iprte4_u_t;

#endif




/************************************************************************
 *                                                                      *
 *  There are 8 instances of this register. This register contains      *
 * the information that the II has to remember once it has launched a   *
 * PIO Read operation. The contents are used to form the correct        *
 * Router Network packet and direct the Crosstalk reply to the          *
 * appropriate processor.                                               *
 *                                                                      *
 ************************************************************************/




#ifdef LITTLE_ENDIAN

typedef union ii_iprte5_u {
	bdrkreg_t	ii_iprte5_regval;
	struct	{
		bdrkreg_t	i_rsvd_1		  :	 3;
		bdrkreg_t	i_addr			  :	38;
		bdrkreg_t	i_init			  :	 3;
		bdrkreg_t	i_source		  :	 8;
		bdrkreg_t	i_rsvd			  :	 2;
		bdrkreg_t	i_widget		  :	 4;
		bdrkreg_t	i_to_cnt		  :	 5;
		bdrkreg_t	i_vld			  :	 1;
	} ii_iprte5_fld_s;
} ii_iprte5_u_t;

#else

typedef union ii_iprte5_u {
	bdrkreg_t	ii_iprte5_regval;
	struct  {
		bdrkreg_t	i_vld                     :	 1;
		bdrkreg_t	i_to_cnt                  :	 5;
		bdrkreg_t	i_widget                  :	 4;
		bdrkreg_t	i_rsvd                    :	 2;
		bdrkreg_t	i_source                  :	 8;
		bdrkreg_t	i_init                    :	 3;
		bdrkreg_t	i_addr                    :	38;
		bdrkreg_t	i_rsvd_1                  :	 3;
	} ii_iprte5_fld_s;
} ii_iprte5_u_t;

#endif




/************************************************************************
 *                                                                      *
 *  There are 8 instances of this register. This register contains      *
 * the information that the II has to remember once it has launched a   *
 * PIO Read operation. The contents are used to form the correct        *
 * Router Network packet and direct the Crosstalk reply to the          *
 * appropriate processor.                                               *
 *                                                                      *
 ************************************************************************/




#ifdef LITTLE_ENDIAN

typedef union ii_iprte6_u {
	bdrkreg_t	ii_iprte6_regval;
	struct	{
		bdrkreg_t	i_rsvd_1		  :	 3;
		bdrkreg_t	i_addr			  :	38;
		bdrkreg_t	i_init			  :	 3;
		bdrkreg_t	i_source		  :	 8;
		bdrkreg_t	i_rsvd			  :	 2;
		bdrkreg_t	i_widget		  :	 4;
		bdrkreg_t	i_to_cnt		  :	 5;
		bdrkreg_t	i_vld			  :	 1;
	} ii_iprte6_fld_s;
} ii_iprte6_u_t;

#else

typedef union ii_iprte6_u {
	bdrkreg_t	ii_iprte6_regval;
	struct  {
		bdrkreg_t	i_vld                     :	 1;
		bdrkreg_t	i_to_cnt                  :	 5;
		bdrkreg_t	i_widget                  :	 4;
		bdrkreg_t	i_rsvd                    :	 2;
		bdrkreg_t	i_source                  :	 8;
		bdrkreg_t	i_init                    :	 3;
		bdrkreg_t	i_addr                    :	38;
		bdrkreg_t	i_rsvd_1                  :	 3;
	} ii_iprte6_fld_s;
} ii_iprte6_u_t;

#endif




/************************************************************************
 *                                                                      *
 *  There are 8 instances of this register. This register contains      *
 * the information that the II has to remember once it has launched a   *
 * PIO Read operation. The contents are used to form the correct        *
 * Router Network packet and direct the Crosstalk reply to the          *
 * appropriate processor.                                               *
 *                                                                      *
 ************************************************************************/




#ifdef LITTLE_ENDIAN

typedef union ii_iprte7_u {
        bdrkreg_t       ii_iprte7_regval;
        struct  {
                bdrkreg_t       i_rsvd_1                  :      3;
                bdrkreg_t       i_addr                    :     38;
                bdrkreg_t       i_init                    :      3;
                bdrkreg_t       i_source                  :      8;
                bdrkreg_t       i_rsvd                    :      2;
                bdrkreg_t       i_widget                  :      4;
                bdrkreg_t       i_to_cnt                  :      5;
                bdrkreg_t       i_vld                     :      1;
        } ii_iprte7_fld_s;
} ii_iprte7_u_t;

#else

typedef union ii_iprte7_u {
	bdrkreg_t	ii_iprte7_regval;
	struct  {
		bdrkreg_t	i_vld                     :	 1;
		bdrkreg_t	i_to_cnt                  :	 5;
		bdrkreg_t	i_widget                  :	 4;
		bdrkreg_t	i_rsvd                    :	 2;
		bdrkreg_t	i_source                  :	 8;
		bdrkreg_t	i_init                    :	 3;
		bdrkreg_t	i_addr                    :	38;
		bdrkreg_t	i_rsvd_1                  :	 3;
	} ii_iprte7_fld_s;
} ii_iprte7_u_t;

#endif




/************************************************************************
 *                                                                      *
 * Description:  Bedrock_II contains a feature which did not exist in   *
 * the Hub which automatically cleans up after a Read Response          *
 * timeout, including deallocation of the IPRTE and recovery of IBuf    *
 * space. The inclusion of this register in Bedrock is for backward     *
 * compatibility                                                        *
 * A write to this register causes an entry from the table of           *
 * outstanding PIO Read Requests to be freed and returned to the        *
 * stack of free entries. This register is used in handling the         *
 * timeout errors that result in a PIO Reply never returning from       *
 * Crosstalk.                                                           *
 * Note that this register does not affect the contents of the IPRTE    *
 * registers. The Valid bits in those registers have to be              *
 * specifically turned off by software.                                 *
 *                                                                      *
 ************************************************************************/




#ifdef LITTLE_ENDIAN

typedef union ii_ipdr_u {
	bdrkreg_t	ii_ipdr_regval;
	struct  {
		bdrkreg_t	i_te                      :	 3;
		bdrkreg_t	i_rsvd_1		  :	 1;
		bdrkreg_t	i_pnd			  :	 1;
		bdrkreg_t	i_init_rpcnt		  :	 1;
		bdrkreg_t	i_rsvd			  :	58;
	} ii_ipdr_fld_s;
} ii_ipdr_u_t;

#else

typedef union ii_ipdr_u {
	bdrkreg_t	ii_ipdr_regval;
	struct	{
		bdrkreg_t	i_rsvd			  :	58;
		bdrkreg_t	i_init_rpcnt		  :	 1;
		bdrkreg_t	i_pnd			  :	 1;
		bdrkreg_t	i_rsvd_1		  :	 1;
		bdrkreg_t	i_te			  :	 3;
	} ii_ipdr_fld_s;
} ii_ipdr_u_t;

#endif




/************************************************************************
 *                                                                      *
 *  A write to this register causes a CRB entry to be returned to the   *
 * queue of free CRBs. The entry should have previously been cleared    *
 * (mark bit) via backdoor access to the pertinent CRB entry. This      *
 * register is used in the last step of handling the errors that are    *
 * captured and marked in CRB entries.  Briefly: 1) first error for     *
 * DMA write from a particular device, and first error for a            *
 * particular BTE stream, lead to a marked CRB entry, and processor     *
 * interrupt, 2) software reads the error information captured in the   *
 * CRB entry, and presumably takes some corrective action, 3)           *
 * software clears the mark bit, and finally 4) software writes to      *
 * the ICDR register to return the CRB entry to the list of free CRB    *
 * entries.                                                             *
 *                                                                      *
 ************************************************************************/




#ifdef LITTLE_ENDIAN

typedef union ii_icdr_u {
	bdrkreg_t	ii_icdr_regval;
	struct  {
		bdrkreg_t	i_crb_num                 :	 4;
		bdrkreg_t	i_pnd			  :	 1;
		bdrkreg_t       i_rsvd                    :     59;
	} ii_icdr_fld_s;
} ii_icdr_u_t;

#else

typedef union ii_icdr_u {
	bdrkreg_t	ii_icdr_regval;
	struct	{
		bdrkreg_t	i_rsvd			  :	59;
		bdrkreg_t	i_pnd			  :	 1;
		bdrkreg_t	i_crb_num		  :	 4;
	} ii_icdr_fld_s;
} ii_icdr_u_t;

#endif




/************************************************************************
 *                                                                      *
 *  This register provides debug access to two FIFOs inside of II.      *
 * Both IOQ_MAX* fields of this register contain the instantaneous      *
 * depth (in units of the number of available entries) of the           *
 * associated IOQ FIFO.  A read of this register will return the        *
 * number of free entries on each FIFO at the time of the read.  So     *
 * when a FIFO is idle, the associated field contains the maximum       *
 * depth of the FIFO.  This register is writable for debug reasons      *
 * and is intended to be written with the maximum desired FIFO depth    *
 * while the FIFO is idle. Software must assure that II is idle when    *
 * this register is written. If there are any active entries in any     *
 * of these FIFOs when this register is written, the results are        *
 * undefined.                                                           *
 *                                                                      *
 ************************************************************************/




#ifdef LITTLE_ENDIAN

typedef union ii_ifdr_u {
	bdrkreg_t	ii_ifdr_regval;
	struct  {
		bdrkreg_t	i_ioq_max_rq              :	 7;
		bdrkreg_t	i_set_ioq_rq		  :	 1;
		bdrkreg_t	i_ioq_max_rp		  :	 7;
		bdrkreg_t	i_set_ioq_rp		  :	 1;
		bdrkreg_t	i_rsvd			  :	48;
	} ii_ifdr_fld_s;
} ii_ifdr_u_t;

#else

typedef union ii_ifdr_u {
	bdrkreg_t	ii_ifdr_regval;
	struct	{
		bdrkreg_t	i_rsvd			  :	48;
		bdrkreg_t	i_set_ioq_rp		  :	 1;
		bdrkreg_t	i_ioq_max_rp		  :	 7;
		bdrkreg_t	i_set_ioq_rq		  :	 1;
		bdrkreg_t	i_ioq_max_rq		  :	 7;
	} ii_ifdr_fld_s;
} ii_ifdr_u_t;

#endif




/************************************************************************
 *                                                                      *
 *  This register allows the II to become sluggish in removing          *
 * messages from its inbound queue (IIQ). This will cause messages to   *
 * back up in either virtual channel. Disabling the "molasses" mode     *
 * subsequently allows the II to be tested under stress. In the         *
 * sluggish ("Molasses") mode, the localized effects of congestion      *
 * can be observed.                                                     *
 *                                                                      *
 ************************************************************************/




#ifdef LITTLE_ENDIAN

typedef union ii_iiap_u {
        bdrkreg_t       ii_iiap_regval;
        struct  {
                bdrkreg_t       i_rq_mls                  :      6;
		bdrkreg_t	i_rsvd_1		  :	 2;
		bdrkreg_t	i_rp_mls		  :	 6;
		bdrkreg_t       i_rsvd                    :     50;
        } ii_iiap_fld_s;
} ii_iiap_u_t;

#else

typedef union ii_iiap_u {
	bdrkreg_t	ii_iiap_regval;
	struct  {
		bdrkreg_t	i_rsvd                    :	50;
		bdrkreg_t	i_rp_mls                  :	 6;
		bdrkreg_t	i_rsvd_1                  :	 2;
		bdrkreg_t	i_rq_mls                  :	 6;
	} ii_iiap_fld_s;
} ii_iiap_u_t;

#endif




/************************************************************************
 *                                                                      *
 *  This register allows several parameters of CRB operation to be      *
 * set. Note that writing to this register can have catastrophic side   *
 * effects, if the CRB is not quiescent, i.e. if the CRB is             *
 * processing protocol messages when the write occurs.                  *
 *                                                                      *
 ************************************************************************/




#ifdef LITTLE_ENDIAN

typedef union ii_icmr_u {
	bdrkreg_t	ii_icmr_regval;
	struct  {
		bdrkreg_t	i_sp_msg                  :	 1;
		bdrkreg_t	i_rd_hdr		  :	 1;
		bdrkreg_t	i_rsvd_4		  :	 2;
		bdrkreg_t	i_c_cnt			  :	 4;
		bdrkreg_t	i_rsvd_3		  :	 4;
		bdrkreg_t	i_clr_rqpd		  :	 1;
		bdrkreg_t	i_clr_rppd		  :	 1;
		bdrkreg_t	i_rsvd_2		  :	 2;
		bdrkreg_t	i_fc_cnt		  :	 4;
		bdrkreg_t	i_crb_vld		  :	15;
		bdrkreg_t	i_crb_mark		  :	15;
		bdrkreg_t	i_rsvd_1		  :	 2;
		bdrkreg_t	i_precise		  :	 1;
		bdrkreg_t	i_rsvd			  :	11;
	} ii_icmr_fld_s;
} ii_icmr_u_t;

#else

typedef union ii_icmr_u {
	bdrkreg_t	ii_icmr_regval;
	struct	{
		bdrkreg_t	i_rsvd			  :	11;
		bdrkreg_t	i_precise		  :	 1;
		bdrkreg_t	i_rsvd_1		  :	 2;
		bdrkreg_t	i_crb_mark		  :	15;
		bdrkreg_t	i_crb_vld		  :	15;
		bdrkreg_t	i_fc_cnt		  :	 4;
		bdrkreg_t	i_rsvd_2		  :	 2;
		bdrkreg_t	i_clr_rppd		  :	 1;
		bdrkreg_t	i_clr_rqpd		  :	 1;
		bdrkreg_t	i_rsvd_3		  :	 4;
		bdrkreg_t	i_c_cnt			  :	 4;
		bdrkreg_t	i_rsvd_4		  :	 2;
		bdrkreg_t	i_rd_hdr		  :	 1;
		bdrkreg_t	i_sp_msg		  :	 1;
	} ii_icmr_fld_s;
} ii_icmr_u_t;

#endif




/************************************************************************
 *                                                                      *
 *  This register allows control of the table portion of the CRB        *
 * logic via software. Control operations from this register have       *
 * priority over all incoming Crosstalk or BTE requests.                *
 *                                                                      *
 ************************************************************************/




#ifdef LITTLE_ENDIAN

typedef union ii_iccr_u {
	bdrkreg_t	ii_iccr_regval;
	struct  {
		bdrkreg_t	i_crb_num                 :	 4;
		bdrkreg_t	i_rsvd_1		  :	 4;
		bdrkreg_t	i_cmd			  :	 8;
		bdrkreg_t	i_pending		  :	 1;
		bdrkreg_t	i_rsvd			  :	47;
	} ii_iccr_fld_s;
} ii_iccr_u_t;

#else

typedef union ii_iccr_u {
	bdrkreg_t	ii_iccr_regval;
	struct	{
		bdrkreg_t	i_rsvd			  :	47;
		bdrkreg_t	i_pending		  :	 1;
		bdrkreg_t	i_cmd			  :	 8;
		bdrkreg_t	i_rsvd_1		  :	 4;
		bdrkreg_t	i_crb_num		  :	 4;
	} ii_iccr_fld_s;
} ii_iccr_u_t;

#endif




/************************************************************************
 *                                                                      *
 *  This register allows the maximum timeout value to be programmed.    *
 *                                                                      *
 ************************************************************************/




#ifdef LITTLE_ENDIAN

typedef union ii_icto_u {
	bdrkreg_t	ii_icto_regval;
	struct  {
		bdrkreg_t	i_timeout                 :	 8;
		bdrkreg_t	i_rsvd			  :	56;
	} ii_icto_fld_s;
} ii_icto_u_t;

#else

typedef union ii_icto_u {
	bdrkreg_t	ii_icto_regval;
	struct	{
		bdrkreg_t	i_rsvd			  :	56;
		bdrkreg_t	i_timeout		  :	 8;
	} ii_icto_fld_s;
} ii_icto_u_t;

#endif




/************************************************************************
 *                                                                      *
 *  This register allows the timeout prescalar to be programmed. An     *
 * internal counter is associated with this register. When the          *
 * internal counter reaches the value of the PRESCALE field, the        *
 * timer registers in all valid CRBs are incremented (CRBx_D[TIMEOUT]   *
 * field). The internal counter resets to zero, and then continues      *
 * counting.                                                            *
 *                                                                      *
 ************************************************************************/




#ifdef LITTLE_ENDIAN

typedef union ii_ictp_u {
	bdrkreg_t	ii_ictp_regval;
	struct  {
		bdrkreg_t	i_prescale                :	24;
		bdrkreg_t	i_rsvd			  :	40;
	} ii_ictp_fld_s;
} ii_ictp_u_t;

#else

typedef union ii_ictp_u {
	bdrkreg_t	ii_ictp_regval;
	struct	{
		bdrkreg_t	i_rsvd			  :	40;
		bdrkreg_t	i_prescale		  :	24;
	} ii_ictp_fld_s;
} ii_ictp_u_t;

#endif




/************************************************************************
 *                                                                      *
 * Description:  There are 15 CRB Entries (ICRB0 to ICRBE) that are     *
 * used for Crosstalk operations (both cacheline and partial            *
 * operations) or BTE/IO. Because the CRB entries are very wide, four   *
 * registers (_A to _D) are required to read and write each entry.      *
 * The CRB Entry registers can be conceptualized as rows and columns    *
 * (illustrated in the table above). Each row contains the 4            *
 * registers required for a single CRB Entry. The first doubleword      *
 * (column) for each entry is labeled A, and the second doubleword      *
 * (higher address) is labeled B, the third doubleword is labeled C,    *
 * and the fourth doubleword is labeled D. All CRB entries have their   *
 * addresses on a quarter cacheline aligned boundary.                   *
 * Upon reset, only the following fields are initialized: valid         *
 * (VLD), priority count, timeout, timeout valid, and context valid.    *
 * All other bits should be cleared by software before use (after       *
 * recovering any potential error state from before the reset).         *
 * The following four tables summarize the format for the four          *
 * registers that are used for each ICRB# Entry.                        *
 *                                                                      *
 ************************************************************************/




#ifdef LITTLE_ENDIAN

typedef union ii_icrb0_a_u {
	bdrkreg_t	ii_icrb0_a_regval;
	struct  {
		bdrkreg_t	ia_iow                    :	 1;
		bdrkreg_t	ia_vld			  :	 1;
		bdrkreg_t	ia_addr			  :	38;
		bdrkreg_t	ia_tnum			  :	 5;
		bdrkreg_t	ia_sidn			  :	 4;
		bdrkreg_t	ia_xt_err		  :	 1;
		bdrkreg_t	ia_mark			  :	 1;
		bdrkreg_t	ia_ln_uce		  :	 1;
		bdrkreg_t	ia_errcode		  :	 3;
		bdrkreg_t	ia_error		  :	 1;
		bdrkreg_t	ia_stall__bte_1		  :	 1;
		bdrkreg_t	ia_stall__bte_0		  :	 1;
		bdrkreg_t       ia_rsvd                   :      6;
	} ii_icrb0_a_fld_s;
} ii_icrb0_a_u_t;

#else

typedef union ii_icrb0_a_u {
	bdrkreg_t	ii_icrb0_a_regval;
	struct	{
		bdrkreg_t	ia_rsvd			  :	 6;
		bdrkreg_t	ia_stall__bte_0		  :	 1;
		bdrkreg_t	ia_stall__bte_1		  :	 1;
		bdrkreg_t	ia_error		  :	 1;
		bdrkreg_t	ia_errcode		  :	 3;
		bdrkreg_t	ia_ln_uce		  :	 1;
		bdrkreg_t	ia_mark			  :	 1;
		bdrkreg_t	ia_xt_err		  :	 1;
		bdrkreg_t	ia_sidn			  :	 4;
		bdrkreg_t	ia_tnum			  :	 5;
		bdrkreg_t	ia_addr			  :	38;
		bdrkreg_t	ia_vld			  :	 1;
		bdrkreg_t	ia_iow			  :	 1;
	} ii_icrb0_a_fld_s;
} ii_icrb0_a_u_t;

#endif




/************************************************************************
 *                                                                      *
 * Description:  There are 15 CRB Entries (ICRB0 to ICRBE) that are     *
 * used for Crosstalk operations (both cacheline and partial            *
 * operations) or BTE/IO. Because the CRB entries are very wide, four   *
 * registers (_A to _D) are required to read and write each entry.      *
 *                                                                      *
 ************************************************************************/





#ifdef LITTLE_ENDIAN

typedef union ii_icrb0_b_u {
	bdrkreg_t	ii_icrb0_b_regval;
	struct	{
		bdrkreg_t	ib_stall__intr		  :	 1;
		bdrkreg_t	ib_stall_ib		  :	 1;
		bdrkreg_t	ib_intvn		  :	 1;
		bdrkreg_t	ib_wb			  :	 1;
		bdrkreg_t	ib_hold			  :	 1;
		bdrkreg_t	ib_ack			  :	 1;
		bdrkreg_t	ib_resp			  :	 1;
		bdrkreg_t	ib_ack_cnt		  :	11;
		bdrkreg_t	ib_rsvd_1		  :	 7;
		bdrkreg_t	ib_exc			  :	 5;
		bdrkreg_t	ib_init			  :	 3;
		bdrkreg_t	ib_imsg			  :	 8;
		bdrkreg_t	ib_imsgtype		  :	 2;
		bdrkreg_t	ib_use_old		  :	 1;
		bdrkreg_t	ib_source		  :	12;
		bdrkreg_t	ib_size			  :	 2;
		bdrkreg_t	ib_ct			  :	 1;
		bdrkreg_t	ib_bte_num		  :	 1;
		bdrkreg_t	ib_rsvd			  :	 4;
	} ii_icrb0_b_fld_s;
} ii_icrb0_b_u_t;

#else

typedef union ii_icrb0_b_u {
	bdrkreg_t	ii_icrb0_b_regval;
	struct  {
		bdrkreg_t	ib_rsvd                   :	 4;
		bdrkreg_t	ib_bte_num                :	 1;
		bdrkreg_t	ib_ct                     :	 1;
		bdrkreg_t	ib_size                   :	 2;
		bdrkreg_t	ib_source                 :	12;
		bdrkreg_t	ib_use_old                :	 1;
		bdrkreg_t	ib_imsgtype               :	 2;
		bdrkreg_t	ib_imsg                   :	 8;
		bdrkreg_t	ib_init                   :	 3;
		bdrkreg_t	ib_exc                    :	 5;
		bdrkreg_t	ib_rsvd_1                 :	 7;
		bdrkreg_t	ib_ack_cnt                :	11;
		bdrkreg_t	ib_resp                   :	 1;
		bdrkreg_t	ib_ack                    :	 1;
		bdrkreg_t	ib_hold                   :	 1;
		bdrkreg_t	ib_wb                     :	 1;
		bdrkreg_t	ib_intvn                  :	 1;
		bdrkreg_t	ib_stall_ib               :	 1;
		bdrkreg_t	ib_stall__intr            :	 1;
	} ii_icrb0_b_fld_s;
} ii_icrb0_b_u_t;

#endif




/************************************************************************
 *                                                                      *
 * Description:  There are 15 CRB Entries (ICRB0 to ICRBE) that are     *
 * used for Crosstalk operations (both cacheline and partial            *
 * operations) or BTE/IO. Because the CRB entries are very wide, four   *
 * registers (_A to _D) are required to read and write each entry.      *
 *                                                                      *
 ************************************************************************/




#ifdef LITTLE_ENDIAN

typedef union ii_icrb0_c_u {
	bdrkreg_t	ii_icrb0_c_regval;
	struct	{
		bdrkreg_t	ic_gbr			  :	 1;
		bdrkreg_t	ic_resprqd		  :	 1;
		bdrkreg_t	ic_bo			  :	 1;
		bdrkreg_t	ic_suppl		  :	12;
		bdrkreg_t	ic_pa_be		  :	34;
		bdrkreg_t	ic_bte_op		  :	 1;
		bdrkreg_t	ic_pr_psc		  :	 4;
		bdrkreg_t	ic_pr_cnt		  :	 4;
		bdrkreg_t	ic_sleep		  :	 1;
		bdrkreg_t	ic_rsvd			  :	 5;
	} ii_icrb0_c_fld_s;
} ii_icrb0_c_u_t;

#else

typedef union ii_icrb0_c_u {
	bdrkreg_t	ii_icrb0_c_regval;
	struct  {
		bdrkreg_t	ic_rsvd                   :	 5;
		bdrkreg_t	ic_sleep                  :	 1;
		bdrkreg_t	ic_pr_cnt                 :	 4;
		bdrkreg_t	ic_pr_psc                 :	 4;
		bdrkreg_t	ic_bte_op                 :	 1;
		bdrkreg_t	ic_pa_be                  :	34;
		bdrkreg_t	ic_suppl                  :	12;
		bdrkreg_t	ic_bo                     :	 1;
		bdrkreg_t	ic_resprqd                :	 1;
		bdrkreg_t	ic_gbr                    :	 1;
	} ii_icrb0_c_fld_s;
} ii_icrb0_c_u_t;

#endif



/************************************************************************
 *                                                                      *
 * Description:  There are 15 CRB Entries (ICRB0 to ICRBE) that are     *
 * used for Crosstalk operations (both cacheline and partial            *
 * operations) or BTE/IO. Because the CRB entries are very wide, four   *
 * registers (_A to _D) are required to read and write each entry.      *
 *                                                                      *
 ************************************************************************/




#ifdef LITTLE_ENDIAN

typedef union ii_icrb0_d_u {
	bdrkreg_t	ii_icrb0_d_regval;
	struct  {
		bdrkreg_t	id_timeout                :	 8;
		bdrkreg_t	id_context		  :	15;
		bdrkreg_t	id_rsvd_1		  :	 1;
		bdrkreg_t	id_tvld			  :	 1;
		bdrkreg_t	id_cvld			  :	 1;
		bdrkreg_t	id_rsvd			  :	38;
	} ii_icrb0_d_fld_s;
} ii_icrb0_d_u_t;

#else

typedef union ii_icrb0_d_u {
	bdrkreg_t	ii_icrb0_d_regval;
	struct	{
		bdrkreg_t	id_rsvd			  :	38;
		bdrkreg_t	id_cvld			  :	 1;
		bdrkreg_t	id_tvld			  :	 1;
		bdrkreg_t	id_rsvd_1		  :	 1;
		bdrkreg_t	id_context		  :	15;
		bdrkreg_t	id_timeout		  :	 8;
	} ii_icrb0_d_fld_s;
} ii_icrb0_d_u_t;

#endif




/************************************************************************
 *                                                                      *
 *  This register contains the lower 64 bits of the header of the       *
 * spurious message captured by II. Valid when the SP_MSG bit in ICMR   *
 * register is set.                                                     *
 *                                                                      *
 ************************************************************************/




#ifdef LITTLE_ENDIAN

typedef union ii_icsml_u {
	bdrkreg_t	ii_icsml_regval;
	struct  {
		bdrkreg_t	i_tt_addr                 :	38;
		bdrkreg_t	i_tt_ack_cnt		  :	11;
		bdrkreg_t	i_newsuppl_ex		  :	11;
		bdrkreg_t	i_reserved		  :	 3;
		bdrkreg_t       i_overflow                :      1;
	} ii_icsml_fld_s;
} ii_icsml_u_t;

#else

typedef union ii_icsml_u {
	bdrkreg_t	ii_icsml_regval;
	struct	{
		bdrkreg_t	i_overflow		  :	 1;
		bdrkreg_t	i_reserved		  :	 3;
		bdrkreg_t	i_newsuppl_ex		  :	11;
		bdrkreg_t	i_tt_ack_cnt		  :	11;
		bdrkreg_t	i_tt_addr		  :	38;
	} ii_icsml_fld_s;
} ii_icsml_u_t;

#endif




/************************************************************************
 *                                                                      *
 *  This register contains the microscopic state, all the inputs to     *
 * the protocol table, captured with the spurious message. Valid when   *
 * the SP_MSG bit in the ICMR register is set.                          *
 *                                                                      *
 ************************************************************************/




#ifdef LITTLE_ENDIAN

typedef union ii_icsmh_u {
	bdrkreg_t	ii_icsmh_regval;
	struct  {
		bdrkreg_t	i_tt_vld                  :	 1;
		bdrkreg_t	i_xerr			  :	 1;
		bdrkreg_t	i_ft_cwact_o		  :	 1;
		bdrkreg_t	i_ft_wact_o		  :	 1;
		bdrkreg_t       i_ft_active_o             :      1;
		bdrkreg_t	i_sync			  :	 1;
		bdrkreg_t	i_mnusg			  :	 1;
		bdrkreg_t	i_mnusz			  :	 1;
		bdrkreg_t	i_plusz			  :	 1;
		bdrkreg_t	i_plusg			  :	 1;
		bdrkreg_t	i_tt_exc		  :	 5;
		bdrkreg_t	i_tt_wb			  :	 1;
		bdrkreg_t	i_tt_hold		  :	 1;
		bdrkreg_t	i_tt_ack		  :	 1;
		bdrkreg_t	i_tt_resp		  :	 1;
		bdrkreg_t	i_tt_intvn		  :	 1;
		bdrkreg_t	i_g_stall_bte1		  :	 1;
		bdrkreg_t	i_g_stall_bte0		  :	 1;
		bdrkreg_t	i_g_stall_il		  :	 1;
		bdrkreg_t	i_g_stall_ib		  :	 1;
		bdrkreg_t	i_tt_imsg		  :	 8;
		bdrkreg_t	i_tt_imsgtype		  :	 2;
		bdrkreg_t	i_tt_use_old		  :	 1;
		bdrkreg_t	i_tt_respreqd		  :	 1;
		bdrkreg_t	i_tt_bte_num		  :	 1;
		bdrkreg_t	i_cbn			  :	 1;
		bdrkreg_t	i_match			  :	 1;
		bdrkreg_t	i_rpcnt_lt_34		  :	 1;
		bdrkreg_t	i_rpcnt_ge_34		  :	 1;
		bdrkreg_t	i_rpcnt_lt_18		  :	 1;
		bdrkreg_t	i_rpcnt_ge_18		  :	 1;
		bdrkreg_t       i_rpcnt_lt_2              :      1;
		bdrkreg_t	i_rpcnt_ge_2		  :	 1;
		bdrkreg_t	i_rqcnt_lt_18		  :	 1;
		bdrkreg_t	i_rqcnt_ge_18		  :	 1;
		bdrkreg_t	i_rqcnt_lt_2		  :	 1;
		bdrkreg_t	i_rqcnt_ge_2		  :	 1;
		bdrkreg_t	i_tt_device		  :	 7;
		bdrkreg_t	i_tt_init		  :	 3;
		bdrkreg_t	i_reserved		  :	 5;
	} ii_icsmh_fld_s;
} ii_icsmh_u_t;

#else

typedef union ii_icsmh_u {
	bdrkreg_t	ii_icsmh_regval;
	struct	{
		bdrkreg_t	i_reserved		  :	 5;
		bdrkreg_t	i_tt_init		  :	 3;
		bdrkreg_t	i_tt_device		  :	 7;
		bdrkreg_t	i_rqcnt_ge_2		  :	 1;
		bdrkreg_t	i_rqcnt_lt_2		  :	 1;
		bdrkreg_t	i_rqcnt_ge_18		  :	 1;
		bdrkreg_t	i_rqcnt_lt_18		  :	 1;
		bdrkreg_t	i_rpcnt_ge_2		  :	 1;
		bdrkreg_t	i_rpcnt_lt_2		  :	 1;
		bdrkreg_t	i_rpcnt_ge_18		  :	 1;
		bdrkreg_t	i_rpcnt_lt_18		  :	 1;
		bdrkreg_t	i_rpcnt_ge_34		  :	 1;
		bdrkreg_t	i_rpcnt_lt_34		  :	 1;
		bdrkreg_t	i_match			  :	 1;
		bdrkreg_t	i_cbn			  :	 1;
		bdrkreg_t	i_tt_bte_num		  :	 1;
		bdrkreg_t	i_tt_respreqd		  :	 1;
		bdrkreg_t	i_tt_use_old		  :	 1;
		bdrkreg_t	i_tt_imsgtype		  :	 2;
		bdrkreg_t	i_tt_imsg		  :	 8;
		bdrkreg_t	i_g_stall_ib		  :	 1;
		bdrkreg_t	i_g_stall_il		  :	 1;
		bdrkreg_t	i_g_stall_bte0		  :	 1;
		bdrkreg_t	i_g_stall_bte1		  :	 1;
		bdrkreg_t	i_tt_intvn		  :	 1;
		bdrkreg_t	i_tt_resp		  :	 1;
		bdrkreg_t	i_tt_ack		  :	 1;
		bdrkreg_t	i_tt_hold		  :	 1;
		bdrkreg_t	i_tt_wb			  :	 1;
		bdrkreg_t	i_tt_exc		  :	 5;
		bdrkreg_t	i_plusg			  :	 1;
		bdrkreg_t	i_plusz			  :	 1;
		bdrkreg_t	i_mnusz			  :	 1;
		bdrkreg_t	i_mnusg			  :	 1;
		bdrkreg_t	i_sync			  :	 1;
		bdrkreg_t	i_ft_active_o		  :	 1;
		bdrkreg_t	i_ft_wact_o		  :	 1;
		bdrkreg_t	i_ft_cwact_o		  :	 1;
		bdrkreg_t	i_xerr			  :	 1;
		bdrkreg_t	i_tt_vld		  :	 1;
	} ii_icsmh_fld_s;
} ii_icsmh_u_t;

#endif


/************************************************************************
 *                                                                      *
 *  The Bedrock DEBUG unit provides a 3-bit selection signal to the     *
 * II unit, thus allowing a choice of one set of debug signal outputs   *
 * from a menu of 8 options. Each option is limited to 32 bits in       *
 * size. There are more signals of interest than can be accommodated    *
 * in this 8*32 framework, so the IDBSS register has been defined to    *
 * extend the range of choices available. For each menu option          *
 * available to the DEBUG unit, the II provides a "submenu" of          *
 * several options. The value of the SUBMENU field in the IDBSS         *
 * register selects the desired submenu. Hence, the particular debug    *
 * signals provided by the II are determined by the 3-bit selection     *
 * signal from the DEBUG unit and the value of the SUBMENU field        *
 * within the IDBSS register. For a detailed description of the         *
 * available menus and submenus for II debug signals, refer to the      *
 * documentation in ii_interface.doc..                                  *
 *                                                                      *
 ************************************************************************/




#ifdef LIITLE_ENDIAN

typedef union ii_idbss_u {
	bdrkreg_t	ii_idbss_regval;
	struct  {
		bdrkreg_t	i_submenu                 :	 3;
		bdrkreg_t	i_rsvd			  :	61;
	} ii_idbss_fld_s;
} ii_idbss_u_t;

#else

typedef union ii_idbss_u {
	bdrkreg_t	ii_idbss_regval;
	struct	{
		bdrkreg_t	i_rsvd			  :	61;
		bdrkreg_t	i_submenu		  :	 3;
	} ii_idbss_fld_s;
} ii_idbss_u_t;

#endif




/************************************************************************
 *                                                                      *
 * Description:  This register is used to set up the length for a       *
 * transfer and then to monitor the progress of that transfer. This     *
 * register needs to be initialized before a transfer is started. A     *
 * legitimate write to this register will set the Busy bit, clear the   *
 * Error bit, and initialize the length to the value desired.           *
 * While the transfer is in progress, hardware will decrement the       *
 * length field with each successful block that is copied. Once the     *
 * transfer completes, hardware will clear the Busy bit. The length     *
 * field will also contain the number of cache lines left to be         *
 * transferred.                                                         *
 *                                                                      *
 ************************************************************************/




#ifdef LIITLE_ENDIAN

typedef union ii_ibls0_u {
	bdrkreg_t	ii_ibls0_regval;
	struct	{
		bdrkreg_t	i_length		  :	16;
		bdrkreg_t	i_error			  :	 1;
		bdrkreg_t	i_rsvd_1		  :	 3;
		bdrkreg_t	i_busy			  :	 1;
		bdrkreg_t       i_rsvd                    :     43;
	} ii_ibls0_fld_s;
} ii_ibls0_u_t;

#else

typedef union ii_ibls0_u {
	bdrkreg_t	ii_ibls0_regval;
	struct  {
		bdrkreg_t	i_rsvd                    :	43;
		bdrkreg_t	i_busy                    :	 1;
		bdrkreg_t	i_rsvd_1                  :	 3;
		bdrkreg_t	i_error                   :	 1;
		bdrkreg_t	i_length                  :	16;
	} ii_ibls0_fld_s;
} ii_ibls0_u_t;

#endif




/************************************************************************
 *                                                                      *
 *  This register should be loaded before a transfer is started. The    *
 * address to be loaded in bits 39:0 is the 40-bit TRex+ physical       *
 * address as described in Section 1.3, Figure2 and Figure3. Since      *
 * the bottom 7 bits of the address are always taken to be zero, BTE    *
 * transfers are always cacheline-aligned.                              *
 *                                                                      *
 ************************************************************************/




#ifdef LITTLE_ENDIAN

typedef union ii_ibsa0_u {
	bdrkreg_t	ii_ibsa0_regval;
	struct  {
		bdrkreg_t	i_rsvd_1                  :	 7;
		bdrkreg_t	i_addr			  :	33;
		bdrkreg_t       i_rsvd                    :     24;
	} ii_ibsa0_fld_s;
} ii_ibsa0_u_t;

#else

typedef union ii_ibsa0_u {
	bdrkreg_t	ii_ibsa0_regval;
	struct	{
		bdrkreg_t	i_rsvd			  :	24;
		bdrkreg_t	i_addr			  :	33;
		bdrkreg_t	i_rsvd_1		  :	 7;
	} ii_ibsa0_fld_s;
} ii_ibsa0_u_t;

#endif




/************************************************************************
 *                                                                      *
 *  This register should be loaded before a transfer is started. The    *
 * address to be loaded in bits 39:0 is the 40-bit TRex+ physical       *
 * address as described in Section 1.3, Figure2 and Figure3. Since      *
 * the bottom 7 bits of the address are always taken to be zero, BTE    *
 * transfers are always cacheline-aligned.                              *
 *                                                                      *
 ************************************************************************/




#ifdef LITTLE_ENDIAN

typedef union ii_ibda0_u {
	bdrkreg_t	ii_ibda0_regval;
	struct  {
		bdrkreg_t	i_rsvd_1                  :	 7;
		bdrkreg_t	i_addr			  :	33;
		bdrkreg_t	i_rsvd			  :	24;
	} ii_ibda0_fld_s;
} ii_ibda0_u_t;

#else

typedef union ii_ibda0_u {
	bdrkreg_t	ii_ibda0_regval;
	struct	{
		bdrkreg_t	i_rsvd			  :	24;
		bdrkreg_t	i_addr			  :	33;
		bdrkreg_t	i_rsvd_1		  :	 7;
	} ii_ibda0_fld_s;
} ii_ibda0_u_t;

#endif




/************************************************************************
 *                                                                      *
 *  Writing to this register sets up the attributes of the transfer     *
 * and initiates the transfer operation. Reading this register has      *
 * the side effect of terminating any transfer in progress. Note:       *
 * stopping a transfer midstream could have an adverse impact on the    *
 * other BTE. If a BTE stream has to be stopped (due to error           *
 * handling for example), both BTE streams should be stopped and        *
 * their transfers discarded.                                           *
 *                                                                      *
 ************************************************************************/




#ifdef LITTLE_ENDIAN

typedef union ii_ibct0_u {
	bdrkreg_t	ii_ibct0_regval;
	struct  {
		bdrkreg_t	i_zerofill                :	 1;
		bdrkreg_t	i_rsvd_2		  :	 3;
		bdrkreg_t	i_notify		  :	 1;
		bdrkreg_t	i_rsvd_1		  :	 3;
		bdrkreg_t       i_poison                  :      1;
		bdrkreg_t       i_rsvd                    :     55;
	} ii_ibct0_fld_s;
} ii_ibct0_u_t;

#else

typedef union ii_ibct0_u {
	bdrkreg_t	ii_ibct0_regval;
	struct	{
		bdrkreg_t	i_rsvd			  :	55;
		bdrkreg_t	i_poison		  :	 1;
		bdrkreg_t	i_rsvd_1		  :	 3;
		bdrkreg_t	i_notify		  :	 1;
		bdrkreg_t	i_rsvd_2		  :	 3;
		bdrkreg_t	i_zerofill		  :	 1;
	} ii_ibct0_fld_s;
} ii_ibct0_u_t;

#endif




/************************************************************************
 *                                                                      *
 *  This register contains the address to which the WINV is sent.       *
 * This address has to be cache line aligned.                           *
 *                                                                      *
 ************************************************************************/




#ifdef LITTLE_ENDIAN

typedef union ii_ibna0_u {
	bdrkreg_t	ii_ibna0_regval;
	struct  {
		bdrkreg_t	i_rsvd_1                  :	 7;
		bdrkreg_t	i_addr			  :	33;
		bdrkreg_t	i_rsvd			  :	24;
	} ii_ibna0_fld_s;
} ii_ibna0_u_t;

#else

typedef union ii_ibna0_u {
	bdrkreg_t	ii_ibna0_regval;
	struct	{
		bdrkreg_t	i_rsvd			  :	24;
		bdrkreg_t	i_addr			  :	33;
		bdrkreg_t	i_rsvd_1		  :	 7;
	} ii_ibna0_fld_s;
} ii_ibna0_u_t;

#endif




/************************************************************************
 *                                                                      *
 *  This register contains the programmable level as well as the node   *
 * ID and PI unit of the processor to which the interrupt will be       *
 * sent.                                                                *
 *                                                                      *
 ************************************************************************/




#ifdef LITTLE_ENDIAN

typedef union ii_ibia0_u {
	bdrkreg_t	ii_ibia0_regval;
	struct  {
		bdrkreg_t	i_pi_id                   :	 1;
		bdrkreg_t	i_node_id		  :	 8;
		bdrkreg_t	i_rsvd_1		  :	 7;
		bdrkreg_t	i_level			  :	 7;
		bdrkreg_t       i_rsvd                    :     41;
	} ii_ibia0_fld_s;
} ii_ibia0_u_t;

#else

typedef union ii_ibia0_u {
	bdrkreg_t	ii_ibia0_regval;
	struct	{
		bdrkreg_t	i_rsvd			  :	41;
		bdrkreg_t	i_level			  :	 7;
		bdrkreg_t	i_rsvd_1		  :	 7;
		bdrkreg_t	i_node_id		  :	 8;
		bdrkreg_t	i_pi_id			  :	 1;
	} ii_ibia0_fld_s;
} ii_ibia0_u_t;

#endif




/************************************************************************
 *                                                                      *
 * Description:  This register is used to set up the length for a       *
 * transfer and then to monitor the progress of that transfer. This     *
 * register needs to be initialized before a transfer is started. A     *
 * legitimate write to this register will set the Busy bit, clear the   *
 * Error bit, and initialize the length to the value desired.           *
 * While the transfer is in progress, hardware will decrement the       *
 * length field with each successful block that is copied. Once the     *
 * transfer completes, hardware will clear the Busy bit. The length     *
 * field will also contain the number of cache lines left to be         *
 * transferred.                                                         *
 *                                                                      *
 ************************************************************************/




#ifdef LITTLE_ENDIAN

typedef union ii_ibls1_u {
	bdrkreg_t	ii_ibls1_regval;
	struct  {
		bdrkreg_t	i_length                  :	16;
		bdrkreg_t	i_error			  :	 1;
		bdrkreg_t	i_rsvd_1		  :	 3;
		bdrkreg_t	i_busy			  :	 1;
		bdrkreg_t       i_rsvd                    :     43;
	} ii_ibls1_fld_s;
} ii_ibls1_u_t;

#else

typedef union ii_ibls1_u {
	bdrkreg_t	ii_ibls1_regval;
	struct	{
		bdrkreg_t	i_rsvd			  :	43;
		bdrkreg_t	i_busy			  :	 1;
		bdrkreg_t	i_rsvd_1		  :	 3;
		bdrkreg_t	i_error			  :	 1;
		bdrkreg_t	i_length		  :	16;
	} ii_ibls1_fld_s;
} ii_ibls1_u_t;

#endif




/************************************************************************
 *                                                                      *
 *  This register should be loaded before a transfer is started. The    *
 * address to be loaded in bits 39:0 is the 40-bit TRex+ physical       *
 * address as described in Section 1.3, Figure2 and Figure3. Since      *
 * the bottom 7 bits of the address are always taken to be zero, BTE    *
 * transfers are always cacheline-aligned.                              *
 *                                                                      *
 ************************************************************************/




#ifdef LITTLE_ENDIAN

typedef union ii_ibsa1_u {
	bdrkreg_t	ii_ibsa1_regval;
	struct  {
		bdrkreg_t	i_rsvd_1                  :	 7;
		bdrkreg_t	i_addr			  :	33;
		bdrkreg_t	i_rsvd			  :	24;
	} ii_ibsa1_fld_s;
} ii_ibsa1_u_t;

#else

typedef union ii_ibsa1_u {
	bdrkreg_t	ii_ibsa1_regval;
	struct	{
		bdrkreg_t	i_rsvd			  :	24;
		bdrkreg_t	i_addr			  :	33;
		bdrkreg_t	i_rsvd_1		  :	 7;
	} ii_ibsa1_fld_s;
} ii_ibsa1_u_t;

#endif




/************************************************************************
 *                                                                      *
 *  This register should be loaded before a transfer is started. The    *
 * address to be loaded in bits 39:0 is the 40-bit TRex+ physical       *
 * address as described in Section 1.3, Figure2 and Figure3. Since      *
 * the bottom 7 bits of the address are always taken to be zero, BTE    *
 * transfers are always cacheline-aligned.                              *
 *                                                                      *
 ************************************************************************/




#ifdef LITTLE_ENDIAN

typedef union ii_ibda1_u {
	bdrkreg_t	ii_ibda1_regval;
	struct  {
		bdrkreg_t	i_rsvd_1                  :	 7;
		bdrkreg_t	i_addr			  :	33;
		bdrkreg_t	i_rsvd			  :	24;
	} ii_ibda1_fld_s;
} ii_ibda1_u_t;

#else

typedef union ii_ibda1_u {
	bdrkreg_t	ii_ibda1_regval;
	struct	{
		bdrkreg_t	i_rsvd			  :	24;
		bdrkreg_t	i_addr			  :	33;
		bdrkreg_t	i_rsvd_1		  :	 7;
	} ii_ibda1_fld_s;
} ii_ibda1_u_t;

#endif




/************************************************************************
 *                                                                      *
 *  Writing to this register sets up the attributes of the transfer     *
 * and initiates the transfer operation. Reading this register has      *
 * the side effect of terminating any transfer in progress. Note:       *
 * stopping a transfer midstream could have an adverse impact on the    *
 * other BTE. If a BTE stream has to be stopped (due to error           *
 * handling for example), both BTE streams should be stopped and        *
 * their transfers discarded.                                           *
 *                                                                      *
 ************************************************************************/




#ifdef LITTLE_ENDIAN

typedef union ii_ibct1_u {
	bdrkreg_t	ii_ibct1_regval;
	struct  {
		bdrkreg_t	i_zerofill                :	 1;
		bdrkreg_t	i_rsvd_2		  :	 3;
		bdrkreg_t	i_notify		  :	 1;
		bdrkreg_t	i_rsvd_1		  :	 3;
		bdrkreg_t	i_poison		  :	 1;
		bdrkreg_t	i_rsvd			  :	55;
	} ii_ibct1_fld_s;
} ii_ibct1_u_t;

#else

typedef union ii_ibct1_u {
	bdrkreg_t	ii_ibct1_regval;
	struct	{
		bdrkreg_t	i_rsvd			  :	55;
		bdrkreg_t	i_poison		  :	 1;
		bdrkreg_t	i_rsvd_1		  :	 3;
		bdrkreg_t	i_notify		  :	 1;
		bdrkreg_t	i_rsvd_2		  :	 3;
		bdrkreg_t	i_zerofill		  :	 1;
	} ii_ibct1_fld_s;
} ii_ibct1_u_t;

#endif




/************************************************************************
 *                                                                      *
 *  This register contains the address to which the WINV is sent.       *
 * This address has to be cache line aligned.                           *
 *                                                                      *
 ************************************************************************/




#ifdef LITTLE_ENDIAN

typedef union ii_ibna1_u {
	bdrkreg_t	ii_ibna1_regval;
	struct  {
		bdrkreg_t	i_rsvd_1                  :	 7;
		bdrkreg_t	i_addr			  :	33;
		bdrkreg_t       i_rsvd                    :     24;
	} ii_ibna1_fld_s;
} ii_ibna1_u_t;

#else

typedef union ii_ibna1_u {
	bdrkreg_t	ii_ibna1_regval;
	struct	{
		bdrkreg_t	i_rsvd			  :	24;
		bdrkreg_t	i_addr			  :	33;
		bdrkreg_t	i_rsvd_1		  :	 7;
	} ii_ibna1_fld_s;
} ii_ibna1_u_t;

#endif




/************************************************************************
 *                                                                      *
 *  This register contains the programmable level as well as the node   *
 * ID and PI unit of the processor to which the interrupt will be       *
 * sent.                                                                *
 *                                                                      *
 ************************************************************************/




#ifdef LITTLE_ENDIAN

typedef union ii_ibia1_u {
	bdrkreg_t	ii_ibia1_regval;
	struct  {
		bdrkreg_t	i_pi_id                   :	 1;
		bdrkreg_t	i_node_id		  :	 8;
		bdrkreg_t	i_rsvd_1		  :	 7;
		bdrkreg_t	i_level			  :	 7;
		bdrkreg_t	i_rsvd			  :	41;
	} ii_ibia1_fld_s;
} ii_ibia1_u_t;

#else

typedef union ii_ibia1_u {
	bdrkreg_t	ii_ibia1_regval;
	struct	{
		bdrkreg_t	i_rsvd			  :	41;
		bdrkreg_t	i_level			  :	 7;
		bdrkreg_t	i_rsvd_1		  :	 7;
		bdrkreg_t	i_node_id		  :	 8;
		bdrkreg_t	i_pi_id			  :	 1;
	} ii_ibia1_fld_s;
} ii_ibia1_u_t;

#endif




/************************************************************************
 *                                                                      *
 *  This register defines the resources that feed information into      *
 * the two performance counters located in the IO Performance           *
 * Profiling Register. There are 17 different quantities that can be    *
 * measured. Given these 17 different options, the two performance      *
 * counters have 15 of them in common; menu selections 0 through 0xE    *
 * are identical for each performance counter. As for the other two     *
 * options, one is available from one performance counter and the       *
 * other is available from the other performance counter. Hence, the    *
 * II supports all 17*16=272 possible combinations of quantities to     *
 * measure.                                                             *
 *                                                                      *
 ************************************************************************/




#ifdef LITTLE_ENDIAN

typedef union ii_ipcr_u {
	bdrkreg_t	ii_ipcr_regval;
	struct  {
		bdrkreg_t	i_ippr0_c                 :	 4;
		bdrkreg_t	i_ippr1_c		  :	 4;
		bdrkreg_t	i_icct			  :	 8;
		bdrkreg_t       i_rsvd                    :     48;
	} ii_ipcr_fld_s;
} ii_ipcr_u_t;

#else

typedef union ii_ipcr_u {
	bdrkreg_t	ii_ipcr_regval;
	struct	{
		bdrkreg_t	i_rsvd			  :	48;
		bdrkreg_t	i_icct			  :	 8;
		bdrkreg_t	i_ippr1_c		  :	 4;
		bdrkreg_t	i_ippr0_c		  :	 4;
	} ii_ipcr_fld_s;
} ii_ipcr_u_t;

#endif




/************************************************************************
 *                                                                      *
 *                                                                      *
 *                                                                      *
 ************************************************************************/




#ifdef LITTLE_ENDIAN

typedef union ii_ippr_u {
	bdrkreg_t	ii_ippr_regval;
	struct  {
		bdrkreg_t	i_ippr0                   :	32;
		bdrkreg_t	i_ippr1			  :	32;
	} ii_ippr_fld_s;
} ii_ippr_u_t;

#else

typedef union ii_ippr_u {
	bdrkreg_t	ii_ippr_regval;
	struct	{
		bdrkreg_t	i_ippr1			  :	32;
		bdrkreg_t	i_ippr0			  :	32;
	} ii_ippr_fld_s;
} ii_ippr_u_t;

#endif






#endif /* _LANGUAGE_C */

/************************************************************************
 *                                                                      *
 * The following defines which were not formed into structures are      *
 * probably indentical to another register, and the name of the         *
 * register is provided against each of these registers. This           *
 * information needs to be checked carefully                            *
 *                                                                      *
 *           IIO_ICRB1_A                IIO_ICRB0_A                       *
 *           IIO_ICRB1_B                IIO_ICRB0_B                       *
 *           IIO_ICRB1_C                IIO_ICRB0_C                       *
 *           IIO_ICRB1_D                IIO_ICRB0_D                       *
 *           IIO_ICRB2_A                IIO_ICRB0_A                       *
 *           IIO_ICRB2_B                IIO_ICRB0_B                       *
 *           IIO_ICRB2_C                IIO_ICRB0_C                       *
 *           IIO_ICRB2_D                IIO_ICRB0_D                       *
 *           IIO_ICRB3_A                IIO_ICRB0_A                       *
 *           IIO_ICRB3_B                IIO_ICRB0_B                       *
 *           IIO_ICRB3_C                IIO_ICRB0_C                       *
 *           IIO_ICRB3_D                IIO_ICRB0_D                       *
 *           IIO_ICRB4_A                IIO_ICRB0_A                       *
 *           IIO_ICRB4_B                IIO_ICRB0_B                       *
 *           IIO_ICRB4_C                IIO_ICRB0_C                       *
 *           IIO_ICRB4_D                IIO_ICRB0_D                       *
 *           IIO_ICRB5_A                IIO_ICRB0_A                       *
 *           IIO_ICRB5_B                IIO_ICRB0_B                       *
 *           IIO_ICRB5_C                IIO_ICRB0_C                       *
 *           IIO_ICRB5_D                IIO_ICRB0_D                       *
 *           IIO_ICRB6_A                IIO_ICRB0_A                       *
 *           IIO_ICRB6_B                IIO_ICRB0_B                       *
 *           IIO_ICRB6_C                IIO_ICRB0_C                       *
 *           IIO_ICRB6_D                IIO_ICRB0_D                       *
 *           IIO_ICRB7_A                IIO_ICRB0_A                       *
 *           IIO_ICRB7_B                IIO_ICRB0_B                       *
 *           IIO_ICRB7_C                IIO_ICRB0_C                       *
 *           IIO_ICRB7_D                IIO_ICRB0_D                       *
 *           IIO_ICRB8_A                IIO_ICRB0_A                       *
 *           IIO_ICRB8_B                IIO_ICRB0_B                       *
 *           IIO_ICRB8_C                IIO_ICRB0_C                       *
 *           IIO_ICRB8_D                IIO_ICRB0_D                       *
 *           IIO_ICRB9_A                IIO_ICRB0_A                       *
 *           IIO_ICRB9_B                IIO_ICRB0_B                       *
 *           IIO_ICRB9_C                IIO_ICRB0_C                       *
 *           IIO_ICRB9_D                IIO_ICRB0_D                       *
 *           IIO_ICRBA_A                IIO_ICRB0_A                       *
 *           IIO_ICRBA_B                IIO_ICRB0_B                       *
 *           IIO_ICRBA_C                IIO_ICRB0_C                       *
 *           IIO_ICRBA_D                IIO_ICRB0_D                       *
 *           IIO_ICRBB_A                IIO_ICRB0_A                       *
 *           IIO_ICRBB_B                IIO_ICRB0_B                       *
 *           IIO_ICRBB_C                IIO_ICRB0_C                       *
 *           IIO_ICRBB_D                IIO_ICRB0_D                       *
 *           IIO_ICRBC_A                IIO_ICRB0_A                       *
 *           IIO_ICRBC_B                IIO_ICRB0_B                       *
 *           IIO_ICRBC_C                IIO_ICRB0_C                       *
 *           IIO_ICRBC_D                IIO_ICRB0_D                       *
 *           IIO_ICRBD_A                IIO_ICRB0_A                       *
 *           IIO_ICRBD_B                IIO_ICRB0_B                       *
 *           IIO_ICRBD_C                IIO_ICRB0_C                       *
 *           IIO_ICRBD_D                IIO_ICRB0_D                       *
 *           IIO_ICRBE_A                IIO_ICRB0_A                       *
 *           IIO_ICRBE_B                IIO_ICRB0_B                       *
 *           IIO_ICRBE_C                IIO_ICRB0_C                       *
 *           IIO_ICRBE_D                IIO_ICRB0_D                       *
 *                                                                      *
 ************************************************************************/


/************************************************************************
 *                                                                      *
 *               MAKE ALL ADDITIONS AFTER THIS LINE                     *
 *                                                                      *
 ************************************************************************/





#endif /* _ASM_SN_SN1_HUBIO_H */
