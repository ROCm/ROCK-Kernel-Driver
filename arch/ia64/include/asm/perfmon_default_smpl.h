/*
 * Copyright (c) 2002-2006 Hewlett-Packard Development Company, L.P.
 * Contributed by Stephane Eranian <eranian@hpl.hp.com>
 *
 * This file implements the old default sampling buffer format
 * for the perfmon2 subsystem. For IA-64 only.
 *
 * It requires the use of the perfmon_compat.h header. It is recommended
 * that applications be ported to the new format instead.
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
#ifndef __ASM_IA64_PERFMON_DEFAULT_SMPL_H__
#define __ASM_IA64_PERFMON_DEFAULT_SMPL_H__ 1

#ifndef __ia64__
#error "this file must be used for compatibility reasons only on IA-64"
#endif

#define PFM_DEFAULT_SMPL_UUID { \
		0x4d, 0x72, 0xbe, 0xc0, 0x06, 0x64, 0x41, 0x43, 0x82,\
		0xb4, 0xd3, 0xfd, 0x27, 0x24, 0x3c, 0x97}

/*
 * format specific parameters (passed at context creation)
 */
struct pfm_default_smpl_arg {
	unsigned long buf_size;		/* size of the buffer in bytes */
	unsigned int  flags;		/* buffer specific flags */
	unsigned int  res1;		/* for future use */
	unsigned long reserved[2];	/* for future use */
};

/*
 * combined context+format specific structure. Can be passed
 * to PFM_CONTEXT_CREATE (not PFM_CONTEXT_CREATE2)
 */
struct pfm_default_smpl_ctx_arg {
	struct pfarg_context		ctx_arg;
	struct pfm_default_smpl_arg	buf_arg;
};

/*
 * This header is at the beginning of the sampling buffer returned to the user.
 * It is directly followed by the first record.
 */
struct pfm_default_smpl_hdr {
	u64	hdr_count;	/* how many valid entries */
	u64	hdr_cur_offs;	/* current offset from top of buffer */
	u64	dr_reserved2;	/* reserved for future use */

	u64	hdr_overflows;	/* how many times the buffer overflowed */
	u64	hdr_buf_size;	/* how many bytes in the buffer */

	u32	hdr_version;	/* smpl format version*/
	u32	hdr_reserved1;		/* for future use */
	u64	hdr_reserved[10];	/* for future use */
};

/*
 * Entry header in the sampling buffer.  The header is directly followed
 * with the values of the PMD registers of interest saved in increasing
 * index order: PMD4, PMD5, and so on. How many PMDs are present depends
 * on how the session was programmed.
 *
 * In the case where multiple counters overflow at the same time, multiple
 * entries are written consecutively.
 *
 * last_reset_value member indicates the initial value of the overflowed PMD.
 */
struct pfm_default_smpl_entry {
	pid_t	pid;		/* thread id (for NPTL, this is gettid()) */
	uint8_t	reserved1[3];	/* for future use */
	uint8_t	ovfl_pmd;	/* overflow pmd for this sample */
	u64	last_reset_val;	/* initial value of overflowed PMD */
	unsigned long ip;	/* where did the overflow interrupt happened */
	u64	tstamp; 	/* overflow timetamp */
	u16	cpu;  		/* cpu on which the overfow occured */
	u16	set;  		/* event set active when overflow ocurred   */
	pid_t	tgid;		/* thread group id (for NPTL, this is getpid()) */
};

#define PFM_DEFAULT_MAX_PMDS		64 /* #pmds supported  */
#define PFM_DEFAULT_MAX_ENTRY_SIZE	(sizeof(struct pfm_default_smpl_entry)+\
					 (sizeof(u64)*PFM_DEFAULT_MAX_PMDS))
#define PFM_DEFAULT_SMPL_MIN_BUF_SIZE	(sizeof(struct pfm_default_smpl_hdr)+\
					 PFM_DEFAULT_MAX_ENTRY_SIZE)

#define PFM_DEFAULT_SMPL_VERSION_MAJ	2U
#define PFM_DEFAULT_SMPL_VERSION_MIN 1U
#define PFM_DEFAULT_SMPL_VERSION (((PFM_DEFAULT_SMPL_VERSION_MAJ&0xffff)<<16)|\
				    (PFM_DEFAULT_SMPL_VERSION_MIN & 0xffff))

#endif /* __ASM_IA64_PERFMON_DEFAULT_SMPL_H__ */
