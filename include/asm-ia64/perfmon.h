/*
 * Copyright (C) 2001-2003 Hewlett-Packard Co
 *               Stephane Eranian <eranian@hpl.hp.com>
 */

#ifndef _ASM_IA64_PERFMON_H
#define _ASM_IA64_PERFMON_H

/*
 * perfmon comamnds supported on all CPU models
 */
#define PFM_WRITE_PMCS		0x01
#define PFM_WRITE_PMDS		0x02
#define PFM_READ_PMDS		0x03
#define PFM_STOP		0x04
#define PFM_START		0x05
#define PFM_ENABLE		0x06 /* obsolete */
#define PFM_DISABLE		0x07 /* obsolete */
#define PFM_CREATE_CONTEXT	0x08
#define PFM_DESTROY_CONTEXT	0x09 /* obsolete use close() */
#define PFM_RESTART		0x0a
#define PFM_PROTECT_CONTEXT	0x0b /* obsolete */
#define PFM_GET_FEATURES	0x0c
#define PFM_DEBUG		0x0d
#define PFM_UNPROTECT_CONTEXT	0x0e /* obsolete */
#define PFM_GET_PMC_RESET_VAL	0x0f
#define PFM_LOAD_CONTEXT	0x10
#define PFM_UNLOAD_CONTEXT	0x11

/*
 * PMU model specific commands (may not be supported on all PMU models)
 */
#define PFM_WRITE_IBRS		0x20
#define PFM_WRITE_DBRS		0x21

/*
 * context flags
 */
#define PFM_FL_NOTIFY_BLOCK    	 0x01	/* block task on user level notifications */
#define PFM_FL_SYSTEM_WIDE	 0x02	/* create a system wide context */
#define PFM_FL_UNSECURE		 0x04   /* allow unsecure monitoring for non self-monitoring task */
#define PFM_FL_OVFL_NO_MSG	 0x80   /* do not post overflow/end messages for notification */

/*
 * event set flags
 */
#define PFM_SETFL_EXCL_IDLE      0x01   /* exclude idle task (syswide only) XXX: DO NOT USE YET */

/*
 * PMC flags
 */
#define PFM_REGFL_OVFL_NOTIFY	0x1	/* send notification on overflow */
#define PFM_REGFL_RANDOM	0x2	/* randomize sampling interval   */

/*
 * PMD/PMC/IBR/DBR return flags (ignored on input)
 *
 * Those flags are used on output and must be checked in case EAGAIN is returned
 * by any of the calls using a pfarg_reg_t or pfarg_dbreg_t structure.
 */
#define PFM_REG_RETFL_NOTAVAIL	(1UL<<31) /* set if register is implemented but not available */
#define PFM_REG_RETFL_EINVAL	(1UL<<30) /* set if register entry is invalid */
#define PFM_REG_RETFL_MASK	(PFM_REG_RETFL_NOTAVAIL|PFM_REG_RETFL_EINVAL)

#define PFM_REG_HAS_ERROR(flag)	(((flag) & PFM_REG_RETFL_MASK) != 0)

typedef unsigned char pfm_uuid_t[16];	/* custom sampling buffer identifier type */

/*
 * Request structure used to define a context
 */
typedef struct {
	pfm_uuid_t    ctx_smpl_buf_id;	/* which buffer format to use (if needed) */
	unsigned long ctx_flags;	/* noblock/block */
	unsigned int  ctx_nextra_sets;	/* number of extra event sets (you always get 1) */
	int	      ctx_fd;		/* return arg: unique identification for context */
	void	      *ctx_smpl_vaddr;	/* return arg: virtual address of sampling buffer, is used */
	unsigned long ctx_reserved[11];	/* for future use */
} pfarg_context_t;

/*
 * Request structure used to write/read a PMC or PMD
 */
