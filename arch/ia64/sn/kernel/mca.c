/*
 * File:	mca.c
 * Purpose:	SN specific MCA code.
 *
 * Copyright (C) 2001-2002 Silicon Graphics, Inc.  All Rights Reserved.
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

#include <linux/types.h>
#include <linux/init.h>
#include <linux/jiffies.h>
#include <linux/threads.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/smp_lock.h>
#include <linux/acpi.h>
#ifdef CONFIG_KDB
#include <linux/kdb.h>
#endif

#include <asm/machvec.h>
#include <asm/page.h>
#include <asm/ptrace.h>
#include <asm/system.h>
#include <asm/sal.h>
#include <asm/sn/sn_sal.h>
#include <asm/mca.h>
#include <asm/sn/mca.h>

#include <asm/irq.h>
#include <asm/hw_irq.h>
#include <asm/smp.h>
#include <asm/sn/sn_cpuid.h>

static char *shub_mmr_names[] = {
	"sh_event_occurred",
	"sh_first_error",
	"sh_event_overflow",

/* PI */
	"sh_pi_first_error",
	"sh_pi_error_summary",
	"sh_pi_error_overflow",

/* PI HW */
	"sh_pi_error_detail_1",
	"sh_pi_error_detail_2",
	"sh_pi_hw_time_stamp",

/* PI UCE */
	"sh_pi_uncorrected_detail_1",
	"sh_pi_uncorrected_detail_2",
	"sh_pi_uncorrected_detail_3",
	"sh_pi_uncorrected_detail_4",
	"sh_pi_uncor_time_stamp",

/* PI CE */
	"sh_pi_corrected_detail_1",
	"sh_pi_corrected_detail_2",
	"sh_pi_corrected_detail_3",
	"sh_pi_corrected_detail_4",
	"sh_pi_cor_time_stamp",

/* MD */
	"sh_mem_error_summary",
	"sh_mem_error_overflow",
/* MD HW */
	"sh_misc_err_hdr_upper",
	"sh_misc_err_hdr_lower",
	"sh_md_dqlp_mmr_xperr_val",
	"sh_md_dqlp_mmr_yperr_val",
	"sh_md_dqrp_mmr_xperr_val",
	"sh_md_dqrp_mmr_yperr_val",
	"sh_md_hw_time_stamp",

/* MD UCE */
	"sh_dir_uc_err_hdr_lower",
	"sh_dir_uc_err_hdr_upper",
	"sh_md_dqlp_mmr_xuerr1",
	"sh_md_dqlp_mmr_xuerr2",
	"sh_md_dqlp_mmr_yuerr1",
	"sh_md_dqlp_mmr_yuerr2",
	"sh_md_dqrp_mmr_xuerr1",
	"sh_md_dqrp_mmr_xuerr2",
	"sh_md_dqrp_mmr_yuerr1",
	"sh_md_dqrp_mmr_yuerr2",
	"sh_md_uncor_time_stamp",

/* MD CE */
	"sh_dir_cor_err_hdr_lower",
	"sh_dir_cor_err_hdr_upper",
	"sh_md_dqlp_mmr_xcerr1",
	"sh_md_dqlp_mmr_xcerr2",
	"sh_md_dqlp_mmr_ycerr1",
	"sh_md_dqlp_mmr_ycerr2",
	"sh_md_dqrp_mmr_xcerr1",
	"sh_md_dqrp_mmr_xcerr2",
	"sh_md_dqrp_mmr_ycerr1",
	"sh_md_dqrp_mmr_ycerr2",
	"sh_md_cor_time_stamp",

/* MD CE, UCE */
	"sh_md_dqls_mmr_xamopw_err",
	"sh_md_dqrs_mmr_yamopw_err",

/* XN */
	"sh_xn_error_summary",
	"sh_xn_first_error",
	"sh_xn_error_overflow",

/* XN HW */
	"sh_xniilb_error_summary",
	"sh_xniilb_first_error",
	"sh_xniilb_error_overflow",
	"sh_xniilb_error_detail_1",
	"sh_xniilb_error_detail_2",
	"sh_xniilb_error_detail_3",

	"sh_ni0_error_summary_1",
	"sh_ni0_first_error_1",
	"sh_ni0_error_overflow_1",

	"sh_ni0_error_summary_2",
	"sh_ni0_first_error_2",
	"sh_ni0_error_overflow_2",
	"sh_ni0_error_detail_1",
	"sh_ni0_error_detail_2",
	"sh_ni0_error_detail_3",

	"sh_ni1_error_summary_1",
	"sh_ni1_first_error_1",
	"sh_ni1_error_overflow_1",

	"sh_ni1_error_summary_2",
	"sh_ni1_first_error_2",
	"sh_ni1_error_overflow_2",

	"sh_ni1_error_detail_1",
	"sh_ni1_error_detail_2",
	"sh_ni1_error_detail_3",

	"sh_xn_hw_time_stamp",

