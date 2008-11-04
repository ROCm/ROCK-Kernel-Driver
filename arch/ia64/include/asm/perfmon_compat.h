/*
 * Copyright (c) 2001-2006 Hewlett-Packard Development Company, L.P.
 * Contributed by Stephane Eranian <eranian@hpl.hp.com>
 *
 * This header file contains perfmon interface definition
 * that are now obsolete and should be dropped in favor
 * of their equivalent functions as explained below.
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

#ifndef _ASM_IA64_PERFMON_COMPAT_H_
#define _ASM_IA64_PERFMON_COMPAT_H_

/*
 * custom sampling buffer identifier type
 */
typedef __u8 pfm_uuid_t[16];

/*
 * obsolete perfmon commands. Supported only on IA-64 for
 * backward compatiblity reasons with perfmon v2.0.
 */
#define PFM_WRITE_PMCS		0x01 /* use pfm_write_pmcs */
#define PFM_WRITE_PMDS		0x02 /* use pfm_write_pmds */
#define PFM_READ_PMDS		0x03 /* use pfm_read_pmds */
#define PFM_STOP		0x04 /* use pfm_stop */
#define PFM_START		0x05 /* use pfm_start */
#define PFM_ENABLE		0x06 /* obsolete */
#define PFM_DISABLE		0x07 /* obsolete */
#define PFM_CREATE_CONTEXT	0x08 /* use pfm_create_context */
#define PFM_DESTROY_CONTEXT	0x09 /* use close() */
#define PFM_RESTART		0x0a /* use pfm_restart */
#define PFM_PROTECT_CONTEXT	0x0b /* obsolete */
#define PFM_GET_FEATURES	0x0c /* use /proc/sys/perfmon */
#define PFM_DEBUG		0x0d /* /proc/sys/kernel/perfmon/debug */
#define PFM_UNPROTECT_CONTEXT	0x0e /* obsolete */
#define PFM_GET_PMC_RESET_VAL	0x0f /* use /proc/perfmon_map */
#define PFM_LOAD_CONTEXT	0x10 /* use pfm_load_context */
#define PFM_UNLOAD_CONTEXT	0x11 /* use pfm_unload_context */

/*
 * PMU model specific commands (may not be supported on all PMU models)
 */
#define PFM_WRITE_IBRS		0x20 /* obsolete: use PFM_WRITE_PMCS[256-263]*/
#define PFM_WRITE_DBRS		0x21 /* obsolete: use PFM_WRITE_PMCS[264-271]*/

/*
 * argument to PFM_CREATE_CONTEXT
 */
struct pfarg_context {
	pfm_uuid_t     ctx_smpl_buf_id;	 /* buffer format to use */
	unsigned long  ctx_flags;	 /* noblock/block */
	unsigned int   ctx_reserved1;	 /* for future use */
	int	       ctx_fd;		 /* return: fildesc */
	void	       *ctx_smpl_vaddr;	 /* return: vaddr of buffer */
	unsigned long  ctx_reserved3[11];/* for future use */
};

/*
 * argument structure for PFM_WRITE_PMCS/PFM_WRITE_PMDS/PFM_WRITE_PMDS
 */
struct pfarg_reg {
	unsigned int	reg_num;	   /* which register */
	unsigned short	reg_set;	   /* event set for this register */
	unsigned short	reg_reserved1;	   /* for future use */

	unsigned long	reg_value;	   /* initial pmc/pmd value */
	unsigned long	reg_flags;	   /* input: flags, ret: error */

	unsigned long	reg_long_reset;	   /* reset value after notification */
	unsigned long	reg_short_reset;   /* reset after counter overflow */

	unsigned long	reg_reset_pmds[4]; /* registers to reset on overflow */
	unsigned long	reg_random_seed;   /* seed for randomization */
	unsigned long	reg_random_mask;   /* random range limit */
	unsigned long   reg_last_reset_val;/* return: PMD last reset value */

	unsigned long	reg_smpl_pmds[4];  /* pmds to be saved on overflow */
	unsigned long	reg_smpl_eventid;  /* opaque sampling event id */
	unsigned long   reg_ovfl_switch_cnt;/* #overflows to switch */

	unsigned long   reg_reserved2[2];   /* for future use */
};

/*
 * argument to PFM_WRITE_IBRS/PFM_WRITE_DBRS
 */
struct pfarg_dbreg {
	unsigned int	dbreg_num;		/* which debug register */
	unsigned short	dbreg_set;		/* event set */
	unsigned short	dbreg_reserved1;	/* for future use */
	unsigned long	dbreg_value;		/* value for debug register */
	unsigned long	dbreg_flags;		/* return: dbreg error */
	unsigned long	dbreg_reserved2[1];	/* for future use */
};

/*
 * argument to PFM_GET_FEATURES
 */
struct pfarg_features {
	unsigned int	ft_version;	/* major [16-31], minor [0-15] */
	unsigned int	ft_reserved;	/* reserved for future use */
	unsigned long	reserved[4];	/* for future use */
};

typedef struct {
	int		msg_type;		/* generic message header */
	int		msg_ctx_fd;		/* generic message header */
	unsigned long	msg_ovfl_pmds[4];	/* which PMDs overflowed */
	unsigned short  msg_active_set;		/* active set on overflow */
	unsigned short  msg_reserved1;		/* for future use */
	unsigned int    msg_reserved2;		/* for future use */
	unsigned long	msg_tstamp;		/* for perf tuning/debug */
} pfm_ovfl_msg_t;

typedef struct {
	int		msg_type;		/* generic message header */
	int		msg_ctx_fd;		/* generic message header */
	unsigned long	msg_tstamp;		/* for perf tuning */
} pfm_end_msg_t;

typedef struct {
	int		msg_type;		/* type of the message */
	int		msg_ctx_fd;		/* context file descriptor */
	unsigned long	msg_tstamp;		/* for perf tuning */
} pfm_gen_msg_t;

typedef union {
	int type;
	pfm_ovfl_msg_t	pfm_ovfl_msg;
	pfm_end_msg_t	pfm_end_msg;
	pfm_gen_msg_t	pfm_gen_msg;
} pfm_msg_t;

/*
 * PMD/PMC return flags in case of error (ignored on input)
 *
 * reg_flags layout:
 * bit 00-15 : generic flags
 * bits[16-23] : arch-specific flags (see asm/perfmon.h)
 * bit 24-31 : error codes
 *
 * Those flags are used on output and must be checked in case EINVAL is
 * returned by a command accepting a vector of values and each has a flag
 * field, such as pfarg_reg or pfarg_reg
 */
#define PFM_REG_RETFL_NOTAVAIL	(1<<31) /* not implemented or unaccessible */
#define PFM_REG_RETFL_EINVAL	(1<<30) /* entry is invalid */
#define PFM_REG_RETFL_MASK	(PFM_REG_RETFL_NOTAVAIL|\
				 PFM_REG_RETFL_EINVAL)

#define PFM_REG_HAS_ERROR(flag)	(((flag) & PFM_REG_RETFL_MASK) != 0)

#endif /* _ASM_IA64_PERFMON_COMPAT_H_ */
