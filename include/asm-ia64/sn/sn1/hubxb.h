/* $Id$
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1992 - 1997, 2000 Silicon Graphics, Inc.
 * Copyright (C) 2000 by Colin Ngam
 */
#ifndef _ASM_SN_SN1_HUBXB_H
#define _ASM_SN_SN1_HUBXB_H

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


#define    XB_PARMS                  0x00700000    /*
                                                    * Controls
                                                    * crossbar-wide
                                                    * parameters.
                                                    */



#define    XB_SLOW_GNT               0x00700008    /*
                                                    * Controls wavefront
                                                    * arbiter grant
                                                    * frequency, used to
                                                    * slow XB grants
                                                    */



#define    XB_SPEW_CONTROL           0x00700010    /*
                                                    * Controls spew
                                                    * settings (debug
                                                    * only).
                                                    */



#define    XB_IOQ_ARB_TRIGGER        0x00700018    /*
                                                    * Controls IOQ
                                                    * trigger level
                                                    */



#define    XB_FIRST_ERROR            0x00700090    /*
                                                    * Records the first
                                                    * crossbar error
                                                    * seen.
                                                    */



#define    XB_POQ0_ERROR             0x00700020    /*
                                                    * POQ0 error
                                                    * register.
                                                    */



#define    XB_PIQ0_ERROR             0x00700028    /*
                                                    * PIQ0 error
                                                    * register.
                                                    */



#define    XB_POQ1_ERROR             0x00700030    /*
                                                    * POQ1 error
                                                    * register.
                                                    */



#define    XB_PIQ1_ERROR             0x00700038    /*
                                                    * PIQ1 error
                                                    * register.
                                                    */



#define    XB_MP0_ERROR              0x00700040    /*
                                                    * MOQ for PI0 error
                                                    * register.
                                                    */



#define    XB_MP1_ERROR              0x00700048    /*
                                                    * MOQ for PI1 error
                                                    * register.
                                                    */



#define    XB_MMQ_ERROR              0x00700050    /*
                                                    * MOQ for misc. (LB,
                                                    * NI, II) error
                                                    * register.
                                                    */



#define    XB_MIQ_ERROR              0x00700058    /*
                                                    * MIQ error register,
                                                    * addtional MIQ
                                                    * errors are logged
                                                    * in MD &quot;Input
                                                    * Error
                                                    * Registers&quot;.
                                                    */



#define    XB_NOQ_ERROR              0x00700060    /* NOQ error register.    */



#define    XB_NIQ_ERROR              0x00700068    /* NIQ error register.    */



#define    XB_IOQ_ERROR              0x00700070    /* IOQ error register.    */



#define    XB_IIQ_ERROR              0x00700078    /* IIQ error register.    */



#define    XB_LOQ_ERROR              0x00700080    /* LOQ error register.    */



#define    XB_LIQ_ERROR              0x00700088    /* LIQ error register.    */



#define    XB_DEBUG_DATA_CTL         0x00700098    /*
                                                    * Debug Datapath
                                                    * Select
                                                    */



#define    XB_DEBUG_ARB_CTL          0x007000A0    /*
                                                    * XB master debug
                                                    * control
                                                    */



#define    XB_POQ0_ERROR_CLEAR       0x00700120    /*
                                                    * Clears
                                                    * XB_POQ0_ERROR
                                                    * register.
                                                    */



#define    XB_PIQ0_ERROR_CLEAR       0x00700128    /*
                                                    * Clears
                                                    * XB_PIQ0_ERROR
                                                    * register.
                                                    */



#define    XB_POQ1_ERROR_CLEAR       0x00700130    /*
                                                    * Clears
                                                    * XB_POQ1_ERROR
                                                    * register.
                                                    */



#define    XB_PIQ1_ERROR_CLEAR       0x00700138    /*
                                                    * Clears
                                                    * XB_PIQ1_ERROR
                                                    * register.
                                                    */



#define    XB_MP0_ERROR_CLEAR        0x00700140    /*
                                                    * Clears XB_MP0_ERROR
                                                    * register.
                                                    */



#define    XB_MP1_ERROR_CLEAR        0x00700148    /*
                                                    * Clears XB_MP1_ERROR
                                                    * register.
                                                    */



#define    XB_MMQ_ERROR_CLEAR        0x00700150    /*
                                                    * Clears XB_MMQ_ERROR
                                                    * register.
                                                    */



#define    XB_XM_MIQ_ERROR_CLEAR     0x00700158    /*
                                                    * Clears XB_MIQ_ERROR
                                                    * register
                                                    */



#define    XB_NOQ_ERROR_CLEAR        0x00700160    /*
                                                    * Clears XB_NOQ_ERROR
                                                    * register.
                                                    */



#define    XB_NIQ_ERROR_CLEAR        0x00700168    /*
                                                    * Clears XB_NIQ_ERROR
                                                    * register.
                                                    */



#define    XB_IOQ_ERROR_CLEAR        0x00700170    /*
                                                    * Clears XB_IOQ
                                                    * _ERROR register.
                                                    */



#define    XB_IIQ_ERROR_CLEAR        0x00700178    /*
                                                    * Clears XB_IIQ
                                                    * _ERROR register.
                                                    */



