/* $Id$
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1992 - 1997, 2000 Silicon Graphics, Inc.
 * Copyright (C) 2000 by Colin Ngam
 */
#ifndef _ASM_SN_SN1_HUBNI_H
#define _ASM_SN_SN1_HUBNI_H


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

#define    NI_PORT_STATUS            0x00680000    /* LLP Status             */



#define    NI_PORT_RESET             0x00680008    /*
                                                    * Reset the Network
                                                    * Interface
                                                    */



#define    NI_RESET_ENABLE           0x00680010    /* Warm Reset Enable      */



#define    NI_DIAG_PARMS             0x00680018    /*
                                                    * Diagnostic
                                                    * Parameters
                                                    */



#define    NI_CHANNEL_CONTROL        0x00680020    /*
                                                    * Virtual channel
                                                    * control
                                                    */



#define    NI_CHANNEL_TEST           0x00680028    /* LLP Test Control.      */



#define    NI_PORT_PARMS             0x00680030    /* LLP Parameters         */



#define    NI_CHANNEL_AGE            0x00680038    /*
                                                    * Network age
                                                    * injection control
                                                    */



#define    NI_PORT_ERRORS            0x00680100    /* Errors                 */



#define    NI_PORT_HEADER_A          0x00680108    /*
                                                    * Error Header first
                                                    * half
                                                    */



#define    NI_PORT_HEADER_B          0x00680110    /*
                                                    * Error Header second
                                                    * half
                                                    */



#define    NI_PORT_SIDEBAND          0x00680118    /* Error Sideband         */



#define    NI_PORT_ERROR_CLEAR       0x00680120    /*
                                                    * Clear the Error
                                                    * bits
                                                    */



#define    NI_LOCAL_TABLE_0          0x00681000    /*
                                                    * Base of Local
                                                    * Mapping Table 0-127
                                                    */



#define    NI_LOCAL_TABLE_1          0x00681008    /*
                                                    * Base of Local
                                                    * Mapping Table 0-127
                                                    */



#define    NI_LOCAL_TABLE_2          0x00681010    /*
                                                    * Base of Local
                                                    * Mapping Table 0-127
                                                    */



#define    NI_LOCAL_TABLE_3          0x00681018    /*
                                                    * Base of Local
                                                    * Mapping Table 0-127
                                                    */



#define    NI_LOCAL_TABLE_4          0x00681020    /*
                                                    * Base of Local
                                                    * Mapping Table 0-127
                                                    */



#define    NI_LOCAL_TABLE_5          0x00681028    /*
                                                    * Base of Local
                                                    * Mapping Table 0-127
                                                    */



#define    NI_LOCAL_TABLE_6          0x00681030    /*
                                                    * Base of Local
                                                    * Mapping Table 0-127
                                                    */



#define    NI_LOCAL_TABLE_7          0x00681038    /*
                                                    * Base of Local
                                                    * Mapping Table 0-127
                                                    */



#define    NI_LOCAL_TABLE_8          0x00681040    /*
                                                    * Base of Local
                                                    * Mapping Table 0-127
                                                    */



#define    NI_LOCAL_TABLE_9          0x00681048    /*
                                                    * Base of Local
                                                    * Mapping Table 0-127
                                                    */



#define    NI_LOCAL_TABLE_10         0x00681050    /*
                                                    * Base of Local
                                                    * Mapping Table 0-127
                                                    */



#define    NI_LOCAL_TABLE_11         0x00681058    /*
                                                    * Base of Local
                                                    * Mapping Table 0-127
                                                    */



#define    NI_LOCAL_TABLE_12         0x00681060    /*
                                                    * Base of Local
                                                    * Mapping Table 0-127
                                                    */



#define    NI_LOCAL_TABLE_13         0x00681068    /*
                                                    * Base of Local
                                                    * Mapping Table 0-127
                                                    */



#define    NI_LOCAL_TABLE_14         0x00681070    /*
                                                    * Base of Local
                                                    * Mapping Table 0-127
                                                    */



#define    NI_LOCAL_TABLE_15         0x00681078    /*
                                                    * Base of Local
                                                    * Mapping Table 0-127
                                                    */



#define    NI_LOCAL_TABLE_16         0x00681080    /*
                                                    * Base of Local
                                                    * Mapping Table 0-127
                                                    */



#define    NI_LOCAL_TABLE_17         0x00681088    /*
                                                    * Base of Local
                                                    * Mapping Table 0-127
                                                    */



#define    NI_LOCAL_TABLE_18         0x00681090    /*
                                                    * Base of Local
                                                    * Mapping Table 0-127
                                                    */



#define    NI_LOCAL_TABLE_19         0x00681098    /*
                                                    * Base of Local
                                                    * Mapping Table 0-127
                                                    */



#define    NI_LOCAL_TABLE_20         0x006810A0    /*
                                                    * Base of Local
                                                    * Mapping Table 0-127
                                                    */



#define    NI_LOCAL_TABLE_21         0x006810A8    /*
                                                    * Base of Local
                                                    * Mapping Table 0-127
                                                    */



#define    NI_LOCAL_TABLE_22         0x006810B0    /*
                                                    * Base of Local
                                                    * Mapping Table 0-127
                                                    */



#define    NI_LOCAL_TABLE_23         0x006810B8    /*
                                                    * Base of Local
                                                    * Mapping Table 0-127
                                                    */



#define    NI_LOCAL_TABLE_24         0x006810C0    /*
                                                    * Base of Local
                                                    * Mapping Table 0-127
                                                    */



#define    NI_LOCAL_TABLE_25         0x006810C8    /*
                                                    * Base of Local
                                                    * Mapping Table 0-127
                                                    */



#define    NI_LOCAL_TABLE_26         0x006810D0    /*
                                                    * Base of Local
                                                    * Mapping Table 0-127
                                                    */



#define    NI_LOCAL_TABLE_27         0x006810D8    /*
                                                    * Base of Local
                                                    * Mapping Table 0-127
                                                    */



#define    NI_LOCAL_TABLE_28         0x006810E0    /*
                                                    * Base of Local
                                                    * Mapping Table 0-127
                                                    */



#define    NI_LOCAL_TABLE_29         0x006810E8    /*
                                                    * Base of Local
                                                    * Mapping Table 0-127
                                                    */



#define    NI_LOCAL_TABLE_30         0x006810F0    /*
                                                    * Base of Local
                                                    * Mapping Table 0-127
                                                    */



#define    NI_LOCAL_TABLE_31         0x006810F8    /*
                                                    * Base of Local
                                                    * Mapping Table 0-127
                                                    */



#define    NI_LOCAL_TABLE_32         0x00681100    /*
                                                    * Base of Local
                                                    * Mapping Table 0-127
                                                    */



#define    NI_LOCAL_TABLE_33         0x00681108    /*
                                                    * Base of Local
                                                    * Mapping Table 0-127
                                                    */



