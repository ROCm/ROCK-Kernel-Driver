/*
 * arch/alpha/kernel/traps.c
 *
 * (C) Copyright 1994 Linus Torvalds
 */

/*
 * This file initializes the trap entry points
 */

#include <linux/config.h>
#include <linux/mm.h>
#include <linux/sched.h>
#include <linux/tty.h>
#include <linux/delay.h>
#include <linux/smp_lock.h>

#include <asm/gentrap.h>
#include <asm/uaccess.h>
#include <asm/unaligned.h>
#include <asm/sysinfo.h>

#include "proto.h"

void
dik_show_regs(struct pt_regs *regs, unsigned long *r9_15)
{
	printk("pc = [<%016lx>]  ra = [<%016lx>]  ps = %04lx\n",
	       regs->pc, regs->r26, regs->ps);
	printk("v0 = %016lx  t0 = %016lx  t1 = %016lx\n",
	       regs->r0, regs->r1, regs->r2);
	printk("t2 = %016lx  t3 = %016lx  t4 = %016lx\n",
 	       regs->r3, regs->r4, regs->r5);
	printk("t5 = %016lx  t6 = %016lx  t7 = %016lx\n",
	       regs->r6, regs->r7, regs->r8);

	if (r9_15) {
		printk("s0 = %016lx  s1 = %016lx  s2 = %016lx\n",
		       r9_15[9], r9_15[10], r9_15[11]);
		printk("s3 = %016lx  s4 = %016lx  s5 = %016lx\n",
		       r9_15[12], r9_15[13], r9_15[14]);
		printk("s6 = %016lx\n", r9_15[15]);
	}

	printk("a0 = %016lx  a1 = %016lx  a2 = %016lx\n",
	       regs->r16, regs->r17, regs->r18);
	printk("a3 = %016lx  a4 = %016lx  a5 = %016lx\n",
 	       regs->r19, regs->r20, regs->r21);
 	printk("t8 = %016lx  t9 = %016lx  t10= %016lx\n",
	       regs->r22, regs->r23, regs->r24);
	printk("t11= %016lx  pv = %016lx  at = %016lx\n",
	       regs->r25, regs->r27, regs->r28);
	printk("gp = %016lx  sp = %p\n", regs->gp, regs+1);
#if 0
__halt();
#endif
}

static char * ireg_name[] = {"v0", "t0", "t1", "t2", "t3", "t4", "t5", "t6",
			   "t7", "s0", "s1", "s2", "s3", "s4", "s5", "s6",
			   "a0", "a1", "a2", "a3", "a4", "a5", "t8", "t9",
			   "t10", "t11", "ra", "pv", "at", "gp", "sp", "zero"};

static char * inst_name[] = {"call_pal", "", "", "", "", "", "", "",
	"lda", "ldah", "ldbu", "ldq_u", "ldwu", "stw", "stb", "stq_u",
	"ALU", "ALU", "ALU", "ALU", "SQRT", "FVAX", "FIEEE", "FLOAT",
	"MISC", "PAL19", "JMP", "PAL1B", "GRAPH", "PAL1D", "PAL1E", "PAL1F",
	"ldf", "ldg", "lds", "ldt", "stf", "stg", "sts", "stt",
	"ldl", "ldq", "ldl_l", "ldq_l", "stl", "stq", "stl_c", "stq_c",
	"br", "fbeq", "fblt", "fble", "bsr", "fbne", "fbge", "fbgt"
	"blbc", "beq", "blt", "ble", "blbs", "bne", "bge", "bgt"
};

static char * jump_name[] = {"jmp", "jsr", "ret", "jsr_coroutine"};

typedef struct {int func; char * text;} alist;

static alist inta_name[] = {{0, "addl"}, {2, "s4addl"}, {9, "subl"},
	{0xb, "s4subl"}, {0xf, "cmpbge"}, {0x12, "s8addl"}, {0x1b, "s8subl"},
	{0x1d, "cmpult"}, {0x20, "addq"}, {0x22, "s4addq"}, {0x29, "subq"},
	{0x2b, "s4subq"}, {0x2d, "cmpeq"}, {0x32, "s8addq"}, {0x3b, "s8subq"},
	{0x3d, "cmpule"}, {0x40, "addl/v"}, {0x49, "subl/v"}, {0x4d, "cmplt"},
	{0x60, "addq/v"}, {0x69, "subq/v"}, {0x6d, "cmple"}, {-1, 0}};

static alist intl_name[] = {{0, "and"}, {8, "andnot"}, {0x14, "cmovlbs"},
	{0x16, "cmovlbc"}, {0x20, "or"}, {0x24, "cmoveq"}, {0x26, "cmovne"},
	{0x28, "ornot"}, {0x40, "xor"}, {0x44, "cmovlt"}, {0x46, "cmovge"},
	{0x48, "eqv"}, {0x61, "amask"}, {0x64, "cmovle"}, {0x66, "cmovgt"},
	{0x6c, "implver"}, {-1, 0}};