#define    XB_LOQ_ERROR_CLEAR        0x00700180    /*
                                                    * Clears XB_LOQ_ERROR
                                                    * register.
                                                    */



#define    XB_LIQ_ERROR_CLEAR        0x00700188    /*
                                                    * Clears XB_LIQ_ERROR
                                                    * register.
                                                    */



#define    XB_FIRST_ERROR_CLEAR      0x00700190    /*
                                                    * Clears
                                                    * XB_FIRST_ERROR
                                                    * register
                                                    */





#ifdef _LANGUAGE_C

/************************************************************************
 *                                                                      *
 *  Access to parameters which control various aspects of the           *
 * crossbar's operation.                                                *
 *                                                                      *
 ************************************************************************/




#ifdef LITTLE_ENDIAN

typedef union xb_parms_u {
	bdrkreg_t	xb_parms_regval;
	struct  {
		bdrkreg_t	p_byp_en                  :	 1;
                bdrkreg_t       p_rsrvd_1                 :      3;
                bdrkreg_t       p_age_wrap                :      8;
                bdrkreg_t       p_deadlock_to_wrap        :     20;
                bdrkreg_t       p_tail_to_wrap            :     20;
                bdrkreg_t       p_rsrvd                   :     12;
	} xb_parms_fld_s;
} xb_parms_u_t;

#else

typedef union xb_parms_u {
	bdrkreg_t	xb_parms_regval;
	struct	{
		bdrkreg_t	p_rsrvd			  :	12;
		bdrkreg_t	p_tail_to_wrap		  :	20;
		bdrkreg_t	p_deadlock_to_wrap	  :	20;
		bdrkreg_t	p_age_wrap		  :	 8;
		bdrkreg_t	p_rsrvd_1		  :	 3;
		bdrkreg_t	p_byp_en		  :	 1;
	} xb_parms_fld_s;
} xb_parms_u_t;

#endif




/************************************************************************
 *                                                                      *
 *  Sets the period of wavefront grants given to each unit. The         *
 * register's value corresponds to the number of cycles between each    *
 * wavefront grant opportunity given to the requesting unit. If set     *
 * to 0xF, no grants are given to this unit. If set to 0xE, the unit    *
 * is granted at the slowest rate (sometimes called "molasses mode").   *
 * This feature can be used to apply backpressure to a unit's output    *
 * queue(s). The setting does not affect bypass grants.                 *
 *                                                                      *
 ************************************************************************/




#ifdef LITTLE_ENDIAN

typedef union xb_slow_gnt_u {
	bdrkreg_t	xb_slow_gnt_regval;
	struct  {
		bdrkreg_t	sg_lb_slow_gnt            :	 4;
                bdrkreg_t       sg_ii_slow_gnt            :      4;
                bdrkreg_t       sg_ni_slow_gnt            :      4;
                bdrkreg_t       sg_mmq_slow_gnt           :      4;
                bdrkreg_t       sg_mp1_slow_gnt           :      4;
                bdrkreg_t       sg_mp0_slow_gnt           :      4;
                bdrkreg_t       sg_pi1_slow_gnt           :      4;
                bdrkreg_t       sg_pi0_slow_gnt           :      4;
                bdrkreg_t       sg_rsrvd                  :     32;
	} xb_slow_gnt_fld_s;
} xb_slow_gnt_u_t;

#else

typedef union xb_slow_gnt_u {
	bdrkreg_t	xb_slow_gnt_regval;
	struct	{
		bdrkreg_t	sg_rsrvd		  :	32;
		bdrkreg_t	sg_pi0_slow_gnt		  :	 4;
		bdrkreg_t	sg_pi1_slow_gnt		  :	 4;
		bdrkreg_t	sg_mp0_slow_gnt		  :	 4;
		bdrkreg_t	sg_mp1_slow_gnt		  :	 4;
		bdrkreg_t	sg_mmq_slow_gnt		  :	 4;
		bdrkreg_t	sg_ni_slow_gnt		  :	 4;
		bdrkreg_t	sg_ii_slow_gnt		  :	 4;
		bdrkreg_t	sg_lb_slow_gnt		  :	 4;
	} xb_slow_gnt_fld_s;
} xb_slow_gnt_u_t;

#endif




/************************************************************************
 *                                                                      *
 *  Enables snooping of internal crossbar traffic by spewing all        *
 * traffic across a selected crossbar point to the PI1 port. Only one   *
 * bit should be set at any one time, and any bit set will preclude     *
 * using the P1 for anything but a debug connection.                    *
 *                                                                      *
 ************************************************************************/




#ifdef LITTLE_ENDIAN

typedef union xb_spew_control_u {
	bdrkreg_t	xb_spew_control_regval;
	struct  {
		bdrkreg_t	sc_snoop_liq              :	 1;
                bdrkreg_t       sc_snoop_iiq              :      1;
                bdrkreg_t       sc_snoop_niq              :      1;
                bdrkreg_t       sc_snoop_miq              :      1;
                bdrkreg_t       sc_snoop_piq0             :      1;
                bdrkreg_t       sc_snoop_loq              :      1;
                bdrkreg_t       sc_snoop_ioq              :      1;
                bdrkreg_t       sc_snoop_noq              :      1;
                bdrkreg_t       sc_snoop_mmq              :      1;
                bdrkreg_t       sc_snoop_mp0              :      1;
                bdrkreg_t       sc_snoop_poq0             :      1;
                bdrkreg_t       sc_rsrvd                  :     53;
	} xb_spew_control_fld_s;
} xb_spew_control_u_t;

