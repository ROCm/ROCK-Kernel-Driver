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
 * This is a special file, part of the dprobes interpreter that contains
 * architecture-specific functions. It is not compiled as a seperate unit.
 * It is #included into the arch-independent dprobes_in.c at a specific
 * location.
 */
#include <linux/binfmts.h>
#ifdef CONFIG_DUMP
#include <linux/dump.h>
#endif
#ifdef CONFIG_KDB
#include <linux/kdb.h>
#endif
#include <asm/desc.h>
#include <asm/ldt.h>
#include <asm/cacheflush.h>
#ifndef GDT_ENTRIES
#define GDT_ENTRIES     (__TSS(NR_CPUS))
#endif

/*
 * Convert segmented address (selector : offset) to flat linear address.
 */
static int dp_seg_to_flat(unsigned long sel, unsigned long off, void **faddr)
{
	struct Xgt_desc_struct g;
	unsigned long ldt_desc, ldt_base, ldtr, seg_desc, seg_base;
	unsigned long gdt_index = (sel & ~7);
	mm_context_t *x = &current->mm->context;

	__asm__ __volatile__ ("sgdt (%0)" : : "r"((unsigned long) &g));

	if (sel & 4) {
		if (!(x->ldt && ((sel >> 3) < x->size)))
			return 1;

		__asm__ __volatile__ ("sldt %0" :"=r"(ldtr));
		ldt_desc = g.address + ((ldtr & ~7));
		ldt_base = _get_base ((char *)ldt_desc);
		seg_desc = ldt_base + (gdt_index);
		seg_base = _get_base ((char *)seg_desc);
	}
	else {
		if ((sel >> 3) > GDT_ENTRIES)
			return 1;
		seg_desc = g.address + gdt_index;
		seg_base = _get_base ((char *)seg_desc);
	}
	*faddr = (void *)(seg_base + off);
	return 0;
}

static void seg2lin(void)
{
	void *faddr;
        unsigned long off = rpnpop();
        unsigned long sel = rpnpop();

	if (dp_seg_to_flat(sel, off, &faddr)) {
		gen_ex(EX_SEG_FAULT, sel, 0);
		return;
	}
	rpnpush((unsigned long) faddr);
}

#ifdef CONFIG_KDB
/* 
 * Temporarily restore the orignal opcode befor calling the kdb 
 * so that kdb shows correct disassembly.
 */
static inline void restore_opcode(void)
{
	if (dprobes.rec->point.probe & DP_PROBE_BREAKPOINT) {
		dprobes.reset_addr = dprobes.probe_addr;
		*((byte_t *)dprobes.reset_addr) = dprobes.opcode;
		flush_icache_range(dprobes.reset_addr, dprobes.reset_addr + sizeof(dprobes.reset_addr));
	}
}

/*
 * After kdb terminates, restore the breakpoint.
 */
static inline void restore_int3(void)
{	
	if (dprobes.rec->point.probe & DP_PROBE_BREAKPOINT) {
		dprobes.reset_addr = dprobes.probe_addr;
		*((byte_t *)dprobes.reset_addr) = DP_INSTR_BREAKPOINT;
		flush_icache_range(dprobes.reset_addr, dprobes.reset_addr + sizeof(dprobes.reset_addr));
	}
}
	
void call_kdb(void)
{
	unsigned long eip = dprobes.regs->eip - 1;
	if (dprobes.status & DP_KERNEL_PROBE) {
		if (dprobes.rec->point.probe & DP_PROBE_BREAKPOINT)
			dprobes.regs->eip--;
		restore_opcode();
		kdb(KDB_REASON_CALL, 0, dprobes.regs);
		restore_int3();
		if (eip == dprobes.regs->eip)
			dprobes.regs->eip++;
	}
}
#else

void call_kdb(void)
{
	printk("Dprobes: Kernel Debugger is not present.\n");
	return;
}
#endif
/*
 * Exiting the interpreter through the registered facility n.
 */