#define    NI_LOCAL_TABLE_34         0x00681110    /*
                                                    * Base of Local
                                                    * Mapping Table 0-127
                                                    */



#define    NI_LOCAL_TABLE_35         0x00681118    /*
                                                    * Base of Local
                                                    * Mapping Table 0-127
                                                    */



#define    NI_LOCAL_TABLE_36         0x00681120    /*
                                                    * Base of Local
                                                    * Mapping Table 0-127
                                                    */



#define    NI_LOCAL_TABLE_37         0x00681128    /*
                                                    * Base of Local
                                                    * Mapping Table 0-127
                                                    */



#define    NI_LOCAL_TABLE_38         0x00681130    /*
                                                    * Base of Local
                                                    * Mapping Table 0-127
                                                    */



#define    NI_LOCAL_TABLE_39         0x00681138    /*
                                                    * Base of Local
                                                    * Mapping Table 0-127
                                                    */



#define    NI_LOCAL_TABLE_40         0x00681140    /*
                                                    * Base of Local
                                                    * Mapping Table 0-127
                                                    */



#define    NI_LOCAL_TABLE_41         0x00681148    /*
                                                    * Base of Local
                                                    * Mapping Table 0-127
                                                    */



#define    NI_LOCAL_TABLE_42         0x00681150    /*
                                                    * Base of Local
                                                    * Mapping Table 0-127
                                                    */



#define    NI_LOCAL_TABLE_43         0x00681158    /*
                                                    * Base of Local
                                                    * Mapping Table 0-127
                                                    */



#define    NI_LOCAL_TABLE_44         0x00681160    /*
                                                    * Base of Local
                                                    * Mapping Table 0-127
                                                    */



#define    NI_LOCAL_TABLE_45         0x00681168    /*
                                                    * Base of Local
                                                    * Mapping Table 0-127
                                                    */



#define    NI_LOCAL_TABLE_46         0x00681170    /*
                                                    * Base of Local
                                                    * Mapping Table 0-127
                                                    */



#define    NI_LOCAL_TABLE_47         0x00681178    /*
                                                    * Base of Local
                                                    * Mapping Table 0-127
                                                    */



#define    NI_LOCAL_TABLE_48         0x00681180    /*
                                                    * Base of Local
                                                    * Mapping Table 0-127
                                                    */



#define    NI_LOCAL_TABLE_49         0x00681188    /*
                                                    * Base of Local
                                                    * Mapping Table 0-127
                                                    */



#define    NI_LOCAL_TABLE_50         0x00681190    /*
                                                    * Base of Local
                                                    * Mapping Table 0-127
                                                    */



#define    NI_LOCAL_TABLE_51         0x00681198    /*
                                                    * Base of Local
                                                    * Mapping Table 0-127
                                                    */



#define    NI_LOCAL_TABLE_52         0x006811A0    /*
                                                    * Base of Local
                                                    * Mapping Table 0-127
                                                    */



#define    NI_LOCAL_TABLE_53         0x006811A8    /*
                                                    * Base of Local
                                                    * Mapping Table 0-127
                                                    */



#define    NI_LOCAL_TABLE_54         0x006811B0    /*
                                                    * Base of Local
                                                    * Mapping Table 0-127
                                                    */



#define    NI_LOCAL_TABLE_55         0x006811B8    /*
                                                    * Base of Local
                                                    * Mapping Table 0-127
                                                    */



#define    NI_LOCAL_TABLE_56         0x006811C0    /*
                                                    * Base of Local
                                                    * Mapping Table 0-127
                                                    */



#define    NI_LOCAL_TABLE_57         0x006811C8    /*
                                                    * Base of Local
                                                    * Mapping Table 0-127
                                                    */



#define    NI_LOCAL_TABLE_58         0x006811D0    /*
                                                    * Base of Local
                                                    * Mapping Table 0-127
                                                    */



#define    NI_LOCAL_TABLE_59         0x006811D8    /*
                                                    * Base of Local
                                                    * Mapping Table 0-127
                                                    */



#define    NI_LOCAL_TABLE_60         0x006811E0    /*
                                                    * Base of Local
                                                    * Mapping Table 0-127
                                                    */



#define    NI_LOCAL_TABLE_61         0x006811E8    /*
                                                    * Base of Local
                                                    * Mapping Table 0-127
                                                    */



#define    NI_LOCAL_TABLE_62         0x006811F0    /*
                                                    * Base of Local
                                                    * Mapping Table 0-127
                                                    */



#define    NI_LOCAL_TABLE_63         0x006811F8    /*
                                                    * Base of Local
                                                    * Mapping Table 0-127
                                                    */



#define    NI_LOCAL_TABLE_64         0x00681200    /*
                                                    * Base of Local
                                                    * Mapping Table 0-127
                                                    */



#define    NI_LOCAL_TABLE_65         0x00681208    /*
                                                    * Base of Local
                                                    * Mapping Table 0-127
                                                    */



#define    NI_LOCAL_TABLE_66         0x00681210    /*
                                                    * Base of Local
                                                    * Mapping Table 0-127
                                                    */



#define    NI_LOCAL_TABLE_67         0x00681218    /*
                                                    * Base of Local
                                                    * Mapping Table 0-127
                                                    */



#define    NI_LOCAL_TABLE_68         0x00681220    /*
                                                    * Base of Local
                                                    * Mapping Table 0-127
                                                    */



#define    NI_LOCAL_TABLE_69         0x00681228    /*
                                                    * Base of Local
                                                    * Mapping Table 0-127
                                                    */



#define    NI_LOCAL_TABLE_70         0x00681230    /*
                                                    * Base of Local
                                                    * Mapping Table 0-127
                                                    */



#define    NI_LOCAL_TABLE_71         0x00681238    /*
                                                    * Base of Local
                                                    * Mapping Table 0-127
                                                    */



#define    NI_LOCAL_TABLE_72         0x00681240    /*
                                                    * Base of Local
                                                    * Mapping Table 0-127
                                                    */



#define    NI_LOCAL_TABLE_73         0x00681248    /*
                                                    * Base of Local
                                                    * Mapping Table 0-127
                                                    */



#define    NI_LOCAL_TABLE_74         0x00681250    /*
                                                    * Base of Local
                                                    * Mapping Table 0-127
                                                    */



#define    NI_LOCAL_TABLE_75         0x00681258    /*
                                                    * Base of Local
                                                    * Mapping Table 0-127
                                                    */



#define    NI_LOCAL_TABLE_76         0x00681260    /*
                                                    * Base of Local
                                                    * Mapping Table 0-127
                                                    */



#define    NI_LOCAL_TABLE_77         0x00681268    /*
                                                    * Base of Local
                                                    * Mapping Table 0-127
                                                    */



