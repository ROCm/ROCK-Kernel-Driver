/*
 * This file contains the McKinley PMU register description tables
 * and pmc checker used by perfmon.c.
 *
 * Copyright (C) 2002  Hewlett Packard Co
 *               Stephane Eranian <eranian@hpl.hp.com>
 */

#define RDEP(x)	(1UL<<(x))

#ifndef CONFIG_MCKINLEY
#error "This file is only valid when CONFIG_MCKINLEY is defined"
#endif

static int pfm_mck_pmc_check(struct task_struct *task, unsigned int cnum, unsigned long *val, struct pt_regs *regs);
static int pfm_write_ibr_dbr(int mode, struct task_struct *task, void *arg, int count, struct pt_regs *regs);

static pfm_reg_desc_t pmc_desc[256]={
/* pmc0  */ { PFM_REG_CONTROL, 0, NULL, NULL, {0UL,0UL, 0UL, 0UL}, {0UL,0UL, 0UL, 0UL}},
/* pmc1  */ { PFM_REG_CONTROL, 0, NULL, NULL, {0UL,0UL, 0UL, 0UL}, {0UL,0UL, 0UL, 0UL}},
/* pmc2  */ { PFM_REG_CONTROL, 0, NULL, NULL, {0UL,0UL, 0UL, 0UL}, {0UL,0UL, 0UL, 0UL}},
/* pmc3  */ { PFM_REG_CONTROL, 0, NULL, NULL, {0UL,0UL, 0UL, 0UL}, {0UL,0UL, 0UL, 0UL}},
/* pmc4  */ { PFM_REG_COUNTING, 6, NULL, pfm_mck_pmc_check, {RDEP(4),0UL, 0UL, 0UL}, {0UL,0UL, 0UL, 0UL}},
/* pmc5  */ { PFM_REG_COUNTING, 6, NULL, NULL, {RDEP(5),0UL, 0UL, 0UL}, {0UL,0UL, 0UL, 0UL}},
/* pmc6  */ { PFM_REG_COUNTING, 6, NULL, NULL, {RDEP(6),0UL, 0UL, 0UL}, {0UL,0UL, 0UL, 0UL}},
/* pmc7  */ { PFM_REG_COUNTING, 6, NULL, NULL, {RDEP(7),0UL, 0UL, 0UL}, {0UL,0UL, 0UL, 0UL}},
/* pmc8  */ { PFM_REG_CONFIG, 0, NULL, pfm_mck_pmc_check, {0UL,0UL, 0UL, 0UL}, {0UL,0UL, 0UL, 0UL}},
/* pmc9  */ { PFM_REG_CONFIG, 0, NULL, NULL, {0UL,0UL, 0UL, 0UL}, {0UL,0UL, 0UL, 0UL}},
/* pmc10 */ { PFM_REG_MONITOR, 4, NULL, NULL, {RDEP(0)|RDEP(1),0UL, 0UL, 0UL}, {0UL,0UL, 0UL, 0UL}},
/* pmc11 */ { PFM_REG_MONITOR, 6, NULL, NULL, {RDEP(2)|RDEP(3)|RDEP(17),0UL, 0UL, 0UL}, {0UL,0UL, 0UL, 0UL}},
/* pmc12 */ { PFM_REG_MONITOR, 6, NULL, NULL, {RDEP(8)|RDEP(9)|RDEP(10)|RDEP(11)|RDEP(12)|RDEP(13)|RDEP(14)|RDEP(15)|RDEP(16),0UL, 0UL, 0UL}, {0UL,0UL, 0UL, 0UL}},
/* pmc13 */ { PFM_REG_CONFIG, 0, NULL, pfm_mck_pmc_check, {0UL,0UL, 0UL, 0UL}, {0UL,0UL, 0UL, 0UL}},
/* pmc14 */ { PFM_REG_CONFIG, 0, NULL, pfm_mck_pmc_check, {0UL,0UL, 0UL, 0UL}, {0UL,0UL, 0UL, 0UL}},
/* pmc15 */ { PFM_REG_CONFIG, 0, NULL, NULL, {0UL,0UL, 0UL, 0UL}, {0UL,0UL, 0UL, 0UL}},
	    { PFM_REG_NONE, 0, NULL, NULL, {0,}, {0,}}, /* end marker */
};