static void dp_exit_n(void)
{
	u8 facility = get_u8_oprnd();
	switch(facility) {
	case DP_EXIT_TO_SGI_KDB:
		call_kdb();	
		break;

	case DP_EXIT_TO_SGI_VMDUMP:
#ifdef CONFIG_DUMP
		if (dprobes.status & DP_KERNEL_PROBE)
			dump("Dumping due to dprobes", dprobes.regs);
#else
		printk("dprobes: Crash dump facility is not present.\n");
#endif
		break;
	case DP_EXIT_TO_CORE_DUMP:
		if (dprobes.status & DP_USER_PROBE) {
			if (do_coredump(SIGINT, SIGINT, dprobes.regs)) {	
				current->mm->dumpable = 1;
				printk("dprobes(%d,%d) process %s dumped core\n", dprobes.major, dprobes.minor, current->comm);
			}
			else
				printk("dprobes(%d,%d) exit to core dump failed\n", dprobes.major, dprobes.minor);
		}
		break;
	default: gen_ex(EX_INVALID_OPERAND, 4, facility); return;
	
	}
	dprobes.status &= ~DP_STATUS_INTERPRETER;
}

/*
 * Interpreter's version of copy_xxx_user functions. These need to save
 * and restore cr2 contents to be able to correctly handle probes in
 * page fault handler.
 */
static int dp_intr_copy_from_user(void *to, void *from, int len) 
{
	unsigned long address;
	int retval;
	__asm__("movl %%cr2,%0":"=r" (address)); /* save cr2 contents */
	retval = __copy_from_user(to, from, len);
	__asm__("movl %0,%%cr2": :"r" (address)); /* restore cr2 contents */
	return retval;
}

static int dp_intr_copy_to_user(void *to, void *from, int len) 
{
	unsigned long address;
	int retval;
	__asm__("movl %%cr2,%0":"=r" (address)); /* save cr2 contents */
	retval = __copy_to_user(to, from, len);
	__asm__("movl %0,%%cr2": :"r" (address)); /* restore cr2 contents */
	return retval;
}

#define BITS_IN_UL	32

#define PROPAGATE_BIT(name, OPERATOR) \
static void name(void) \
{ \
	unsigned long index, num, pos, val; \
	index = rpnpop(); \
	if (!index || index > BITS_IN_UL) { \
		gen_ex(EX_INVALID_OPERAND, 3, index); \
		return; \
	} \
	num = rpnpop(); \
	pos = 1UL << (index - 1); \
	val = num & pos; \
	for (; pos; pos OPERATOR 1, val OPERATOR 1) \
		num &= ~pos, num |= val; \
	rpnpush(num); \
}	

PROPAGATE_BIT(pbl, <<=)
PROPAGATE_BIT(pbr, >>=)

#define PROPAGATE_BIT_IMM(name, OPERATOR) \
static void name(void) \
{ \
	unsigned long index, num, pos, val; \
	index = (unsigned long)get_u8_oprnd(); \
	if (!index || index > BITS_IN_UL) { \
		dprobes.status &= ~DP_STATUS_INTERPRETER; \
		return; \
	} \
	num = rpnpop(); \
	pos = 1UL << (index - 1); \
	val = num & pos; \
	for (; pos; pos OPERATOR 1, val OPERATOR 1) \
		num &= ~pos, num |= val; \
	rpnpush(num); \
}

PROPAGATE_BIT_IMM(pbl_i, <<=)
PROPAGATE_BIT_IMM(pbr_i, >>=)

static void pzl(void) 
{ 
	unsigned long index, num, pos;
	index = rpnpop(); 
	if (!index || index > BITS_IN_UL) {
		gen_ex(EX_INVALID_OPERAND, 3, index); \
		return;
	}
	num = rpnpop();
	pos = 1UL << index;
	for (; pos; pos <<= 1)
		num &= ~pos;
	rpnpush(num);
}
	
static void pzl_i(void) 
{ 
	unsigned long index, num, pos;
	index = (unsigned long)get_u8_oprnd();
	if (!index || index > BITS_IN_UL) {
		gen_ex(EX_INVALID_OPERAND, 3, index);
		return;
	}
	num = rpnpop();
	pos = 1UL << index;
	for (; pos; pos <<= 1)
		num &= ~pos;
	rpnpush(num);
}

static void pzr(void)
{
	unsigned long index, num, pos;
	index = rpnpop();
	if (!index || index > BITS_IN_UL) {
		gen_ex(EX_INVALID_OPERAND, 3, index);
		return;
	}
	if (index == 1)
		return;
	num = rpnpop();
	pos = 1UL << (index - 2);
	for (; pos; pos >>= 1)
		num &= ~pos;
	rpnpush(num);
}