/* XN HW & UCE & SBE */
	"sh_xnpi_error_summary",
	"sh_xnpi_first_error",
	"sh_xnpi_error_overflow",
	"sh_xnpi_error_detail_1",

	"sh_xnmd_error_summary",
	"sh_xnmd_first_error",
	"sh_xnmd_error_overflow",
	"sh_xnmd_ecc_err_report",
	"sh_xnmd_error_detail_1",

/* XN UCE */
	"sh_xn_uncorrected_detail_1",
	"sh_xn_uncorrected_detail_2",
	"sh_xn_uncorrected_detail_3",
	"sh_xn_uncorrected_detail_4",
	"sh_xn_uncor_time_stamp",

/* XN CE */
	"sh_xn_corrected_detail_1",
	"sh_xn_corrected_detail_2",
	"sh_xn_corrected_detail_3",
	"sh_xn_corrected_detail_4",
	"sh_xn_cor_time_stamp",

/* LB HW */
	"sh_lb_error_summary",
	"sh_lb_first_error",
	"sh_lb_error_overflow",
	"sh_lb_error_detail_1",
	"sh_lb_error_detail_2",
	"sh_lb_error_detail_3",
	"sh_lb_error_detail_4",
	"sh_lb_error_detail_5",
	"sh_junk_error_status",
};

void 
sal_log_plat_print(int header_len, int sect_len, u8 *p_data, prfunc_t prfunc) 
{
	sal_log_plat_info_t *sh_info = (sal_log_plat_info_t *) p_data;
	u64 *mmr_val = (u64 *)&(sh_info->shub_state);
	char **mmr_name = shub_mmr_names;
	int mmr_count = sizeof(sal_log_shub_state_t)>>3;

	while(mmr_count) {
		if(*mmr_val) {
			prfunc("%-40s: %#016lx\n",*mmr_name, *mmr_val);
		}
		mmr_name++;
		mmr_val++;
		mmr_count--;
	}

}

void
sn_cpei_handler(int irq, void *devid, struct pt_regs *regs) {

        struct ia64_sal_retval isrv;
// this function's sole purpose is to call SAL when we receive
// a CE interrupt from SHUB or when the timer routine decides
// we need to call SAL to check for CEs.

        // CALL SAL_LOG_CE
        SAL_CALL(isrv, SN_SAL_LOG_CE, irq, 0, 0, 0, 0, 0, 0);
}

#include <linux/timer.h>

#define CPEI_INTERVAL   (HZ/100)
struct timer_list sn_cpei_timer = TIMER_INITIALIZER(NULL, 0, 0);
void sn_init_cpei_timer(void);

void
sn_cpei_timer_handler(unsigned long dummy) {
        sn_cpei_handler(-1, NULL, NULL);
        del_timer(&sn_cpei_timer);
        sn_cpei_timer.expires = jiffies + CPEI_INTERVAL;
        add_timer(&sn_cpei_timer);
}

void
sn_init_cpei_timer() {
        sn_cpei_timer.expires = jiffies + CPEI_INTERVAL;
        sn_cpei_timer.function = sn_cpei_timer_handler;
        add_timer(&sn_cpei_timer);
}

#ifdef ajmtestceintr

struct timer_list sn_ce_timer;

void
sn_ce_timer_handler(long dummy) {
        unsigned long *pi_ce_error_inject_reg = 0xc00000092fffff00;

        *pi_ce_error_inject_reg = 0x0000000000000100;
        del_timer(&sn_ce_timer);
        sn_ce_timer.expires = jiffies + CPEI_INTERVAL;
        add_timer(&sn_ce_timer);
}

sn_init_ce_timer() {
        sn_ce_timer.expires = jiffies + CPEI_INTERVAL;
        sn_ce_timer.function = sn_ce_timer_handler;
        add_timer(&sn_ce_timer);
}
#endif /* ajmtestceintr */