static pfm_reg_desc_t pmd_desc[256]={
/* pmd0  */ { PFM_REG_BUFFER, 0, NULL, NULL, {RDEP(1),0UL, 0UL, 0UL}, {RDEP(10),0UL, 0UL, 0UL}},
/* pmd1  */ { PFM_REG_BUFFER, 0, NULL, NULL, {RDEP(0),0UL, 0UL, 0UL}, {RDEP(10),0UL, 0UL, 0UL}},
/* pmd2  */ { PFM_REG_BUFFER, 0, NULL, NULL, {RDEP(3)|RDEP(17),0UL, 0UL, 0UL}, {RDEP(11),0UL, 0UL, 0UL}},
/* pmd3  */ { PFM_REG_BUFFER, 0, NULL, NULL, {RDEP(2)|RDEP(17),0UL, 0UL, 0UL}, {RDEP(11),0UL, 0UL, 0UL}},
/* pmd4  */ { PFM_REG_COUNTING, 0, NULL, NULL, {0UL,0UL, 0UL, 0UL}, {RDEP(4),0UL, 0UL, 0UL}},
/* pmd5  */ { PFM_REG_COUNTING, 0, NULL, NULL, {0UL,0UL, 0UL, 0UL}, {RDEP(5),0UL, 0UL, 0UL}},
/* pmd6  */ { PFM_REG_COUNTING, 0, NULL, NULL, {0UL,0UL, 0UL, 0UL}, {RDEP(6),0UL, 0UL, 0UL}},
/* pmd7  */ { PFM_REG_COUNTING, 0, NULL, NULL, {0UL,0UL, 0UL, 0UL}, {RDEP(7),0UL, 0UL, 0UL}},
/* pmd8  */ { PFM_REG_BUFFER, 0, NULL, NULL, {RDEP(9)|RDEP(10)|RDEP(11)|RDEP(12)|RDEP(13)|RDEP(14)|RDEP(15)|RDEP(16),0UL, 0UL, 0UL}, {RDEP(12),0UL, 0UL, 0UL}},
/* pmd9  */ { PFM_REG_BUFFER, 0, NULL, NULL, {RDEP(8)|RDEP(10)|RDEP(11)|RDEP(12)|RDEP(13)|RDEP(14)|RDEP(15)|RDEP(16),0UL, 0UL, 0UL}, {RDEP(12),0UL, 0UL, 0UL}},
/* pmd10 */ { PFM_REG_BUFFER, 0, NULL, NULL, {RDEP(8)|RDEP(9)|RDEP(11)|RDEP(12)|RDEP(13)|RDEP(14)|RDEP(15)|RDEP(16),0UL, 0UL, 0UL}, {RDEP(12),0UL, 0UL, 0UL}},
/* pmd11 */ { PFM_REG_BUFFER, 0, NULL, NULL, {RDEP(8)|RDEP(9)|RDEP(10)|RDEP(12)|RDEP(13)|RDEP(14)|RDEP(15)|RDEP(16),0UL, 0UL, 0UL}, {RDEP(12),0UL, 0UL, 0UL}},
/* pmd12 */ { PFM_REG_BUFFER, 0, NULL, NULL, {RDEP(8)|RDEP(9)|RDEP(10)|RDEP(11)|RDEP(13)|RDEP(14)|RDEP(15)|RDEP(16),0UL, 0UL, 0UL}, {RDEP(12),0UL, 0UL, 0UL}},
/* pmd13 */ { PFM_REG_BUFFER, 0, NULL, NULL, {RDEP(8)|RDEP(9)|RDEP(10)|RDEP(11)|RDEP(12)|RDEP(14)|RDEP(15)|RDEP(16),0UL, 0UL, 0UL}, {RDEP(12),0UL, 0UL, 0UL}},
/* pmd14 */ { PFM_REG_BUFFER, 0, NULL, NULL, {RDEP(8)|RDEP(9)|RDEP(10)|RDEP(11)|RDEP(12)|RDEP(13)|RDEP(15)|RDEP(16),0UL, 0UL, 0UL}, {RDEP(12),0UL, 0UL, 0UL}},
/* pmd15 */ { PFM_REG_BUFFER, 0, NULL, NULL, {RDEP(8)|RDEP(9)|RDEP(10)|RDEP(11)|RDEP(12)|RDEP(13)|RDEP(14)|RDEP(16),0UL, 0UL, 0UL}, {RDEP(12),0UL, 0UL, 0UL}},
/* pmd16 */ { PFM_REG_BUFFER, 0, NULL, NULL, {RDEP(8)|RDEP(9)|RDEP(10)|RDEP(11)|RDEP(12)|RDEP(13)|RDEP(14)|RDEP(15),0UL, 0UL, 0UL}, {RDEP(12),0UL, 0UL, 0UL}},
/* pmd17 */ { PFM_REG_BUFFER, 0, NULL, NULL, {RDEP(2)|RDEP(3),0UL, 0UL, 0UL}, {RDEP(11),0UL, 0UL, 0UL}},
	    { PFM_REG_NONE, 0, NULL, NULL, {0,}, {0,}}, /* end marker */
};