#define    NI_LOCAL_TABLE_78         0x00681270    /*
                                                    * Base of Local
                                                    * Mapping Table 0-127
                                                    */



#define    NI_LOCAL_TABLE_79         0x00681278    /*
                                                    * Base of Local
                                                    * Mapping Table 0-127
                                                    */



#define    NI_LOCAL_TABLE_80         0x00681280    /*
                                                    * Base of Local
                                                    * Mapping Table 0-127
                                                    */



#define    NI_LOCAL_TABLE_81         0x00681288    /*
                                                    * Base of Local
                                                    * Mapping Table 0-127
                                                    */



#define    NI_LOCAL_TABLE_82         0x00681290    /*
                                                    * Base of Local
                                                    * Mapping Table 0-127
                                                    */



#define    NI_LOCAL_TABLE_83         0x00681298    /*
                                                    * Base of Local
                                                    * Mapping Table 0-127
                                                    */



#define    NI_LOCAL_TABLE_84         0x006812A0    /*
                                                    * Base of Local
                                                    * Mapping Table 0-127
                                                    */



#define    NI_LOCAL_TABLE_85         0x006812A8    /*
                                                    * Base of Local
                                                    * Mapping Table 0-127
                                                    */



#define    NI_LOCAL_TABLE_86         0x006812B0    /*
                                                    * Base of Local
                                                    * Mapping Table 0-127
                                                    */



#define    NI_LOCAL_TABLE_87         0x006812B8    /*
                                                    * Base of Local
                                                    * Mapping Table 0-127
                                                    */



#define    NI_LOCAL_TABLE_88         0x006812C0    /*
                                                    * Base of Local
                                                    * Mapping Table 0-127
                                                    */



#define    NI_LOCAL_TABLE_89         0x006812C8    /*
                                                    * Base of Local
                                                    * Mapping Table 0-127
                                                    */



#define    NI_LOCAL_TABLE_90         0x006812D0    /*
                                                    * Base of Local
                                                    * Mapping Table 0-127
                                                    */



#define    NI_LOCAL_TABLE_91         0x006812D8    /*
                                                    * Base of Local
                                                    * Mapping Table 0-127
                                                    */



#define    NI_LOCAL_TABLE_92         0x006812E0    /*
                                                    * Base of Local
                                                    * Mapping Table 0-127
                                                    */



#define    NI_LOCAL_TABLE_93         0x006812E8    /*
                                                    * Base of Local
                                                    * Mapping Table 0-127
                                                    */



#define    NI_LOCAL_TABLE_94         0x006812F0    /*
                                                    * Base of Local
                                                    * Mapping Table 0-127
                                                    */



#define    NI_LOCAL_TABLE_95         0x006812F8    /*
                                                    * Base of Local
                                                    * Mapping Table 0-127
                                                    */



#define    NI_LOCAL_TABLE_96         0x00681300    /*
                                                    * Base of Local
                                                    * Mapping Table 0-127
                                                    */



#define    NI_LOCAL_TABLE_97         0x00681308    /*
                                                    * Base of Local
                                                    * Mapping Table 0-127
                                                    */



#define    NI_LOCAL_TABLE_98         0x00681310    /*
                                                    * Base of Local
                                                    * Mapping Table 0-127
                                                    */



#define    NI_LOCAL_TABLE_99         0x00681318    /*
                                                    * Base of Local
                                                    * Mapping Table 0-127
                                                    */



#define    NI_LOCAL_TABLE_100        0x00681320    /*
                                                    * Base of Local
                                                    * Mapping Table 0-127
                                                    */



#define    NI_LOCAL_TABLE_101        0x00681328    /*
                                                    * Base of Local
                                                    * Mapping Table 0-127
                                                    */



#define    NI_LOCAL_TABLE_102        0x00681330    /*
                                                    * Base of Local
                                                    * Mapping Table 0-127
                                                    */



#define    NI_LOCAL_TABLE_103        0x00681338    /*
                                                    * Base of Local
                                                    * Mapping Table 0-127
                                                    */



#define    NI_LOCAL_TABLE_104        0x00681340    /*
                                                    * Base of Local
                                                    * Mapping Table 0-127
                                                    */



#define    NI_LOCAL_TABLE_105        0x00681348    /*
                                                    * Base of Local
                                                    * Mapping Table 0-127
                                                    */



#define    NI_LOCAL_TABLE_106        0x00681350    /*
                                                    * Base of Local
                                                    * Mapping Table 0-127
                                                    */



#define    NI_LOCAL_TABLE_107        0x00681358    /*
                                                    * Base of Local
                                                    * Mapping Table 0-127
                                                    */



#define    NI_LOCAL_TABLE_108        0x00681360    /*
                                                    * Base of Local
                                                    * Mapping Table 0-127
                                                    */



#define    NI_LOCAL_TABLE_109        0x00681368    /*
                                                    * Base of Local
                                                    * Mapping Table 0-127
                                                    */



#define    NI_LOCAL_TABLE_110        0x00681370    /*
                                                    * Base of Local
                                                    * Mapping Table 0-127
                                                    */



#define    NI_LOCAL_TABLE_111        0x00681378    /*
                                                    * Base of Local
                                                    * Mapping Table 0-127
                                                    */



#define    NI_LOCAL_TABLE_112        0x00681380    /*
                                                    * Base of Local
                                                    * Mapping Table 0-127
                                                    */



#define    NI_LOCAL_TABLE_113        0x00681388    /*
                                                    * Base of Local
                                                    * Mapping Table 0-127
                                                    */



#define    NI_LOCAL_TABLE_114        0x00681390    /*
                                                    * Base of Local
                                                    * Mapping Table 0-127
                                                    */



#define    NI_LOCAL_TABLE_115        0x00681398    /*
                                                    * Base of Local
                                                    * Mapping Table 0-127
                                                    */



#define    NI_LOCAL_TABLE_116        0x006813A0    /*
                                                    * Base of Local
                                                    * Mapping Table 0-127
                                                    */



#define    NI_LOCAL_TABLE_117        0x006813A8    /*
                                                    * Base of Local
                                                    * Mapping Table 0-127
                                                    */



#define    NI_LOCAL_TABLE_118        0x006813B0    /*
                                                    * Base of Local
                                                    * Mapping Table 0-127
                                                    */



#define    NI_LOCAL_TABLE_119        0x006813B8    /*
                                                    * Base of Local
                                                    * Mapping Table 0-127
                                                    */



#define    NI_LOCAL_TABLE_120        0x006813C0    /*
                                                    * Base of Local
                                                    * Mapping Table 0-127
                                                    */



#define    NI_LOCAL_TABLE_121        0x006813C8    /*
                                                    * Base of Local
                                                    * Mapping Table 0-127
                                                    */



#define    NI_LOCAL_TABLE_122        0x006813D0    /*
                                                    * Base of Local
                                                    * Mapping Table 0-127
                                                    */



