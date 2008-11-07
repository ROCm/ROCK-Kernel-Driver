/*
 * This file contains the Cell PMU register description tables
 * and pmc checker used by perfmon.c.
 *
 * Copyright IBM Corporation 2007
 * (C) Copyright 2007 TOSHIBA CORPORATION
 *
 * Based on other Perfmon2 PMU modules.
 * Copyright (c) 2005-2006 Hewlett-Packard Development Company, L.P.
 * Contributed by Stephane Eranian <eranian@hpl.hp.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA
 * 02111-1307 USA
 */

#include <linux/module.h>
#include <linux/perfmon_kern.h>
#include <linux/io.h>
#include <asm/cell-pmu.h>
#include <asm/cell-regs.h>
#include <asm/machdep.h>
#include <asm/rtas.h>
#include <asm/ps3.h>
#include <asm/spu.h>

MODULE_AUTHOR("Kevin Corry <kevcorry@us.ibm.com>, "
	      "Carl Love <carll@us.ibm.com>");
MODULE_DESCRIPTION("Cell PMU description table");
MODULE_LICENSE("GPL");

struct pfm_cell_platform_pmu_info {
	u32  (*read_ctr)(u32 cpu, u32 ctr);
	void (*write_ctr)(u32 cpu, u32 ctr, u32 val);
	void (*write_pm07_control)(u32 cpu, u32 ctr, u32 val);
	void (*write_pm)(u32 cpu, enum pm_reg_name reg, u32 val);
	void (*enable_pm)(u32 cpu);
	void (*disable_pm)(u32 cpu);
	void (*enable_pm_interrupts)(u32 cpu, u32 thread, u32 mask);
	u32  (*get_and_clear_pm_interrupts)(u32 cpu);
	u32  (*get_hw_thread_id)(int cpu);
	struct cbe_ppe_priv_regs __iomem *(*get_cpu_ppe_priv_regs)(int cpu);
	struct cbe_pmd_regs __iomem *(*get_cpu_pmd_regs)(int cpu);
	struct cbe_mic_tm_regs __iomem *(*get_cpu_mic_tm_regs)(int cpu);
	int (*rtas_token)(const char *service);
	int (*rtas_call)(int token, int param1, int param2, int *param3, ...);
};

/*
 * Mapping from Perfmon logical control registers to Cell hardware registers.
 */
static struct pfm_regmap_desc pfm_cell_pmc_desc[] = {
	/* Per-counter control registers. */
	PMC_D(PFM_REG_I, "pm0_control",       0, 0, 0, 0),
	PMC_D(PFM_REG_I, "pm1_control",       0, 0, 0, 0),
	PMC_D(PFM_REG_I, "pm2_control",       0, 0, 0, 0),
	PMC_D(PFM_REG_I, "pm3_control",       0, 0, 0, 0),
	PMC_D(PFM_REG_I, "pm4_control",       0, 0, 0, 0),
	PMC_D(PFM_REG_I, "pm5_control",       0, 0, 0, 0),
	PMC_D(PFM_REG_I, "pm6_control",       0, 0, 0, 0),
	PMC_D(PFM_REG_I, "pm7_control",       0, 0, 0, 0),

	/* Per-counter RTAS arguments. Each of these registers has three fields.
	 *   bits 63-48: debug-bus word
	 *   bits 47-32: sub-unit
	 *   bits 31-0 : full signal number
	 *   (MSB = 63, LSB = 0)
	 */
	PMC_D(PFM_REG_I, "pm0_event",         0, 0, 0, 0),
	PMC_D(PFM_REG_I, "pm1_event",         0, 0, 0, 0),
	PMC_D(PFM_REG_I, "pm2_event",         0, 0, 0, 0),
	PMC_D(PFM_REG_I, "pm3_event",         0, 0, 0, 0),
	PMC_D(PFM_REG_I, "pm4_event",         0, 0, 0, 0),
	PMC_D(PFM_REG_I, "pm5_event",         0, 0, 0, 0),
	PMC_D(PFM_REG_I, "pm6_event",         0, 0, 0, 0),
	PMC_D(PFM_REG_I, "pm7_event",         0, 0, 0, 0),

	/* Global control registers. Same order as enum pm_reg_name. */
	PMC_D(PFM_REG_I, "group_control",     0, 0, 0, 0),
	PMC_D(PFM_REG_I, "debug_bus_control", 0, 0, 0, 0),
	PMC_D(PFM_REG_I, "trace_address",     0, 0, 0, 0),
	PMC_D(PFM_REG_I, "ext_trace_timer",   0, 0, 0, 0),
	PMC_D(PFM_REG_I, "pm_status",         0, 0, 0, 0),
	/* set the interrupt overflow bit for the four 32 bit counters
	 * that is currently supported.  Will need to fix when 32 and 16
	 * bit counters are supported.
	 */
	PMC_D(PFM_REG_I, "pm_control",        0xF0000000, 0xF0000000, 0, 0),
	PMC_D(PFM_REG_I, "pm_interval",       0, 0, 0, 0), /* FIX: Does user-space also need read access to this one? */
	PMC_D(PFM_REG_I, "pm_start_stop",     0, 0, 0, 0),
};
#define PFM_PM_NUM_PMCS	ARRAY_SIZE(pfm_cell_pmc_desc)

#define CELL_PMC_GROUP_CONTROL    16
#define CELL_PMC_PM_STATUS        20
#define CELL_PMC_PM_CONTROL       21
#define CELL_PMC_PM_CONTROL_CNTR_MASK   0x01E00000UL
#define CELL_PMC_PM_CONTROL_CNTR_16     0x01E00000UL

/*
 * Mapping from Perfmon logical data counters to Cell hardware counters.
 */
static struct pfm_regmap_desc pfm_cell_pmd_desc[] = {
	PMD_D(PFM_REG_C, "pm0", 0),
	PMD_D(PFM_REG_C, "pm1", 0),
	PMD_D(PFM_REG_C, "pm2", 0),
	PMD_D(PFM_REG_C, "pm3", 0),
	PMD_D(PFM_REG_C, "pm4", 0),
	PMD_D(PFM_REG_C, "pm5", 0),
	PMD_D(PFM_REG_C, "pm6", 0),
	PMD_D(PFM_REG_C, "pm7", 0),
};
#define PFM_PM_NUM_PMDS	ARRAY_SIZE(pfm_cell_pmd_desc)

