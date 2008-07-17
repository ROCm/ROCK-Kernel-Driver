/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (c) 2006, 2007-2008 Silicon Graphics, Inc.  All Rights Reserved.
 *
 * Common code for doing accurate backtraces on i386 and x86_64, including
 * printing the values of arguments.
 */

#include <linux/init.h>
#include <linux/kallsyms.h>
#include <linux/kdb.h>
#include <linux/kdbprivate.h>
#include <linux/ctype.h>
#include <linux/string.h>
#include <linux/stringify.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/nmi.h>
#include <asm/asm-offsets.h>
#include <asm/system.h>

#define KDB_DEBUG_BB(fmt, ...)							\
	{if (KDB_DEBUG(BB)) kdb_printf(fmt, ## __VA_ARGS__);}
#define KDB_DEBUG_BB_OFFSET_PRINTF(offset, prefix, suffix)			\
	kdb_printf(prefix "%c0x%x" suffix,					\
		   offset >= 0 ? '+' : '-',					\
		   offset >= 0 ? offset : -offset)
#define KDB_DEBUG_BB_OFFSET(offset, prefix, suffix)				\
	{if (KDB_DEBUG(BB)) KDB_DEBUG_BB_OFFSET_PRINTF(offset, prefix, suffix);}

#define	BB_CHECK(expr, val, ret)						\
({										\
	if (unlikely(expr)) {							\
		kdb_printf("%s, line %d: BB_CHECK(" #expr ") failed "		\
			#val "=%lx\n",						\
			__FUNCTION__, __LINE__, (long)val);			\
		bb_giveup = 1;							\
		return ret;							\
	}									\
})

static int bb_giveup;

/* Use BBRG_Rxx for both i386 and x86_64.  RAX through R15 must be at the end,
 * starting with RAX.  Some of these codes do not reflect actual registers,
 * such codes are special cases when parsing the record of register changes.
 * When updating BBRG_ entries, update bbrg_name as well.
 */

enum bb_reg_code
{
	BBRG_UNDEFINED = 0,	/* Register contents are undefined */
	BBRG_OSP,		/* original stack pointer on entry to function */
	BBRG_RAX,
	BBRG_RBX,
	BBRG_RCX,
	BBRG_RDX,
	BBRG_RDI,
	BBRG_RSI,
	BBRG_RBP,
	BBRG_RSP,
	BBRG_R8,
	BBRG_R9,
	BBRG_R10,
	BBRG_R11,
	BBRG_R12,
	BBRG_R13,
	BBRG_R14,
	BBRG_R15,
};

const static char *bbrg_name[] = {
	[BBRG_UNDEFINED]   = "undefined",
	[BBRG_OSP]         = "osp",
	[BBRG_RAX]         = "rax",
	[BBRG_RBX]         = "rbx",
	[BBRG_RCX]         = "rcx",
	[BBRG_RDX]         = "rdx",
	[BBRG_RDI]         = "rdi",
	[BBRG_RSI]         = "rsi",
	[BBRG_RBP]         = "rbp",
	[BBRG_RSP]         = "rsp",
	[BBRG_R8]          = "r8",
	[BBRG_R9]          = "r9",
	[BBRG_R10]         = "r10",
	[BBRG_R11]         = "r11",
	[BBRG_R12]         = "r12",
	[BBRG_R13]         = "r13",
	[BBRG_R14]         = "r14",
	[BBRG_R15]         = "r15",
};

/* Map a register name to its register code.  This includes the sub-register
 * addressable fields, e.g. parts of rax can be addressed as ax, al, ah, eax.
 * The list is sorted so it can be binary chopped, sort command is:
 *   LANG=C sort -t '"' -k2
 */

struct bb_reg_code_map {
	enum bb_reg_code reg;
	const char *name;
};

const static struct bb_reg_code_map
bb_reg_code_map[] = {
	{ BBRG_RAX, "ah" },
	{ BBRG_RAX, "al" },
	{ BBRG_RAX, "ax" },
	{ BBRG_RBX, "bh" },
	{ BBRG_RBX, "bl" },
	{ BBRG_RBP, "bp" },
	{ BBRG_RBP, "bpl" },
	{ BBRG_RBX, "bx" },
	{ BBRG_RCX, "ch" },
	{ BBRG_RCX, "cl" },
	{ BBRG_RCX, "cx" },
	{ BBRG_RDX, "dh" },
	{ BBRG_RDI, "di" },
	{ BBRG_RDI, "dil" },
	{ BBRG_RDX, "dl" },
	{ BBRG_RDX, "dx" },
	{ BBRG_RAX, "eax" },
	{ BBRG_RBP, "ebp" },
	{ BBRG_RBX, "ebx" },
	{ BBRG_RCX, "ecx" },
	{ BBRG_RDI, "edi" },
	{ BBRG_RDX, "edx" },
	{ BBRG_RSI, "esi" },
	{ BBRG_RSP, "esp" },
	{ BBRG_R10, "r10" },
	{ BBRG_R10, "r10d" },
	{ BBRG_R10, "r10l" },
	{ BBRG_R10, "r10w" },
	{ BBRG_R11, "r11" },
	{ BBRG_R11, "r11d" },
	{ BBRG_R11, "r11l" },
	{ BBRG_R11, "r11w" },
	{ BBRG_R12, "r12" },
	{ BBRG_R12, "r12d" },
	{ BBRG_R12, "r12l" },
	{ BBRG_R12, "r12w" },
	{ BBRG_R13, "r13" },
	{ BBRG_R13, "r13d" },
	{ BBRG_R13, "r13l" },
	{ BBRG_R13, "r13w" },
	{ BBRG_R14, "r14" },
	{ BBRG_R14, "r14d" },
	{ BBRG_R14, "r14l" },
	{ BBRG_R14, "r14w" },
	{ BBRG_R15, "r15" },
	{ BBRG_R15, "r15d" },
	{ BBRG_R15, "r15l" },
	{ BBRG_R15, "r15w" },
	{ BBRG_R8,  "r8" },
	{ BBRG_R8,  "r8d" },
	{ BBRG_R8,  "r8l" },
	{ BBRG_R8,  "r8w" },
	{ BBRG_R9,  "r9" },
	{ BBRG_R9,  "r9d" },
	{ BBRG_R9,  "r9l" },
	{ BBRG_R9,  "r9w" },
	{ BBRG_RAX, "rax" },
	{ BBRG_RBP, "rbp" },
	{ BBRG_RBX, "rbx" },
	{ BBRG_RCX, "rcx" },
	{ BBRG_RDI, "rdi" },
	{ BBRG_RDX, "rdx" },
	{ BBRG_RSI, "rsi" },
	{ BBRG_RSP, "rsp" },
	{ BBRG_RSI, "si" },
	{ BBRG_RSI, "sil" },
	{ BBRG_RSP, "sp" },
	{ BBRG_RSP, "spl" },
};

/* Record register contents in terms of the values that were passed to this
 * function, IOW track which registers contain an input value.  A register's
 * contents can be undefined, it can contain an input register value or it can
 * contain an offset from the original stack pointer.
 *
 * This structure is used to represent the current contents of the integer
 * registers, it is held in an array that is indexed by BBRG_xxx.  The element
 * for BBRG_xxx indicates what input value is currently in BBRG_xxx.  When
 * 'value' is BBRG_OSP then register BBRG_xxx contains a stack pointer,
 * pointing at 'offset' from the original stack pointer on entry to the
 * function.  When 'value' is not BBRG_OSP then element BBRG_xxx contains the
 * original contents of an input register and offset is ignored.
 *
 * An input register 'value' can be stored in more than one register and/or in
 * more than one memory location.
 */

struct bb_reg_contains
{
	enum bb_reg_code value: 8;
	short offset;
};

/* Note: the offsets in struct bb_mem_contains in this code are _NOT_ offsets
 * from OSP, they are offsets from current RSP.  It fits better with the way
 * that struct pt_regs is built, some code pushes extra data before pt_regs so
 * working with OSP relative offsets gets messy.  struct bb_mem_contains
 * entries must be in descending order of RSP offset.
 */

typedef struct { DECLARE_BITMAP(bits, BBRG_R15+1); } bbrgmask_t;
#define BB_SKIP(reg) (1 << (BBRG_ ## reg))
struct bb_mem_contains {
	short offset_address;
	enum bb_reg_code value: 8;
};

/* Transfer of control to a label outside the current function.  If the
 * transfer is to a known common restore path that expects known registers
 * and/or a known memory state (e.g. struct pt_regs) then do a sanity check on
 * the state at this point.
 */

struct bb_name_state {
	const char *name;			/* target function */
	bfd_vma address;			/* Address of target function */
	const char *fname;			/* optional from function name */
	const struct bb_mem_contains *mem;	/* expected memory state */
	const struct bb_reg_contains *regs;	/* expected register state */
	const unsigned short mem_size;		/* ARRAY_SIZE(mem) */
	const unsigned short regs_size;		/* ARRAY_SIZE(regs) */
	const short osp_offset;			/* RSP in regs == OSP+osp_offset */
	const bbrgmask_t skip_mem;		/* Some slots in mem may be undefined */
	const bbrgmask_t skip_regs;		/* Some slots in regs may be undefined */
};

/* NS (NAME_STATE) macros define the register and memory state when we transfer
 * control to or start decoding a special case name.  Use NS when the target
 * label always has the same state.  Use NS_FROM and specify the source label
 * if the target state is slightly different depending on where it is branched
 * from.  This gives better state checking, by isolating the special cases.
 *
 * Note: for the same target label, NS_FROM entries must be followed by a
 * single NS entry.
 */

#define	NS_FROM(iname, ifname, imem, iregs, iskip_mem, iskip_regs, iosp_offset) \
	{ \
		.name = iname, \
		.fname = ifname, \
		.mem = imem, \
		.regs = iregs, \
		.mem_size = ARRAY_SIZE(imem), \
		.regs_size = ARRAY_SIZE(iregs), \
		.skip_mem.bits[0] = iskip_mem, \
		.skip_regs.bits[0] = iskip_regs, \
		.osp_offset = iosp_offset, \
       		.address = 0 \
	}

/* Shorter forms for the common cases */
#define	NS(iname, imem, iregs, iskip_mem, iskip_regs, iosp_offset) \
	  NS_FROM(iname, NULL, imem, iregs, iskip_mem, iskip_regs, iosp_offset)
#define	NS_MEM(iname, imem, iskip_mem) \
	  NS_FROM(iname, NULL, imem, no_regs, iskip_mem, 0, 0)
#define	NS_MEM_FROM(iname, ifname, imem, iskip_mem) \
	  NS_FROM(iname, ifname, imem, no_regs, iskip_mem, 0, 0)
#define	NS_REG(iname, iregs, iskip_regs) \
	  NS_FROM(iname, NULL, no_memory, iregs, 0, iskip_regs, 0)
#define	NS_REG_FROM(iname, ifname, iregs, iskip_regs) \
	  NS_FROM(iname, ifname, no_memory, iregs, 0, iskip_regs, 0)

static void
bb_reg_code_set_value(enum bb_reg_code dst, enum bb_reg_code src);

static const char *bb_mod_name, *bb_func_name;

static int
bb_noret(const char *name)
{
	if (strcmp(name, "panic") == 0 ||
	    strcmp(name, "do_exit") == 0 ||
	    strcmp(name, "do_group_exit") == 0 ||
	    strcmp(name, "complete_and_exit") == 0)
		return 1;
	return 0;
}

/*============================================================================*/
/*                                                                            */
/* Most of the basic block code and data is common to x86_64 and i386.  This  */
/* large ifdef  contains almost all of the differences between the two        */
/* architectures.                                                             */
/*                                                                            */
/* Make sure you update the correct section of this ifdef.                    */
/*                                                                            */
/*============================================================================*/

#ifdef	CONFIG_X86_64

/* Registers that can be used to pass parameters, in the order that parameters
 * are passed.
 */

const static enum bb_reg_code
bb_param_reg[] = {
	BBRG_RDI,
	BBRG_RSI,
	BBRG_RDX,
	BBRG_RCX,
	BBRG_R8,
	BBRG_R9,
};

const static enum bb_reg_code
bb_preserved_reg[] = {
	BBRG_RBX,
	BBRG_RBP,
	BBRG_RSP,
	BBRG_R12,
	BBRG_R13,
	BBRG_R14,
	BBRG_R15,
};

static const struct bb_mem_contains full_pt_regs[] = {
	{ 0x70, BBRG_RDI },
	{ 0x68, BBRG_RSI },
	{ 0x60, BBRG_RDX },
	{ 0x58, BBRG_RCX },
	{ 0x50, BBRG_RAX },
	{ 0x48, BBRG_R8  },
	{ 0x40, BBRG_R9  },
	{ 0x38, BBRG_R10 },
	{ 0x30, BBRG_R11 },
	{ 0x28, BBRG_RBX },
	{ 0x20, BBRG_RBP },
	{ 0x18, BBRG_R12 },
	{ 0x10, BBRG_R13 },
	{ 0x08, BBRG_R14 },
	{ 0x00, BBRG_R15 },
};
static const struct bb_mem_contains partial_pt_regs[] = {
	{ 0x40, BBRG_RDI },
	{ 0x38, BBRG_RSI },
	{ 0x30, BBRG_RDX },
	{ 0x28, BBRG_RCX },
	{ 0x20, BBRG_RAX },
	{ 0x18, BBRG_R8  },
	{ 0x10, BBRG_R9  },
	{ 0x08, BBRG_R10 },
	{ 0x00, BBRG_R11 },
};
static const struct bb_mem_contains partial_pt_regs_plus_1[] = {
	{ 0x48, BBRG_RDI },
	{ 0x40, BBRG_RSI },
	{ 0x38, BBRG_RDX },
	{ 0x30, BBRG_RCX },
	{ 0x28, BBRG_RAX },
	{ 0x20, BBRG_R8  },
	{ 0x18, BBRG_R9  },
	{ 0x10, BBRG_R10 },
	{ 0x08, BBRG_R11 },
};
static const struct bb_mem_contains partial_pt_regs_plus_2[] = {
	{ 0x50, BBRG_RDI },
	{ 0x48, BBRG_RSI },
	{ 0x40, BBRG_RDX },
	{ 0x38, BBRG_RCX },
	{ 0x30, BBRG_RAX },
	{ 0x28, BBRG_R8  },
	{ 0x20, BBRG_R9  },
	{ 0x18, BBRG_R10 },
	{ 0x10, BBRG_R11 },
};
static const struct bb_mem_contains no_memory[] = {
};
/* Hardware has already pushed an error_code on the stack.  Use undefined just
 * to set the initial stack offset.
 */
static const struct bb_mem_contains error_code[] = {
	{ 0x0, BBRG_UNDEFINED },
};
/* error_code plus original rax */
static const struct bb_mem_contains error_code_rax[] = {
	{ 0x8, BBRG_UNDEFINED },
	{ 0x0, BBRG_RAX },
};

static const struct bb_reg_contains all_regs[] = {
	[BBRG_RAX] = { BBRG_RAX, 0 },
	[BBRG_RBX] = { BBRG_RBX, 0 },
	[BBRG_RCX] = { BBRG_RCX, 0 },
	[BBRG_RDX] = { BBRG_RDX, 0 },
	[BBRG_RDI] = { BBRG_RDI, 0 },
	[BBRG_RSI] = { BBRG_RSI, 0 },
	[BBRG_RBP] = { BBRG_RBP, 0 },
	[BBRG_RSP] = { BBRG_OSP, 0 },
	[BBRG_R8 ] = { BBRG_R8,  0 },
	[BBRG_R9 ] = { BBRG_R9,  0 },
	[BBRG_R10] = { BBRG_R10, 0 },
	[BBRG_R11] = { BBRG_R11, 0 },
	[BBRG_R12] = { BBRG_R12, 0 },
	[BBRG_R13] = { BBRG_R13, 0 },
	[BBRG_R14] = { BBRG_R14, 0 },
	[BBRG_R15] = { BBRG_R15, 0 },
};
static const struct bb_reg_contains no_regs[] = {
};

static struct bb_name_state bb_special_cases[] = {

	/* First the cases that pass data only in memory.  We do not check any
	 * register state for these cases.
	 */

	/* Simple cases, no exceptions */
	NS_MEM("ia32_ptregs_common", partial_pt_regs_plus_1, 0),
	NS_MEM("ia32_sysret", partial_pt_regs, 0),
	NS_MEM("int_careful", partial_pt_regs, 0),
	NS_MEM("int_restore_rest", full_pt_regs, 0),
	NS_MEM("int_signal", full_pt_regs, 0),
	NS_MEM("int_very_careful", partial_pt_regs, 0),
	NS_MEM("int_with_check", partial_pt_regs, 0),
#ifdef	CONFIG_TRACE_IRQFLAGS
	NS_MEM("paranoid_exit0", full_pt_regs, 0),
#endif	/* CONFIG_TRACE_IRQFLAGS */
	NS_MEM("paranoid_exit1", full_pt_regs, 0),
	NS_MEM("ptregscall_common", partial_pt_regs_plus_1, 0),
	NS_MEM("restore_norax", partial_pt_regs, 0),
	NS_MEM("restore", partial_pt_regs, 0),
	NS_MEM("ret_from_intr", partial_pt_regs_plus_2, 0),
	NS_MEM("stub32_clone", partial_pt_regs_plus_1, 0),
	NS_MEM("stub32_execve", partial_pt_regs_plus_1, 0),
	NS_MEM("stub32_fork", partial_pt_regs_plus_1, 0),
	NS_MEM("stub32_iopl", partial_pt_regs_plus_1, 0),
	NS_MEM("stub32_rt_sigreturn", partial_pt_regs_plus_1, 0),
	NS_MEM("stub32_rt_sigsuspend", partial_pt_regs_plus_1, 0),
	NS_MEM("stub32_sigaltstack", partial_pt_regs_plus_1, 0),
	NS_MEM("stub32_sigreturn", partial_pt_regs_plus_1, 0),
	NS_MEM("stub32_sigsuspend", partial_pt_regs_plus_1, 0),
	NS_MEM("stub32_vfork", partial_pt_regs_plus_1, 0),
	NS_MEM("stub_clone", partial_pt_regs_plus_1, 0),
	NS_MEM("stub_execve", partial_pt_regs_plus_1, 0),
	NS_MEM("stub_fork", partial_pt_regs_plus_1, 0),
	NS_MEM("stub_iopl", partial_pt_regs_plus_1, 0),
	NS_MEM("stub_rt_sigreturn", partial_pt_regs_plus_1, 0),
	NS_MEM("stub_rt_sigsuspend", partial_pt_regs_plus_1, 0),
	NS_MEM("stub_sigaltstack", partial_pt_regs_plus_1, 0),
	NS_MEM("stub_vfork", partial_pt_regs_plus_1, 0),

	NS_MEM_FROM("ia32_badsys", "ia32_sysenter_target",
		partial_pt_regs,
		/* ia32_sysenter_target uses CLEAR_RREGS to clear R8-R11 on
		 * some paths.  It also stomps on RAX.
		 */
		BB_SKIP(R8) | BB_SKIP(R9) | BB_SKIP(R10) | BB_SKIP(R11) |
		BB_SKIP(RAX)),
	NS_MEM_FROM("ia32_badsys", "ia32_cstar_target",
		partial_pt_regs,
		/* ia32_cstar_target uses CLEAR_RREGS to clear R8-R11 on some
		 * paths.  It also stomps on RAX.  Even more confusing, instead
		 * of storing RCX it stores RBP.  WTF?
		 */
		BB_SKIP(R8) | BB_SKIP(R9) | BB_SKIP(R10) | BB_SKIP(R11) |
		BB_SKIP(RAX) | BB_SKIP(RCX)),
	NS_MEM("ia32_badsys", partial_pt_regs, 0),

	/* Various bits of code branch to int_ret_from_sys_call, with slightly
	 * different missing values in pt_regs.
	 */
	NS_MEM_FROM("int_ret_from_sys_call", "ret_from_fork",
		partial_pt_regs,
		BB_SKIP(R11)),
	NS_MEM_FROM("int_ret_from_sys_call", "stub_execve",
		partial_pt_regs,
		BB_SKIP(RAX) | BB_SKIP(RCX)),
	NS_MEM_FROM("int_ret_from_sys_call", "stub_rt_sigreturn",
		partial_pt_regs,
		BB_SKIP(RAX) | BB_SKIP(RCX)),
	NS_MEM_FROM("int_ret_from_sys_call", "kernel_execve",
		partial_pt_regs,
		BB_SKIP(RAX)),
	NS_MEM_FROM("int_ret_from_sys_call", "ia32_syscall",
		partial_pt_regs,
		/* ia32_syscall only saves RDI through RCX. */
		BB_SKIP(R8) | BB_SKIP(R9) | BB_SKIP(R10) | BB_SKIP(R11) |
		BB_SKIP(RAX)),
	NS_MEM_FROM("int_ret_from_sys_call", "ia32_sysenter_target",
		partial_pt_regs,
		/* ia32_sysenter_target uses CLEAR_RREGS to clear R8-R11 on
		* some paths.  It also stomps on RAX.
		*/
		BB_SKIP(R8) | BB_SKIP(R9) | BB_SKIP(R10) | BB_SKIP(R11) |
		BB_SKIP(RAX)),
	NS_MEM_FROM("int_ret_from_sys_call", "ia32_cstar_target",
		partial_pt_regs,
		/* ia32_cstar_target uses CLEAR_RREGS to clear R8-R11 on some
		 * paths.  It also stomps on RAX.  Even more confusing, instead
		 * of storing RCX it stores RBP.  WTF?
		 */
		BB_SKIP(R8) | BB_SKIP(R9) | BB_SKIP(R10) | BB_SKIP(R11) |
		BB_SKIP(RAX) | BB_SKIP(RCX)),
	NS_MEM("int_ret_from_sys_call", partial_pt_regs, 0),

#ifdef	CONFIG_PREEMPT
	NS_MEM("retint_kernel", partial_pt_regs, BB_SKIP(RAX)),
#endif	/* CONFIG_PREEMPT */

	NS_MEM("retint_careful", partial_pt_regs, BB_SKIP(RAX)),

	/* Horrible hack: For a brand new x86_64 task, switch_to() branches to
	 * ret_from_fork with a totally different stack state from all the
	 * other tasks that come out of switch_to().  This non-standard state
	 * cannot be represented so just ignore the branch from switch_to() to
	 * ret_from_fork.  Due to inlining and linker labels, switch_to() can
	 * appear as several different function labels, including schedule,
	 * context_switch and __sched_text_start.
	 */
	NS_MEM_FROM("ret_from_fork", "schedule", no_memory, 0),
	NS_MEM_FROM("ret_from_fork", "__sched_text_start", no_memory, 0),
	NS_MEM_FROM("ret_from_fork", "context_switch", no_memory, 0),
	NS_MEM("ret_from_fork", full_pt_regs, 0),


	NS_MEM_FROM("ret_from_sys_call", "ret_from_fork",
		partial_pt_regs,
		BB_SKIP(R11)),
	NS_MEM("ret_from_sys_call", partial_pt_regs, 0),

	NS_MEM("retint_restore_args",
		partial_pt_regs,
		BB_SKIP(RAX) | BB_SKIP(RCX)),

	NS_MEM("retint_swapgs",
		partial_pt_regs,
		BB_SKIP(RAX) | BB_SKIP(RCX)),

	/* Now the cases that pass data in registers.  We do not check any
	 * memory state for these cases.
	 */

	NS_REG("bad_put_user",
		all_regs,
		BB_SKIP(RAX) | BB_SKIP(RCX) | BB_SKIP(R8)),

	NS_REG("bad_get_user",
		all_regs,
		BB_SKIP(RAX) | BB_SKIP(RCX) | BB_SKIP(R8)),

	NS_REG("bad_to_user",
		all_regs,
		BB_SKIP(RAX) | BB_SKIP(RCX)),

	NS_REG("ia32_ptregs_common",
		all_regs,
		0),

	NS_REG("copy_user_generic_unrolled",
		all_regs,
		BB_SKIP(RAX) | BB_SKIP(RCX)),

	NS_REG("copy_user_generic_string",
		all_regs,
		BB_SKIP(RAX) | BB_SKIP(RCX)),

	NS_REG("irq_return",
		all_regs,
		0),

	/* Finally the cases that pass data in both registers and memory.
	 */

	NS("invalid_TSS", error_code, all_regs, 0, 0, 0),
	NS("segment_not_present", error_code, all_regs, 0, 0, 0),
	NS("alignment_check", error_code, all_regs, 0, 0, 0),
	NS("page_fault", error_code, all_regs, 0, 0, 0),
	NS("general_protection", error_code, all_regs, 0, 0, 0),
	NS("error_entry", error_code_rax, all_regs, 0, BB_SKIP(RAX), -0x10),
	NS("common_interrupt", error_code, all_regs, 0, 0, -0x8),
};

static const char *bb_spurious[] = {
				/* schedule */
	"thread_return",
				/* ret_from_fork */
	"rff_action",
	"rff_trace",
				/* system_call */
	"ret_from_sys_call",
	"sysret_check",
	"sysret_careful",
	"sysret_signal",
	"badsys",
	"tracesys",
	"int_ret_from_sys_call",
	"int_with_check",
	"int_careful",
	"int_very_careful",
	"int_signal",
	"int_restore_rest",
				/* common_interrupt */
	"ret_from_intr",
	"exit_intr",
	"retint_with_reschedule",
	"retint_check",
	"retint_swapgs",
	"retint_restore_args",
	"restore_args",
	"irq_return",
	"bad_iret",
	"retint_careful",
	"retint_signal",
#ifdef	CONFIG_PREEMPT
	"retint_kernel",
#endif	/* CONFIG_PREEMPT */
				/* .macro paranoidexit */
#ifdef	CONFIG_TRACE_IRQFLAGS
	"paranoid_exit0",
	"paranoid_userspace0",
	"paranoid_restore0",
	"paranoid_swapgs0",
	"paranoid_schedule0",
#endif	/* CONFIG_TRACE_IRQFLAGS */
	"paranoid_exit1",
	"paranoid_swapgs1",
	"paranoid_restore1",
	"paranoid_userspace1",
	"paranoid_schedule1",
				/* error_entry */
	"error_swapgs",
	"error_sti",
	"error_exit",
	"error_kernelspace",
				/* load_gs_index */
	"gs_change",
	"bad_gs",
				/* ia32_sysenter_target */
	"sysenter_do_call",
	"sysenter_tracesys",
				/* ia32_cstar_target */
	"cstar_do_call",
	"cstar_tracesys",
	"ia32_badarg",
				/* ia32_syscall */
	"ia32_do_syscall",
	"ia32_sysret",
	"ia32_tracesys",
	"ia32_badsys",
#ifdef	CONFIG_HIBERNATION
				/* restore_image */
	"loop",
	"done",
#endif	/* CONFIG_HIBERNATION */
#ifdef	CONFIG_KPROBES
				/* jprobe_return */
	"jprobe_return_end",
				/* kretprobe_trampoline_holder */
	"kretprobe_trampoline",
#endif	/* CONFIG_KPROBES */
#ifdef	CONFIG_KEXEC
				/* relocate_kernel */
	"relocate_new_kernel",
#endif	/* CONFIG_KEXEC */
#ifdef	CONFIG_XEN
				/* arch/i386/xen/xen-asm.S */
	"xen_irq_enable_direct_end",
	"xen_irq_disable_direct_end",
	"xen_save_fl_direct_end",
	"xen_restore_fl_direct_end",
	"xen_iret_start_crit",
	"iret_restore_end",
	"xen_iret_end_crit",
	"hyper_iret",
#endif	/* CONFIG_XEN */
};

static const char *bb_hardware_handlers[] = {
	"system_call",
	"common_interrupt",
	"error_entry",
	"debug",
	"nmi",
	"int3",
	"double_fault",
	"stack_segment",
	"machine_check",
	"kdb_call",
};

static int
bb_hardware_pushed_arch(kdb_machreg_t rsp,
			const struct kdb_activation_record *ar)
{
	/* x86_64 interrupt stacks are 16 byte aligned and you must get the
	 * next rsp from stack, it cannot be statically calculated.  Do not
	 * include the word at rsp, it is pushed by hardware but is treated as
	 * a normal software return value.
	 *
	 * When an IST switch occurs (e.g. NMI) then the saved rsp points to
	 * another stack entirely.  Assume that the IST stack is 16 byte
	 * aligned and just return the size of the hardware data on this stack.
	 * The stack unwind code will take care of the stack switch.
	 */
	kdb_machreg_t saved_rsp = *((kdb_machreg_t *)rsp + 3);
	int hardware_pushed = saved_rsp - rsp - KDB_WORD_SIZE;
	if (hardware_pushed < 4 * KDB_WORD_SIZE ||
	    saved_rsp < ar->stack.logical_start ||
	    saved_rsp >= ar->stack.logical_end)
		return 4 * KDB_WORD_SIZE;
	else
		return hardware_pushed;
}

static void
bb_start_block0(void)
{
	bb_reg_code_set_value(BBRG_RAX, BBRG_RAX);
	bb_reg_code_set_value(BBRG_RBX, BBRG_RBX);
	bb_reg_code_set_value(BBRG_RCX, BBRG_RCX);
	bb_reg_code_set_value(BBRG_RDX, BBRG_RDX);
	bb_reg_code_set_value(BBRG_RDI, BBRG_RDI);
	bb_reg_code_set_value(BBRG_RSI, BBRG_RSI);
	bb_reg_code_set_value(BBRG_RBP, BBRG_RBP);
	bb_reg_code_set_value(BBRG_RSP, BBRG_OSP);
	bb_reg_code_set_value(BBRG_R8, BBRG_R8);
	bb_reg_code_set_value(BBRG_R9, BBRG_R9);
	bb_reg_code_set_value(BBRG_R10, BBRG_R10);
	bb_reg_code_set_value(BBRG_R11, BBRG_R11);
	bb_reg_code_set_value(BBRG_R12, BBRG_R12);
	bb_reg_code_set_value(BBRG_R13, BBRG_R13);
	bb_reg_code_set_value(BBRG_R14, BBRG_R14);
	bb_reg_code_set_value(BBRG_R15, BBRG_R15);
}

/* x86_64 does not have a special case for __switch_to */

static void
bb_fixup_switch_to(char *p)
{
}

static int
bb_asmlinkage_arch(void)
{
	return strncmp(bb_func_name, "__down", 6) == 0 ||
	       strncmp(bb_func_name, "__up", 4) == 0 ||
	       strncmp(bb_func_name, "stub_", 5) == 0 ||
	       strcmp(bb_func_name, "ret_from_fork") == 0 ||
	       strcmp(bb_func_name, "ptregscall_common") == 0;
}

#else	/* !CONFIG_X86_64 */

/* Registers that can be used to pass parameters, in the order that parameters
 * are passed.
 */

const static enum bb_reg_code
bb_param_reg[] = {
	BBRG_RAX,
	BBRG_RDX,
	BBRG_RCX,
};

const static enum bb_reg_code
bb_preserved_reg[] = {
	BBRG_RBX,
	BBRG_RBP,
	BBRG_RSP,
	BBRG_RSI,
	BBRG_RDI,
};

static const struct bb_mem_contains full_pt_regs[] = {
	{ 0x18, BBRG_RAX },
	{ 0x14, BBRG_RBP },
	{ 0x10, BBRG_RDI },
	{ 0x0c, BBRG_RSI },
	{ 0x08, BBRG_RDX },
	{ 0x04, BBRG_RCX },
	{ 0x00, BBRG_RBX },
};
static const struct bb_mem_contains no_memory[] = {
};
/* Hardware has already pushed an error_code on the stack.  Use undefined just
 * to set the initial stack offset.
 */
static const struct bb_mem_contains error_code[] = {
	{ 0x0, BBRG_UNDEFINED },
};
/* rbx already pushed */
static const struct bb_mem_contains rbx_pushed[] = {
	{ 0x0, BBRG_RBX },
};
#ifdef	CONFIG_MATH_EMULATION
static const struct bb_mem_contains mem_fpu_reg_round[] = {
	{ 0xc, BBRG_RBP },
	{ 0x8, BBRG_RSI },
	{ 0x4, BBRG_RDI },
	{ 0x0, BBRG_RBX },
};
#endif	/* CONFIG_MATH_EMULATION */

static const struct bb_reg_contains all_regs[] = {
	[BBRG_RAX] = { BBRG_RAX, 0 },
	[BBRG_RBX] = { BBRG_RBX, 0 },
	[BBRG_RCX] = { BBRG_RCX, 0 },
	[BBRG_RDX] = { BBRG_RDX, 0 },
	[BBRG_RDI] = { BBRG_RDI, 0 },
	[BBRG_RSI] = { BBRG_RSI, 0 },
	[BBRG_RBP] = { BBRG_RBP, 0 },
	[BBRG_RSP] = { BBRG_OSP, 0 },
};
static const struct bb_reg_contains no_regs[] = {
};
#ifdef	CONFIG_MATH_EMULATION
static const struct bb_reg_contains reg_fpu_reg_round[] = {
	[BBRG_RBP] = { BBRG_OSP, -0x4 },
	[BBRG_RSP] = { BBRG_OSP, -0x10 },
};
#endif	/* CONFIG_MATH_EMULATION */

static struct bb_name_state bb_special_cases[] = {

	/* First the cases that pass data only in memory.  We do not check any
	 * register state for these cases.
	 */

	/* Simple cases, no exceptions */
	NS_MEM("check_userspace", full_pt_regs, 0),
	NS_MEM("device_not_available_emulate", full_pt_regs, 0),
	NS_MEM("ldt_ss", full_pt_regs, 0),
	NS_MEM("no_singlestep", full_pt_regs, 0),
	NS_MEM("restore_all", full_pt_regs, 0),
	NS_MEM("restore_nocheck", full_pt_regs, 0),
	NS_MEM("restore_nocheck_notrace", full_pt_regs, 0),
	NS_MEM("ret_from_exception", full_pt_regs, 0),
	NS_MEM("ret_from_fork", full_pt_regs, 0),
	NS_MEM("ret_from_intr", full_pt_regs, 0),
	NS_MEM("work_notifysig", full_pt_regs, 0),
	NS_MEM("work_pending", full_pt_regs, 0),

#ifdef	CONFIG_PREEMPT
	NS_MEM("resume_kernel", full_pt_regs, 0),
#endif	/* CONFIG_PREEMPT */

	NS_MEM("common_interrupt", error_code, 0),
	NS_MEM("error_code", error_code, 0),

	NS_MEM("bad_put_user", rbx_pushed, 0),

	NS_MEM_FROM("resume_userspace", "syscall_badsys",
		full_pt_regs, BB_SKIP(RAX)),
	NS_MEM_FROM("resume_userspace", "syscall_fault",
		full_pt_regs, BB_SKIP(RAX)),
	NS_MEM_FROM("resume_userspace", "syscall_trace_entry",
		full_pt_regs, BB_SKIP(RAX)),
	/* Too difficult to trace through the various vm86 functions for now.
	 * They are C functions that start off with some memory state, fiddle
	 * the registers then jmp directly to resume_userspace.  For the
	 * moment, just assume that they are valid and do no checks.
	 */
	NS_FROM("resume_userspace", "do_int",
		no_memory, no_regs, 0, 0, 0),
	NS_FROM("resume_userspace", "do_sys_vm86",
		no_memory, no_regs, 0, 0, 0),
	NS_FROM("resume_userspace", "handle_vm86_fault",
		no_memory, no_regs, 0, 0, 0),
	NS_FROM("resume_userspace", "handle_vm86_trap",
		no_memory, no_regs, 0, 0, 0),
	NS_MEM("resume_userspace", full_pt_regs, 0),

	NS_MEM_FROM("syscall_badsys", "ia32_sysenter_target",
		full_pt_regs, BB_SKIP(RBP)),
	NS_MEM("syscall_badsys", full_pt_regs, 0),

	NS_MEM_FROM("syscall_call", "syscall_trace_entry",
		full_pt_regs, BB_SKIP(RAX)),
	NS_MEM("syscall_call", full_pt_regs, 0),

	NS_MEM_FROM("syscall_exit", "syscall_trace_entry",
		full_pt_regs, BB_SKIP(RAX)),
	NS_MEM("syscall_exit", full_pt_regs, 0),

	NS_MEM_FROM("syscall_exit_work", "ia32_sysenter_target",
		full_pt_regs, BB_SKIP(RAX) | BB_SKIP(RBP)),
	NS_MEM_FROM("syscall_exit_work", "system_call",
		full_pt_regs, BB_SKIP(RAX)),
	NS_MEM("syscall_exit_work", full_pt_regs, 0),

	NS_MEM_FROM("syscall_trace_entry", "ia32_sysenter_target",
		full_pt_regs, BB_SKIP(RBP)),
	NS_MEM_FROM("syscall_trace_entry", "system_call",
		full_pt_regs, BB_SKIP(RAX)),
	NS_MEM("syscall_trace_entry", full_pt_regs, 0),

	/* Now the cases that pass data in registers.  We do not check any
	 * memory state for these cases.
	 */

	NS_REG("syscall_fault", all_regs, 0),

	NS_REG("bad_get_user", all_regs,
		BB_SKIP(RAX) | BB_SKIP(RDX)),

	/* Finally the cases that pass data in both registers and memory.
	*/

	/* This entry is redundant now because bb_fixup_switch_to() hides the
	 * jmp __switch_to case, however the entry is left here as
	 * documentation.
	 *
	 * NS("__switch_to", no_memory, no_regs, 0, 0, 0),
	 */

	NS("iret_exc", no_memory, all_regs, 0, 0, 0x20),

#ifdef	CONFIG_MATH_EMULATION
	NS("fpu_reg_round", mem_fpu_reg_round, reg_fpu_reg_round, 0, 0, 0),
#endif	/* CONFIG_MATH_EMULATION */
};

static const char *bb_spurious[] = {
				/* ret_from_exception */
	"ret_from_intr",
	"check_userspace",
	"resume_userspace",
				/* resume_kernel */
#ifdef	CONFIG_PREEMPT
	"need_resched",
#endif	/* CONFIG_PREEMPT */
				/* ia32_sysenter_target */
	"sysenter_past_esp",
				/* system_call */
	"no_singlestep",
	"syscall_call",
	"syscall_exit",
	"restore_all",
	"restore_nocheck",
	"restore_nocheck_notrace",
	"ldt_ss",
	/* do not include iret_exc, it is in a .fixup section */
				/* work_pending */
	"work_resched",
	"work_notifysig",
#ifdef	CONFIG_VM86
	"work_notifysig_v86",
#endif	/* CONFIG_VM86 */
				/* page_fault */
	"error_code",
				/* device_not_available */
	"device_not_available_emulate",
				/* debug */
	"debug_esp_fix_insn",
	"debug_stack_correct",
				/* nmi */
	"nmi_stack_correct",
	"nmi_stack_fixup",
	"nmi_debug_stack_check",
	"nmi_espfix_stack",
#ifdef	CONFIG_HIBERNATION
				/* restore_image */
	"copy_loop",
	"done",
#endif	/* CONFIG_HIBERNATION */
#ifdef	CONFIG_KPROBES
				/* jprobe_return */
	"jprobe_return_end",
#endif	/* CONFIG_KPROBES */
#ifdef	CONFIG_KEXEC
				/* relocate_kernel */
	"relocate_new_kernel",
#endif	/* CONFIG_KEXEC */
#ifdef	CONFIG_MATH_EMULATION
				/* assorted *.S files in arch/i386/math_emu */
	"Denorm_done",
	"Denorm_shift_more_than_32",
	"Denorm_shift_more_than_63",
	"Denorm_shift_more_than_64",
	"Do_unmasked_underflow",
	"Exp_not_underflow",
	"fpu_Arith_exit",
	"fpu_reg_round",
	"fpu_reg_round_signed_special_exit",
	"fpu_reg_round_special_exit",
	"L_accum_done",
	"L_accum_loaded",
	"L_accum_loop",
	"L_arg1_larger",
	"L_bugged",
	"L_bugged_1",
	"L_bugged_2",
	"L_bugged_3",
	"L_bugged_4",
	"L_bugged_denorm_486",
	"L_bugged_round24",
	"L_bugged_round53",
	"L_bugged_round64",
	"LCheck_24_round_up",
	"LCheck_53_round_up",
	"LCheck_Round_Overflow",
	"LCheck_truncate_24",
	"LCheck_truncate_53",
	"LCheck_truncate_64",
	"LDenormal_adj_exponent",
	"L_deNormalised",
	"LDo_24_round_up",
	"LDo_2nd_32_bits",
	"LDo_2nd_div",
	"LDo_3rd_32_bits",
	"LDo_3rd_div",
	"LDo_53_round_up",
	"LDo_64_round_up",
	"L_done",
	"LDo_truncate_24",
	"LDown_24",
	"LDown_53",
	"LDown_64",
	"L_entry_bugged",
	"L_error_exit",
	"L_exactly_32",
	"L_exception_exit",
	"L_exit",
	"L_exit_nuo_valid",
	"L_exit_nuo_zero",
	"L_exit_valid",
	"L_extent_zero",
	"LFirst_div_done",
	"LFirst_div_not_1",
	"L_Full_Division",
	"LGreater_Half_24",
	"LGreater_Half_53",
	"LGreater_than_1",
	"LLess_than_1",
	"L_Make_denorm",
	"L_more_31_no_low",
	"L_more_63_no_low",
	"L_more_than_31",
	"L_more_than_63",
	"L_more_than_64",
	"L_more_than_65",
	"L_more_than_95",
	"L_must_be_zero",
	"L_n_exit",
	"L_no_adjust",
	"L_no_bit_lost",
	"L_no_overflow",
	"L_no_precision_loss",
	"L_Normalised",
	"L_norm_bugged",
	"L_n_shift_1",
	"L_nuo_shift_1",
	"L_overflow",
	"L_precision_lost_down",
	"L_precision_lost_up",
	"LPrevent_2nd_overflow",
	"LPrevent_3rd_overflow",
	"LPseudoDenormal",
	"L_Re_normalise",
	"LResult_Normalised",
	"L_round",
	"LRound_large",
	"LRound_nearest_24",
	"LRound_nearest_53",
	"LRound_nearest_64",
	"LRound_not_small",
	"LRound_ovfl",
	"LRound_precision",
	"LRound_prep",
	"L_round_the_result",
	"LRound_To_24",
	"LRound_To_53",
	"LRound_To_64",
	"LSecond_div_done",
	"LSecond_div_not_1",
	"L_shift_1",
	"L_shift_32",
	"L_shift_65_nc",
	"L_shift_done",
	"Ls_less_than_32",
	"Ls_more_than_63",
	"Ls_more_than_95",
	"L_Store_significand",
	"L_subtr",
	"LTest_over",
	"LTruncate_53",
	"LTruncate_64",
	"L_underflow",
	"L_underflow_to_zero",
	"LUp_24",
	"LUp_53",
	"LUp_64",
	"L_zero",
	"Normalise_result",
	"Signal_underflow",
	"sqrt_arg_ge_2",
	"sqrt_get_more_precision",
	"sqrt_more_prec_large",
	"sqrt_more_prec_ok",
	"sqrt_more_prec_small",
	"sqrt_near_exact",
	"sqrt_near_exact_large",
	"sqrt_near_exact_ok",
	"sqrt_near_exact_small",
	"sqrt_near_exact_x",
	"sqrt_prelim_no_adjust",
	"sqrt_round_result",
	"sqrt_stage_2_done",
	"sqrt_stage_2_error",
	"sqrt_stage_2_finish",
	"sqrt_stage_2_positive",
	"sqrt_stage_3_error",
	"sqrt_stage_3_finished",
	"sqrt_stage_3_no_error",
	"sqrt_stage_3_positive",
	"Unmasked_underflow",
	"xExp_not_underflow",
#endif	/* CONFIG_MATH_EMULATION */
};

static const char *bb_hardware_handlers[] = {
	"ret_from_exception",
	"system_call",
	"work_pending",
	"syscall_fault",
	"page_fault",
	"coprocessor_error",
	"simd_coprocessor_error",
	"device_not_available",
	"debug",
	"nmi",
	"int3",
	"overflow",
	"bounds",
	"invalid_op",
	"coprocessor_segment_overrun",
	"invalid_TSS",
	"segment_not_present",
	"stack_segment",
	"general_protection",
	"alignment_check",
	"kdb_call",
	"divide_error",
	"machine_check",
	"spurious_interrupt_bug",
};

static int
bb_hardware_pushed_arch(kdb_machreg_t rsp,
			const struct kdb_activation_record *ar)
{
	return (2 * KDB_WORD_SIZE);
}

static void
bb_start_block0(void)
{
	bb_reg_code_set_value(BBRG_RAX, BBRG_RAX);
	bb_reg_code_set_value(BBRG_RBX, BBRG_RBX);
	bb_reg_code_set_value(BBRG_RCX, BBRG_RCX);
	bb_reg_code_set_value(BBRG_RDX, BBRG_RDX);
	bb_reg_code_set_value(BBRG_RDI, BBRG_RDI);
	bb_reg_code_set_value(BBRG_RSI, BBRG_RSI);
	bb_reg_code_set_value(BBRG_RBP, BBRG_RBP);
	bb_reg_code_set_value(BBRG_RSP, BBRG_OSP);
}

/* The i386 code that switches stack in a context switch is an extremely
 * special case.  It saves the rip pointing to a label that is not otherwise
 * referenced, saves the current rsp then pushes a word.  The magic code that
 * resumes the new task picks up the saved rip and rsp, effectively referencing
 * a label that otherwise is not used and ignoring the pushed word.
 *
 * The simplest way to handle this very strange case is to recognise jmp
 * address <__switch_to> and treat it as a popfl instruction.  This avoids
 * terminating the block on this jmp and removes one word from the stack state,
 * which is the end effect of all the magic code.
 *
 * Called with the instruction line, starting after the first ':'.
 */

static void
bb_fixup_switch_to(char *p)
{
	char *p1 = p;
	p += strspn(p, " \t");		/* start of instruction */
	if (strncmp(p, "jmp", 3))
		return;
	p += strcspn(p, " \t");		/* end of instruction */
	p += strspn(p, " \t");		/* start of address */
	p += strcspn(p, " \t");		/* end of address */
	p += strspn(p, " \t");		/* start of comment */
	if (strcmp(p, "<__switch_to>") == 0)
		strcpy(p1, "popfl");
}

static int
bb_asmlinkage_arch(void)
{
	return strcmp(bb_func_name, "ret_from_exception") == 0 ||
	       strcmp(bb_func_name, "syscall_trace_entry") == 0;
}

#endif	/* CONFIG_X86_64 */


/*============================================================================*/
/*                                                                            */
/* Common code and data.                                                      */
/*                                                                            */
/*============================================================================*/


/* Tracking registers by decoding the instructions is quite a bit harder than
 * doing the same tracking using compiler generated information.  Register
 * contents can remain in the same register, they can be copied to other
 * registers, they can be stored on stack or they can be modified/overwritten.
 * At any one time, there are 0 or more copies of the original value that was
 * supplied in each register on input to the current function.  If a register
 * exists in multiple places, one copy of that register is the master version,
 * the others are temporary copies which may or may not be destroyed before the
 * end of the function.
 *
 * The compiler knows which copy of a register is the master and which are
 * temporary copies, which makes it relatively easy to track register contents
 * as they are saved and restored.  Without that compiler based knowledge, this
 * code has to track _every_ possible copy of each register, simply because we
 * do not know which is the master copy and which are temporary copies which
 * may be destroyed later.
 *
 * It gets worse: registers that contain parameters can be copied to other
 * registers which are then saved on stack in a lower level function.  Also the
 * stack pointer may be held in multiple registers (typically RSP and RBP)
 * which contain different offsets from the base of the stack on entry to this
 * function.  All of which means that we have to track _all_ register
 * movements, or at least as much as possible.
 *
 * Start with the basic block that contains the start of the function, by
 * definition all registers contain their initial value.  Track each
 * instruction's effect on register contents, this includes reading from a
 * parameter register before any write to that register, IOW the register
 * really does contain a parameter.  The register state is represented by a
 * dynamically sized array with each entry containing :-
 *
 *   Register name
 *   Location it is copied to (another register or stack + offset)
 *
 * Besides the register tracking array, we track which parameter registers are
 * read before being written, to determine how many parameters are passed in
 * registers.  We also track which registers contain stack pointers, including
 * their offset from the original stack pointer on entry to the function.
 *
 * At each exit from the current basic block (via JMP instruction or drop
 * through), the register state is cloned to form the state on input to the
 * target basic block and the target is marked for processing using this state.
 * When there are multiple ways to enter a basic block (e.g. several JMP
 * instructions referencing the same target) then there will be multiple sets
 * of register state to form the "input" for that basic block, there is no
 * guarantee that all paths to that block will have the same register state.
 *
 * As each target block is processed, all the known sets of register state are
 * merged to form a suitable subset of the state which agrees with all the
 * inputs.  The most common case is where one path to this block copies a
 * register to another register but another path does not, therefore the copy
 * is only a temporary and should not be propogated into this block.
 *
 * If the target block already has an input state from the current transfer
 * point and the new input state is identical to the previous input state then
 * we have reached a steady state for the arc from the current location to the
 * target block.  Therefore there is no need to process the target block again.
 *
 * The steps of "process a block, create state for target block(s), pick a new
 * target block, merge state for target block, process target block" will
 * continue until all the state changes have propogated all the way down the
 * basic block tree, including round any cycles in the tree.  The merge step
 * only deletes tracking entries from the input state(s), it never adds a
 * tracking entry.  Therefore the overall algorithm is guaranteed to converge
 * to a steady state, the worst possible case is that every tracking entry into
 * a block is deleted, which will result in an empty output state.
 *
 * As each instruction is decoded, it is checked to see if this is the point at
 * which execution left this function.  This can be a call to another function
 * (actually the return address to this function) or is the instruction which
 * was about to be executed when an interrupt occurred (including an oops).
 * Save the register state at this point.
 *
 * We always know what the registers contain when execution left this function.
 * For an interrupt, the registers are in struct pt_regs.  For a call to
 * another function, we have already deduced the register state on entry to the
 * other function by unwinding to the start of that function.  Given the
 * register state on exit from this function plus the known register contents
 * on entry to the next function, we can determine the stack pointer value on
 * input to this function.  That in turn lets us calculate the address of input
 * registers that have been stored on stack, giving us the input parameters.
 * Finally the stack pointer gives us the return address which is the exit
 * point from the calling function, repeat the unwind process on that function.
 *
 * The data that tracks which registers contain input parameters is function
 * global, not local to any basic block.  To determine which input registers
 * contain parameters, we have to decode the entire function.  Otherwise an
 * exit early in the function might not have read any parameters yet.
 */

/* Record memory contents in terms of the values that were passed to this
 * function, IOW track which memory locations contain an input value.  A memory
 * location's contents can be undefined, it can contain an input register value
 * or it can contain an offset from the original stack pointer.
 *
 * This structure is used to record register contents that have been stored in
 * memory.  Location (BBRG_OSP + 'offset_address') contains the input value
 * from register 'value'.  When 'value' is BBRG_OSP then offset_value contains
 * the offset from the original stack pointer that was stored in this memory
 * location.  When 'value' is not BBRG_OSP then the memory location contains
 * the original contents of an input register and offset_value is ignored.
 *
 * An input register 'value' can be stored in more than one register and/or in
 * more than one memory location.
 */

struct bb_memory_contains
{
	short offset_address;
	enum bb_reg_code value: 8;
	short offset_value;
};

/* Track the register state in each basic block. */

struct bb_reg_state
{
	/* Indexed by register value 'reg - BBRG_RAX' */
	struct bb_reg_contains contains[KDB_INT_REGISTERS];
	int ref_count;
	int mem_count;
	/* dynamic size for memory locations, see mem_count */
	struct bb_memory_contains memory[0];
};

static struct bb_reg_state *bb_reg_state, *bb_exit_state;
static int bb_reg_state_max, bb_reg_params, bb_memory_params;

struct bb_actual
{
	bfd_vma value;
	int valid;
};

/* Contains the actual hex value of a register, plus a valid bit.  Indexed by
 * register value 'reg - BBRG_RAX'
 */
static struct bb_actual bb_actual[KDB_INT_REGISTERS];

static bfd_vma bb_func_start, bb_func_end;
static bfd_vma bb_common_interrupt, bb_error_entry, bb_ret_from_intr,
	       bb_thread_return, bb_sync_regs, bb_save_v86_state,
	       bb__sched_text_start, bb__sched_text_end;

/* Record jmp instructions, both conditional and unconditional.  These form the
 * arcs between the basic blocks.  This is also used to record the state when
 * one block drops through into the next.
 *
 * A bb can have multiple associated bb_jmp entries, one for each jcc
 * instruction plus at most one bb_jmp for the drop through case.  If a bb
 * drops through to the next bb then the drop through bb_jmp entry will be the
 * last entry in the set of bb_jmp's that are associated with the bb.  This is
 * enforced by the fact that jcc entries are added during the disassembly phase
 * of pass 1, the drop through entries are added near the end of pass 1.
 *
 * At address 'from' in this block, we have a jump to address 'to'.  The
 * register state at 'from' is copied to the target block.
 */

struct bb_jmp
{
	bfd_vma from;
	bfd_vma to;
	struct bb_reg_state *state;
	unsigned int drop_through: 1;
};

struct bb
{
	bfd_vma start;
	/* The end address of a basic block is sloppy.  It can be the first
	 * byte of the last instruction in the block or it can be the last byte
	 * of the block.
	 */
	bfd_vma end;
	unsigned int changed: 1;
	unsigned int drop_through: 1;
};

static struct bb **bb_list, *bb_curr;
static int bb_max, bb_count;

static struct bb_jmp *bb_jmp_list;
static int bb_jmp_max, bb_jmp_count;

/* Add a new bb entry to the list.  This does an insert sort. */

static struct bb *
bb_new(bfd_vma order)
{
	int i, j;
	struct bb *bb, *p;
	if (bb_giveup)
		return NULL;
	if (bb_count == bb_max) {
		struct bb **bb_list_new;
		bb_max += 10;
		bb_list_new = debug_kmalloc(bb_max*sizeof(*bb_list_new),
					    GFP_ATOMIC);
		if (!bb_list_new) {
			kdb_printf("\n\n%s: out of debug_kmalloc\n", __FUNCTION__);
			bb_giveup = 1;
			return NULL;
		}
		memcpy(bb_list_new, bb_list, bb_count*sizeof(*bb_list));
		debug_kfree(bb_list);
		bb_list = bb_list_new;
	}
	bb = debug_kmalloc(sizeof(*bb), GFP_ATOMIC);
	if (!bb) {
		kdb_printf("\n\n%s: out of debug_kmalloc\n", __FUNCTION__);
		bb_giveup = 1;
		return NULL;
	}
	memset(bb, 0, sizeof(*bb));
	for (i = 0; i < bb_count; ++i) {
		p = bb_list[i];
		if ((p->start && p->start > order) ||
		    (p->end && p->end > order))
			break;
	}
	for (j = bb_count-1; j >= i; --j)
		bb_list[j+1] = bb_list[j];
	bb_list[i] = bb;
	++bb_count;
	return bb;
}

/* Add a new bb_jmp entry to the list.  This list is not sorted. */

static struct bb_jmp *
bb_jmp_new(bfd_vma from, bfd_vma to, unsigned int drop_through)
{
	struct bb_jmp *bb_jmp;
	if (bb_giveup)
		return NULL;
	if (bb_jmp_count == bb_jmp_max) {
		struct bb_jmp *bb_jmp_list_new;
		bb_jmp_max += 10;
		bb_jmp_list_new =
			debug_kmalloc(bb_jmp_max*sizeof(*bb_jmp_list_new),
				      GFP_ATOMIC);
		if (!bb_jmp_list_new) {
			kdb_printf("\n\n%s: out of debug_kmalloc\n",
				   __FUNCTION__);
			bb_giveup = 1;
			return NULL;
		}
		memcpy(bb_jmp_list_new, bb_jmp_list,
		       bb_jmp_count*sizeof(*bb_jmp_list));
		debug_kfree(bb_jmp_list);
		bb_jmp_list = bb_jmp_list_new;
	}
	bb_jmp = bb_jmp_list + bb_jmp_count++;
	bb_jmp->from = from;
	bb_jmp->to = to;
	bb_jmp->drop_through = drop_through;
	bb_jmp->state = NULL;
	return bb_jmp;
}

static void
bb_delete(int i)
{
	struct bb *bb = bb_list[i];
	memcpy(bb_list+i, bb_list+i+1, (bb_count-i-1)*sizeof(*bb_list));
	bb_list[--bb_count] = NULL;
	debug_kfree(bb);
}

static struct bb *
bb_add(bfd_vma start, bfd_vma end)
{
	int i;
	struct bb *bb;
	/* Ignore basic blocks whose start address is outside the current
	 * function.  These occur for call instructions and for tail recursion.
	 */
	if (start &&
	    (start < bb_func_start || start >= bb_func_end))
		       return NULL;
	for (i = 0; i < bb_count; ++i) {
		bb = bb_list[i];
		if ((start && bb->start == start) ||
		    (end && bb->end == end))
			return bb;
	}
	bb = bb_new(start ? start : end);
	if (bb) {
		bb->start = start;
		bb->end = end;
	}
	return bb;
}

static struct bb_jmp *
bb_jmp_add(bfd_vma from, bfd_vma to, unsigned int drop_through)
{
	int i;
	struct bb_jmp *bb_jmp;
	for (i = 0, bb_jmp = bb_jmp_list; i < bb_jmp_count; ++i, ++bb_jmp) {
		if (bb_jmp->from == from &&
		    bb_jmp->to == to &&
		    bb_jmp->drop_through == drop_through)
			return bb_jmp;
	}
	bb_jmp = bb_jmp_new(from, to, drop_through);
	return bb_jmp;
}

static unsigned long bb_curr_addr, bb_exit_addr;
static char bb_buffer[256];	/* A bit too big to go on stack */

/* Computed jmp uses 'jmp *addr(,%reg,[48])' where 'addr' is the start of a
 * table of addresses that point into the current function.  Run the table and
 * generate bb starts for each target address plus a bb_jmp from this address
 * to the target address.
 *
 * Only called for 'jmp' instructions, with the pointer starting at 'jmp'.
 */

static void
bb_pass1_computed_jmp(char *p)
{
	unsigned long table, scale;
	kdb_machreg_t addr;
	struct bb* bb;
	p += strcspn(p, " \t");		/* end of instruction */
	p += strspn(p, " \t");		/* start of address */
	if (*p++ != '*')
		return;
	table = simple_strtoul(p, &p, 0);
	if (strncmp(p, "(,%", 3) != 0)
		return;
	p += 3;
	p += strcspn(p, ",");		/* end of reg */
	if (*p++ != ',')
		return;
	scale = simple_strtoul(p, &p, 0);
	if (scale != KDB_WORD_SIZE || strcmp(p, ")"))
		return;
	while (!bb_giveup) {
		if (kdb_getword(&addr, table, sizeof(addr)))
			return;
		if (addr < bb_func_start || addr >= bb_func_end)
			return;
		bb = bb_add(addr, 0);
		if (bb)
			bb_jmp_add(bb_curr_addr, addr, 0);
		table += KDB_WORD_SIZE;
	}
}

/* Pass 1, identify the start and end of each basic block */

static int
bb_dis_pass1(PTR file, const char *fmt, ...)
{
	int l = strlen(bb_buffer);
	char *p;
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(bb_buffer + l, sizeof(bb_buffer) - l, fmt, ap);
	va_end(ap);
	if ((p = strchr(bb_buffer, '\n'))) {
		*p = '\0';
		/* ret[q], iret[q], sysexit, sysret, ud2a or jmp[q] end a
		 * block.  As does a call to a function marked noret.
		 */
		p = bb_buffer;
		p += strcspn(p, ":");
		if (*p++ == ':') {
			bb_fixup_switch_to(p);
			p += strspn(p, " \t");	/* start of instruction */
			if (strncmp(p, "ret", 3) == 0 ||
			    strncmp(p, "iret", 4) == 0 ||
			    strncmp(p, "sysexit", 7) == 0 ||
			    strncmp(p, "sysret", 6) == 0 ||
			    strncmp(p, "ud2a", 4) == 0 ||
			    strncmp(p, "jmp", 3) == 0) {
				if (strncmp(p, "jmp", 3) == 0)
					bb_pass1_computed_jmp(p);
				bb_add(0, bb_curr_addr);
			};
			if (strncmp(p, "call", 4) == 0) {
				strsep(&p, " \t");	/* end of opcode */
				if (p)
					p += strspn(p, " \t");	/* operand(s) */
				if (p && strchr(p, '<')) {
					p = strchr(p, '<') + 1;
					*strchr(p, '>') = '\0';
					if (bb_noret(p))
						bb_add(0, bb_curr_addr);
				}
			};
		}
		bb_buffer[0] = '\0';
	}
	return 0;
}

static void
bb_printaddr_pass1(bfd_vma addr, disassemble_info *dip)
{
	kdb_symtab_t symtab;
	unsigned int offset;
	struct bb* bb;
	/* disasm only calls the printaddr routine for the target of jmp, loop
	 * or call instructions, i.e. the start of a basic block.  call is
	 * ignored by bb_add because the target address is outside the current
	 * function.
	 */
	dip->fprintf_func(dip->stream, "0x%lx", addr);
	kdbnearsym(addr, &symtab);
	if (symtab.sym_name) {
		dip->fprintf_func(dip->stream, " <%s", symtab.sym_name);
		if ((offset = addr - symtab.sym_start))
			dip->fprintf_func(dip->stream, "+0x%x", offset);
		dip->fprintf_func(dip->stream, ">");
	}
	bb = bb_add(addr, 0);
	if (bb)
		bb_jmp_add(bb_curr_addr, addr, 0);
}

static void
bb_pass1(void)
{
	int i;
	unsigned long addr;
	struct bb *bb;
	struct bb_jmp *bb_jmp;

	if (KDB_DEBUG(BB) | KDB_DEBUG(BB_SUMM))
		kdb_printf("%s: func_name %s func_start " kdb_bfd_vma_fmt0
			   " func_end " kdb_bfd_vma_fmt0 "\n",
			   __FUNCTION__,
			   bb_func_name,
			   bb_func_start,
			   bb_func_end);
	kdb_di.fprintf_func = bb_dis_pass1;
	kdb_di.print_address_func = bb_printaddr_pass1;

	bb_add(bb_func_start, 0);
	for (bb_curr_addr = bb_func_start;
	     bb_curr_addr < bb_func_end;
	     ++bb_curr_addr) {
		unsigned char c;
		if (kdb_getarea(c, bb_curr_addr)) {
			kdb_printf("%s: unreadable function code at ",
				   __FUNCTION__);
			kdb_symbol_print(bb_curr_addr, NULL, KDB_SP_DEFAULT);
			kdb_printf(", giving up\n");
			bb_giveup = 1;
			return;
		}
	}
	for (addr = bb_func_start; addr < bb_func_end; ) {
		bb_curr_addr = addr;
		addr += kdba_id_printinsn(addr, &kdb_di);
		kdb_di.fprintf_func(NULL, "\n");
	}
	if (bb_giveup)
		goto out;

	/* Special case: a block consisting of a single instruction which is
	 * both the target of a jmp and is also an ending instruction, so we
	 * add two blocks using the same address, one as a start and one as an
	 * end, in no guaranteed order.  The end must be ordered after the
	 * start.
	 */
	for (i = 0; i < bb_count-1; ++i) {
		struct bb *bb1 = bb_list[i], *bb2 = bb_list[i+1];
		if (bb1->end && bb1->end == bb2->start) {
			bb = bb_list[i+1];
			bb_list[i+1] = bb_list[i];
			bb_list[i] = bb;
		}
	}

	/* Some bb have a start address, some have an end address.  Collapse
	 * them into entries that have both start and end addresses.  The first
	 * entry is guaranteed to have a start address.
	 */
	for (i = 0; i < bb_count-1; ++i) {
		struct bb *bb1 = bb_list[i], *bb2 = bb_list[i+1];
		if (bb1->end)
			continue;
		if (bb2->start) {
			bb1->end = bb2->start - 1;
			bb1->drop_through = 1;
			bb_jmp_add(bb1->end, bb2->start, 1);
		} else {
			bb1->end = bb2->end;
			bb_delete(i+1);
		}
	}
	bb = bb_list[bb_count-1];
	if (!bb->end)
		bb->end = bb_func_end - 1;

	/* It would be nice to check that all bb have a valid start and end
	 * address but there is just too much garbage code in the kernel to do
	 * that check.  Aligned functions in assembler code mean that there is
	 * space between the end of one function and the start of the next and
	 * that space contains previous code from the assembler's buffers.  It
	 * looks like dead code with nothing that branches to it, so no start
	 * address.  do_sys_vm86() ends with 'jmp resume_userspace' which the C
	 * compiler does not know about so gcc appends the normal exit code,
	 * again nothing branches to this dangling code.
	 *
	 * The best we can do is delete bb entries with no start address.
	 */
	for (i = 0; i < bb_count; ++i) {
		struct bb *bb = bb_list[i];
		if (!bb->start)
			bb_delete(i--);
	}
	for (i = 0; i < bb_count; ++i) {
		struct bb *bb = bb_list[i];
		if (!bb->end) {
			kdb_printf("%s: incomplete bb state\n", __FUNCTION__);
			bb_giveup = 1;
			goto debug;
		}
	}

out:
	if (!KDB_DEBUG(BB))
		return;
debug:
	kdb_printf("%s: end\n", __FUNCTION__);
	for (i = 0; i < bb_count; ++i) {
		bb = bb_list[i];
		kdb_printf("  bb[%d] start "
			   kdb_bfd_vma_fmt0
			   " end " kdb_bfd_vma_fmt0
			   " drop_through %d",
			   i, bb->start, bb->end, bb->drop_through);
		kdb_printf("\n");
	}
	for (i = 0; i < bb_jmp_count; ++i) {
		bb_jmp = bb_jmp_list + i;
		kdb_printf("  bb_jmp[%d] from "
			   kdb_bfd_vma_fmt0
			   " to " kdb_bfd_vma_fmt0
			   " drop_through %d\n",
			   i, bb_jmp->from, bb_jmp->to, bb_jmp->drop_through);
	}
}

/* Pass 2, record register changes in each basic block */

/* For each opcode that we care about, indicate how it uses its operands.  Most
 * opcodes can be handled generically because they completely specify their
 * operands in the instruction, however many opcodes have side effects such as
 * reading or writing rax or updating rsp.  Instructions that change registers
 * that are not listed in the operands must be handled as special cases.  In
 * addition, instructions that copy registers while preserving their contents
 * (push, pop, mov) or change the contents in a well defined way (add with an
 * immediate, lea) must be handled as special cases in order to track the
 * register contents.
 *
 * The tables below only list opcodes that are actually used in the Linux
 * kernel, so they omit most of the floating point and all of the SSE type
 * instructions.  The operand usage entries only cater for accesses to memory
 * and to the integer registers, accesses to floating point registers and flags
 * are not relevant for kernel backtraces.
 */

enum bb_operand_usage {
	BBOU_UNKNOWN = 0,
		/* generic entries.  because xchg can do any combinations of
		 * read src, write src, read dst and  write dst we need to
		 * define all 16 possibilities.  These are ordered by rs = 1,
		 * rd = 2, ws = 4, wd = 8, bb_usage_x*() functions rely on this
		 * order.
		 */
	BBOU_RS = 1,	/* read src */		/*  1 */
	BBOU_RD,	/* read dst */		/*  2 */
	BBOU_RSRD,				/*  3 */
	BBOU_WS,	/* write src */		/*  4 */
	BBOU_RSWS,				/*  5 */
	BBOU_RDWS,				/*  6 */
	BBOU_RSRDWS,				/*  7 */
	BBOU_WD,	/* write dst */		/*  8 */
	BBOU_RSWD,				/*  9 */
	BBOU_RDWD,				/* 10 */
	BBOU_RSRDWD,				/* 11 */
	BBOU_WSWD,				/* 12 */
	BBOU_RSWSWD,				/* 13 */
	BBOU_RDWSWD,				/* 14 */
	BBOU_RSRDWSWD,				/* 15 */
		/* opcode specific entries */
	BBOU_ADD,
	BBOU_CALL,
	BBOU_CBW,
	BBOU_CMOV,
	BBOU_CMPXCHG,
	BBOU_CMPXCHGD,
	BBOU_CPUID,
	BBOU_CWD,
	BBOU_DIV,
	BBOU_IDIV,
	BBOU_IMUL,
	BBOU_IRET,
	BBOU_JMP,
	BBOU_LAHF,
	BBOU_LEA,
	BBOU_LEAVE,
	BBOU_LODS,
	BBOU_LOOP,
	BBOU_LSS,
	BBOU_MONITOR,
	BBOU_MOV,
	BBOU_MOVS,
	BBOU_MUL,
	BBOU_MWAIT,
	BBOU_NOP,
	BBOU_OUTS,
	BBOU_POP,
	BBOU_POPF,
	BBOU_PUSH,
	BBOU_PUSHF,
	BBOU_RDMSR,
	BBOU_RDTSC,
	BBOU_RET,
	BBOU_SAHF,
	BBOU_SCAS,
	BBOU_SUB,
	BBOU_SYSEXIT,
	BBOU_SYSRET,
	BBOU_WRMSR,
	BBOU_XADD,
	BBOU_XCHG,
	BBOU_XOR,
};

struct bb_opcode_usage {
	int length;
	enum bb_operand_usage usage;
	const char *opcode;
};

/* This table is sorted in alphabetical order of opcode, except that the
 * trailing '"' is treated as a high value.  For example, 'in' sorts after
 * 'inc', 'bt' after 'btc'.  This modified sort order ensures that shorter
 * opcodes come after long ones.  A normal sort would put 'in' first, so 'in'
 * would match both 'inc' and 'in'.  When adding any new entries to this table,
 * be careful to put shorter entries last in their group.
 *
 * To automatically sort the table (in vi)
 *   Mark the first and last opcode line with 'a and 'b
 *   'a
 *   !'bsed -e 's/"}/}}/' | LANG=C sort -t '"' -k2 | sed -e 's/}}/"}/'
 *
 * If a new instruction has to be added, first consider if it affects registers
 * other than those listed in the operands.  Also consider if you want to track
 * the results of issuing the instruction, IOW can you extract useful
 * information by looking in detail at the modified registers or memory.  If
 * either test is true then you need a special case to handle the instruction.
 *
 * The generic entries at the start of enum bb_operand_usage all have one thing
 * in common, if a register or memory location is updated then that location
 * becomes undefined, i.e. we lose track of anything that was previously saved
 * in that location.  So only use a generic BBOU_* value when the result of the
 * instruction cannot be calculated exactly _and_ when all the affected
 * registers are listed in the operands.
 *
 * Examples:
 *
 * 'call' does not generate a known result, but as a side effect of call,
 * several scratch registers become undefined, so it needs a special BBOU_CALL
 * entry.
 *
 * 'adc' generates a variable result, it depends on the carry flag, so 'adc'
 * gets a generic entry.  'add' can generate an exact result (add with
 * immediate on a register that points to the stack) or it can generate an
 * unknown result (add a variable, or add immediate to a register that does not
 * contain a stack pointer) so 'add' has its own BBOU_ADD entry.
 */

static const struct bb_opcode_usage
bb_opcode_usage_all[] = {
	{3, BBOU_RSRDWD,  "adc"},
	{3, BBOU_ADD,     "add"},
	{3, BBOU_RSRDWD,  "and"},
	{3, BBOU_RSWD,    "bsf"},
	{3, BBOU_RSWD,    "bsr"},
	{5, BBOU_RSWS,    "bswap"},
	{3, BBOU_RSRDWD,  "btc"},
	{3, BBOU_RSRDWD,  "btr"},
	{3, BBOU_RSRDWD,  "bts"},
	{2, BBOU_RSRD,    "bt"},
	{4, BBOU_CALL,    "call"},
	{4, BBOU_CBW,     "cbtw"},	/* Intel cbw */
	{3, BBOU_NOP,     "clc"},
	{3, BBOU_NOP,     "cld"},
	{7, BBOU_RS,      "clflush"},
	{4, BBOU_NOP,     "clgi"},
	{3, BBOU_NOP,     "cli"},
	{4, BBOU_CWD,     "cltd"},	/* Intel cdq */
	{4, BBOU_CBW,     "cltq"},	/* Intel cdqe */
	{4, BBOU_NOP,     "clts"},
	{4, BBOU_CMOV,    "cmov"},
	{9, BBOU_CMPXCHGD,"cmpxchg16"},
	{8, BBOU_CMPXCHGD,"cmpxchg8"},
	{7, BBOU_CMPXCHG, "cmpxchg"},
	{3, BBOU_RSRD,    "cmp"},
	{5, BBOU_CPUID,   "cpuid"},
	{4, BBOU_CWD,     "cqto"},	/* Intel cdo */
	{4, BBOU_CWD,     "cwtd"},	/* Intel cwd */
	{4, BBOU_CBW,     "cwtl"},	/* Intel cwde */
	{4, BBOU_NOP,     "data"},	/* alternative ASM_NOP<n> generates data16 on x86_64 */
	{3, BBOU_RSWS,    "dec"},
	{3, BBOU_DIV,     "div"},
	{5, BBOU_RS,      "fdivl"},
	{5, BBOU_NOP,     "finit"},
	{6, BBOU_RS,      "fistpl"},
	{4, BBOU_RS,      "fldl"},
	{4, BBOU_RS,      "fmul"},
	{6, BBOU_NOP,     "fnclex"},
	{6, BBOU_NOP,     "fninit"},
	{6, BBOU_RS,      "fnsave"},
	{7, BBOU_NOP,     "fnsetpm"},
	{6, BBOU_RS,      "frstor"},
	{5, BBOU_WS,      "fstsw"},
	{5, BBOU_RS,      "fsubp"},
	{5, BBOU_NOP,     "fwait"},
	{7, BBOU_RS,      "fxrstor"},
	{6, BBOU_RS,      "fxsave"},
	{3, BBOU_NOP,     "hlt"},
	{4, BBOU_IDIV,    "idiv"},
	{4, BBOU_IMUL,    "imul"},
	{3, BBOU_RSWS,    "inc"},
	{3, BBOU_NOP,     "int"},
	{7, BBOU_RSRD,    "invlpga"},
	{6, BBOU_RS,      "invlpg"},
	{2, BBOU_RSWD,    "in"},
	{4, BBOU_IRET,    "iret"},
	{1, BBOU_JMP,     "j"},
	{4, BBOU_LAHF,    "lahf"},
	{3, BBOU_RSWD,    "lar"},
	{5, BBOU_RS,      "lcall"},
	{5, BBOU_LEAVE,   "leave"},
	{3, BBOU_LEA,     "lea"},
	{6, BBOU_NOP,     "lfence"},
	{4, BBOU_RS,      "lgdt"},
	{4, BBOU_RS,      "lidt"},
	{4, BBOU_RS,      "ljmp"},
	{4, BBOU_RS,      "lldt"},
	{4, BBOU_RS,      "lmsw"},
	{4, BBOU_LODS,    "lods"},
	{4, BBOU_LOOP,    "loop"},
	{4, BBOU_NOP,     "lret"},
	{3, BBOU_RSWD,    "lsl"},
	{3, BBOU_LSS,     "lss"},
	{3, BBOU_RS,      "ltr"},
	{6, BBOU_NOP,     "mfence"},
	{7, BBOU_MONITOR, "monitor"},
	{4, BBOU_MOVS,    "movs"},
	{3, BBOU_MOV,     "mov"},
	{3, BBOU_MUL,     "mul"},
	{5, BBOU_MWAIT,   "mwait"},
	{3, BBOU_RSWS,    "neg"},
	{3, BBOU_NOP,     "nop"},
	{3, BBOU_RSWS,    "not"},
	{2, BBOU_RSRDWD,  "or"},
	{4, BBOU_OUTS,    "outs"},
	{3, BBOU_RSRD,    "out"},
	{5, BBOU_NOP,     "pause"},
	{4, BBOU_POPF,    "popf"},
	{3, BBOU_POP,     "pop"},
	{8, BBOU_RS,      "prefetch"},
	{5, BBOU_PUSHF,   "pushf"},
	{4, BBOU_PUSH,    "push"},
	{3, BBOU_RSRDWD,  "rcl"},
	{3, BBOU_RSRDWD,  "rcr"},
	{5, BBOU_RDMSR,   "rdmsr"},
	{5, BBOU_RDMSR,   "rdpmc"},	/* same side effects as rdmsr */
	{5, BBOU_RDTSC,   "rdtsc"},
	{3, BBOU_RET,     "ret"},
	{3, BBOU_RSRDWD,  "rol"},
	{3, BBOU_RSRDWD,  "ror"},
	{4, BBOU_SAHF,    "sahf"},
	{3, BBOU_RSRDWD,  "sar"},
	{3, BBOU_RSRDWD,  "sbb"},
	{4, BBOU_SCAS,    "scas"},
	{3, BBOU_WS,      "set"},
	{6, BBOU_NOP,     "sfence"},
	{4, BBOU_WS,      "sgdt"},
	{3, BBOU_RSRDWD,  "shl"},
	{3, BBOU_RSRDWD,  "shr"},
	{4, BBOU_WS,      "sidt"},
	{4, BBOU_WS,      "sldt"},
	{3, BBOU_NOP,     "stc"},
	{3, BBOU_NOP,     "std"},
	{4, BBOU_NOP,     "stgi"},
	{3, BBOU_NOP,     "sti"},
	{4, BBOU_SCAS,    "stos"},
	{4, BBOU_WS,      "strl"},
	{3, BBOU_WS,      "str"},
	{3, BBOU_SUB,     "sub"},
	{6, BBOU_NOP,     "swapgs"},
	{7, BBOU_SYSEXIT, "sysexit"},
	{6, BBOU_SYSRET,  "sysret"},
	{4, BBOU_NOP,     "test"},
	{4, BBOU_NOP,     "ud2a"},
	{7, BBOU_RS,      "vmclear"},
	{8, BBOU_NOP,     "vmlaunch"},
	{6, BBOU_RS,      "vmload"},
	{7, BBOU_RS,      "vmptrld"},
	{6, BBOU_WD,      "vmread"},	/* vmread src is an encoding, not a register */
	{8, BBOU_NOP,     "vmresume"},
	{5, BBOU_RS,      "vmrun"},
	{6, BBOU_RS,      "vmsave"},
	{7, BBOU_WD,      "vmwrite"},	/* vmwrite src is an encoding, not a register */
	{6, BBOU_NOP,     "wbinvd"},
	{5, BBOU_WRMSR,   "wrmsr"},
	{4, BBOU_XADD,    "xadd"},
	{4, BBOU_XCHG,    "xchg"},
	{3, BBOU_XOR,     "xor"},
       {10, BBOU_WS,      "xstore-rng"},
};

/* To speed up searching, index bb_opcode_usage_all by the first letter of each
 * opcode.
 */
static struct {
	const struct bb_opcode_usage *opcode;
	int size;
} bb_opcode_usage[26];

struct bb_operand {
	char *base;
	char *index;
	char *segment;
	long disp;
	unsigned int scale;
	enum bb_reg_code base_rc;		/* UNDEFINED or RAX through R15 */
	enum bb_reg_code index_rc;		/* UNDEFINED or RAX through R15 */
	unsigned int present		:1;
	unsigned int disp_present	:1;
	unsigned int indirect		:1;	/* must be combined with reg or memory */
	unsigned int immediate		:1;	/* exactly one of these 3 must be set */
	unsigned int reg		:1;
	unsigned int memory		:1;
};

struct bb_decode {
	char *prefix;
	char *opcode;
	const struct bb_opcode_usage *match;
	struct bb_operand src;
	struct bb_operand dst;
	struct bb_operand dst2;
};

static struct bb_decode bb_decode;

static enum bb_reg_code
bb_reg_map(const char *reg)
{
	int lo, hi, c;
	const struct bb_reg_code_map *p;
	lo = 0;
	hi = ARRAY_SIZE(bb_reg_code_map) - 1;
	while (lo <= hi) {
		int mid = (hi + lo) / 2;
		p = bb_reg_code_map + mid;
		c = strcmp(p->name, reg+1);
		if (c == 0)
			return p->reg;
		else if (c > 0)
			hi = mid - 1;
		else
			lo = mid + 1;
	}
	return BBRG_UNDEFINED;
}

static void
bb_parse_operand(char *str, struct bb_operand *operand)
{
	char *p = str;
	int sign = 1;
	operand->present = 1;
	/* extract any segment prefix */
	if (p[0] == '%' && p[1] && p[2] == 's' && p[3] == ':') {
		operand->memory = 1;
		operand->segment = p;
		p[3] = '\0';
		p += 4;
	}
	/* extract displacement, base, index, scale */
	if (*p == '*') {
		/* jmp/call *disp(%reg), *%reg or *0xnnn */
		operand->indirect = 1;
		++p;
	}
	if (*p == '-') {
		sign = -1;
		++p;
	}
	if (*p == '$') {
		operand->immediate = 1;
		operand->disp_present = 1;
		operand->disp = simple_strtoul(p+1, &p, 0);
	} else if (isdigit(*p)) {
		operand->memory = 1;
		operand->disp_present = 1;
		operand->disp = simple_strtoul(p, &p, 0) * sign;
	}
	if (*p == '%') {
		operand->reg = 1;
		operand->base = p;
	} else if (*p == '(') {
		operand->memory = 1;
		operand->base = ++p;
		p += strcspn(p, ",)");
		if (p == operand->base)
			operand->base = NULL;
		if (*p == ',') {
			*p = '\0';
			operand->index = ++p;
			p += strcspn(p, ",)");
			if (p == operand->index)
				operand->index = NULL;
		}
		if (*p == ',') {
			*p = '\0';
			operand->scale = simple_strtoul(p+1, &p, 0);
		}
		*p = '\0';
	} else if (*p) {
		kdb_printf("%s: unexpected token '%c' after disp '%s'\n",
			   __FUNCTION__, *p, str);
		bb_giveup = 1;
	}
	if ((operand->immediate + operand->reg + operand->memory != 1) ||
	    (operand->indirect && operand->immediate)) {
		kdb_printf("%s: incorrect decode '%s' N %d I %d R %d M %d\n",
			   __FUNCTION__, str,
			   operand->indirect, operand->immediate, operand->reg,
			   operand->memory);
		bb_giveup = 1;
	}
	if (operand->base)
		operand->base_rc = bb_reg_map(operand->base);
	if (operand->index)
		operand->index_rc = bb_reg_map(operand->index);
}

static void
bb_print_operand(const char *type, const struct bb_operand *operand)
{
	if (!operand->present)
		return;
	kdb_printf("  %s %c%c: ",
		   type,
		   operand->indirect ? 'N' : ' ',
		   operand->immediate ? 'I' :
		     operand->reg ? 'R' :
		     operand->memory ? 'M' :
		     '?'
		   );
	if (operand->segment)
		kdb_printf("%s:", operand->segment);
	if (operand->immediate) {
		kdb_printf("$0x%lx", operand->disp);
	} else if (operand->reg) {
		if (operand->indirect)
			kdb_printf("*");
		kdb_printf("%s", operand->base);
	} else if (operand->memory) {
		if (operand->indirect && (operand->base || operand->index))
			kdb_printf("*");
		if (operand->disp_present) {
			kdb_printf("0x%lx", operand->disp);
		}
		if (operand->base || operand->index || operand->scale) {
			kdb_printf("(");
			if (operand->base)
				kdb_printf("%s", operand->base);
			if (operand->index || operand->scale)
				kdb_printf(",");
			if (operand->index)
				kdb_printf("%s", operand->index);
			if (operand->scale)
				kdb_printf(",%d", operand->scale);
			kdb_printf(")");
		}
	}
	if (operand->base_rc)
		kdb_printf(" base_rc %d (%s)",
			   operand->base_rc, bbrg_name[operand->base_rc]);
	if (operand->index_rc)
		kdb_printf(" index_rc %d (%s)",
			   operand->index_rc,
			   bbrg_name[operand->index_rc]);
	kdb_printf("\n");
}

static void
bb_print_opcode(void)
{
	const struct bb_opcode_usage *o = bb_decode.match;
	kdb_printf("  ");
	if (bb_decode.prefix)
		kdb_printf("%s ", bb_decode.prefix);
	kdb_printf("opcode '%s' matched by '%s', usage %d\n",
		   bb_decode.opcode, o->opcode, o->usage);
}

static int
bb_parse_opcode(void)
{
	int c, i;
	const struct bb_opcode_usage *o;
	static int bb_parse_opcode_error_limit = 5;
	c = bb_decode.opcode[0] - 'a';
	if (c < 0 || c >= ARRAY_SIZE(bb_opcode_usage))
		goto nomatch;
	o = bb_opcode_usage[c].opcode;
	if (!o)
		goto nomatch;
	for (i = 0; i < bb_opcode_usage[c].size; ++i, ++o) {
		if (strncmp(bb_decode.opcode, o->opcode, o->length) == 0) {
			bb_decode.match = o;
			if (KDB_DEBUG(BB))
				bb_print_opcode();
			return 0;
		}
	}
nomatch:
	if (!bb_parse_opcode_error_limit)
		return 1;
	--bb_parse_opcode_error_limit;
	kdb_printf("%s: no match at [%s]%s " kdb_bfd_vma_fmt0 " - '%s'\n",
		   __FUNCTION__,
		   bb_mod_name, bb_func_name, bb_curr_addr,
		   bb_decode.opcode);
	return 1;
}

static bool
bb_is_int_reg(enum bb_reg_code reg)
{
	return reg >= BBRG_RAX && reg < (BBRG_RAX + KDB_INT_REGISTERS);
}

static bool
bb_is_simple_memory(const struct bb_operand *operand)
{
	return operand->memory &&
	       bb_is_int_reg(operand->base_rc) &&
	       !operand->index_rc &&
	       operand->scale == 0 &&
	       !operand->segment;
}

static bool
bb_is_static_disp(const struct bb_operand *operand)
{
	return operand->memory &&
	       !operand->base_rc &&
	       !operand->index_rc &&
	       operand->scale == 0 &&
	       !operand->segment &&
	       !operand->indirect;
}

static enum bb_reg_code
bb_reg_code_value(enum bb_reg_code reg)
{
	BB_CHECK(!bb_is_int_reg(reg), reg, 0);
	return bb_reg_state->contains[reg - BBRG_RAX].value;
}

static short
bb_reg_code_offset(enum bb_reg_code reg)
{
	BB_CHECK(!bb_is_int_reg(reg), reg, 0);
	return bb_reg_state->contains[reg - BBRG_RAX].offset;
}

static void
bb_reg_code_set_value(enum bb_reg_code dst, enum bb_reg_code src)
{
	BB_CHECK(!bb_is_int_reg(dst), dst, );
	bb_reg_state->contains[dst - BBRG_RAX].value = src;
}

static void
bb_reg_code_set_offset(enum bb_reg_code dst, short offset)
{
	BB_CHECK(!bb_is_int_reg(dst), dst, );
	bb_reg_state->contains[dst - BBRG_RAX].offset = offset;
}

static bool
bb_is_osp_defined(enum bb_reg_code reg)
{
	if (bb_is_int_reg(reg))
		return bb_reg_code_value(reg) == BBRG_OSP;
	else
		return 0;
}

static bfd_vma
bb_actual_value(enum bb_reg_code reg)
{
	BB_CHECK(!bb_is_int_reg(reg), reg, 0);
	return bb_actual[reg - BBRG_RAX].value;
}

static int
bb_actual_valid(enum bb_reg_code reg)
{
	BB_CHECK(!bb_is_int_reg(reg), reg, 0);
	return bb_actual[reg - BBRG_RAX].valid;
}

static void
bb_actual_set_value(enum bb_reg_code reg, bfd_vma value)
{
	BB_CHECK(!bb_is_int_reg(reg), reg, );
	bb_actual[reg - BBRG_RAX].value = value;
}

static void
bb_actual_set_valid(enum bb_reg_code reg, int valid)
{
	BB_CHECK(!bb_is_int_reg(reg), reg, );
	bb_actual[reg - BBRG_RAX].valid = valid;
}

/* The scheduler code switches RSP then does PUSH, it is not an error for RSP
 * to be undefined in this area of the code.
 */
static bool
bb_is_scheduler_address(void)
{
	return bb_curr_addr >= bb__sched_text_start &&
	       bb_curr_addr < bb__sched_text_end;
}

static void
bb_reg_read(enum bb_reg_code reg)
{
	int i, r = 0;
	if (!bb_is_int_reg(reg) ||
	    bb_reg_code_value(reg) != reg)
		return;
	for (i = 0;
	     i < min_t(unsigned int, REGPARM, ARRAY_SIZE(bb_param_reg));
	     ++i) {
		if (reg == bb_param_reg[i]) {
			r = i + 1;
			break;
		}
	}
	bb_reg_params = max(bb_reg_params, r);
}

static void
bb_do_reg_state_print(const struct bb_reg_state *s)
{
	int i, offset_address, offset_value;
	const struct bb_memory_contains *c;
	enum bb_reg_code value;
	kdb_printf("  bb_reg_state %p\n", s);
	for (i = 0; i < ARRAY_SIZE(s->contains); ++i) {
		value = s->contains[i].value;
		offset_value = s->contains[i].offset;
		kdb_printf("    %s = %s",
			   bbrg_name[i + BBRG_RAX], bbrg_name[value]);
		if (value == BBRG_OSP)
			KDB_DEBUG_BB_OFFSET_PRINTF(offset_value, "", "");
		kdb_printf("\n");
	}
	for (i = 0, c = s->memory; i < s->mem_count; ++i, ++c) {
		offset_address = c->offset_address;
		value = c->value;
		offset_value = c->offset_value;
		kdb_printf("    slot %d offset_address %c0x%x %s",
			   i,
			   offset_address >= 0 ? '+' : '-',
			   offset_address >= 0 ? offset_address : -offset_address,
			   bbrg_name[value]);
		if (value == BBRG_OSP)
			KDB_DEBUG_BB_OFFSET_PRINTF(offset_value, "", "");
		kdb_printf("\n");
	}
}

static void
bb_reg_state_print(const struct bb_reg_state *s)
{
	if (KDB_DEBUG(BB))
		bb_do_reg_state_print(s);
}

/* Set register 'dst' to contain the value from 'src'.  This includes reading
 * from 'src' and writing to 'dst'.  The offset value is copied iff 'src'
 * contains a stack pointer.
 *
 * Be very careful about the context here.  'dst' and 'src' reflect integer
 * registers by name, _not_ by the value of their contents.  "mov %rax,%rsi"
 * will call this function as bb_reg_set_reg(BBRG_RSI, BBRG_RAX), which
 * reflects what the assembler code is doing.  However we need to track the
 * _values_ in the registers, not their names.  IOW, we really care about "what
 * value does rax contain when it is copied into rsi?", so we can record the
 * fact that we now have two copies of that value, one in rax and one in rsi.
 */

static void
bb_reg_set_reg(enum bb_reg_code dst, enum bb_reg_code src)
{
	enum bb_reg_code src_value = BBRG_UNDEFINED;
	short offset_value = 0;
	KDB_DEBUG_BB("  %s = %s", bbrg_name[dst], bbrg_name[src]);
	if (bb_is_int_reg(src)) {
		bb_reg_read(src);
		src_value = bb_reg_code_value(src);
		KDB_DEBUG_BB(" (%s", bbrg_name[src_value]);
		if (bb_is_osp_defined(src)) {
			offset_value = bb_reg_code_offset(src);
			KDB_DEBUG_BB_OFFSET(offset_value, "", "");
		}
		KDB_DEBUG_BB(")");
	}
	if (bb_is_int_reg(dst)) {
		bb_reg_code_set_value(dst, src_value);
		bb_reg_code_set_offset(dst, offset_value);
	}
	KDB_DEBUG_BB("\n");
}

static void
bb_reg_set_undef(enum bb_reg_code dst)
{
	bb_reg_set_reg(dst, BBRG_UNDEFINED);
}

/* Delete any record of a stored register held in osp + 'offset' */

static void
bb_delete_memory(short offset)
{
	int i;
	struct bb_memory_contains *c;
	for (i = 0, c = bb_reg_state->memory;
	     i < bb_reg_state->mem_count;
	     ++i, ++c) {
		if (c->offset_address == offset &&
		    c->value != BBRG_UNDEFINED) {
			KDB_DEBUG_BB("  delete %s from ",
				     bbrg_name[c->value]);
			KDB_DEBUG_BB_OFFSET(offset, "osp", "");
			KDB_DEBUG_BB(" slot %d\n",
				     (int)(c - bb_reg_state->memory));
			memset(c, BBRG_UNDEFINED, sizeof(*c));
			if (i == bb_reg_state->mem_count - 1)
				--bb_reg_state->mem_count;
		}
	}
}

/* Set memory location *('dst' + 'offset_address') to contain the supplied
 * value and offset.  'dst' is assumed to be a register that contains a stack
 * pointer.
 */

static void
bb_memory_set_reg_value(enum bb_reg_code dst, short offset_address,
			enum bb_reg_code value, short offset_value)
{
	int i;
	struct bb_memory_contains *c, *free = NULL;
	BB_CHECK(!bb_is_osp_defined(dst), dst, );
	KDB_DEBUG_BB("  *(%s", bbrg_name[dst]);
	KDB_DEBUG_BB_OFFSET(offset_address, "", "");
	offset_address += bb_reg_code_offset(dst);
	KDB_DEBUG_BB_OFFSET(offset_address, " osp", ") = ");
	KDB_DEBUG_BB("%s", bbrg_name[value]);
	if (value == BBRG_OSP)
		KDB_DEBUG_BB_OFFSET(offset_value, "", "");
	for (i = 0, c = bb_reg_state->memory;
	     i < bb_reg_state_max;
	     ++i, ++c) {
		if (c->offset_address == offset_address)
			free = c;
		else if (c->value == BBRG_UNDEFINED && !free)
			free = c;
	}
	if (!free) {
		struct bb_reg_state *new, *old = bb_reg_state;
		size_t old_size, new_size;
		int slot;
		old_size = sizeof(*old) + bb_reg_state_max *
				  sizeof(old->memory[0]);
		slot = bb_reg_state_max;
		bb_reg_state_max += 5;
		new_size = sizeof(*new) + bb_reg_state_max *
				  sizeof(new->memory[0]);
		new = debug_kmalloc(new_size, GFP_ATOMIC);
		if (!new) {
			kdb_printf("\n\n%s: out of debug_kmalloc\n", __FUNCTION__);
			bb_giveup = 1;
		} else {
			memcpy(new, old, old_size);
			memset((char *)new + old_size, BBRG_UNDEFINED,
			       new_size - old_size);
			bb_reg_state = new;
			debug_kfree(old);
			free = bb_reg_state->memory + slot;
		}
	}
	if (free) {
		int slot = free - bb_reg_state->memory;
		free->offset_address = offset_address;
		free->value = value;
		free->offset_value = offset_value;
		KDB_DEBUG_BB(" slot %d", slot);
		bb_reg_state->mem_count = max(bb_reg_state->mem_count, slot+1);
	}
	KDB_DEBUG_BB("\n");
}

/* Set memory location *('dst' + 'offset') to contain the value from register
 * 'src'.  'dst' is assumed to be a register that contains a stack pointer.
 * This differs from bb_memory_set_reg_value because it takes a src register
 * which contains a value and possibly an offset, bb_memory_set_reg_value is
 * passed the value and offset directly.
 */

static void
bb_memory_set_reg(enum bb_reg_code dst, enum bb_reg_code src,
		  short offset_address)
{
	int offset_value;
	enum bb_reg_code value;
	BB_CHECK(!bb_is_osp_defined(dst), dst, );
	if (!bb_is_int_reg(src))
		return;
	value = bb_reg_code_value(src);
	if (value == BBRG_UNDEFINED) {
		bb_delete_memory(offset_address + bb_reg_code_offset(dst));
		return;
	}
	offset_value = bb_reg_code_offset(src);
	bb_reg_read(src);
	bb_memory_set_reg_value(dst, offset_address, value, offset_value);
}

/* Set register 'dst' to contain the value from memory *('src' + offset_address).
 * 'src' is assumed to be a register that contains a stack pointer.
 */

static void
bb_reg_set_memory(enum bb_reg_code dst, enum bb_reg_code src, short offset_address)
{
	int i, defined = 0;
	struct bb_memory_contains *s;
	BB_CHECK(!bb_is_osp_defined(src), src, );
	KDB_DEBUG_BB("  %s = *(%s",
		     bbrg_name[dst], bbrg_name[src]);
	KDB_DEBUG_BB_OFFSET(offset_address, "", ")");
	offset_address += bb_reg_code_offset(src);
	KDB_DEBUG_BB_OFFSET(offset_address, " (osp", ")");
	for (i = 0, s = bb_reg_state->memory;
	     i < bb_reg_state->mem_count;
	     ++i, ++s) {
		if (s->offset_address == offset_address && bb_is_int_reg(dst)) {
			bb_reg_code_set_value(dst, s->value);
			KDB_DEBUG_BB(" value %s", bbrg_name[s->value]);
			if (s->value == BBRG_OSP) {
				bb_reg_code_set_offset(dst, s->offset_value);
				KDB_DEBUG_BB_OFFSET(s->offset_value, "", "");
			} else {
				bb_reg_code_set_offset(dst, 0);
			}
			defined = 1;
		}
	}
	if (!defined)
		bb_reg_set_reg(dst, BBRG_UNDEFINED);
	else
		KDB_DEBUG_BB("\n");
}

/* A generic read from an operand. */

static void
bb_read_operand(const struct bb_operand *operand)
{
	int m = 0;
	if (operand->base_rc)
		bb_reg_read(operand->base_rc);
	if (operand->index_rc)
		bb_reg_read(operand->index_rc);
	if (bb_is_simple_memory(operand) &&
	    bb_is_osp_defined(operand->base_rc) &&
	    bb_decode.match->usage != BBOU_LEA) {
		m = (bb_reg_code_offset(operand->base_rc) + operand->disp +
		     KDB_WORD_SIZE - 1) / KDB_WORD_SIZE;
		bb_memory_params = max(bb_memory_params, m);
	}
}

/* A generic write to an operand, resulting in an undefined value in that
 * location.  All well defined operands are handled separately, this function
 * only handles the opcodes where the result is undefined.
 */

static void
bb_write_operand(const struct bb_operand *operand)
{
	enum bb_reg_code base_rc = operand->base_rc;
	if (operand->memory) {
		if (base_rc)
			bb_reg_read(base_rc);
		if (operand->index_rc)
			bb_reg_read(operand->index_rc);
	} else if (operand->reg && base_rc) {
		bb_reg_set_undef(base_rc);
	}
	if (bb_is_simple_memory(operand) && bb_is_osp_defined(base_rc)) {
		int offset;
		offset = bb_reg_code_offset(base_rc) + operand->disp;
		offset = ALIGN(offset - KDB_WORD_SIZE + 1, KDB_WORD_SIZE);
		bb_delete_memory(offset);
	}
}

/* Adjust a register that contains a stack pointer */

static void
bb_adjust_osp(enum bb_reg_code reg, int adjust)
{
	int offset = bb_reg_code_offset(reg), old_offset = offset;
	KDB_DEBUG_BB("  %s osp offset ", bbrg_name[reg]);
	KDB_DEBUG_BB_OFFSET(bb_reg_code_offset(reg), "", " -> ");
	offset += adjust;
	bb_reg_code_set_offset(reg, offset);
	KDB_DEBUG_BB_OFFSET(bb_reg_code_offset(reg), "", "\n");
	/* When RSP is adjusted upwards, it invalidates any memory
	 * stored between the old and current stack offsets.
	 */
	if (reg == BBRG_RSP) {
		while (old_offset < bb_reg_code_offset(reg)) {
			bb_delete_memory(old_offset);
			old_offset += KDB_WORD_SIZE;
		}
	}
}

/* The current instruction adjusts a register that contains a stack pointer.
 * Direction is 1 or -1, depending on whether the instruction is add/lea or
 * sub.
 */

static void
bb_adjust_osp_instruction(int direction)
{
	enum bb_reg_code dst_reg = bb_decode.dst.base_rc;
	if (bb_decode.src.immediate ||
	    bb_decode.match->usage == BBOU_LEA /* lea has its own checks */) {
		int adjust = direction * bb_decode.src.disp;
		bb_adjust_osp(dst_reg, adjust);
	} else {
		/* variable stack adjustment, osp offset is not well defined */
		KDB_DEBUG_BB("  %s osp offset ", bbrg_name[dst_reg]);
		KDB_DEBUG_BB_OFFSET(bb_reg_code_offset(dst_reg), "", " -> undefined\n");
		bb_reg_code_set_value(dst_reg, BBRG_UNDEFINED);
		bb_reg_code_set_offset(dst_reg, 0);
	}
}

/* Some instructions using memory have an explicit length suffix (b, w, l, q).
 * The equivalent instructions using a register imply the length from the
 * register name.  Deduce the operand length.
 */

static int
bb_operand_length(const struct bb_operand *operand, char opcode_suffix)
{
	int l = 0;
	switch (opcode_suffix) {
	case 'b':
		l = 8;
		break;
	case 'w':
		l = 16;
		break;
	case 'l':
		l = 32;
		break;
	case 'q':
		l = 64;
		break;
	}
	if (l == 0 && operand->reg) {
		switch (strlen(operand->base)) {
		case 3:
			switch (operand->base[2]) {
			case 'h':
			case 'l':
				l = 8;
				break;
			default:
				l = 16;
				break;
			}
		case 4:
			if (operand->base[1] == 'r')
				l = 64;
			else
				l = 32;
			break;
		}
	}
	return l;
}

static int
bb_reg_state_size(const struct bb_reg_state *state)
{
	return sizeof(*state) +
	       state->mem_count * sizeof(state->memory[0]);
}

/* Canonicalize the current bb_reg_state so it can be compared against
 * previously created states.  Sort the memory entries in descending order of
 * offset_address (stack grows down).  Empty slots are moved to the end of the
 * list and trimmed.
 */

static void
bb_reg_state_canonicalize(void)
{
	int i, order, changed;
	struct bb_memory_contains *p1, *p2, temp;
	do {
		changed = 0;
		for (i = 0, p1 = bb_reg_state->memory;
		     i < bb_reg_state->mem_count-1;
		     ++i, ++p1) {
			p2 = p1 + 1;
			if (p2->value == BBRG_UNDEFINED) {
				order = 0;
			} else if (p1->value == BBRG_UNDEFINED) {
				order = 1;
			} else if (p1->offset_address < p2->offset_address) {
				order = 1;
			} else if (p1->offset_address > p2->offset_address) {
				order = -1;
			} else {
				order = 0;
			}
			if (order > 0) {
				temp = *p2;
				*p2 = *p1;
				*p1 = temp;
				changed = 1;
			}
		}
	} while(changed);
	for (i = 0, p1 = bb_reg_state->memory;
	     i < bb_reg_state_max;
	     ++i, ++p1) {
		if (p1->value != BBRG_UNDEFINED)
			bb_reg_state->mem_count = i + 1;
	}
	bb_reg_state_print(bb_reg_state);
}

static int
bb_special_case(bfd_vma to)
{
	int i, j, rsp_offset, expect_offset, offset, errors = 0, max_errors = 40;
	enum bb_reg_code reg, expect_value, value;
	struct bb_name_state *r;

	for (i = 0, r = bb_special_cases;
	     i < ARRAY_SIZE(bb_special_cases);
	     ++i, ++r) {
		if (to == r->address &&
		    (r->fname == NULL || strcmp(bb_func_name, r->fname) == 0))
			goto match;
	}
	/* Some inline assembler code has jumps to .fixup sections which result
	 * in out of line transfers with undefined state, ignore them.
	 */
	if (strcmp(bb_func_name, "strnlen_user") == 0 ||
	    strcmp(bb_func_name, "copy_from_user") == 0)
		return 1;
	return 0;

match:
	/* Check the running registers match */
	for (reg = BBRG_RAX; reg < r->regs_size; ++reg) {
		expect_value = r->regs[reg].value;
		if (test_bit(expect_value, r->skip_regs.bits)) {
			/* this regs entry is not defined for this label */
			continue;
		}
		if (expect_value == BBRG_UNDEFINED)
			continue;
		expect_offset = r->regs[reg].offset;
		value = bb_reg_code_value(reg);
		offset = bb_reg_code_offset(reg);
		if (expect_value == value &&
		    (value != BBRG_OSP || r->osp_offset == offset))
			continue;
		kdb_printf("%s: Expected %s to contain %s",
			   __FUNCTION__,
			   bbrg_name[reg],
			   bbrg_name[expect_value]);
		if (r->osp_offset)
			KDB_DEBUG_BB_OFFSET_PRINTF(r->osp_offset, "", "");
		kdb_printf(".  It actually contains %s", bbrg_name[value]);
		if (offset)
			KDB_DEBUG_BB_OFFSET_PRINTF(offset, "", "");
		kdb_printf("\n");
		++errors;
		if (max_errors-- == 0)
			goto fail;
	}
	/* Check that any memory data on stack matches */
	i = j = 0;
	while (i < bb_reg_state->mem_count &&
	       j < r->mem_size) {
		expect_value = r->mem[j].value;
		if (test_bit(expect_value, r->skip_mem.bits) ||
		    expect_value == BBRG_UNDEFINED) {
			/* this memory slot is not defined for this label */
			++j;
			continue;
		}
		rsp_offset = bb_reg_state->memory[i].offset_address -
			bb_reg_code_offset(BBRG_RSP);
		if (rsp_offset >
		    r->mem[j].offset_address) {
			/* extra slots in memory are OK */
			++i;
		} else if (rsp_offset <
			   r->mem[j].offset_address) {
			/* Required memory slot is missing */
			kdb_printf("%s: Invalid bb_reg_state.memory, "
			           "missing memory entry[%d] %s\n",
			   __FUNCTION__, j, bbrg_name[expect_value]);
			++errors;
			if (max_errors-- == 0)
				goto fail;
			++j;
		} else {
			if (bb_reg_state->memory[i].offset_value ||
			    bb_reg_state->memory[i].value != expect_value) {
				/* memory slot is present but contains wrong
				 * value.
				 */
				kdb_printf("%s: Invalid bb_reg_state.memory, "
					    "wrong value in slot %d, "
					    "should be %s, it is %s\n",
				   __FUNCTION__, i,
				   bbrg_name[expect_value],
				   bbrg_name[bb_reg_state->memory[i].value]);
				++errors;
				if (max_errors-- == 0)
					goto fail;
			}
			++i;
			++j;
		}
	}
	while (j < r->mem_size) {
		expect_value = r->mem[j].value;
		if (test_bit(expect_value, r->skip_mem.bits) ||
		    expect_value == BBRG_UNDEFINED)
			++j;
		else
			break;
	}
	if (j != r->mem_size) {
		/* Hit end of memory before testing all the pt_reg slots */
		kdb_printf("%s: Invalid bb_reg_state.memory, "
			    "missing trailing entries\n",
		   __FUNCTION__);
		++errors;
		if (max_errors-- == 0)
			goto fail;
	}
	if (errors)
		goto fail;
	return 1;
fail:
	kdb_printf("%s: on transfer to %s\n", __FUNCTION__, r->name);
	bb_giveup = 1;
	return 1;
}

/* Transfer of control to a label outside the current function.  If the
 * transfer is to a known common code path then do a sanity check on the state
 * at this point.
 */

static void
bb_sanity_check(int type)
{
	enum bb_reg_code expect, actual;
	int i, offset, error = 0;

	for (i = 0; i < ARRAY_SIZE(bb_preserved_reg); ++i) {
		expect = bb_preserved_reg[i];
		actual = bb_reg_code_value(expect);
		offset = bb_reg_code_offset(expect);
		if (expect == actual)
			continue;
		/* type == 1 is sysret/sysexit, ignore RSP */
		if (type && expect == BBRG_RSP)
			continue;
		/* type == 1 is sysret/sysexit, ignore RBP for i386 */
		/* We used to have "#ifndef CONFIG_X86_64" for the type=1 RBP
		 * test; however, x86_64 can run ia32 compatible mode and
		 * hit this problem. Perform the following test anyway!
		 */
		if (type && expect == BBRG_RBP)
			continue;
		/* RSP should contain OSP+0.  Except for ptregscall_common and
		 * ia32_ptregs_common, they get a partial pt_regs, fudge the
		 * stack to make it a full pt_regs then reverse the effect on
		 * exit, so the offset is -0x50 on exit.
		 */
		if (expect == BBRG_RSP &&
		    bb_is_osp_defined(expect) &&
		    (offset == 0 ||
		     (offset == -0x50 &&
		      (strcmp(bb_func_name, "ptregscall_common") == 0 ||
		       strcmp(bb_func_name, "ia32_ptregs_common") == 0))))
			continue;
		kdb_printf("%s: Expected %s, got %s",
			   __FUNCTION__,
			   bbrg_name[expect], bbrg_name[actual]);
		if (offset)
			KDB_DEBUG_BB_OFFSET_PRINTF(offset, "", "");
		kdb_printf("\n");
		error = 1;
	}
	BB_CHECK(error, error, );
}

/* Transfer of control.  Follow the arc and save the current state as input to
 * another basic block.
 */

static void
bb_transfer(bfd_vma from, bfd_vma to, unsigned int drop_through)
{
	int i, found;
	size_t size;
	struct bb* bb = NULL;	/*stupid gcc */
	struct bb_jmp *bb_jmp;
	struct bb_reg_state *state;
	bb_reg_state_canonicalize();
	found = 0;
	for (i = 0; i < bb_jmp_count; ++i) {
		bb_jmp = bb_jmp_list + i;
		if (bb_jmp->from == from &&
		    bb_jmp->to == to &&
		    bb_jmp->drop_through == drop_through) {
			found = 1;
			break;
		}
	}
	if (!found) {
		/* Transfer outside the current function.  Check the special
		 * cases (mainly in entry.S) first.  If it is not a known
		 * special case then check if the target address is the start
		 * of a function or not.  If it is the start of a function then
		 * assume tail recursion and require that the state be the same
		 * as on entry.  Otherwise assume out of line code (e.g.
		 * spinlock contention path) and ignore it, the state can be
		 * anything.
		 */
		kdb_symtab_t symtab;
		if (bb_special_case(to))
			return;
		kdbnearsym(to, &symtab);
		if (symtab.sym_start != to)
			return;
		bb_sanity_check(0);
		if (bb_giveup)
			return;
#ifdef	NO_SIBLINGS
		/* Only print this message when the kernel is compiled with
		 * -fno-optimize-sibling-calls.  Otherwise it would print a
		 * message for every tail recursion call.  If you see the
		 * message below then you probably have an assembler label that
		 * is not listed in the special cases.
		 */
		kdb_printf("  not matched: from "
			   kdb_bfd_vma_fmt0
			   " to " kdb_bfd_vma_fmt0
			   " drop_through %d bb_jmp[%d]\n",
			   from, to, drop_through, i);
#endif	/* NO_SIBLINGS */
		return;
	}
	KDB_DEBUG_BB("  matched: from " kdb_bfd_vma_fmt0
		     " to " kdb_bfd_vma_fmt0
		     " drop_through %d bb_jmp[%d]\n",
		     from, to, drop_through, i);
	found = 0;
	for (i = 0; i < bb_count; ++i) {
		bb = bb_list[i];
		if (bb->start == to) {
			found = 1;
			break;
		}
	}
	BB_CHECK(!found, to, );
	/* If the register state for this arc has already been set (we are
	 * rescanning the block that originates the arc) and the state is the
	 * same as the previous state for this arc then this input to the
	 * target block is the same as last time, so there is no need to rescan
	 * the target block.
	 */
	state = bb_jmp->state;
	size = bb_reg_state_size(bb_reg_state);
	if (state) {
		bb_reg_state->ref_count = state->ref_count;
		if (memcmp(state, bb_reg_state, size) == 0) {
			KDB_DEBUG_BB("  no state change\n");
			return;
		}
		if (--state->ref_count == 0)
			debug_kfree(state);
		bb_jmp->state = NULL;
	}
	/* New input state is required.  To save space, check if any other arcs
	 * have the same state and reuse them where possible.  The overall set
	 * of inputs to the target block is now different so the target block
	 * must be rescanned.
	 */
	bb->changed = 1;
	for (i = 0; i < bb_jmp_count; ++i) {
		state = bb_jmp_list[i].state;
		if (!state)
			continue;
		bb_reg_state->ref_count = state->ref_count;
		if (memcmp(state, bb_reg_state, size) == 0) {
			KDB_DEBUG_BB("  reuse bb_jmp[%d]\n", i);
			bb_jmp->state = state;
			++state->ref_count;
			return;
		}
	}
	state = debug_kmalloc(size, GFP_ATOMIC);
	if (!state) {
		kdb_printf("\n\n%s: out of debug_kmalloc\n", __FUNCTION__);
		bb_giveup = 1;
		return;
	}
	memcpy(state, bb_reg_state, size);
	state->ref_count = 1;
	bb_jmp->state = state;
	KDB_DEBUG_BB("  new state %p\n", state);
}

/* Isolate the processing for 'mov' so it can be used for 'xadd'/'xchg' as
 * well.
 *
 * xadd/xchg expect this function to return BBOU_NOP for special cases,
 * otherwise it returns BBOU_RSWD.  All special cases must be handled entirely
 * within this function, including doing bb_read_operand or bb_write_operand
 * where necessary.
 */

static enum bb_operand_usage
bb_usage_mov(const struct bb_operand *src, const struct bb_operand *dst, int l)
{
	int full_register_src, full_register_dst;
	full_register_src = bb_operand_length(src, bb_decode.opcode[l])
			    == KDB_WORD_SIZE * 8;
	full_register_dst = bb_operand_length(dst, bb_decode.opcode[l])
			    == KDB_WORD_SIZE * 8;
	/* If both src and dst are full integer registers then record the
	 * register change.
	 */
	if (src->reg &&
	    bb_is_int_reg(src->base_rc) &&
	    dst->reg &&
	    bb_is_int_reg(dst->base_rc) &&
	    full_register_src &&
	    full_register_dst) {
		/* Special case for the code that switches stacks in
		 * jprobe_return.  That code must modify RSP but it does it in
		 * a well defined manner.  Do not invalidate RSP.
		 */
		if (src->base_rc == BBRG_RBX &&
		    dst->base_rc == BBRG_RSP &&
		    strcmp(bb_func_name, "jprobe_return") == 0) {
			bb_read_operand(src);
			return BBOU_NOP;
		}
		/* math_abort takes the equivalent of a longjmp structure and
		 * resets the stack.  Ignore this, it leaves RSP well defined.
		 */
		if (dst->base_rc == BBRG_RSP &&
		    strcmp(bb_func_name, "math_abort") == 0) {
			bb_read_operand(src);
			return BBOU_NOP;
		}
		bb_reg_set_reg(dst->base_rc, src->base_rc);
		return BBOU_NOP;
	}
	/* If the move is from a full integer register to stack then record it.
	 */
	if (src->reg &&
	    bb_is_simple_memory(dst) &&
	    bb_is_osp_defined(dst->base_rc) &&
	    full_register_src) {
		/* Ugly special case.  Initializing list heads on stack causes
		 * false references to stack variables when the list head is
		 * used.  Static code analysis cannot detect that the list head
		 * has been changed by a previous execution loop and that a
		 * basic block is only executed after the list head has been
		 * changed.
		 *
		 * These false references can result in valid stack variables
		 * being incorrectly cleared on some logic paths.  Ignore
		 * stores to stack variables which point to themselves or to
		 * the previous word so the list head initialization is not
		 * recorded.
		 */
		if (bb_is_osp_defined(src->base_rc)) {
			int stack1 = bb_reg_code_offset(src->base_rc);
			int stack2 = bb_reg_code_offset(dst->base_rc) +
				     dst->disp;
			if (stack1 == stack2 ||
			    stack1 == stack2 - KDB_WORD_SIZE)
				return BBOU_NOP;
		}
		bb_memory_set_reg(dst->base_rc, src->base_rc, dst->disp);
		return BBOU_NOP;
	}
	/* If the move is from stack to a full integer register then record it.
	 */
	if (bb_is_simple_memory(src) &&
	    bb_is_osp_defined(src->base_rc) &&
	    dst->reg &&
	    bb_is_int_reg(dst->base_rc) &&
	    full_register_dst) {
#ifdef	CONFIG_X86_32
#ifndef TSS_sysenter_sp0
#define TSS_sysenter_sp0 SYSENTER_stack_sp0
#endif
		/* mov from TSS_sysenter_sp0+offset to esp to fix up the
		 * sysenter stack, it leaves esp well defined.  mov
		 * TSS_ysenter_sp0+offset(%esp),%esp is followed by up to 5
		 * push instructions to mimic the hardware stack push.  If
		 * TSS_sysenter_sp0 is offset then only 3 words will be
		 * pushed.
		 */
		if (dst->base_rc == BBRG_RSP &&
		    src->disp >= TSS_sysenter_sp0 &&
		    bb_is_osp_defined(BBRG_RSP)) {
			int pushes;
			pushes = src->disp == TSS_sysenter_sp0 ? 5 : 3;
			bb_reg_code_set_offset(BBRG_RSP,
				bb_reg_code_offset(BBRG_RSP) +
					pushes * KDB_WORD_SIZE);
			KDB_DEBUG_BB_OFFSET(
				bb_reg_code_offset(BBRG_RSP),
				"  sysenter fixup, RSP",
			       "\n");
			return BBOU_NOP;
		}
#endif	/* CONFIG_X86_32 */
		bb_read_operand(src);
		bb_reg_set_memory(dst->base_rc, src->base_rc, src->disp);
		return BBOU_NOP;
	}
	/* move %gs:0x<nn>,%rsp is used to unconditionally switch to another
	 * stack.  Ignore this special case, it is handled by the stack
	 * unwinding code.
	 */
	if (src->segment &&
	    strcmp(src->segment, "%gs") == 0 &&
	    dst->reg &&
	    dst->base_rc == BBRG_RSP)
		return BBOU_NOP;
	/* move %reg,%reg is a nop */
	if (src->reg &&
	    dst->reg &&
	    !src->segment &&
	    !dst->segment &&
	    strcmp(src->base, dst->base) == 0)
		return BBOU_NOP;
	/* Special case for the code that switches stacks in the scheduler
	 * (switch_to()).  That code must modify RSP but it does it in a well
	 * defined manner.  Do not invalidate RSP.
	 */
	if (dst->reg &&
	    dst->base_rc == BBRG_RSP &&
	    full_register_dst &&
	    bb_is_scheduler_address()) {
		bb_read_operand(src);
		return BBOU_NOP;
	}
	/* Special case for the code that switches stacks in resume from
	 * hibernation code.  That code must modify RSP but it does it in a
	 * well defined manner.  Do not invalidate RSP.
	 */
	if (src->memory &&
	    dst->reg &&
	    dst->base_rc == BBRG_RSP &&
	    full_register_dst &&
	    strcmp(bb_func_name, "restore_image") == 0) {
		bb_read_operand(src);
		return BBOU_NOP;
	}
	return BBOU_RSWD;
}

static enum bb_operand_usage
bb_usage_xadd(const struct bb_operand *src, const struct bb_operand *dst)
{
	/* Simulate xadd as a series of instructions including mov, that way we
	 * get the benefit of all the special cases already handled by
	 * BBOU_MOV.
	 *
	 * tmp = src + dst, src = dst, dst = tmp.
	 *
	 * For tmp, pick a register that is undefined.  If all registers are
	 * defined then pick one that is not being used by xadd.
	 */
	enum bb_reg_code reg = BBRG_UNDEFINED;
	struct bb_operand tmp;
	struct bb_reg_contains save_tmp;
	enum bb_operand_usage usage;
	int undefined = 0;
	for (reg = BBRG_RAX; reg < BBRG_RAX + KDB_INT_REGISTERS; ++reg) {
		if (bb_reg_code_value(reg) == BBRG_UNDEFINED) {
			undefined = 1;
			break;
		}
	}
	if (!undefined) {
		for (reg = BBRG_RAX; reg < BBRG_RAX + KDB_INT_REGISTERS; ++reg) {
			if (reg != src->base_rc &&
			    reg != src->index_rc &&
			    reg != dst->base_rc &&
			    reg != dst->index_rc &&
			    reg != BBRG_RSP)
				break;
		}
	}
	KDB_DEBUG_BB("  %s saving tmp %s\n", __FUNCTION__, bbrg_name[reg]);
	save_tmp = bb_reg_state->contains[reg - BBRG_RAX];
	bb_reg_set_undef(reg);
	memset(&tmp, 0, sizeof(tmp));
	tmp.present = 1;
	tmp.reg = 1;
	tmp.base = debug_kmalloc(strlen(bbrg_name[reg]) + 2, GFP_ATOMIC);
	if (tmp.base) {
		tmp.base[0] = '%';
		strcpy(tmp.base + 1, bbrg_name[reg]);
	}
	tmp.base_rc = reg;
	bb_read_operand(src);
	bb_read_operand(dst);
	if (bb_usage_mov(src, dst, sizeof("xadd")-1) == BBOU_NOP)
		usage = BBOU_RSRD;
	else
		usage = BBOU_RSRDWS;
	bb_usage_mov(&tmp, dst, sizeof("xadd")-1);
	KDB_DEBUG_BB("  %s restoring tmp %s\n", __FUNCTION__, bbrg_name[reg]);
	bb_reg_state->contains[reg - BBRG_RAX] = save_tmp;
	debug_kfree(tmp.base);
	return usage;
}

static enum bb_operand_usage
bb_usage_xchg(const struct bb_operand *src, const struct bb_operand *dst)
{
	/* Simulate xchg as a series of mov instructions, that way we get the
	 * benefit of all the special cases already handled by BBOU_MOV.
	 *
	 * mov dst,tmp; mov src,dst; mov tmp,src;
	 *
	 * For tmp, pick a register that is undefined.  If all registers are
	 * defined then pick one that is not being used by xchg.
	 */
	enum bb_reg_code reg = BBRG_UNDEFINED;
	int rs = BBOU_RS, rd = BBOU_RD, ws = BBOU_WS, wd = BBOU_WD;
	struct bb_operand tmp;
	struct bb_reg_contains save_tmp;
	int undefined = 0;
	for (reg = BBRG_RAX; reg < BBRG_RAX + KDB_INT_REGISTERS; ++reg) {
		if (bb_reg_code_value(reg) == BBRG_UNDEFINED) {
			undefined = 1;
			break;
		}
	}
	if (!undefined) {
		for (reg = BBRG_RAX; reg < BBRG_RAX + KDB_INT_REGISTERS; ++reg) {
			if (reg != src->base_rc &&
			    reg != src->index_rc &&
			    reg != dst->base_rc &&
			    reg != dst->index_rc &&
			    reg != BBRG_RSP)
				break;
		}
	}
	KDB_DEBUG_BB("  %s saving tmp %s\n", __FUNCTION__, bbrg_name[reg]);
	save_tmp = bb_reg_state->contains[reg - BBRG_RAX];
	memset(&tmp, 0, sizeof(tmp));
	tmp.present = 1;
	tmp.reg = 1;
	tmp.base = debug_kmalloc(strlen(bbrg_name[reg]) + 2, GFP_ATOMIC);
	if (tmp.base) {
		tmp.base[0] = '%';
		strcpy(tmp.base + 1, bbrg_name[reg]);
	}
	tmp.base_rc = reg;
	if (bb_usage_mov(dst, &tmp, sizeof("xchg")-1) == BBOU_NOP)
		rd = 0;
	if (bb_usage_mov(src, dst, sizeof("xchg")-1) == BBOU_NOP) {
		rs = 0;
		wd = 0;
	}
	if (bb_usage_mov(&tmp, src, sizeof("xchg")-1) == BBOU_NOP)
		ws = 0;
	KDB_DEBUG_BB("  %s restoring tmp %s\n", __FUNCTION__, bbrg_name[reg]);
	bb_reg_state->contains[reg - BBRG_RAX] = save_tmp;
	debug_kfree(tmp.base);
	return rs | rd | ws | wd;
}

/* Invalidate all the scratch registers */

static void
bb_invalidate_scratch_reg(void)
{
	int i, j;
	for (i = BBRG_RAX; i < BBRG_RAX + KDB_INT_REGISTERS; ++i) {
		for (j = 0; j < ARRAY_SIZE(bb_preserved_reg); ++j) {
			if (i == bb_preserved_reg[j])
				goto preserved;
		}
		bb_reg_set_undef(i);
preserved:
		continue;
	}
}

static void
bb_pass2_computed_jmp(const struct bb_operand *src)
{
	unsigned long table = src->disp;
	kdb_machreg_t addr;
	while (!bb_giveup) {
		if (kdb_getword(&addr, table, sizeof(addr)))
			return;
		if (addr < bb_func_start || addr >= bb_func_end)
			return;
		bb_transfer(bb_curr_addr, addr, 0);
		table += KDB_WORD_SIZE;
	}
}

/* The current instruction has been decoded and all the information is in
 * bb_decode.  Based on the opcode, track any operand usage that we care about.
 */

static void
bb_usage(void)
{
	enum bb_operand_usage usage = bb_decode.match->usage;
	struct bb_operand *src = &bb_decode.src;
	struct bb_operand *dst = &bb_decode.dst;
	struct bb_operand *dst2 = &bb_decode.dst2;
	int opcode_suffix, operand_length;

	/* First handle all the special usage cases, and map them to a generic
	 * case after catering for the side effects.
	 */

	if (usage == BBOU_IMUL &&
	    src->present && !dst->present && !dst2->present) {
		/* single operand imul, same effects as mul */
		usage = BBOU_MUL;
	}

	/* AT&T syntax uses movs<l1><l2> for move with sign extension, instead
	 * of the Intel movsx.  The AT&T syntax causes problems for the opcode
	 * mapping; movs with sign extension needs to be treated as a generic
	 * read src, write dst, but instead it falls under the movs I/O
	 * instruction.  Fix it.
	 */
	if (usage == BBOU_MOVS && strlen(bb_decode.opcode) > 5)
		usage = BBOU_RSWD;

	/* This switch statement deliberately does not use 'default' at the top
	 * level.  That way the compiler will complain if a new BBOU_ enum is
	 * added above and not explicitly handled here.
	 */
	switch (usage) {
	case BBOU_UNKNOWN:	/* drop through */
	case BBOU_RS:		/* drop through */
	case BBOU_RD:		/* drop through */
	case BBOU_RSRD:		/* drop through */
	case BBOU_WS:		/* drop through */
	case BBOU_RSWS:		/* drop through */
	case BBOU_RDWS:		/* drop through */
	case BBOU_RSRDWS:	/* drop through */
	case BBOU_WD:		/* drop through */
	case BBOU_RSWD:		/* drop through */
	case BBOU_RDWD:		/* drop through */
	case BBOU_RSRDWD:	/* drop through */
	case BBOU_WSWD:		/* drop through */
	case BBOU_RSWSWD:	/* drop through */
	case BBOU_RDWSWD:	/* drop through */
	case BBOU_RSRDWSWD:
		break;		/* ignore generic usage for now */
	case BBOU_ADD:
		/* Special case for add instructions that adjust registers
		 * which are mapping the stack.
		 */
		if (dst->reg && bb_is_osp_defined(dst->base_rc)) {
			bb_adjust_osp_instruction(1);
			usage = BBOU_RS;
		} else {
			usage = BBOU_RSRDWD;
		}
		break;
	case BBOU_CALL:
		/* Invalidate the scratch registers.  Functions sync_regs and
		 * save_v86_state are special, their return value is the new
		 * stack pointer.
		 */
		bb_reg_state_print(bb_reg_state);
		bb_invalidate_scratch_reg();
		if (bb_is_static_disp(src)) {
			if (src->disp == bb_sync_regs) {
				bb_reg_set_reg(BBRG_RAX, BBRG_RSP);
			} else if (src->disp == bb_save_v86_state) {
				bb_reg_set_reg(BBRG_RAX, BBRG_RSP);
				bb_adjust_osp(BBRG_RAX, +KDB_WORD_SIZE);
			}
		}
		usage = BBOU_NOP;
		break;
	case BBOU_CBW:
		/* Convert word in RAX.  Read RAX, write RAX */
		bb_reg_read(BBRG_RAX);
		bb_reg_set_undef(BBRG_RAX);
		usage = BBOU_NOP;
		break;
	case BBOU_CMOV:
		/* cmove %gs:0x<nn>,%rsp is used to conditionally switch to
		 * another stack.  Ignore this special case, it is handled by
		 * the stack unwinding code.
		 */
		if (src->segment &&
		    strcmp(src->segment, "%gs") == 0 &&
		    dst->reg &&
		    dst->base_rc == BBRG_RSP)
			usage = BBOU_NOP;
		else
			usage = BBOU_RSWD;
		break;
	case BBOU_CMPXCHG:
		/* Read RAX, write RAX plus src read, dst write */
		bb_reg_read(BBRG_RAX);
		bb_reg_set_undef(BBRG_RAX);
		usage = BBOU_RSWD;
		break;
	case BBOU_CMPXCHGD:
		/* Read RAX, RBX, RCX, RDX, write RAX, RDX plus src read/write */
		bb_reg_read(BBRG_RAX);
		bb_reg_read(BBRG_RBX);
		bb_reg_read(BBRG_RCX);
		bb_reg_read(BBRG_RDX);
		bb_reg_set_undef(BBRG_RAX);
		bb_reg_set_undef(BBRG_RDX);
		usage = BBOU_RSWS;
		break;
	case BBOU_CPUID:
		/* Read RAX, write RAX, RBX, RCX, RDX */
		bb_reg_read(BBRG_RAX);
		bb_reg_set_undef(BBRG_RAX);
		bb_reg_set_undef(BBRG_RBX);
		bb_reg_set_undef(BBRG_RCX);
		bb_reg_set_undef(BBRG_RDX);
		usage = BBOU_NOP;
		break;
	case BBOU_CWD:
		/* Convert word in RAX, RDX.  Read RAX, write RDX */
		bb_reg_read(BBRG_RAX);
		bb_reg_set_undef(BBRG_RDX);
		usage = BBOU_NOP;
		break;
	case BBOU_DIV:	/* drop through */
	case BBOU_IDIV:
		/* The 8 bit variants only affect RAX, the 16, 32 and 64 bit
		 * variants affect RDX as well.
		 */
		switch (usage) {
		case BBOU_DIV:
			opcode_suffix = bb_decode.opcode[3];
			break;
		case BBOU_IDIV:
			opcode_suffix = bb_decode.opcode[4];
			break;
		default:
			opcode_suffix = 'q';
			break;
		}
		operand_length = bb_operand_length(src, opcode_suffix);
		bb_reg_read(BBRG_RAX);
		bb_reg_set_undef(BBRG_RAX);
		if (operand_length != 8) {
			bb_reg_read(BBRG_RDX);
			bb_reg_set_undef(BBRG_RDX);
		}
		usage = BBOU_RS;
		break;
	case BBOU_IMUL:
		/* Only the two and three operand forms get here.  The one
		 * operand form is treated as mul.
		 */
		if (dst2->present) {
			/* The three operand form is a special case, read the first two
			 * operands, write the third.
			 */
			bb_read_operand(src);
			bb_read_operand(dst);
			bb_write_operand(dst2);
			usage = BBOU_NOP;
		} else {
			usage = BBOU_RSRDWD;
		}
		break;
	case BBOU_IRET:
		bb_sanity_check(0);
		usage = BBOU_NOP;
		break;
	case BBOU_JMP:
		if (bb_is_static_disp(src))
			bb_transfer(bb_curr_addr, src->disp, 0);
		else if (src->indirect &&
			 src->disp &&
			 src->base == NULL &&
			 src->index &&
			 src->scale == KDB_WORD_SIZE)
			bb_pass2_computed_jmp(src);
		usage = BBOU_RS;
		break;
	case BBOU_LAHF:
		/* Write RAX */
		bb_reg_set_undef(BBRG_RAX);
		usage = BBOU_NOP;
		break;
	case BBOU_LEA:
		/* dst = src + disp.  Often used to calculate offsets into the
		 * stack, so check if it uses a stack pointer.
		 */
		usage = BBOU_RSWD;
		if (bb_is_simple_memory(src)) {
		       if (bb_is_osp_defined(src->base_rc)) {
				bb_reg_set_reg(dst->base_rc, src->base_rc);
				bb_adjust_osp_instruction(1);
				usage = BBOU_RS;
			} else if (src->disp == 0 &&
				   src->base_rc == dst->base_rc) {
				/* lea 0(%reg),%reg is generated by i386
				 * GENERIC_NOP7.
				 */
				usage = BBOU_NOP;
			} else if (src->disp == 4096 &&
				   (src->base_rc == BBRG_R8 ||
				    src->base_rc == BBRG_RDI) &&
				   strcmp(bb_func_name, "relocate_kernel") == 0) {
				/* relocate_kernel: setup a new stack at the
				 * end of the physical control page, using
				 * (x86_64) lea 4096(%r8),%rsp or (i386) lea
				 * 4096(%edi),%esp
				 */
				usage = BBOU_NOP;
			}
		}
		break;
	case BBOU_LEAVE:
		/* RSP = RBP; RBP = *(RSP); RSP += KDB_WORD_SIZE; */
		bb_reg_set_reg(BBRG_RSP, BBRG_RBP);
		if (bb_is_osp_defined(BBRG_RSP))
			bb_reg_set_memory(BBRG_RBP, BBRG_RSP, 0);
		else
			bb_reg_set_undef(BBRG_RBP);
		if (bb_is_osp_defined(BBRG_RSP))
			bb_adjust_osp(BBRG_RSP, KDB_WORD_SIZE);
		/* common_interrupt uses leave in a non-standard manner */
		if (strcmp(bb_func_name, "common_interrupt") != 0)
			bb_sanity_check(0);
		usage = BBOU_NOP;
		break;
	case BBOU_LODS:
		/* Read RSI, write RAX, RSI */
		bb_reg_read(BBRG_RSI);
		bb_reg_set_undef(BBRG_RAX);
		bb_reg_set_undef(BBRG_RSI);
		usage = BBOU_NOP;
		break;
	case BBOU_LOOP:
		/* Read and write RCX */
		bb_reg_read(BBRG_RCX);
		bb_reg_set_undef(BBRG_RCX);
		if (bb_is_static_disp(src))
			bb_transfer(bb_curr_addr, src->disp, 0);
		usage = BBOU_NOP;
		break;
	case BBOU_LSS:
		/* lss offset(%esp),%esp leaves esp well defined */
		if (dst->reg &&
		    dst->base_rc == BBRG_RSP &&
		    bb_is_simple_memory(src) &&
		    src->base_rc == BBRG_RSP) {
			bb_adjust_osp(BBRG_RSP, 2*KDB_WORD_SIZE + src->disp);
			usage = BBOU_NOP;
		} else {
			usage = BBOU_RSWD;
		}
		break;
	case BBOU_MONITOR:
		/* Read RAX, RCX, RDX */
		bb_reg_set_undef(BBRG_RAX);
		bb_reg_set_undef(BBRG_RCX);
		bb_reg_set_undef(BBRG_RDX);
		usage = BBOU_NOP;
		break;
	case BBOU_MOV:
		usage = bb_usage_mov(src, dst, sizeof("mov")-1);
		break;
	case BBOU_MOVS:
		/* Read RSI, RDI, write RSI, RDI */
		bb_reg_read(BBRG_RSI);
		bb_reg_read(BBRG_RDI);
		bb_reg_set_undef(BBRG_RSI);
		bb_reg_set_undef(BBRG_RDI);
		usage = BBOU_NOP;
		break;
	case BBOU_MUL:
		/* imul (one operand form only) or mul.  Read RAX.  If the
		 * operand length is not 8 then write RDX.
		 */
		if (bb_decode.opcode[0] == 'i')
			opcode_suffix = bb_decode.opcode[4];
		else
			opcode_suffix = bb_decode.opcode[3];
		operand_length = bb_operand_length(src, opcode_suffix);
		bb_reg_read(BBRG_RAX);
		if (operand_length != 8)
			bb_reg_set_undef(BBRG_RDX);
		usage = BBOU_NOP;
		break;
	case BBOU_MWAIT:
		/* Read RAX, RCX */
		bb_reg_read(BBRG_RAX);
		bb_reg_read(BBRG_RCX);
		usage = BBOU_NOP;
		break;
	case BBOU_NOP:
		break;
	case BBOU_OUTS:
		/* Read RSI, RDX, write RSI */
		bb_reg_read(BBRG_RSI);
		bb_reg_read(BBRG_RDX);
		bb_reg_set_undef(BBRG_RSI);
		usage = BBOU_NOP;
		break;
	case BBOU_POP:
		/* Complicated by the fact that you can pop from top of stack
		 * to a stack location, for this case the destination location
		 * is calculated after adjusting RSP.  Analysis of the kernel
		 * code shows that gcc only uses this strange format to get the
		 * flags into a local variable, e.g. pushf; popl 0x10(%esp); so
		 * I am going to ignore this special case.
		 */
		usage = BBOU_WS;
		if (!bb_is_osp_defined(BBRG_RSP)) {
			if (!bb_is_scheduler_address()) {
				kdb_printf("pop when BBRG_RSP is undefined?\n");
				bb_giveup = 1;
			}
		} else {
			if (src->reg) {
				bb_reg_set_memory(src->base_rc, BBRG_RSP, 0);
				usage = BBOU_NOP;
			}
			/* pop %rsp does not adjust rsp */
			if (!src->reg ||
			    src->base_rc != BBRG_RSP)
				bb_adjust_osp(BBRG_RSP, KDB_WORD_SIZE);
		}
		break;
	case BBOU_POPF:
		/* Do not care about flags, just adjust RSP */
		if (!bb_is_osp_defined(BBRG_RSP)) {
			if (!bb_is_scheduler_address()) {
				kdb_printf("popf when BBRG_RSP is undefined?\n");
				bb_giveup = 1;
			}
		} else {
			bb_adjust_osp(BBRG_RSP, KDB_WORD_SIZE);
		}
		usage = BBOU_WS;
		break;
	case BBOU_PUSH:
		/* Complicated by the fact that you can push from a stack
		 * location to top of stack, the source location is calculated
		 * before adjusting RSP.  Analysis of the kernel code shows
		 * that gcc only uses this strange format to restore the flags
		 * from a local variable, e.g. pushl 0x10(%esp); popf; so I am
		 * going to ignore this special case.
		 */
		usage = BBOU_RS;
		if (!bb_is_osp_defined(BBRG_RSP)) {
			if (!bb_is_scheduler_address()) {
				kdb_printf("push when BBRG_RSP is undefined?\n");
				bb_giveup = 1;
			}
		} else {
			bb_adjust_osp(BBRG_RSP, -KDB_WORD_SIZE);
			if (src->reg &&
			    bb_reg_code_offset(BBRG_RSP) <= 0)
				bb_memory_set_reg(BBRG_RSP, src->base_rc, 0);
		}
		break;
	case BBOU_PUSHF:
		/* Do not care about flags, just adjust RSP */
		if (!bb_is_osp_defined(BBRG_RSP)) {
			if (!bb_is_scheduler_address()) {
				kdb_printf("pushf when BBRG_RSP is undefined?\n");
				bb_giveup = 1;
			}
		} else {
			bb_adjust_osp(BBRG_RSP, -KDB_WORD_SIZE);
		}
		usage = BBOU_WS;
		break;
	case BBOU_RDMSR:
		/* Read RCX, write RAX, RDX */
		bb_reg_read(BBRG_RCX);
		bb_reg_set_undef(BBRG_RAX);
		bb_reg_set_undef(BBRG_RDX);
		usage = BBOU_NOP;
		break;
	case BBOU_RDTSC:
		/* Write RAX, RDX */
		bb_reg_set_undef(BBRG_RAX);
		bb_reg_set_undef(BBRG_RDX);
		usage = BBOU_NOP;
		break;
	case BBOU_RET:
		usage = BBOU_NOP;
		/* Functions that restore state which was saved by another
		 * function or build new kernel stacks.  We cannot verify what
		 * is being restored so skip the sanity check.
		 */
		if (strcmp(bb_func_name, "restore_image") == 0 ||
		    strcmp(bb_func_name, "relocate_kernel") == 0 ||
		    strcmp(bb_func_name, "identity_mapped") == 0 ||
		    strcmp(bb_func_name, "xen_iret_crit_fixup") == 0 ||
		    strcmp(bb_func_name, "math_abort") == 0)
			break;
		bb_sanity_check(0);
		break;
	case BBOU_SAHF:
		/* Read RAX */
		bb_reg_read(BBRG_RAX);
		usage = BBOU_NOP;
		break;
	case BBOU_SCAS:
		/* Read RAX, RDI, write RDI */
		bb_reg_read(BBRG_RAX);
		bb_reg_read(BBRG_RDI);
		bb_reg_set_undef(BBRG_RDI);
		usage = BBOU_NOP;
		break;
	case BBOU_SUB:
		/* Special case for sub instructions that adjust registers
		 * which are mapping the stack.
		 */
		if (dst->reg && bb_is_osp_defined(dst->base_rc)) {
			bb_adjust_osp_instruction(-1);
			usage = BBOU_RS;
		} else {
			usage = BBOU_RSRDWD;
		}
		break;
	case BBOU_SYSEXIT:
		bb_sanity_check(1);
		usage = BBOU_NOP;
		break;
	case BBOU_SYSRET:
		bb_sanity_check(1);
		usage = BBOU_NOP;
		break;
	case BBOU_WRMSR:
		/* Read RCX, RAX, RDX */
		bb_reg_read(BBRG_RCX);
		bb_reg_read(BBRG_RAX);
		bb_reg_read(BBRG_RDX);
		usage = BBOU_NOP;
		break;
	case BBOU_XADD:
		usage = bb_usage_xadd(src, dst);
		break;
	case BBOU_XCHG:
		/* i386 do_IRQ with 4K stacks does xchg %ebx,%esp; call
		 * irq_handler; mov %ebx,%esp; to switch stacks.  Ignore this
		 * stack switch when tracking registers, it is handled by
		 * higher level backtrace code.  Convert xchg %ebx,%esp to mov
		 * %esp,%ebx so the later mov %ebx,%esp becomes a NOP and the
		 * stack remains defined so we can backtrace through do_IRQ's
		 * stack switch.
		 *
		 * Ditto for do_softirq.
		 */
		if (src->reg &&
		    dst->reg &&
		    src->base_rc == BBRG_RBX &&
		    dst->base_rc == BBRG_RSP &&
		    (strcmp(bb_func_name, "do_IRQ") == 0 ||
		     strcmp(bb_func_name, "do_softirq") == 0)) {
			strcpy(bb_decode.opcode, "mov");
			usage = bb_usage_mov(dst, src, sizeof("mov")-1);
		} else {
			usage = bb_usage_xchg(src, dst);
		}
		break;
	case BBOU_XOR:
		/* xor %reg,%reg only counts as a register write, the original
		 * contents of reg are irrelevant.
		 */
		if (src->reg && dst->reg && src->base_rc == dst->base_rc)
			usage = BBOU_WS;
		else
			usage = BBOU_RSRDWD;
		break;
	}

	/* The switch statement above handled all the special cases.  Every
	 * opcode should now have a usage of NOP or one of the generic cases.
	 */
	if (usage == BBOU_UNKNOWN || usage == BBOU_NOP) {
		/* nothing to do */
	} else if (usage >= BBOU_RS && usage <= BBOU_RSRDWSWD) {
		if (usage & BBOU_RS)
			bb_read_operand(src);
		if (usage & BBOU_RD)
			bb_read_operand(dst);
		if (usage & BBOU_WS)
			bb_write_operand(src);
		if (usage & BBOU_WD)
			bb_write_operand(dst);
	} else {
		kdb_printf("%s: opcode not fully handled\n", __FUNCTION__);
		if (!KDB_DEBUG(BB)) {
			bb_print_opcode();
			if (bb_decode.src.present)
				bb_print_operand("src", &bb_decode.src);
			if (bb_decode.dst.present)
				bb_print_operand("dst", &bb_decode.dst);
			if (bb_decode.dst2.present)
				bb_print_operand("dst2", &bb_decode.dst2);
		}
		bb_giveup = 1;
	}
}

static void
bb_parse_buffer(void)
{
	char *p, *src, *dst = NULL, *dst2 = NULL;
	int paren = 0;
	p = bb_buffer;
	memset(&bb_decode, 0, sizeof(bb_decode));
	KDB_DEBUG_BB(" '%s'\n", p);
	p += strcspn(p, ":");	/* skip address and function name+offset: */
	if (*p++ != ':') {
		kdb_printf("%s: cannot find ':' in buffer '%s'\n",
			   __FUNCTION__, bb_buffer);
		bb_giveup = 1;
		return;
	}
	p += strspn(p, " \t");	/* step to opcode */
	if (strncmp(p, "(bad)", 5) == 0)
		strcpy(p, "nop");
	/* separate any opcode prefix */
	if (strncmp(p, "lock", 4) == 0 ||
	    strncmp(p, "rep", 3) == 0 ||
	    strncmp(p, "rex", 3) == 0 ||
	    strncmp(p, "addr", 4) == 0) {
		bb_decode.prefix = p;
		p += strcspn(p, " \t");
		*p++ = '\0';
		p += strspn(p, " \t");
	}
	bb_decode.opcode = p;
	strsep(&p, " \t");	/* step to end of opcode */
	if (bb_parse_opcode())
		return;
	if (!p)
		goto no_operands;
	p += strspn(p, " \t");	/* step to operand(s) */
	if (!*p)
		goto no_operands;
	src = p;
	p = strsep(&p, " \t");	/* strip comments after operands */
	/* split 'src','dst' but ignore ',' inside '(' ')' */
	while (*p) {
		if (*p == '(') {
			++paren;
		} else if (*p == ')') {
			--paren;
		} else if (*p == ',' && paren == 0) {
			*p = '\0';
			if (dst)
				dst2 = p+1;
			else
				dst = p+1;
		}
		++p;
	}
	bb_parse_operand(src, &bb_decode.src);
	if (KDB_DEBUG(BB))
		bb_print_operand("src", &bb_decode.src);
	if (dst && !bb_giveup) {
		bb_parse_operand(dst, &bb_decode.dst);
		if (KDB_DEBUG(BB))
			bb_print_operand("dst", &bb_decode.dst);
	}
	if (dst2 && !bb_giveup) {
		bb_parse_operand(dst2, &bb_decode.dst2);
		if (KDB_DEBUG(BB))
			bb_print_operand("dst2", &bb_decode.dst2);
	}
no_operands:
	if (!bb_giveup)
		bb_usage();
}

static int
bb_dis_pass2(PTR file, const char *fmt, ...)
{
	char *p;
	int l = strlen(bb_buffer);
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(bb_buffer + l, sizeof(bb_buffer) - l, fmt, ap);
	va_end(ap);
	if ((p = strchr(bb_buffer, '\n'))) {
		*p = '\0';
		p = bb_buffer;
		p += strcspn(p, ":");
		if (*p++ == ':')
			bb_fixup_switch_to(p);
		bb_parse_buffer();
		bb_buffer[0] = '\0';
	}
	return 0;
}

static void
bb_printaddr_pass2(bfd_vma addr, disassemble_info *dip)
{
	kdb_symtab_t symtab;
	unsigned int offset;
	dip->fprintf_func(dip->stream, "0x%lx", addr);
	kdbnearsym(addr, &symtab);
	if (symtab.sym_name) {
		dip->fprintf_func(dip->stream, " <%s", symtab.sym_name);
		if ((offset = addr - symtab.sym_start))
			dip->fprintf_func(dip->stream, "+0x%x", offset);
		dip->fprintf_func(dip->stream, ">");
	}
}

/* Set the starting register and memory state for the current bb */

static void
bb_start_block0_special(void)
{
	int i;
	short offset_address;
	enum bb_reg_code reg, value;
	struct bb_name_state *r;
	for (i = 0, r = bb_special_cases;
	     i < ARRAY_SIZE(bb_special_cases);
	     ++i, ++r) {
		if (bb_func_start == r->address && r->fname == NULL)
			goto match;
	}
	return;
match:
	/* Set the running registers */
	for (reg = BBRG_RAX; reg < r->regs_size; ++reg) {
		value = r->regs[reg].value;
		if (test_bit(value, r->skip_regs.bits)) {
			/* this regs entry is not defined for this label */
			continue;
		}
		bb_reg_code_set_value(reg, value);
		bb_reg_code_set_offset(reg, r->regs[reg].offset);
	}
	/* Set any memory contents, e.g. pt_regs.  Adjust RSP as required. */
	offset_address = 0;
	for (i = 0; i < r->mem_size; ++i) {
		offset_address = max_t(int,
				r->mem[i].offset_address + KDB_WORD_SIZE,
				offset_address);
	}
	if (bb_reg_code_offset(BBRG_RSP) > -offset_address)
		bb_adjust_osp(BBRG_RSP, -offset_address - bb_reg_code_offset(BBRG_RSP));
	for (i = 0; i < r->mem_size; ++i) {
		value = r->mem[i].value;
		if (test_bit(value, r->skip_mem.bits)) {
			/* this memory entry is not defined for this label */
			continue;
		}
		bb_memory_set_reg_value(BBRG_RSP, r->mem[i].offset_address,
					value, 0);
		bb_reg_set_undef(value);
	}
	return;
}

static void
bb_pass2_start_block(int number)
{
	int i, j, k, first, changed;
	size_t size;
	struct bb_jmp *bb_jmp;
	struct bb_reg_state *state;
	struct bb_memory_contains *c1, *c2;
	bb_reg_state->mem_count = bb_reg_state_max;
	size = bb_reg_state_size(bb_reg_state);
	memset(bb_reg_state, 0, size);

	if (number == 0) {
		/* The first block is assumed to have well defined inputs */
		bb_start_block0();
		/* Some assembler labels have non-standard entry
		 * states.
		 */
		bb_start_block0_special();
		bb_reg_state_print(bb_reg_state);
		return;
	}

	/* Merge all the input states for the current bb together */
	first = 1;
	changed = 0;
	for (i = 0; i < bb_jmp_count; ++i) {
		bb_jmp = bb_jmp_list + i;
		if (bb_jmp->to != bb_curr->start)
			continue;
		state = bb_jmp->state;
		if (!state)
			continue;
		if (first) {
			size = bb_reg_state_size(state);
			memcpy(bb_reg_state, state, size);
			KDB_DEBUG_BB("  first state %p\n", state);
			bb_reg_state_print(bb_reg_state);
			first = 0;
			continue;
		}

		KDB_DEBUG_BB("  merging state %p\n", state);
		/* Merge the register states */
		for (j = 0; j < ARRAY_SIZE(state->contains); ++j) {
			if (memcmp(bb_reg_state->contains + j,
				   state->contains + j,
				   sizeof(bb_reg_state->contains[0]))) {
				/* Different states for this register from two
				 * or more inputs, make it undefined.
				 */
				if (bb_reg_state->contains[j].value ==
				    BBRG_UNDEFINED) {
					KDB_DEBUG_BB("  ignoring %s\n",
						    bbrg_name[j + BBRG_RAX]);
				} else {
					bb_reg_set_undef(BBRG_RAX + j);
					changed = 1;
				}
			}
		}

		/* Merge the memory states.  This relies on both
		 * bb_reg_state->memory and state->memory being sorted in
		 * descending order, with undefined entries at the end.
		 */
		c1 = bb_reg_state->memory;
		c2 = state->memory;
		j = k = 0;
		while (j < bb_reg_state->mem_count &&
		       k < state->mem_count) {
			if (c1->offset_address < c2->offset_address) {
				KDB_DEBUG_BB_OFFSET(c2->offset_address,
						    "  ignoring c2->offset_address ",
						    "\n");
				++c2;
				++k;
				continue;
			}
			if (c1->offset_address > c2->offset_address) {
				/* Memory location is not in all input states,
				 * delete the memory location.
				 */
				bb_delete_memory(c1->offset_address);
				changed = 1;
				++c1;
				++j;
				continue;
			}
			if (memcmp(c1, c2, sizeof(*c1))) {
				/* Same location, different contents, delete
				 * the memory location.
				 */
				bb_delete_memory(c1->offset_address);
				KDB_DEBUG_BB_OFFSET(c2->offset_address,
						    "  ignoring c2->offset_address ",
						    "\n");
				changed = 1;
			}
			++c1;
			++c2;
			++j;
			++k;
		}
		while (j < bb_reg_state->mem_count) {
			bb_delete_memory(c1->offset_address);
			changed = 1;
			++c1;
			++j;
		}
	}
	if (changed) {
		KDB_DEBUG_BB("  final state\n");
		bb_reg_state_print(bb_reg_state);
	}
}

/* We have reached the exit point from the current function, either a call to
 * the next function or the instruction that was about to executed when an
 * interrupt occurred.  Save the current register state in bb_exit_state.
 */

static void
bb_save_exit_state(void)
{
	size_t size;
	debug_kfree(bb_exit_state);
	bb_exit_state = NULL;
	bb_reg_state_canonicalize();
	size = bb_reg_state_size(bb_reg_state);
	bb_exit_state = debug_kmalloc(size, GFP_ATOMIC);
	if (!bb_exit_state) {
		kdb_printf("\n\n%s: out of debug_kmalloc\n", __FUNCTION__);
		bb_giveup = 1;
		return;
	}
	memcpy(bb_exit_state, bb_reg_state, size);
}

static int
bb_pass2_do_changed_blocks(int allow_missing)
{
	int i, j, missing, changed, maxloops;
	unsigned long addr;
	struct bb_jmp *bb_jmp;
	KDB_DEBUG_BB("\n  %s: allow_missing %d\n", __FUNCTION__, allow_missing);
	/* Absolute worst case is we have to iterate over all the basic blocks
	 * in an "out of order" state, each iteration losing one register or
	 * memory state.  Any more loops than that is a bug.  "out of order"
	 * means that the layout of blocks in memory does not match the logic
	 * flow through those blocks so (for example) block 27 comes before
	 * block 2.  To allow for out of order blocks, multiply maxloops by the
	 * number of blocks.
	 */
	maxloops = (KDB_INT_REGISTERS + bb_reg_state_max) * bb_count;
	changed = 1;
	do {
		changed = 0;
		for (i = 0; i < bb_count; ++i) {
			bb_curr = bb_list[i];
			if (!bb_curr->changed)
				continue;
			missing = 0;
			for (j = 0, bb_jmp = bb_jmp_list;
			     j < bb_jmp_count;
			     ++j, ++bb_jmp) {
				if (bb_jmp->to == bb_curr->start &&
				    !bb_jmp->state)
					++missing;
			}
			if (missing > allow_missing)
				continue;
			bb_curr->changed = 0;
			changed = 1;
			KDB_DEBUG_BB("\n  bb[%d]\n", i);
			bb_pass2_start_block(i);
			for (addr = bb_curr->start;
			     addr <= bb_curr->end; ) {
				bb_curr_addr = addr;
				if (addr == bb_exit_addr)
					bb_save_exit_state();
				addr += kdba_id_printinsn(addr, &kdb_di);
				kdb_di.fprintf_func(NULL, "\n");
				if (bb_giveup)
					goto done;
			}
			if (!bb_exit_state) {
				/* ATTRIB_NORET functions are a problem with
				 * the current gcc.  Allow the trailing address
				 * a bit of leaway.
				 */
				if (addr == bb_exit_addr ||
				    addr == bb_exit_addr + 1)
					bb_save_exit_state();
			}
			if (bb_curr->drop_through)
				bb_transfer(bb_curr->end,
					    bb_list[i+1]->start, 1);
		}
		if (maxloops-- == 0) {
			kdb_printf("\n\n%s maxloops reached\n",
				   __FUNCTION__);
			bb_giveup = 1;
			goto done;
		}
	} while(changed);
done:
	for (i = 0; i < bb_count; ++i) {
		bb_curr = bb_list[i];
		if (bb_curr->changed)
			return 1;	/* more to do, increase allow_missing */
	}
	return 0;	/* all blocks done */
}

/* Assume that the current function is a pass through function that does not
 * refer to its register parameters.  Exclude known asmlinkage functions and
 * assume the other functions actually use their registers.
 */

static void
bb_assume_pass_through(void)
{
	static int first_time = 1;
	if (strncmp(bb_func_name, "sys_", 4) == 0 ||
	    strncmp(bb_func_name, "compat_sys_", 11) == 0 ||
	    strcmp(bb_func_name, "schedule") == 0 ||
	    strcmp(bb_func_name, "do_softirq") == 0 ||
	    strcmp(bb_func_name, "printk") == 0 ||
	    strcmp(bb_func_name, "vprintk") == 0 ||
	    strcmp(bb_func_name, "preempt_schedule") == 0 ||
	    strcmp(bb_func_name, "start_kernel") == 0 ||
	    strcmp(bb_func_name, "csum_partial") == 0 ||
	    strcmp(bb_func_name, "csum_partial_copy_generic") == 0 ||
	    strcmp(bb_func_name, "math_state_restore") == 0 ||
	    strcmp(bb_func_name, "panic") == 0 ||
	    strcmp(bb_func_name, "kdb_printf") == 0 ||
	    strcmp(bb_func_name, "kdb_interrupt") == 0)
		return;
	if (bb_asmlinkage_arch())
		return;
	bb_reg_params = REGPARM;
	if (first_time) {
		kdb_printf("  %s has memory parameters but no register "
			   "parameters.\n  Assuming it is a 'pass "
			   "through' function that does not refer to "
			   "its register\n  parameters and setting %d "
			   "register parameters\n",
			   bb_func_name, REGPARM);
		first_time = 0;
		return;
	}
	kdb_printf("  Assuming %s is 'pass through' with %d register "
		   "parameters\n",
		   bb_func_name, REGPARM);
}

static void
bb_pass2(void)
{
	int allow_missing;
	if (KDB_DEBUG(BB) | KDB_DEBUG(BB_SUMM))
		kdb_printf("%s: start\n", __FUNCTION__);

	kdb_di.fprintf_func = bb_dis_pass2;
	kdb_di.print_address_func = bb_printaddr_pass2;

	bb_reg_state = debug_kmalloc(sizeof(*bb_reg_state), GFP_ATOMIC);
	if (!bb_reg_state) {
		kdb_printf("\n\n%s: out of debug_kmalloc\n", __FUNCTION__);
		bb_giveup = 1;
		return;
	}
	bb_list[0]->changed = 1;

	/* If a block does not have all its input states available then it is
	 * possible for a register to initially appear to hold a known value,
	 * but when other inputs are available then it becomes a variable
	 * value.  The initial false state of "known" can generate false values
	 * for other registers and can even make it look like stack locations
	 * are being changed.
	 *
	 * To avoid these false positives, only process blocks which have all
	 * their inputs defined.  That gives a clean depth first traversal of
	 * the tree, except for loops.  If there are any loops, then start
	 * processing blocks with one missing input, then two missing inputs
	 * etc.
	 *
	 * Absolute worst case is we have to iterate over all the jmp entries,
	 * each iteration allowing one more missing input.  Any more loops than
	 * that is a bug.  Watch out for the corner case of 0 jmp entries.
	 */
	for (allow_missing = 0; allow_missing <= bb_jmp_count; ++allow_missing) {
		if (!bb_pass2_do_changed_blocks(allow_missing))
			break;
		if (bb_giveup)
			break;
	}
	if (allow_missing > bb_jmp_count) {
		kdb_printf("\n\n%s maxloops reached\n",
			   __FUNCTION__);
		bb_giveup = 1;
		return;
	}

	if (bb_memory_params && bb_reg_params)
		bb_reg_params = REGPARM;
	if (REGPARM &&
	    bb_memory_params &&
	    !bb_reg_params)
		bb_assume_pass_through();
	if (KDB_DEBUG(BB) | KDB_DEBUG(BB_SUMM)) {
		kdb_printf("%s: end bb_reg_params %d bb_memory_params %d\n",
			   __FUNCTION__, bb_reg_params, bb_memory_params);
		if (bb_exit_state) {
			kdb_printf("%s: bb_exit_state at " kdb_bfd_vma_fmt0 "\n",
				   __FUNCTION__, bb_exit_addr);
			bb_do_reg_state_print(bb_exit_state);
		}
	}
}

static void
bb_cleanup(void)
{
	int i;
	struct bb* bb;
	struct bb_reg_state *state;
	while (bb_count) {
		bb = bb_list[0];
		bb_delete(0);
	}
	debug_kfree(bb_list);
	bb_list = NULL;
	bb_count = bb_max = 0;
	for (i = 0; i < bb_jmp_count; ++i) {
		state = bb_jmp_list[i].state;
		if (state && --state->ref_count == 0)
			debug_kfree(state);
	}
	debug_kfree(bb_jmp_list);
	bb_jmp_list = NULL;
	bb_jmp_count = bb_jmp_max = 0;
	debug_kfree(bb_reg_state);
	bb_reg_state = NULL;
	bb_reg_state_max = 0;
	debug_kfree(bb_exit_state);
	bb_exit_state = NULL;
	bb_reg_params = bb_memory_params = 0;
	bb_giveup = 0;
}

static int
bb_spurious_global_label(const char *func_name)
{
	int i;
	for (i = 0; i < ARRAY_SIZE(bb_spurious); ++i) {
		if (strcmp(bb_spurious[i], func_name) == 0)
			return 1;
	}
	return 0;
}

/* Given the current actual register contents plus the exit state deduced from
 * a basic block analysis of the current function, rollback the actual register
 * contents to the values they had on entry to this function.
 */

static void
bb_actual_rollback(const struct kdb_activation_record *ar)
{
	int i, offset_address;
	struct bb_memory_contains *c;
	enum bb_reg_code reg;
	unsigned long address, osp = 0;
	struct bb_actual new[ARRAY_SIZE(bb_actual)];


	if (!bb_exit_state) {
		kdb_printf("%s: no bb_exit_state, cannot rollback\n",
			   __FUNCTION__);
		bb_giveup = 1;
		return;
	}
	memcpy(bb_reg_state, bb_exit_state, bb_reg_state_size(bb_exit_state));
	memset(new, 0, sizeof(new));

	/* The most important register for obtaining saved state is rsp so get
	 * its new value first.  Prefer rsp if it is valid, then other
	 * registers.  Saved values of rsp in memory are unusable without a
	 * register that points to memory.
	 */
	if (!bb_actual_valid(BBRG_RSP)) {
		kdb_printf("%s: no starting value for RSP, cannot rollback\n",
			   __FUNCTION__);
		bb_giveup = 1;
		return;
	}
	if (KDB_DEBUG(BB) | KDB_DEBUG(BB_SUMM))
		kdb_printf("%s: rsp " kdb_bfd_vma_fmt0,
			   __FUNCTION__, bb_actual_value(BBRG_RSP));
	i = BBRG_RSP;
	if (!bb_is_osp_defined(i)) {
	       	for (i = BBRG_RAX; i < BBRG_RAX + KDB_INT_REGISTERS; ++i) {
			if (bb_is_osp_defined(i) && bb_actual_valid(i))
				break;
		}
	}
	if (bb_is_osp_defined(i) && bb_actual_valid(i)) {
		osp = new[BBRG_RSP - BBRG_RAX].value =
		      bb_actual_value(i) - bb_reg_code_offset(i);
		new[BBRG_RSP - BBRG_RAX].valid = 1;
		if (KDB_DEBUG(BB) | KDB_DEBUG(BB_SUMM))
			kdb_printf(" -> osp " kdb_bfd_vma_fmt0 "\n", osp);
	} else {
		bb_actual_set_valid(BBRG_RSP, 0);
		if (KDB_DEBUG(BB) | KDB_DEBUG(BB_SUMM))
			kdb_printf(" -> undefined\n");
		kdb_printf("%s: no ending value for RSP, cannot rollback\n",
			   __FUNCTION__);
		bb_giveup = 1;
		return;
	}

	/* Now the other registers.  First look at register values that have
	 * been copied to other registers.
	 */
	for (i = BBRG_RAX; i < BBRG_RAX + KDB_INT_REGISTERS; ++i) {
		reg = bb_reg_code_value(i);
		if (bb_is_int_reg(reg)) {
			new[reg - BBRG_RAX] = bb_actual[i - BBRG_RAX];
			if (KDB_DEBUG(BB) | KDB_DEBUG(BB_SUMM)) {
				kdb_printf("%s: %s is in %s ",
					    __FUNCTION__,
					    bbrg_name[reg],
					    bbrg_name[i]);
				if (bb_actual_valid(i))
					kdb_printf(" -> " kdb_bfd_vma_fmt0 "\n",
						    bb_actual_value(i));
				else
					kdb_printf("(invalid)\n");
			}
		}
	}

	/* Finally register values that have been saved on stack */
	for (i = 0, c = bb_reg_state->memory;
	     i < bb_reg_state->mem_count;
	     ++i, ++c) {
		offset_address = c->offset_address;
		reg = c->value;
		if (!bb_is_int_reg(reg))
			continue;
		address = osp + offset_address;
		if (address < ar->stack.logical_start ||
		    address >= ar->stack.logical_end) {
			new[reg - BBRG_RAX].value = 0;
			new[reg - BBRG_RAX].valid = 0;
			if (KDB_DEBUG(BB) | KDB_DEBUG(BB_SUMM))
				kdb_printf("%s: %s -> undefined\n",
					   __FUNCTION__,
					   bbrg_name[reg]);
		} else {
			if (KDB_DEBUG(BB) | KDB_DEBUG(BB_SUMM)) {
				kdb_printf("%s: %s -> *(osp",
					   __FUNCTION__,
					   bbrg_name[reg]);
				KDB_DEBUG_BB_OFFSET_PRINTF(offset_address, "", " ");
				kdb_printf(kdb_bfd_vma_fmt0, address);
			}
			new[reg - BBRG_RAX].value = *(bfd_vma *)address;
			new[reg - BBRG_RAX].valid = 1;
			if (KDB_DEBUG(BB) | KDB_DEBUG(BB_SUMM))
				kdb_printf(") = " kdb_bfd_vma_fmt0 "\n",
					   new[reg - BBRG_RAX].value);
		}
	}

	memcpy(bb_actual, new, sizeof(bb_actual));
}

/* Return true if the current function is an interrupt handler */

static bool
bb_interrupt_handler(kdb_machreg_t rip)
{
	unsigned long disp8, disp32, target, addr = (unsigned long)rip;
	unsigned char code[5];
	int i;

	for (i = 0; i < ARRAY_SIZE(bb_hardware_handlers); ++i)
		if (strcmp(bb_func_name, bb_hardware_handlers[i]) == 0)
			return 1;

	/* Given the large number of interrupt handlers, it is easiest to look
	 * at the next instruction and see if it is a jmp to the common exit
	 * routines.
	 */
	if (kdb_getarea(code, addr) ||
	    kdb_getword(&disp32, addr+1, 4) ||
	    kdb_getword(&disp8, addr+1, 1))
		return 0;	/* not a valid code address */
	if (code[0] == 0xe9) {
		target = addr + (s32) disp32 + 5;	/* jmp disp32 */
		if (target == bb_ret_from_intr ||
		    target == bb_common_interrupt ||
		    target == bb_error_entry)
			return 1;
	}
	if (code[0] == 0xeb) {
		target = addr + (s8) disp8 + 2;		/* jmp disp8 */
		if (target == bb_ret_from_intr ||
		    target == bb_common_interrupt ||
		    target == bb_error_entry)
			return 1;
	}

	return 0;
}

/* Copy argument information that was deduced by the basic block analysis and
 * rollback into the kdb stack activation record.
 */

static void
bb_arguments(struct kdb_activation_record *ar)
{
	int i;
	enum bb_reg_code reg;
	kdb_machreg_t rsp;
	ar->args = bb_reg_params + bb_memory_params;
	bitmap_zero(ar->valid.bits, KDBA_MAXARGS);
	for (i = 0; i < bb_reg_params; ++i) {
		reg = bb_param_reg[i];
		if (bb_actual_valid(reg)) {
			ar->arg[i] = bb_actual_value(reg);
			set_bit(i, ar->valid.bits);
		}
	}
	if (!bb_actual_valid(BBRG_RSP))
		return;
	rsp = bb_actual_value(BBRG_RSP);
	for (i = bb_reg_params; i < ar->args; ++i) {
		rsp += KDB_WORD_SIZE;
		if (kdb_getarea(ar->arg[i], rsp) == 0)
			set_bit(i, ar->valid.bits);
	}
}

/* Given an exit address from a function, decompose the entire function into
 * basic blocks and determine the register state at the exit point.
 */

static void
kdb_bb(unsigned long exit)
{
	kdb_symtab_t symtab;
	if (!kdbnearsym(exit, &symtab)) {
		kdb_printf("%s: address " kdb_bfd_vma_fmt0 " not recognised\n",
			   __FUNCTION__, exit);
		bb_giveup = 1;
		return;
	}
	bb_exit_addr = exit;
	bb_mod_name = symtab.mod_name;
	bb_func_name = symtab.sym_name;
	bb_func_start = symtab.sym_start;
	bb_func_end = symtab.sym_end;
	/* Various global labels exist in the middle of assembler code and have
	 * a non-standard state.  Ignore these labels and use the start of the
	 * previous label instead.
	 */
	while (bb_spurious_global_label(symtab.sym_name)) {
		if (!kdbnearsym(symtab.sym_start - 1, &symtab))
			break;
		bb_func_start = symtab.sym_start;
	}
	bb_mod_name = symtab.mod_name;
	bb_func_name = symtab.sym_name;
	bb_func_start = symtab.sym_start;
	/* Ignore spurious labels past this point and use the next non-spurious
	 * label as the end point.
	 */
	if (kdbnearsym(bb_func_end, &symtab)) {
		while (bb_spurious_global_label(symtab.sym_name)) {
			bb_func_end = symtab.sym_end;
			if (!kdbnearsym(symtab.sym_end + 1, &symtab))
				break;
		}
	}
	bb_pass1();
	if (!bb_giveup)
		bb_pass2();
	if (bb_giveup)
		kdb_printf("%s: " kdb_bfd_vma_fmt0
			   " [%s]%s failed at " kdb_bfd_vma_fmt0 "\n\n",
			   __FUNCTION__, exit,
			   bb_mod_name, bb_func_name, bb_curr_addr);
}

static int
kdb_bb1(int argc, const char **argv)
{
	int diag;
	unsigned long addr;
	bb_cleanup();	/* in case previous command was interrupted */
	kdba_id_init(&kdb_di);
	if (argc != 1)
		return KDB_ARGCOUNT;
	if ((diag = kdbgetularg((char *)argv[1], &addr)))
		return diag;
	kdb_save_flags();
	kdb_flags |= KDB_DEBUG_FLAG_BB << KDB_DEBUG_FLAG_SHIFT;
	kdb_bb(addr);
	bb_cleanup();
	kdb_restore_flags();
	kdbnearsym_cleanup();
	return 0;
}

/* Run a basic block analysis on every function in the base kernel.  Used as a
 * global sanity check to find errors in the basic block code.
 */

static int
kdb_bb_all(int argc, const char **argv)
{
	loff_t pos = 0;
	const char *symname;
	unsigned long addr;
	int i, max_errors = 20;
	struct bb_name_state *r;
	kdb_printf("%s: build variables:"
		   " CCVERSION \"" __stringify(CCVERSION) "\""
#ifdef	CONFIG_X86_64
		   " CONFIG_X86_64"
#endif
#ifdef	CONFIG_4KSTACKS
		   " CONFIG_4KSTACKS"
#endif
#ifdef	CONFIG_PREEMPT
		   " CONFIG_PREEMPT"
#endif
#ifdef	CONFIG_VM86
		   " CONFIG_VM86"
#endif
#ifdef	CONFIG_FRAME_POINTER
		   " CONFIG_FRAME_POINTER"
#endif
#ifdef	CONFIG_TRACE_IRQFLAGS
		   " CONFIG_TRACE_IRQFLAGS"
#endif
#ifdef	CONFIG_HIBERNATION
		   " CONFIG_HIBERNATION"
#endif
#ifdef	CONFIG_KPROBES
		   " CONFIG_KPROBES"
#endif
#ifdef	CONFIG_KEXEC
		   " CONFIG_KEXEC"
#endif
#ifdef	CONFIG_MATH_EMULATION
		   " CONFIG_MATH_EMULATION"
#endif
#ifdef	CONFIG_XEN
		   " CONFIG_XEN"
#endif
#ifdef	CONFIG_DEBUG_INFO
		   " CONFIG_DEBUG_INFO"
#endif
#ifdef	NO_SIBLINGS
		   " NO_SIBLINGS"
#endif
		   " REGPARM=" __stringify(REGPARM)
		   "\n\n", __FUNCTION__);
	for (i = 0, r = bb_special_cases;
	     i < ARRAY_SIZE(bb_special_cases);
	     ++i, ++r) {
		if (!r->address)
			kdb_printf("%s: cannot find special_case name %s\n",
				   __FUNCTION__, r->name);
	}
	for (i = 0; i < ARRAY_SIZE(bb_spurious); ++i) {
		if (!kallsyms_lookup_name(bb_spurious[i]))
			kdb_printf("%s: cannot find spurious label %s\n",
				   __FUNCTION__, bb_spurious[i]);
	}
	while ((symname = kdb_walk_kallsyms(&pos))) {
		if (strcmp(symname, "_stext") == 0 ||
		    strcmp(symname, "stext") == 0)
			break;
	}
	if (!symname) {
		kdb_printf("%s: cannot find _stext\n", __FUNCTION__);
		return 0;
	}
	kdba_id_init(&kdb_di);
	i = 0;
	while ((symname = kdb_walk_kallsyms(&pos))) {
		if (strcmp(symname, "_etext") == 0)
			break;
		if (i++ % 100 == 0)
			kdb_printf(".");
		/* x86_64 has some 16 bit functions that appear between stext
		 * and _etext.  Skip them.
		 */
		if (strcmp(symname, "verify_cpu") == 0 ||
		    strcmp(symname, "verify_cpu_noamd") == 0 ||
		    strcmp(symname, "verify_cpu_sse_test") == 0 ||
		    strcmp(symname, "verify_cpu_no_longmode") == 0 ||
		    strcmp(symname, "verify_cpu_sse_ok") == 0 ||
		    strcmp(symname, "mode_seta") == 0 ||
		    strcmp(symname, "bad_address") == 0 ||
		    strcmp(symname, "wakeup_code") == 0 ||
		    strcmp(symname, "wakeup_code_start") == 0 ||
		    strcmp(symname, "wakeup_start") == 0 ||
		    strcmp(symname, "wakeup_32_vector") == 0 ||
		    strcmp(symname, "wakeup_32") == 0 ||
		    strcmp(symname, "wakeup_long64_vector") == 0 ||
		    strcmp(symname, "wakeup_long64") == 0 ||
		    strcmp(symname, "gdta") == 0 ||
		    strcmp(symname, "idt_48a") == 0 ||
		    strcmp(symname, "gdt_48a") == 0 ||
		    strcmp(symname, "bogus_real_magic") == 0 ||
		    strcmp(symname, "bogus_64_magic") == 0 ||
		    strcmp(symname, "no_longmode") == 0 ||
		    strcmp(symname, "mode_set") == 0 ||
		    strcmp(symname, "mode_seta") == 0 ||
		    strcmp(symname, "setbada") == 0 ||
		    strcmp(symname, "check_vesa") == 0 ||
		    strcmp(symname, "check_vesaa") == 0 ||
		    strcmp(symname, "_setbada") == 0 ||
		    strcmp(symname, "wakeup_stack_begin") == 0 ||
		    strcmp(symname, "wakeup_stack") == 0 ||
		    strcmp(symname, "wakeup_level4_pgt") == 0 ||
		    strcmp(symname, "acpi_copy_wakeup_routine") == 0 ||
		    strcmp(symname, "wakeup_end") == 0 ||
		    strcmp(symname, "do_suspend_lowlevel_s4bios") == 0 ||
		    strcmp(symname, "do_suspend_lowlevel") == 0 ||
		    strcmp(symname, "wakeup_pmode_return") == 0 ||
		    strcmp(symname, "restore_registers") == 0)
			continue;
		/* __kprobes_text_end contains branches to the middle of code,
		 * with undefined states.
		 */
		if (strcmp(symname, "__kprobes_text_end") == 0)
			continue;
		/* Data in the middle of the text segment :( */
		if (strcmp(symname, "level2_kernel_pgt") == 0 ||
		    strcmp(symname, "level3_kernel_pgt") == 0)
			continue;
		if (bb_spurious_global_label(symname))
			continue;
		if ((addr = kallsyms_lookup_name(symname)) == 0)
			continue;
		// kdb_printf("BB " kdb_bfd_vma_fmt0 " %s\n", addr, symname);
		bb_cleanup();	/* in case previous command was interrupted */
		kdbnearsym_cleanup();
		kdb_bb(addr);
		touch_nmi_watchdog();
		if (bb_giveup) {
			if (max_errors-- == 0) {
				kdb_printf("%s: max_errors reached, giving up\n",
					   __FUNCTION__);
				break;
			} else {
				bb_giveup = 0;
			}
		}
	}
	kdb_printf("\n");
	bb_cleanup();
	kdbnearsym_cleanup();
	return 0;
}

/*
 *=============================================================================
 *
 * Everything above this line is doing basic block analysis, function by
 * function.  Everything below this line uses the basic block data to do a
 * complete backtrace over all functions that are used by a process.
 *
 *=============================================================================
 */


/*============================================================================*/
/*                                                                            */
/* Most of the backtrace code and data is common to x86_64 and i386.  This    */
/* large ifdef contains all of the differences between the two architectures. */
/*                                                                            */
/* Make sure you update the correct section of this ifdef.                    */
/*                                                                            */
/*============================================================================*/
#define XCS "cs"
#define RSP "sp"
#define RIP "ip"
#define ARCH_RSP sp
#define ARCH_RIP ip

#ifdef	CONFIG_X86_64

#define ARCH_NORMAL_PADDING (16 * 8)

/* x86_64 has multiple alternate stacks, with different sizes and different
 * offsets to get the link from one stack to the next.  Some of the stacks are
 * referenced via cpu_pda, some via per_cpu orig_ist.  Debug events can even
 * have multiple nested stacks within the single physical stack, each nested
 * stack has its own link and some of those links are wrong.
 *
 * Consistent it's not!
 *
 * Do not assume that these stacks are aligned on their size.
 */
#define INTERRUPT_STACK (N_EXCEPTION_STACKS + 1)
void
kdba_get_stack_info_alternate(kdb_machreg_t addr, int cpu,
			      struct kdb_activation_record *ar)
{
	static struct {
		const char *id;
		unsigned int total_size;
		unsigned int nested_size;
		unsigned int next;
	} *sdp, stack_data[] = {
		[STACKFAULT_STACK - 1] =  { "stackfault",    EXCEPTION_STKSZ, EXCEPTION_STKSZ, EXCEPTION_STKSZ - 2*sizeof(void *) },
		[DOUBLEFAULT_STACK - 1] = { "doublefault",   EXCEPTION_STKSZ, EXCEPTION_STKSZ, EXCEPTION_STKSZ - 2*sizeof(void *) },
		[NMI_STACK - 1] =         { "nmi",           EXCEPTION_STKSZ, EXCEPTION_STKSZ, EXCEPTION_STKSZ - 2*sizeof(void *) },
		[DEBUG_STACK - 1] =       { "debug",         DEBUG_STKSZ,     EXCEPTION_STKSZ, EXCEPTION_STKSZ - 2*sizeof(void *) },
		[MCE_STACK - 1] =         { "machine check", EXCEPTION_STKSZ, EXCEPTION_STKSZ, EXCEPTION_STKSZ - 2*sizeof(void *) },
		[INTERRUPT_STACK - 1] =   { "interrupt",     IRQSTACKSIZE,    IRQSTACKSIZE,    IRQSTACKSIZE    -   sizeof(void *) },
	};
	unsigned long total_start = 0, total_size, total_end;
	int sd, found = 0;
	extern unsigned long kdba_orig_ist(int, int);

	for (sd = 0, sdp = stack_data;
	     sd < ARRAY_SIZE(stack_data);
	     ++sd, ++sdp) {
		total_size = sdp->total_size;
		if (!total_size)
			continue;	/* in case stack_data[] has any holes */
		if (cpu < 0) {
			/* Arbitrary address which can be on any cpu, see if it
			 * falls within any of the alternate stacks
			 */
			int c;
			for_each_online_cpu(c) {
				if (sd == INTERRUPT_STACK - 1)
					total_end = (unsigned long)cpu_pda(c)->irqstackptr;
				else
					total_end = per_cpu(orig_ist, c).ist[sd];
				total_start = total_end - total_size;
				if (addr >= total_start && addr < total_end) {
					found = 1;
					cpu = c;
					break;
				}
			}
			if (!found)
				continue;
		}
		/* Only check the supplied or found cpu */
		if (sd == INTERRUPT_STACK - 1)
			total_end = (unsigned long)cpu_pda(cpu)->irqstackptr;
		else
			total_end = per_cpu(orig_ist, cpu).ist[sd];
		total_start = total_end - total_size;
		if (addr >= total_start && addr < total_end) {
			found = 1;
			break;
		}
	}
	if (!found)
		return;
	/* find which nested stack the address is in */
	while (addr > total_start + sdp->nested_size)
		total_start += sdp->nested_size;
	ar->stack.physical_start = total_start;
	ar->stack.physical_end = total_start + sdp->nested_size;
	ar->stack.logical_start = total_start;
	ar->stack.logical_end = total_start + sdp->next;
	ar->stack.next = *(unsigned long *)ar->stack.logical_end;
	ar->stack.id = sdp->id;

	/* Nasty: when switching to the interrupt stack, the stack state of the
	 * caller is split over two stacks, the original stack and the
	 * interrupt stack.  One word (the previous frame pointer) is stored on
	 * the interrupt stack, the rest of the interrupt data is in the old
	 * frame.  To make the interrupted stack state look as though it is
	 * contiguous, copy the missing word from the interrupt stack to the
	 * original stack and adjust the new stack pointer accordingly.
	 */

	if (sd == INTERRUPT_STACK - 1) {
		*(unsigned long *)(ar->stack.next - KDB_WORD_SIZE) =
			ar->stack.next;
		ar->stack.next -= KDB_WORD_SIZE;
	}
}

/* rip is not in the thread struct for x86_64.  We know that the stack value
 * was saved in schedule near the label thread_return.  Setting rip to
 * thread_return lets the stack trace find that we are in schedule and
 * correctly decode its prologue.
 */

static kdb_machreg_t
kdba_bt_stack_rip(const struct task_struct *p)
{
	return bb_thread_return;
}

#else	/* !CONFIG_X86_64 */

#define ARCH_NORMAL_PADDING (19 * 4)

#ifdef	CONFIG_4KSTACKS
static struct thread_info **kdba_hardirq_ctx, **kdba_softirq_ctx;
#endif	/* CONFIG_4KSTACKS */

/* On a 4K stack kernel, hardirq_ctx and softirq_ctx are [NR_CPUS] arrays.  The
 * first element of each per-cpu stack is a struct thread_info.
 */
void
kdba_get_stack_info_alternate(kdb_machreg_t addr, int cpu,
			      struct kdb_activation_record *ar)
{
#ifdef	CONFIG_4KSTACKS
	struct thread_info *tinfo;
	tinfo = (struct thread_info *)(addr & -THREAD_SIZE);
	if (cpu < 0) {
		/* Arbitrary address, see if it falls within any of the irq
		 * stacks
		 */
		int found = 0;
		for_each_online_cpu(cpu) {
			if (tinfo == kdba_hardirq_ctx[cpu] ||
			    tinfo == kdba_softirq_ctx[cpu]) {
				found = 1;
				break;
			}
		}
		if (!found)
			return;
	}
	if (tinfo == kdba_hardirq_ctx[cpu] ||
	    tinfo == kdba_softirq_ctx[cpu]) {
		ar->stack.physical_start = (kdb_machreg_t)tinfo;
		ar->stack.physical_end = ar->stack.physical_start + THREAD_SIZE;
		ar->stack.logical_start = ar->stack.physical_start +
					  sizeof(struct thread_info);
		ar->stack.logical_end = ar->stack.physical_end;
		ar->stack.next = tinfo->previous_esp;
		if (tinfo == kdba_hardirq_ctx[cpu])
			ar->stack.id = "hardirq_ctx";
		else
			ar->stack.id = "softirq_ctx";
	}
#endif	/* CONFIG_4KSTACKS */
}

/* rip is in the thread struct for i386 */

static kdb_machreg_t
kdba_bt_stack_rip(const struct task_struct *p)
{
	return p->thread.ip;
}

#endif	/* CONFIG_X86_64 */

/* Given an address which claims to be on a stack, an optional cpu number and
 * an optional task address, get information about the stack.
 *
 * t == NULL, cpu < 0 indicates an arbitrary stack address with no associated
 * struct task, the address can be in an alternate stack or any task's normal
 * stack.
 *
 * t != NULL, cpu >= 0 indicates a running task, the address can be in an
 * alternate stack or that task's normal stack.
 *
 * t != NULL, cpu < 0 indicates a blocked task, the address can only be in that
 * task's normal stack.
 *
 * t == NULL, cpu >= 0 is not a valid combination.
 */

static void
kdba_get_stack_info(kdb_machreg_t rsp, int cpu,
		    struct kdb_activation_record *ar,
		    const struct task_struct *t)
{
	struct thread_info *tinfo;
	struct task_struct *g, *p;
	memset(&ar->stack, 0, sizeof(ar->stack));
	if (KDB_DEBUG(ARA))
		kdb_printf("%s: " RSP "=0x%lx cpu=%d task=%p\n",
			   __FUNCTION__, rsp, cpu, t);
	if (t == NULL || cpu >= 0) {
		kdba_get_stack_info_alternate(rsp, cpu, ar);
		if (ar->stack.logical_start)
			goto out;
	}
	rsp &= -THREAD_SIZE;
	tinfo = (struct thread_info *)rsp;
	if (t == NULL) {
		/* Arbitrary stack address without an associated task, see if
		 * it falls within any normal process stack, including the idle
		 * tasks.
		 */
		kdb_do_each_thread(g, p) {
			if (tinfo == task_thread_info(p)) {
				t = p;
				goto found;
			}
		} kdb_while_each_thread(g, p);
		for_each_online_cpu(cpu) {
			p = idle_task(cpu);
			if (tinfo == task_thread_info(p)) {
				t = p;
				goto found;
			}
		}
	found:
		if (KDB_DEBUG(ARA))
			kdb_printf("%s: found task %p\n", __FUNCTION__, t);
	} else if (cpu >= 0) {
		/* running task */
		struct kdb_running_process *krp = kdb_running_process + cpu;
		if (krp->p != t || tinfo != task_thread_info(t))
			t = NULL;
		if (KDB_DEBUG(ARA))
			kdb_printf("%s: running task %p\n", __FUNCTION__, t);
	} else {
		/* blocked task */
		if (tinfo != task_thread_info(t))
			t = NULL;
		if (KDB_DEBUG(ARA))
			kdb_printf("%s: blocked task %p\n", __FUNCTION__, t);
	}
	if (t) {
		ar->stack.physical_start = rsp;
		ar->stack.physical_end = rsp + THREAD_SIZE;
		ar->stack.logical_start = rsp + sizeof(struct thread_info);
		ar->stack.logical_end = ar->stack.physical_end - ARCH_NORMAL_PADDING;
		ar->stack.next = 0;
		ar->stack.id = "normal";
	}
out:
	if (ar->stack.physical_start && KDB_DEBUG(ARA)) {
		kdb_printf("%s: ar->stack\n", __FUNCTION__);
		kdb_printf("    physical_start=0x%lx\n", ar->stack.physical_start);
		kdb_printf("    physical_end=0x%lx\n", ar->stack.physical_end);
		kdb_printf("    logical_start=0x%lx\n", ar->stack.logical_start);
		kdb_printf("    logical_end=0x%lx\n", ar->stack.logical_end);
		kdb_printf("    next=0x%lx\n", ar->stack.next);
		kdb_printf("    id=%s\n", ar->stack.id);
		kdb_printf("    set MDCOUNT %ld\n",
			   (ar->stack.physical_end - ar->stack.physical_start) /
			   KDB_WORD_SIZE);
		kdb_printf("    mds " kdb_machreg_fmt0 "\n",
			   ar->stack.physical_start);
	}
}

static void
bt_print_one(kdb_machreg_t rip, kdb_machreg_t rsp,
	      const struct kdb_activation_record *ar,
	      const kdb_symtab_t *symtab, int argcount)
{
	int btsymarg = 0;
	int nosect = 0;

	kdbgetintenv("BTSYMARG", &btsymarg);
	kdbgetintenv("NOSECT", &nosect);

	kdb_printf(kdb_machreg_fmt0, rsp);
	kdb_symbol_print(rip, symtab,
			 KDB_SP_SPACEB|KDB_SP_VALUE);
	if (argcount && ar->args) {
		int i, argc = ar->args;
		kdb_printf(" (");
		if (argc > argcount)
			argc = argcount;
		for (i = 0; i < argc; i++) {
			if (i)
				kdb_printf(", ");
			if (test_bit(i, ar->valid.bits))
				kdb_printf("0x%lx", ar->arg[i]);
			else
				kdb_printf("invalid");
		}
		kdb_printf(")");
	}
	kdb_printf("\n");
	if (symtab->sym_name) {
		if (!nosect) {
			kdb_printf("                               %s",
				   symtab->mod_name);
			if (symtab->sec_name && symtab->sec_start)
				kdb_printf(" 0x%lx 0x%lx",
					   symtab->sec_start, symtab->sec_end);
			kdb_printf(" 0x%lx 0x%lx\n",
				   symtab->sym_start, symtab->sym_end);
		}
	}
	if (argcount && ar->args && btsymarg) {
		int i, argc = ar->args;
		kdb_symtab_t arg_symtab;
		for (i = 0; i < argc; i++) {
			kdb_machreg_t arg = ar->arg[i];
			if (test_bit(i, ar->valid.bits) &&
			    kdbnearsym(arg, &arg_symtab)) {
				kdb_printf("                       ARG %2d ", i);
				kdb_symbol_print(arg, &arg_symtab,
						 KDB_SP_DEFAULT|KDB_SP_NEWLINE);
			}
		}
	}
}

static void
kdba_bt_new_stack(struct kdb_activation_record *ar, kdb_machreg_t *rsp,
		   int *count, int *suppress)
{
	/* Nasty: common_interrupt builds a partial pt_regs, with r15 through
	 * rbx not being filled in.  It passes struct pt_regs* to do_IRQ (in
	 * rdi) but the stack pointer is not adjusted to account for r15
	 * through rbx.  This has two effects :-
	 *
	 * (1) struct pt_regs on an external interrupt actually overlaps with
	 *     the local stack area used by do_IRQ.  Not only are r15-rbx
	 *     undefined, the area that claims to hold their values can even
	 *     change as the irq is processed.
	 *
	 * (2) The back stack pointer saved for the new frame is not pointing
	 *     at pt_regs, it is pointing at rbx within the pt_regs passed to
	 *     do_IRQ.
	 *
	 * There is nothing that I can do about (1) but I have to fix (2)
	 * because kdb backtrace looks for the "start" address of pt_regs as it
	 * walks back through the stacks.  When switching from the interrupt
	 * stack to another stack, we have to assume that pt_regs has been
	 * seen and turn off backtrace supression.
	 */
	int probable_pt_regs = strcmp(ar->stack.id, "interrupt") == 0;
	*rsp = ar->stack.next;
	if (KDB_DEBUG(ARA))
		kdb_printf("new " RSP "=" kdb_machreg_fmt0 "\n", *rsp);
	bb_actual_set_value(BBRG_RSP, *rsp);
	kdba_get_stack_info(*rsp, -1, ar, NULL);
	if (!ar->stack.physical_start) {
		kdb_printf("+++ Cannot resolve next stack\n");
	} else if (!*suppress) {
		kdb_printf(" ======================= <%s>\n",
			   ar->stack.id);
		++*count;
	}
	if (probable_pt_regs)
		*suppress = 0;
}

/*
 * kdba_bt_stack
 *
 * Inputs:
 *	addr	Address provided to 'bt' command, if any.
 *	argcount
 *	p	Pointer to task for 'btp' command.
 * Outputs:
 *	None.
 * Returns:
 *	zero for success, a kdb diagnostic if error
 * Locking:
 *	none.
 * Remarks:
 *	Ultimately all the bt* commands come through this routine.  If
 *	old_style is 0 then it uses the basic block analysis to get an accurate
 *	backtrace with arguments, otherwise it falls back to the old method of
 *	printing anything on stack that looks like a kernel address.
 *
 *	Allowing for the stack data pushed by the hardware is tricky.  We
 *	deduce the presence of hardware pushed data by looking for interrupt
 *	handlers, either by name or by the code that they contain.  This
 *	information must be applied to the next function up the stack, because
 *	the hardware data is above the saved rip for the interrupted (next)
 *	function.
 *
 *	To make things worse, the amount of data pushed is arch specific and
 *	may depend on the rsp for the next function, not the current function.
 *	The number of bytes pushed by hardware cannot be calculated until we
 *	are actually processing the stack for the interrupted function and have
 *	its rsp.
 *
 *	It is also possible for an interrupt to occur in user space and for the
 *	interrupt handler to also be interrupted.  Check the code selector
 *	whenever the previous function is an interrupt handler and stop
 *	backtracing if the interrupt was not in kernel space.
 */

static int
kdba_bt_stack(kdb_machreg_t addr, int argcount, const struct task_struct *p,
	       int old_style)
{
	struct kdb_activation_record ar;
	kdb_machreg_t rip = 0, rsp = 0, prev_rsp, cs;
	kdb_symtab_t symtab;
	int rip_at_rsp = 0, count = 0, btsp = 0, suppress,
	    interrupt_handler = 0, prev_interrupt_handler = 0, hardware_pushed,
	    prev_noret = 0;
	struct pt_regs *regs = NULL;

	kdbgetintenv("BTSP", &btsp);
	suppress = !btsp;
	memset(&ar, 0, sizeof(ar));
	if (old_style)
		kdb_printf("Using old style backtrace, unreliable with no arguments\n");

	/*
	 * The caller may have supplied an address at which the stack traceback
	 * operation should begin.  This address is assumed by this code to
	 * point to a return address on the stack to be traced back.
	 *
	 * Warning: type in the wrong address and you will get garbage in the
	 * backtrace.
	 */
	if (addr) {
		rsp = addr;
		kdb_getword(&rip, rsp, sizeof(rip));
		rip_at_rsp = 1;
		suppress = 0;
		kdba_get_stack_info(rsp, -1, &ar, NULL);
	} else {
		if (task_curr(p)) {
			struct kdb_running_process *krp =
			    kdb_running_process + task_cpu(p);
			kdb_machreg_t cs;
			regs = krp->regs;
			if (krp->seqno &&
			    krp->p == p &&
			    krp->seqno >= kdb_seqno - 1 &&
			    !KDB_NULL_REGS(regs)) {
				/* valid saved state, continue processing */
			} else {
				kdb_printf
				    ("Process did not save state, cannot backtrace\n");
				kdb_ps1(p);
				return 0;
			}
			kdba_getregcontents(XCS, regs, &cs);
			if ((cs & 0xffff) != __KERNEL_CS) {
				kdb_printf("Stack is not in kernel space, backtrace not available\n");
				return 0;
			}
			rip = krp->arch.ARCH_RIP;
			rsp = krp->arch.ARCH_RSP;
			kdba_get_stack_info(rsp, kdb_process_cpu(p), &ar, p);
		} else {
			/* Not on cpu, assume blocked.  Blocked tasks do not
			 * have pt_regs.  p->thread contains some data, alas
			 * what it contains differs between i386 and x86_64.
			 */
			rip = kdba_bt_stack_rip(p);
			rsp = p->thread.sp;
			suppress = 0;
			kdba_get_stack_info(rsp, -1, &ar, p);
		}
	}
	if (!ar.stack.physical_start) {
		kdb_printf(RSP "=0x%lx is not in a valid kernel stack, backtrace not available\n",
			   rsp);
		return 0;
	}
	memset(&bb_actual, 0, sizeof(bb_actual));
	bb_actual_set_value(BBRG_RSP, rsp);
	bb_actual_set_valid(BBRG_RSP, 1);

	kdb_printf(RSP "%*s" RIP "%*sFunction (args)\n",
		   2*KDB_WORD_SIZE, " ",
		   2*KDB_WORD_SIZE, " ");
	if (ar.stack.next && !suppress)
		kdb_printf(" ======================= <%s>\n",
			   ar.stack.id);

	bb_cleanup();
	/* Run through all the stacks */
	while (ar.stack.physical_start) {
		if (rip_at_rsp) {
			rip = *(kdb_machreg_t *)rsp;
			/* I wish that gcc was fixed to include a nop
			 * instruction after ATTRIB_NORET functions.  The lack
			 * of a nop means that the return address points to the
			 * start of next function, so fudge it to point to one
			 * byte previous.
			 *
			 * No, we cannot just decrement all rip values.
			 * Sometimes an rip legally points to the start of a
			 * function, e.g. interrupted code or hand crafted
			 * assembler.
			 */
			if (prev_noret) {
				kdbnearsym(rip, &symtab);
				if (rip == symtab.sym_start) {
					--rip;
					if (KDB_DEBUG(ARA))
						kdb_printf("\tprev_noret, " RIP
							   "=0x%lx\n", rip);
				}
			}
		}
		kdbnearsym(rip, &symtab);
		if (old_style) {
		       	if (__kernel_text_address(rip) && !suppress) {
				bt_print_one(rip, rsp, &ar, &symtab, 0);
				++count;
			}
			if (rsp == (unsigned long)regs) {
				if (ar.stack.next && suppress)
					kdb_printf(" ======================= <%s>\n",
						   ar.stack.id);
				++count;
				suppress = 0;
			}
			rsp += sizeof(rip);
			rip_at_rsp = 1;
			if (rsp >= ar.stack.logical_end) {
				if (!ar.stack.next)
					break;
				kdba_bt_new_stack(&ar, &rsp, &count, &suppress);
				rip_at_rsp = 0;
				continue;
			}
		} else {
			/* Start each analysis with no dynamic data from the
			 * previous kdb_bb() run.
			 */
			bb_cleanup();
			kdb_bb(rip);
			if (bb_giveup)
				break;
			prev_interrupt_handler = interrupt_handler;
			interrupt_handler = bb_interrupt_handler(rip);
			prev_rsp = rsp;
			if (rip_at_rsp) {
				if (prev_interrupt_handler) {
					cs = *((kdb_machreg_t *)rsp + 1) & 0xffff;
					hardware_pushed =
						bb_hardware_pushed_arch(rsp, &ar);
				} else {
					cs = __KERNEL_CS;
					hardware_pushed = 0;
				}
				rsp += sizeof(rip) + hardware_pushed;
				if (KDB_DEBUG(ARA))
					kdb_printf("%s: " RSP " "
						   kdb_machreg_fmt0
						   " -> " kdb_machreg_fmt0
						   " hardware_pushed %d"
						   " prev_interrupt_handler %d"
						   " cs 0x%lx\n",
						   __FUNCTION__,
						   prev_rsp,
						   rsp,
						   hardware_pushed,
						   prev_interrupt_handler,
						   cs);
				if (rsp >= ar.stack.logical_end &&
				    ar.stack.next) {
					kdba_bt_new_stack(&ar, &rsp, &count,
							   &suppress);
					rip_at_rsp = 0;
					continue;
				}
				bb_actual_set_value(BBRG_RSP, rsp);
			} else {
				cs = __KERNEL_CS;
			}
			rip_at_rsp = 1;
			bb_actual_rollback(&ar);
			if (bb_giveup)
				break;
			if (bb_actual_value(BBRG_RSP) < rsp) {
				kdb_printf("%s: " RSP " is going backwards, "
					   kdb_machreg_fmt0 " -> "
					   kdb_machreg_fmt0 "\n",
					   __FUNCTION__,
					   rsp,
					   bb_actual_value(BBRG_RSP));
				bb_giveup = 1;
				break;
			}
			bb_arguments(&ar);
			if (!suppress) {
				bt_print_one(rip, prev_rsp, &ar, &symtab, argcount);
				++count;
			}
			/* Functions that terminate the backtrace */
			if (strcmp(bb_func_name, "cpu_idle") == 0 ||
			    strcmp(bb_func_name, "child_rip") == 0)
				break;
			if (rsp >= ar.stack.logical_end &&
			    !ar.stack.next)
				break;
			if (rsp <= (unsigned long)regs &&
			    bb_actual_value(BBRG_RSP) > (unsigned long)regs) {
				if (ar.stack.next && suppress)
					kdb_printf(" ======================= <%s>\n",
						   ar.stack.id);
				++count;
				suppress = 0;
			}
			if (cs != __KERNEL_CS) {
				kdb_printf("Reached user space\n");
				break;
			}
			rsp = bb_actual_value(BBRG_RSP);
		}
		prev_noret = bb_noret(bb_func_name);
		if (count > 200)
			break;
	}
	if (bb_giveup)
		return 1;
	bb_cleanup();
	kdbnearsym_cleanup();

	if (count > 200) {
		kdb_printf("bt truncated, count limit reached\n");
		return 1;
	} else if (suppress) {
		kdb_printf
		    ("bt did not find pt_regs - no trace produced.  Suggest 'set BTSP 1'\n");
		return 1;
	}

	return 0;
}

/*
 * kdba_bt_address
 *
 *	Do a backtrace starting at a specified stack address.  Use this if the
 *	heuristics get the stack decode wrong.
 *
 * Inputs:
 *	addr	Address provided to 'bt' command.
 *	argcount
 * Outputs:
 *	None.
 * Returns:
 *	zero for success, a kdb diagnostic if error
 * Locking:
 *	none.
 * Remarks:
 *	mds %rsp comes in handy when examining the stack to do a manual
 *	traceback.
 */

int kdba_bt_address(kdb_machreg_t addr, int argcount)
{
	int ret;
	kdba_id_init(&kdb_di);			/* kdb_bb needs this done once */
	ret = kdba_bt_stack(addr, argcount, NULL, 0);
	if (ret == 1)
		ret = kdba_bt_stack(addr, argcount, NULL, 1);
	return ret;
}

/*
 * kdba_bt_process
 *
 *	Do a backtrace for a specified process.
 *
 * Inputs:
 *	p	Struct task pointer extracted by 'bt' command.
 *	argcount
 * Outputs:
 *	None.
 * Returns:
 *	zero for success, a kdb diagnostic if error
 * Locking:
 *	none.
 */

int kdba_bt_process(const struct task_struct *p, int argcount)
{
	int ret;
	kdba_id_init(&kdb_di);			/* kdb_bb needs this done once */
	ret = kdba_bt_stack(0, argcount, p, 0);
	if (ret == 1)
		ret = kdba_bt_stack(0, argcount, p, 1);
	return ret;
}

static int __init kdba_bt_x86_init(void)
{
	int i, c, cp = -1;
	struct bb_name_state *r;

	kdb_register_repeat("bb1", kdb_bb1, "<vaddr>",	"Analyse one basic block", 0, KDB_REPEAT_NONE);
	kdb_register_repeat("bb_all", kdb_bb_all, "",	"Backtrace check on all built in functions", 0, KDB_REPEAT_NONE);

	/* Split the opcode usage table by the first letter of each set of
	 * opcodes, for faster mapping of opcode to its operand usage.
	 */
	for (i = 0; i < ARRAY_SIZE(bb_opcode_usage_all); ++i) {
		c = bb_opcode_usage_all[i].opcode[0] - 'a';
		if (c != cp) {
			cp = c;
			bb_opcode_usage[c].opcode = bb_opcode_usage_all + i;
		}
		++bb_opcode_usage[c].size;
	}

	bb_common_interrupt = kallsyms_lookup_name("common_interrupt");
	bb_error_entry = kallsyms_lookup_name("error_entry");
	bb_ret_from_intr = kallsyms_lookup_name("ret_from_intr");
	bb_thread_return = kallsyms_lookup_name("thread_return");
	bb_sync_regs = kallsyms_lookup_name("sync_regs");
	bb_save_v86_state = kallsyms_lookup_name("save_v86_state");
	bb__sched_text_start = kallsyms_lookup_name("__sched_text_start");
	bb__sched_text_end = kallsyms_lookup_name("__sched_text_end");
	for (i = 0, r = bb_special_cases;
	     i < ARRAY_SIZE(bb_special_cases);
	     ++i, ++r) {
		r->address = kallsyms_lookup_name(r->name);
	}

#ifdef	CONFIG_4KSTACKS
	kdba_hardirq_ctx = (struct thread_info **)kallsyms_lookup_name("hardirq_ctx");
	kdba_softirq_ctx = (struct thread_info **)kallsyms_lookup_name("softirq_ctx");
#endif	/* CONFIG_4KSTACKS */

	return 0;
}

static void __exit kdba_bt_x86_exit(void)
{
	kdb_unregister("bb1");
	kdb_unregister("bb_all");
}

module_init(kdba_bt_x86_init)
module_exit(kdba_bt_x86_exit)