#else

typedef union xb_spew_control_u {
	bdrkreg_t	xb_spew_control_regval;
	struct	{
		bdrkreg_t	sc_rsrvd		  :	53;
		bdrkreg_t	sc_snoop_poq0		  :	 1;
		bdrkreg_t	sc_snoop_mp0		  :	 1;
		bdrkreg_t	sc_snoop_mmq		  :	 1;
		bdrkreg_t	sc_snoop_noq		  :	 1;
		bdrkreg_t	sc_snoop_ioq		  :	 1;
		bdrkreg_t	sc_snoop_loq		  :	 1;
		bdrkreg_t	sc_snoop_piq0		  :	 1;
		bdrkreg_t	sc_snoop_miq		  :	 1;
		bdrkreg_t	sc_snoop_niq		  :	 1;
		bdrkreg_t	sc_snoop_iiq		  :	 1;
		bdrkreg_t	sc_snoop_liq		  :	 1;
	} xb_spew_control_fld_s;
} xb_spew_control_u_t;

#endif




/************************************************************************
 *                                                                      *
 *  Number of clocks the IOQ will wait before beginning XB              *
 * arbitration. This is set so that the slower IOQ data rate can        *
 * catch up up with the XB data rate in the IOQ buffer.                 *
 *                                                                      *
 ************************************************************************/




#ifdef LITTLE_ENDIAN

typedef union xb_ioq_arb_trigger_u {
	bdrkreg_t	xb_ioq_arb_trigger_regval;
	struct  {
		bdrkreg_t	iat_ioq_arb_trigger       :	 4;
	        bdrkreg_t       iat_rsrvd                 :     60;
	} xb_ioq_arb_trigger_fld_s;
} xb_ioq_arb_trigger_u_t;

#else

typedef union xb_ioq_arb_trigger_u {
	bdrkreg_t	xb_ioq_arb_trigger_regval;
	struct	{
		bdrkreg_t	iat_rsrvd		  :	60;
		bdrkreg_t	iat_ioq_arb_trigger	  :	 4;
	} xb_ioq_arb_trigger_fld_s;
} xb_ioq_arb_trigger_u_t;

#endif




/************************************************************************
 *                                                                      *
 *  Records errors seen by POQ0.Can be written to test software, will   *
 * cause an interrupt.                                                  *
 *                                                                      *
 ************************************************************************/




#ifdef LITTLE_ENDIAN

typedef union xb_poq0_error_u {
	bdrkreg_t	xb_poq0_error_regval;
	struct  {
		bdrkreg_t	pe_invalid_xsel           :	 2;
                bdrkreg_t       pe_rsrvd_3                :      2;
                bdrkreg_t       pe_overflow               :      2;
                bdrkreg_t       pe_rsrvd_2                :      2;
                bdrkreg_t       pe_underflow              :      2;
                bdrkreg_t       pe_rsrvd_1                :      2;
                bdrkreg_t       pe_tail_timeout           :      2;
                bdrkreg_t       pe_unused                 :      6;
                bdrkreg_t       pe_rsrvd                  :     44;
	} xb_poq0_error_fld_s;
} xb_poq0_error_u_t;

#else

typedef union xb_poq0_error_u {
	bdrkreg_t	xb_poq0_error_regval;
	struct	{
		bdrkreg_t	pe_rsrvd		  :	44;
		bdrkreg_t	pe_unused		  :	 6;
		bdrkreg_t	pe_tail_timeout		  :	 2;
		bdrkreg_t	pe_rsrvd_1		  :	 2;
		bdrkreg_t	pe_underflow		  :	 2;
		bdrkreg_t	pe_rsrvd_2		  :	 2;
		bdrkreg_t	pe_overflow		  :	 2;
		bdrkreg_t	pe_rsrvd_3		  :	 2;
		bdrkreg_t	pe_invalid_xsel		  :	 2;
	} xb_poq0_error_fld_s;
} xb_poq0_error_u_t;

#endif




/************************************************************************
 *                                                                      *
 *  Records errors seen by PIQ0. Note that the PIQ/PI interface         *
 * precludes PIQ underflow.                                             *
 *                                                                      *
 ************************************************************************/




#ifdef LITTLE_ENDIAN

typedef union xb_piq0_error_u {
	bdrkreg_t	xb_piq0_error_regval;
	struct  {
		bdrkreg_t	pe_overflow               :	 2;
                bdrkreg_t       pe_rsrvd_1                :      2;
                bdrkreg_t       pe_deadlock_timeout       :      2;
                bdrkreg_t       pe_rsrvd                  :     58;
	} xb_piq0_error_fld_s;
} xb_piq0_error_u_t;

#else