#define PFM_EVENT_PMC_BUS_WORD(x)      (((x) >> 48) & 0x00ff)
#define PFM_EVENT_PMC_FULL_SIGNAL_NUMBER(x) ((x) & 0xffffffff)
#define PFM_EVENT_PMC_SIGNAL_GROUP(x) (((x) & 0xffffffff) / 100)
#define PFM_PM_CTR_INPUT_MUX_BIT(pm07_control) (((pm07_control) >> 26) & 0x1f)
#define PFM_PM_CTR_INPUT_MUX_GROUP_INDEX(pm07_control) ((pm07_control) >> 31)
#define PFM_GROUP_CONTROL_GROUP0_WORD(grp_ctrl) ((grp_ctrl) >> 30)
#define PFM_GROUP_CONTROL_GROUP1_WORD(grp_ctrl) (((grp_ctrl) >> 28) & 0x3)
#define PFM_NUM_OF_GROUPS 2
#define PFM_PPU_IU1_THREAD1_BASE_BIT 19
#define PFM_PPU_XU_THREAD1_BASE_BIT  16
#define PFM_COUNTER_CTRL_PMC_PPU_TH0 0x100000000ULL
#define PFM_COUNTER_CTRL_PMC_PPU_TH1 0x200000000ULL

/*
 * Debug-bus signal handling.
 *
 * Some Cell systems have firmware that can handle the debug-bus signal
 * routing. For systems without this firmware, we have a minimal in-kernel
 * implementation as well.
 */

/* The firmware only sees physical CPUs, so divide by 2 if SMT is on. */
#ifdef CONFIG_SCHED_SMT
#define RTAS_CPU(cpu) ((cpu) / 2)
#else
#define RTAS_CPU(cpu) (cpu)
#endif
#define RTAS_BUS_WORD(x)      (u16)(((x) >> 48) & 0x0000ffff)
#define RTAS_SUB_UNIT(x)      (u16)(((x) >> 32) & 0x0000ffff)
#define RTAS_SIGNAL_NUMBER(x) (s32)( (x)        & 0xffffffff)
#define RTAS_SIGNAL_GROUP(x)  (RTAS_SIGNAL_NUMBER(x) / 100)

#define subfunc_RESET		1
#define subfunc_ACTIVATE	2

#define passthru_ENABLE		1
#define passthru_DISABLE	2

/**
 * struct cell_rtas_arg
 *
 * @cpu: Processor to modify. Linux numbers CPUs based on SMT IDs, but the
 *       firmware only sees the physical CPUs. So this value should be the
 *       SMT ID (from smp_processor_id() or get_cpu()) divided by 2.
 * @sub_unit: Hardware subunit this applies to (if applicable).
 * @signal_group: Signal group to enable/disable on the trace bus.
 * @bus_word: For signal groups that propagate via the trace bus, this trace
 *            bus word will be used. This is a mask of (1 << TraceBusWord).
 *            For other signal groups, this specifies the trigger or event bus.
 * @bit: Trigger/Event bit, if applicable for the signal group.
 *
 * An array of these structures are passed to rtas_call() to set up the
 * signals on the debug bus.
 **/
struct cell_rtas_arg {
	u16 cpu;
	u16 sub_unit;
	s16 signal_group;
	u8 bus_word;
	u8 bit;
};

/**
 * rtas_reset_signals
 *
 * Use the firmware RTAS call to disable signal pass-thru and to reset the
 * debug-bus signals.
 **/
static int rtas_reset_signals(u32 cpu)
{
	struct cell_rtas_arg signal;
	u64 real_addr = virt_to_phys(&signal);
	int rc;
	struct pfm_cell_platform_pmu_info *info =
		((struct pfm_arch_pmu_info *)
		 (pfm_pmu_conf->pmu_info))->platform_info;

	memset(&signal, 0, sizeof(signal));
	signal.cpu = RTAS_CPU(cpu);
	rc = info->rtas_call(info->rtas_token("ibm,cbe-perftools"),
		       5, 1, NULL,
		       subfunc_RESET,
		       passthru_DISABLE,
		       real_addr >> 32,
		       real_addr & 0xffffffff,
		       sizeof(signal));

	return rc;
}

/**
 * rtas_activate_signals
 *
 * Use the firmware RTAS call to enable signal pass-thru and to activate the
 * desired signal groups on the debug-bus.
 **/
static int rtas_activate_signals(struct cell_rtas_arg *signals,
				 int num_signals)
{
	u64 real_addr = virt_to_phys(signals);
	int rc;
	struct pfm_cell_platform_pmu_info *info =
		((struct pfm_arch_pmu_info *)
		 (pfm_pmu_conf->pmu_info))->platform_info;

	rc = info->rtas_call(info->rtas_token("ibm,cbe-perftools"),
		       5, 1, NULL,
		       subfunc_ACTIVATE,
		       passthru_ENABLE,
		       real_addr >> 32,
		       real_addr & 0xffffffff,
		       num_signals * sizeof(*signals));

	return rc;
}

#define HID1_RESET_MASK			(~0x00000001ffffffffUL)
#define PPU_IU1_WORD0_HID1_EN_MASK	(~0x00000001f0c0802cUL)
#define PPU_IU1_WORD0_HID1_EN_WORD	( 0x00000001f0400000UL)
#define PPU_IU1_WORD1_HID1_EN_MASK	(~0x000000010fc08023UL)
#define PPU_IU1_WORD1_HID1_EN_WORD	( 0x000000010f400001UL)
#define PPU_XU_WORD0_HID1_EN_MASK	(~0x00000001f038402cUL)
#define PPU_XU_WORD0_HID1_EN_WORD	( 0x00000001f0080008UL)
#define PPU_XU_WORD1_HID1_EN_MASK	(~0x000000010f074023UL)
#define PPU_XU_WORD1_HID1_EN_WORD	( 0x000000010f030002UL)

/* The bus_word field in the cell_rtas_arg structure is a bit-mask
 * indicating which debug-bus word(s) to use.
 */
enum {
	BUS_WORD_0 = 1,
	BUS_WORD_1 = 2,
	BUS_WORD_2 = 4,
	BUS_WORD_3 = 8,
};

/* Definitions of the signal-groups that the built-in signal-activation
 * code can handle.
 */
enum {
	SIG_GROUP_NONE = 0,

	/* 2.x PowerPC Processor Unit (PPU) Signal Groups */
	SIG_GROUP_PPU_BASE = 20,
	SIG_GROUP_PPU_IU1 = 21,
	SIG_GROUP_PPU_XU = 22,

	/* 3.x PowerPC Storage Subsystem (PPSS) Signal Groups */
	SIG_GROUP_PPSS_BASE = 30,

	/* 4.x Synergistic Processor Unit (SPU) Signal Groups */
	SIG_GROUP_SPU_BASE = 40,

	/* 5.x Memory Flow Controller (MFC) Signal Groups */
	SIG_GROUP_MFC_BASE = 50,

	/* 6.x Element )nterconnect Bus (EIB) Signal Groups */
	SIG_GROUP_EIB_BASE = 60,

	/* 7.x Memory Interface Controller (MIC) Signal Groups */
	SIG_GROUP_MIC_BASE = 70,

