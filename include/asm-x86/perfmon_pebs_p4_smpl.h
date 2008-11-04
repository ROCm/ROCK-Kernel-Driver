/*
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
 *
 * This file implements the sampling format to support Intel
 * Precise Event Based Sampling (PEBS) feature of Pentium 4
 * and other Netburst-based processors. Not to be used for
 * Intel Core-based processors.
 *
 * What is PEBS?
 * ------------
 *  This is a hardware feature to enhance sampling by providing
 *  better precision as to where a sample is taken. This avoids the
 *  typical skew in the instruction one can observe with any
 *  interrupt-based sampling technique.
 *
 *  PEBS also lowers sampling overhead significantly by having the
 *  processor store samples instead of the OS. PMU interrupt are only
 *  generated after multiple samples are written.
 *
 *  Another benefit of PEBS is that samples can be captured inside
 *  critical sections where interrupts are masked.
 *
 * How does it work?
 *  PEBS effectively implements a Hw buffer. The Os must pass a region
 *  of memory where samples are to be stored. The region can have any
 *  size. The OS must also specify the sampling period to reload. The PMU
 *  will interrupt when it reaches the end of the buffer or a specified
 *  threshold location inside the memory region.
 *
 *  The description of the buffer is stored in the Data Save Area (DS).
 *  The samples are stored sequentially in the buffer. The format of the
 *  buffer is fixed and specified in the PEBS documentation.  The sample
 *  format changes between 32-bit and 64-bit modes due to extended register
 *  file.
 *
 *  PEBS does not work when HyperThreading is enabled due to certain MSR
 *  being shared being to two threads.
 *
 *  What does the format do?
 *   It provides access to the PEBS feature for both 32-bit and 64-bit
 *   processors that support it.
 *
 *   The same code is used for both 32-bit and 64-bit modes, but different
 *   format names are used because the two modes are not compatible due to
 *   data model and register file differences. Similarly the public data
 *   structures describing the samples are different.
 *
 *   It is important to realize that the format provides a zero-copy environment
 *   for the samples, i.e,, the OS never touches the samples. Whatever the
 *   processor write is directly accessible to the user.
 *
 *   Parameters to the buffer can be passed via pfm_create_context() in
 *   the pfm_pebs_smpl_arg structure.
 *
 *   It is not possible to mix a 32-bit PEBS application on top of a 64-bit
 *   host kernel.
 */
#ifndef __PERFMON_PEBS_P4_SMPL_H__
#define __PERFMON_PEBS_P4_SMPL_H__ 1

#ifdef __i386__
/*
 * The 32-bit and 64-bit formats are not compatible, thus we have
 * two different identifications so that 32-bit programs running on
 * 64-bit OS will fail to use the 64-bit PEBS support.
 */
#define PFM_PEBS_P4_SMPL_NAME	"pebs32_p4"
#else
#define PFM_PEBS_P4_SMPL_NAME	"pebs64_p4"
#endif

/*
 * format specific parameters (passed at context creation)
 *
 * intr_thres: index from start of buffer of entry where the
 * PMU interrupt must be triggered. It must be several samples
 * short of the end of the buffer.
 */
struct pfm_pebs_p4_smpl_arg {
	u64 cnt_reset;	  /* counter reset value */
	size_t buf_size;  /* size of the PEBS buffer in bytes */
	size_t intr_thres;/* index of PEBS interrupt threshold entry */
	u64 reserved[6];  /* for future use */
};

/*
 * Data Save Area (32 and 64-bit mode)
 *
 * The DS area must be exposed to the user because this is the only
 * way to report on the number of valid entries recorded by the CPU.
 * This is required when the buffer is not full, i..e, there was not
 * PMU interrupt.
 *
 * Layout of the structure is mandated by hardware and specified in
 * the Intel documentation.
 */
struct pfm_ds_area_p4 {
	unsigned long	bts_buf_base;
	unsigned long	bts_index;
	unsigned long	bts_abs_max;
	unsigned long	bts_intr_thres;
	unsigned long	pebs_buf_base;
	unsigned long	pebs_index;
	unsigned long	pebs_abs_max;
	unsigned long	pebs_intr_thres;
	u64     	pebs_cnt_reset;
};

/*
 * This header is at the beginning of the sampling buffer returned to the user.
 *
 * Because of PEBS alignement constraints, the actual PEBS buffer area does
 * not necessarily begin right after the header. The hdr_start_offs must be
 * used to compute the first byte of the buffer. The offset is defined as
 * the number of bytes between the end of the header and the beginning of
 * the buffer. As such the formula is:
 * 	actual_buffer = (unsigned long)(hdr+1)+hdr->hdr_start_offs
 */
struct pfm_pebs_p4_smpl_hdr {
	u64 overflows;			/* #overflows for buffer */
	size_t buf_size;		/* bytes in the buffer */
	size_t start_offs; 		/* actual buffer start offset */
	u32 version;			/* smpl format version */
	u32 reserved1;			/* for future use */
	u64 reserved2[5];		/* for future use */
	struct pfm_ds_area_p4 ds;	/* data save area */
};

/*
 * 64-bit PEBS record format is described in
 * http://www.intel.com/technology/64bitextensions/30083502.pdf
 *
 * The format does not peek at samples. The sample structure is only
 * used to ensure that the buffer is large enough to accomodate one
 * sample.
 */
#ifdef __i386__
struct pfm_pebs_p4_smpl_entry {
	u32	eflags;
	u32	ip;
	u32	eax;
	u32	ebx;
	u32	ecx;
	u32	edx;
	u32	esi;
	u32	edi;
	u32	ebp;
	u32	esp;
};
#else
struct pfm_pebs_p4_smpl_entry {
	u64	eflags;
	u64	ip;
	u64	eax;
	u64	ebx;
	u64	ecx;
	u64	edx;
	u64	esi;
	u64	edi;
	u64	ebp;
	u64	esp;
	u64	r8;
	u64	r9;
	u64	r10;
	u64	r11;
	u64	r12;
	u64	r13;
	u64	r14;
	u64	r15;
};
#endif

#define PFM_PEBS_P4_SMPL_VERSION_MAJ 1U
#define PFM_PEBS_P4_SMPL_VERSION_MIN 0U
#define PFM_PEBS_P4_SMPL_VERSION (((PFM_PEBS_P4_SMPL_VERSION_MAJ&0xffff)<<16)|\
				   (PFM_PEBS_P4_SMPL_VERSION_MIN & 0xffff))

#endif /* __PERFMON_PEBS_P4_SMPL_H__ */