static void pzr_i(void)
{
	unsigned long index, num, pos;
	index = (unsigned long)get_u8_oprnd();
	if (!index || index > BITS_IN_UL) {
		gen_ex(EX_INVALID_OPERAND, 3, index);
		return;
	}
	if (index == 1)
		return;
	num = rpnpop();
	pos = 1UL << (index - 2);
	for (; pos; pos >>= 1)
		num &= ~pos;
	rpnpush(num);
}

static void ror_i(void)
{
	byte_t count = get_u8_oprnd();
	__asm__ __volatile__("rorl %%cl, %0" 
				:"=m"(dprobes.rpn_stack[dprobes.rpn_tos])
				:"c"((unsigned long) count));
}

static void rol_i(void)
{
	byte_t count = get_u8_oprnd();
	__asm__ __volatile__("roll %%cl, %0" 
				:"=m"(dprobes.rpn_stack[dprobes.rpn_tos])
				:"c"((unsigned long) count));
}

static void ror(void)
{
	unsigned long oprnd = rpnpop();
	unsigned long count = rpnpop();
	__asm__ __volatile__("rorl %%cl, %0" : "=m"(oprnd)
				:"c"(count) );
	rpnpush(oprnd);
}

static void rol(void)
{
	unsigned long oprnd = rpnpop();
	unsigned long count = rpnpop();
	__asm__ __volatile__("roll %%cl, %0" : "=m"(oprnd)
				:"c"(count));
	rpnpush(oprnd);
}

/*
 * IO group
 */
static void push_io_u8(void)
{
	rpnpush(inb(rpnpop()));
}
static void push_io_u16(void)
{
        rpnpush(inw(rpnpop()));
}

static void push_io_u32(void)
{
        rpnpush(inl(rpnpop()));
}

static void pop_io_u8(void)
{
	u8 b = (u8)rpnpop();
	outb(b, rpnpop());
}

static void pop_io_u16(void)
{
        u16 w = (u16)rpnpop();
	outw(w, rpnpop());
}

static void pop_io_u32(void)
{
        u32 d = (u32)rpnpop();
	outl(d, rpnpop());
}

/*
 * Register push and pop, current and user registers.
 */

/*
 * y -- allowed and implemented, n -- not allowed and not implemented.
 *
 * Index	Register	push u	push r	pop u	pop r	
 *
 * 0		cs		y	y	n	n
 * 1	    	ds		y	y	y	y
 * 2	    	es		y	y	y	y
 * 3		fs		y	y	y	y
 * 4		gs		y	y	y	y			
 * 5		ss		y	y	n	n
 * 6		eax		y	y	y	y
 * 7		ebx		y	y	y	y
 * 8		ecx		y	y	y	y
 * 9		edx		y	y	y	y
 * a		edi		y	y	y	y
 * b		esi		y	y	y	y	
 * c		eflags		y	y	n	n
 * d		eip		y	y	n	n
 * e		esp		y	y	n	n
 * f		ebp		y	y	y	y
 * 20		tr		n	y	n	n
 * 21		ldtr		n	y	n	n
 * 22		gdtr		n	y	n	n
 * 23		idtr		n	y	n	n
 * 24		cr0		n	y	n	n
 * 25		cr1			RESERVED			
 * 26		cr2		n	y	n	n
 * 27		cr3		n	y	n	n
 * 28		cr4		n	y	n	n
 * 29		cr5			RESERVED
 * 2a		cr6			RESERVED
 * 2b		cr7			RESERVED
 * 2c		dr0		n	y	n	n
 * 2d		dr1		n	y	n	n
 * 2e		dr2		n	y	n	n
 * 2f		dr3		n	y	n	n
 * 30		dr4			RESERVED	
 * 31		dr5			RESERVED
 * 32		dr6		n	y	n	n
 * 33		dr7		n	y	n	n
 * 34		tr0			RESERVED
 * 35		tr1			RESERVED
 * 36		tr2			RESERVED
 * 37		tr3			RESERVED	
 * 38		tr4			RESERVED
 * 39		tr5			RESERVED
 * 3a		tr6			RESERVED
 * 3b		tr7			RESERVED
 * 3c		cpuid		y	y	n	n
 * 3d		msr		y	y	n	n
 * 3e		fr0		y 	n	n	n
 * 3f		fr1		y 	n	n	n
 * 40		fr2		y 	n	n	n
 * 41		fr3		y 	n	n	n
 * 42		fr4		y 	n	n	n
 * 43		fr5		y 	n	n	n
 * 44		fr6		y 	n	n	n
 * 45		fr7		y 	n	n	n
 * 46		fcw		y 	n	n	n
 * 47		fsw		y 	n	n	n
 * 48		ftw		y 	n	n	n
 * 49		fip		y 	n	n	n
 * 4a		fcs		y 	n	n	n
 * 4b		fdp		y 	n	n	n
 * 4c		fds		y 	n	n	n
 * 4d		xmm0		y 	n	n	n
 * 4e		xmm1		y 	n	n	n
 * 4f		xmm2		y 	n	n	n
 * 50		xmm3		y 	n	n	n
 * 51		xmm4		y 	n	n	n
 * 52		xmm5		y 	n	n	n
 * 53		xmm6		y 	n	n	n
 * 54		xmm7		y 	n	n	n
 * 55		mxcsr		y 	n	n	n
 */