	/* 8.x Cell Broadband Engine Interface (BEI) Signal Groups */
	SIG_GROUP_BEI_BASE = 80,
};

/**
 * rmw_spr
 *
 * Read-modify-write for a special-purpose-register.
 **/
#define rmw_spr(spr_id, a_mask, o_mask) \
	do { \
		u64 value = mfspr(spr_id); \
		value &= (u64)(a_mask); \
		value |= (u64)(o_mask); \
		mtspr((spr_id), value); \
	} while (0)

/**
 * rmw_mmio_reg64
 *
 * Read-modify-write for a 64-bit MMIO register.
 **/
#define rmw_mmio_reg64(mem, a_mask, o_mask) \
	do { \
		u64 value = in_be64(&(mem)); \
		value &= (u64)(a_mask); \
		value |= (u64)(o_mask); \
		out_be64(&(mem), value); \
	} while (0)

/**
 * rmwb_mmio_reg64
 *
 * Set or unset a specified bit within a 64-bit MMIO register.
 **/
#define rmwb_mmio_reg64(mem, bit_num, set_bit) \
	rmw_mmio_reg64((mem), ~(1UL << (63 - (bit_num))), \
		       ((set_bit) << (63 - (bit_num))))

/**
 * passthru
 *
 * Enable or disable passthru mode in all the Cell signal islands.
 **/
static int passthru(u32 cpu, u64 enable)
{
	struct cbe_ppe_priv_regs __iomem *ppe_priv_regs;
	struct cbe_pmd_regs __iomem *pmd_regs;
	struct cbe_mic_tm_regs __iomem *mic_tm_regs;
	struct pfm_cell_platform_pmu_info *info =
		((struct pfm_arch_pmu_info *)
		 (pfm_pmu_conf->pmu_info))->platform_info;

	ppe_priv_regs = info->get_cpu_ppe_priv_regs(cpu);
	pmd_regs = info->get_cpu_pmd_regs(cpu);
	mic_tm_regs = info->get_cpu_mic_tm_regs(cpu);

	if (!ppe_priv_regs || !pmd_regs || !mic_tm_regs) {
		PFM_ERR("Error getting Cell PPE, PMD, and MIC "
			"register maps: 0x%p, 0x%p, 0x%p",
			ppe_priv_regs, pmd_regs, mic_tm_regs);
		return -EINVAL;
	}

	rmwb_mmio_reg64(ppe_priv_regs->L2_debug1, 61, enable);
	rmwb_mmio_reg64(ppe_priv_regs->ciu_dr1, 5, enable);
	rmwb_mmio_reg64(pmd_regs->on_ramp_trace, 39, enable);
	rmwb_mmio_reg64(mic_tm_regs->MBL_debug, 20, enable);

	return 0;
}

#define passthru_enable(cpu)  passthru(cpu, 1)
#define passthru_disable(cpu) passthru(cpu, 0)

static inline void reset_signal_registers(u32 cpu)
{
	rmw_spr(SPRN_HID1, HID1_RESET_MASK, 0);
}

/**
 * celleb_reset_signals
 *
 * Non-rtas version of resetting the debug-bus signals.
 **/
static int celleb_reset_signals(u32 cpu)
{
	int rc;
	rc = passthru_disable(cpu);
	if (!rc)
		reset_signal_registers(cpu);
	return rc;
}

/**
 * ppu_selection
 *
 * Write the HID1 register to connect the specified PPU signal-group to the
 * debug-bus.
 **/
static int ppu_selection(struct cell_rtas_arg *signal)
{
	u64 hid1_enable_word = 0;
	u64 hid1_enable_mask = 0;

	switch (signal->signal_group) {

	case SIG_GROUP_PPU_IU1: /* 2.1 PPU Instruction Unit - Group 1 */
		switch (signal->bus_word) {
		case BUS_WORD_0:
			hid1_enable_mask = PPU_IU1_WORD0_HID1_EN_MASK;
			hid1_enable_word = PPU_IU1_WORD0_HID1_EN_WORD;
			break;
		case BUS_WORD_1:
			hid1_enable_mask = PPU_IU1_WORD1_HID1_EN_MASK;
			hid1_enable_word = PPU_IU1_WORD1_HID1_EN_WORD;
			break;
		default:
			PFM_ERR("Invalid bus-word (0x%x) for signal-group %d.",
				signal->bus_word, signal->signal_group);
			return -EINVAL;
		}
		break;

	case SIG_GROUP_PPU_XU:  /* 2.2 PPU Execution Unit */
		switch (signal->bus_word) {
		case BUS_WORD_0:
			hid1_enable_mask = PPU_XU_WORD0_HID1_EN_MASK;
			hid1_enable_word = PPU_XU_WORD0_HID1_EN_WORD;
			break;
		case BUS_WORD_1:
			hid1_enable_mask = PPU_XU_WORD1_HID1_EN_MASK;
			hid1_enable_word = PPU_XU_WORD1_HID1_EN_WORD;
			break;
		default:
			PFM_ERR("Invalid bus-word (0x%x) for signal-group %d.",
				signal->bus_word, signal->signal_group);
			return -EINVAL;
		}
		break;

	default:
		PFM_ERR("Signal-group %d not implemented.",
			signal->signal_group);
		return -EINVAL;
	}

	rmw_spr(SPRN_HID1, hid1_enable_mask, hid1_enable_word);

	return 0;
}

/**
 * celleb_activate_signals
 *
 * Non-rtas version of activating the debug-bus signals.
 **/
static int celleb_activate_signals(struct cell_rtas_arg *signals,
				   int num_signals)
{
	int i, rc = -EINVAL;

	for (i = 0; i < num_signals; i++) {
		switch (signals[i].signal_group) {

		/* 2.x PowerPC Processor Unit (PPU) Signal Selection */
		case SIG_GROUP_PPU_IU1:
		case SIG_GROUP_PPU_XU:
			rc = ppu_selection(signals + i);
			if (rc)
				return rc;
			break;

		default:
			PFM_ERR("Signal-group %d not implemented.",
				signals[i].signal_group);
			return -EINVAL;
		}
	}

	if (0 < i)
		rc = passthru_enable(signals[0].cpu);

	return rc;
}

/**
 * ps3_reset_signals
 *
 * ps3 version of resetting the debug-bus signals.
 **/
static int ps3_reset_signals(u32 cpu)
{
#ifdef CONFIG_PPC_PS3
	return ps3_set_signal(0, 0, 0, 0);
#else
	return 0;
#endif
}

/**
 * ps3_activate_signals
 *
 * ps3 version of activating the debug-bus signals.
 **/
static int ps3_activate_signals(struct cell_rtas_arg *signals,
				int num_signals)
{
#ifdef CONFIG_PPC_PS3
	int i;

	for (i = 0; i < num_signals; i++)
		ps3_set_signal(signals[i].signal_group, signals[i].bit,
			       signals[i].sub_unit, signals[i].bus_word);
#endif
	return 0;
}


