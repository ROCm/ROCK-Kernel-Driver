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
#ifdef CONFIG_DUMP
#include <linux/dump.h>
#endif
#ifdef CONFIG_KDB
#include <linux/kdb.h>
#endif
#include <linux/binfmts.h>
#include <asm/ptrace.h>

#include <asm/processor.h>
void ia64_getreg_unknown_kr(void) { }
void ia64_setreg_unknown_kr(void) { }
#ifdef CONFIG_KDB
/* 
 * Temporarily restore the orignal opcode befor calling the kdb 
 * so that kdb shows correct disassembly.
 */


static inline void restore_opcode(void)
{
	if (dprobes.rec->point.probe & DP_PROBE_BREAKPOINT) {
		dprobes.reset_addr = dprobes.probe_addr;
		remove_bp_instr((byte_t *)dprobes.reset_addr, dprobes.rec);
		flush_icache_range(dprobes.reset_addr, dprobes.reset_addr+16);
	}
}

/*
 * After kdb terminates, restore the breakpoint.
 */
static inline void restore_int3(void)
{	
	if (dprobes.rec->point.probe & DP_PROBE_BREAKPOINT) {
		dprobes.reset_addr = dprobes.probe_addr;
		insert_bp_instr((byte_t *)dprobes.reset_addr, dprobes.rec);
		flush_icache_range(dprobes.reset_addr, dprobes.reset_addr+16);
	}
}
	