static alist ints_name[] = {{2, "mskbl"}, {6, "extbl"}, {0xb, "insbl"},
	{0x12, "mskwl"}, {0x16, "extwl"}, {0x1b, "inswl"}, {0x22, "mskll"},
	{0x26, "extll"}, {0x2b, "insll"}, {0x30, "zap"}, {0x31, "zapnot"},
	{0x32, "mskql"}, {0x34, "srl"}, {0x36, "extql"}, {0x39, "sll"},
	{0x3b, "insql"}, {0x3c, "sra"}, {0x52, "mskwh"}, {0x57, "inswh"},
	{0x5a, "extwh"}, {0x62, "msklh"}, {0x67, "inslh"}, {0x6a, "extlh"},
	{0x72, "mskqh"}, {0x77, "insqh"}, {0x7a, "extqh"}, {-1, 0}};

static alist intm_name[] = {{0, "mull"}, {0x20, "mulq"}, {0x30, "umulh"},
	{0x40, "mull/v"}, {0x60, "mulq/v"}, {-1, 0}};

static alist * int_name[] = {inta_name, intl_name, ints_name, intm_name};

static char *
assoc(int fcode, alist * a)
{
	while ((fcode != a->func) && (a->func != -1))
		++a;
	return a->text;
}

static char *
iname(unsigned int instr)
{
	int opcode = instr >> 26;
	char * name = inst_name[opcode];

	switch (opcode) {
		default:
			break;

		case 0x10:
		case 0x11:
		case 0x12:
		case 0x13: {
			char * specific_name
			  = assoc((instr >> 5) & 0x3f, int_name[opcode - 0x10]);
			if (specific_name)
				name = specific_name;
			break;
		}

		case 0x1a:
			name = jump_name[(instr >> 14) & 3];
			break;
	}
	
	return name;
}

static enum {NOT_INST, PAL, BRANCH, MEMORY, JUMP, OPERATE, FOPERATE, MISC}
iformat(int opcode)
{
	if (opcode >= 0x30)
		return BRANCH;
	if (opcode >= 0x20)
		return MEMORY;
	if (opcode == 0)
		return PAL;
	if (opcode < 8)
		return NOT_INST;
	if (opcode < 0x10)
		return MEMORY;
	if (opcode < 0x14)
		return OPERATE;
	if (opcode < 0x18)
		return FOPERATE;
	switch (opcode) {
		case 0x18:
			return MISC;
		case 0x1A:
			return JUMP;
		case 0x1C:
			return OPERATE;
		default:
			return NOT_INST;
	}
}

/*
 * The purpose here is to provide useful clues about a kernel crash, so
 * less likely instructions, e.g. floating point, aren't fully decoded.
 */
static void
disassemble(unsigned int instr)
{
	int optype = instr >> 26;
	char buf[40], *s = buf;

	s += sprintf(buf, "%08x  %s ", instr, iname(instr));
	switch (iformat(optype)) {
		default:
		case NOT_INST:
		case MISC:
			break;

		case PAL:
			s += sprintf(s, "%d", instr);
			break;

		case BRANCH: {
			int reg = (instr >> 21) & 0x1f;
			int offset = instr & 0x1fffff;

			if (offset >= 0x100000)
				offset -= 0x200000;
			if (((optype & 3) == 0) || (optype >= 0x38)) {
				if ((optype != 0x30) || (reg != 0x1f))
					s += sprintf(s, "%s,", ireg_name[reg]);
			} else
				s += sprintf(s, "f%d,", reg);
			s += sprintf(s, ".%+d", (offset + 1) << 2);
			break;
		}

		case MEMORY: {
			int addr_reg = (instr >> 16) & 0x1f;
			int value_reg = (instr >> 21) & 0x1f;
			int offset = instr & 0xffff;

			if (offset >= 0x8000)
				offset -= 0x10000;
			if ((optype >= 0x20) && (optype < 0x28))
				s += sprintf(s, "f%d", value_reg);
			else
				s += sprintf(s, "%s", ireg_name[value_reg]);

			s += sprintf(s, ",%d(%s)", offset, ireg_name[addr_reg]);
			break;
		}

		case JUMP: {
			int target_reg = (instr >> 16) & 0x1f;
			int return_reg = (instr >> 21) & 0x1f;

			s += sprintf(s, "%s,", ireg_name[return_reg]);
			s += sprintf(s, "(%s)", ireg_name[target_reg]);
			break;
		}

		case OPERATE: {
			int areg = (instr >> 21) & 0x1f;
			int breg = (instr >> 16) & 0x1f;
			int creg = instr & 0x1f;
			int litflag = instr & (1<<12);
			int lit = (instr >> 13) & 0xff;

			s += sprintf(s, "%s,", ireg_name[areg]);
			if (litflag)
				s += sprintf(s, "%d", lit);
			else
				s += sprintf(s, "%s", ireg_name[breg]);
			s += sprintf(s, ",%s", ireg_name[creg]);
			break;
		}

		case FOPERATE: {
			int areg = (instr >> 21) & 0x1f;
			int breg = (instr >> 16) & 0x1f;
			int creg = instr & 0x1f;

			s += sprintf(s, "f%d,f%d,f%d", areg, breg, creg);
			break;
		}
	}
	buf[s-buf] = 0;
	printk("%s\n", buf);
}

static void
dik_show_code(unsigned int *pc)
{
	long i;

	printk("Code:");
	for (i = -6; i < 2; i++) {
		unsigned int insn;
		if (__get_user(insn, pc+i))
			break;
		printk("%c", i ? ' ' : '*');
		disassemble(insn);
	}
	printk("\n");
}