/**
 * reset_signals
 *
 * Call to the firmware (if available) to reset the debug-bus signals.
 * Otherwise call the built-in version.
 **/
int reset_signals(u32 cpu)
{
	int rc;

	if (machine_is(celleb))
		rc = celleb_reset_signals(cpu);
	else if (machine_is(ps3))
		rc = ps3_reset_signals(cpu);
	else
		rc = rtas_reset_signals(cpu);

	return rc;
}

/**
 * activate_signals
 *
 * Call to the firmware (if available) to activate the debug-bus signals.
 * Otherwise call the built-in version.
 **/
int activate_signals(struct cell_rtas_arg *signals, int num_signals)
{
	int rc;

	if (machine_is(celleb))
		rc = celleb_activate_signals(signals, num_signals);
	else if (machine_is(ps3))
		rc = ps3_activate_signals(signals, num_signals);
	else
		rc = rtas_activate_signals(signals, num_signals);

	return rc;
}

/**
 *  pfm_cell_pmc_check
 *
 * Verify that we are going to write a valid value to the specified PMC.
 **/
int pfm_cell_pmc_check(struct pfm_context *ctx,
		       struct pfm_event_set *set,
		       struct pfarg_pmc *req)
{
	u16 cnum, reg_num = req->reg_num;
	s16 signal_group = RTAS_SIGNAL_GROUP(req->reg_value);
	u8 bus_word = RTAS_BUS_WORD(req->reg_value);

	if (reg_num < NR_CTRS || reg_num >= (NR_CTRS * 2))
		return -EINVAL;

	switch (signal_group) {
	case SIG_GROUP_PPU_IU1:
	case SIG_GROUP_PPU_XU:
		if ((bus_word != 0) && (bus_word != 1)) {
			PFM_ERR("Invalid bus word (%d) for signal-group %d",
				bus_word, signal_group);
			return -EINVAL;
		}
		break;
	default:
		PFM_ERR("Signal-group %d not implemented.", signal_group);
		return -EINVAL;
	}

	for (cnum = NR_CTRS; cnum < (NR_CTRS * 2); cnum++) {
		if (test_bit(cnum, cast_ulp(set->used_pmcs)) &&
		    bus_word == RTAS_BUS_WORD(set->pmcs[cnum]) &&
		    signal_group != RTAS_SIGNAL_GROUP(set->pmcs[cnum])) {
			PFM_ERR("Impossible signal-group combination: "
				"(%u,%u,%d) (%u,%u,%d)",
				reg_num, bus_word, signal_group, cnum,
				RTAS_BUS_WORD(set->pmcs[cnum]),
				RTAS_SIGNAL_GROUP(set->pmcs[cnum]));
			return  -EBUSY;
		}
	}

	return 0;
}

/**
 * write_pm07_event
 *
 * Pull out the RTAS arguments from the 64-bit register value and make the
 * RTAS activate-signals call.
 **/
static void write_pm07_event(int cpu, unsigned int ctr, u64 value)
{
	struct cell_rtas_arg signal;
	s32 signal_number;
	int rc;

	signal_number = RTAS_SIGNAL_NUMBER(value);
	if (!signal_number) {
		/* Don't include counters that are counting cycles. */
		return;
	}

	signal.cpu = RTAS_CPU(cpu);
	signal.bus_word = 1 << RTAS_BUS_WORD(value);
	signal.sub_unit = RTAS_SUB_UNIT(value);
	signal.signal_group = signal_number / 100;
	signal.bit = abs(signal_number) % 100;

	rc = activate_signals(&signal, 1);
	if (rc) {
		PFM_WARN("%s(%d, %u, %lu): Error calling "
			 "activate_signals(): %d\n", __func__,
			 cpu, ctr, (unsigned long)value, rc);
		/* FIX: Could we change this routine to return an error? */
	}
}

/**
 * pfm_cell_probe_pmu
 *
 * Simply check the processor version register to see if we're currently
 * on a Cell system.
 **/
static int pfm_cell_probe_pmu(void)
{
	unsigned long pvr = mfspr(SPRN_PVR);

	if (PVR_VER(pvr) != PV_BE)
		return -1;

	return 0;
}

/**
 * pfm_cell_write_pmc
 **/
static void pfm_cell_write_pmc(unsigned int cnum, u64 value)
{
	int cpu = smp_processor_id();
	struct pfm_cell_platform_pmu_info *info =
		((struct pfm_arch_pmu_info *)
		 (pfm_pmu_conf->pmu_info))->platform_info;

	if (cnum < NR_CTRS) {
		info->write_pm07_control(cpu, cnum, value);

	} else if (cnum < NR_CTRS * 2) {
		write_pm07_event(cpu, cnum - NR_CTRS, value);

	} else if (cnum == CELL_PMC_PM_STATUS) {
		/* The pm_status register must be treated separately from
		 * the other "global" PMCs. This call will ensure that
		 * the interrupts are routed to the correct CPU, as well
		 * as writing the desired value to the pm_status register.
		 */
		info->enable_pm_interrupts(cpu, info->get_hw_thread_id(cpu),
					   value);

	} else if (cnum < PFM_PM_NUM_PMCS) {
		info->write_pm(cpu, cnum - (NR_CTRS * 2), value);
	}
}

/**
 * pfm_cell_write_pmd
 **/
static void pfm_cell_write_pmd(unsigned int cnum, u64 value)
{
	int cpu = smp_processor_id();
	struct pfm_cell_platform_pmu_info *info =
		((struct pfm_arch_pmu_info *)
		 (pfm_pmu_conf->pmu_info))->platform_info;

	if (cnum < NR_CTRS)
		info->write_ctr(cpu, cnum, value);
}

/**
 * pfm_cell_read_pmd
 **/
static u64 pfm_cell_read_pmd(unsigned int cnum)
{
	int cpu = smp_processor_id();
	struct pfm_cell_platform_pmu_info *info =
		((struct pfm_arch_pmu_info *)
		 (pfm_pmu_conf->pmu_info))->platform_info;

	if (cnum < NR_CTRS)
		return info->read_ctr(cpu, cnum);

	return -EINVAL;
}

/**
 * pfm_cell_enable_counters
 *
 * Just need to turn on the global disable bit in pm_control.
 **/
static void pfm_cell_enable_counters(struct pfm_context *ctx,
				     struct pfm_event_set *set)
{
	struct pfm_cell_platform_pmu_info *info =
		((struct pfm_arch_pmu_info *)
		 (pfm_pmu_conf->pmu_info))->platform_info;

	info->enable_pm(smp_processor_id());
}

/**
 * pfm_cell_disable_counters
 *
 * Just need to turn off the global disable bit in pm_control.
 **/