typedef union xb_piq0_error_u {
	bdrkreg_t	xb_piq0_error_regval;
	struct	{
		bdrkreg_t	pe_rsrvd		  :	58;
		bdrkreg_t	pe_deadlock_timeout	  :	 2;
		bdrkreg_t	pe_rsrvd_1		  :	 2;
		bdrkreg_t	pe_overflow		  :	 2;
	} xb_piq0_error_fld_s;
} xb_piq0_error_u_t;

#endif




/************************************************************************
 *                                                                      *
 *  Records errors seen by MP0 queue (the MOQ for processor 0). Since   *
 * the xselect is decoded on the MD/MOQ interface, no invalid xselect   *
 * errors are possible.                                                 *
 *                                                                      *
 ************************************************************************/




#ifdef LITTLE_ENDIAN

typedef union xb_mp0_error_u {
	bdrkreg_t	xb_mp0_error_regval;
	struct  {
		bdrkreg_t	me_rsrvd_3                :	 4;
                bdrkreg_t       me_overflow               :      2;
                bdrkreg_t       me_rsrvd_2                :      2;
                bdrkreg_t       me_underflow              :      2;
                bdrkreg_t       me_rsrvd_1                :      2;
                bdrkreg_t       me_tail_timeout           :      2;
                bdrkreg_t       me_rsrvd                  :     50;
	} xb_mp0_error_fld_s;
} xb_mp0_error_u_t;

#else

typedef union xb_mp0_error_u {
	bdrkreg_t	xb_mp0_error_regval;
	struct	{
		bdrkreg_t	me_rsrvd		  :	50;
		bdrkreg_t	me_tail_timeout		  :	 2;
		bdrkreg_t	me_rsrvd_1		  :	 2;
		bdrkreg_t	me_underflow		  :	 2;
		bdrkreg_t	me_rsrvd_2		  :	 2;
		bdrkreg_t	me_overflow		  :	 2;
		bdrkreg_t	me_rsrvd_3		  :	 4;
	} xb_mp0_error_fld_s;
} xb_mp0_error_u_t;

#endif




/************************************************************************
 *                                                                      *
 *  Records errors seen by MIQ.                                         *
 *                                                                      *
 ************************************************************************/



#ifdef LITTLE_ENDIAN

typedef union xb_miq_error_u {
	bdrkreg_t	xb_miq_error_regval;
	struct  {
		bdrkreg_t	me_rsrvd_1                :	 4;
                bdrkreg_t       me_deadlock_timeout       :      4;
                bdrkreg_t       me_rsrvd                  :     56;
	} xb_miq_error_fld_s;
} xb_miq_error_u_t;

#else

typedef union xb_miq_error_u {
	bdrkreg_t	xb_miq_error_regval;
	struct	{
		bdrkreg_t	me_rsrvd		  :	56;
		bdrkreg_t	me_deadlock_timeout	  :	 4;
		bdrkreg_t	me_rsrvd_1		  :	 4;
	} xb_miq_error_fld_s;
} xb_miq_error_u_t;

#endif




/************************************************************************
 *                                                                      *
 *  Records errors seen by NOQ.                                         *
 *                                                                      *
 ************************************************************************/




#ifdef LITTLE_ENDIAN

typedef union xb_noq_error_u {
	bdrkreg_t	xb_noq_error_regval;
	struct  {
		bdrkreg_t	ne_rsvd                   :	 4;
                bdrkreg_t       ne_overflow               :      4;
                bdrkreg_t       ne_underflow              :      4;
                bdrkreg_t       ne_tail_timeout           :      4;
                bdrkreg_t       ne_rsrvd                  :     48;
	} xb_noq_error_fld_s;
} xb_noq_error_u_t;

#else

typedef union xb_noq_error_u {
	bdrkreg_t	xb_noq_error_regval;
	struct	{
		bdrkreg_t	ne_rsrvd		  :	48;
		bdrkreg_t	ne_tail_timeout		  :	 4;
		bdrkreg_t	ne_underflow		  :	 4;
		bdrkreg_t	ne_overflow		  :	 4;
		bdrkreg_t	ne_rsvd			  :	 4;
	} xb_noq_error_fld_s;
} xb_noq_error_u_t;

#endif




/************************************************************************
 *                                                                      *
 *  Records errors seen by LOQ.                                         *
 *                                                                      *
 ************************************************************************/




#ifdef LITTLE_ENDIAN

typedef union xb_loq_error_u {
	bdrkreg_t	xb_loq_error_regval;
	struct  {
		bdrkreg_t	le_invalid_xsel           :	 2;
                bdrkreg_t       le_rsrvd_1                :      6;
                bdrkreg_t       le_underflow              :      2;
                bdrkreg_t       le_rsvd                   :      2;
                bdrkreg_t       le_tail_timeout           :      2;
                bdrkreg_t       le_rsrvd                  :     50;
	} xb_loq_error_fld_s;
} xb_loq_error_u_t;

#else