static void dp_save_fpu(void)
{
	dprobes.status &= ~DP_STATUS_FIRSTFPU;
	if (HAVE_HWFP) {
		if (!current->used_math) {
			memset(&dprobes.fpu_save_area,0, sizeof(dprobes.fpu_save_area));
			return;
		}

		if ((read_cr0()) & CR0_TS) {
			dprobes.fpu_save_area = current->thread.i387;
		}
		else {
			if (cpu_has_fxsr) {
				__asm__ __volatile__ (
					"fxsave %0\n" 	
					"fxrstor %0" :"=m" (dprobes.fpu_save_area));
			} else {
				__asm__ __volatile__ (
					"fsave %0\n" 	
					"frstor %0" :"=m" (dprobes.fpu_save_area));
		
			}
 		}
	}
	else
		memset(&dprobes.fpu_save_area,0, sizeof(dprobes.fpu_save_area));

}

/*
 * Define Floating Point Control Registers' push functions.
 */

#define DEFINE_PUSHFP(REG) \
static void pushfp_##REG(void) \
{ \
	if (dprobes.status & DP_STATUS_FIRSTFPU) \
		dp_save_fpu(); \
	if (cpu_has_fxsr) \
		rpnpush(dprobes.fpu_save_area.fxsave.REG); \
	else \
		rpnpush(dprobes.fpu_save_area.fsave.REG); \
}

DEFINE_PUSHFP(cwd)
DEFINE_PUSHFP(swd)
DEFINE_PUSHFP(twd)
DEFINE_PUSHFP(fip)
DEFINE_PUSHFP(fcs)
DEFINE_PUSHFP(foo)
DEFINE_PUSHFP(fos)

static void pushfp_mxcsr(void)
{
	if (dprobes.status & DP_STATUS_FIRSTFPU)
		dp_save_fpu();
	if (cpu_has_xmm)
		rpnpush(dprobes.fpu_save_area.fxsave.mxcsr);
	else
		rpnpush(0); /* could we return 0x1f80 ? */
}

/*
 * Floating Point Data Registers' push function.
 */
static void pushfp_st(int offset)
{
	unsigned long st_0, st_4, st_8;
	if (dprobes.status & DP_STATUS_FIRSTFPU)
		dp_save_fpu();
	if (cpu_has_fxsr) {
		offset = (offset - 0x3e) * 16;
		st_0 = *(unsigned long *) ((unsigned char *)
			(dprobes.fpu_save_area.fxsave.st_space) + offset);
		st_4 = *(unsigned long *) ((unsigned char *)
			(dprobes.fpu_save_area.fxsave.st_space) + offset + 4);
		st_8 = *(unsigned long *) ((unsigned char *)
			(dprobes.fpu_save_area.fxsave.st_space) + offset + 8);
	}
	else {
		offset = (offset - 0x3e) * 10;
		st_0 = *(unsigned long *) ((unsigned char *)
			(dprobes.fpu_save_area.fsave.st_space) + offset);
		st_4 = *(unsigned long *) ((unsigned char *)
			(dprobes.fpu_save_area.fsave.st_space) + offset + 4);
		st_8 = *(unsigned short *) ((unsigned char *)
			(dprobes.fpu_save_area.fsave.st_space) + offset + 8);
	}
	rpnpush(st_8);
	rpnpush(st_4);
	rpnpush(st_0);
}