static void pfm_cell_disable_counters(struct pfm_context *ctx,
				      struct pfm_event_set *set)
{
	struct pfm_cell_platform_pmu_info *info =
		((struct pfm_arch_pmu_info *)
		 (pfm_pmu_conf->pmu_info))->platform_info;

	info->disable_pm(smp_processor_id());
	if (machine_is(ps3))
		reset_signals(smp_processor_id());
}

/*
 * Return the thread id of the specified ppu signal.
 */
static inline u32 get_target_ppu_thread_id(u32 group, u32 bit)
{
	if ((group == SIG_GROUP_PPU_IU1 &&
	     bit < PFM_PPU_IU1_THREAD1_BASE_BIT) ||
	    (group == SIG_GROUP_PPU_XU &&
	     bit < PFM_PPU_XU_THREAD1_BASE_BIT))
		return 0;
	else
		return 1;
}

/*
 * Return whether the specified counter is for PPU signal group.
 */
static inline int is_counter_for_ppu_sig_grp(u32 counter_control, u32 sig_grp)
{
	if (!(counter_control & CBE_PM_CTR_INPUT_CONTROL) &&
	    (counter_control & CBE_PM_CTR_ENABLE) &&
	    ((sig_grp == SIG_GROUP_PPU_IU1) || (sig_grp == SIG_GROUP_PPU_XU)))
		return 1;
	else
		return 0;
}

/*
 * Search ppu signal groups.
 */
static int get_ppu_signal_groups(struct pfm_event_set *set,
				 u32 *ppu_sig_grp0, u32 *ppu_sig_grp1)
{
	u64 pm_event, *used_pmcs = set->used_pmcs;
	int i, j;
	u32 grp0_wd, grp1_wd, wd, sig_grp;

	*ppu_sig_grp0 = 0;
	*ppu_sig_grp1 = 0;
	grp0_wd = PFM_GROUP_CONTROL_GROUP0_WORD(
		set->pmcs[CELL_PMC_GROUP_CONTROL]);
	grp1_wd = PFM_GROUP_CONTROL_GROUP1_WORD(
		set->pmcs[CELL_PMC_GROUP_CONTROL]);

	for (i = 0, j = 0; (i < NR_CTRS) && (j < PFM_NUM_OF_GROUPS); i++) {
		if (test_bit(i + NR_CTRS, used_pmcs)) {
			pm_event = set->pmcs[i + NR_CTRS];
			wd = PFM_EVENT_PMC_BUS_WORD(pm_event);
			sig_grp = PFM_EVENT_PMC_SIGNAL_GROUP(pm_event);
			if ((sig_grp == SIG_GROUP_PPU_IU1) ||
			    (sig_grp == SIG_GROUP_PPU_XU)) {

				if (wd == grp0_wd && *ppu_sig_grp0 == 0) {
					*ppu_sig_grp0 = sig_grp;
					j++;
				} else if (wd == grp1_wd &&
					   *ppu_sig_grp1 == 0) {
					*ppu_sig_grp1 = sig_grp;
					j++;
				}
			}
		}
	}
	return j;
}

/**
 * pfm_cell_restore_pmcs
 *
 * Write all control register values that are saved in the specified event
 * set. We could use the pfm_arch_write_pmc() function to restore each PMC
 * individually (as is done in other architectures), but that results in
 * multiple RTAS calls. As an optimization, we will setup the RTAS argument
 * array so we can do all event-control registers in one RTAS call.
 *
 * In per-thread mode,
 * The counter enable bit of the pmX_control PMC is enabled while the target
 * task runs on the target HW thread.
 **/
void pfm_cell_restore_pmcs(struct pfm_context *ctx, struct pfm_event_set *set)
{
	u64 ctr_ctrl;
	u64 *used_pmcs = set->used_pmcs;
	int i;
	int cpu = smp_processor_id();
	u32 current_th_id;
	struct pfm_cell_platform_pmu_info *info =
		((struct pfm_arch_pmu_info *)
		 (pfm_pmu_conf->pmu_info))->platform_info;

	for (i = 0; i < NR_CTRS; i++) {
		ctr_ctrl = set->pmcs[i];

		if (ctr_ctrl & PFM_COUNTER_CTRL_PMC_PPU_TH0) {
			current_th_id = info->get_hw_thread_id(cpu);

			/*
			 * Set the counter enable bit down if the current
			 * HW thread is NOT 0
			 **/
			if (current_th_id)
				ctr_ctrl = ctr_ctrl & ~CBE_PM_CTR_ENABLE;

		} else if (ctr_ctrl & PFM_COUNTER_CTRL_PMC_PPU_TH1) {
			current_th_id = info->get_hw_thread_id(cpu);

			/*
			 * Set the counter enable bit down if the current
			 * HW thread is 0
			 **/
			if (!current_th_id)
				ctr_ctrl = ctr_ctrl & ~CBE_PM_CTR_ENABLE;
		}

		/* Write the per-counter control register. If the PMC is not
		 * in use, then it will simply clear the register, which will
		 * disable the associated counter.
		 */
		info->write_pm07_control(cpu, i, ctr_ctrl);

		if (test_bit(i + NR_CTRS, used_pmcs))
			write_pm07_event(cpu, 0, set->pmcs[i + NR_CTRS]);
	}

	/* Write all the global PMCs. Need to call pfm_cell_write_pmc()
	 * instead of cbe_write_pm() due to special handling for the
	 * pm_status register.
	 */
	for (i *= 2; i < PFM_PM_NUM_PMCS; i++)
		pfm_cell_write_pmc(i, set->pmcs[i]);
}

/**
 * pfm_cell_restore_pmds
 *
 * Write to pm_control register before writing to counter registers
 * so that we can decide the counter width berfore writing to the couters.
 **/
void pfm_cell_restore_pmds(struct pfm_context *ctx, struct pfm_event_set *set)
{
	u64 *used_pmds;
	unsigned int i, max_pmd;
	int cpu = smp_processor_id();
	struct pfm_cell_platform_pmu_info *info =
		((struct pfm_arch_pmu_info *)
		 (pfm_pmu_conf->pmu_info))->platform_info;

	/*
	 * Write pm_control register value
	 */
	info->write_pm(cpu, pm_control,
		       set->pmcs[CELL_PMC_PM_CONTROL] &
		       ~CBE_PM_ENABLE_PERF_MON);
	PFM_DBG("restore pm_control(0x%lx) before restoring pmds",
		set->pmcs[CELL_PMC_PM_CONTROL]);

	max_pmd = ctx->regs.max_pmd;
	used_pmds = set->used_pmds;

	for (i = 0; i < max_pmd; i++)
		if (test_bit(i, used_pmds) &&
		    !(pfm_pmu_conf->pmd_desc[i].type & PFM_REG_RO))
			pfm_cell_write_pmd(i, set->pmds[i].value);
}

