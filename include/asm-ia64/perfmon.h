/*
 * Copyright (C) 2001 Hewlett-Packard Co
 * Copyright (C) 2001 Stephane Eranian <eranian@hpl.hp.com>
 */

#ifndef _ASM_IA64_PERFMON_H
#define _ASM_IA64_PERFMON_H

#include <linux/types.h>

/*
 * Request structure used to define a context
 */
typedef struct {
	unsigned long smpl_entries;	/* how many entries in sampling buffer */
	unsigned long smpl_regs;	/* which pmds to record on overflow */
	void	      *smpl_vaddr;	/* returns address of BTB buffer */

	pid_t	      notify_pid;	/* which process to notify on overflow */
	int	      notify_sig; 	/* XXX: not used anymore */

	int	      flags;		/* NOBLOCK/BLOCK/ INHERIT flags (will replace API flags) */
} pfreq_context_t;

/*
 * Request structure used to write/read a PMC or PMD
 */
typedef struct {
	unsigned long	reg_num;	/* which register */
	unsigned long	reg_value;	/* configuration (PMC) or initial value (PMD) */
	unsigned long	reg_smpl_reset;	/* reset of sampling buffer overflow (large) */
	unsigned long	reg_ovfl_reset;	/* reset on counter overflow (small) */
	int		reg_flags;	/* (PMD): notify/don't notify */
} pfreq_reg_t;

/*
 * main request structure passed by user
 */
typedef union {
	pfreq_context_t	pfr_ctx;	/* request to configure a context */
	pfreq_reg_t	pfr_reg;	/* request to configure a PMD/PMC */
} perfmon_req_t;

#ifdef __KERNEL__

extern void pfm_save_regs (struct task_struct *);
extern void pfm_load_regs (struct task_struct *);

extern int pfm_inherit (struct task_struct *, struct pt_regs *);
extern void pfm_context_exit (struct task_struct *);
extern void pfm_flush_regs (struct task_struct *);
extern void pfm_cleanup_notifiers (struct task_struct *);

#endif /* __KERNEL__ */

#endif /* _ASM_IA64_PERFMON_H */