static void
dik_show_trace(unsigned long *sp)
{
	long i = 0;
	printk("Trace:");
	while (0x1ff8 & (unsigned long) sp) {
		extern unsigned long _stext, _etext;
		unsigned long tmp = *sp;
		sp++;
		if (tmp < (unsigned long) &_stext)
			continue;
		if (tmp >= (unsigned long) &_etext)
			continue;
		/*
		 * Assume that only the low 24-bits of a kernel text address
		 * is interesting.
		 */
		printk("%6x%c", (int)tmp & 0xffffff, (++i % 11) ? ' ' : '\n');
		if (i > 40) {
			printk(" ...");
			break;
		}
	}
	printk("\n");
}

void
die_if_kernel(char * str, struct pt_regs *regs, long err, unsigned long *r9_15)
{
	if (regs->ps & 8)
		return;
#ifdef CONFIG_SMP
	printk("CPU %d ", hard_smp_processor_id());
#endif
	printk("%s(%d): %s %ld\n", current->comm, current->pid, str, err);
	dik_show_regs(regs, r9_15);
	dik_show_code((unsigned int *)regs->pc);
	dik_show_trace((unsigned long *)(regs+1));

	if (current->thread.flags & (1UL << 63)) {
		printk("die_if_kernel recursion detected.\n");
		sti();
		while (1);
	}
	current->thread.flags |= (1UL << 63);
	do_exit(SIGSEGV);
}

#ifndef CONFIG_MATHEMU
static long dummy_emul(void) { return 0; }
long (*alpha_fp_emul_imprecise)(struct pt_regs *regs, unsigned long writemask)
  = (void *)dummy_emul;
long (*alpha_fp_emul) (unsigned long pc)
  = (void *)dummy_emul;
#else
long alpha_fp_emul_imprecise(struct pt_regs *regs, unsigned long writemask);
long alpha_fp_emul (unsigned long pc);
#endif

asmlinkage void
do_entArith(unsigned long summary, unsigned long write_mask,
	    unsigned long a2, unsigned long a3, unsigned long a4,
	    unsigned long a5, struct pt_regs regs)
{
	if (summary & 1) {
		/* Software-completion summary bit is set, so try to
		   emulate the instruction.  */
		if (!amask(AMASK_PRECISE_TRAP)) {
			/* 21264 (except pass 1) has precise exceptions.  */
			if (alpha_fp_emul(regs.pc - 4))
				return;
		} else {
			if (alpha_fp_emul_imprecise(&regs, write_mask))
				return;
		}
	}

#if 0
	printk("%s: arithmetic trap at %016lx: %02lx %016lx\n",
		current->comm, regs.pc, summary, write_mask);
#endif
	die_if_kernel("Arithmetic fault", &regs, 0, 0);
	send_sig(SIGFPE, current, 1);
}

asmlinkage void
do_entIF(unsigned long type, unsigned long a1,
	 unsigned long a2, unsigned long a3, unsigned long a4,
	 unsigned long a5, struct pt_regs regs)
{
	die_if_kernel((type == 1 ? "Kernel Bug" : "Instruction fault"),
		      &regs, type, 0);

	switch (type) {
	      case 0: /* breakpoint */
		if (ptrace_cancel_bpt(current)) {
			regs.pc -= 4;	/* make pc point to former bpt */
		}
		send_sig(SIGTRAP, current, 1);
		break;

	      case 1: /* bugcheck */
		send_sig(SIGTRAP, current, 1);
		break;

	      case 2: /* gentrap */
		/*
		 * The exception code should be passed on to the signal
		 * handler as the second argument.  Linux doesn't do that
		 * yet (also notice that Linux *always* behaves like
		 * DEC Unix with SA_SIGINFO off; see DEC Unix man page
		 * for sigaction(2)).
		 */
		switch ((long) regs.r16) {
		      case GEN_INTOVF: case GEN_INTDIV: case GEN_FLTOVF:
		      case GEN_FLTDIV: case GEN_FLTUND: case GEN_FLTINV:
		      case GEN_FLTINE: case GEN_ROPRAND:
			send_sig(SIGFPE, current, 1);
			break;

		      case GEN_DECOVF:
		      case GEN_DECDIV:
		      case GEN_DECINV:
		      case GEN_ASSERTERR:
		      case GEN_NULPTRERR:
		      case GEN_STKOVF:
		      case GEN_STRLENERR:
		      case GEN_SUBSTRERR:
		      case GEN_RANGERR:
		      case GEN_SUBRNG:
		      case GEN_SUBRNG1:
		      case GEN_SUBRNG2:
		      case GEN_SUBRNG3:
		      case GEN_SUBRNG4:
		      case GEN_SUBRNG5:
		      case GEN_SUBRNG6:
		      case GEN_SUBRNG7:
			send_sig(SIGTRAP, current, 1);
			break;
		}
		break;

	      case 3: /* FEN fault */
		send_sig(SIGILL, current, 1);
		break;

	      case 4: /* opDEC */
		if (implver() == IMPLVER_EV4) {
			/* EV4 does not implement anything except normal
			   rounding.  Everything else will come here as
			   an illegal instruction.  Emulate them.  */
			if (alpha_fp_emul(regs.pc-4))
				return;
		}
		send_sig(SIGILL, current, 1);
		break;

	      default:
		panic("do_entIF: unexpected instruction-fault type");
	}
}