/**
 * pfm_cell_get_cntr_width
 *
 * This function check the 16bit counter field in pm_control pmc.
 *
 * Return value
 *  16 : all counters are 16bit width.
 *  32 : all counters are 32bit width.
 *  0  : several counter width exists.
 **/
static int pfm_cell_get_cntr_width(struct pfm_context *ctx,
				   struct pfm_event_set *s)
{
	int width = 0;
	int tmp = 0;
	u64 cntr_field;

	if (ctx->flags.switch_ovfl || ctx->flags.switch_time) {
		list_for_each_entry(s, &ctx->set_list, list) {
			cntr_field = s->pmcs[CELL_PMC_PM_CONTROL] &
				CELL_PMC_PM_CONTROL_CNTR_MASK;

			if (cntr_field == CELL_PMC_PM_CONTROL_CNTR_16)
				tmp = 16;
			else if (cntr_field == 0x0)
				tmp = 32;
			else
				return 0;

			if (tmp != width && width != 0)
				return 0;

			width = tmp;
		}
	} else {
		cntr_field = s->pmcs[CELL_PMC_PM_CONTROL] &
			CELL_PMC_PM_CONTROL_CNTR_MASK;

		if (cntr_field == CELL_PMC_PM_CONTROL_CNTR_16)
			width = 16;
		else if (cntr_field == 0x0)
			width = 32;
		else
			width = 0;
	}
	return width;
}

/**
 * pfm_cell_check_cntr_ovfl_mask
 *
 * Return value
 *  1  : cntr_ovfl interrupt is used.
 *  0  : cntr_ovfl interrupt is not used.
 **/
static int pfm_cell_check_cntr_ovfl(struct pfm_context *ctx,
				    struct pfm_event_set *s)
{
	if (ctx->flags.switch_ovfl || ctx->flags.switch_time) {
		list_for_each_entry(s, &ctx->set_list, list) {
			if (CBE_PM_OVERFLOW_CTRS(s->pmcs[CELL_PMC_PM_STATUS]))
				return 1;
		}
	} else {
		if (CBE_PM_OVERFLOW_CTRS(s->pmcs[CELL_PMC_PM_STATUS]))
			return 1;
	}
	return 0;
}

#ifdef CONFIG_PPC_PS3
/**
 * update_sub_unit_field
 *
 **/
static inline u64 update_sub_unit_field(u64 pm_event, u64 spe_id)
{
	return ((pm_event & 0xFFFF0000FFFFFFFF) | (spe_id << 32));
}

/**
 * pfm_get_spe_id
 *
 **/
static u64 pfm_get_spe_id(void *arg)
{
	struct spu *spu = arg;
	u64 spe_id;

	if (machine_is(ps3))
		spe_id = ps3_get_spe_id(arg);
	else
		spe_id = spu->spe_id;

	return spe_id;
}

/**
 * pfm_spu_number_to_id
 *
 **/
static int pfm_spu_number_to_id(int number, u64 *spe_id)
{
	struct spu *spu;
	int i;

	for (i = 0; i < MAX_NUMNODES; i++) {
		if (cbe_spu_info[i].n_spus == 0)
			continue;

		list_for_each_entry(spu, &cbe_spu_info[i].spus, cbe_list)
			if (spu->number == number) {
				*spe_id = pfm_get_spe_id(spu);
				return 0;
			}
	}
	return -ENODEV;
}

/**
 * pfm_update_pmX_event_subunit_field
 *
 * In system wide mode,
 * This function updates the subunit field of SPE pmX_event.
 **/
static int pfm_update_pmX_event_subunit_field(struct pfm_context *ctx)
{
	struct pfm_event_set *set;
	int i, last_pmc, ret;
	u64 signal_group, spe_id;
	int sub_unit;
	u64 *used_pmcs;

	last_pmc = NR_CTRS + 8;
	ret = 0;
	list_for_each_entry(set, &ctx->set_list, list) {

		used_pmcs = set->used_pmcs;
		for (i = NR_CTRS; i < last_pmc; i++) {
			if (!test_bit(i, used_pmcs))
				continue;

			signal_group = PFM_EVENT_PMC_SIGNAL_GROUP(set->pmcs[i]);

			/*
			 * If the target event is a SPE signal group event,
			 * The sub_unit field in pmX_event pmc is changed to the
			 * specified spe_id.
			 */
			if (SIG_GROUP_SPU_BASE < signal_group &&
			    signal_group < SIG_GROUP_EIB_BASE) {
				sub_unit = RTAS_SUB_UNIT(set->pmcs[i]);

				ret = pfm_spu_number_to_id(sub_unit, &spe_id);
				if (ret)
					return ret;

				set->pmcs[i] = update_sub_unit_field(
					set->pmcs[i], spe_id);
			}
		}
	}
	return 0;
}
#endif

/**
 * pfm_cell_load_context
 *
 * In per-thread mode,
 *  The pmX_control PMCs which are used for PPU IU/XU event are marked with
 *  the thread id(PFM_COUNTER_CTRL_PMC_PPU_TH0/TH1).
 **/
static int pfm_cell_load_context(struct pfm_context *ctx)
{
	int i;
	u32 ppu_sig_grp[PFM_NUM_OF_GROUPS] = {SIG_GROUP_NONE, SIG_GROUP_NONE};
	u32 bit;
	int index;
	u32 target_th_id;
	int ppu_sig_num = 0;
	struct pfm_event_set *s;
	int cntr_width = 32;
	int ret = 0;

	if (pfm_cell_check_cntr_ovfl(ctx, ctx->active_set)) {
		cntr_width = pfm_cell_get_cntr_width(ctx, ctx->active_set);

		/*
		 * Counter overflow interrupt works with only 32bit counter,
		 * because perfmon core uses pfm_cell_pmu_conf.counter_width
		 * to deal with the counter overflow. we can't change the
		 * counter width here.
		 */
		if (cntr_width != 32)
			return -EINVAL;
	}

	if (ctx->flags.system) {
#ifdef CONFIG_PPC_PS3
		if (machine_is(ps3))
			ret = pfm_update_pmX_event_subunit_field(ctx);
#endif
		return ret;
	}

	list_for_each_entry(s, &ctx->set_list, list) {
		ppu_sig_num = get_ppu_signal_groups(s, &ppu_sig_grp[0],
						    &ppu_sig_grp[1]);

		for (i = 0; i < NR_CTRS; i++) {
			index = PFM_PM_CTR_INPUT_MUX_GROUP_INDEX(s->pmcs[i]);
			if (ppu_sig_num &&
			    (ppu_sig_grp[index] != SIG_GROUP_NONE) &&
			    is_counter_for_ppu_sig_grp(s->pmcs[i],
						       ppu_sig_grp[index])) {

				bit = PFM_PM_CTR_INPUT_MUX_BIT(s->pmcs[i]);
				target_th_id = get_target_ppu_thread_id(
					ppu_sig_grp[index], bit);
				if (!target_th_id)
					s->pmcs[i] |=
						PFM_COUNTER_CTRL_PMC_PPU_TH0;
				else
					s->pmcs[i] |=
						PFM_COUNTER_CTRL_PMC_PPU_TH1;
				PFM_DBG("set:%d mark ctr:%d target_thread:%d",
					s->id, i, target_th_id);
			}
		}
	}

	return ret;
}