typedef struct {
	unsigned int	reg_num;	   /* which register                             */
	unsigned int	reg_set;	   /* event set for this register                */

	unsigned long	reg_value;	   /* initial pmc/pmd value                      */
	unsigned long	reg_flags;	   /* input: pmc/pmd flags, return: reg error    */

	unsigned long	reg_long_reset;	   /* reset after buffer overflow notification   */
	unsigned long	reg_short_reset;   /* reset after counter overflow               */

	unsigned long	reg_reset_pmds[4]; /* which other counters to reset on overflow  */
	unsigned long	reg_random_seed;   /* seed value when randomization is used      */
	unsigned long	reg_random_mask;   /* bitmask used to limit random value         */
	unsigned long   reg_last_reset_val;/* return: PMD last reset value               */

	unsigned long	reg_smpl_pmds[4];  /* which pmds are accessed when PMC overflows */
	unsigned long	reg_smpl_eventid;  /* opaque sampling event identifier           */

	unsigned long   reserved[3];	   /* for future use                             */
} pfarg_reg_t;

typedef struct {
	unsigned int	dbreg_num;		/* which debug register        */
	unsigned int 	dbreg_set;		/* event set for this register */
	unsigned long	dbreg_value;		/* value for debug register    */
	unsigned long	dbreg_flags;		/* return: dbreg error         */
	unsigned long	dbreg_reserved[1];	/* for future use              */
} pfarg_dbreg_t;

typedef struct {
	unsigned int	ft_version;	/* perfmon: major [16-31], minor [0-15] */
	unsigned int	ft_reserved;	/* reserved for future use              */
	unsigned long	reserved[4];	/* for future use                       */
} pfarg_features_t;

typedef struct {
	pid_t		load_pid;	  /* process to load the context into */
	unsigned int	load_set;	  /* first event set to load          */
	unsigned long	load_reserved[2]; /* for future use                   */
} pfarg_load_t;

typedef struct {
	int		msg_type;		/* generic message header */
	int		msg_ctx_fd;		/* generic message header */
	unsigned long	msg_tstamp;		/* for perf tuning */
	unsigned int	msg_active_set;		/* active set at the time of overflow */
	unsigned long	msg_ovfl_pmds[4];	/* which PMDs overflowed */
} pfm_ovfl_msg_t;

typedef struct {
	int		msg_type;		/* generic message header */
	int		msg_ctx_fd;		/* generic message header */
	unsigned long	msg_tstamp;		/* for perf tuning */
} pfm_end_msg_t;

typedef struct {
	int		msg_type;		/* type of the message */
	int		msg_ctx_fd;		/* unique identifier for the context */
	unsigned long	msg_tstamp;		/* for perf tuning */
} pfm_gen_msg_t;

#define PFM_MSG_OVFL	1	/* an overflow happened */
#define PFM_MSG_END	2	/* task to which context was attached ended */

typedef union {
	pfm_ovfl_msg_t	pfm_ovfl_msg;
	pfm_end_msg_t	pfm_end_msg;
	pfm_gen_msg_t	pfm_gen_msg;
} pfm_msg_t;

/*
 * Define the version numbers for both perfmon as a whole and the sampling buffer format.
 */
#define PFM_VERSION_MAJ		 2U
#define PFM_VERSION_MIN		 0U
#define PFM_SMPL_HDR_VERSION_MAJ 2U
#define PFM_SMPL_HDR_VERSION_MIN 0U
#define PFM_VERSION		 (((PFM_VERSION_MAJ&0xffff)<<16)|(PFM_VERSION_MIN & 0xffff))
#define PFM_VERSION_MAJOR(x)	 (((x)>>16) & 0xffff)
#define PFM_VERSION_MINOR(x)	 ((x) & 0xffff)


/*
 * miscellaneous architected definitions
 */
#define PMU_FIRST_COUNTER	4	/* first counting monitor (PMC/PMD) */
#define PMU_MAX_PMCS		256	/* maximum architected number of PMC registers */
#define PMU_MAX_PMDS		256	/* maximum architected number of PMD registers */

#ifdef __KERNEL__

extern long perfmonctl(int fd, int cmd, void *arg, int narg);

extern void pfm_save_regs (struct task_struct *);
extern void pfm_load_regs (struct task_struct *);

extern void pfm_exit_thread(struct task_struct *);
extern int  pfm_use_debug_registers(struct task_struct *);
extern int  pfm_release_debug_registers(struct task_struct *);
extern void pfm_syst_wide_update_task(struct task_struct *, unsigned long info, int is_ctxswin);
extern void pfm_inherit(struct task_struct *task, struct pt_regs *regs);
extern void pfm_init_percpu(void);
extern void pfm_handle_work(void);

