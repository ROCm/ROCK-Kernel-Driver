#ifndef __ASM_I386_DPROBES_H__
#define __ASM_I386_DPROBES_H__

/*
 * IBM Dynamic Probes
 * Copyright (c) International Business Machines Corp., 2000
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or 
 * (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.  
 */

/*
 * RPN stack width will always be equal to the machine register width.
 */
#define RPN_STACK_SIZE  	1024
#define CALL_FRAME_SIZE		10
#define NR_NESTED_CALLS		32
/* 1st is a dummy frame, not used by call instruction */
#define CALL_STACK_SIZE		(NR_NESTED_CALLS+1)*CALL_FRAME_SIZE 
#define FMT_LOG_HDR_SIZE 	256

/* Offsets of different items on call stack */
#define OFFSET_SBP		0
#define OFFSET_GV_RANGE		1
#define OFFSET_LV_RANGE		3
#define OFFSET_RPN_STACK_RANGE	5
#define OFFSET_EX_HANDLER	7
#define OFFSET_CALLED_ADDR	8
#define OFFSET_RETURN_ADDR	9

/* 
 * offsets of different repitition entries (3 byte prefix) in the 
 * exception stacktrace buffer.
 */
struct ex_buffer_offset {
	unsigned long st;	/* ex stack traces */
	unsigned long sf;	/* ex stack frames */
	unsigned long rpn;	/* rpn stack entries */
	unsigned long lv;	/* lv entries */
	unsigned long gv;	/* gv entries */
};

/* Token bytes of all available 3 byte prefixes */
#define TOKEN_SEG_FAULT		-2
#define TOKEN_MEMORY_FAULT	-1
#define TOKEN_MEMORY_LOG	0
#define TOKEN_ASCII_LOG		1
#define TOKEN_STACK_TRACE	2
#define TOKEN_STACK_FRAME	3
#define TOKEN_RPN_ENTRY		4
#define TOKEN_LV_ENTRY		5
#define TOKEN_GV_ENTRY		6
#define TOKEN_LOG		7

#define PREFIX_SIZE		3

struct dprobes_struct {
	unsigned long status;	/* status */
	/*
	 * details about the probe that is hit, kept here for quick access at
	 * trap1 time.
	 */
	unsigned long probe_addr;
#ifdef CONFIG_KDB
	unsigned long reset_addr;
#endif
	struct pt_regs *regs;
	struct pt_regs *uregs;
	struct dp_module_struct *mod;
	struct dp_record_struct *rec;

	/* fpu registers are saved here */
	union i387_union fpu_save_area;

	/*
	 * Optional data to store with the log record. This data is collected
	 * in the interpreter and used in the trap1 handler when writing the
	 * log to the specified log target.
	 */
	unsigned long eip;
	unsigned short cs;
	unsigned long esp;
	unsigned short ss;
	unsigned short major, minor;

	/*
	 * per-processor data used by the interpreter.
	 */
	unsigned long rpn_tos, call_tos, log_len, prev_log_len, ex_log_len;
	byte_t * rpn_code;
	byte_t * rpn_ip;
	unsigned short jmp_count;
	byte_t opcode;
	byte_t reserved;
	byte_t ex_pending;
	unsigned long ex_code;
	unsigned long ex_parm1, ex_parm2, ex_hand;
	struct ex_buffer_offset ex_off;
	unsigned long rpn_sbp;
	unsigned long heap_size;

	/*
	 * locks used to handle recursion.
	 */
	spinlock_t lock;
	long sem;

	unsigned long rpn_stack[RPN_STACK_SIZE];
	unsigned long call_stack[CALL_STACK_SIZE];
	unsigned char log[LOG_SIZE];
	unsigned char ex_log[EX_LOG_SIZE];
	unsigned char log_hdr[FMT_LOG_HDR_SIZE];
};

/*
 * status
 *
 * DP_KENREL_PROBE: When this is set, we will not use the pte* fields in the
 * alias structure at trap1 time, we can write directly to the probe_addr.
 *
 * DP_STATUS_ERROR: Contains all the bits to check for error conditions.
 *
 * DP_STATUS_DONE: Contains all the per-probe hit bits that need to be turned
 * off after a probe handler is executed.
 *
 * DP_STATUS_ABORT: Indicates that the designated exit facility should not
 * be called after the original instruction is successfully single-stepped.
 */
#define DP_STATUS_INACTIVE      0x00000000
#define DP_STATUS_ACTIVE	0x00000001

#define DP_STATUS_ERROR		0x00ff0000
#define DP_STATUS_GPF		0x00010000
#define DP_STATUS_PF		0x00020000
#define DP_STATUS_LOG_OVERFLOW	0x00040000

#define DP_STATUS_INTERPRETER   0x01000000
#define DP_STATUS_SS	    	0x02000000
#define DP_KERNEL_PROBE	 	0x04000000
#define DP_USER_PROBE	   	0x08000000
#define DP_STATUS_ABORT		0x10000000
#define DP_STATUS_FIRSTFPU	0x20000000
#define DP_STATUS_DONE		0xffff0000

#define DP_INSTR_BREAKPOINT	0xcc

/* arch-specific rec flags */
#define DP_REC_OPCODE_EMULATED		0x01000000
#define DP_REC_OPCODE_IF_MODIFIER	0x02000000

#ifndef EF_CF
#define EF_CF	0x00000001
#endif
#ifndef EF_TF
#define EF_TF	0x00000100
#endif
#ifndef EF_IE
#define EF_IE	0x00000200
#endif
#ifndef EF_DF
#define EF_DF	0x00000400
#endif
#ifndef EF_RF
#define EF_RF	0x00010000
#endif

#define CR0_TS  0x00000008

#ifndef HAVE_HWFP
#ifdef CONFIG_MATH_EMULATION
#define HAVE_HWFP (boot_cpu_data.hard_math)
#else
#define HAVE_HWFP 1
#endif
#endif

/*
 *  Function declarations
 */
extern void dp_interpreter(void);
extern int dp_gpf(struct pt_regs *);
extern int dp_pf(struct pt_regs *);
extern int dp_handle_fault(struct pt_regs *);
extern int dp_do_debug(struct pt_regs *, unsigned long);
extern int dp_trap1(struct pt_regs *);
extern int dp_trap(struct pt_regs *, int, unsigned int, unsigned long);
extern int __remove_probe(byte_t *, struct dp_record_struct *);
extern int __insert_probe(byte_t *, struct dp_record_struct *, struct dp_module_struct *, struct page *);
extern int register_userspace_probes(struct dp_record_struct *);
extern void unregister_userspace_probes(struct dp_record_struct *);
extern inline int insert_probe_userspace(byte_t *, struct dp_record_struct *, struct page *, struct vm_area_struct *);
extern inline int remove_probe_userspace(byte_t *, struct dp_record_struct *, struct page *, struct vm_area_struct *);

#endif