/**
 * pfm_cell_unload_context
 *
 * For system-wide contexts and self-monitored contexts, make the RTAS call
 * to reset the debug-bus signals.
 *
 * For non-self-monitored contexts, the monitored thread will already have
 * been taken off the CPU and we don't need to do anything additional.
 **/
static void pfm_cell_unload_context(struct pfm_context *ctx)
{
	if (ctx->task == current || ctx->flags.system)
		reset_signals(smp_processor_id());
}

/**
 * pfm_cell_ctxswout_thread
 *
 * When a monitored thread is switched out (self-monitored or externally
 * monitored) we need to reset the debug-bus signals so the next context that
 * gets switched in can start from a clean set of signals.
 **/
int pfm_cell_ctxswout_thread(struct task_struct *task,
			     struct pfm_context *ctx, struct pfm_event_set *set)
{
	reset_signals(smp_processor_id());
	return 0;
}

/**
 * pfm_cell_get_ovfl_pmds
 *
 * Determine which counters in this set have overflowed and fill in the
 * set->povfl_pmds mask and set->npend_ovfls count. On Cell, the pm_status
 * register contains a bit for each counter to indicate overflow. However,
 * those 8 bits are in the reverse order than what Perfmon2 is expecting,
 * so we need to reverse the order of the overflow bits.
 **/
static void pfm_cell_get_ovfl_pmds(struct pfm_context *ctx,
				   struct pfm_event_set *set)
{
	struct pfm_arch_context *ctx_arch = pfm_ctx_arch(ctx);
	u32 pm_status, ovfl_ctrs;
	u64 povfl_pmds = 0;
	int i;
	struct pfm_cell_platform_pmu_info *info =
		((struct pfm_arch_pmu_info *)
		 (pfm_pmu_conf->pmu_info))->platform_info;

	if (!ctx_arch->last_read_updated)
		/* This routine was not called via the interrupt handler.
		 * Need to start by getting interrupts and updating
		 * last_read_pm_status.
		 */
		ctx_arch->last_read_pm_status =
 info->get_and_clear_pm_interrupts(smp_processor_id());

	/* Reset the flag that the interrupt handler last read pm_status. */
	ctx_arch->last_read_updated = 0;

	pm_status = ctx_arch->last_read_pm_status &
		    set->pmcs[CELL_PMC_PM_STATUS];
	ovfl_ctrs = CBE_PM_OVERFLOW_CTRS(pm_status);

	/* Reverse the order of the bits in ovfl_ctrs
	 * and store the result in povfl_pmds.
	 */
	for (i = 0; i < PFM_PM_NUM_PMDS; i++) {
		povfl_pmds = (povfl_pmds << 1) | (ovfl_ctrs & 1);
		ovfl_ctrs >>= 1;
	}

	/* Mask povfl_pmds with set->used_pmds to get set->povfl_pmds.
	 * Count the bits set in set->povfl_pmds to get set->npend_ovfls.
	 */
	bitmap_and(set->povfl_pmds, &povfl_pmds,
		   set->used_pmds, PFM_PM_NUM_PMDS);
	set->npend_ovfls = bitmap_weight(set->povfl_pmds, PFM_PM_NUM_PMDS);
}

/**
 * pfm_cell_acquire_pmu
 *
 * acquire PMU resource.
 * This acquisition is done when the first context is created.
 **/
int pfm_cell_acquire_pmu(u64 *unavail_pmcs, u64 *unavail_pmds)
{
#ifdef CONFIG_PPC_PS3
	int ret;

	if (machine_is(ps3)) {
		PFM_DBG("");
		ret = ps3_lpm_open(PS3_LPM_TB_TYPE_INTERNAL, NULL, 0);
		if (ret) {
			PFM_ERR("Can't create PS3 lpm. error:%d", ret);
			return -EFAULT;
		}
	}
#endif
	return 0;
}

/**
 * pfm_cell_release_pmu
 *
 * release PMU resource.
 * actual release happens when last context is destroyed
 **/
void pfm_cell_release_pmu(void)
{
#ifdef CONFIG_PPC_PS3
	if (machine_is(ps3)) {
		if (ps3_lpm_close())
			PFM_ERR("Can't delete PS3 lpm.");
	}
#endif
}

/**
 * handle_trace_buffer_interrupts
 *
 * This routine is for processing just the interval timer and trace buffer
 * overflow interrupts. Performance counter interrupts are handled by the
 * perf_irq_handler() routine, which reads and saves the pm_status register.
 * This routine should not read the actual pm_status register, but rather
 * the value passed in.
 **/
static void handle_trace_buffer_interrupts(unsigned long iip,
					   struct pt_regs *regs,
					   struct pfm_context *ctx,
					   u32 pm_status)
{
	/* FIX: Currently ignoring trace-buffer interrupts. */
	return;
}

/**
 * pfm_cell_irq_handler
 *
 * Handler for all Cell performance-monitor interrupts.
 **/
