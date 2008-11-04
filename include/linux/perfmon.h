/*
 * Copyright (c) 2001-2006 Hewlett-Packard Development Company, L.P.
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

#ifndef __LINUX_PERFMON_H__
#define __LINUX_PERFMON_H__

/*
 * This file contains all the user visible generic definitions for the
 * interface. Model-specific user-visible definitions are located in
 * the asm/perfmon.h file.
 */

/*
 * include arch-specific user interface definitions
 */
#include <asm/perfmon.h>

/*
 * defined by each arch
 */
#define PFM_MAX_PMCS	PFM_ARCH_MAX_PMCS
#define PFM_MAX_PMDS	PFM_ARCH_MAX_PMDS

/*
 * number of elements for each type of bitvector
 * all bitvectors use u64 fixed size type on all architectures.
 */
#define PFM_BVSIZE(x)	(((x)+(sizeof(__u64)<<3)-1) / (sizeof(__u64)<<3))
#define PFM_PMD_BV	PFM_BVSIZE(PFM_MAX_PMDS)
#define PFM_PMC_BV	PFM_BVSIZE(PFM_MAX_PMCS)

/*
 * register flags layout:
 * bit[00-15] : generic flags
 * bit[16-31] : arch-specific flags
 *
 * PFM_REGFL_NO_EMUL64: must be set on the PMC controlling the PMD
 */
#define PFM_REGFL_OVFL_NOTIFY	0x1	/* PMD: send notification on event */
#define PFM_REGFL_RANDOM	0x2	/* PMD: randomize value after event */
#define PFM_REGFL_NO_EMUL64	0x4	/* PMC: no 64-bit emulation */

/*
 * event set flags layout:
 * bits[00-15] : generic flags
 * bits[16-31] : arch-specific flags (see asm/perfmon.h)
 */
#define PFM_SETFL_OVFL_SWITCH	0x01 /* enable switch on overflow */
#define PFM_SETFL_TIME_SWITCH	0x02 /* enable switch on timeout */

/*
 * argument to pfm_create_context() system call
 * structure shared with user level
 */
struct pfarg_ctx {
	__u32		ctx_flags;	  /* noblock/block/syswide */
	__u32		ctx_reserved1;	  /* for future use */
	__u64		ctx_reserved2[7]; /* for future use */
};

/*
 * context flags layout:
 * bits[00-15]: generic flags
 * bits[16-31]: arch-specific flags (see perfmon_const.h)
 */
#define PFM_FL_NOTIFY_BLOCK    	 0x01	/* block task on user notifications */
#define PFM_FL_SYSTEM_WIDE	 0x02	/* create a system wide context */
#define PFM_FL_OVFL_NO_MSG	 0x80   /* no overflow msgs */

/*
 * argument to pfm_write_pmcs() system call.
 * structure shared with user level
 */
struct pfarg_pmc {
	__u16 reg_num;		/* which register */
	__u16 reg_set;		/* event set for this register */
	__u32 reg_flags;	/* REGFL flags */
	__u64 reg_value;	/* pmc value */
	__u64 reg_reserved2[4];	/* for future use */
};

/*
 * argument to pfm_write_pmds() and pfm_read_pmds() system calls.
 * structure shared with user level
 */
struct pfarg_pmd {
	__u16 reg_num;	   	/* which register */
	__u16 reg_set;	   	/* event set for this register */
	__u32 reg_flags; 	/* REGFL flags */
	__u64 reg_value;	/* initial pmc/pmd value */
	__u64 reg_long_reset;	/* value to reload after notification */
	__u64 reg_short_reset;  /* reset after counter overflow */
	__u64 reg_last_reset_val;	/* return: PMD last reset value */
	__u64 reg_ovfl_switch_cnt;	/* #overflows before switch */
	__u64 reg_reset_pmds[PFM_PMD_BV]; /* reset on overflow */
	__u64 reg_smpl_pmds[PFM_PMD_BV];  /* record in sample */
	__u64 reg_smpl_eventid; /* opaque event identifier */
	__u64 reg_random_mask; 	/* bitmask used to limit random value */
	__u32 reg_random_seed;  /* seed for randomization (OBSOLETE) */
	__u32 reg_reserved2[7];	/* for future use */
};