#define    NI_LOCAL_TABLE_123        0x006813D8    /*
                                                    * Base of Local
                                                    * Mapping Table 0-127
                                                    */



#define    NI_LOCAL_TABLE_124        0x006813E0    /*
                                                    * Base of Local
                                                    * Mapping Table 0-127
                                                    */



#define    NI_LOCAL_TABLE_125        0x006813E8    /*
                                                    * Base of Local
                                                    * Mapping Table 0-127
                                                    */



#define    NI_LOCAL_TABLE_126        0x006813F0    /*
                                                    * Base of Local
                                                    * Mapping Table 0-127
                                                    */



#define    NI_LOCAL_TABLE_127        0x006813F8    /*
                                                    * Base of Local
                                                    * Mapping Table 0-127
                                                    */



#define    NI_GLOBAL_TABLE           0x00682000    /*
                                                    * Base of Global
                                                    * Mapping Table
                                                    */





#ifdef _LANGUAGE_C

/************************************************************************
 *                                                                      *
 *  This register describes the LLP status.                             *
 *                                                                      *
 ************************************************************************/




#ifdef LITTLE_ENDIAN

typedef union ni_port_status_u {
	bdrkreg_t	ni_port_status_regval;
	struct  {
		bdrkreg_t	ps_port_status            :	 2;
                bdrkreg_t       ps_remote_power           :      1;
                bdrkreg_t       ps_rsvd                   :     61;
	} ni_port_status_fld_s;
} ni_port_status_u_t;

#else

typedef union ni_port_status_u {
	bdrkreg_t	ni_port_status_regval;
	struct	{
		bdrkreg_t	ps_rsvd			  :	61;
		bdrkreg_t	ps_remote_power		  :	 1;
		bdrkreg_t	ps_port_status		  :	 2;
	} ni_port_status_fld_s;
} ni_port_status_u_t;

#endif




/************************************************************************
 *                                                                      *
 *  Writing this register issues a reset to the network interface.      *
 *                                                                      *
 ************************************************************************/




#ifdef LITTLE_ENDIAN

typedef union ni_port_reset_u {
	bdrkreg_t	ni_port_reset_regval;
	struct  {
		bdrkreg_t	pr_link_reset_out         :	 1;
                bdrkreg_t       pr_port_reset             :      1;
                bdrkreg_t       pr_local_reset            :      1;
                bdrkreg_t       pr_rsvd                   :     61;
	} ni_port_reset_fld_s;
} ni_port_reset_u_t;

#else

typedef union ni_port_reset_u {
	bdrkreg_t	ni_port_reset_regval;
	struct	{
		bdrkreg_t	pr_rsvd			  :	61;
		bdrkreg_t	pr_local_reset		  :	 1;
		bdrkreg_t	pr_port_reset		  :	 1;
		bdrkreg_t	pr_link_reset_out	  :	 1;
	} ni_port_reset_fld_s;
} ni_port_reset_u_t;

#endif



/************************************************************************
 *                                                                      *
 *  This register contains the warm reset enable bit.                   *
 *                                                                      *
 ************************************************************************/




#ifdef LITTLE_ENDIAN

typedef union ni_reset_enable_u {
	bdrkreg_t	ni_reset_enable_regval;
	struct  {
		bdrkreg_t	re_reset_ok               :	 1;
                bdrkreg_t       re_rsvd                   :     63;
	} ni_reset_enable_fld_s;
} ni_reset_enable_u_t;

#else

typedef union ni_reset_enable_u {
	bdrkreg_t	ni_reset_enable_regval;
	struct	{
		bdrkreg_t	re_rsvd			  :	63;
		bdrkreg_t	re_reset_ok		  :	 1;
	} ni_reset_enable_fld_s;
} ni_reset_enable_u_t;

#endif




/************************************************************************
 *                                                                      *
 *  This register contains parameters for diagnostics.                  *
 *                                                                      *
 ************************************************************************/




#ifdef LITTLE_ENDIAN

typedef union ni_diag_parms_u {
	bdrkreg_t	ni_diag_parms_regval;
	struct  {
		bdrkreg_t	dp_send_data_error        :	 1;
                bdrkreg_t       dp_port_disable           :      1;
                bdrkreg_t       dp_send_err_off           :      1;
                bdrkreg_t       dp_rsvd                   :     61;
	} ni_diag_parms_fld_s;
} ni_diag_parms_u_t;

#else

typedef union ni_diag_parms_u {
	bdrkreg_t	ni_diag_parms_regval;
	struct	{
		bdrkreg_t	dp_rsvd			  :	61;
		bdrkreg_t	dp_send_err_off		  :	 1;
		bdrkreg_t	dp_port_disable		  :	 1;
		bdrkreg_t	dp_send_data_error	  :	 1;
	} ni_diag_parms_fld_s;
} ni_diag_parms_u_t;

#endif




/************************************************************************
 *                                                                      *
 *  This register contains the virtual channel selection control for    *
 * outgoing messages from the Bedrock.                                  *
 *                                                                      *
 ************************************************************************/




#ifdef LITTLE_ENDIAN

typedef union ni_channel_control_u {
	bdrkreg_t	ni_channel_control_regval;
	struct  {
		bdrkreg_t	cc_vch_one_request        :	 1;
                bdrkreg_t       cc_vch_two_request        :      1;
                bdrkreg_t       cc_vch_nine_request       :      1;
                bdrkreg_t       cc_vch_vector_request     :      1;
                bdrkreg_t       cc_vch_one_reply          :      1;
                bdrkreg_t       cc_vch_two_reply          :      1;
                bdrkreg_t       cc_vch_nine_reply         :      1;
                bdrkreg_t       cc_vch_vector_reply       :      1;
                bdrkreg_t       cc_send_vch_sel           :      1;
                bdrkreg_t       cc_rsvd                   :     55;
	} ni_channel_control_fld_s;
} ni_channel_control_u_t;

#else

typedef union ni_channel_control_u {
	bdrkreg_t	ni_channel_control_regval;
	struct	{
		bdrkreg_t	cc_rsvd			  :	55;
		bdrkreg_t	cc_send_vch_sel		  :	 1;
		bdrkreg_t	cc_vch_vector_reply	  :	 1;
		bdrkreg_t	cc_vch_nine_reply	  :	 1;
		bdrkreg_t	cc_vch_two_reply	  :	 1;
		bdrkreg_t	cc_vch_one_reply	  :	 1;
		bdrkreg_t	cc_vch_vector_request	  :	 1;
		bdrkreg_t	cc_vch_nine_request	  :	 1;
		bdrkreg_t	cc_vch_two_request	  :	 1;
		bdrkreg_t	cc_vch_one_request	  :	 1;
	} ni_channel_control_fld_s;
} ni_channel_control_u_t;