typedef union xb_loq_error_u {
	bdrkreg_t	xb_loq_error_regval;
	struct	{
		bdrkreg_t	le_rsrvd		  :	50;
		bdrkreg_t	le_tail_timeout		  :	 2;
		bdrkreg_t	le_rsvd			  :	 2;
		bdrkreg_t	le_underflow		  :	 2;
		bdrkreg_t	le_rsrvd_1		  :	 6;
		bdrkreg_t	le_invalid_xsel		  :	 2;
	} xb_loq_error_fld_s;
} xb_loq_error_u_t;

#endif




/************************************************************************
 *                                                                      *
 *  Records errors seen by LIQ. Note that the LIQ only records errors   *
 * for the request channel. The reply channel can never deadlock or     *
 * overflow because it does not have hardware flow control.             *
 *                                                                      *
 ************************************************************************/




#ifdef LITTLE_ENDIAN

typedef union xb_liq_error_u {
	bdrkreg_t	xb_liq_error_regval;
	struct  {
		bdrkreg_t	le_overflow               :	 1;
                bdrkreg_t       le_rsrvd_1                :      3;
                bdrkreg_t       le_deadlock_timeout       :      1;
                bdrkreg_t       le_rsrvd                  :     59;
	} xb_liq_error_fld_s;
} xb_liq_error_u_t;

#else

typedef union xb_liq_error_u {
	bdrkreg_t	xb_liq_error_regval;
	struct	{
		bdrkreg_t	le_rsrvd		  :	59;
		bdrkreg_t	le_deadlock_timeout	  :	 1;
		bdrkreg_t	le_rsrvd_1		  :	 3;
		bdrkreg_t	le_overflow		  :	 1;
	} xb_liq_error_fld_s;
} xb_liq_error_u_t;

#endif




/************************************************************************
 *                                                                      *
 *  First error is latched whenever the Valid bit is clear and an       *
 * error occurs. Any valid bit on in this register causes an            *
 * interrupt to PI0 and PI1. This interrupt bit will persist until      *
 * the specific error register to capture the error is cleared, then    *
 * the FIRST_ERROR register is cleared (in that oder.) The              *
 * FIRST_ERROR register is not writable, but will be set when any of    *
 * the corresponding error registers are written by software.           *
 *                                                                      *
 ************************************************************************/




#ifdef LITTLE_ENDIAN

typedef union xb_first_error_u {
	bdrkreg_t	xb_first_error_regval;
	struct  {
		bdrkreg_t	fe_type                   :	 4;
                bdrkreg_t       fe_channel                :      4;
                bdrkreg_t       fe_source                 :      4;
                bdrkreg_t       fe_valid                  :      1;
                bdrkreg_t       fe_rsrvd                  :     51;
	} xb_first_error_fld_s;
} xb_first_error_u_t;

#else

typedef union xb_first_error_u {
	bdrkreg_t	xb_first_error_regval;
	struct	{
		bdrkreg_t	fe_rsrvd		  :	51;
		bdrkreg_t	fe_valid		  :	 1;
		bdrkreg_t	fe_source		  :	 4;
		bdrkreg_t	fe_channel		  :	 4;
		bdrkreg_t	fe_type			  :	 4;
	} xb_first_error_fld_s;
} xb_first_error_u_t;

#endif




/************************************************************************
 *                                                                      *
 *  Controls DEBUG_DATA mux setting. Allows user to watch the output    *
 * of any OQ or input of any IQ on the DEBUG port. Note that bits       *
 * 13:0 are one-hot. If more than one bit is set in [13:0], the debug   *
 * output is undefined. Details on the debug output lines can be        *
 * found in the XB chapter of the Bedrock Interface Specification.      *
 *                                                                      *
 ************************************************************************/




#ifdef LITTLE_ENDIAN

typedef union xb_debug_data_ctl_u {
	bdrkreg_t	xb_debug_data_ctl_regval;
	struct  {
		bdrkreg_t	ddc_observe_liq_traffic   :	 1;
                bdrkreg_t       ddc_observe_iiq_traffic   :      1;
                bdrkreg_t       ddc_observe_niq_traffic   :      1;
                bdrkreg_t       ddc_observe_miq_traffic   :      1;
                bdrkreg_t       ddc_observe_piq1_traffic  :      1;
                bdrkreg_t       ddc_observe_piq0_traffic  :      1;
                bdrkreg_t       ddc_observe_loq_traffic   :      1;
                bdrkreg_t       ddc_observe_ioq_traffic   :      1;
                bdrkreg_t       ddc_observe_noq_traffic   :      1;
                bdrkreg_t       ddc_observe_mp1_traffic   :      1;
                bdrkreg_t       ddc_observe_mp0_traffic   :      1;
                bdrkreg_t       ddc_observe_mmq_traffic   :      1;
                bdrkreg_t       ddc_observe_poq1_traffic  :      1;
                bdrkreg_t       ddc_observe_poq0_traffic  :      1;
                bdrkreg_t       ddc_observe_source_field  :      1;
                bdrkreg_t       ddc_observe_lodata        :      1;
                bdrkreg_t       ddc_rsrvd                 :     48;
	} xb_debug_data_ctl_fld_s;
} xb_debug_data_ctl_u_t;

#else