static void push_xmm(int offset)
{
	unsigned long xmm_0 = 0, xmm_4 = 0, xmm_8 = 0, xmm_12 = 0;
	if (dprobes.status & DP_STATUS_FIRSTFPU)
		dp_save_fpu();
	if (cpu_has_xmm) {
		offset = (offset - 0x4d) * 16;
		xmm_0 = *(unsigned long *) ((unsigned char *)
			(dprobes.fpu_save_area.fxsave.xmm_space) + offset);
		xmm_4 = *(unsigned long *) ((unsigned char *)
			(dprobes.fpu_save_area.fxsave.xmm_space) + offset + 4);
		xmm_8 = *(unsigned long *) ((unsigned char *)
			(dprobes.fpu_save_area.fxsave.xmm_space) + offset + 8);
		xmm_12 = *(unsigned long *) ((unsigned char *)
			(dprobes.fpu_save_area.fxsave.xmm_space) + offset + 12);
	}
	rpnpush(xmm_12);
	rpnpush(xmm_8);
	rpnpush(xmm_4);
	rpnpush(xmm_0);
}

/*
 * push r, ss.
 */
static unsigned long getr_ss(void)
{
	unsigned long reg;
	if (dprobes.status & DP_KERNEL_PROBE)
		__asm__ __volatile__("movw %%ss, %%ax\n\t"
				      :"=a"(reg)
				     );
	else 
		reg = dprobes.regs->xss;
	return reg;
}

/*
 * push r, esp
 */
static unsigned long getr_esp(void)
{
	if (dprobes.status & DP_KERNEL_PROBE)
		return (unsigned long)&dprobes.regs->esp;
	else 
		return dprobes.regs->esp;
}

/*
 * Push GDTR
 */
static void push_gdtr(void)
{
	struct Xgt_desc_struct gdtr;
	__asm__ __volatile__("sgdt (%0)" : : "r"((unsigned long) &gdtr));

	rpnpush(gdtr.address);
	rpnpush((unsigned long) gdtr.size);
}

/*
 * Push LDTR
 */
static void push_ldtr(void)
{
	unsigned long ldtr = 0; 
	__asm__ __volatile__("sldt %0" : "=r"(ldtr));
	rpnpush(ldtr);
}

/*
 * Push IDTR
 */
static void push_idtr(void)
{
	struct Xgt_desc_struct idtr;
	__asm__ __volatile__("sidt (%0)" : : "r"((unsigned long) &idtr));

	rpnpush(idtr.address);
	rpnpush((unsigned long)idtr.size);
}

/*
 * Push TR
 */
static void push_tr(void)
{
	unsigned long tr;
	__asm__ __volatile__("str %%ax" : "=a"(tr));
	rpnpush(tr);
}

/*
 * Push cpuid.
 */
static void push_cpuid(void)
{
	int op, cpuid_eax = 0, cpuid_ebx = 0, cpuid_ecx = 0, cpuid_edx = 0;
	op = (int) rpnpop();
	if ((boot_cpu_data.x86 >= 5) && (op <= boot_cpu_data.cpuid_level)) 
		cpuid(op, &cpuid_eax, &cpuid_ebx, &cpuid_ecx, &cpuid_edx);
	rpnpush(cpuid_edx);
	rpnpush(cpuid_ecx);
	rpnpush(cpuid_ebx);
	rpnpush(cpuid_eax);
}
	
/*
 * push msr. 
 */
static void push_msr(void)
{
	unsigned long msr, val1 =0, val2 = 0;
	msr = rpnpop();
	if (boot_cpu_data.x86 >= 5) 
		__asm__ __volatile__ (
			"0:	rdmsr\n"
			"1:\n"
			".section .fixup,\"ax\"\n"
			"2:	jmp 1b\n"
			".previous\n"
			".section __ex_table,\"a\"\n"
			"       .align 4\n"
			"       .long 0b,2b\n"
			".previous"
			:"=a"(val1), "=d"(val2)
			:"c"(msr));
	rpnpush(val2);
	rpnpush(val1);
}

/*	
 * Prototype for control registers'(CR0 TO CR4) and debug registers'(DR0 TO DR7)
 * push functions.
 */