static void pfm_cell_irq_handler(struct pt_regs *regs, struct pfm_context *ctx)
{
	struct pfm_arch_context *ctx_arch = pfm_ctx_arch(ctx);
	u32 last_read_pm_status;
	int cpu = smp_processor_id();
	struct pfm_cell_platform_pmu_info *info =
		((struct pfm_arch_pmu_info *)
		 (pfm_pmu_conf->pmu_info))->platform_info;

	/* Need to disable and reenable the performance counters to get the
	 * desired behavior from the hardware. This is specific to the Cell
	 * PMU hardware.
	 */
	info->disable_pm(cpu);

	/* Read the pm_status register to get the interrupt bits. If a
	 * perfmormance counter overflow interrupt occurred, call the core
	 * perfmon interrupt handler to service the counter overflow. If the
	 * interrupt was for the interval timer or the trace_buffer,
	 * call the interval timer and trace buffer interrupt handler.
	 *
	 * The value read from the pm_status register is stored in the
	 * pmf_arch_context structure for use by other routines. Note that
	 * reading the pm_status register resets the interrupt flags to zero.
	 * Hence, it is important that the register is only read in one place.
	 *
	 * The pm_status reg interrupt reg format is:
	 * [pmd0:pmd1:pmd2:pmd3:pmd4:pmd5:pmd6:pmd7:intt:tbf:tbu:]
	 * - pmd0 to pm7 are the perf counter overflow interrupts.
	 * - intt is the interval timer overflowed interrupt.
	 * - tbf is the trace buffer full interrupt.
	 * - tbu is the trace buffer underflow interrupt.
	 * - The pmd0 bit is the MSB of the 32 bit register.
	 */
	ctx_arch->last_read_pm_status = last_read_pm_status =
			info->get_and_clear_pm_interrupts(cpu);

	/* Set flag for pfm_cell_get_ovfl_pmds() routine so it knows
	 * last_read_pm_status was updated by the interrupt handler.
	 */
	ctx_arch->last_read_updated = 1;

	if (last_read_pm_status & CBE_PM_ALL_OVERFLOW_INTR)
		/* At least one counter overflowed. */
		pfm_interrupt_handler(instruction_pointer(regs), regs);

	if (last_read_pm_status & (CBE_PM_INTERVAL_INTR |
				   CBE_PM_TRACE_BUFFER_FULL_INTR |
				   CBE_PM_TRACE_BUFFER_UNDERFLOW_INTR))
		/* Trace buffer or interval timer overflow. */
		handle_trace_buffer_interrupts(instruction_pointer(regs),
					       regs, ctx, last_read_pm_status);

	/* The interrupt settings is the value written to the pm_status
	 * register. It is saved in the context when the register is
	 * written.
	 */
	info->enable_pm_interrupts(cpu, info->get_hw_thread_id(cpu),
	ctx->active_set->pmcs[CELL_PMC_PM_STATUS]);

	/* The writes to the various performance counters only writes to a
	 * latch. The new values (interrupt setting bits, reset counter value
	 * etc.) are not copied to the actual registers until the performance
	 * monitor is enabled. In order to get this to work as desired, the
	 * permormance monitor needs to be disabled while writting to the
	 * latches. This is a HW design issue.
	 */
	info->enable_pm(cpu);
}


static struct pfm_cell_platform_pmu_info ps3_platform_pmu_info = {
#ifdef CONFIG_PPC_PS3
	.read_ctr                    = ps3_read_ctr,
	.write_ctr                   = ps3_write_ctr,
	.write_pm07_control          = ps3_write_pm07_control,
	.write_pm                    = ps3_write_pm,
	.enable_pm                   = ps3_enable_pm,
	.disable_pm                  = ps3_disable_pm,
	.enable_pm_interrupts        = ps3_enable_pm_interrupts,
	.get_and_clear_pm_interrupts = ps3_get_and_clear_pm_interrupts,
	.get_hw_thread_id            = ps3_get_hw_thread_id,
	.get_cpu_ppe_priv_regs       = NULL,
	.get_cpu_pmd_regs            = NULL,
	.get_cpu_mic_tm_regs         = NULL,
	.rtas_token                  = NULL,
	.rtas_call                   = NULL,
#endif
};

static struct pfm_cell_platform_pmu_info native_platform_pmu_info = {
#ifdef CONFIG_PPC_CELL_NATIVE
	.read_ctr                    = cbe_read_ctr,
	.write_ctr                   = cbe_write_ctr,
	.write_pm07_control          = cbe_write_pm07_control,
	.write_pm                    = cbe_write_pm,
	.enable_pm                   = cbe_enable_pm,
	.disable_pm                  = cbe_disable_pm,
	.enable_pm_interrupts        = cbe_enable_pm_interrupts,
	.get_and_clear_pm_interrupts = cbe_get_and_clear_pm_interrupts,
	.get_hw_thread_id            = cbe_get_hw_thread_id,
	.get_cpu_ppe_priv_regs       = cbe_get_cpu_ppe_priv_regs,
	.get_cpu_pmd_regs            = cbe_get_cpu_pmd_regs,
	.get_cpu_mic_tm_regs         = cbe_get_cpu_mic_tm_regs,
	.rtas_token                  = rtas_token,
	.rtas_call                   = rtas_call,
#endif
};

static struct pfm_arch_pmu_info pfm_cell_pmu_info = {
	.pmu_style        = PFM_POWERPC_PMU_CELL,
	.acquire_pmu      = pfm_cell_acquire_pmu,
	.release_pmu      = pfm_cell_release_pmu,
	.write_pmc        = pfm_cell_write_pmc,
	.write_pmd        = pfm_cell_write_pmd,
	.read_pmd         = pfm_cell_read_pmd,
	.enable_counters  = pfm_cell_enable_counters,
	.disable_counters = pfm_cell_disable_counters,
	.irq_handler      = pfm_cell_irq_handler,
	.get_ovfl_pmds    = pfm_cell_get_ovfl_pmds,
	.restore_pmcs     = pfm_cell_restore_pmcs,
	.restore_pmds     = pfm_cell_restore_pmds,
	.ctxswout_thread  = pfm_cell_ctxswout_thread,
	.load_context     = pfm_cell_load_context,
	.unload_context   = pfm_cell_unload_context,
};

static struct pfm_pmu_config pfm_cell_pmu_conf = {
	.pmu_name = "Cell",
	.version = "0.1",
	.counter_width = 32,
	.pmd_desc = pfm_cell_pmd_desc,
	.pmc_desc = pfm_cell_pmc_desc,
	.num_pmc_entries = PFM_PM_NUM_PMCS,
	.num_pmd_entries = PFM_PM_NUM_PMDS,
	.probe_pmu  = pfm_cell_probe_pmu,
	.pmu_info = &pfm_cell_pmu_info,
	.flags = PFM_PMU_BUILTIN_FLAG,
	.owner = THIS_MODULE,
};

/**
 * pfm_cell_platform_probe
 *
 * If we're on a system without the firmware rtas call available, set up the
 * PMC write-checker for all the pmX_event control registers.
 **/
static void pfm_cell_platform_probe(void)
{
	if (machine_is(celleb)) {
		int cnum;
		pfm_cell_pmu_conf.pmc_write_check = pfm_cell_pmc_check;
		for (cnum = NR_CTRS; cnum < (NR_CTRS * 2); cnum++)
			pfm_cell_pmc_desc[cnum].type |= PFM_REG_WC;
	}

	if (machine_is(ps3))
		pfm_cell_pmu_info.platform_info = &ps3_platform_pmu_info;
	else
		pfm_cell_pmu_info.platform_info = &native_platform_pmu_info;
}

static int __init pfm_cell_pmu_init_module(void)
{
	pfm_cell_platform_probe();
	return pfm_pmu_register(&pfm_cell_pmu_conf);
}

static void __exit pfm_cell_pmu_cleanup_module(void)
{
	pfm_pmu_unregister(&pfm_cell_pmu_conf);
}

module_init(pfm_cell_pmu_init_module);
module_exit(pfm_cell_pmu_cleanup_module);