typedef union xb_debug_data_ctl_u {
	bdrkreg_t	xb_debug_data_ctl_regval;
	struct	{
		bdrkreg_t	ddc_rsrvd		  :	48;
		bdrkreg_t	ddc_observe_lodata	  :	 1;
		bdrkreg_t	ddc_observe_source_field  :	 1;
		bdrkreg_t	ddc_observe_poq0_traffic  :	 1;
		bdrkreg_t	ddc_observe_poq1_traffic  :	 1;
		bdrkreg_t	ddc_observe_mmq_traffic	  :	 1;
		bdrkreg_t	ddc_observe_mp0_traffic	  :	 1;
		bdrkreg_t	ddc_observe_mp1_traffic	  :	 1;
		bdrkreg_t	ddc_observe_noq_traffic	  :	 1;
		bdrkreg_t	ddc_observe_ioq_traffic	  :	 1;
		bdrkreg_t	ddc_observe_loq_traffic	  :	 1;
		bdrkreg_t	ddc_observe_piq0_traffic  :	 1;
		bdrkreg_t	ddc_observe_piq1_traffic  :	 1;
		bdrkreg_t	ddc_observe_miq_traffic	  :	 1;
		bdrkreg_t	ddc_observe_niq_traffic	  :	 1;
		bdrkreg_t	ddc_observe_iiq_traffic	  :	 1;
		bdrkreg_t	ddc_observe_liq_traffic	  :	 1;
	} xb_debug_data_ctl_fld_s;
} xb_debug_data_ctl_u_t;

#endif




/************************************************************************
 *                                                                      *
 *  Controls debug mux setting for XB Input/Output Queues and           *
 * Arbiter. Can select one of the following values. Details on the      *
 * debug output lines can be found in the XB chapter of the Bedrock     *
 * Interface Specification.                                             *
 *                                                                      *
 ************************************************************************/




#ifdef LITTLE_ENDIAN

typedef union xb_debug_arb_ctl_u {
	bdrkreg_t	xb_debug_arb_ctl_regval;
	struct  {
		bdrkreg_t	dac_xb_debug_select       :	 3;
		bdrkreg_t       dac_rsrvd                 :     61;
	} xb_debug_arb_ctl_fld_s;
} xb_debug_arb_ctl_u_t;

#else

typedef union xb_debug_arb_ctl_u {
        bdrkreg_t       xb_debug_arb_ctl_regval;
        struct  {
                bdrkreg_t       dac_rsrvd                 :     61;
                bdrkreg_t       dac_xb_debug_select       :      3;
        } xb_debug_arb_ctl_fld_s;
} xb_debug_arb_ctl_u_t;

#endif




/************************************************************************
 *                                                                      *
 *  Records errors seen by POQ0.Can be written to test software, will   *
 * cause an interrupt.                                                  *
 *                                                                      *
 ************************************************************************/




#ifdef LITTLE_ENDIAN

typedef union xb_poq0_error_clear_u {
	bdrkreg_t	xb_poq0_error_clear_regval;
	struct  {
		bdrkreg_t	pec_invalid_xsel          :	 2;
                bdrkreg_t       pec_rsrvd_3               :      2;
                bdrkreg_t       pec_overflow              :      2;
                bdrkreg_t       pec_rsrvd_2               :      2;
                bdrkreg_t       pec_underflow             :      2;
                bdrkreg_t       pec_rsrvd_1               :      2;
                bdrkreg_t       pec_tail_timeout          :      2;
                bdrkreg_t       pec_unused                :      6;
                bdrkreg_t       pec_rsrvd                 :     44;
	} xb_poq0_error_clear_fld_s;
} xb_poq0_error_clear_u_t;

#else

typedef union xb_poq0_error_clear_u {
	bdrkreg_t	xb_poq0_error_clear_regval;
	struct	{
		bdrkreg_t	pec_rsrvd		  :	44;
		bdrkreg_t	pec_unused		  :	 6;
		bdrkreg_t	pec_tail_timeout	  :	 2;
		bdrkreg_t	pec_rsrvd_1		  :	 2;
		bdrkreg_t	pec_underflow		  :	 2;
		bdrkreg_t	pec_rsrvd_2		  :	 2;
		bdrkreg_t	pec_overflow		  :	 2;
		bdrkreg_t	pec_rsrvd_3		  :	 2;
		bdrkreg_t	pec_invalid_xsel	  :	 2;
	} xb_poq0_error_clear_fld_s;
} xb_poq0_error_clear_u_t;

#endif




/************************************************************************
 *                                                                      *
 *  Records errors seen by PIQ0. Note that the PIQ/PI interface         *
 * precludes PIQ underflow.                                             *
 *                                                                      *
 ************************************************************************/




#ifdef LITTLE_ENDIAN

typedef union xb_piq0_error_clear_u {
	bdrkreg_t	xb_piq0_error_clear_regval;
	struct  {
		bdrkreg_t	pec_overflow              :	 2;
                bdrkreg_t       pec_rsrvd_1               :      2;
                bdrkreg_t       pec_deadlock_timeout      :      2;
                bdrkreg_t       pec_rsrvd                 :     58;
	} xb_piq0_error_clear_fld_s;
} xb_piq0_error_clear_u_t;

#else