#endif




/************************************************************************
 *                                                                      *
 *  This register allows access to the LLP test logic.                  *
 *                                                                      *
 ************************************************************************/




#ifdef LITTLE_ENDIAN

typedef union ni_channel_test_u {
	bdrkreg_t	ni_channel_test_regval;
	struct  {
		bdrkreg_t	ct_testseed               :	20;
                bdrkreg_t       ct_testmask               :      8;
                bdrkreg_t       ct_testdata               :     20;
                bdrkreg_t       ct_testvalid              :      1;
                bdrkreg_t       ct_testcberr              :      1;
                bdrkreg_t       ct_testflit               :      3;
                bdrkreg_t       ct_testclear              :      1;
                bdrkreg_t       ct_testerrcapture         :      1;
                bdrkreg_t       ct_rsvd                   :      9;
	} ni_channel_test_fld_s;
} ni_channel_test_u_t;

#else

typedef union ni_channel_test_u {
	bdrkreg_t	ni_channel_test_regval;
	struct	{
		bdrkreg_t	ct_rsvd			  :	 9;
		bdrkreg_t	ct_testerrcapture	  :	 1;
		bdrkreg_t	ct_testclear		  :	 1;
		bdrkreg_t	ct_testflit		  :	 3;
		bdrkreg_t	ct_testcberr		  :	 1;
		bdrkreg_t	ct_testvalid		  :	 1;
		bdrkreg_t	ct_testdata		  :	20;
		bdrkreg_t	ct_testmask		  :	 8;
		bdrkreg_t	ct_testseed		  :	20;
	} ni_channel_test_fld_s;
} ni_channel_test_u_t;

#endif




/************************************************************************
 *                                                                      *
 *  This register contains LLP port parameters and enables for the      *
 * capture of header data.                                              *
 *                                                                      *
 ************************************************************************/




#ifdef LITTLE_ENDIAN

typedef union ni_port_parms_u {
	bdrkreg_t	ni_port_parms_regval;
	struct  {
		bdrkreg_t	pp_max_burst              :	10;
                bdrkreg_t       pp_null_timeout           :      6;
                bdrkreg_t       pp_max_retry              :     10;
                bdrkreg_t       pp_d_avail_sel            :      2;
                bdrkreg_t       pp_rsvd_1                 :      1;
                bdrkreg_t       pp_first_err_enable       :      1;
                bdrkreg_t       pp_squash_err_enable      :      1;
                bdrkreg_t       pp_vch_err_enable         :      4;
                bdrkreg_t       pp_rsvd                   :     29;
	} ni_port_parms_fld_s;
} ni_port_parms_u_t;

#else

typedef union ni_port_parms_u {
	bdrkreg_t	ni_port_parms_regval;
	struct	{
		bdrkreg_t	pp_rsvd			  :	29;
		bdrkreg_t	pp_vch_err_enable	  :	 4;
		bdrkreg_t	pp_squash_err_enable	  :	 1;
		bdrkreg_t	pp_first_err_enable	  :	 1;
		bdrkreg_t	pp_rsvd_1		  :	 1;
		bdrkreg_t	pp_d_avail_sel		  :	 2;
		bdrkreg_t	pp_max_retry		  :	10;
		bdrkreg_t	pp_null_timeout		  :	 6;
		bdrkreg_t	pp_max_burst		  :	10;
	} ni_port_parms_fld_s;
} ni_port_parms_u_t;

#endif




/************************************************************************
 *                                                                      *
 *  This register contains the age at which request and reply packets   *
 * are injected into the network. This feature allows replies to be     *
 * given a higher fixed priority than requests, which can be            *
 * important in some network saturation situations.                     *
 *                                                                      *
 ************************************************************************/




#ifdef LITTLE_ENDIAN

typedef union ni_channel_age_u {
	bdrkreg_t	ni_channel_age_regval;
	struct  {
		bdrkreg_t	ca_request_inject_age     :	 8;
                bdrkreg_t       ca_reply_inject_age       :      8;
                bdrkreg_t       ca_rsvd                   :     48;
	} ni_channel_age_fld_s;
} ni_channel_age_u_t;

#else

typedef union ni_channel_age_u {
	bdrkreg_t	ni_channel_age_regval;
	struct	{
		bdrkreg_t	ca_rsvd			  :	48;
		bdrkreg_t	ca_reply_inject_age	  :	 8;
		bdrkreg_t	ca_request_inject_age	  :	 8;
	} ni_channel_age_fld_s;
} ni_channel_age_u_t;

#endif




/************************************************************************
 *                                                                      *
 *  This register contains latched LLP port and problematic message     *
 * errors. The contents are the same information as the                 *
 * NI_PORT_ERROR_CLEAR register, but, in this register read accesses    *
 * are non-destructive. Bits [52:24] assert the NI interrupt.           *
 *                                                                      *
 ************************************************************************/




#ifdef LITTLE_ENDIAN

typedef union ni_port_errors_u {
	bdrkreg_t	ni_port_errors_regval;
	struct  {
		bdrkreg_t	pe_sn_error_count         :	 8;
                bdrkreg_t       pe_cb_error_count         :      8;
                bdrkreg_t       pe_retry_count            :      8;
                bdrkreg_t       pe_tail_timeout           :      4;
                bdrkreg_t       pe_fifo_overflow          :      4;
                bdrkreg_t       pe_external_short         :      4;
                bdrkreg_t       pe_external_long          :      4;
                bdrkreg_t       pe_external_bad_header    :      4;
                bdrkreg_t       pe_internal_short         :      4;
                bdrkreg_t       pe_internal_long          :      4;
                bdrkreg_t       pe_link_reset_in          :      1;
                bdrkreg_t       pe_rsvd                   :     11;
	} ni_port_errors_fld_s;
} ni_port_errors_u_t;

#else

typedef union ni_port_errors_u {
	bdrkreg_t	ni_port_errors_regval;
	struct	{
		bdrkreg_t	pe_rsvd			  :	11;
		bdrkreg_t	pe_link_reset_in	  :	 1;
		bdrkreg_t	pe_internal_long	  :	 4;
		bdrkreg_t	pe_internal_short	  :	 4;
		bdrkreg_t	pe_external_bad_header	  :	 4;
		bdrkreg_t	pe_external_long	  :	 4;
		bdrkreg_t	pe_external_short	  :	 4;
		bdrkreg_t	pe_fifo_overflow	  :	 4;
		bdrkreg_t	pe_tail_timeout		  :	 4;
		bdrkreg_t	pe_retry_count		  :	 8;
		bdrkreg_t	pe_cb_error_count	  :	 8;
		bdrkreg_t	pe_sn_error_count	  :	 8;
	} ni_port_errors_fld_s;
} ni_port_errors_u_t;

#endif




