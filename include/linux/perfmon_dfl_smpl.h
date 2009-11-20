/*
 * Copyright (c) 2005-2006 Hewlett-Packard Development Company, L.P.
 *               Contributed by Stephane Eranian <eranian@hpl.hp.com>
 *
 * This file implements the new dfl sampling buffer format
 * for perfmon2 subsystem.
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
#ifndef __PERFMON_DFL_SMPL_H__
#define __PERFMON_DFL_SMPL_H__ 1

/*
 * format specific parameters (passed at context creation)
 */
struct pfm_dfl_smpl_arg {
	__u64 buf_size;		/* size of the buffer in bytes */
	__u32 buf_flags;	/* buffer specific flags */
	__u32 reserved1;	/* for future use */
	__u64 reserved[6];	/* for future use */
};

/*
 * This header is at the beginning of the sampling buffer returned to the user.
 * It is directly followed by the first record.
 */
struct pfm_dfl_smpl_hdr {
	__u64 hdr_count;	/* how many valid entries */
	__u64 hdr_cur_offs;	/* current offset from top of buffer */
	__u64 hdr_overflows;	/* #overflows for buffer */
	__u64 hdr_buf_size;	/* bytes in the buffer */
	__u64 hdr_min_buf_space;/* minimal buffer size (internal use) */
	__u32 hdr_version;	/* smpl format version */
	__u32 hdr_buf_flags;	/* copy of buf_flags */
	__u64 hdr_reserved[10];	/* for future use */
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
struct pfm_dfl_smpl_entry {
	__u32	pid;		/* thread id (for NPTL, this is gettid()) */
	__u16	ovfl_pmd;	/* index of overflowed PMD for this sample */
	__u16	reserved;	/* for future use */
	__u64	last_reset_val;	/* initial value of overflowed PMD */
	__u64	ip;		/* where did the overflow intr happened */
	__u64	tstamp;		/* overflow timetamp */
	__u16	cpu;		/* cpu on which the overfow occurred */
	__u16	set;		/* event set active when overflow ocurred */
	__u32	tgid;		/* thread group id (getpid() for NPTL) */
};

#define PFM_DFL_SMPL_VERSION_MAJ 1U
#define PFM_DFL_SMPL_VERSION_MIN 0U
#define PFM_DFL_SMPL_VERSION (((PFM_DFL_SMPL_VERSION_MAJ&0xffff)<<16)|\
				(PFM_DFL_SMPL_VERSION_MIN & 0xffff))

#endif /* __PERFMON_DFL_SMPL_H__ */