/* There is an ifdef in the PALcode in MILO that enables a 
   "kernel debugging entry point" as an unprivilaged call_pal.

   We don't want to have anything to do with it, but unfortunately
   several versions of MILO included in distributions have it enabled,
   and if we don't put something on the entry point we'll oops.  */

asmlinkage void
do_entDbg(unsigned long type, unsigned long a1,
	  unsigned long a2, unsigned long a3, unsigned long a4,
	  unsigned long a5, struct pt_regs regs)
{
	die_if_kernel("Instruction fault", &regs, type, 0);
	force_sig(SIGILL, current);
}


/*
 * entUna has a different register layout to be reasonably simple. It
 * needs access to all the integer registers (the kernel doesn't use
 * fp-regs), and it needs to have them in order for simpler access.
 *
 * Due to the non-standard register layout (and because we don't want
 * to handle floating-point regs), user-mode unaligned accesses are
 * handled separately by do_entUnaUser below.
 *
 * Oh, btw, we don't handle the "gp" register correctly, but if we fault
 * on a gp-register unaligned load/store, something is _very_ wrong
 * in the kernel anyway..
 */
struct allregs {
	unsigned long regs[32];
	unsigned long ps, pc, gp, a0, a1, a2;
};

struct unaligned_stat {
	unsigned long count, va, pc;
} unaligned[2];


/* Macro for exception fixup code to access integer registers.  */
#define una_reg(r)  (regs.regs[(r) >= 16 && (r) <= 18 ? (r)+19 : (r)])


asmlinkage void
do_entUna(void * va, unsigned long opcode, unsigned long reg,
	  unsigned long a3, unsigned long a4, unsigned long a5,
	  struct allregs regs)
{
	long error, tmp1, tmp2, tmp3, tmp4;
	unsigned long pc = regs.pc - 4;
	unsigned fixup;

	unaligned[0].count++;
	unaligned[0].va = (unsigned long) va;
	unaligned[0].pc = pc;

	/* We don't want to use the generic get/put unaligned macros as
	   we want to trap exceptions.  Only if we actually get an
	   exception will we decide whether we should have caught it.  */