void call_kdb(void)
{
	unsigned long cr_iip = dprobes.regs->cr_iip - 1;
	if (dprobes.status & DP_KERNEL_PROBE) {
		if (dprobes.rec->point.probe & DP_PROBE_BREAKPOINT)
			dprobes.regs->cr_iip--;
		restore_opcode();
		kdb(KDB_REASON_CALL, 0, dprobes.regs);
		restore_int3();
		if (cr_iip == dprobes.regs->cr_iip)
			dprobes.regs->cr_iip++;
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
	int retval;
 
	retval = __copy_from_user(to, from, len);
	return retval;
}

static int dp_intr_copy_to_user(void *to, void *from, int len)
{
	int retval;
 
	retval = __copy_to_user(to, from, len);
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
	unsigned long val = dprobes.rpn_stack[dprobes.rpn_tos];
 
	val = (val >> count) | (val << (64 - count));
	dprobes.rpn_stack[dprobes.rpn_tos] = val;
}

static void rol_i(void)
{
	byte_t count = get_u8_oprnd();
	unsigned long val = dprobes.rpn_stack[dprobes.rpn_tos];
 
	val = (val << count) | (val >> (64 - count));
	dprobes.rpn_stack[dprobes.rpn_tos] = val;
}

static void ror(void)
{
	unsigned long val = rpnpop();
	unsigned long count = rpnpop();
 
	val = (val >> count) | (val << (64 - count));
	rpnpush(val);
}

static void rol(void)
{
	unsigned long val = rpnpop();
	unsigned long count = rpnpop();
 
	val = (val << count) | (val >> (64 - count));
	rpnpush(val);
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
 * y -- allowed and implemented 
 * n -- not allowed and not implemented.
 *
 *	Register		push u	push r	pop u	pop r	
 *	---------------		------	------	-----	-----
 *	psr			y	y	n	n
 *	psr.l			y	y	n	n
 *	psr.um			y	y	n	n
 *	cr0, cr.dcr		n	y	n	n
 *	cr1, cr.itm		n	y	n	n
 *	cr2, cr.iva		n	y	n	n
 *	cr3-cr7				RESERVED	
 *	cr8, cr.pta		n	y	n	n
 *	cr9-cr15			RESERVED	
 *	cr16, cr.ipsr		n	y	n	n
 *	cr17, cr.isr		n	n	n	n
 *	cr18				RESERVED	
 *	cr19, cr.iip		n	y	n	n
 *	cr20, cr.ifa		n	n	n	n
 *	cr21, cr.itir		n	n	n	n
 *	cr22, cr.iipa		n	n	n	n
 *	cr23, cr.ifs		n	y	n	n
 *	cr24, cr.iim		n	n	n	n
 *	cr25, cr.iha		n	n	n	n
 *	cr26-cr63			RESERVED	
 *	cr64, cr.lid		n	y	n	n
 *	cr65, cr.ivr		n	y	n	n
 *	cr66, cr.tpr		n	y	n	n
 *	cr67, cr.eoi		n	y	n	n
 *	cr68, cr.irr0		n	y	n	n
 *	cr69, cr.irr1		n	y	n	n
 *	cr70, cr.irr2		n	y	n	n
 *	cr71, cr.irr3		n	y	n	n
 *	cr72, cr.itv		n	y	n	n
 *	cr73, cr.pmv		n	n	n	n
 *	cr74, cr.cmcv		n	y	n	n
 *	cr75-cr79			RESERVED	
 *	cr80, cr.lrr0		n	y	n	n
 *	cr81, cr.lrr1		n	y	n	n
 *	dbr0-dbr7		n	y	n	n
 *	ibr0-ibr7		n	y	n	n
 *	pmc0-pmc31		n	n	n	n
 *	rr0-rr7			n	y	n	n
 *	pkr0-pkr17		n	n	n	n
 *	itr0-itr7		n	n	n	n
 *	dtr0-dtr7 		n	n	n	n
 *	itc, dtc		n	n	n	n
 *	r0			y	y	n	n
 *	r1, gp			y	y	y	y
 *	r2-r7			y	y	y	y
 *	r8, ret0		y	y	y	y
 *	r9, ret1		y	y	y	y
 *	r10, ret2		y	y	y	y
 *	r11, ret3		y	y	y	y
 *	r12, sp			y	y	n	n
 *	r13-r31			y	y	y	y
 *	r32-r127 		y	y	y	y
 *	f0-f7			y	y	n	n
 *	f8, farg0, fret0	y	y	n	n
 *	f9, farg1, fret1	y	y	n	n
 *	f10, farg2, fret2	y	y	n	n
 *	f11, farg3, fret3	y	y	n	n
 *	f12, farg4, fret4	y	y	n	n
 *	f13, farg5, fret5	y	y	n	n
 *	f14, farg6, fret6	y	y	n	n
 *	f15, farg7, fret7	y	y	n	n
 *	f16-f31			y	y	n	n
 *	f32-f127		y	y	n	n
 *	pr			y	y	n	n
 *	pr.rot			y	y	n	n
 *	p0-p63			y	y	n	n
 *	b0, rp			y	y	n	n
 *	b1-b7			y	y	n	n
 *	ip			y	y	n	n
 *	ar0, ar.k0		y	y	n	n
 *	ar1, ar.k1		y	y	n	n
 *	ar2, ar.k2		y	y	n	n
 *	ar3, ar.k3		y	y	n	n
 *	ar4, ar.k4		y	y	n	n
 *	ar5, ar.k5		y	y	n	n
 *	ar6, ar.k6		y	y	n	n
 *	ar7, ar.k7		y	y	n	n
 *	ar8-ar15			RESERVED
 *	ar16, ar.rsc		y	y	n	n
 *	ar17, ar.bsp		y	y	n	n
 *	ar18, ar.bspstore	y	y	n	n
 *	ar19, ar.rnat		y	y	n	n
 *	ar20				RESERVED
 *	ar21				IA-32
 *	ar22-ar23			RESERVED
 *	ar24-ar30			IA-32
 *	ar31				RESERVED
 *	ar32, ar.ccv		y	y	n	n
 *	ar33-ar35			RESERVED
 *	ar36, ar.unat		y	y	n	n
 *	ar37-ar39			RESERVED
 *	ar40, ar.fpsr		y	y	n	n
 *	ar41-ar43			RESERVED
 *	ar44, ar.itc		n	n	n	n
 *	ar45-ar47			RESERVED
 *	ar48-ar63			IGNORED
 *	ar64, ar.pfs		y	y	n	n
 *	ar65, ar.lc		y	y	n	n
 *	ar66, ar.ec		n	n	n	n
 *	ar67-ar111			RESERVED
 *	ar112-ar127			RESERVED
 *	pmd0-pmd31		n	n	n	n
 *	cpuid0-cpuid4		n	y	n	n
 */

/*
 * Current and user register push function.
 */
static void push(struct pt_regs *regs, struct switch_stack *sw)
{
	unsigned short regnum;
	unsigned short index = get_u16_oprnd();
	unsigned long val = 0; 
	struct ia64_fpreg fpval;

	fpval.u.bits[0] = 0;
	fpval.u.bits[1] = 0;

	switch(index & 0xff00) {
		case DP_PSR:
			val = regs->cr_ipsr;
			switch(index) {
				case DP_PSR_L:
					/* psr.l, Processor Status Register, lower 32 bits */
					val = val & 0xffffffff;
					break;
				case DP_PSR_UM:	
					/* psr.um, Processor Status Register, user mask */
					val = val & 0x3f;
					break;
				default:	
					/* psr, Processor Status Register */
					break;
			}
			rpnpush(val);
			break;
		case DP_CR:
			switch(index) {
				case DP_CR0:	
					/* cr0, cr.dcr, Default Control Register */
					__asm__ __volatile__ ("mov %0=cr.dcr" : "=r"(val) :: "memory");
					break;
				case DP_CR1:	
					/* cr1, cr.itm, Interval Time Counter and Match Register */
					__asm__ __volatile__ ("mov %0=cr.itm" : "=r"(val) :: "memory");
					break;
				case DP_CR2:	
					/* cr2, cr.iva, Interruption Vector Address */
 					__asm__ __volatile__ ("mov %0=cr.iva" : "=r"(val) :: "memory");
					break;
				case DP_CR8:
					/* cr8, cr.pta, Page Table Address */
 					__asm__ __volatile__ ("mov %0=cr.pta" : "=r"(val) :: "memory");
					break;
				case DP_CR16:
					/* cr16, cr.ipsr, Interruption Processor Status Register */
					val = regs->cr_ipsr;
					break;
				case DP_CR17:
					/* cr17, cr.isr, Interruption Status Register */
					/* Shouldn't get here, unimplemented since reading or
					 *   writing interruption control regs when PSR.ic is 
					 *   set to one causes an illegal operation fault.
					 */
					break;
				case DP_CR19:
					/* cr19, cr.iip, Interruption Instruction Bundle Pointer */
					val = regs->cr_iip;
					break;
				case DP_CR20:
					/* cr20, cr.ifa, Interruption Fault Address */
					/* fall thru */
				case DP_CR21:
					/* cr21, cr.itir, Interruption TLB Insertion Register */
					/* fall thru */
				case DP_CR22:
					/* cr22, cr.iipa, Interruption Instruction Previous Address */
					/* Shouldn't get here, unimplemented since reading or
					 *   writing interruption control regs when PSR.ic is 
					 *   set to one causes an illegal operation fault.
					 */
					break;
				case DP_CR23:
					/* cr23, cr.ifs, Interruption Function State */
					val = regs->cr_ifs;
					break;
				case DP_CR24:
					/* cr24, cr.iim, Interruption Immediate */
					/* fall thru */
				case DP_CR25:
					/* cr25, cr.iha, Interruption Hash Address */
					/* Shouldn't get here, unimplemented since reading or
					 *   writing interruption control regs when PSR.ic is 
					 *   set to one causes an illegal operation fault.
					 */
					break;
				case DP_CR64:
					/* cr64, cr.lid, Local ID */
					__asm__ __volatile__ ("mov %0=cr.lid" : "=r"(val) :: "memory");
					break;
				case DP_CR65:
					/* cr65, cr.ivr, External Interrupt Vector Register */
					val = ia64_get_ivr();
					break;
				case DP_CR66:
					/* cr66, cr.tpr, Task Priority Register */
					__asm__ __volatile__ ("mov %0=cr.tpr" : "=r"(val) :: "memory");
					break;
				case DP_CR67:
					/* cr67, cr.eoi, End Of External Interrupt */
					/* cr.eoi reads always return 0 */
					val = 0;
					break;
				case DP_CR68:
					/* cr68, cr.irr0, External Interrupt Request Register 0 */
					__asm__ __volatile__ ("mov %0=cr.irr0" : "=r"(val) :: "memory");
					break;
				case DP_CR69:
					/* cr69, cr.irr1, External Interrupt Request Register 1 */
					__asm__ __volatile__ ("mov %0=cr.irr1" : "=r"(val) :: "memory");
					break;
				case DP_CR70:
					/* cr70, cr.irr2, External Interrupt Request Register 2 */
					__asm__ __volatile__ ("mov %0=cr.irr2" : "=r"(val) :: "memory");
					break;
				case DP_CR71:
					/* cr71, cr.irr3, External Interrupt Request Register 3 */
					__asm__ __volatile__ ("mov %0=cr.irr3" : "=r"(val) :: "memory");
					break;
				case DP_CR72:
					/* cr72, cr.itv, Interval Timer Vector */
 					__asm__ __volatile__ ("mov %0=cr.itv" : "=r"(val) :: "memory");
					break;
				case DP_CR73:
					/* cr73, cr.pmv, Performance Monitoring Vector */
					/* Shouldn't get here, unimplemented */
					break;
				case DP_CR74:
					/* cr74, cr.cmcv, Corrected Machine Check Vector */
 					__asm__ __volatile__ ("mov %0=cr.cmcv" : "=r"(val) :: "memory");
					break;
				case DP_CR80:
					/* cr80, cr.lrr0, Local Redirection Register 0 */
 					__asm__ __volatile__ ("mov %0=cr.lrr0" : "=r"(val) :: "memory");
					break;
				case DP_CR81:
					/* cr81, cr.lrr1, Local Redirection Register 1 */
 					__asm__ __volatile__ ("mov %0=cr.lrr1" : "=r"(val) :: "memory");
					break;
				default:
					/* Shouldn't get here */
					break;
			}
			rpnpush(val);
			break;
		case DP_DBR:
			/* dbr0-dbr7, Data Breakpoint Registers */
			ia64_save_debug_regs(&current->thread.dbr[0]);
			regnum = index & 0x00ff;
			val = current->thread.dbr[regnum];
			rpnpush(val);
			break;
		case DP_IBR:
			/* ibr0-ibr7, Instruction Breakpoint Registers */
			ia64_save_debug_regs(&current->thread.dbr[0]);
			regnum = index & 0x00ff;
			val = current->thread.ibr[regnum];
			rpnpush(val);
			break;
		case DP_PMC:
			/* pmc0-pmc31, Performance Monitor Configuration Registers */
			/* Shouldn't get here, unimplemented */
			rpnpush(val);
			break;
		case DP_RR:
			/* rr0-rr7, Region Registers */
			regnum = index & 0x00ff;
			val = ia64_get_rr((unsigned long)regnum << 61);
			rpnpush(val);
			break;
		case DP_PKR:
			/* pkr0-pkr17, Protection Key Registers */
			/* Shouldn't get here, the pkr registers are not
			 * used by Linux IA-64 kernel. 
			 */
			rpnpush(val);
			break;
		case DP_ITR:
			/* itr0-itr7, Instruction TLB Registers */
			/* Shouldn't get here, there is no instruction
			 *   available to read itr registers.
			 */
			rpnpush(val);
			break;
		case DP_DTR:
			/* dtr0-dtr7, Data TLB Registers */
			/* Shouldn't get here, there is no instruction
			 *   available to read the dtr registers.
			 */
			rpnpush(val);
			break;
		case DP_ITC:
			/* itc, Instruction TLB Register */
			/* Shouldn't get here, there is no instruction
			 *   available to read the itc register.
			 */
			rpnpush(val);
			break;
		case DP_DTC:
			/* dtc, Data TLB Register */
			/* Shouldn't get here, there is no instruction
			 *   available to read the dtc register.
			 */
			rpnpush(val);
			break;
		case DP_GR_STATIC:  /* r0-r31, Static General Registers */
		case DP_GR_STACKED: /* r32-r127, Stacked General Registers */
			regnum = index & 0x00ff; 
			val = kp_get_gr(regnum, regs, sw);
			rpnpush(val);
			break;
		case DP_FR_STATIC:
			/* f0-f31, Static Floating-point Registers */
			switch(index) {
				case DP_FR0:	/* f0, always reads +0.0 */
					__asm__ __volatile__ ("stf.spill [%0]=f0" :: "r"(&fpval) : "memory");
					break;
				case DP_FR1:	/* f1, always reads +1.0 */
					__asm__ __volatile__ ("stf.spill [%0]=f1" :: "r"(&fpval) : "memory");
					break;
				case DP_FR2:	/* f2 */
					fpval = sw->f2;
					break;
				case DP_FR3:	/* f3 */
					fpval = sw->f3;
					break;
				case DP_FR4:	/* f4 */
					fpval = sw->f4;
					break;
				case DP_FR5:	/* f5 */
					fpval = sw->f5;
					break;
				case DP_FR6:	/* f6 */
					fpval = regs->f6;
					break;
				case DP_FR7:	/* f7 */
					fpval = regs->f7;
					break;
				case DP_FR8:	/* f8, farg0, fret0 */
					fpval = regs->f8;
					break;
				case DP_FR9:	/* f9, farg1, fret1 */
					fpval = regs->f9;
					break;
				case DP_FR10:	/* f10, farg2, fret2 */
					fpval = regs->f10;
					break;
				case DP_FR11:	/* f11, farg3, fret3 */
					fpval = regs->f11;
					break;
				case DP_FR12:	/* f12, farg4, fret4 */
					fpval = sw->f12;
					break;
				case DP_FR13:	/* f13, farg5, fret5 */
					fpval = sw->f13;
					break;
				case DP_FR14:	/* f14, farg6, fret6 */
					fpval = sw->f14;
					break;
				case DP_FR15:	/* f15, farg7, fret7 */
					fpval = sw->f15;
					break;
				case DP_FR16:	/* f16 */
					fpval = sw->f16;
					break;
				case DP_FR17:	/* f17 */
					fpval = sw->f17;
					break;
				case DP_FR18:	/* f18 */
					fpval = sw->f18;
					break;
				case DP_FR19:	/* f19 */
					fpval = sw->f19;
					break;
				case DP_FR20:	/* f20 */
					fpval = sw->f20;
					break;
				case DP_FR21:	/* f21 */
					fpval = sw->f21;
					break;
				case DP_FR22:	/* f22 */
					fpval = sw->f22;
					break;
				case DP_FR23:	/* f23 */
					fpval = sw->f23;
					break;
				case DP_FR24:	/* f24 */
					fpval = sw->f24;
					break;
				case DP_FR25:	/* f25 */
					fpval = sw->f25;
					break;
				case DP_FR26:	/* f26 */
					fpval = sw->f26;
					break;
				case DP_FR27:	/* f27 */
					fpval = sw->f27;
					break;
				case DP_FR28:	/* f28 */
					fpval = sw->f28;
					break;
				case DP_FR29:	/* f29 */
					fpval = sw->f29;
					break;
				case DP_FR30:	/* f30 */
					fpval = sw->f30;
					break;
				case DP_FR31:	/* f31 */
					fpval = sw->f31;
					break;
				default:
					/* Shouldn't get here */
					break;
			}
			rpnpush(fpval.u.bits[0]);
			rpnpush(fpval.u.bits[1]);
			break;
		case DP_FR_ROTATING:
			/* f32-f127, Rotating Floating-point Registers */
			regnum = index & 0x00ff;
			ia64_flush_fph(current);
			fpval = current->thread.fph[regnum - 32]; 
			rpnpush(fpval.u.bits[0]);
			rpnpush(fpval.u.bits[1]);
			break;
		case DP_PR:		
			/* p0-p63, Predicate Registers */
			val = regs->pr; 
			switch(index) {
				case DP_PR:	/* pr */
					break;
				case DP_PR0:	/* p0 */
					/* always reads as 1 */
					val = 1;
					break;
				case DP_PR_ROT:	/* pr.rot */
					val = val >> 16;
					val = val & 0xffffff;
					break;
				default:	/* p1-p63 */
					val = val >> ((index & 0x00ff) - 1);
					val = val & 0x01;
					break;
			}
			rpnpush(val);
			break;
		case DP_BR:            
			/* b0-b7, Branch Registers */
			switch(index) {
				case DP_BR0:	/* b0 */
					val = regs->b0;
					break;
				case DP_BR1:	/* b1, rp */
					val = sw->b1;
					break;
				case DP_BR2:	/* b2 */
					val = sw->b2;
					break;
				case DP_BR3:	/* b3 */
					val = sw->b3;
					break;
				case DP_BR4:	/* b4 */
					val = sw->b4;
					break;
				case DP_BR5:	/* b5 */
					val = sw->b5;
					break;
				case DP_BR6:	/* b6 */
					val = regs->b6;
					break;
				case DP_BR7:	/* b7 */
					val = regs->b7;
					break;
			}
			rpnpush(val);
			break;
		case DP_IP:	/* ip, Instruction Pointer */
			val = regs->cr_iip;
			rpnpush(val);
			break;
		case DP_AR:
			/* ar0-ar7,ar16-ar19,ar32,ar36,ar40,ar44,ar64-ar66 - Application Registers */
			switch(index) {
				case DP_AR0:	/* ar0, ar.k0, Kernel Register */
				case DP_AR1:	/* ar1, ar.k1, Kernel Register */
				case DP_AR2:	/* ar2, ar.k2, Kernel Register */
				case DP_AR3:	/* ar3, ar.k3, Kernel Register */
				case DP_AR4:	/* ar4, ar.k4, Kernel Register */
				case DP_AR5:	/* ar5, ar.k5, Kernel Register */
				case DP_AR6:	/* ar6, ar.k6, Kernel Register */
				case DP_AR7:	/* ar7, ar.k7, Kernel Register */
					regnum = index & 0x00ff;
					val = ia64_get_kr(regnum);
					break;
				case DP_AR16:	
					/* ar16, ar.rsc, Register Stack Configuration */
					val = regs->ar_rsc;
					break;
				case DP_AR17:	
					/* ar17, ar.bsp, RSE Backing Store Pointer */
					val = regs->ar_bspstore + (regs->loadrs >> 16);
					break;
				case DP_AR18:	
					/* ar18, ar.bspstore, RSE Backing Store Pointer for Memory Stores */
					val = regs->ar_bspstore;
					break;
				case DP_AR19:	
					/* ar19, ar.rnat, RSE NaT Collection */
					val = regs->ar_rnat;
					break;
				case DP_AR32:	
					/* ar32, ar.ccv, Compare and Exchange Value Register */
					val = regs->ar_ccv;
					break;
				case DP_AR36:	
					/* ar36, ar.unat, User NaT Collection Register */
					val = regs->ar_unat;
					break;
				case DP_AR40:	
					/* ar40, ar.fpsr, Floating-point Status Register */
					val = regs->ar_fpsr;
					break;
				case DP_AR44:	
					/* ar44, ar.itc, Interval Time Counter */
					/* Shouldn't get here, unimplemented. 
					 * The ar.itc contents are not saved in pt_regs struct 
					 * when the break instruction is hit.  Sampling ar.itc
					 * value right here with an asm instruction is probably 
					 * not useful.
					 */
					val = 0;
					break;
				case DP_AR64:	
					/* ar64, ar.pfs, Previous Function State */
					val = regs->ar_pfs;
					break;
				case DP_AR65:	
					/* ar65, ar.lc, Loop Count Register */
					val = sw->ar_lc;
					break;
				case DP_AR66:	
					/* ar66, ar.ec, Epilog Count Register */
					/* Shouldn't get here, unimplemented */
					break;
				default:
					/* Shouldn't get here */
					break;
			}
			rpnpush(val);
			break;
		case DP_PMD:	
			/* pmd0-pmd31, Performance Monitor Data Registers */
			/* Shouldn't get here, unimplemented */
			rpnpush(val);
			break;
		case DP_CPUID:	
			/* cpuid0-cpuid4, Processor Identification Registers */
			regnum = index & 0x00ff;
			val = ia64_get_cpuid(regnum);
			rpnpush(val);
			break;
		default:		
			/* Shouldn't get here */
			rpnpush(val); 
			break;
	} 
	return;
}

/*
 * Current register main push function.
 */
static void pushr(void)
{
	struct dprobes_struct *dp = &dprobes;

	push(dp->regs, dp->sw);
	return;
}

/*
 * User register main push function.
 */
static void pushu(void)
{
	struct dprobes_struct *dp = &dprobes;

	push(dp->uregs, dp->sw);
	return;
}

/*
 * Current and user register pop function.
 */
static void pop(struct pt_regs *regs, struct switch_stack *sw)
{
	unsigned short regnum;
	unsigned short index = get_u16_oprnd();
	unsigned long val = rpnpop();

	switch(index & 0xff00) {
		case DP_GR_STATIC:  /* r0-r31, Static General Registers */
		case DP_GR_STACKED: /* r32-r127, Stacked General Registers */
			regnum = index & 0x00ff; 
			kp_set_gr(regnum, val, regs, sw);
			break;
		default:		
			/* Shouldn't get here */
			break;
	} 
	return;
}

/*
 * Current register main pop function.
 */
static void popr(void)
{
	struct dprobes_struct *dp = &dprobes;

	pop(dp->regs, dp->sw);
	return;
}

/*
 * User register main pop function.
 */
static void popu(void)
{
	struct dprobes_struct *dp = &dprobes;

	pop(dp->uregs, dp->sw);
	return;
}

/*
 * Entry point for the dprobes interpreter(Probe handler).
 */
static void dp_asm_interpreter(byte_t rpn_instr)
{
	struct dprobes_struct *dp = &dprobes;

		switch(rpn_instr) {

			case DP_PUSH_IO_U8:	push_io_u8(); break;		
			case DP_PUSH_IO_U16:	push_io_u16(); break;		
			case DP_PUSH_IO_U32:	push_io_u32(); break;		
			case DP_POP_IO_U8:	pop_io_u8(); break;		
			case DP_POP_IO_U16:	pop_io_u16(); break;
			case DP_POP_IO_U32:	pop_io_u32(); break;

			default: gen_ex(EX_INVALID_OPCODE, *(dp->rpn_ip - 1),
			(unsigned long)(dp->rpn_ip - dp->rpn_code - 1)); break;		}
	return;
}