/************************************************************************
 *                                                                      *
 *  This register provides the sideband data associated with the        *
 * NI_PORT_HEADER registers and also additional data for error          *
 * processing. This register is not cleared on reset.                   *
 *                                                                      *
 ************************************************************************/




#ifdef LITTLE_ENDIAN

typedef union ni_port_sideband_u {
	bdrkreg_t	ni_port_sideband_regval;
	struct  {
		bdrkreg_t	ps_sideband               :	 8;
                bdrkreg_t       ps_bad_dest               :      1;
                bdrkreg_t       ps_bad_prexsel            :      1;
                bdrkreg_t       ps_rcv_error              :      1;
                bdrkreg_t       ps_bad_message            :      1;
                bdrkreg_t       ps_squash                 :      1;
                bdrkreg_t       ps_sn_status              :      1;
                bdrkreg_t       ps_cb_status              :      1;
                bdrkreg_t       ps_send_error             :      1;
                bdrkreg_t       ps_vch_active             :      4;
                bdrkreg_t       ps_rsvd                   :     44;
	} ni_port_sideband_fld_s;
} ni_port_sideband_u_t;

#else

typedef union ni_port_sideband_u {
	bdrkreg_t	ni_port_sideband_regval;
	struct	{
		bdrkreg_t	ps_rsvd			  :	44;
		bdrkreg_t	ps_vch_active		  :	 4;
		bdrkreg_t	ps_send_error		  :	 1;
		bdrkreg_t	ps_cb_status		  :	 1;
		bdrkreg_t	ps_sn_status		  :	 1;
		bdrkreg_t	ps_squash		  :	 1;
		bdrkreg_t	ps_bad_message		  :	 1;
		bdrkreg_t	ps_rcv_error		  :	 1;
		bdrkreg_t	ps_bad_prexsel		  :	 1;
		bdrkreg_t	ps_bad_dest		  :	 1;
		bdrkreg_t	ps_sideband		  :	 8;
	} ni_port_sideband_fld_s;
} ni_port_sideband_u_t;

#endif




/************************************************************************
 *                                                                      *
 *  This register contains latched LLP port and problematic message     *
 * errors. The contents are the same information as the                 *
 * NI_PORT_ERROR_CLEAR register, but, in this register read accesses    *
 * are non-destructive. Bits [52:24] assert the NI interrupt.           *
 *                                                                      *
 ************************************************************************/




#ifdef LITTLE_ENDIAN

typedef union ni_port_error_clear_u {
	bdrkreg_t	ni_port_error_clear_regval;
	struct  {
		bdrkreg_t	pec_sn_error_count        :	 8;
                bdrkreg_t       pec_cb_error_count        :      8;
                bdrkreg_t       pec_retry_count           :      8;
                bdrkreg_t       pec_tail_timeout          :      4;
                bdrkreg_t       pec_fifo_overflow         :      4;
                bdrkreg_t       pec_external_short        :      4;
                bdrkreg_t       pec_external_long         :      4;
                bdrkreg_t       pec_external_bad_header   :      4;
                bdrkreg_t       pec_internal_short        :      4;
                bdrkreg_t       pec_internal_long         :      4;
                bdrkreg_t       pec_link_reset_in         :      1;
                bdrkreg_t       pec_rsvd                  :     11;
	} ni_port_error_clear_fld_s;
} ni_port_error_clear_u_t;

#else

typedef union ni_port_error_clear_u {
	bdrkreg_t	ni_port_error_clear_regval;
	struct	{
		bdrkreg_t	pec_rsvd		  :	11;
		bdrkreg_t	pec_link_reset_in	  :	 1;
		bdrkreg_t	pec_internal_long	  :	 4;
		bdrkreg_t	pec_internal_short	  :	 4;
		bdrkreg_t	pec_external_bad_header	  :	 4;
		bdrkreg_t	pec_external_long	  :	 4;
		bdrkreg_t	pec_external_short	  :	 4;
		bdrkreg_t	pec_fifo_overflow	  :	 4;
		bdrkreg_t	pec_tail_timeout	  :	 4;
		bdrkreg_t	pec_retry_count		  :	 8;
		bdrkreg_t	pec_cb_error_count	  :	 8;
		bdrkreg_t	pec_sn_error_count	  :	 8;
	} ni_port_error_clear_fld_s;
} ni_port_error_clear_u_t;

#endif




/************************************************************************
 *                                                                      *
 *  Lookup table for the next hop's exit port. The table entry          *
 * selection is based on the 7-bit LocalCube routing destination.       *
 *                                                                      *
 ************************************************************************/




#ifdef LITTLE_ENDIAN

typedef union ni_local_table_0_u {
	bdrkreg_t	ni_local_table_0_regval;
	struct  {
		bdrkreg_t	lt0_next_exit_port        :	 4;
                bdrkreg_t       lt0_next_vch_lsb          :      1;
                bdrkreg_t       lt0_rsvd                  :     59;
	} ni_local_table_0_fld_s;
} ni_local_table_0_u_t;

#else

typedef union ni_local_table_0_u {
	bdrkreg_t	ni_local_table_0_regval;
	struct	{
		bdrkreg_t	lt0_rsvd		  :	59;
		bdrkreg_t	lt0_next_vch_lsb	  :	 1;
		bdrkreg_t	lt0_next_exit_port	  :	 4;
	} ni_local_table_0_fld_s;
} ni_local_table_0_u_t;

#endif




/************************************************************************
 *                                                                      *
 *  Lookup table for the next hop's exit port. The table entry          *
 * selection is based on the 7-bit LocalCube routing destination.       *
 *                                                                      *
 ************************************************************************/




#ifdef LITTLE_ENDIAN

typedef union ni_local_table_127_u {
	bdrkreg_t	ni_local_table_127_regval;
	struct  {
		bdrkreg_t	lt1_next_exit_port        :	 4;
                bdrkreg_t       lt1_next_vch_lsb          :      1;
                bdrkreg_t       lt1_rsvd                  :     59;
	} ni_local_table_127_fld_s;
} ni_local_table_127_u_t;

#else

typedef union ni_local_table_127_u {
	bdrkreg_t	ni_local_table_127_regval;
	struct	{
		bdrkreg_t	lt1_rsvd		  :	59;
		bdrkreg_t	lt1_next_vch_lsb	  :	 1;
		bdrkreg_t	lt1_next_exit_port	  :	 4;
	} ni_local_table_127_fld_s;
} ni_local_table_127_u_t;

#endif




/************************************************************************
 *                                                                      *
 *  Lookup table for the next hop's exit port. The table entry          *
 * selection is based on the 1-bit MetaCube routing destination.        *
 *                                                                      *
 ************************************************************************/




#ifdef LITTLE_ENDIAN