	switch (opcode) {
	case 0x0c: /* ldwu */
		__asm__ __volatile__(
		"1:	ldq_u %1,0(%3)\n"
		"2:	ldq_u %2,1(%3)\n"
		"	extwl %1,%3,%1\n"
		"	extwh %2,%3,%2\n"
		"3:\n"
		".section __ex_table,\"a\"\n"
		"	.gprel32 1b\n"
		"	lda %1,3b-1b(%0)\n"
		"	.gprel32 2b\n"
		"	lda %2,3b-2b(%0)\n"
		".previous"
			: "=r"(error), "=&r"(tmp1), "=&r"(tmp2)
			: "r"(va), "0"(0));
		if (error)
			goto got_exception;
		una_reg(reg) = tmp1|tmp2;
		return;

	case 0x28: /* ldl */
		__asm__ __volatile__(
		"1:	ldq_u %1,0(%3)\n"
		"2:	ldq_u %2,3(%3)\n"
		"	extll %1,%3,%1\n"
		"	extlh %2,%3,%2\n"
		"3:\n"
		".section __ex_table,\"a\"\n"
		"	.gprel32 1b\n"
		"	lda %1,3b-1b(%0)\n"
		"	.gprel32 2b\n"
		"	lda %2,3b-2b(%0)\n"
		".previous"
			: "=r"(error), "=&r"(tmp1), "=&r"(tmp2)
			: "r"(va), "0"(0));
		if (error)
			goto got_exception;
		una_reg(reg) = (int)(tmp1|tmp2);
		return;

	case 0x29: /* ldq */
		__asm__ __volatile__(
		"1:	ldq_u %1,0(%3)\n"
		"2:	ldq_u %2,7(%3)\n"
		"	extql %1,%3,%1\n"
		"	extqh %2,%3,%2\n"
		"3:\n"
		".section __ex_table,\"a\"\n"
		"	.gprel32 1b\n"
		"	lda %1,3b-1b(%0)\n"
		"	.gprel32 2b\n"
		"	lda %2,3b-2b(%0)\n"
		".previous"
			: "=r"(error), "=&r"(tmp1), "=&r"(tmp2)
			: "r"(va), "0"(0));
		if (error)
			goto got_exception;
		una_reg(reg) = tmp1|tmp2;
		return;

	/* Note that the store sequences do not indicate that they change
	   memory because it _should_ be affecting nothing in this context.
	   (Otherwise we have other, much larger, problems.)  */
	case 0x0d: /* stw */
		__asm__ __volatile__(
		"1:	ldq_u %2,1(%5)\n"
		"2:	ldq_u %1,0(%5)\n"
		"	inswh %6,%5,%4\n"
		"	inswl %6,%5,%3\n"
		"	mskwh %2,%5,%2\n"
		"	mskwl %1,%5,%1\n"
		"	or %2,%4,%2\n"
		"	or %1,%3,%1\n"
		"3:	stq_u %2,1(%5)\n"
		"4:	stq_u %1,0(%5)\n"
		"5:\n"
		".section __ex_table,\"a\"\n"
		"	.gprel32 1b\n"
		"	lda %2,5b-1b(%0)\n"
		"	.gprel32 2b\n"
		"	lda %1,5b-2b(%0)\n"
		"	.gprel32 3b\n"
		"	lda $31,5b-3b(%0)\n"
		"	.gprel32 4b\n"
		"	lda $31,5b-4b(%0)\n"
		".previous"
			: "=r"(error), "=&r"(tmp1), "=&r"(tmp2),
			  "=&r"(tmp3), "=&r"(tmp4)
			: "r"(va), "r"(una_reg(reg)), "0"(0));
		if (error)
			goto got_exception;
		return;

	case 0x2c: /* stl */
		__asm__ __volatile__(
		"1:	ldq_u %2,3(%5)\n"
		"2:	ldq_u %1,0(%5)\n"
		"	inslh %6,%5,%4\n"
		"	insll %6,%5,%3\n"
		"	msklh %2,%5,%2\n"
		"	mskll %1,%5,%1\n"
		"	or %2,%4,%2\n"
		"	or %1,%3,%1\n"
		"3:	stq_u %2,3(%5)\n"
		"4:	stq_u %1,0(%5)\n"
		"5:\n"
		".section __ex_table,\"a\"\n"
		"	.gprel32 1b\n"
		"	lda %2,5b-1b(%0)\n"
		"	.gprel32 2b\n"
		"	lda %1,5b-2b(%0)\n"
		"	.gprel32 3b\n"
		"	lda $31,5b-3b(%0)\n"
		"	.gprel32 4b\n"
		"	lda $31,5b-4b(%0)\n"
		".previous"
			: "=r"(error), "=&r"(tmp1), "=&r"(tmp2),
			  "=&r"(tmp3), "=&r"(tmp4)
			: "r"(va), "r"(una_reg(reg)), "0"(0));
		if (error)
			goto got_exception;
		return;

	case 0x2d: /* stq */
		__asm__ __volatile__(
		"1:	ldq_u %2,7(%5)\n"
		"2:	ldq_u %1,0(%5)\n"
		"	insqh %6,%5,%4\n"
		"	insql %6,%5,%3\n"
		"	mskqh %2,%5,%2\n"
		"	mskql %1,%5,%1\n"
		"	or %2,%4,%2\n"
		"	or %1,%3,%1\n"
		"3:	stq_u %2,7(%5)\n"
		"4:	stq_u %1,0(%5)\n"
		"5:\n"
		".section __ex_table,\"a\"\n\t"
		"	.gprel32 1b\n"
		"	lda %2,5b-1b(%0)\n"
		"	.gprel32 2b\n"
		"	lda %1,5b-2b(%0)\n"
		"	.gprel32 3b\n"
		"	lda $31,5b-3b(%0)\n"
		"	.gprel32 4b\n"
		"	lda $31,5b-4b(%0)\n"
		".previous"
			: "=r"(error), "=&r"(tmp1), "=&r"(tmp2),
			  "=&r"(tmp3), "=&r"(tmp4)
			: "r"(va), "r"(una_reg(reg)), "0"(0));
		if (error)
			goto got_exception;
		return;
	}

	lock_kernel();
	printk("Bad unaligned kernel access at %016lx: %p %lx %ld\n",
		pc, va, opcode, reg);
	do_exit(SIGSEGV);

got_exception:
	/* Ok, we caught the exception, but we don't want it.  Is there
	   someone to pass it along to?  */
	if ((fixup = search_exception_table(pc, regs.gp)) != 0) {
		unsigned long newpc;
		newpc = fixup_exception(una_reg, fixup, pc);

		printk("Forwarding unaligned exception at %lx (%lx)\n",
		       pc, newpc);

		(&regs)->pc = newpc;
		return;
	}

	/*
	 * Yikes!  No one to forward the exception to.
	 * Since the registers are in a weird format, dump them ourselves.
 	 */
	lock_kernel();

	printk("%s(%d): unhandled unaligned exception\n",
	       current->comm, current->pid);

	printk("pc = [<%016lx>]  ra = [<%016lx>]  ps = %04lx\n",
	       pc, una_reg(26), regs.ps);
	printk("r0 = %016lx  r1 = %016lx  r2 = %016lx\n",
	       una_reg(0), una_reg(1), una_reg(2));
	printk("r3 = %016lx  r4 = %016lx  r5 = %016lx\n",
 	       una_reg(3), una_reg(4), una_reg(5));
	printk("r6 = %016lx  r7 = %016lx  r8 = %016lx\n",
	       una_reg(6), una_reg(7), una_reg(8));
	printk("r9 = %016lx  r10= %016lx  r11= %016lx\n",
	       una_reg(9), una_reg(10), una_reg(11));
	printk("r12= %016lx  r13= %016lx  r14= %016lx\n",
	       una_reg(12), una_reg(13), una_reg(14));
	printk("r15= %016lx\n", una_reg(15));
	printk("r16= %016lx  r17= %016lx  r18= %016lx\n",
	       una_reg(16), una_reg(17), una_reg(18));
	printk("r19= %016lx  r20= %016lx  r21= %016lx\n",
 	       una_reg(19), una_reg(20), una_reg(21));
 	printk("r22= %016lx  r23= %016lx  r24= %016lx\n",
	       una_reg(22), una_reg(23), una_reg(24));
	printk("r25= %016lx  r27= %016lx  r28= %016lx\n",
	       una_reg(25), una_reg(27), una_reg(28));
	printk("gp = %016lx  sp = %p\n", regs.gp, &regs+1);