/*
 * Reset PMD register flags
 */
#define PFM_PMD_NO_RESET	0
#define PFM_PMD_LONG_RESET	1
#define PFM_PMD_SHORT_RESET	2

typedef struct {
	unsigned int notify_user:1;	/* notify user program of overflow                           */
	unsigned int reset_pmds :2;	/* PFM_PMD_NO_RESET, PFM_PMD_LONG_RESET, PFM_PMD_SHORT_RESET */
	unsigned int block:1;		/* block monitored task on kernel exit                       */
	unsigned int stop_monitoring:1; /* will mask monitoring via PMCx.plm                         */
	unsigned int reserved:26;	/* for future use                                            */
} pfm_ovfl_ctrl_t;

typedef struct {
	unsigned long   ovfl_pmds[4];	/* bitmask of overflowed pmds                            */
	unsigned long   ovfl_notify[4];	/* bitmask of overflow pmds which asked for notification */
	unsigned long   pmd_value;	/* current 64-bit value of 1st pmd which overflowed      */
	unsigned long   pmd_last_reset;	/* last reset value of 1st pmd which overflowed          */
	unsigned long	pmd_eventid;	/* eventid associated with 1st pmd which overflowed      */
	unsigned int    active_set;	/* event set active at the time of the overflow          */
	unsigned int    reserved1;
	unsigned long	smpl_pmds[4];
	unsigned long   smpl_pmds_values[PMU_MAX_PMDS];
	pfm_ovfl_ctrl_t ovfl_ctrl;	/* return: perfmon controls to set by handler            */
} pfm_ovfl_arg_t;


typedef struct _pfm_buffer_fmt_t {
	char		*fmt_name;
	pfm_uuid_t	fmt_uuid;
	size_t		fmt_arg_size;
	unsigned long	fmt_flags;

	int		(*fmt_validate)(struct task_struct *task, unsigned int flags, int cpu, void *arg);
	int		(*fmt_getsize)(struct task_struct *task, unsigned int flags, int cpu, void *arg, unsigned long *size);
	int 		(*fmt_init)(struct task_struct *task, void *buf, unsigned int flags, int cpu, void *arg);
	int		(*fmt_handler)(struct task_struct *task, void *buf, pfm_ovfl_arg_t *arg, struct pt_regs *regs);
	int		(*fmt_restart)(struct task_struct *task, pfm_ovfl_ctrl_t *ctrl, void *buf, struct pt_regs *regs);
	int		(*fmt_restart_active)(struct task_struct *task, pfm_ovfl_ctrl_t *ctrl, void *buf, struct pt_regs *regs);
	int		(*fmt_exit)(struct task_struct *task, void *buf, struct pt_regs *regs);

	struct _pfm_buffer_fmt_t *fmt_next;
	struct _pfm_buffer_fmt_t *fmt_prev;
} pfm_buffer_fmt_t;

extern int pfm_register_buffer_fmt(pfm_buffer_fmt_t *fmt);
extern int pfm_unregister_buffer_fmt(pfm_uuid_t uuid);

/*
 * perfmon interface exported to modules
 */
extern long pfm_mod_fast_read_pmds(struct task_struct *, unsigned long mask[4], unsigned long *addr, struct pt_regs *regs);
extern long pfm_mod_read_pmds(struct task_struct *, pfarg_reg_t *req, unsigned int nreq, struct pt_regs *regs);
extern long pfm_mod_write_pmcs(struct task_struct *, pfarg_reg_t *req, unsigned int nreq, struct pt_regs *regs);

/*
 * describe the content of the local_cpu_date->pfm_syst_info field
 */
#define PFM_CPUINFO_SYST_WIDE	0x1	/* if set a system wide session exists */
#define PFM_CPUINFO_DCR_PP	0x2	/* if set the system wide session has started */
#define PFM_CPUINFO_EXCL_IDLE	0x4	/* the system wide session excludes the idle task */

#endif /* __KERNEL__ */

#endif /* _ASM_IA64_PERFMON_H */