typedef union ni_global_table_u {
	bdrkreg_t	ni_global_table_regval;
	struct  {
		bdrkreg_t	gt_next_exit_port         :	 4;
                bdrkreg_t       gt_next_vch_lsb           :      1;
                bdrkreg_t       gt_rsvd                   :     59;
	} ni_global_table_fld_s;
} ni_global_table_u_t;

#else

typedef union ni_global_table_u {
	bdrkreg_t	ni_global_table_regval;
	struct	{
		bdrkreg_t	gt_rsvd			  :	59;
		bdrkreg_t	gt_next_vch_lsb		  :	 1;
		bdrkreg_t	gt_next_exit_port	  :	 4;
	} ni_global_table_fld_s;
} ni_global_table_u_t;

#endif






#endif /* _LANGUAGE_C */

/************************************************************************
 *                                                                      *
 * The following defines which were not formed into structures are      *
 * probably indentical to another register, and the name of the         *
 * register is provided against each of these registers. This           *
 * information needs to be checked carefully                            *
 *                                                                      *
 *           NI_LOCAL_TABLE_1          NI_LOCAL_TABLE_0                 *
 *           NI_LOCAL_TABLE_2          NI_LOCAL_TABLE_0                 *
 *           NI_LOCAL_TABLE_3          NI_LOCAL_TABLE_0                 *
 *           NI_LOCAL_TABLE_4          NI_LOCAL_TABLE_0                 *
 *           NI_LOCAL_TABLE_5          NI_LOCAL_TABLE_0                 *
 *           NI_LOCAL_TABLE_6          NI_LOCAL_TABLE_0                 *
 *           NI_LOCAL_TABLE_7          NI_LOCAL_TABLE_0                 *
 *           NI_LOCAL_TABLE_8          NI_LOCAL_TABLE_0                 *
 *           NI_LOCAL_TABLE_9          NI_LOCAL_TABLE_0                 *
 *           NI_LOCAL_TABLE_10         NI_LOCAL_TABLE_0                 *
 *           NI_LOCAL_TABLE_11         NI_LOCAL_TABLE_0                 *
 *           NI_LOCAL_TABLE_12         NI_LOCAL_TABLE_0                 *
 *           NI_LOCAL_TABLE_13         NI_LOCAL_TABLE_0                 *
 *           NI_LOCAL_TABLE_14         NI_LOCAL_TABLE_0                 *
 *           NI_LOCAL_TABLE_15         NI_LOCAL_TABLE_0                 *
 *           NI_LOCAL_TABLE_16         NI_LOCAL_TABLE_0                 *
 *           NI_LOCAL_TABLE_17         NI_LOCAL_TABLE_0                 *
 *           NI_LOCAL_TABLE_18         NI_LOCAL_TABLE_0                 *
 *           NI_LOCAL_TABLE_19         NI_LOCAL_TABLE_0                 *
 *           NI_LOCAL_TABLE_20         NI_LOCAL_TABLE_0                 *
 *           NI_LOCAL_TABLE_21         NI_LOCAL_TABLE_0                 *
 *           NI_LOCAL_TABLE_22         NI_LOCAL_TABLE_0                 *
 *           NI_LOCAL_TABLE_23         NI_LOCAL_TABLE_0                 *
 *           NI_LOCAL_TABLE_24         NI_LOCAL_TABLE_0                 *
 *           NI_LOCAL_TABLE_25         NI_LOCAL_TABLE_0                 *
 *           NI_LOCAL_TABLE_26         NI_LOCAL_TABLE_0                 *
 *           NI_LOCAL_TABLE_27         NI_LOCAL_TABLE_0                 *
 *           NI_LOCAL_TABLE_28         NI_LOCAL_TABLE_0                 *
 *           NI_LOCAL_TABLE_29         NI_LOCAL_TABLE_0                 *
 *           NI_LOCAL_TABLE_30         NI_LOCAL_TABLE_0                 *
 *           NI_LOCAL_TABLE_31         NI_LOCAL_TABLE_0                 *
 *           NI_LOCAL_TABLE_32         NI_LOCAL_TABLE_0                 *
 *           NI_LOCAL_TABLE_33         NI_LOCAL_TABLE_0                 *
 *           NI_LOCAL_TABLE_34         NI_LOCAL_TABLE_0                 *
 *           NI_LOCAL_TABLE_35         NI_LOCAL_TABLE_0                 *
 *           NI_LOCAL_TABLE_36         NI_LOCAL_TABLE_0                 *
 *           NI_LOCAL_TABLE_37         NI_LOCAL_TABLE_0                 *
 *           NI_LOCAL_TABLE_38         NI_LOCAL_TABLE_0                 *
 *           NI_LOCAL_TABLE_39         NI_LOCAL_TABLE_0                 *
 *           NI_LOCAL_TABLE_40         NI_LOCAL_TABLE_0                 *
 *           NI_LOCAL_TABLE_41         NI_LOCAL_TABLE_0                 *
 *           NI_LOCAL_TABLE_42         NI_LOCAL_TABLE_0                 *
 *           NI_LOCAL_TABLE_43         NI_LOCAL_TABLE_0                 *
 *           NI_LOCAL_TABLE_44         NI_LOCAL_TABLE_0                 *
 *           NI_LOCAL_TABLE_45         NI_LOCAL_TABLE_0                 *
 *           NI_LOCAL_TABLE_46         NI_LOCAL_TABLE_0                 *
 *           NI_LOCAL_TABLE_47         NI_LOCAL_TABLE_0                 *
 *           NI_LOCAL_TABLE_48         NI_LOCAL_TABLE_0                 *
 *           NI_LOCAL_TABLE_49         NI_LOCAL_TABLE_0                 *
 *           NI_LOCAL_TABLE_50         NI_LOCAL_TABLE_0                 *
 *           NI_LOCAL_TABLE_51         NI_LOCAL_TABLE_0                 *
 *           NI_LOCAL_TABLE_52         NI_LOCAL_TABLE_0                 *
 *           NI_LOCAL_TABLE_53         NI_LOCAL_TABLE_0                 *
 *           NI_LOCAL_TABLE_54         NI_LOCAL_TABLE_0                 *
 *           NI_LOCAL_TABLE_55         NI_LOCAL_TABLE_0                 *
 *           NI_LOCAL_TABLE_56         NI_LOCAL_TABLE_0                 *
 *           NI_LOCAL_TABLE_57         NI_LOCAL_TABLE_0                 *
 *           NI_LOCAL_TABLE_58         NI_LOCAL_TABLE_0                 *
 *           NI_LOCAL_TABLE_59         NI_LOCAL_TABLE_0                 *
 *           NI_LOCAL_TABLE_60         NI_LOCAL_TABLE_0                 *
 *           NI_LOCAL_TABLE_61         NI_LOCAL_TABLE_0                 *
 *           NI_LOCAL_TABLE_62         NI_LOCAL_TABLE_0                 *
 *           NI_LOCAL_TABLE_63         NI_LOCAL_TABLE_0                 *
 *           NI_LOCAL_TABLE_64         NI_LOCAL_TABLE_0                 *
 *           NI_LOCAL_TABLE_65         NI_LOCAL_TABLE_0                 *
 *           NI_LOCAL_TABLE_66         NI_LOCAL_TABLE_0                 *
 *           NI_LOCAL_TABLE_67         NI_LOCAL_TABLE_0                 *
 *           NI_LOCAL_TABLE_68         NI_LOCAL_TABLE_0                 *
 *           NI_LOCAL_TABLE_69         NI_LOCAL_TABLE_0                 *
 *           NI_LOCAL_TABLE_70         NI_LOCAL_TABLE_0                 *
 *           NI_LOCAL_TABLE_71         NI_LOCAL_TABLE_0                 *
 *           NI_LOCAL_TABLE_72         NI_LOCAL_TABLE_0                 *
 *           NI_LOCAL_TABLE_73         NI_LOCAL_TABLE_0                 *
 *           NI_LOCAL_TABLE_74         NI_LOCAL_TABLE_0                 *
 *           NI_LOCAL_TABLE_75         NI_LOCAL_TABLE_0                 *
 *           NI_LOCAL_TABLE_76         NI_LOCAL_TABLE_0                 *
 *           NI_LOCAL_TABLE_77         NI_LOCAL_TABLE_0                 *
 *           NI_LOCAL_TABLE_78         NI_LOCAL_TABLE_0                 *
 *           NI_LOCAL_TABLE_79         NI_LOCAL_TABLE_0                 *
 *           NI_LOCAL_TABLE_80         NI_LOCAL_TABLE_0                 *
 *           NI_LOCAL_TABLE_81         NI_LOCAL_TABLE_0                 *
 *           NI_LOCAL_TABLE_82         NI_LOCAL_TABLE_0                 *
 *           NI_LOCAL_TABLE_83         NI_LOCAL_TABLE_0                 *
 *           NI_LOCAL_TABLE_84         NI_LOCAL_TABLE_0                 *
 *           NI_LOCAL_TABLE_85         NI_LOCAL_TABLE_0                 *
 *           NI_LOCAL_TABLE_86         NI_LOCAL_TABLE_0                 *
 *           NI_LOCAL_TABLE_87         NI_LOCAL_TABLE_0                 *
 *           NI_LOCAL_TABLE_88         NI_LOCAL_TABLE_0                 *
 *           NI_LOCAL_TABLE_89         NI_LOCAL_TABLE_0                 *
 *           NI_LOCAL_TABLE_90         NI_LOCAL_TABLE_0                 *
 *           NI_LOCAL_TABLE_91         NI_LOCAL_TABLE_0                 *
 *           NI_LOCAL_TABLE_92         NI_LOCAL_TABLE_0                 *
 *           NI_LOCAL_TABLE_93         NI_LOCAL_TABLE_0                 *
 *           NI_LOCAL_TABLE_94         NI_LOCAL_TABLE_0                 *
 *           NI_LOCAL_TABLE_95         NI_LOCAL_TABLE_0                 *
 *           NI_LOCAL_TABLE_96         NI_LOCAL_TABLE_0                 *
 *           NI_LOCAL_TABLE_97         NI_LOCAL_TABLE_0                 *
 *           NI_LOCAL_TABLE_98         NI_LOCAL_TABLE_0                 *
 *           NI_LOCAL_TABLE_99         NI_LOCAL_TABLE_0                 *
 *           NI_LOCAL_TABLE_100        NI_LOCAL_TABLE_0                 *
 *           NI_LOCAL_TABLE_101        NI_LOCAL_TABLE_0                 *
 *           NI_LOCAL_TABLE_102        NI_LOCAL_TABLE_0                 *
 *           NI_LOCAL_TABLE_103        NI_LOCAL_TABLE_0                 *
 *           NI_LOCAL_TABLE_104        NI_LOCAL_TABLE_0                 *
 *           NI_LOCAL_TABLE_105        NI_LOCAL_TABLE_0                 *
 *           NI_LOCAL_TABLE_106        NI_LOCAL_TABLE_0                 *
 *           NI_LOCAL_TABLE_107        NI_LOCAL_TABLE_0                 *
 *           NI_LOCAL_TABLE_108        NI_LOCAL_TABLE_0                 *
 *           NI_LOCAL_TABLE_109        NI_LOCAL_TABLE_0                 *
 *           NI_LOCAL_TABLE_110        NI_LOCAL_TABLE_0                 *
 *           NI_LOCAL_TABLE_111        NI_LOCAL_TABLE_0                 *
 *           NI_LOCAL_TABLE_112        NI_LOCAL_TABLE_0                 *
 *           NI_LOCAL_TABLE_113        NI_LOCAL_TABLE_0                 *
 *           NI_LOCAL_TABLE_114        NI_LOCAL_TABLE_0                 *
 *           NI_LOCAL_TABLE_115        NI_LOCAL_TABLE_0                 *
 *           NI_LOCAL_TABLE_116        NI_LOCAL_TABLE_0                 *
 *           NI_LOCAL_TABLE_117        NI_LOCAL_TABLE_0                 *
 *           NI_LOCAL_TABLE_118        NI_LOCAL_TABLE_0                 *
 *           NI_LOCAL_TABLE_119        NI_LOCAL_TABLE_0                 *
 *           NI_LOCAL_TABLE_120        NI_LOCAL_TABLE_0                 *
 *           NI_LOCAL_TABLE_121        NI_LOCAL_TABLE_0                 *
 *           NI_LOCAL_TABLE_122        NI_LOCAL_TABLE_0                 *
 *           NI_LOCAL_TABLE_123        NI_LOCAL_TABLE_0                 *
 *           NI_LOCAL_TABLE_124        NI_LOCAL_TABLE_0                 *
 *           NI_LOCAL_TABLE_125        NI_LOCAL_TABLE_0                 *
 *           NI_LOCAL_TABLE_126        NI_LOCAL_TABLE_0                 *
 *                                                                      *
 ************************************************************************/


/************************************************************************
 *                                                                      *
 * The following defines were not formed into structures                *
 *                                                                      *
 * This could be because the document did not contain details of the    *
 * register, or because the automated script did not recognize the      *
 * register details in the documentation. If these register need        *
 * structure definition, please create them manually                    *
 *                                                                      *
 *           NI_PORT_HEADER_A         0x680108                          *
 *           NI_PORT_HEADER_B         0x680110                          *
 *                                                                      *
 ************************************************************************/


/************************************************************************
 *                                                                      *
 *               MAKE ALL ADDITIONS AFTER THIS LINE                     *
 *                                                                      *
 ************************************************************************/





#endif /* _ASM_SN_SN1_HUBNI_H */