	dik_show_code((unsigned int *)pc);
	dik_show_trace((unsigned long *)(&regs+1));

	if (current->thread.flags & (1UL << 63)) {
		printk("die_if_kernel recursion detected.\n");
		sti();
		while (1);
	}
	current->thread.flags |= (1UL << 63);
	do_exit(SIGSEGV);
}

/*
 * Convert an s-floating point value in memory format to the
 * corresponding value in register format.  The exponent
 * needs to be remapped to preserve non-finite values
 * (infinities, not-a-numbers, denormals).
 */
static inline unsigned long
s_mem_to_reg (unsigned long s_mem)
{
	unsigned long frac    = (s_mem >>  0) & 0x7fffff;
	unsigned long sign    = (s_mem >> 31) & 0x1;
	unsigned long exp_msb = (s_mem >> 30) & 0x1;
	unsigned long exp_low = (s_mem >> 23) & 0x7f;
	unsigned long exp;

	exp = (exp_msb << 10) | exp_low;	/* common case */
	if (exp_msb) {
		if (exp_low == 0x7f) {
			exp = 0x7ff;
		}
	} else {
		if (exp_low == 0x00) {
			exp = 0x000;
		} else {
			exp |= (0x7 << 7);
		}
	}
	return (sign << 63) | (exp << 52) | (frac << 29);
}

/*
 * Convert an s-floating point value in register format to the
 * corresponding value in memory format.
 */
static inline unsigned long
s_reg_to_mem (unsigned long s_reg)
{
	return ((s_reg >> 62) << 30) | ((s_reg << 5) >> 34);
}

/*
 * Handle user-level unaligned fault.  Handling user-level unaligned
 * faults is *extremely* slow and produces nasty messages.  A user
 * program *should* fix unaligned faults ASAP.
 *
 * Notice that we have (almost) the regular kernel stack layout here,
 * so finding the appropriate registers is a little more difficult
 * than in the kernel case.
 *
 * Finally, we handle regular integer load/stores only.  In
 * particular, load-linked/store-conditionally and floating point
 * load/stores are not supported.  The former make no sense with
 * unaligned faults (they are guaranteed to fail) and I don't think
 * the latter will occur in any decent program.
 *
 * Sigh. We *do* have to handle some FP operations, because GCC will
 * uses them as temporary storage for integer memory to memory copies.
 * However, we need to deal with stt/ldt and sts/lds only.
 */

#define OP_INT_MASK	( 1L << 0x28 | 1L << 0x2c   /* ldl stl */	\
			| 1L << 0x29 | 1L << 0x2d   /* ldq stq */	\
			| 1L << 0x0c | 1L << 0x0d   /* ldwu stw */	\
			| 1L << 0x0a | 1L << 0x0e ) /* ldbu stb */

#define OP_WRITE_MASK	( 1L << 0x26 | 1L << 0x27   /* sts stt */	\
			| 1L << 0x2c | 1L << 0x2d   /* stl stq */	\
			| 1L << 0x0d | 1L << 0x0e ) /* stw stb */

#define R(x)	((size_t) &((struct pt_regs *)0)->x)

static int unauser_reg_offsets[32] = {
	R(r0), R(r1), R(r2), R(r3), R(r4), R(r5), R(r6), R(r7), R(r8),
	/* r9 ... r15 are stored in front of regs.  */
	-56, -48, -40, -32, -24, -16, -8,
	R(r16), R(r17), R(r18),
	R(r19), R(r20), R(r21), R(r22), R(r23), R(r24), R(r25), R(r26),
	R(r27), R(r28), R(gp),
	0, 0
};

#undef R