typedef union xb_piq0_error_clear_u {
	bdrkreg_t	xb_piq0_error_clear_regval;
	struct	{
		bdrkreg_t	pec_rsrvd		  :	58;
		bdrkreg_t	pec_deadlock_timeout	  :	 2;
		bdrkreg_t	pec_rsrvd_1		  :	 2;
		bdrkreg_t	pec_overflow		  :	 2;
	} xb_piq0_error_clear_fld_s;
} xb_piq0_error_clear_u_t;

#endif




/************************************************************************
 *                                                                      *
 *  Records errors seen by MP0 queue (the MOQ for processor 0). Since   *
 * the xselect is decoded on the MD/MOQ interface, no invalid xselect   *
 * errors are possible.                                                 *
 *                                                                      *
 ************************************************************************/




#ifdef LITTLE_ENDIAN

typedef union xb_mp0_error_clear_u {
	bdrkreg_t	xb_mp0_error_clear_regval;
	struct  {
		bdrkreg_t	mec_rsrvd_3               :	 4;
                bdrkreg_t       mec_overflow              :      2;
                bdrkreg_t       mec_rsrvd_2               :      2;
                bdrkreg_t       mec_underflow             :      2;
                bdrkreg_t       mec_rsrvd_1               :      2;
                bdrkreg_t       mec_tail_timeout          :      2;
                bdrkreg_t       mec_rsrvd                 :     50;
	} xb_mp0_error_clear_fld_s;
} xb_mp0_error_clear_u_t;

#else

typedef union xb_mp0_error_clear_u {
	bdrkreg_t	xb_mp0_error_clear_regval;
	struct	{
		bdrkreg_t	mec_rsrvd		  :	50;
		bdrkreg_t	mec_tail_timeout	  :	 2;
		bdrkreg_t	mec_rsrvd_1		  :	 2;
		bdrkreg_t	mec_underflow		  :	 2;
		bdrkreg_t	mec_rsrvd_2		  :	 2;
		bdrkreg_t	mec_overflow		  :	 2;
		bdrkreg_t	mec_rsrvd_3		  :	 4;
	} xb_mp0_error_clear_fld_s;
} xb_mp0_error_clear_u_t;

#endif




/************************************************************************
 *                                                                      *
 *  Records errors seen by MIQ.                                         *
 *                                                                      *
 ************************************************************************/




#ifdef LITTLE_ENDIAN

typedef union xb_xm_miq_error_clear_u {
	bdrkreg_t	xb_xm_miq_error_clear_regval;
	struct  {
		bdrkreg_t	xmec_rsrvd_1              :	 4;
                bdrkreg_t       xmec_deadlock_timeout     :      4;
                bdrkreg_t       xmec_rsrvd                :     56;
	} xb_xm_miq_error_clear_fld_s;
} xb_xm_miq_error_clear_u_t;

#else

typedef union xb_xm_miq_error_clear_u {
	bdrkreg_t	xb_xm_miq_error_clear_regval;
	struct	{
		bdrkreg_t	xmec_rsrvd		  :	56;
		bdrkreg_t	xmec_deadlock_timeout	  :	 4;
		bdrkreg_t	xmec_rsrvd_1		  :	 4;
	} xb_xm_miq_error_clear_fld_s;
} xb_xm_miq_error_clear_u_t;

#endif




/************************************************************************
 *                                                                      *
 *  Records errors seen by NOQ.                                         *
 *                                                                      *
 ************************************************************************/




#ifdef LITTLE_ENDIAN

typedef union xb_noq_error_clear_u {
	bdrkreg_t	xb_noq_error_clear_regval;
	struct  {
		bdrkreg_t	nec_rsvd                  :	 4;
                bdrkreg_t       nec_overflow              :      4;
                bdrkreg_t       nec_underflow             :      4;
                bdrkreg_t       nec_tail_timeout          :      4;
                bdrkreg_t       nec_rsrvd                 :     48;
	} xb_noq_error_clear_fld_s;
} xb_noq_error_clear_u_t;

#else

typedef union xb_noq_error_clear_u {
	bdrkreg_t	xb_noq_error_clear_regval;
	struct	{
		bdrkreg_t	nec_rsrvd		  :	48;
		bdrkreg_t	nec_tail_timeout	  :	 4;
		bdrkreg_t	nec_underflow		  :	 4;
		bdrkreg_t	nec_overflow		  :	 4;
		bdrkreg_t	nec_rsvd		  :	 4;
	} xb_noq_error_clear_fld_s;
} xb_noq_error_clear_u_t;

#endif




/************************************************************************
 *                                                                      *
 *  Records errors seen by LOQ.                                         *
 *                                                                      *
 ************************************************************************/




#ifdef LITTLE_ENDIAN

typedef union xb_loq_error_clear_u {
	bdrkreg_t	xb_loq_error_clear_regval;
	struct  {
		bdrkreg_t	lec_invalid_xsel          :	 2;
                bdrkreg_t       lec_rsrvd_1               :      6;
                bdrkreg_t       lec_underflow             :      2;
                bdrkreg_t       lec_rsvd                  :      2;
                bdrkreg_t       lec_tail_timeout          :      2;
                bdrkreg_t       lec_rsrvd                 :     50;
	} xb_loq_error_clear_fld_s;
} xb_loq_error_clear_u_t;