#define DEFINE_PUSHCRDB(REG) \
static void push_##REG(void) \
{ \
	unsigned long reg; \
	__asm__ __volatile__ ("movl %%" #REG ", %0" : "=r"(reg)); \
	rpnpush(reg); \
}

/*
 * Define control and debug registers' push functions.
 */
DEFINE_PUSHCRDB(cr0)
DEFINE_PUSHCRDB(cr2)
DEFINE_PUSHCRDB(cr3)
DEFINE_PUSHCRDB(cr4)
DEFINE_PUSHCRDB(dr0)
DEFINE_PUSHCRDB(dr1)
DEFINE_PUSHCRDB(dr2)
DEFINE_PUSHCRDB(dr3)
DEFINE_PUSHCRDB(dr6)
DEFINE_PUSHCRDB(dr7)

/*
 * Since fs and gs are not in pt_regs, these
 * are directly taken from the processor.
 */
static unsigned long read_fs(void)
{
	unsigned long val;
	__asm__ __volatile__("movw %%fs, %%ax" :"=a"(val));
	return val;
}

static unsigned long read_gs(void)
{
	unsigned long val;
	__asm__ __volatile__("movw %%gs, %%ax" :"=a"(val));
	return val;
}

static void write_fs(unsigned long val)
{
	__asm__ __volatile__("movw %%ax, %%fs" : : "a"(val));
}

static void write_gs(unsigned long val)
{
	__asm__ __volatile__("movw %%ax, %%gs" : :"a"(val));
}

/*
 * Current register main push function.
 */
static void pushr(void)
{
	struct dprobes_struct *dp = &dprobes;
	unsigned short index = get_u16_oprnd();
	unsigned long val;
	switch(index) {
		case DP_CS: 	val = dp->regs->xcs; break;
		case DP_DS:	val = dp->regs->xds; break;
		case DP_ES:	val = dp->regs->xes; break;
		case DP_FS:	val = read_fs(); break;
		case DP_GS:	val = read_gs(); break;
		case DP_SS:	val = getr_ss(); break;
		case DP_EAX:	val = dp->regs->eax; break;
		case DP_EBX:	val = dp->regs->ebx; break;
		case DP_ECX:	val = dp->regs->ecx; break;
		case DP_EDX:	val = dp->regs->edx; break;
		case DP_EDI:	val = dp->regs->edi; break;
		case DP_ESI:	val = dp->regs->esi; break;
		case DP_EFLAGS:	val = dp->regs->eflags; break;
		case DP_EIP:	val = dp->regs->eip; break;
		case DP_ESP:	val = getr_esp(); break;
		case DP_EBP:	val = dp->regs->ebp; break;

		/* special registers */
		case DP_TR:	push_tr(); return;
		case DP_LDTR:	push_ldtr(); return;
		case DP_GDTR:	push_gdtr(); return;
		case DP_IDTR:	push_idtr(); return;
		case DP_CR0:	push_cr0(); return;
		case DP_CR2:	push_cr2(); return;
		case DP_CR3:	push_cr3(); return;
		case DP_CR4:	push_cr4(); return;
		case DP_DR0:	push_dr0(); return;
		case DP_DR1:	push_dr1(); return;
		case DP_DR2:	push_dr2(); return;
		case DP_DR3:	push_dr3(); return;
		case DP_DR6:	push_dr6(); return;
		case DP_DR7:	push_dr7(); return;
		case DP_CPUID:	push_cpuid(); return;
		case DP_MSR:	push_msr(); return;
		default:	val = 0; break;
	}
	rpnpush(val);
	return;
}

/*
 * User register main push function.
 */
static void pushu(void)
{
	struct dprobes_struct *dp = &dprobes;
	unsigned short index = get_u16_oprnd();
	unsigned long val;
	switch(index) {
		case DP_CS: 	val = dp->uregs->xcs; break;
		case DP_DS:	val = dp->uregs->xds; break;
		case DP_ES:	val = dp->uregs->xes; break;
		case DP_FS:	val = read_fs(); break;
		case DP_GS:	val = read_gs(); break;
		case DP_SS:	val = dp->uregs->xss; break;
		case DP_EAX:	val = dp->uregs->eax; break;
		case DP_EBX:	val = dp->uregs->ebx; break;
		case DP_ECX:	val = dp->uregs->ecx; break;
		case DP_EDX:	val = dp->uregs->edx; break;
		case DP_EDI:	val = dp->uregs->edi; break;
		case DP_ESI:	val = dp->uregs->esi; break;
		case DP_EFLAGS:	val = dp->uregs->eflags; break;
		case DP_EIP:	val = dp->uregs->eip; break;
		case DP_ESP:	val = dp->uregs->esp; break;
		case DP_EBP:	val = dp->uregs->ebp; break;
		case DP_CPUID:	push_cpuid(); return;
		case DP_MSR:	push_msr(); return;
		case DP_FR0:	pushfp_st(0x3e); return;
		case DP_FR1:	pushfp_st(0x3f); return;
		case DP_FR2:	pushfp_st(0x40); return;
		case DP_FR3:	pushfp_st(0x41); return;
		case DP_FR4:	pushfp_st(0x42); return;
		case DP_FR5:	pushfp_st(0x43); return;
		case DP_FR6:	pushfp_st(0x44); return;
		case DP_FR7:	pushfp_st(0x45); return;
		case DP_FCW:	pushfp_cwd(); return;
		case DP_FSW:	pushfp_swd(); return;
		case DP_FTW:	pushfp_twd(); return;
		case DP_FIP:	pushfp_fip(); return;
		case DP_FCS:	pushfp_fcs(); return;
		case DP_FDP:	pushfp_foo(); return;
		case DP_FDS:	pushfp_fos(); return;
		
		case DP_XMM0:	push_xmm(0x4d); return;
		case DP_XMM1:	push_xmm(0x4e); return;
		case DP_XMM2:	push_xmm(0x4f); return;
		case DP_XMM3:	push_xmm(0x50); return;
		case DP_XMM4:	push_xmm(0x51); return;
		case DP_XMM5:	push_xmm(0x52); return;
		case DP_XMM6:	push_xmm(0x53); return;
		case DP_XMM7:	push_xmm(0x54); return;
		case DP_MXCSR:	pushfp_mxcsr(); return;

		default: 	val = 0; break;
	}
	rpnpush(val);
	return;
}

/*
 * Current register main pop function.
 */
static void popr(void)
{
	struct dprobes_struct *dp = &dprobes;
	unsigned short index = get_u16_oprnd();
	unsigned long val = rpnpop();
	switch(index) {
		case DP_DS:	dp->regs->xds = val; break;
		case DP_ES:	dp->regs->xes = val; break;
		case DP_FS:	write_fs(val); break;
		case DP_GS:	write_gs(val); break;
		case DP_EAX:	dp->regs->eax = val; break;
		case DP_EBX:	dp->regs->ebx = val; break;
		case DP_ECX:	dp->regs->ecx = val; break;
		case DP_EDX:	dp->regs->edx = val; break;
		case DP_EDI:	dp->regs->edi = val; break;
		case DP_ESI:	dp->regs->esi = val; break;
		case DP_EBP:	dp->regs->ebp = val; break;
		default:	break;
	}
}

/*
 * User register main pop function.
 */
static void popu(void)
{
	struct dprobes_struct *dp = &dprobes;
	unsigned short index = get_u16_oprnd();
	unsigned long val = rpnpop();
	switch(index) {
		case DP_DS:	dp->uregs->xds = val; break;
		case DP_ES:	dp->uregs->xes = val; break;
		case DP_FS:	write_fs(val); break;
		case DP_GS:	write_gs(val); break;
		case DP_EAX:	dp->uregs->eax = val; break;
		case DP_EBX:	dp->uregs->ebx = val; break;
		case DP_ECX:	dp->uregs->ecx = val; break;
		case DP_EDX:	dp->uregs->edx = val; break;
		case DP_EDI:	dp->uregs->edi = val; break;
		case DP_ESI:	dp->uregs->esi = val; break;
		case DP_EBP:	dp->uregs->ebp = val; break;
		default:	break;
	}
}

/*
 * Entry point for the dprobes interpreter(Probe handler).
 */
static void dp_asm_interpreter(byte_t rpn_instr)
{
	struct dprobes_struct *dp = &dprobes;

		switch(rpn_instr) {

			case DP_SEG2LIN:	seg2lin(); break;		

			case DP_PUSH_IO_U8:	push_io_u8(); break;		
			case DP_PUSH_IO_U16:	push_io_u16(); break;		
			case DP_PUSH_IO_U32:	push_io_u32(); break;		
			case DP_POP_IO_U8:	pop_io_u8(); break;		
			case DP_POP_IO_U16:	pop_io_u16(); break;
			case DP_POP_IO_U32:	pop_io_u32(); break;

			default: gen_ex(EX_INVALID_OPCODE, *(dp->rpn_ip - 1),
			(unsigned long)(dp->rpn_ip -dp->rpn_code - 1)); break;		}
	return;
}
