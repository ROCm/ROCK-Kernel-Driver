/*
 * This file contains the HP SKI Simulator PMU register description tables
 * and pmc checkers used by perfmon.c.
 *
 * Copyright (C) 2002-2003  Hewlett Packard Co
 *               Stephane Eranian <eranian@hpl.hp.com>
 *
 * File mostly contributed by Ian Wienand <ianw@gelato.unsw.edu.au>
 *
 * This file is included as a dummy template so the kernel does not
 * try to initalize registers the simulator can't handle.
 *
 * Note the simulator does not (currently) implement these registers, i.e.,
 * they do not count anything. But you can read/write them.
 */

static pfm_reg_desc_t pfm_hpsim_pmc_desc[PMU_MAX_PMCS]={
/* pmc0  */ { PFM_REG_CONTROL , 0, 0x1UL, -1UL, NULL, NULL, {0UL, 0UL, 0UL, 0UL}, {0UL,0UL, 0UL, 0UL}},
/* pmc1  */ { PFM_REG_CONTROL , 0, 0x0UL, -1UL, NULL, NULL, {0UL, 0UL, 0UL, 0UL}, {0UL,0UL, 0UL, 0UL}},
/* pmc2  */ { PFM_REG_CONTROL , 0, 0x0UL, -1UL, NULL, NULL, {0UL, 0UL, 0UL, 0UL}, {0UL,0UL, 0UL, 0UL}},
/* pmc3  */ { PFM_REG_CONTROL , 0, 0x0UL, -1UL, NULL, NULL, {0UL, 0UL, 0UL, 0UL}, {0UL,0UL, 0UL, 0UL}},
/* pmc4  */ { PFM_REG_COUNTING, 0, 0x0UL, -1UL, NULL, NULL, {RDEP(4), 0UL, 0UL, 0UL}, {0UL,0UL, 0UL, 0UL}},
/* pmc5  */ { PFM_REG_COUNTING, 0, 0x0UL, -1UL, NULL, NULL, {RDEP(5), 0UL, 0UL, 0UL}, {0UL,0UL, 0UL, 0UL}},
/* pmc6  */ { PFM_REG_COUNTING, 0, 0x0UL, -1UL, NULL, NULL, {RDEP(6), 0UL, 0UL, 0UL}, {0UL,0UL, 0UL, 0UL}},
/* pmc7  */ { PFM_REG_COUNTING, 0, 0x0UL, -1UL, NULL, NULL, {RDEP(7), 0UL, 0UL, 0UL}, {0UL,0UL, 0UL, 0UL}},
/* pmc8  */ { PFM_REG_COUNTING, 0, 0x0UL, -1UL, NULL, NULL, {RDEP(8), 0UL, 0UL, 0UL}, {0UL,0UL, 0UL, 0UL}},
/* pmc9  */ { PFM_REG_COUNTING, 0, 0x0UL, -1UL, NULL, NULL, {RDEP(9), 0UL, 0UL, 0UL}, {0UL,0UL, 0UL, 0UL}},
/* pmc10 */ { PFM_REG_COUNTING, 0, 0x0UL, -1UL, NULL, NULL, {RDEP(10), 0UL, 0UL, 0UL}, {0UL,0UL, 0UL, 0UL}},
/* pmc11 */ { PFM_REG_COUNTING, 0, 0x0UL, -1UL, NULL, NULL, {RDEP(11), 0UL, 0UL, 0UL}, {0UL,0UL, 0UL, 0UL}},
/* pmc12 */ { PFM_REG_COUNTING, 0, 0x0UL, -1UL, NULL, NULL, {RDEP(12), 0UL, 0UL, 0UL}, {0UL,0UL, 0UL, 0UL}},
/* pmc13 */ { PFM_REG_COUNTING, 0, 0x0UL, -1UL, NULL, NULL, {RDEP(13), 0UL, 0UL, 0UL}, {0UL,0UL, 0UL, 0UL}},
/* pmc14 */ { PFM_REG_COUNTING, 0, 0x0UL, -1UL, NULL, NULL, {RDEP(14), 0UL, 0UL, 0UL}, {0UL,0UL, 0UL, 0UL}},
/* pmc15 */ { PFM_REG_COUNTING, 0, 0x0UL, -1UL, NULL, NULL, {RDEP(15), 0UL, 0UL, 0UL}, {0UL,0UL, 0UL, 0UL}},
	    { PFM_REG_END     , 0, 0x0UL, -1UL, NULL, NULL, {0,}, {0,}}, /* end marker */
};