#else

typedef union xb_loq_error_clear_u {
	bdrkreg_t	xb_loq_error_clear_regval;
	struct	{
		bdrkreg_t	lec_rsrvd		  :	50;
		bdrkreg_t	lec_tail_timeout	  :	 2;
		bdrkreg_t	lec_rsvd		  :	 2;
		bdrkreg_t	lec_underflow		  :	 2;
		bdrkreg_t	lec_rsrvd_1		  :	 6;
		bdrkreg_t	lec_invalid_xsel	  :	 2;
	} xb_loq_error_clear_fld_s;
} xb_loq_error_clear_u_t;

#endif




/************************************************************************
 *                                                                      *
 *  Records errors seen by LIQ. Note that the LIQ only records errors   *
 * for the request channel. The reply channel can never deadlock or     *
 * overflow because it does not have hardware flow control.             *
 *                                                                      *
 ************************************************************************/




#ifdef LITTLE_ENDIAN

typedef union xb_liq_error_clear_u {
	bdrkreg_t	xb_liq_error_clear_regval;
	struct  {
		bdrkreg_t	lec_overflow              :	 1;
                bdrkreg_t       lec_rsrvd_1               :      3;
                bdrkreg_t       lec_deadlock_timeout      :      1;
                bdrkreg_t       lec_rsrvd                 :     59;
	} xb_liq_error_clear_fld_s;
} xb_liq_error_clear_u_t;

#else

typedef union xb_liq_error_clear_u {
        bdrkreg_t       xb_liq_error_clear_regval;
        struct  {
                bdrkreg_t       lec_rsrvd                 :     59;
                bdrkreg_t       lec_deadlock_timeout      :      1;
                bdrkreg_t       lec_rsrvd_1               :      3;
                bdrkreg_t       lec_overflow              :      1;
        } xb_liq_error_clear_fld_s;
} xb_liq_error_clear_u_t;

#endif




/************************************************************************
 *                                                                      *
 *  First error is latched whenever the Valid bit is clear and an       *
 * error occurs. Any valid bit on in this register causes an            *
 * interrupt to PI0 and PI1. This interrupt bit will persist until      *
 * the specific error register to capture the error is cleared, then    *
 * the FIRST_ERROR register is cleared (in that oder.) The              *
 * FIRST_ERROR register is not writable, but will be set when any of    *
 * the corresponding error registers are written by software.           *
 *                                                                      *
 ************************************************************************/




#ifdef LITTLE_ENDIAN

typedef union xb_first_error_clear_u {
	bdrkreg_t	xb_first_error_clear_regval;
	struct  {
		bdrkreg_t	fec_type                  :	 4;
                bdrkreg_t       fec_channel               :      4;
                bdrkreg_t       fec_source                :      4;
                bdrkreg_t       fec_valid                 :      1;
                bdrkreg_t       fec_rsrvd                 :     51;
	} xb_first_error_clear_fld_s;
} xb_first_error_clear_u_t;

#else

typedef union xb_first_error_clear_u {
	bdrkreg_t	xb_first_error_clear_regval;
	struct	{
		bdrkreg_t	fec_rsrvd		  :	51;
		bdrkreg_t	fec_valid		  :	 1;
		bdrkreg_t	fec_source		  :	 4;
		bdrkreg_t	fec_channel		  :	 4;
		bdrkreg_t	fec_type		  :	 4;
	} xb_first_error_clear_fld_s;
} xb_first_error_clear_u_t;

#endif






#endif /* _LANGUAGE_C */

/************************************************************************
 *                                                                      *
 * The following defines were not formed into structures                *
 *                                                                      *
 * This could be because the document did not contain details of the    *
 * register, or because the automated script did not recognize the      *
 * register details in the documentation. If these register need        *
 * structure definition, please create them manually                    *
 *                                                                      *
 *           XB_POQ1_ERROR            0x700030                          *
 *           XB_PIQ1_ERROR            0x700038                          *
 *           XB_MP1_ERROR             0x700048                          *
 *           XB_MMQ_ERROR             0x700050                          *
 *           XB_NIQ_ERROR             0x700068                          *
 *           XB_IOQ_ERROR             0x700070                          *
 *           XB_IIQ_ERROR             0x700078                          *
 *           XB_POQ1_ERROR_CLEAR      0x700130                          *
 *           XB_PIQ1_ERROR_CLEAR      0x700138                          *
 *           XB_MP1_ERROR_CLEAR       0x700148                          *
 *           XB_MMQ_ERROR_CLEAR       0x700150                          *
 *           XB_NIQ_ERROR_CLEAR       0x700168                          *
 *           XB_IOQ_ERROR_CLEAR       0x700170                          *
 *           XB_IIQ_ERROR_CLEAR       0x700178                          *
 *                                                                      *
 ************************************************************************/


/************************************************************************
 *                                                                      *
 *               MAKE ALL ADDITIONS AFTER THIS LINE                     *
 *                                                                      *
 ************************************************************************/





#endif /* _ASM_SN_SN1_HUBXB_H */