asmlinkage void
do_entUnaUser(void * va, unsigned long opcode,
	      unsigned long reg, struct pt_regs *regs)
{
	static int cnt = 0;
	static long last_time = 0;

	unsigned long tmp1, tmp2, tmp3, tmp4;
	unsigned long fake_reg, *reg_addr = &fake_reg;
	unsigned long uac_bits;
	long error;

	/* Check the UAC bits to decide what the user wants us to do
	   with the unaliged access.  */

	uac_bits = (current->thread.flags >> UAC_SHIFT) & UAC_BITMASK;
	if (!(uac_bits & UAC_NOPRINT)) {
		if (cnt >= 5 && jiffies - last_time > 5*HZ) {
			cnt = 0;
		}
		if (++cnt < 5) {
			printk("%s(%d): unaligned trap at %016lx: %p %lx %ld\n",
			       current->comm, current->pid,
			       regs->pc - 4, va, opcode, reg);
		}
		last_time = jiffies;
	}
	if (uac_bits & UAC_SIGBUS) {
		goto give_sigbus;
	}
	if (uac_bits & UAC_NOFIX) {
		/* Not sure why you'd want to use this, but... */
		return;
	}

	/* Don't bother reading ds in the access check since we already
	   know that this came from the user.  Also rely on the fact that
	   the page at TASK_SIZE is unmapped and so can't be touched anyway. */
	if (!__access_ok((unsigned long)va, 0, USER_DS))
		goto give_sigsegv;

	++unaligned[1].count;
	unaligned[1].va = (unsigned long)va;
	unaligned[1].pc = regs->pc - 4;

	if ((1L << opcode) & OP_INT_MASK) {
		/* it's an integer load/store */
		if (reg < 30) {
			reg_addr = (unsigned long *)
			  ((char *)regs + unauser_reg_offsets[reg]);
		} else if (reg == 30) {
			/* usp in PAL regs */
			fake_reg = rdusp();
		} else {
			/* zero "register" */
			fake_reg = 0;
		}
	}

	/* We don't want to use the generic get/put unaligned macros as
	   we want to trap exceptions.  Only if we actually get an
	   exception will we decide whether we should have caught it.  */

	switch (opcode) {
	case 0x0c: /* ldwu */
		__asm__ __volatile__(
		"1:	ldq_u %1,0(%3)\n"
		"2:	ldq_u %2,1(%3)\n"
		"	extwl %1,%3,%1\n"
		"	extwh %2,%3,%2\n"
		"3:\n"
		".section __ex_table,\"a\"\n"
		"	.gprel32 1b\n"
		"	lda %1,3b-1b(%0)\n"
		"	.gprel32 2b\n"
		"	lda %2,3b-2b(%0)\n"
		".previous"
			: "=r"(error), "=&r"(tmp1), "=&r"(tmp2)
			: "r"(va), "0"(0));
		if (error)
			goto give_sigsegv;
		*reg_addr = tmp1|tmp2;
		break;

	case 0x22: /* lds */
		__asm__ __volatile__(
		"1:	ldq_u %1,0(%3)\n"
		"2:	ldq_u %2,3(%3)\n"
		"	extll %1,%3,%1\n"
		"	extlh %2,%3,%2\n"
		"3:\n"
		".section __ex_table,\"a\"\n"
		"	.gprel32 1b\n"
		"	lda %1,3b-1b(%0)\n"
		"	.gprel32 2b\n"
		"	lda %2,3b-2b(%0)\n"
		".previous"
			: "=r"(error), "=&r"(tmp1), "=&r"(tmp2)
			: "r"(va), "0"(0));
		if (error)
			goto give_sigsegv;
		alpha_write_fp_reg(reg, s_mem_to_reg((int)(tmp1|tmp2)));
		return;

	case 0x23: /* ldt */
		__asm__ __volatile__(
		"1:	ldq_u %1,0(%3)\n"
		"2:	ldq_u %2,7(%3)\n"
		"	extql %1,%3,%1\n"
		"	extqh %2,%3,%2\n"
		"3:\n"
		".section __ex_table,\"a\"\n"
		"	.gprel32 1b\n"
		"	lda %1,3b-1b(%0)\n"
		"	.gprel32 2b\n"
		"	lda %2,3b-2b(%0)\n"
		".previous"
			: "=r"(error), "=&r"(tmp1), "=&r"(tmp2)
			: "r"(va), "0"(0));
		if (error)
			goto give_sigsegv;
		alpha_write_fp_reg(reg, tmp1|tmp2);
		return;

	case 0x28: /* ldl */
		__asm__ __volatile__(
		"1:	ldq_u %1,0(%3)\n"
		"2:	ldq_u %2,3(%3)\n"
		"	extll %1,%3,%1\n"
		"	extlh %2,%3,%2\n"
		"3:\n"
		".section __ex_table,\"a\"\n"
		"	.gprel32 1b\n"
		"	lda %1,3b-1b(%0)\n"
		"	.gprel32 2b\n"
		"	lda %2,3b-2b(%0)\n"
		".previous"
			: "=r"(error), "=&r"(tmp1), "=&r"(tmp2)
			: "r"(va), "0"(0));
		if (error)
			goto give_sigsegv;
		*reg_addr = (int)(tmp1|tmp2);
		break;

	case 0x29: /* ldq */
		__asm__ __volatile__(
		"1:	ldq_u %1,0(%3)\n"
		"2:	ldq_u %2,7(%3)\n"
		"	extql %1,%3,%1\n"
		"	extqh %2,%3,%2\n"
		"3:\n"
		".section __ex_table,\"a\"\n"
		"	.gprel32 1b\n"
		"	lda %1,3b-1b(%0)\n"
		"	.gprel32 2b\n"
		"	lda %2,3b-2b(%0)\n"
		".previous"
			: "=r"(error), "=&r"(tmp1), "=&r"(tmp2)
			: "r"(va), "0"(0));
		if (error)
			goto give_sigsegv;
		*reg_addr = tmp1|tmp2;
		break;

	/* Note that the store sequences do not indicate that they change
	   memory because it _should_ be affecting nothing in this context.
	   (Otherwise we have other, much larger, problems.)  */
	case 0x0d: /* stw */
		__asm__ __volatile__(
		"1:	ldq_u %2,1(%5)\n"
		"2:	ldq_u %1,0(%5)\n"
		"	inswh %6,%5,%4\n"
		"	inswl %6,%5,%3\n"
		"	mskwh %2,%5,%2\n"
		"	mskwl %1,%5,%1\n"
		"	or %2,%4,%2\n"
		"	or %1,%3,%1\n"
		"3:	stq_u %2,1(%5)\n"
		"4:	stq_u %1,0(%5)\n"
		"5:\n"
		".section __ex_table,\"a\"\n"
		"	.gprel32 1b\n"
		"	lda %2,5b-1b(%0)\n"
		"	.gprel32 2b\n"
		"	lda %1,5b-2b(%0)\n"
		"	.gprel32 3b\n"
		"	lda $31,5b-3b(%0)\n"
		"	.gprel32 4b\n"
		"	lda $31,5b-4b(%0)\n"
		".previous"
			: "=r"(error), "=&r"(tmp1), "=&r"(tmp2),
			  "=&r"(tmp3), "=&r"(tmp4)
			: "r"(va), "r"(*reg_addr), "0"(0));
		if (error)
			goto give_sigsegv;
		return;

	case 0x26: /* sts */
		fake_reg = s_reg_to_mem(alpha_read_fp_reg(reg));
		/* FALLTHRU */

	case 0x2c: /* stl */
		__asm__ __volatile__(
		"1:	ldq_u %2,3(%5)\n"
		"2:	ldq_u %1,0(%5)\n"
		"	inslh %6,%5,%4\n"
		"	insll %6,%5,%3\n"
		"	msklh %2,%5,%2\n"
		"	mskll %1,%5,%1\n"
		"	or %2,%4,%2\n"
		"	or %1,%3,%1\n"
		"3:	stq_u %2,3(%5)\n"
		"4:	stq_u %1,0(%5)\n"
		"5:\n"
		".section __ex_table,\"a\"\n"
		"	.gprel32 1b\n"
		"	lda %2,5b-1b(%0)\n"
		"	.gprel32 2b\n"
		"	lda %1,5b-2b(%0)\n"
		"	.gprel32 3b\n"
		"	lda $31,5b-3b(%0)\n"
		"	.gprel32 4b\n"
		"	lda $31,5b-4b(%0)\n"
		".previous"
			: "=r"(error), "=&r"(tmp1), "=&r"(tmp2),
			  "=&r"(tmp3), "=&r"(tmp4)
			: "r"(va), "r"(*reg_addr), "0"(0));
		if (error)
			goto give_sigsegv;
		return;

	case 0x27: /* stt */
		fake_reg = alpha_read_fp_reg(reg);
		/* FALLTHRU */

	case 0x2d: /* stq */
		__asm__ __volatile__(
		"1:	ldq_u %2,7(%5)\n"
		"2:	ldq_u %1,0(%5)\n"
		"	insqh %6,%5,%4\n"
		"	insql %6,%5,%3\n"
		"	mskqh %2,%5,%2\n"
		"	mskql %1,%5,%1\n"
		"	or %2,%4,%2\n"
		"	or %1,%3,%1\n"
		"3:	stq_u %2,7(%5)\n"
		"4:	stq_u %1,0(%5)\n"
		"5:\n"
		".section __ex_table,\"a\"\n\t"
		"	.gprel32 1b\n"
		"	lda %2,5b-1b(%0)\n"
		"	.gprel32 2b\n"
		"	lda %1,5b-2b(%0)\n"
		"	.gprel32 3b\n"
		"	lda $31,5b-3b(%0)\n"
		"	.gprel32 4b\n"
		"	lda $31,5b-4b(%0)\n"
		".previous"
			: "=r"(error), "=&r"(tmp1), "=&r"(tmp2),
			  "=&r"(tmp3), "=&r"(tmp4)
			: "r"(va), "r"(*reg_addr), "0"(0));
		if (error)
			goto give_sigsegv;
		return;

	default:
		/* What instruction were you trying to use, exactly?  */
		goto give_sigbus;
	}

	/* Only integer loads should get here; everyone else returns early. */
	if (reg == 30)
		wrusp(fake_reg);
	return;

give_sigsegv:
	regs->pc -= 4;  /* make pc point to faulting insn */
	send_sig(SIGSEGV, current, 1);
	return;

give_sigbus:
	regs->pc -= 4;
	send_sig(SIGBUS, current, 1);
	return;
}

/*
 * Unimplemented system calls.
 */
asmlinkage long
alpha_ni_syscall(unsigned long a0, unsigned long a1, unsigned long a2,
		 unsigned long a3, unsigned long a4, unsigned long a5,
		 struct pt_regs regs)
{
	/* We only get here for OSF system calls, minus #112;
	   the rest go to sys_ni_syscall.  */
	printk("<sc %ld(%lx,%lx,%lx)>", regs.r0, a0, a1, a2);
	return -ENOSYS;
}

void
trap_init(void)
{
	/* Tell PAL-code what global pointer we want in the kernel.  */
	register unsigned long gptr __asm__("$29");
	wrkgp(gptr);

	wrent(entArith, 1);
	wrent(entMM, 2);
	wrent(entIF, 3);
	wrent(entUna, 4);
	wrent(entSys, 5);
	wrent(entDbg, 6);
}