static int
pfm_mck_pmc_check(struct task_struct *task, unsigned int cnum, unsigned long *val, struct pt_regs *regs)
{
	struct thread_struct *th = &task->thread;
	pfm_context_t *ctx = task->thread.pfm_context;
	int ret = 0, check_case1 = 0;
	unsigned long val8 = 0, val14 = 0, val13 = 0;

	/*
	 * we must clear the debug registers if any pmc13.ena_dbrpX bit is enabled 
	 * before they are written (fl_using_dbreg==0) to avoid picking up stale information. 
	 */
	if (cnum == 13 && (*val & (0xfUL << 45)) && ctx->ctx_fl_using_dbreg == 0) {

		/* don't mix debug with perfmon */
		if ((task->thread.flags & IA64_THREAD_DBG_VALID) != 0) return -EINVAL;

		/* 
		 * a count of 0 will mark the debug registers as in use and also
		 * ensure that they are properly cleared.
		 */
		ret = pfm_write_ibr_dbr(1, task, NULL, 0, regs);
		if (ret) return ret;
	}
	/* 
	 * we must clear the (instruction) debug registers if any pmc14.ibrpX bit is enabled 
	 * before they are (fl_using_dbreg==0) to avoid picking up stale information. 
	 */
	if (cnum == 14 && ((*val & 0x2222) != 0x2222) && ctx->ctx_fl_using_dbreg == 0) {

		/* don't mix debug with perfmon */
		if ((task->thread.flags & IA64_THREAD_DBG_VALID) != 0) return -EINVAL;

		/* 
		 * a count of 0 will mark the debug registers as in use and also
		 * ensure that they are properly cleared.
		 */
		ret = pfm_write_ibr_dbr(0, task, NULL, 0, regs);
		if (ret) return ret;

	}

	switch(cnum) {
		case  4: *val |= 1UL << 23; /* force power enable bit */
			 break;
		case  8: val8 = *val;
			 val13 = th->pmc[13];
			 val14 = th->pmc[14];
			 check_case1 = 1;
			 break;
		case 13: val8  = th->pmc[8];
			 val13 = *val;
			 val14 = th->pmc[14];
			 check_case1 = 1;
			 break;
		case 14: val8  = th->pmc[13];
			 val13 = th->pmc[13];
			 val14 = *val;
			 check_case1 = 1;
			 break;
	}
	/* check illegal configuration which can produce inconsistencies in tagging
	 * i-side events in L1D and L2 caches
	 */
	if (check_case1) {
		ret =   ((val13 >> 45) & 0xf) == 0 
		   && ((val8 & 0x1) == 0)
		   && ((((val14>>1) & 0x3) == 0x2 || ((val14>>1) & 0x3) == 0x0)
		       ||(((val14>>4) & 0x3) == 0x2 || ((val14>>4) & 0x3) == 0x0));

		if (ret) printk("perfmon: failure check_case1\n");
	}

	return ret ? -EINVAL : 0;
}
