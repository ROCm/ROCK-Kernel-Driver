/*
 * File:	mca.h
 * Purpose:	Machine check handling specific to the SN platform defines
 *
 * Copyright (C) 2001-2002 Silicon Graphics, Inc. All rights reserved.
 * 
 * This program is free software; you can redistribute it and/or modify it 
 * under the terms of version 2 of the GNU General Public License 
 * as published by the Free Software Foundation.
 * 
 * This program is distributed in the hope that it would be useful, but 
 * WITHOUT ANY WARRANTY; without even the implied warranty of 
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. 
 * 
 * Further, this software is distributed without any warranty that it is 
 * free of the rightful claim of any third person regarding infringement 
 * or the like.  Any license provided herein, whether implied or 
 * otherwise, applies only to this software file.  Patent licenses, if 
 * any, provided herein do not apply to combinations of this program with 
 * other software, or any other product whatsoever.
 * 
 * You should have received a copy of the GNU General Public 
 * License along with this program; if not, write the Free Software 
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston MA 02111-1307, USA.
 * 
 * Contact information:  Silicon Graphics, Inc., 1600 Amphitheatre Pkwy, 
 * Mountain View, CA  94043, or:
 * 
 * http://www.sgi.com 
 * 
 * For further information regarding this notice, see: 
 * 
 * http://oss.sgi.com/projects/GenInfo/NoticeExplan
 */

#include <linux/config.h>
#include <linux/types.h>
#include <asm/sal.h>
#include <asm/mca.h>

#ifdef CONFIG_IA64_SGI_SN

typedef u64 __uint64_t;

typedef struct {
	__uint64_t sh_event_occurred;
	__uint64_t sh_first_error;
	__uint64_t sh_event_overflow;
	__uint64_t sh_pi_first_error;
	__uint64_t sh_pi_error_summary;
	__uint64_t sh_pi_error_overflow;
	__uint64_t sh_pi_error_detail_1;
	__uint64_t sh_pi_error_detail_2;
	__uint64_t sh_pi_hw_time_stamp;
	__uint64_t sh_pi_uncorrected_detail_1;
	__uint64_t sh_pi_uncorrected_detail_2;
	__uint64_t sh_pi_uncorrected_detail_3;
	__uint64_t sh_pi_uncorrected_detail_4;
	__uint64_t sh_pi_uncor_time_stamp;
	__uint64_t sh_pi_corrected_detail_1;
	__uint64_t sh_pi_corrected_detail_2;
	__uint64_t sh_pi_corrected_detail_3;
	__uint64_t sh_pi_corrected_detail_4;
	__uint64_t sh_pi_cor_time_stamp;
	__uint64_t sh_mem_error_summary;
	__uint64_t sh_mem_error_overflow;
	__uint64_t sh_misc_err_hdr_lower;
	__uint64_t sh_misc_err_hdr_upper;
	__uint64_t sh_dir_uc_err_hdr_lower;
	__uint64_t sh_dir_uc_err_hdr_upper;
	__uint64_t sh_dir_cor_err_hdr_lower;
	__uint64_t sh_dir_cor_err_hdr_upper;
	__uint64_t sh_mem_error_mask;
	__uint64_t sh_md_uncor_time_stamp;
	__uint64_t sh_md_cor_time_stamp;
	__uint64_t sh_md_hw_time_stamp;
	__uint64_t sh_xn_error_summary;
	__uint64_t sh_xn_first_error;
	__uint64_t sh_xn_error_overflow;
	__uint64_t sh_xniilb_error_summary;
	__uint64_t sh_xniilb_first_error;
	__uint64_t sh_xniilb_error_overflow;
	__uint64_t sh_xniilb_error_detail_1;
	__uint64_t sh_xniilb_error_detail_2;
	__uint64_t sh_xniilb_error_detail_3;
	__uint64_t sh_xnpi_error_summary;
	__uint64_t sh_xnpi_first_error;
	__uint64_t sh_xnpi_error_overflow;
	__uint64_t sh_xnpi_error_detail_1;
	__uint64_t sh_xnmd_error_summary;
	__uint64_t sh_xnmd_first_error;
	__uint64_t sh_xnmd_error_overflow;
	__uint64_t sh_xnmd_ecc_err_report;
	__uint64_t sh_xnmd_error_detail_1;
	__uint64_t sh_lb_error_summary;
	__uint64_t sh_lb_first_error;
	__uint64_t sh_lb_error_overflow;
	__uint64_t sh_lb_error_detail_1;
	__uint64_t sh_lb_error_detail_2;
	__uint64_t sh_lb_error_detail_3;
	__uint64_t sh_lb_error_detail_4;
	__uint64_t sh_lb_error_detail_5;
} sal_log_shub_state_t;

typedef struct {
sal_log_section_hdr_t header;
	struct
	{
		__uint64_t    err_status      : 1,
		guid            : 1,
		oem_data        : 1,
		reserved        : 61;
	} valid;
	__uint64_t             err_status;
	efi_guid_t      guid;
	__uint64_t shub_nic;
	sal_log_shub_state_t    shub_state;
} sal_log_plat_info_t;


extern void sal_log_plat_print(int header_len, int sect_len, u8 *p_data, prfunc_t prfunc);

#ifdef platform_plat_specific_err_print
#undef platform_plat_specific_err_print
#endif
#define platform_plat_specific_err_print sal_log_plat_print

#endif /* CONFIG_IA64_SGI_SN */