static pfm_reg_desc_t pfm_hpsim_pmd_desc[PMU_MAX_PMDS]={
/* pmd0  */ { PFM_REG_BUFFER, 0, 0x0UL, -1UL, NULL, NULL, {0UL,0UL, 0UL, 0UL}, {0UL,0UL, 0UL, 0UL}},
/* pmd1  */ { PFM_REG_BUFFER, 0, 0x0UL, -1UL, NULL, NULL, {0UL,0UL, 0UL, 0UL}, {0UL,0UL, 0UL, 0UL}},
/* pmd2  */ { PFM_REG_BUFFER, 0, 0x0UL, -1UL, NULL, NULL, {0UL,0UL, 0UL, 0UL}, {0UL,0UL, 0UL, 0UL}},
/* pmd3  */ { PFM_REG_BUFFER, 0, 0x0UL, -1UL, NULL, NULL, {0UL,0UL, 0UL, 0UL}, {0UL,0UL, 0UL, 0UL}},
/* pmd4  */ { PFM_REG_COUNTING, 0, 0x0UL, -1UL, NULL, NULL, {0UL,0UL, 0UL, 0UL}, {RDEP(4),0UL, 0UL, 0UL}},
/* pmd5  */ { PFM_REG_COUNTING, 0, 0x0UL, -1UL, NULL, NULL, {0UL,0UL, 0UL, 0UL}, {RDEP(5),0UL, 0UL, 0UL}},
/* pmd6  */ { PFM_REG_COUNTING, 0, 0x0UL, -1UL, NULL, NULL, {0UL,0UL, 0UL, 0UL}, {RDEP(6),0UL, 0UL, 0UL}},
/* pmd7  */ { PFM_REG_COUNTING, 0, 0x0UL, -1UL, NULL, NULL, {0UL,0UL, 0UL, 0UL}, {RDEP(7),0UL, 0UL, 0UL}},
/* pmd8  */ { PFM_REG_COUNTING, 0, 0x0UL, -1UL, NULL, NULL, {0UL,0UL, 0UL, 0UL}, {RDEP(8),0UL, 0UL, 0UL}},
/* pmd9  */ { PFM_REG_COUNTING, 0, 0x0UL, -1UL, NULL, NULL, {0UL,0UL, 0UL, 0UL}, {RDEP(9),0UL, 0UL, 0UL}},
/* pmd10 */ { PFM_REG_COUNTING, 0, 0x0UL, -1UL, NULL, NULL, {0UL,0UL, 0UL, 0UL}, {RDEP(10),0UL, 0UL, 0UL}},
/* pmd11 */ { PFM_REG_COUNTING, 0, 0x0UL, -1UL, NULL, NULL, {0UL,0UL, 0UL, 0UL}, {RDEP(11),0UL, 0UL, 0UL}},
/* pmd12 */ { PFM_REG_COUNTING, 0, 0x0UL, -1UL, NULL, NULL, {0UL,0UL, 0UL, 0UL}, {RDEP(12),0UL, 0UL, 0UL}},
/* pmd13 */ { PFM_REG_COUNTING, 0, 0x0UL, -1UL, NULL, NULL, {0UL,0UL, 0UL, 0UL}, {RDEP(13),0UL, 0UL, 0UL}},
/* pmd14 */ { PFM_REG_COUNTING, 0, 0x0UL, -1UL, NULL, NULL, {0UL,0UL, 0UL, 0UL}, {RDEP(14),0UL, 0UL, 0UL}},
/* pmd15 */ { PFM_REG_COUNTING, 0, 0x0UL, -1UL, NULL, NULL, {0UL,0UL, 0UL, 0UL}, {RDEP(15),0UL, 0UL, 0UL}},
	    { PFM_REG_END     , 0, 0x0UL, -1UL, NULL, NULL, {0,}, {0,}}, /* end marker */
};

static int
pfm_hpsim_probe(void)
{
	return local_cpu_data->family == 0x7 && ia64_platform_is("hpsim") ? 0 : -1;
}

/*
 * impl_pmcs, impl_pmds are computed at runtime to minimize errors!
 */
static pmu_config_t pmu_conf_hpsim={
	.pmu_name   = "hpsim",
	.pmu_family = 0x7, /* ski emulator reports as Itanium */
	.ovfl_val   = (1UL << 32) - 1,
	.num_ibrs   = 0, /* does not use */
	.num_dbrs   = 0, /* does not use */
	.pmd_desc   = pfm_hpsim_pmd_desc,
	.pmc_desc   = pfm_hpsim_pmc_desc,
	.probe      = pfm_hpsim_probe
};