/*
 * optional argument to pfm_start() system call. Pass NULL if not needed.
 * structure shared with user level
 */
struct pfarg_start {
	__u16 start_set;	/* event set to start with */
	__u16 start_reserved1;	/* for future use */
	__u32 start_reserved2;	/* for future use */
	__u64 reserved3[3];	/* for future use */
};

/*
 * argument to pfm_load_context() system call.
 * structure shared with user level
 */
struct pfarg_load {
	__u32	load_pid;	   /* thread or CPU to attach to */
	__u16	load_set;	   /* set to load first */
	__u16	load_reserved1;	   /* for future use */
	__u64	load_reserved2[3]; /* for future use */
};

/*
 * argument to pfm_create_evtsets() and pfm_delete_evtsets() system calls.
 * structure shared with user level.
 */
struct pfarg_setdesc {
	__u16	set_id;		  /* which set */
	__u16	set_reserved1;	  /* for future use */
	__u32	set_flags; 	  /* SETFL flags  */
	__u64	set_timeout;	  /* switch timeout in nsecs */
	__u64	reserved[6];	  /* for future use */
};

/*
 * argument to pfm_getinfo_evtsets() system call.
 * structure shared with user level
 */
struct pfarg_setinfo {
	__u16	set_id;			/* which set */
	__u16	set_reserved1;		/* for future use */
	__u32	set_flags;		/* out: SETFL flags */
	__u64 	set_ovfl_pmds[PFM_PMD_BV]; /* out: last ovfl PMDs */
	__u64	set_runs;		/* out: #times the set was active */
	__u64	set_timeout;		/* out: eff/leftover timeout (nsecs) */
	__u64	set_act_duration;	/* out: time set was active in nsecs */
	__u64	set_avail_pmcs[PFM_PMC_BV];/* out: available PMCs */
	__u64	set_avail_pmds[PFM_PMD_BV];/* out: available PMDs */
	__u64	set_reserved3[6];	/* for future use */
};

/*
 * default value for the user and group security parameters in
 * /proc/sys/kernel/perfmon/sys_group
 * /proc/sys/kernel/perfmon/task_group
 */
#define PFM_GROUP_PERM_ANY	-1	/* any user/group */

/*
 * overflow notification message.
 * structure shared with user level
 */
struct pfarg_ovfl_msg {
	__u32 		msg_type;	/* message type: PFM_MSG_OVFL */
	__u32		msg_ovfl_pid;	/* process id */
	__u16 		msg_active_set;	/* active set at overflow */
	__u16 		msg_ovfl_cpu;	/* cpu of PMU interrupt */
	__u32		msg_ovfl_tid;	/* thread id */
	__u64		msg_ovfl_ip;    /* IP on PMU intr */
	__u64		msg_ovfl_pmds[PFM_PMD_BV];/* overflowed PMDs */
};

#define PFM_MSG_OVFL	1	/* an overflow happened */
#define PFM_MSG_END	2	/* task to which context was attached ended */

/*
 * generic notification message (union).
 * union shared with user level
 */
union pfarg_msg {
	__u32	type;
	struct pfarg_ovfl_msg pfm_ovfl_msg;
};

/*
 * perfmon version number
 */
#define PFM_VERSION_MAJ		 2U
#define PFM_VERSION_MIN		 82U
#define PFM_VERSION		 (((PFM_VERSION_MAJ&0xffff)<<16)|\
				  (PFM_VERSION_MIN & 0xffff))
#define PFM_VERSION_MAJOR(x)	 (((x)>>16) & 0xffff)
#define PFM_VERSION_MINOR(x)	 ((x) & 0xffff)

#endif /* __LINUX_PERFMON_H__ */
